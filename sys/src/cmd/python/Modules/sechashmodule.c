/* Plan 9 sechash(2) module */

#include "Python.h"
#include "structmember.h"

#define _PLAN9_SOURCE
#include <libsec.h>

typedef struct {
	PyObject_HEAD

	char *t;
	int n, b;
	DigestState *(*f)(uchar *, ulong, uchar *, DigestState *);
	DigestState s;
} SHobject;

static PyTypeObject MD5type;
static PyTypeObject SHA1type;
static PyTypeObject SHA224type;
static PyTypeObject SHA256type;
static PyTypeObject SHA384type;
static PyTypeObject SHA512type;

static void
sh_copy(SHobject *src, SHobject *dest)
{
	dest->t = src->t;
	dest->n = src->n;
	dest->b = src->b;
	dest->f = src->f;
	dest->s = src->s;
}

static void
sh_update(SHobject *s, uchar *buffer, int count)
{
	(*s->f)(buffer, count, NULL, &s->s);
}

static void
sh_final(SHobject *s, uchar digest[])
{
	(*s->f)(NULL, 0, (uchar*)digest, &s->s);
}

static void
sh_dealloc(PyObject *ptr)
{
	PyObject_Del(ptr);
}

PyDoc_STRVAR(SH_copy__doc__, "Return a copy of the hash object.");

static PyObject *
SH_copy(SHobject *self, PyObject *unused)
{
	SHobject *newobj;

	newobj = PyObject_New(SHobject, ((PyObject*)self)->ob_type);
	if(newobj != NULL)
		sh_copy(self, newobj);
	return (PyObject *)newobj;
}

PyDoc_STRVAR(SH_digest__doc__,
"Return the digest value as a string of binary data.");

static PyObject *
SH_digest(SHobject *self, PyObject *unused)
{
	uchar digest[64];
	SHobject temp;

	sh_copy(self, &temp);
	sh_final(&temp, digest);
	return PyString_FromStringAndSize((const char *)digest, self->n);
}

PyDoc_STRVAR(SH_hexdigest__doc__,
"Return the digest value as a string of hexadecimal digits.");

static PyObject *
SH_hexdigest(SHobject *self, PyObject *unused)
{
	uchar digest[64];
	SHobject temp;
	PyObject *retval;
	char *hex_digest;
	int i, j;

	/* Get the raw (binary) digest value */
	sh_copy(self, &temp);
	sh_final(&temp, digest);

	/* Create a new string */
	retval = PyString_FromStringAndSize(NULL, self->n * 2);
	if (!retval)
		return NULL;
	hex_digest = PyString_AsString(retval);
	if (!hex_digest) {
		Py_DECREF(retval);
		return NULL;
	}

	/* Make hex version of the digest */
	for (i=j=0; i<self->n; i++) {
		char c;
		c = (digest[i] >> 4) & 0xf;
		c = (c>9) ? c+'a'-10 : c + '0';
		hex_digest[j++] = c;
		c = (digest[i] & 0xf);
		c = (c>9) ? c+'a'-10 : c + '0';
		hex_digest[j++] = c;
	}
	return retval;
}

PyDoc_STRVAR(SH_update__doc__,
"Update this hash object's state with the provided string.");

static PyObject *
SH_update(SHobject *self, PyObject *args)
{
	uchar *cp;
	int len;

	if (!PyArg_ParseTuple(args, "s#:update", &cp, &len))
		return NULL;
	sh_update(self, cp, len);
	Py_INCREF(Py_None);
	return Py_None;
}

static PyMethodDef SH_methods[] = {
	{"copy", (PyCFunction)SH_copy, METH_NOARGS, SH_copy__doc__},
	{"digest", (PyCFunction)SH_digest, METH_NOARGS, SH_digest__doc__},
	{"hexdigest", (PyCFunction)SH_hexdigest, METH_NOARGS, SH_hexdigest__doc__},
	{"update", (PyCFunction)SH_update, METH_VARARGS, SH_update__doc__},
	{NULL, NULL}
};

static PyObject *
SH_get_block_size(PyObject *self, void *closure)
{
	return PyInt_FromLong(((SHobject*)self)->b);
}

static PyObject *
SH_get_name(PyObject *self, void *closure)
{
	char *s = ((SHobject*)self)->t;
	return PyString_FromStringAndSize(s, strlen(s));
}

static PyGetSetDef SH_getseters[] = {
	{"block_size", (getter)SH_get_block_size, NULL, NULL, NULL},
	{"name", (getter)SH_get_name, NULL, NULL, NULL},
	{NULL}
};

static PyMemberDef SH_members[] = {
	{"digest_size", T_INT, offsetof(SHobject, n), READONLY, NULL},
	{"digestsize", T_INT, offsetof(SHobject, n), READONLY, NULL},
	{NULL}
};

static PyTypeObject MD5type = {PyObject_HEAD_INIT(NULL)0,"_sechash.md5",sizeof(SHobject),0,sh_dealloc,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,Py_TPFLAGS_DEFAULT,0,0,0,0,0,0,0,SH_methods,SH_members,SH_getseters};
static PyTypeObject SHA1type = {PyObject_HEAD_INIT(NULL)0,"_sechash.sha1",sizeof(SHobject),0,sh_dealloc,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,Py_TPFLAGS_DEFAULT,0,0,0,0,0,0,0,SH_methods,SH_members,SH_getseters};
static PyTypeObject SHA224type = {PyObject_HEAD_INIT(NULL)0,"_sechash.sha224",sizeof(SHobject),0,sh_dealloc,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,Py_TPFLAGS_DEFAULT,0,0,0,0,0,0,0,SH_methods,SH_members,SH_getseters};
static PyTypeObject SHA256type = {PyObject_HEAD_INIT(NULL)0,"_sechash.sha256",sizeof(SHobject),0,sh_dealloc,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,Py_TPFLAGS_DEFAULT,0,0,0,0,0,0,0,SH_methods,SH_members,SH_getseters};
static PyTypeObject SHA384type = {PyObject_HEAD_INIT(NULL)0,"_sechash.sha384",sizeof(SHobject),0,sh_dealloc,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,Py_TPFLAGS_DEFAULT,0,0,0,0,0,0,0,SH_methods,SH_members,SH_getseters};
static PyTypeObject SHA512type = {PyObject_HEAD_INIT(NULL)0,"_sechash.sha512",sizeof(SHobject),0,sh_dealloc,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,Py_TPFLAGS_DEFAULT,0,0,0,0,0,0,0,SH_methods,SH_members,SH_getseters};


PyDoc_STRVAR(SHA512_new__doc__,
"Return a new SHA-512 hash object; optionally initialized with a string.");

static PyObject *
SHA512_new(PyObject *self, PyObject *args, PyObject *kwdict)
{
	static char *kwlist[] = {"string", NULL};
	SHobject *new;
	uchar *cp = NULL;
	int len;

	if (!PyArg_ParseTupleAndKeywords(args, kwdict, "|s#:new", kwlist, &cp, &len))
		return NULL;
	if ((new = (SHobject *)PyObject_New(SHobject, &SHA512type)) == NULL)
		return NULL;
	memset(&new->s, 0, sizeof(new->s));
	new->t = "SHA512";
	new->b = 128;
	new->n = SHA2_512dlen;
	new->f = sha2_512;
	if (cp)
		sh_update(new, cp, len);
	return (PyObject *)new;
}

PyDoc_STRVAR(SHA384_new__doc__,
"Return a new SHA-384 hash object; optionally initialized with a string.");

static PyObject *
SHA384_new(PyObject *self, PyObject *args, PyObject *kwdict)
{
	static char *kwlist[] = {"string", NULL};
	SHobject *new;
	uchar *cp = NULL;
	int len;

	if (!PyArg_ParseTupleAndKeywords(args, kwdict, "|s#:new", kwlist, &cp, &len))
		return NULL;
	if ((new = (SHobject *)PyObject_New(SHobject, &SHA384type)) == NULL)
		return NULL;
	memset(&new->s, 0, sizeof(new->s));
	new->t = "SHA384";
	new->b = 128;
	new->n = SHA2_384dlen;
	new->f = sha2_384;
	if (cp)
		sh_update(new, cp, len);
	return (PyObject *)new;
}

PyDoc_STRVAR(SHA256_new__doc__,
"Return a new SHA-256 hash object; optionally initialized with a string.");

static PyObject *
SHA256_new(PyObject *self, PyObject *args, PyObject *kwdict)
{
	static char *kwlist[] = {"string", NULL};
	SHobject *new;
	uchar *cp = NULL;
	int len;

	if (!PyArg_ParseTupleAndKeywords(args, kwdict, "|s#:new", kwlist, &cp, &len))
		return NULL;
	if ((new = (SHobject *)PyObject_New(SHobject, &SHA256type)) == NULL)
		return NULL;
	memset(&new->s, 0, sizeof(new->s));
	new->t = "SHA256";
	new->b = 64;
	new->n = SHA2_256dlen;
	new->f = sha2_256;
	if (cp)
		sh_update(new, cp, len);
	return (PyObject *)new;
}

PyDoc_STRVAR(SHA224_new__doc__,
"Return a new SHA-224 hash object; optionally initialized with a string.");

static PyObject *
SHA224_new(PyObject *self, PyObject *args, PyObject *kwdict)
{
	static char *kwlist[] = {"string", NULL};
	SHobject *new;
	uchar *cp = NULL;
	int len;

	if (!PyArg_ParseTupleAndKeywords(args, kwdict, "|s#:new", kwlist, &cp, &len))
		return NULL;
	if ((new = (SHobject *)PyObject_New(SHobject, &SHA224type)) == NULL)
		return NULL;
	memset(&new->s, 0, sizeof(new->s));
	new->t = "SHA224";
	new->b = 64;
	new->n = SHA2_224dlen;
	new->f = sha2_224;
	if (cp)
		sh_update(new, cp, len);
	return (PyObject *)new;
}

PyDoc_STRVAR(SHA1_new__doc__,
"Return a new SHA1 hash object; optionally initialized with a string.");

static PyObject *
SHA1_new(PyObject *self, PyObject *args, PyObject *kwdict)
{
	static char *kwlist[] = {"string", NULL};
	SHobject *new;
	uchar *cp = NULL;
	int len;

	if (!PyArg_ParseTupleAndKeywords(args, kwdict, "|s#:new", kwlist, &cp, &len))
		return NULL;
	if ((new = (SHobject *)PyObject_New(SHobject, &SHA1type)) == NULL)
		return NULL;
	memset(&new->s, 0, sizeof(new->s));
	new->t = "SHA1";
	new->b = 64;
	new->n = SHA1dlen;
	new->f = sha1;
	if (cp)
		sh_update(new, cp, len);
	return (PyObject *)new;
}

PyDoc_STRVAR(MD5_new__doc__,
"Return a new MD5 hash object; optionally initialized with a string.");

static PyObject *
MD5_new(PyObject *self, PyObject *args, PyObject *kwdict)
{
	static char *kwlist[] = {"string", NULL};
	SHobject *new;
	uchar *cp = NULL;
	int len;

	if (!PyArg_ParseTupleAndKeywords(args, kwdict, "|s#:new", kwlist, &cp, &len))
		return NULL;
	if ((new = (SHobject *)PyObject_New(SHobject, &MD5type)) == NULL)
		return NULL;
	memset(&new->s, 0, sizeof(new->s));
	new->t = "MD5";
	new->b = 16;
	new->n = MD5dlen;
	new->f = md5;
	if (cp)
		sh_update(new, cp, len);
	return (PyObject *)new;
}


/* List of functions exported by this module */

static struct PyMethodDef SH_functions[] = {
	{"sha512", (PyCFunction)SHA512_new, METH_VARARGS|METH_KEYWORDS, SHA512_new__doc__},
	{"sha384", (PyCFunction)SHA384_new, METH_VARARGS|METH_KEYWORDS, SHA384_new__doc__},
	{"sha256", (PyCFunction)SHA256_new, METH_VARARGS|METH_KEYWORDS, SHA256_new__doc__},
	{"sha224", (PyCFunction)SHA224_new, METH_VARARGS|METH_KEYWORDS, SHA224_new__doc__},
	{"sha1",   (PyCFunction)SHA1_new,   METH_VARARGS|METH_KEYWORDS, SHA1_new__doc__},
	{"md5",    (PyCFunction)MD5_new,    METH_VARARGS|METH_KEYWORDS, MD5_new__doc__},
	{NULL,	NULL}		 /* Sentinel */
};

PyMODINIT_FUNC
init_sechash(void)
{
	MD5type.ob_type = &PyType_Type;
	if (PyType_Ready(&MD5type) < 0)
		return;
	SHA1type.ob_type = &PyType_Type;
	if (PyType_Ready(&SHA1type) < 0)
		return;
	SHA224type.ob_type = &PyType_Type;
	if (PyType_Ready(&SHA224type) < 0)
		return;
	SHA256type.ob_type = &PyType_Type;
	if (PyType_Ready(&SHA256type) < 0)
		return;
	SHA384type.ob_type = &PyType_Type;
	if (PyType_Ready(&SHA384type) < 0)
		return;
	SHA512type.ob_type = &PyType_Type;
	if (PyType_Ready(&SHA512type) < 0)
		return;
	Py_InitModule("_sechash", SH_functions);
}
