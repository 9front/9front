/*
 /  Author: Sam Rushing <rushing@nightmare.com>
 /  Hacked for Unix by AMK
 /  $Id: mmapmodule.c 51474 2006-08-22 13:57:07Z neal.norwitz $

 / mmapmodule.cpp -- map a view of a file into memory
 /
 / todo: need permission flags, perhaps a 'chsize' analog
 /   not all functions check range yet!!!
 /
 /
 / This version of mmapmodule.c has been changed significantly
 / from the original mmapfile.c on which it was based.
 / The original version of mmapfile is maintained by Sam at
 / ftp://squirl.nightmare.com/pub/python/python-ext.
*/

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#ifndef MS_WINDOWS
#define UNIX
#endif

#ifdef MS_WINDOWS
#include <windows.h>
static int
my_getpagesize(void)
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return si.dwPageSize;
}
#endif

#ifdef UNIX
#include <sys/mman.h>
#include <sys/stat.h>

#if defined(HAVE_SYSCONF) && defined(_SC_PAGESIZE)
static int
my_getpagesize(void)
{
	return sysconf(_SC_PAGESIZE);
}
#else
#define my_getpagesize getpagesize
#endif

#endif /* UNIX */

#include <string.h>

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif /* HAVE_SYS_TYPES_H */

/* Prefer MAP_ANONYMOUS since MAP_ANON is deprecated according to man page. */
#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#  define MAP_ANONYMOUS MAP_ANON
#endif

static PyObject *mmap_module_error;

typedef enum
{
	ACCESS_DEFAULT,
	ACCESS_READ,
	ACCESS_WRITE,
	ACCESS_COPY
} access_mode;

typedef struct {
	PyObject_HEAD
	char *	data;
	size_t	size;
	size_t	pos;

#ifdef MS_WINDOWS
	HANDLE	map_handle;
	HANDLE	file_handle;
	char *	tagname;
#endif

#ifdef UNIX
        int fd;
#endif

        access_mode access;
} mmap_object;


static void
mmap_object_dealloc(mmap_object *m_obj)
{
#ifdef MS_WINDOWS
	if (m_obj->data != NULL)
		UnmapViewOfFile (m_obj->data);
	if (m_obj->map_handle != INVALID_HANDLE_VALUE)
		CloseHandle (m_obj->map_handle);
	if (m_obj->file_handle != INVALID_HANDLE_VALUE)
		CloseHandle (m_obj->file_handle);
	if (m_obj->tagname)
		PyMem_Free(m_obj->tagname);
#endif /* MS_WINDOWS */

#ifdef UNIX
	if (m_obj->fd >= 0)
		(void) close(m_obj->fd);
	if (m_obj->data!=NULL) {
		msync(m_obj->data, m_obj->size, MS_SYNC);
		munmap(m_obj->data, m_obj->size);
	}
#endif /* UNIX */

	PyObject_Del(m_obj);
}

static PyObject *
mmap_close_method(mmap_object *self, PyObject *unused)
{
#ifdef MS_WINDOWS
	/* For each resource we maintain, we need to check
	   the value is valid, and if so, free the resource
	   and set the member value to an invalid value so
	   the dealloc does not attempt to resource clearing
	   again.
	   TODO - should we check for errors in the close operations???
	*/
	if (self->data != NULL) {
		UnmapViewOfFile(self->data);
		self->data = NULL;
	}
	if (self->map_handle != INVALID_HANDLE_VALUE) {
		CloseHandle(self->map_handle);
		self->map_handle = INVALID_HANDLE_VALUE;
	}
	if (self->file_handle != INVALID_HANDLE_VALUE) {
		CloseHandle(self->file_handle);
		self->file_handle = INVALID_HANDLE_VALUE;
	}
#endif /* MS_WINDOWS */

#ifdef UNIX
	(void) close(self->fd);
	self->fd = -1;
	if (self->data != NULL) {
		munmap(self->data, self->size);
		self->data = NULL;
	}
#endif

	Py_INCREF(Py_None);
	return Py_None;
}

#ifdef MS_WINDOWS
#define CHECK_VALID(err)						\
do {									\
    if (self->map_handle == INVALID_HANDLE_VALUE) {						\
	PyErr_SetString(PyExc_ValueError, "mmap closed or invalid");	\
	return err;							\
    }									\
} while (0)
#endif /* MS_WINDOWS */

#ifdef UNIX
#define CHECK_VALID(err)						\
do {									\
    if (self->data == NULL) {						\
	PyErr_SetString(PyExc_ValueError, "mmap closed or invalid");	\
	return err;							\
	}								\
} while (0)
#endif /* UNIX */

static PyObject *
mmap_read_byte_method(mmap_object *self,
		      PyObject *unused)
{
	CHECK_VALID(NULL);
	if (self->pos < self->size) {
	        char value = self->data[self->pos];
		self->pos += 1;
		return Py_BuildValue("c", value);
	} else {
		PyErr_SetString(PyExc_ValueError, "read byte out of range");
		return NULL;
	}
}

static PyObject *
mmap_read_line_method(mmap_object *self,
		      PyObject *unused)
{
	char *start = self->data+self->pos;
	char *eof = self->data+self->size;
	char *eol;
	PyObject *result;

	CHECK_VALID(NULL);

	eol = memchr(start, '\n', self->size - self->pos);
	if (!eol)
		eol = eof;
	else
		++eol;		/* we're interested in the position after the
				   newline. */
	result = PyString_FromStringAndSize(start, (eol - start));
	self->pos += (eol - start);
	return result;
}

static PyObject *
mmap_read_method(mmap_object *self,
		 PyObject *args)
{
	Py_ssize_t num_bytes;
	PyObject *result;

	CHECK_VALID(NULL);
	if (!PyArg_ParseTuple(args, "n:read", &num_bytes))
		return(NULL);

	/* silently 'adjust' out-of-range requests */
	if ((self->pos + num_bytes) > self->size) {
		num_bytes -= (self->pos+num_bytes) - self->size;
	}
	result = Py_BuildValue("s#", self->data+self->pos, num_bytes);
	self->pos += num_bytes;
	return result;
}

static PyObject *
mmap_find_method(mmap_object *self,
		 PyObject *args)
{
	Py_ssize_t start = self->pos;
	char *needle;
	Py_ssize_t len;

	CHECK_VALID(NULL);
	if (!PyArg_ParseTuple(args, "s#|n:find", &needle, &len, &start)) {
		return NULL;
	} else {
		char *p;
		char *e = self->data + self->size;

                if (start < 0)
			start += self->size;
                if (start < 0)
			start = 0;
                else if ((size_t)start > self->size)
			start = self->size;

		for (p = self->data + start; p + len <= e; ++p) {
			Py_ssize_t i;
			for (i = 0; i < len && needle[i] == p[i]; ++i)
				/* nothing */;
			if (i == len) {
				return PyInt_FromSsize_t(p - self->data);
			}
		}
		return PyInt_FromLong(-1);
	}
}

static int
is_writeable(mmap_object *self)
{
	if (self->access != ACCESS_READ)
		return 1;
	PyErr_Format(PyExc_TypeError, "mmap can't modify a readonly memory map.");
	return 0;
}

static int
is_resizeable(mmap_object *self)
{
	if ((self->access == ACCESS_WRITE) || (self->access == ACCESS_DEFAULT))
		return 1;
	PyErr_Format(PyExc_TypeError,
		     "mmap can't resize a readonly or copy-on-write memory map.");
	return 0;
}


static PyObject *
mmap_write_method(mmap_object *self,
		  PyObject *args)
{
	Py_ssize_t length;
	char *data;

	CHECK_VALID(NULL);
	if (!PyArg_ParseTuple(args, "s#:write", &data, &length))
		return(NULL);

	if (!is_writeable(self))
		return NULL;

	if ((self->pos + length) > self->size) {
		PyErr_SetString(PyExc_ValueError, "data out of range");
		return NULL;
	}
	memcpy(self->data+self->pos, data, length);
	self->pos = self->pos+length;
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *
mmap_write_byte_method(mmap_object *self,
		       PyObject *args)
{
	char value;

	CHECK_VALID(NULL);
	if (!PyArg_ParseTuple(args, "c:write_byte", &value))
		return(NULL);

	if (!is_writeable(self))
		return NULL;
	*(self->data+self->pos) = value;
	self->pos += 1;
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *
mmap_size_method(mmap_object *self,
		 PyObject *unused)
{
	CHECK_VALID(NULL);

#ifdef MS_WINDOWS
	if (self->file_handle != INVALID_HANDLE_VALUE) {
		DWORD low,high;
		PY_LONG_LONG size;
		low = GetFileSize(self->file_handle, &high);
		if (low == INVALID_FILE_SIZE) {
			/* It might be that the function appears to have failed,
			   when indeed its size equals INVALID_FILE_SIZE */
			DWORD error = GetLastError();
			if (error != NO_ERROR)
				return PyErr_SetFromWindowsErr(error);
		}
		if (!high && low < LONG_MAX)
			return PyInt_FromLong((long)low);
		size = (((PY_LONG_LONG)high)<<32) + low;
		return PyLong_FromLongLong(size);
	} else {
		return PyInt_FromSsize_t(self->size);
	}
#endif /* MS_WINDOWS */

#ifdef UNIX
	{
		struct stat buf;
		if (-1 == fstat(self->fd, &buf)) {
			PyErr_SetFromErrno(mmap_module_error);
			return NULL;
		}
		return PyInt_FromSsize_t(buf.st_size);
	}
#endif /* UNIX */
}

/* This assumes that you want the entire file mapped,
 / and when recreating the map will make the new file
 / have the new size
 /
 / Is this really necessary?  This could easily be done
 / from python by just closing and re-opening with the
 / new size?
 */

static PyObject *
mmap_resize_method(mmap_object *self,
		   PyObject *args)
{
	Py_ssize_t new_size;
	CHECK_VALID(NULL);
	if (!PyArg_ParseTuple(args, "n:resize", &new_size) ||
	    !is_resizeable(self)) {
		return NULL;
#ifdef MS_WINDOWS
	} else {
		DWORD dwErrCode = 0;
		DWORD newSizeLow, newSizeHigh;
		/* First, unmap the file view */
		UnmapViewOfFile(self->data);
		/* Close the mapping object */
		CloseHandle(self->map_handle);
		/* Move to the desired EOF position */
#if SIZEOF_SIZE_T > 4
		newSizeHigh = (DWORD)(new_size >> 32);
		newSizeLow = (DWORD)(new_size & 0xFFFFFFFF);
#else
		newSizeHigh = 0;
		newSizeLow = (DWORD)new_size;
#endif
		SetFilePointer(self->file_handle,
			       newSizeLow, &newSizeHigh, FILE_BEGIN);
		/* Change the size of the file */
		SetEndOfFile(self->file_handle);
		/* Create another mapping object and remap the file view */
		self->map_handle = CreateFileMapping(
			self->file_handle,
			NULL,
			PAGE_READWRITE,
			newSizeHigh,
			newSizeLow,
			self->tagname);
		if (self->map_handle != NULL) {
			self->data = (char *) MapViewOfFile(self->map_handle,
							    FILE_MAP_WRITE,
							    0,
							    0,
							    0);
			if (self->data != NULL) {
				self->size = new_size;
				Py_INCREF(Py_None);
				return Py_None;
			} else {
				dwErrCode = GetLastError();
			}
		} else {
			dwErrCode = GetLastError();
		}
		PyErr_SetFromWindowsErr(dwErrCode);
		return NULL;
#endif /* MS_WINDOWS */

#ifdef UNIX
#ifndef HAVE_MREMAP
	} else {
		PyErr_SetString(PyExc_SystemError,
				"mmap: resizing not available--no mremap()");
		return NULL;
#else
	} else {
		void *newmap;

		if (ftruncate(self->fd, new_size) == -1) {
			PyErr_SetFromErrno(mmap_module_error);
			return NULL;
		}

#ifdef MREMAP_MAYMOVE
		newmap = mremap(self->data, self->size, new_size, MREMAP_MAYMOVE);
#else
		newmap = mremap(self->data, self->size, new_size, 0);
#endif
		if (newmap == (void *)-1)
		{
			PyErr_SetFromErrno(mmap_module_error);
			return NULL;
		}
		self->data = newmap;
		self->size = new_size;
		Py_INCREF(Py_None);
		return Py_None;
#endif /* HAVE_MREMAP */
#endif /* UNIX */
	}
}

static PyObject *
mmap_tell_method(mmap_object *self, PyObject *unused)
{
	CHECK_VALID(NULL);
	return PyInt_FromSize_t(self->pos);
}

static PyObject *
mmap_flush_method(mmap_object *self, PyObject *args)
{
	Py_ssize_t offset = 0;
	Py_ssize_t size = self->size;
	CHECK_VALID(NULL);
	if (!PyArg_ParseTuple(args, "|nn:flush", &offset, &size))
		return NULL;
	if ((size_t)(offset + size) > self->size) {
		PyErr_SetString(PyExc_ValueError, "flush values out of range");
		return NULL;
	} else {
#ifdef MS_WINDOWS
		return PyInt_FromLong((long)
                                      FlushViewOfFile(self->data+offset, size));
#endif /* MS_WINDOWS */
#ifdef UNIX
		/* XXX semantics of return value? */
		/* XXX flags for msync? */
		if (-1 == msync(self->data + offset, size,
				MS_SYNC))
		{
			PyErr_SetFromErrno(mmap_module_error);
			return NULL;
		}
		return PyInt_FromLong(0);
#endif /* UNIX */
	}
}

static PyObject *
mmap_seek_method(mmap_object *self, PyObject *args)
{
	Py_ssize_t dist;
	int how=0;
	CHECK_VALID(NULL);
	if (!PyArg_ParseTuple(args, "n|i:seek", &dist, &how))
		return NULL;
	else {
		size_t where;
		switch (how) {
		case 0: /* relative to start */
			if (dist < 0)
				goto onoutofrange;
			where = dist;
			break;
		case 1: /* relative to current position */
			if ((Py_ssize_t)self->pos + dist < 0)
				goto onoutofrange;
			where = self->pos + dist;
			break;
		case 2: /* relative to end */
			if ((Py_ssize_t)self->size + dist < 0)
				goto onoutofrange;
			where = self->size + dist;
			break;
		default:
			PyErr_SetString(PyExc_ValueError, "unknown seek type");
			return NULL;
		}
		if (where > self->size)
			goto onoutofrange;
		self->pos = where;
		Py_INCREF(Py_None);
		return Py_None;
	}

  onoutofrange:
	PyErr_SetString(PyExc_ValueError, "seek out of range");
	return NULL;
}

static PyObject *
mmap_move_method(mmap_object *self, PyObject *args)
{
	unsigned long dest, src, count;
	CHECK_VALID(NULL);
	if (!PyArg_ParseTuple(args, "kkk:move", &dest, &src, &count) ||
	    !is_writeable(self)) {
		return NULL;
	} else {
		/* bounds check the values */
		if (/* end of source after end of data?? */
			((src+count) > self->size)
			/* dest will fit? */
			|| (dest+count > self->size)) {
			PyErr_SetString(PyExc_ValueError,
					"source or destination out of range");
			return NULL;
		} else {
			memmove(self->data+dest, self->data+src, count);
			Py_INCREF(Py_None);
			return Py_None;
		}
	}
}

static struct PyMethodDef mmap_object_methods[] = {
	{"close",	(PyCFunction) mmap_close_method,	METH_NOARGS},
	{"find",	(PyCFunction) mmap_find_method,		METH_VARARGS},
	{"flush",	(PyCFunction) mmap_flush_method,	METH_VARARGS},
	{"move",	(PyCFunction) mmap_move_method,		METH_VARARGS},
	{"read",	(PyCFunction) mmap_read_method,		METH_VARARGS},
	{"read_byte",	(PyCFunction) mmap_read_byte_method,  	METH_NOARGS},
	{"readline",	(PyCFunction) mmap_read_line_method,	METH_NOARGS},
	{"resize",	(PyCFunction) mmap_resize_method,	METH_VARARGS},
	{"seek",	(PyCFunction) mmap_seek_method,		METH_VARARGS},
	{"size",	(PyCFunction) mmap_size_method,		METH_NOARGS},
	{"tell",	(PyCFunction) mmap_tell_method,		METH_NOARGS},
	{"write",	(PyCFunction) mmap_write_method,	METH_VARARGS},
	{"write_byte",	(PyCFunction) mmap_write_byte_method,	METH_VARARGS},
	{NULL,	   NULL}       /* sentinel */
};

/* Functions for treating an mmap'ed file as a buffer */

static Py_ssize_t
mmap_buffer_getreadbuf(mmap_object *self, Py_ssize_t index, const void **ptr)
{
	CHECK_VALID(-1);
	if (index != 0) {
		PyErr_SetString(PyExc_SystemError,
				"Accessing non-existent mmap segment");
		return -1;
	}
	*ptr = self->data;
	return self->size;
}

static Py_ssize_t
mmap_buffer_getwritebuf(mmap_object *self, Py_ssize_t index, const void **ptr)
{
	CHECK_VALID(-1);
	if (index != 0) {
		PyErr_SetString(PyExc_SystemError,
				"Accessing non-existent mmap segment");
		return -1;
	}
	if (!is_writeable(self))
		return -1;
	*ptr = self->data;
	return self->size;
}

static Py_ssize_t
mmap_buffer_getsegcount(mmap_object *self, Py_ssize_t *lenp)
{
	CHECK_VALID(-1);
	if (lenp)
		*lenp = self->size;
	return 1;
}

static Py_ssize_t
mmap_buffer_getcharbuffer(mmap_object *self, Py_ssize_t index, const void **ptr)
{
	if (index != 0) {
		PyErr_SetString(PyExc_SystemError,
				"accessing non-existent buffer segment");
		return -1;
	}
	*ptr = (const char *)self->data;
	return self->size;
}

static PyObject *
mmap_object_getattr(mmap_object *self, char *name)
{
	return Py_FindMethod(mmap_object_methods, (PyObject *)self, name);
}

static Py_ssize_t
mmap_length(mmap_object *self)
{
	CHECK_VALID(-1);
	return self->size;
}

static PyObject *
mmap_item(mmap_object *self, Py_ssize_t i)
{
	CHECK_VALID(NULL);
	if (i < 0 || (size_t)i >= self->size) {
		PyErr_SetString(PyExc_IndexError, "mmap index out of range");
		return NULL;
	}
	return PyString_FromStringAndSize(self->data + i, 1);
}

static PyObject *
mmap_slice(mmap_object *self, Py_ssize_t ilow, Py_ssize_t ihigh)
{
	CHECK_VALID(NULL);
	if (ilow < 0)
		ilow = 0;
	else if ((size_t)ilow > self->size)
		ilow = self->size;
	if (ihigh < 0)
		ihigh = 0;
	if (ihigh < ilow)
		ihigh = ilow;
	else if ((size_t)ihigh > self->size)
		ihigh = self->size;

	return PyString_FromStringAndSize(self->data + ilow, ihigh-ilow);
}

static PyObject *
mmap_concat(mmap_object *self, PyObject *bb)
{
	CHECK_VALID(NULL);
	PyErr_SetString(PyExc_SystemError,
			"mmaps don't support concatenation");
	return NULL;
}

static PyObject *
mmap_repeat(mmap_object *self, Py_ssize_t n)
{
	CHECK_VALID(NULL);
	PyErr_SetString(PyExc_SystemError,
			"mmaps don't support repeat operation");
	return NULL;
}

static int
mmap_ass_slice(mmap_object *self, Py_ssize_t ilow, Py_ssize_t ihigh, PyObject *v)
{
	const char *buf;

	CHECK_VALID(-1);
	if (ilow < 0)
		ilow = 0;
	else if ((size_t)ilow > self->size)
		ilow = self->size;
	if (ihigh < 0)
		ihigh = 0;
	if (ihigh < ilow)
		ihigh = ilow;
	else if ((size_t)ihigh > self->size)
		ihigh = self->size;

	if (v == NULL) {
		PyErr_SetString(PyExc_TypeError,
				"mmap object doesn't support slice deletion");
		return -1;
	}
	if (! (PyString_Check(v)) ) {
		PyErr_SetString(PyExc_IndexError,
				"mmap slice assignment must be a string");
		return -1;
	}
	if (PyString_Size(v) != (ihigh - ilow)) {
		PyErr_SetString(PyExc_IndexError,
				"mmap slice assignment is wrong size");
		return -1;
	}
	if (!is_writeable(self))
		return -1;
	buf = PyString_AsString(v);
	memcpy(self->data + ilow, buf, ihigh-ilow);
	return 0;
}

static int
mmap_ass_item(mmap_object *self, Py_ssize_t i, PyObject *v)
{
	const char *buf;

	CHECK_VALID(-1);
	if (i < 0 || (size_t)i >= self->size) {
		PyErr_SetString(PyExc_IndexError, "mmap index out of range");
		return -1;
	}
	if (v == NULL) {
		PyErr_SetString(PyExc_TypeError,
				"mmap object doesn't support item deletion");
		return -1;
	}
	if (! (PyString_Check(v) && PyString_Size(v)==1) ) {
		PyErr_SetString(PyExc_IndexError,
				"mmap assignment must be single-character string");
		return -1;
	}
	if (!is_writeable(self))
		return -1;
	buf = PyString_AsString(v);
	self->data[i] = buf[0];
	return 0;
}

static PySequenceMethods mmap_as_sequence = {
	(lenfunc)mmap_length,		       /*sq_length*/
	(binaryfunc)mmap_concat,	       /*sq_concat*/
	(ssizeargfunc)mmap_repeat,	       /*sq_repeat*/
	(ssizeargfunc)mmap_item,		       /*sq_item*/
	(ssizessizeargfunc)mmap_slice,	       /*sq_slice*/
	(ssizeobjargproc)mmap_ass_item,	       /*sq_ass_item*/
	(ssizessizeobjargproc)mmap_ass_slice,      /*sq_ass_slice*/
};

static PyBufferProcs mmap_as_buffer = {
	(readbufferproc)mmap_buffer_getreadbuf,
	(writebufferproc)mmap_buffer_getwritebuf,
	(segcountproc)mmap_buffer_getsegcount,
	(charbufferproc)mmap_buffer_getcharbuffer,
};

static PyTypeObject mmap_object_type = {
	PyObject_HEAD_INIT(0) /* patched in module init */
	0,					/* ob_size */
	"mmap.mmap",				/* tp_name */
	sizeof(mmap_object),			/* tp_size */
	0,					/* tp_itemsize */
	/* methods */
	(destructor) mmap_object_dealloc,	/* tp_dealloc */
	0,					/* tp_print */
	(getattrfunc) mmap_object_getattr,	/* tp_getattr */
	0,					/* tp_setattr */
	0,					/* tp_compare */
	0,					/* tp_repr */
	0,					/* tp_as_number */
	&mmap_as_sequence,			/*tp_as_sequence*/
	0,					/*tp_as_mapping*/
	0,					/*tp_hash*/
	0,					/*tp_call*/
	0,					/*tp_str*/
	0,					/*tp_getattro*/
	0,					/*tp_setattro*/
	&mmap_as_buffer,			/*tp_as_buffer*/
	Py_TPFLAGS_HAVE_GETCHARBUFFER,		/*tp_flags*/
	0,					/*tp_doc*/
};


/* extract the map size from the given PyObject

   Returns -1 on error, with an appropriate Python exception raised. On
   success, the map size is returned. */
static Py_ssize_t
_GetMapSize(PyObject *o)
{
	if (PyIndex_Check(o)) {
		Py_ssize_t i = PyNumber_AsSsize_t(o, PyExc_OverflowError);
		if (i==-1 && PyErr_Occurred()) 
			return -1;
		if (i < 0) {	 
			PyErr_SetString(PyExc_OverflowError,
					"memory mapped size must be positive");
			return -1;
		}
		return i;
	}

	PyErr_SetString(PyExc_TypeError, "map size must be an integral value");
	return -1;
}

#ifdef UNIX
static PyObject *
new_mmap_object(PyObject *self, PyObject *args, PyObject *kwdict)
{
#ifdef HAVE_FSTAT
	struct stat st;
#endif
	mmap_object *m_obj;
	PyObject *map_size_obj = NULL;
	Py_ssize_t map_size;
	int fd, flags = MAP_SHARED, prot = PROT_WRITE | PROT_READ;
	int devzero = -1;
	int access = (int)ACCESS_DEFAULT;
	static char *keywords[] = {"fileno", "length",
                                         "flags", "prot",
                                         "access", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwdict, "iO|iii", keywords,
					 &fd, &map_size_obj, &flags, &prot,
                                         &access))
		return NULL;
	map_size = _GetMapSize(map_size_obj);
	if (map_size < 0)
		return NULL;

	if ((access != (int)ACCESS_DEFAULT) &&
	    ((flags != MAP_SHARED) || (prot != (PROT_WRITE | PROT_READ))))
		return PyErr_Format(PyExc_ValueError,
				    "mmap can't specify both access and flags, prot.");
	switch ((access_mode)access) {
	case ACCESS_READ:
		flags = MAP_SHARED;
		prot = PROT_READ;
		break;
	case ACCESS_WRITE:
		flags = MAP_SHARED;
		prot = PROT_READ | PROT_WRITE;
		break;
	case ACCESS_COPY:
		flags = MAP_PRIVATE;
		prot = PROT_READ | PROT_WRITE;
		break;
	case ACCESS_DEFAULT:
		/* use the specified or default values of flags and prot */
		break;
	default:
		return PyErr_Format(PyExc_ValueError,
				    "mmap invalid access parameter.");
	}

#ifdef HAVE_FSTAT
#  ifdef __VMS
	/* on OpenVMS we must ensure that all bytes are written to the file */
	fsync(fd);
#  endif
	if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
		if (map_size == 0) {
			map_size = st.st_size;
		} else if ((size_t)map_size > st.st_size) {
			PyErr_SetString(PyExc_ValueError,
					"mmap length is greater than file size");
			return NULL;
		}
	}
#endif
	m_obj = PyObject_New(mmap_object, &mmap_object_type);
	if (m_obj == NULL) {return NULL;}
	m_obj->data = NULL;
	m_obj->size = (size_t) map_size;
	m_obj->pos = (size_t) 0;
	if (fd == -1) {
		m_obj->fd = -1;
		/* Assume the caller wants to map anonymous memory.
		   This is the same behaviour as Windows.  mmap.mmap(-1, size)
		   on both Windows and Unix map anonymous memory.
		*/
#ifdef MAP_ANONYMOUS
		/* BSD way to map anonymous memory */
		flags |= MAP_ANONYMOUS;
#else
		/* SVR4 method to map anonymous memory is to open /dev/zero */
		fd = devzero = open("/dev/zero", O_RDWR);
		if (devzero == -1) {
			Py_DECREF(m_obj);
			PyErr_SetFromErrno(mmap_module_error);
			return NULL;
		}
#endif
	} else {
		m_obj->fd = dup(fd);
		if (m_obj->fd == -1) {
			Py_DECREF(m_obj);
			PyErr_SetFromErrno(mmap_module_error);
			return NULL;
		}
	}

	m_obj->data = mmap(NULL, map_size,
			   prot, flags,
			   fd, 0);

	if (devzero != -1) {
		close(devzero);
	}

	if (m_obj->data == (char *)-1) {
	        m_obj->data = NULL;
		Py_DECREF(m_obj);
		PyErr_SetFromErrno(mmap_module_error);
		return NULL;
	}
	m_obj->access = (access_mode)access;
	return (PyObject *)m_obj;
}
#endif /* UNIX */

#ifdef MS_WINDOWS
static PyObject *
new_mmap_object(PyObject *self, PyObject *args, PyObject *kwdict)
{
	mmap_object *m_obj;
	PyObject *map_size_obj = NULL;
	Py_ssize_t map_size;
	DWORD size_hi;	/* upper 32 bits of m_obj->size */
	DWORD size_lo;	/* lower 32 bits of m_obj->size */
	char *tagname = "";
	DWORD dwErr = 0;
	int fileno;
	HANDLE fh = 0;
	int access = (access_mode)ACCESS_DEFAULT;
	DWORD flProtect, dwDesiredAccess;
	static char *keywords[] = { "fileno", "length",
                                          "tagname",
                                          "access", NULL };

	if (!PyArg_ParseTupleAndKeywords(args, kwdict, "iO|zi", keywords,
					 &fileno, &map_size_obj,
					 &tagname, &access)) {
		return NULL;
	}

	switch((access_mode)access) {
	case ACCESS_READ:
		flProtect = PAGE_READONLY;
		dwDesiredAccess = FILE_MAP_READ;
		break;
	case ACCESS_DEFAULT:  case ACCESS_WRITE:
		flProtect = PAGE_READWRITE;
		dwDesiredAccess = FILE_MAP_WRITE;
		break;
	case ACCESS_COPY:
		flProtect = PAGE_WRITECOPY;
		dwDesiredAccess = FILE_MAP_COPY;
		break;
	default:
		return PyErr_Format(PyExc_ValueError,
				    "mmap invalid access parameter.");
	}

	map_size = _GetMapSize(map_size_obj);
	if (map_size < 0)
		return NULL;

	/* assume -1 and 0 both mean invalid filedescriptor
	   to 'anonymously' map memory.
	   XXX: fileno == 0 is a valid fd, but was accepted prior to 2.5.
	   XXX: Should this code be added?
	   if (fileno == 0)
	   	PyErr_Warn(PyExc_DeprecationWarning,
			   "don't use 0 for anonymous memory");
	 */
	if (fileno != -1 && fileno != 0) {
		fh = (HANDLE)_get_osfhandle(fileno);
		if (fh==(HANDLE)-1) {
			PyErr_SetFromErrno(mmap_module_error);
			return NULL;
		}
		/* Win9x appears to need us seeked to zero */
		lseek(fileno, 0, SEEK_SET);
	}

	m_obj = PyObject_New(mmap_object, &mmap_object_type);
	if (m_obj == NULL)
		return NULL;
	/* Set every field to an invalid marker, so we can safely
	   destruct the object in the face of failure */
	m_obj->data = NULL;
	m_obj->file_handle = INVALID_HANDLE_VALUE;
	m_obj->map_handle = INVALID_HANDLE_VALUE;
	m_obj->tagname = NULL;

	if (fh) {
		/* It is necessary to duplicate the handle, so the
		   Python code can close it on us */
		if (!DuplicateHandle(
			GetCurrentProcess(), /* source process handle */
			fh, /* handle to be duplicated */
			GetCurrentProcess(), /* target proc handle */
			(LPHANDLE)&m_obj->file_handle, /* result */
			0, /* access - ignored due to options value */
			FALSE, /* inherited by child processes? */
			DUPLICATE_SAME_ACCESS)) { /* options */
			dwErr = GetLastError();
			Py_DECREF(m_obj);
			PyErr_SetFromWindowsErr(dwErr);
			return NULL;
		}
		if (!map_size) {
			DWORD low,high;
			low = GetFileSize(fh, &high);
			/* low might just happen to have the value INVALID_FILE_SIZE;
    			   so we need to check the last error also. */
			if (low == INVALID_FILE_SIZE &&
			    (dwErr = GetLastError()) != NO_ERROR) {
				Py_DECREF(m_obj);
				return PyErr_SetFromWindowsErr(dwErr);
			}	
				    
#if SIZEOF_SIZE_T > 4
			m_obj->size = (((size_t)high)<<32) + low;
#else
			if (high)
				/* File is too large to map completely */
				m_obj->size = (size_t)-1;
			else
				m_obj->size = low;
#endif
		} else {
			m_obj->size = map_size;
		}
	}
	else {
		m_obj->size = map_size;
	}

	/* set the initial position */
	m_obj->pos = (size_t) 0;

	/* set the tag name */
	if (tagname != NULL && *tagname != '\0') {
		m_obj->tagname = PyMem_Malloc(strlen(tagname)+1);
		if (m_obj->tagname == NULL) {
			PyErr_NoMemory();
			Py_DECREF(m_obj);
			return NULL;
		}
		strcpy(m_obj->tagname, tagname);
	}
	else
		m_obj->tagname = NULL;

	m_obj->access = (access_mode)access;
	/* DWORD is a 4-byte int.  If we're on a box where size_t consumes
	 * more than 4 bytes, we need to break it apart.  Else (size_t
	 * consumes 4 bytes), C doesn't define what happens if we shift
	 * right by 32, so we need different code.
	 */
#if SIZEOF_SIZE_T > 4
	size_hi = (DWORD)(m_obj->size >> 32);
	size_lo = (DWORD)(m_obj->size & 0xFFFFFFFF);
#else
	size_hi = 0;
	size_lo = (DWORD)m_obj->size;
#endif
	m_obj->map_handle = CreateFileMapping(m_obj->file_handle,
					      NULL,
					      flProtect,
					      size_hi,
					      size_lo,
					      m_obj->tagname);
	if (m_obj->map_handle != NULL) {
		m_obj->data = (char *) MapViewOfFile(m_obj->map_handle,
						     dwDesiredAccess,
						     0,
						     0,
						     0);
		if (m_obj->data != NULL)
			return (PyObject *)m_obj;
		else
			dwErr = GetLastError();
	} else
		dwErr = GetLastError();
	Py_DECREF(m_obj);
	PyErr_SetFromWindowsErr(dwErr);
	return NULL;
}
#endif /* MS_WINDOWS */

/* List of functions exported by this module */
static struct PyMethodDef mmap_functions[] = {
	{"mmap",	(PyCFunction) new_mmap_object,
	 METH_VARARGS|METH_KEYWORDS},
	{NULL,		NULL}	     /* Sentinel */
};

static void
setint(PyObject *d, const char *name, long value)
{
	PyObject *o = PyInt_FromLong(value);
	if (o && PyDict_SetItemString(d, name, o) == 0) {
		Py_DECREF(o);
	}
}

PyMODINIT_FUNC
	initmmap(void)
{
	PyObject *dict, *module;

	/* Patch the object type */
	mmap_object_type.ob_type = &PyType_Type;

	module = Py_InitModule("mmap", mmap_functions);
	if (module == NULL)
		return;
	dict = PyModule_GetDict(module);
	if (!dict)
		return;
	mmap_module_error = PyExc_EnvironmentError;
	PyDict_SetItemString(dict, "error", mmap_module_error);
#ifdef PROT_EXEC
	setint(dict, "PROT_EXEC", PROT_EXEC);
#endif
#ifdef PROT_READ
	setint(dict, "PROT_READ", PROT_READ);
#endif
#ifdef PROT_WRITE
	setint(dict, "PROT_WRITE", PROT_WRITE);
#endif

#ifdef MAP_SHARED
	setint(dict, "MAP_SHARED", MAP_SHARED);
#endif
#ifdef MAP_PRIVATE
	setint(dict, "MAP_PRIVATE", MAP_PRIVATE);
#endif
#ifdef MAP_DENYWRITE
	setint(dict, "MAP_DENYWRITE", MAP_DENYWRITE);
#endif
#ifdef MAP_EXECUTABLE
	setint(dict, "MAP_EXECUTABLE", MAP_EXECUTABLE);
#endif
#ifdef MAP_ANONYMOUS
	setint(dict, "MAP_ANON", MAP_ANONYMOUS);
	setint(dict, "MAP_ANONYMOUS", MAP_ANONYMOUS);
#endif

	setint(dict, "PAGESIZE", (long)my_getpagesize());

	setint(dict, "ACCESS_READ", ACCESS_READ);
	setint(dict, "ACCESS_WRITE", ACCESS_WRITE);
	setint(dict, "ACCESS_COPY", ACCESS_COPY);
}
