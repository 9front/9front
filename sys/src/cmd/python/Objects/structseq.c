/* Implementation helper: a struct that looks like a tuple.  See timemodule
   and posixmodule for example uses. */

#include "Python.h"
#include "structmember.h"
#include "structseq.h"

static char visible_length_key[] = "n_sequence_fields";
static char real_length_key[] = "n_fields";
static char unnamed_fields_key[] = "n_unnamed_fields";

/* Fields with this name have only a field index, not a field name. 
   They are only allowed for indices < n_visible_fields. */
char *PyStructSequence_UnnamedField = "unnamed field";

#define VISIBLE_SIZE(op) ((op)->ob_size)
#define VISIBLE_SIZE_TP(tp) PyInt_AsLong( \
                      PyDict_GetItemString((tp)->tp_dict, visible_length_key))

#define REAL_SIZE_TP(tp) PyInt_AsLong( \
                      PyDict_GetItemString((tp)->tp_dict, real_length_key))
#define REAL_SIZE(op) REAL_SIZE_TP((op)->ob_type)

#define UNNAMED_FIELDS_TP(tp) PyInt_AsLong( \
                      PyDict_GetItemString((tp)->tp_dict, unnamed_fields_key))
#define UNNAMED_FIELDS(op) UNNAMED_FIELDS_TP((op)->ob_type)


PyObject *
PyStructSequence_New(PyTypeObject *type)
{
	PyStructSequence *obj;
       
	obj = PyObject_New(PyStructSequence, type);
	obj->ob_size = VISIBLE_SIZE_TP(type);

	return (PyObject*) obj;
}

static void
structseq_dealloc(PyStructSequence *obj)
{
	Py_ssize_t i, size;

	size = REAL_SIZE(obj);
	for (i = 0; i < size; ++i) {
		Py_XDECREF(obj->ob_item[i]);
	}
	PyObject_Del(obj);
}

static Py_ssize_t
structseq_length(PyStructSequence *obj)
{
	return VISIBLE_SIZE(obj);
}

static PyObject*
structseq_item(PyStructSequence *obj, Py_ssize_t i)
{
	if (i < 0 || i >= VISIBLE_SIZE(obj)) {
		PyErr_SetString(PyExc_IndexError, "tuple index out of range");
		return NULL;
	}
	Py_INCREF(obj->ob_item[i]);
	return obj->ob_item[i];
}

static PyObject*
structseq_slice(PyStructSequence *obj, Py_ssize_t low, Py_ssize_t high)
{
	PyTupleObject *np;
	Py_ssize_t i;

	if (low < 0)
		low = 0;
	if (high > VISIBLE_SIZE(obj))
		high = VISIBLE_SIZE(obj);
	if (high < low)
		high = low;
	np = (PyTupleObject *)PyTuple_New(high-low);
	if (np == NULL)
		return NULL;
	for(i = low; i < high; ++i) {
		PyObject *v = obj->ob_item[i];
		Py_INCREF(v);
		PyTuple_SET_ITEM(np, i-low, v);
	}
	return (PyObject *) np;
}

static PyObject *
structseq_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	PyObject *arg = NULL;
	PyObject *dict = NULL;
	PyObject *ob;
	PyStructSequence *res = NULL;
	Py_ssize_t len, min_len, max_len, i, n_unnamed_fields;
	static char *kwlist[] = {"sequence", "dict", 0};

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|O:structseq", 
					 kwlist, &arg, &dict))
		return NULL;

	arg = PySequence_Fast(arg, "constructor requires a sequence");

	if (!arg) {				
		return NULL;
	}

	if (dict && !PyDict_Check(dict)) {
		PyErr_Format(PyExc_TypeError, 
			     "%.500s() takes a dict as second arg, if any",
			     type->tp_name);
		Py_DECREF(arg);
		return NULL;
	}

	len = PySequence_Fast_GET_SIZE(arg);
	min_len = VISIBLE_SIZE_TP(type);
	max_len = REAL_SIZE_TP(type);
	n_unnamed_fields = UNNAMED_FIELDS_TP(type);

	if (min_len != max_len) {
		if (len < min_len) {
			PyErr_Format(PyExc_TypeError, 
	       "%.500s() takes an at least %zd-sequence (%zd-sequence given)",
				     type->tp_name, min_len, len);
			Py_DECREF(arg);
			return NULL;
		}

		if (len > max_len) {
			PyErr_Format(PyExc_TypeError, 
	       "%.500s() takes an at most %zd-sequence (%zd-sequence given)",
				     type->tp_name, max_len, len);
			Py_DECREF(arg);
			return NULL;
		}
	} 
	else {
		if (len != min_len) {
			PyErr_Format(PyExc_TypeError, 
	       "%.500s() takes a %zd-sequence (%zd-sequence given)",
				     type->tp_name, min_len, len);
			Py_DECREF(arg);
			return NULL;
		}
	}

	res = (PyStructSequence*) PyStructSequence_New(type);
	if (res == NULL) {
		return NULL;
	}
	for (i = 0; i < len; ++i) {
		PyObject *v = PySequence_Fast_GET_ITEM(arg, i);
		Py_INCREF(v);
		res->ob_item[i] = v;
	}
	for (; i < max_len; ++i) {
		if (dict && (ob = PyDict_GetItemString(
			dict, type->tp_members[i-n_unnamed_fields].name))) {
		}
		else {
			ob = Py_None;
		}
		Py_INCREF(ob);
		res->ob_item[i] = ob;
	}
	
	Py_DECREF(arg);
	return (PyObject*) res;
}

static PyObject *
make_tuple(PyStructSequence *obj)
{
	return structseq_slice(obj, 0, VISIBLE_SIZE(obj));
}

static PyObject *
structseq_repr(PyStructSequence *obj)
{
	PyObject *tup, *str;
	tup = make_tuple(obj);
	str = PyObject_Repr(tup);
	Py_DECREF(tup);
	return str;
}

static PyObject *
structseq_concat(PyStructSequence *obj, PyObject *b)
{
	PyObject *tup, *result;
	tup = make_tuple(obj);
	result = PySequence_Concat(tup, b);
	Py_DECREF(tup);
	return result;
}

static PyObject *
structseq_repeat(PyStructSequence *obj, Py_ssize_t n)
{
	PyObject *tup, *result;
	tup = make_tuple(obj);
	result = PySequence_Repeat(tup, n);
	Py_DECREF(tup);
	return result;
}

static int
structseq_contains(PyStructSequence *obj, PyObject *o)
{
	PyObject *tup;
	int result;
	tup = make_tuple(obj);
	if (!tup)
		return -1;
	result = PySequence_Contains(tup, o);
	Py_DECREF(tup);
	return result;
}

static long
structseq_hash(PyObject *obj)
{
	PyObject *tup;
	long result;
	tup = make_tuple((PyStructSequence*) obj);
	if (!tup)
		return -1;
	result = PyObject_Hash(tup);
	Py_DECREF(tup);
	return result;
}

static PyObject *
structseq_richcompare(PyObject *obj, PyObject *o2, int op)
{
	PyObject *tup, *result;
	tup = make_tuple((PyStructSequence*) obj);
	result = PyObject_RichCompare(tup, o2, op);
	Py_DECREF(tup);
	return result;
}

static PyObject *
structseq_reduce(PyStructSequence* self)
{
	PyObject* tup;
	PyObject* dict;
	PyObject* result;
	Py_ssize_t n_fields, n_visible_fields, n_unnamed_fields;
	int i;
	
	n_fields = REAL_SIZE(self);
	n_visible_fields = VISIBLE_SIZE(self);
	n_unnamed_fields = UNNAMED_FIELDS(self);
	tup = PyTuple_New(n_visible_fields);
	if (!tup) {
		return NULL;
	}

	dict = PyDict_New();
	if (!dict) {
		Py_DECREF(tup);
		return NULL;
	}

	for (i = 0; i < n_visible_fields; i++) {
		Py_INCREF(self->ob_item[i]);
		PyTuple_SET_ITEM(tup, i, self->ob_item[i]);
	}
	
	for (; i < n_fields; i++) {
		char *n = self->ob_type->tp_members[i-n_unnamed_fields].name;
		PyDict_SetItemString(dict, n,
				     self->ob_item[i]);
	}

	result = Py_BuildValue("(O(OO))", self->ob_type, tup, dict);

	Py_DECREF(tup);
	Py_DECREF(dict);

	return result;
}

static PySequenceMethods structseq_as_sequence = {
	(lenfunc)structseq_length,
	(binaryfunc)structseq_concat,           /* sq_concat */
	(ssizeargfunc)structseq_repeat,         /* sq_repeat */
	(ssizeargfunc)structseq_item,		/* sq_item */
	(ssizessizeargfunc)structseq_slice,	/* sq_slice */
	0,					/* sq_ass_item */
	0,					/* sq_ass_slice */
	(objobjproc)structseq_contains,	        /* sq_contains */
};

static PyMethodDef structseq_methods[] = {
	{"__reduce__", (PyCFunction)structseq_reduce, 
	 METH_NOARGS, NULL},
	{NULL, NULL}
};

static PyTypeObject _struct_sequence_template = {
	PyObject_HEAD_INIT(&PyType_Type)
	0,					/* ob_size */
	NULL,	                     		/* tp_name */
        0,		                        /* tp_basicsize */
	0,	                      		/* tp_itemsize */
	(destructor)structseq_dealloc,	        /* tp_dealloc */
	0,                        	        /* tp_print */
	0,			 		/* tp_getattr */
	0,					/* tp_setattr */
	0,               			/* tp_compare */
	(reprfunc)structseq_repr,             	/* tp_repr */
	0,					/* tp_as_number */
	&structseq_as_sequence,			/* tp_as_sequence */
	0,					/* tp_as_mapping */
	structseq_hash,				/* tp_hash */
	0,              			/* tp_call */
	0,					/* tp_str */
	0,                       		/* tp_getattro */
	0,	                           	/* tp_setattro */
	0,					/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,                     /* tp_flags */
	NULL,	 		         	/* tp_doc */
	0,					/* tp_traverse */
	0,					/* tp_clear */
	structseq_richcompare,			/* tp_richcompare */
	0,					/* tp_weaklistoffset */
	0,					/* tp_iter */
	0,					/* tp_iternext */
	structseq_methods,      		/* tp_methods */
        NULL,			             	/* tp_members */
	0,			          	/* tp_getset */
	0,					/* tp_base */
	0,					/* tp_dict */
	0,					/* tp_descr_get */
	0,					/* tp_descr_set */
	0,	                                /* tp_dictoffset */
	0,					/* tp_init */
	0,					/* tp_alloc */
	structseq_new,				/* tp_new */
};

void
PyStructSequence_InitType(PyTypeObject *type, PyStructSequence_Desc *desc)
{
	PyObject *dict;
	PyMemberDef* members;
	int n_members, n_unnamed_members, i, k;

#ifdef Py_TRACE_REFS
	/* if the type object was chained, unchain it first
	   before overwriting its storage */
	if (type->_ob_next) {
		_Py_ForgetReference((PyObject*)type);
	}
#endif

	n_unnamed_members = 0;
	for (i = 0; desc->fields[i].name != NULL; ++i)
		if (desc->fields[i].name == PyStructSequence_UnnamedField)
			n_unnamed_members++;
	n_members = i;

	memcpy(type, &_struct_sequence_template, sizeof(PyTypeObject));
	type->tp_name = desc->name;
	type->tp_doc = desc->doc;
	type->tp_basicsize = sizeof(PyStructSequence)+
		sizeof(PyObject*)*(n_members-1);
	type->tp_itemsize = 0;

	members = PyMem_NEW(PyMemberDef, n_members-n_unnamed_members+1);
	if (members == NULL)
		return;
	
	for (i = k = 0; i < n_members; ++i) {
		if (desc->fields[i].name == PyStructSequence_UnnamedField)
			continue;
		members[k].name = desc->fields[i].name;
		members[k].type = T_OBJECT;
		members[k].offset = offsetof(PyStructSequence, ob_item)
		  + i * sizeof(PyObject*);
		members[k].flags = READONLY;
		members[k].doc = desc->fields[i].doc;
		k++;
	}
	members[k].name = NULL;

	type->tp_members = members;

	if (PyType_Ready(type) < 0)
		return;
	Py_INCREF(type);

	dict = type->tp_dict;
	PyDict_SetItemString(dict, visible_length_key, 
		       PyInt_FromLong((long) desc->n_in_sequence));
	PyDict_SetItemString(dict, real_length_key, 
		       PyInt_FromLong((long) n_members));
	PyDict_SetItemString(dict, unnamed_fields_key, 
		       PyInt_FromLong((long) n_unnamed_members));
}
