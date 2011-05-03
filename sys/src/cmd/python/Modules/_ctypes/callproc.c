/*****************************************************************
  This file should be kept compatible with Python 2.3, see PEP 291.
 *****************************************************************/


/*
 * History: First version dated from 3/97, derived from my SCMLIB version
 * for win16.
 */
/*
 * Related Work:
 *	- calldll	http://www.nightmare.com/software.html
 *	- libffi	http://sourceware.cygnus.com/libffi/
 *	- ffcall 	http://clisp.cons.org/~haible/packages-ffcall.html
 *   and, of course, Don Beaudry's MESS package, but this is more ctypes
 *   related.
 */


/*
  How are functions called, and how are parameters converted to C ?

  1. _ctypes.c::CFuncPtr_call receives an argument tuple 'inargs' and a
  keyword dictionary 'kwds'.

  2. After several checks, _build_callargs() is called which returns another
  tuple 'callargs'.  This may be the same tuple as 'inargs', a slice of
  'inargs', or a completely fresh tuple, depending on several things (is is a
  COM method, are 'paramflags' available).

  3. _build_callargs also calculates bitarrays containing indexes into
  the callargs tuple, specifying how to build the return value(s) of
  the function.

  4. _CallProc is then called with the 'callargs' tuple.  _CallProc first
  allocates two arrays.  The first is an array of 'struct argument' items, the
  second array has 'void *' entried.

  5. If 'converters' are present (converters is a sequence of argtypes'
  from_param methods), for each item in 'callargs' converter is called and the
  result passed to ConvParam.  If 'converters' are not present, each argument
  is directly passed to ConvParm.

  6. For each arg, ConvParam stores the contained C data (or a pointer to it,
  for structures) into the 'struct argument' array.

  7. Finally, a loop fills the 'void *' array so that each item points to the
  data contained in or pointed to by the 'struct argument' array.

  8. The 'void *' argument array is what _call_function_pointer
  expects. _call_function_pointer then has very little to do - only some
  libffi specific stuff, then it calls ffi_call.

  So, there are 4 data structures holding processed arguments:
  - the inargs tuple (in CFuncPtr_call)
  - the callargs tuple (in CFuncPtr_call)
  - the 'struct argguments' array
  - the 'void *' array

 */

#include "Python.h"
#include "structmember.h"

#ifdef MS_WIN32
#include <windows.h>
#else
#include "ctypes_dlfcn.h"
#endif

#ifdef MS_WIN32
#include <malloc.h>
#endif

#include <ffi.h>
#include "ctypes.h"

#if defined(_DEBUG) || defined(__MINGW32__)
/* Don't use structured exception handling on Windows if this is defined.
   MingW, AFAIK, doesn't support it.
*/
#define DONT_USE_SEH
#endif

#ifdef MS_WIN32
PyObject *ComError;

static TCHAR *FormatError(DWORD code)
{
	TCHAR *lpMsgBuf;
	DWORD n;
	n = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
			  NULL,
			  code,
			  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
			  (LPTSTR) &lpMsgBuf,
			  0,
			  NULL);
	if (n) {
		while (isspace(lpMsgBuf[n-1]))
			--n;
		lpMsgBuf[n] = '\0'; /* rstrip() */
	}
	return lpMsgBuf;
}

#ifndef DONT_USE_SEH
void SetException(DWORD code, EXCEPTION_RECORD *pr)
{
	TCHAR *lpMsgBuf;
	lpMsgBuf = FormatError(code);
	if(lpMsgBuf) {
		PyErr_SetFromWindowsErr(code);
		LocalFree(lpMsgBuf);
	} else {
		switch (code) {
		case EXCEPTION_ACCESS_VIOLATION:
			/* The thread attempted to read from or write
			   to a virtual address for which it does not
			   have the appropriate access. */
			if (pr->ExceptionInformation[0] == 0)
				PyErr_Format(PyExc_WindowsError,
					     "exception: access violation reading %p",
					     pr->ExceptionInformation[1]);
			else
				PyErr_Format(PyExc_WindowsError,
					     "exception: access violation writing %p",
					     pr->ExceptionInformation[1]);
			break;
		case EXCEPTION_BREAKPOINT:
			/* A breakpoint was encountered. */
			PyErr_SetString(PyExc_WindowsError,
					"exception: breakpoint encountered");
			break;
			
		case EXCEPTION_DATATYPE_MISALIGNMENT:
			/* The thread attempted to read or write data that is
			   misaligned on hardware that does not provide
			   alignment. For example, 16-bit values must be
			   aligned on 2-byte boundaries, 32-bit values on
			   4-byte boundaries, and so on. */
			PyErr_SetString(PyExc_WindowsError,
					"exception: datatype misalignment");
			break;

		case EXCEPTION_SINGLE_STEP:
			/* A trace trap or other single-instruction mechanism
			   signaled that one instruction has been executed. */
			PyErr_SetString(PyExc_WindowsError,
					"exception: single step");
			break;

		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: 
			/* The thread attempted to access an array element
			   that is out of bounds, and the underlying hardware
			   supports bounds checking. */
			PyErr_SetString(PyExc_WindowsError,
					"exception: array bounds exceeded");
			break;

		case EXCEPTION_FLT_DENORMAL_OPERAND:
			/* One of the operands in a floating-point operation
			   is denormal. A denormal value is one that is too
			   small to represent as a standard floating-point
			   value. */
			PyErr_SetString(PyExc_WindowsError,
					"exception: floating-point operand denormal");
			break;

		case EXCEPTION_FLT_DIVIDE_BY_ZERO:
			/* The thread attempted to divide a floating-point
			   value by a floating-point divisor of zero. */
			PyErr_SetString(PyExc_WindowsError,
					"exception: float divide by zero");
			break;

		case EXCEPTION_FLT_INEXACT_RESULT:
			/* The result of a floating-point operation cannot be
			   represented exactly as a decimal fraction. */
			PyErr_SetString(PyExc_WindowsError,
					"exception: float inexact");
			break;

		case EXCEPTION_FLT_INVALID_OPERATION:
			/* This exception represents any floating-point
			   exception not included in this list. */
			PyErr_SetString(PyExc_WindowsError,
					"exception: float invalid operation");
			break;

		case EXCEPTION_FLT_OVERFLOW:
			/* The exponent of a floating-point operation is
			   greater than the magnitude allowed by the
			   corresponding type. */
			PyErr_SetString(PyExc_WindowsError,
					"exception: float overflow");
			break;

		case EXCEPTION_FLT_STACK_CHECK:
			/* The stack overflowed or underflowed as the result
			   of a floating-point operation. */
			PyErr_SetString(PyExc_WindowsError,
					"exception: stack over/underflow");
			break;

		case EXCEPTION_STACK_OVERFLOW:
			/* The stack overflowed or underflowed as the result
			   of a floating-point operation. */
			PyErr_SetString(PyExc_WindowsError,
					"exception: stack overflow");
			break;

		case EXCEPTION_FLT_UNDERFLOW:
			/* The exponent of a floating-point operation is less
			   than the magnitude allowed by the corresponding
			   type. */
			PyErr_SetString(PyExc_WindowsError,
					"exception: float underflow");
			break;

		case EXCEPTION_INT_DIVIDE_BY_ZERO:
			/* The thread attempted to divide an integer value by
			   an integer divisor of zero. */
			PyErr_SetString(PyExc_WindowsError,
					"exception: integer divide by zero");
			break;

		case EXCEPTION_INT_OVERFLOW:
			/* The result of an integer operation caused a carry
			   out of the most significant bit of the result. */
			PyErr_SetString(PyExc_WindowsError,
					"exception: integer overflow");
			break;

		case EXCEPTION_PRIV_INSTRUCTION:
			/* The thread attempted to execute an instruction
			   whose operation is not allowed in the current
			   machine mode. */
			PyErr_SetString(PyExc_WindowsError,
					"exception: priviledged instruction");
			break;

		case EXCEPTION_NONCONTINUABLE_EXCEPTION:
			/* The thread attempted to continue execution after a
			   noncontinuable exception occurred. */
			PyErr_SetString(PyExc_WindowsError,
					"exception: nocontinuable");
			break;
		default:
			printf("error %d\n", code);
			PyErr_Format(PyExc_WindowsError,
				     "exception code 0x%08x",
				     code);
			break;
		}
	}
}

static DWORD HandleException(EXCEPTION_POINTERS *ptrs,
			     DWORD *pdw, EXCEPTION_RECORD *record)
{
	*pdw = ptrs->ExceptionRecord->ExceptionCode;
	*record = *ptrs->ExceptionRecord;
	return EXCEPTION_EXECUTE_HANDLER;
}
#endif

static PyObject *
check_hresult(PyObject *self, PyObject *args)
{
	HRESULT hr;
	if (!PyArg_ParseTuple(args, "i", &hr))
		return NULL;
	if (FAILED(hr))
		return PyErr_SetFromWindowsErr(hr);
	return PyInt_FromLong(hr);
}

#endif

/**************************************************************/

PyCArgObject *
new_CArgObject(void)
{
	PyCArgObject *p;
	p = PyObject_New(PyCArgObject, &PyCArg_Type);
	if (p == NULL)
		return NULL;
	p->pffi_type = NULL;
	p->tag = '\0';
	p->obj = NULL;
	memset(&p->value, 0, sizeof(p->value));
	return p;
}

static void
PyCArg_dealloc(PyCArgObject *self)
{
	Py_XDECREF(self->obj);
	PyObject_Del(self);
}

static PyObject *
PyCArg_repr(PyCArgObject *self)
{
	char buffer[256];
	switch(self->tag) {
	case 'b':
	case 'B':
		sprintf(buffer, "<cparam '%c' (%d)>",
			self->tag, self->value.b);
		break;
	case 'h':
	case 'H':
		sprintf(buffer, "<cparam '%c' (%d)>",
			self->tag, self->value.h);
		break;
	case 'i':
	case 'I':
		sprintf(buffer, "<cparam '%c' (%d)>",
			self->tag, self->value.i);
		break;
	case 'l':
	case 'L':
		sprintf(buffer, "<cparam '%c' (%ld)>",
			self->tag, self->value.l);
		break;
		
#ifdef HAVE_LONG_LONG
	case 'q':
	case 'Q':
		sprintf(buffer,
#ifdef MS_WIN32
			"<cparam '%c' (%I64d)>",
#else
			"<cparam '%c' (%qd)>",
#endif
			self->tag, self->value.q);
		break;
#endif
	case 'd':
		sprintf(buffer, "<cparam '%c' (%f)>",
			self->tag, self->value.d);
		break;
	case 'f':
		sprintf(buffer, "<cparam '%c' (%f)>",
			self->tag, self->value.f);
		break;

	case 'c':
		sprintf(buffer, "<cparam '%c' (%c)>",
			self->tag, self->value.c);
		break;

/* Hm, are these 'z' and 'Z' codes useful at all?
   Shouldn't they be replaced by the functionality of c_string
   and c_wstring ?
*/
	case 'z':
	case 'Z':
	case 'P':
		sprintf(buffer, "<cparam '%c' (%08lx)>",
			self->tag, (long)self->value.p);
		break;

	default:
		sprintf(buffer, "<cparam '%c' at %08lx>",
			self->tag, (long)self);
		break;
	}
	return PyString_FromString(buffer);
}

static PyMemberDef PyCArgType_members[] = {
	{ "_obj", T_OBJECT,
	  offsetof(PyCArgObject, obj), READONLY,
	  "the wrapped object" },
	{ NULL },
};

PyTypeObject PyCArg_Type = {
	PyObject_HEAD_INIT(NULL)
	0,
	"CArgObject",
	sizeof(PyCArgObject),
	0,
	(destructor)PyCArg_dealloc,		/* tp_dealloc */
	0,					/* tp_print */
	0,					/* tp_getattr */
	0,					/* tp_setattr */
	0,					/* tp_compare */
	(reprfunc)PyCArg_repr,			/* tp_repr */
	0,					/* tp_as_number */
	0,					/* tp_as_sequence */
	0,					/* tp_as_mapping */
	0,					/* tp_hash */
	0,					/* tp_call */
	0,					/* tp_str */
	0,					/* tp_getattro */
	0,					/* tp_setattro */
	0,					/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,			/* tp_flags */
	0,					/* tp_doc */
	0,					/* tp_traverse */
	0,					/* tp_clear */
	0,					/* tp_richcompare */
	0,					/* tp_weaklistoffset */
	0,					/* tp_iter */
	0,					/* tp_iternext */
	0,					/* tp_methods */
	PyCArgType_members,			/* tp_members */
};

/****************************************************************/
/*
 * Convert a PyObject * into a parameter suitable to pass to an
 * C function call.
 *
 * 1. Python integers are converted to C int and passed by value.
 *
 * 2. 3-tuples are expected to have a format character in the first
 *    item, which must be 'i', 'f', 'd', 'q', or 'P'.
 *    The second item will have to be an integer, float, double, long long
 *    or integer (denoting an address void *), will be converted to the
 *    corresponding C data type and passed by value.
 *
 * 3. Other Python objects are tested for an '_as_parameter_' attribute.
 *    The value of this attribute must be an integer which will be passed
 *    by value, or a 2-tuple or 3-tuple which will be used according
 *    to point 2 above. The third item (if any), is ignored. It is normally
 *    used to keep the object alive where this parameter refers to.
 *    XXX This convention is dangerous - you can construct arbitrary tuples
 *    in Python and pass them. Would it be safer to use a custom container
 *    datatype instead of a tuple?
 *
 * 4. Other Python objects cannot be passed as parameters - an exception is raised.
 *
 * 5. ConvParam will store the converted result in a struct containing format
 *    and value.
 */

union result {
	char c;
	char b;
	short h;
	int i;
	long l;
#ifdef HAVE_LONG_LONG
	PY_LONG_LONG q;
#endif
	double d;
	float f;
	void *p;
};

struct argument {
	ffi_type *ffi_type;
	PyObject *keep;
	union result value;
};

/*
 * Convert a single Python object into a PyCArgObject and return it.
 */
static int ConvParam(PyObject *obj, int index, struct argument *pa)
{
	StgDictObject *dict;
	pa->keep = NULL; /* so we cannot forget it later */

	dict = PyObject_stgdict(obj);
	if (dict) {
		PyCArgObject *carg;
		assert(dict->paramfunc);
		/* If it has an stgdict, it is a CDataObject */
		carg = dict->paramfunc((CDataObject *)obj);
		pa->ffi_type = carg->pffi_type;
		memcpy(&pa->value, &carg->value, sizeof(pa->value));
		pa->keep = (PyObject *)carg;
		return 0;
	}

	if (PyCArg_CheckExact(obj)) {
		PyCArgObject *carg = (PyCArgObject *)obj;
		pa->ffi_type = carg->pffi_type;
		Py_INCREF(obj);
		pa->keep = obj;
		memcpy(&pa->value, &carg->value, sizeof(pa->value));
		return 0;
	}

	/* check for None, integer, string or unicode and use directly if successful */
	if (obj == Py_None) {
		pa->ffi_type = &ffi_type_pointer;
		pa->value.p = NULL;
		return 0;
	}

	if (PyInt_Check(obj)) {
		pa->ffi_type = &ffi_type_sint;
		pa->value.i = PyInt_AS_LONG(obj);
		return 0;
	}

	if (PyLong_Check(obj)) {
		pa->ffi_type = &ffi_type_sint;
		pa->value.i = (long)PyLong_AsUnsignedLong(obj);
		if (pa->value.i == -1 && PyErr_Occurred()) {
			PyErr_Clear();
			pa->value.i = PyLong_AsLong(obj);
			if (pa->value.i == -1 && PyErr_Occurred()) {
				PyErr_SetString(PyExc_OverflowError,
						"long int too long to convert");
				return -1;
			}
		}
		return 0;
	}

	if (PyString_Check(obj)) {
		pa->ffi_type = &ffi_type_pointer;
		pa->value.p = PyString_AS_STRING(obj);
		Py_INCREF(obj);
		pa->keep = obj;
		return 0;
	}

#ifdef CTYPES_UNICODE
	if (PyUnicode_Check(obj)) {
#ifdef HAVE_USABLE_WCHAR_T
		pa->ffi_type = &ffi_type_pointer;
		pa->value.p = PyUnicode_AS_UNICODE(obj);
		Py_INCREF(obj);
		pa->keep = obj;
		return 0;
#else
		int size = PyUnicode_GET_SIZE(obj);
		size += 1; /* terminating NUL */
		size *= sizeof(wchar_t);
		pa->value.p = PyMem_Malloc(size);
		if (!pa->value.p)
			return -1;
		memset(pa->value.p, 0, size);
		pa->keep = PyCObject_FromVoidPtr(pa->value.p, PyMem_Free);
		if (!pa->keep) {
			PyMem_Free(pa->value.p);
			return -1;
		}
		if (-1 == PyUnicode_AsWideChar((PyUnicodeObject *)obj,
					       pa->value.p, PyUnicode_GET_SIZE(obj)))
			return -1;
		return 0;
#endif
	}
#endif

	{
		PyObject *arg;
		arg = PyObject_GetAttrString(obj, "_as_parameter_");
		/* Which types should we exactly allow here?
		   integers are required for using Python classes
		   as parameters (they have to expose the '_as_parameter_'
		   attribute)
		*/
		if (arg) {
			int result;
			result = ConvParam(arg, index, pa);
			Py_DECREF(arg);
			return result;
		}
		PyErr_Format(PyExc_TypeError,
			     "Don't know how to convert parameter %d", index);
		return -1;
	}
}


ffi_type *GetType(PyObject *obj)
{
	StgDictObject *dict;
	if (obj == NULL)
		return &ffi_type_sint;
	dict = PyType_stgdict(obj);
	if (dict == NULL)
		return &ffi_type_sint;
#if defined(MS_WIN32) && !defined(_WIN32_WCE)
	/* This little trick works correctly with MSVC.
	   It returns small structures in registers
	*/
	if (dict->ffi_type_pointer.type == FFI_TYPE_STRUCT) {
		if (dict->ffi_type_pointer.size <= 4)
			return &ffi_type_sint32;
		else if (dict->ffi_type_pointer.size <= 8)
			return &ffi_type_sint64;
	}
#endif
	return &dict->ffi_type_pointer;
}


/*
 * libffi uses:
 *
 * ffi_status ffi_prep_cif(ffi_cif *cif, ffi_abi abi,
 *	                   unsigned int nargs,
 *                         ffi_type *rtype,
 *                         ffi_type **atypes);
 *
 * and then
 *
 * void ffi_call(ffi_cif *cif, void *fn, void *rvalue, void **avalues);
 */
static int _call_function_pointer(int flags,
				  PPROC pProc,
				  void **avalues,
				  ffi_type **atypes,
				  ffi_type *restype,
				  void *resmem,
				  int argcount)
{
#ifdef WITH_THREAD
	PyThreadState *_save = NULL; /* For Py_BLOCK_THREADS and Py_UNBLOCK_THREADS */
#endif
	ffi_cif cif;
	int cc;
#ifdef MS_WIN32
	int delta;
#ifndef DONT_USE_SEH
	DWORD dwExceptionCode = 0;
	EXCEPTION_RECORD record;
#endif
#endif
	/* XXX check before here */
	if (restype == NULL) {
		PyErr_SetString(PyExc_RuntimeError,
				"No ffi_type for result");
		return -1;
	}
	
	cc = FFI_DEFAULT_ABI;
#if defined(MS_WIN32) && !defined(_WIN32_WCE)
	if ((flags & FUNCFLAG_CDECL) == 0)
		cc = FFI_STDCALL;
#endif
	if (FFI_OK != ffi_prep_cif(&cif,
				   cc,
				   argcount,
				   restype,
				   atypes)) {
		PyErr_SetString(PyExc_RuntimeError,
				"ffi_prep_cif failed");
		return -1;
	}

#ifdef WITH_THREAD
	if ((flags & FUNCFLAG_PYTHONAPI) == 0)
		Py_UNBLOCK_THREADS
#endif
#ifdef MS_WIN32
#ifndef DONT_USE_SEH
	__try {
#endif
		delta =
#endif
			ffi_call(&cif, (void *)pProc, resmem, avalues);
#ifdef MS_WIN32
#ifndef DONT_USE_SEH
	}
	__except (HandleException(GetExceptionInformation(),
				  &dwExceptionCode, &record)) {
		;
	}
#endif
#endif
#ifdef WITH_THREAD
	if ((flags & FUNCFLAG_PYTHONAPI) == 0)
		Py_BLOCK_THREADS
#endif
#ifdef MS_WIN32
#ifndef DONT_USE_SEH
	if (dwExceptionCode) {
		SetException(dwExceptionCode, &record);
		return -1;
	}
#endif
	if (delta < 0) {
		if (flags & FUNCFLAG_CDECL)
			PyErr_Format(PyExc_ValueError,
				     "Procedure called with not enough "
				     "arguments (%d bytes missing) "
				     "or wrong calling convention",
				     -delta);
		else
			PyErr_Format(PyExc_ValueError,
				     "Procedure probably called with not enough "
				     "arguments (%d bytes missing)",
				     -delta);
		return -1;
	} else if (delta > 0) {
		PyErr_Format(PyExc_ValueError,
			     "Procedure probably called with too many "
			     "arguments (%d bytes in excess)",
			     delta);
		return -1;
	}
#endif
	if ((flags & FUNCFLAG_PYTHONAPI) && PyErr_Occurred())
		return -1;
	return 0;
}

/*
 * Convert the C value in result into a Python object, depending on restype.
 *
 * - If restype is NULL, return a Python integer.
 * - If restype is None, return None.
 * - If restype is a simple ctypes type (c_int, c_void_p), call the type's getfunc,
 *   pass the result to checker and return the result.
 * - If restype is another ctypes type, return an instance of that.
 * - Otherwise, call restype and return the result.
 */
static PyObject *GetResult(PyObject *restype, void *result, PyObject *checker)
{
	StgDictObject *dict;
	PyObject *retval, *v;

	if (restype == NULL)
		return PyInt_FromLong(*(int *)result);

	if (restype == Py_None) {
		Py_INCREF(Py_None);
		return Py_None;
	}

	dict = PyType_stgdict(restype);
	if (dict == NULL)
		return PyObject_CallFunction(restype, "i", *(int *)result);

	if (dict->getfunc && !IsSimpleSubType(restype)) {
		retval = dict->getfunc(result, dict->size);
		/* If restype is py_object (detected by comparing getfunc with
		   O_get), we have to call Py_DECREF because O_get has already
		   called Py_INCREF.
		*/
		if (dict->getfunc == getentry("O")->getfunc) {
			Py_DECREF(retval);
		}
	} else
		retval = CData_FromBaseObj(restype, NULL, 0, result);

	if (!checker || !retval)
		return retval;

	v = PyObject_CallFunctionObjArgs(checker, retval, NULL);
	if (v == NULL)
		_AddTraceback("GetResult", __FILE__, __LINE__-2);
	Py_DECREF(retval);
	return v;
}

/*
 * Raise a new exception 'exc_class', adding additional text to the original
 * exception string.
 */
void Extend_Error_Info(PyObject *exc_class, char *fmt, ...)
{
	va_list vargs;
	PyObject *tp, *v, *tb, *s, *cls_str, *msg_str;

	va_start(vargs, fmt);
	s = PyString_FromFormatV(fmt, vargs);
	va_end(vargs);
	if (!s)
		return;

	PyErr_Fetch(&tp, &v, &tb);
	PyErr_NormalizeException(&tp, &v, &tb);
	cls_str = PyObject_Str(tp);
	if (cls_str) {
		PyString_ConcatAndDel(&s, cls_str);
		PyString_ConcatAndDel(&s, PyString_FromString(": "));
		if (s == NULL)
			goto error;
	} else
		PyErr_Clear();
	msg_str = PyObject_Str(v);
	if (msg_str)
		PyString_ConcatAndDel(&s, msg_str);
	else {
		PyErr_Clear();
		PyString_ConcatAndDel(&s, PyString_FromString("???"));
		if (s == NULL)
			goto error;
	}
	PyErr_SetObject(exc_class, s);
error:
	Py_XDECREF(tp);
	Py_XDECREF(v);
	Py_XDECREF(tb);
	Py_XDECREF(s);
}


#ifdef MS_WIN32

static PyObject *
GetComError(HRESULT errcode, GUID *riid, IUnknown *pIunk)
{
	HRESULT hr;
	ISupportErrorInfo *psei = NULL;
	IErrorInfo *pei = NULL;
	BSTR descr=NULL, helpfile=NULL, source=NULL;
	GUID guid;
	DWORD helpcontext=0;
	LPOLESTR progid;
	PyObject *obj;
	TCHAR *text;

	/* We absolutely have to release the GIL during COM method calls,
	   otherwise we may get a deadlock!
	*/
#ifdef WITH_THREAD
	Py_BEGIN_ALLOW_THREADS
#endif

	hr = pIunk->lpVtbl->QueryInterface(pIunk, &IID_ISupportErrorInfo, (void **)&psei);
	if (FAILED(hr))
		goto failed;

	hr = psei->lpVtbl->InterfaceSupportsErrorInfo(psei, riid);
	psei->lpVtbl->Release(psei);
	if (FAILED(hr))
		goto failed;

	hr = GetErrorInfo(0, &pei);
	if (hr != S_OK)
		goto failed;

	pei->lpVtbl->GetDescription(pei, &descr);
	pei->lpVtbl->GetGUID(pei, &guid);
	pei->lpVtbl->GetHelpContext(pei, &helpcontext);
	pei->lpVtbl->GetHelpFile(pei, &helpfile);
	pei->lpVtbl->GetSource(pei, &source);

	pei->lpVtbl->Release(pei);

  failed:
#ifdef WITH_THREAD
	Py_END_ALLOW_THREADS
#endif

	progid = NULL;
	ProgIDFromCLSID(&guid, &progid);

	text = FormatError(errcode);
	obj = Py_BuildValue(
#ifdef _UNICODE
		"iu(uuuiu)",
#else
		"is(uuuiu)",
#endif
		errcode,
		text,
		descr, source, helpfile, helpcontext,
		progid);
	if (obj) {
		PyErr_SetObject(ComError, obj);
		Py_DECREF(obj);
	}
	LocalFree(text);

	if (descr)
		SysFreeString(descr);
	if (helpfile)
		SysFreeString(helpfile);
	if (source)
		SysFreeString(source);

	return NULL;
}
#endif

/*
 * Requirements, must be ensured by the caller:
 * - argtuple is tuple of arguments
 * - argtypes is either NULL, or a tuple of the same size as argtuple
 *
 * - XXX various requirements for restype, not yet collected
 */
PyObject *_CallProc(PPROC pProc,
		    PyObject *argtuple,
#ifdef MS_WIN32
		    IUnknown *pIunk,
		    GUID *iid,
#endif
		    int flags,
		    PyObject *argtypes, /* misleading name: This is a tuple of
					   methods, not types: the .from_param
					   class methods of the types */
		    PyObject *restype,
		    PyObject *checker)
{
	int i, n, argcount, argtype_count;
	void *resbuf;
	struct argument *args, *pa;
	ffi_type **atypes;
	ffi_type *rtype;
	void **avalues;
	PyObject *retval = NULL;

	n = argcount = PyTuple_GET_SIZE(argtuple);
#ifdef MS_WIN32
	/* an optional COM object this pointer */
	if (pIunk)
		++argcount;
#endif

	args = (struct argument *)alloca(sizeof(struct argument) * argcount);
	if (!args) {
		PyErr_NoMemory();
		return NULL;
	}
	memset(args, 0, sizeof(struct argument) * argcount);
	argtype_count = argtypes ? PyTuple_GET_SIZE(argtypes) : 0;
#ifdef MS_WIN32
	if (pIunk) {
		args[0].ffi_type = &ffi_type_pointer;
		args[0].value.p = pIunk;
		pa = &args[1];
	} else
#endif
		pa = &args[0];

	/* Convert the arguments */
	for (i = 0; i < n; ++i, ++pa) {
		PyObject *converter;
		PyObject *arg;
		int err;

		arg = PyTuple_GET_ITEM(argtuple, i);	/* borrowed ref */
		/* For cdecl functions, we allow more actual arguments
		   than the length of the argtypes tuple.
		   This is checked in _ctypes::CFuncPtr_Call
		*/
		if (argtypes && argtype_count > i) {
			PyObject *v;
			converter = PyTuple_GET_ITEM(argtypes, i);
			v = PyObject_CallFunctionObjArgs(converter,
							   arg,
							   NULL);
			if (v == NULL) {
				Extend_Error_Info(PyExc_ArgError, "argument %d: ", i+1);
				goto cleanup;
			}

			err = ConvParam(v, i+1, pa);
			Py_DECREF(v);
			if (-1 == err) {
				Extend_Error_Info(PyExc_ArgError, "argument %d: ", i+1);
				goto cleanup;
			}
		} else {
			err = ConvParam(arg, i+1, pa);
			if (-1 == err) {
				Extend_Error_Info(PyExc_ArgError, "argument %d: ", i+1);
				goto cleanup; /* leaking ? */
			}
		}
	}

	rtype = GetType(restype);
	resbuf = alloca(max(rtype->size, sizeof(ffi_arg)));

	avalues = (void **)alloca(sizeof(void *) * argcount);
	atypes = (ffi_type **)alloca(sizeof(ffi_type *) * argcount);
	if (!resbuf || !avalues || !atypes) {
		PyErr_NoMemory();
		goto cleanup;
	}
	for (i = 0; i < argcount; ++i) {
		atypes[i] = args[i].ffi_type;
		if (atypes[i]->type == FFI_TYPE_STRUCT)
			avalues[i] = (void *)args[i].value.p;
		else
			avalues[i] = (void *)&args[i].value;
	}

	if (-1 == _call_function_pointer(flags, pProc, avalues, atypes,
					 rtype, resbuf, argcount))
		goto cleanup;

#ifdef WORDS_BIGENDIAN
	/* libffi returns the result in a buffer with sizeof(ffi_arg). This
	   causes problems on big endian machines, since the result buffer
	   address cannot simply be used as result pointer, instead we must
	   adjust the pointer value:
	 */
	/*
	  XXX I should find out and clarify why this is needed at all,
	  especially why adjusting for ffi_type_float must be avoided on
	  64-bit platforms.
	 */
	if (rtype->type != FFI_TYPE_FLOAT
	    && rtype->type != FFI_TYPE_STRUCT
	    && rtype->size < sizeof(ffi_arg))
		resbuf = (char *)resbuf + sizeof(ffi_arg) - rtype->size;
#endif

#ifdef MS_WIN32
	if (iid && pIunk) {
		if (*(int *)resbuf & 0x80000000)
			retval = GetComError(*(HRESULT *)resbuf, iid, pIunk);
		else
			retval = PyInt_FromLong(*(int *)resbuf);
	} else if (flags & FUNCFLAG_HRESULT) {
		if (*(int *)resbuf & 0x80000000)
			retval = PyErr_SetFromWindowsErr(*(int *)resbuf);
		else
			retval = PyInt_FromLong(*(int *)resbuf);
	} else
#endif
		retval = GetResult(restype, resbuf, checker);
  cleanup:
	for (i = 0; i < argcount; ++i)
		Py_XDECREF(args[i].keep);
	return retval;
}

#ifdef MS_WIN32

#ifdef _UNICODE
#  define PYBUILD_TSTR "u"
#else
#  define PYBUILD_TSTR "s"
#  ifndef _T
#    define _T(text) text
#  endif
#endif

static char format_error_doc[] =
"FormatError([integer]) -> string\n\
\n\
Convert a win32 error code into a string. If the error code is not\n\
given, the return value of a call to GetLastError() is used.\n";
static PyObject *format_error(PyObject *self, PyObject *args)
{
	PyObject *result;
	TCHAR *lpMsgBuf;
	DWORD code = 0;
	if (!PyArg_ParseTuple(args, "|i:FormatError", &code))
		return NULL;
	if (code == 0)
		code = GetLastError();
	lpMsgBuf = FormatError(code);
	if (lpMsgBuf) {
		result = Py_BuildValue(PYBUILD_TSTR, lpMsgBuf);
		LocalFree(lpMsgBuf);
	} else {
		result = Py_BuildValue("s", "<no description>");
	}
	return result;
}

static char load_library_doc[] =
"LoadLibrary(name) -> handle\n\
\n\
Load an executable (usually a DLL), and return a handle to it.\n\
The handle may be used to locate exported functions in this\n\
module.\n";
static PyObject *load_library(PyObject *self, PyObject *args)
{
	TCHAR *name;
	PyObject *nameobj;
	PyObject *ignored;
	HMODULE hMod;
	if (!PyArg_ParseTuple(args, "O|O:LoadLibrary", &nameobj, &ignored))
		return NULL;
#ifdef _UNICODE
	name = alloca((PyString_Size(nameobj) + 1) * sizeof(WCHAR));
	if (!name) {
		PyErr_NoMemory();
		return NULL;
	}

	{
		int r;
		char *aname = PyString_AsString(nameobj);
		if(!aname)
			return NULL;
		r = MultiByteToWideChar(CP_ACP, 0, aname, -1, name, PyString_Size(nameobj) + 1);
		name[r] = 0;
	}
#else
	name = PyString_AsString(nameobj);
	if(!name)
		return NULL;
#endif

	hMod = LoadLibrary(name);
	if (!hMod)
		return PyErr_SetFromWindowsErr(GetLastError());
	return Py_BuildValue("i", hMod);
}

static char free_library_doc[] =
"FreeLibrary(handle) -> void\n\
\n\
Free the handle of an executable previously loaded by LoadLibrary.\n";
static PyObject *free_library(PyObject *self, PyObject *args)
{
	HMODULE hMod;
	if (!PyArg_ParseTuple(args, "i:FreeLibrary", &hMod))
		return NULL;
	if (!FreeLibrary(hMod))
		return PyErr_SetFromWindowsErr(GetLastError());
	Py_INCREF(Py_None);
	return Py_None;
}

/* obsolete, should be removed */
/* Only used by sample code (in samples\Windows\COM.py) */
static PyObject *
call_commethod(PyObject *self, PyObject *args)
{
	IUnknown *pIunk;
	int index;
	PyObject *arguments;
	PPROC *lpVtbl;
	PyObject *result;
	CDataObject *pcom;
	PyObject *argtypes = NULL;

	if (!PyArg_ParseTuple(args,
			      "OiO!|O!",
			      &pcom, &index,
			      &PyTuple_Type, &arguments,
			      &PyTuple_Type, &argtypes))
		return NULL;

	if (argtypes && (PyTuple_GET_SIZE(arguments) != PyTuple_GET_SIZE(argtypes))) {
		PyErr_Format(PyExc_TypeError,
			     "Method takes %d arguments (%d given)",
			     PyTuple_GET_SIZE(argtypes), PyTuple_GET_SIZE(arguments));
		return NULL;
	}

	if (!CDataObject_Check(pcom) || (pcom->b_size != sizeof(void *))) {
		PyErr_Format(PyExc_TypeError,
			     "COM Pointer expected instead of %s instance",
			     pcom->ob_type->tp_name);
		return NULL;
	}

	if ((*(void **)(pcom->b_ptr)) == NULL) {
		PyErr_SetString(PyExc_ValueError,
				"The COM 'this' pointer is NULL");
		return NULL;
	}

	pIunk = (IUnknown *)(*(void **)(pcom->b_ptr));
	lpVtbl = (PPROC *)(pIunk->lpVtbl);

	result =  _CallProc(lpVtbl[index],
			    arguments,
#ifdef MS_WIN32
			    pIunk,
			    NULL,
#endif
			    FUNCFLAG_HRESULT, /* flags */
			    argtypes, /* self->argtypes */
			    NULL, /* self->restype */
			    NULL); /* checker */
	return result;
}

static char copy_com_pointer_doc[] =
"CopyComPointer(src, dst) -> HRESULT value\n";

static PyObject *
copy_com_pointer(PyObject *self, PyObject *args)
{
	PyObject *p1, *p2, *r = NULL;
	struct argument a, b;
	IUnknown *src, **pdst;
	if (!PyArg_ParseTuple(args, "OO:CopyComPointer", &p1, &p2))
		return NULL;
	a.keep = b.keep = NULL;

	if (-1 == ConvParam(p1, 0, &a) || -1 == ConvParam(p2, 1, &b))
		goto done;
	src = (IUnknown *)a.value.p;
	pdst = (IUnknown **)b.value.p;

	if (pdst == NULL)
		r = PyInt_FromLong(E_POINTER);
	else {
		if (src)
			src->lpVtbl->AddRef(src);
		*pdst = src;
		r = PyInt_FromLong(S_OK);
	}
  done:
	Py_XDECREF(a.keep);
	Py_XDECREF(b.keep);
	return r;
}
#else

static PyObject *py_dl_open(PyObject *self, PyObject *args)
{
	char *name;
	void * handle;
#ifdef RTLD_LOCAL	
	int mode = RTLD_NOW | RTLD_LOCAL;
#else
	/* cygwin doesn't define RTLD_LOCAL */
	int mode = RTLD_NOW;
#endif
	if (!PyArg_ParseTuple(args, "z|i:dlopen", &name, &mode))
		return NULL;
	mode |= RTLD_NOW;
	handle = ctypes_dlopen(name, mode);
	if (!handle) {
		PyErr_SetString(PyExc_OSError,
				       ctypes_dlerror());
		return NULL;
	}
	return PyLong_FromVoidPtr(handle);
}

static PyObject *py_dl_close(PyObject *self, PyObject *args)
{
	void * handle;

	if (!PyArg_ParseTuple(args, "i:dlclose", &handle))
		return NULL;
	if (dlclose(handle)) {
		PyErr_SetString(PyExc_OSError,
				       ctypes_dlerror());
		return NULL;
	}
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *py_dl_sym(PyObject *self, PyObject *args)
{
	char *name;
	void *handle;
	void *ptr;

	if (!PyArg_ParseTuple(args, "is:dlsym", &handle, &name))
		return NULL;
	ptr = ctypes_dlsym(handle, name);
	if (!ptr) {
		PyErr_SetString(PyExc_OSError,
				       ctypes_dlerror());
		return NULL;
	}
	return Py_BuildValue("i", ptr);
}
#endif

/*
 * Only for debugging so far: So that we can call CFunction instances
 *
 * XXX Needs to accept more arguments: flags, argtypes, restype
 */
static PyObject *
call_function(PyObject *self, PyObject *args)
{
	PPROC func;
	PyObject *arguments;
	PyObject *result;

	if (!PyArg_ParseTuple(args,
			      "iO!",
			      &func,
			      &PyTuple_Type, &arguments))
		return NULL;

	result =  _CallProc(func,
			    arguments,
#ifdef MS_WIN32
			    NULL,
			    NULL,
#endif
			    0, /* flags */
			    NULL, /* self->argtypes */
			    NULL, /* self->restype */
			    NULL); /* checker */
	return result;
}

/*
 * Only for debugging so far: So that we can call CFunction instances
 *
 * XXX Needs to accept more arguments: flags, argtypes, restype
 */
static PyObject *
call_cdeclfunction(PyObject *self, PyObject *args)
{
	PPROC func;
	PyObject *arguments;
	PyObject *result;

	if (!PyArg_ParseTuple(args,
			      "iO!",
			      &func,
			      &PyTuple_Type, &arguments))
		return NULL;

	result =  _CallProc(func,
			    arguments,
#ifdef MS_WIN32
			    NULL,
			    NULL,
#endif
			    FUNCFLAG_CDECL, /* flags */
			    NULL, /* self->argtypes */
			    NULL, /* self->restype */
			    NULL); /* checker */
	return result;
}

/*****************************************************************
 * functions
 */
static char sizeof_doc[] =
"sizeof(C type) -> integer\n"
"sizeof(C instance) -> integer\n"
"Return the size in bytes of a C instance";

static PyObject *
sizeof_func(PyObject *self, PyObject *obj)
{
	StgDictObject *dict;

	dict = PyType_stgdict(obj);
	if (dict)
		return PyInt_FromLong(dict->size);

	if (CDataObject_Check(obj))
		return PyInt_FromLong(((CDataObject *)obj)->b_size);
	PyErr_SetString(PyExc_TypeError,
			"this type has no size");
	return NULL;
}

static char alignment_doc[] =
"alignment(C type) -> integer\n"
"alignment(C instance) -> integer\n"
"Return the alignment requirements of a C instance";

static PyObject *
align_func(PyObject *self, PyObject *obj)
{
	StgDictObject *dict;

	dict = PyType_stgdict(obj);
	if (dict)
		return PyInt_FromLong(dict->align);

	dict = PyObject_stgdict(obj);
	if (dict)
		return PyInt_FromLong(dict->align);

	PyErr_SetString(PyExc_TypeError,
			"no alignment info");
	return NULL;
}

static char byref_doc[] =
"byref(C instance) -> byref-object\n"
"Return a pointer lookalike to a C instance, only usable\n"
"as function argument";

/*
 * We must return something which can be converted to a parameter,
 * but still has a reference to self.
 */
static PyObject *
byref(PyObject *self, PyObject *obj)
{
	PyCArgObject *parg;
	if (!CDataObject_Check(obj)) {
		PyErr_Format(PyExc_TypeError,
			     "byref() argument must be a ctypes instance, not '%s'",
			     obj->ob_type->tp_name);
		return NULL;
	}

	parg = new_CArgObject();
	if (parg == NULL)
		return NULL;

	parg->tag = 'P';
	parg->pffi_type = &ffi_type_pointer;
	Py_INCREF(obj);
	parg->obj = obj;
	parg->value.p = ((CDataObject *)obj)->b_ptr;
	return (PyObject *)parg;
}

static char addressof_doc[] =
"addressof(C instance) -> integer\n"
"Return the address of the C instance internal buffer";

static PyObject *
addressof(PyObject *self, PyObject *obj)
{
	if (CDataObject_Check(obj))
		return PyLong_FromVoidPtr(((CDataObject *)obj)->b_ptr);
	PyErr_SetString(PyExc_TypeError,
			"invalid type");
	return NULL;
}

static int
converter(PyObject *obj, void **address)
{
	*address = PyLong_AsVoidPtr(obj);
	return *address != NULL;
}

static PyObject *
My_PyObj_FromPtr(PyObject *self, PyObject *args)
{
	PyObject *ob;
	if (!PyArg_ParseTuple(args, "O&:PyObj_FromPtr", converter, &ob))
		return NULL;
	Py_INCREF(ob);
	return ob;
}

static PyObject *
My_Py_INCREF(PyObject *self, PyObject *arg)
{
	Py_INCREF(arg); /* that's what this function is for */
	Py_INCREF(arg); /* that for returning it */
	return arg;
}

static PyObject *
My_Py_DECREF(PyObject *self, PyObject *arg)
{
	Py_DECREF(arg); /* that's what this function is for */
	Py_INCREF(arg); /* that's for returning it */
	return arg;
}

#ifdef CTYPES_UNICODE

static char set_conversion_mode_doc[] =
"set_conversion_mode(encoding, errors) -> (previous-encoding, previous-errors)\n\
\n\
Set the encoding and error handling ctypes uses when converting\n\
between unicode and strings.  Returns the previous values.\n";

static PyObject *
set_conversion_mode(PyObject *self, PyObject *args)
{
	char *coding, *mode;
	PyObject *result;

	if (!PyArg_ParseTuple(args, "zs:set_conversion_mode", &coding, &mode))
		return NULL;
	result = Py_BuildValue("(zz)", conversion_mode_encoding, conversion_mode_errors);
	if (coding) {
		PyMem_Free(conversion_mode_encoding);
		conversion_mode_encoding = PyMem_Malloc(strlen(coding) + 1);
		strcpy(conversion_mode_encoding, coding);
	} else {
		conversion_mode_encoding = NULL;
	}
	PyMem_Free(conversion_mode_errors);
	conversion_mode_errors = PyMem_Malloc(strlen(mode) + 1);
	strcpy(conversion_mode_errors, mode);
	return result;
}
#endif

static PyObject *
resize(PyObject *self, PyObject *args)
{
	CDataObject *obj;
	StgDictObject *dict;
	Py_ssize_t size;

	if (!PyArg_ParseTuple(args,
#if (PY_VERSION_HEX < 0x02050000)
			      "Oi:resize",
#else
			      "On:resize",
#endif
			      (PyObject *)&obj, &size))
		return NULL;

	dict = PyObject_stgdict((PyObject *)obj);
	if (dict == NULL) {
		PyErr_SetString(PyExc_TypeError,
				"excepted ctypes instance");
		return NULL;
	}
	if (size < dict->size) {
		PyErr_Format(PyExc_ValueError,
#if PY_VERSION_HEX < 0x02050000
			     "minimum size is %d",
#else
			     "minimum size is %zd",
#endif
			     dict->size);
		return NULL;
	}
	if (obj->b_needsfree == 0) {
		PyErr_Format(PyExc_ValueError,
			     "Memory cannot be resized because this object doesn't own it");
		return NULL;
	}
	if (size <= sizeof(obj->b_value)) {
		/* internal default buffer is large enough */
		obj->b_size = size;
		goto done;
	}
	if (obj->b_size <= sizeof(obj->b_value)) {
		/* We are currently using the objects default buffer, but it
		   isn't large enough any more. */
		void *ptr = PyMem_Malloc(size);
		if (ptr == NULL)
			return PyErr_NoMemory();
		memset(ptr, 0, size);
		memmove(ptr, obj->b_ptr, obj->b_size);
		obj->b_ptr = ptr;
		obj->b_size = size;
	} else {
		void * ptr = PyMem_Realloc(obj->b_ptr, size);
		if (ptr == NULL)
			return PyErr_NoMemory();
		obj->b_ptr = ptr;
		obj->b_size = size;
	}
  done:
	Py_INCREF(Py_None);
	return Py_None;
}

PyMethodDef module_methods[] = {
	{"resize", resize, METH_VARARGS, "Resize the memory buffer of a ctypes instance"},
#ifdef CTYPES_UNICODE
	{"set_conversion_mode", set_conversion_mode, METH_VARARGS, set_conversion_mode_doc},
#endif
#ifdef MS_WIN32
	{"CopyComPointer", copy_com_pointer, METH_VARARGS, copy_com_pointer_doc},
	{"FormatError", format_error, METH_VARARGS, format_error_doc},
	{"LoadLibrary", load_library, METH_VARARGS, load_library_doc},
	{"FreeLibrary", free_library, METH_VARARGS, free_library_doc},
	{"call_commethod", call_commethod, METH_VARARGS },
	{"_check_HRESULT", check_hresult, METH_VARARGS},
#else
	{"dlopen", py_dl_open, METH_VARARGS,
	 "dlopen(name, flag={RTLD_GLOBAL|RTLD_LOCAL}) open a shared library"},
	{"dlclose", py_dl_close, METH_VARARGS, "dlclose a library"},
	{"dlsym", py_dl_sym, METH_VARARGS, "find symbol in shared library"},
#endif
	{"alignment", align_func, METH_O, alignment_doc},
	{"sizeof", sizeof_func, METH_O, sizeof_doc},
	{"byref", byref, METH_O, byref_doc},
	{"addressof", addressof, METH_O, addressof_doc},
	{"call_function", call_function, METH_VARARGS },
	{"call_cdeclfunction", call_cdeclfunction, METH_VARARGS },
	{"PyObj_FromPtr", My_PyObj_FromPtr, METH_VARARGS },
	{"Py_INCREF", My_Py_INCREF, METH_O },
	{"Py_DECREF", My_Py_DECREF, METH_O },
	{NULL,      NULL}        /* Sentinel */
};

/*
 Local Variables:
 compile-command: "cd .. && python setup.py -q build -g && python setup.py -q build install --home ~"
 End:
*/
