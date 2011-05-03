
#include "Python.h"
#include "structmember.h"

/* Itertools module written and maintained 
   by Raymond D. Hettinger <python@rcn.com>
   Copyright (c) 2003 Python Software Foundation.
   All rights reserved.
*/


/* groupby object ***********************************************************/

typedef struct {
	PyObject_HEAD
	PyObject *it;
	PyObject *keyfunc;
	PyObject *tgtkey;
	PyObject *currkey;
	PyObject *currvalue;
} groupbyobject;

static PyTypeObject groupby_type;
static PyObject *_grouper_create(groupbyobject *, PyObject *);

static PyObject *
groupby_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	static char *kwargs[] = {"iterable", "key", NULL};
	groupbyobject *gbo;
 	PyObject *it, *keyfunc = Py_None;
 
 	if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|O:groupby", kwargs,
					 &it, &keyfunc))
		return NULL;

	gbo = (groupbyobject *)type->tp_alloc(type, 0);
	if (gbo == NULL)
		return NULL;
	gbo->tgtkey = NULL;
	gbo->currkey = NULL;
	gbo->currvalue = NULL;
	gbo->keyfunc = keyfunc;
	Py_INCREF(keyfunc);
	gbo->it = PyObject_GetIter(it);
	if (gbo->it == NULL) {
		Py_DECREF(gbo);
		return NULL;
	}
	return (PyObject *)gbo;
}

static void
groupby_dealloc(groupbyobject *gbo)
{
	PyObject_GC_UnTrack(gbo);
	Py_XDECREF(gbo->it);
	Py_XDECREF(gbo->keyfunc);
	Py_XDECREF(gbo->tgtkey);
	Py_XDECREF(gbo->currkey);
	Py_XDECREF(gbo->currvalue);
	gbo->ob_type->tp_free(gbo);
}

static int
groupby_traverse(groupbyobject *gbo, visitproc visit, void *arg)
{
	Py_VISIT(gbo->it);
	Py_VISIT(gbo->keyfunc);
	Py_VISIT(gbo->tgtkey);
	Py_VISIT(gbo->currkey);
	Py_VISIT(gbo->currvalue);
	return 0;
}

static PyObject *
groupby_next(groupbyobject *gbo)
{
	PyObject *newvalue, *newkey, *r, *grouper, *tmp;

	/* skip to next iteration group */
	for (;;) {
		if (gbo->currkey == NULL)
			/* pass */;
		else if (gbo->tgtkey == NULL)
			break;
		else {
			int rcmp;

			rcmp = PyObject_RichCompareBool(gbo->tgtkey,
							gbo->currkey, Py_EQ);
			if (rcmp == -1)
				return NULL;
			else if (rcmp == 0)
				break;
		}

		newvalue = PyIter_Next(gbo->it);
		if (newvalue == NULL)
			return NULL;

		if (gbo->keyfunc == Py_None) {
			newkey = newvalue;
			Py_INCREF(newvalue);
		} else {
			newkey = PyObject_CallFunctionObjArgs(gbo->keyfunc,
							      newvalue, NULL);
			if (newkey == NULL) {
				Py_DECREF(newvalue);
				return NULL;
			}
		}

		tmp = gbo->currkey;
		gbo->currkey = newkey;
		Py_XDECREF(tmp);

		tmp = gbo->currvalue;
		gbo->currvalue = newvalue;
		Py_XDECREF(tmp);
	}

	Py_INCREF(gbo->currkey);
	tmp = gbo->tgtkey;
	gbo->tgtkey = gbo->currkey;
	Py_XDECREF(tmp);

	grouper = _grouper_create(gbo, gbo->tgtkey);
	if (grouper == NULL)
		return NULL;

	r = PyTuple_Pack(2, gbo->currkey, grouper);
	Py_DECREF(grouper);
	return r;
}

PyDoc_STRVAR(groupby_doc,
"groupby(iterable[, keyfunc]) -> create an iterator which returns\n\
(key, sub-iterator) grouped by each value of key(value).\n");

static PyTypeObject groupby_type = {
	PyObject_HEAD_INIT(NULL)
	0,				/* ob_size */
	"itertools.groupby",		/* tp_name */
	sizeof(groupbyobject),		/* tp_basicsize */
	0,				/* tp_itemsize */
	/* methods */
	(destructor)groupby_dealloc,	/* tp_dealloc */
	0,				/* tp_print */
	0,				/* tp_getattr */
	0,				/* tp_setattr */
	0,				/* tp_compare */
	0,				/* tp_repr */
	0,				/* tp_as_number */
	0,				/* tp_as_sequence */
	0,				/* tp_as_mapping */
	0,				/* tp_hash */
	0,				/* tp_call */
	0,				/* tp_str */
	PyObject_GenericGetAttr,	/* tp_getattro */
	0,				/* tp_setattro */
	0,				/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
		Py_TPFLAGS_BASETYPE,	/* tp_flags */
	groupby_doc,			/* tp_doc */
	(traverseproc)groupby_traverse,	/* tp_traverse */
	0,				/* tp_clear */
	0,				/* tp_richcompare */
	0,				/* tp_weaklistoffset */
	PyObject_SelfIter,		/* tp_iter */
	(iternextfunc)groupby_next,	/* tp_iternext */
	0,				/* tp_methods */
	0,				/* tp_members */
	0,				/* tp_getset */
	0,				/* tp_base */
	0,				/* tp_dict */
	0,				/* tp_descr_get */
	0,				/* tp_descr_set */
	0,				/* tp_dictoffset */
	0,				/* tp_init */
	0,				/* tp_alloc */
	groupby_new,			/* tp_new */
	PyObject_GC_Del,		/* tp_free */
};


/* _grouper object (internal) ************************************************/

typedef struct {
	PyObject_HEAD
	PyObject *parent;
	PyObject *tgtkey;
} _grouperobject;

static PyTypeObject _grouper_type;

static PyObject *
_grouper_create(groupbyobject *parent, PyObject *tgtkey)
{
	_grouperobject *igo;

	igo = PyObject_New(_grouperobject, &_grouper_type);
	if (igo == NULL)
		return NULL;
	igo->parent = (PyObject *)parent;
	Py_INCREF(parent);
	igo->tgtkey = tgtkey;
	Py_INCREF(tgtkey);

	return (PyObject *)igo;
}

static void
_grouper_dealloc(_grouperobject *igo)
{
	Py_DECREF(igo->parent);
	Py_DECREF(igo->tgtkey);
	PyObject_Del(igo);
}

static PyObject *
_grouper_next(_grouperobject *igo)
{
	groupbyobject *gbo = (groupbyobject *)igo->parent;
	PyObject *newvalue, *newkey, *r;
	int rcmp;

	if (gbo->currvalue == NULL) {
		newvalue = PyIter_Next(gbo->it);
		if (newvalue == NULL)
			return NULL;

		if (gbo->keyfunc == Py_None) {
			newkey = newvalue;
			Py_INCREF(newvalue);
		} else {
			newkey = PyObject_CallFunctionObjArgs(gbo->keyfunc,
							      newvalue, NULL);
			if (newkey == NULL) {
				Py_DECREF(newvalue);
				return NULL;
			}
		}

		assert(gbo->currkey == NULL);
		gbo->currkey = newkey;
		gbo->currvalue = newvalue;
	}

	assert(gbo->currkey != NULL);
	rcmp = PyObject_RichCompareBool(igo->tgtkey, gbo->currkey, Py_EQ);
	if (rcmp <= 0)
		/* got any error or current group is end */
		return NULL;

	r = gbo->currvalue;
	gbo->currvalue = NULL;
	Py_CLEAR(gbo->currkey);

	return r;
}

static PyTypeObject _grouper_type = {
	PyObject_HEAD_INIT(NULL)
	0,				/* ob_size */
	"itertools._grouper",		/* tp_name */
	sizeof(_grouperobject),		/* tp_basicsize */
	0,				/* tp_itemsize */
	/* methods */
	(destructor)_grouper_dealloc,	/* tp_dealloc */
	0,				/* tp_print */
	0,				/* tp_getattr */
	0,				/* tp_setattr */
	0,				/* tp_compare */
	0,				/* tp_repr */
	0,				/* tp_as_number */
	0,				/* tp_as_sequence */
	0,				/* tp_as_mapping */
	0,				/* tp_hash */
	0,				/* tp_call */
	0,				/* tp_str */
	PyObject_GenericGetAttr,	/* tp_getattro */
	0,				/* tp_setattro */
	0,				/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,		/* tp_flags */
	0,				/* tp_doc */
	0, 				/* tp_traverse */
	0,				/* tp_clear */
	0,				/* tp_richcompare */
	0,				/* tp_weaklistoffset */
	PyObject_SelfIter,		/* tp_iter */
	(iternextfunc)_grouper_next,	/* tp_iternext */
	0,				/* tp_methods */
	0,				/* tp_members */
	0,				/* tp_getset */
	0,				/* tp_base */
	0,				/* tp_dict */
	0,				/* tp_descr_get */
	0,				/* tp_descr_set */
	0,				/* tp_dictoffset */
	0,				/* tp_init */
	0,				/* tp_alloc */
	0,				/* tp_new */
	PyObject_Del,			/* tp_free */
};

 

/* tee object and with supporting function and objects ***************/

/* The teedataobject pre-allocates space for LINKCELLS number of objects.
   To help the object fit neatly inside cache lines (space for 16 to 32
   pointers), the value should be a multiple of 16 minus  space for 
   the other structure members including PyHEAD overhead.  The larger the
   value, the less memory overhead per object and the less time spent
   allocating/deallocating new links.  The smaller the number, the less
   wasted space and the more rapid freeing of older data.
*/
#define LINKCELLS 57

typedef struct {
	PyObject_HEAD
	PyObject *it;
	int numread;
	PyObject *nextlink;
	PyObject *(values[LINKCELLS]);
} teedataobject;

typedef struct {
	PyObject_HEAD
	teedataobject *dataobj;
	int index;
	PyObject *weakreflist;
} teeobject;

static PyTypeObject teedataobject_type;

static PyObject *
teedataobject_new(PyObject *it)
{
	teedataobject *tdo;

	tdo = PyObject_GC_New(teedataobject, &teedataobject_type);
	if (tdo == NULL)
		return NULL;

	tdo->numread = 0;
	tdo->nextlink = NULL;
	Py_INCREF(it);
	tdo->it = it;
	PyObject_GC_Track(tdo);
	return (PyObject *)tdo;
}

static PyObject *
teedataobject_jumplink(teedataobject *tdo)
{
	if (tdo->nextlink == NULL)
		tdo->nextlink = teedataobject_new(tdo->it);
	Py_XINCREF(tdo->nextlink);
	return tdo->nextlink;
}

static PyObject *
teedataobject_getitem(teedataobject *tdo, int i)
{
	PyObject *value;

	assert(i < LINKCELLS);
	if (i < tdo->numread)
		value = tdo->values[i];
	else {
		/* this is the lead iterator, so fetch more data */
		assert(i == tdo->numread);
		value = PyIter_Next(tdo->it);
		if (value == NULL)
			return NULL;
		tdo->numread++;
		tdo->values[i] = value;
	}
	Py_INCREF(value);
	return value;
}

static int
teedataobject_traverse(teedataobject *tdo, visitproc visit, void * arg)
{
	int i;
	Py_VISIT(tdo->it);
	for (i = 0; i < tdo->numread; i++)
		Py_VISIT(tdo->values[i]);
	Py_VISIT(tdo->nextlink);
	return 0;
}

static int
teedataobject_clear(teedataobject *tdo)
{
	int i;
	Py_CLEAR(tdo->it);
	for (i=0 ; i<tdo->numread ; i++)
		Py_CLEAR(tdo->values[i]);
	Py_CLEAR(tdo->nextlink);
	return 0;
}

static void
teedataobject_dealloc(teedataobject *tdo)
{
	PyObject_GC_UnTrack(tdo);
	teedataobject_clear(tdo);
	PyObject_GC_Del(tdo);
}

PyDoc_STRVAR(teedataobject_doc, "Data container common to multiple tee objects.");

static PyTypeObject teedataobject_type = {
	PyObject_HEAD_INIT(0)	/* Must fill in type value later */
	0,					/* ob_size */
	"itertools.tee_dataobject",		/* tp_name */
	sizeof(teedataobject),			/* tp_basicsize */
	0,					/* tp_itemsize */
	/* methods */
	(destructor)teedataobject_dealloc,	/* tp_dealloc */
	0,					/* tp_print */
	0,					/* tp_getattr */
	0,					/* tp_setattr */
	0,					/* tp_compare */
	0,					/* tp_repr */
	0,					/* tp_as_number */
	0,					/* tp_as_sequence */
	0,					/* tp_as_mapping */
	0,					/* tp_hash */
	0,					/* tp_call */
	0,					/* tp_str */
	PyObject_GenericGetAttr,		/* tp_getattro */
	0,					/* tp_setattro */
	0,					/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,	/* tp_flags */
	teedataobject_doc,			/* tp_doc */
	(traverseproc)teedataobject_traverse,	/* tp_traverse */
	(inquiry)teedataobject_clear,		/* tp_clear */
	0,					/* tp_richcompare */
	0,					/* tp_weaklistoffset */
	0,					/* tp_iter */
	0,					/* tp_iternext */
	0,					/* tp_methods */
	0,					/* tp_members */
	0,					/* tp_getset */
	0,					/* tp_base */
	0,					/* tp_dict */
	0,					/* tp_descr_get */
	0,					/* tp_descr_set */
	0,					/* tp_dictoffset */
	0,					/* tp_init */
	0,					/* tp_alloc */
	0,					/* tp_new */
	PyObject_GC_Del,			/* tp_free */
};


static PyTypeObject tee_type;

static PyObject *
tee_next(teeobject *to)
{
	PyObject *value, *link;

	if (to->index >= LINKCELLS) {
		link = teedataobject_jumplink(to->dataobj);
		Py_DECREF(to->dataobj);
		to->dataobj = (teedataobject *)link;
		to->index = 0;
	}
	value = teedataobject_getitem(to->dataobj, to->index);
	if (value == NULL)
		return NULL;
	to->index++;
	return value;
}

static int
tee_traverse(teeobject *to, visitproc visit, void *arg)
{
	Py_VISIT((PyObject *)to->dataobj);
	return 0;
}

static PyObject *
tee_copy(teeobject *to)
{
	teeobject *newto;

	newto = PyObject_GC_New(teeobject, &tee_type);
	if (newto == NULL)
		return NULL;
	Py_INCREF(to->dataobj);
	newto->dataobj = to->dataobj;
	newto->index = to->index;
	newto->weakreflist = NULL;
	PyObject_GC_Track(newto);
	return (PyObject *)newto;
}

PyDoc_STRVAR(teecopy_doc, "Returns an independent iterator.");

static PyObject *
tee_fromiterable(PyObject *iterable)
{
	teeobject *to;
	PyObject *it = NULL;

	it = PyObject_GetIter(iterable);
	if (it == NULL)
		return NULL;
	if (PyObject_TypeCheck(it, &tee_type)) {
		to = (teeobject *)tee_copy((teeobject *)it);
		goto done;
	}

	to = PyObject_GC_New(teeobject, &tee_type);
	if (to == NULL) 
		goto done;
	to->dataobj = (teedataobject *)teedataobject_new(it);
	if (!to->dataobj) {
		PyObject_GC_Del(to);
		to = NULL;
		goto done;
	}

	to->index = 0;
	to->weakreflist = NULL;
	PyObject_GC_Track(to);
done:
	Py_XDECREF(it);
	return (PyObject *)to;
}

static PyObject *
tee_new(PyTypeObject *type, PyObject *args, PyObject *kw)
{
	PyObject *iterable;

	if (!PyArg_UnpackTuple(args, "tee", 1, 1, &iterable))
		return NULL;
	return tee_fromiterable(iterable);
}

static int
tee_clear(teeobject *to)
{
	if (to->weakreflist != NULL)
		PyObject_ClearWeakRefs((PyObject *) to);
	Py_CLEAR(to->dataobj);
	return 0;
}

static void
tee_dealloc(teeobject *to)
{
	PyObject_GC_UnTrack(to);
	tee_clear(to);
	PyObject_GC_Del(to);
}

PyDoc_STRVAR(teeobject_doc,
"Iterator wrapped to make it copyable");

static PyMethodDef tee_methods[] = {
	{"__copy__",	(PyCFunction)tee_copy,	METH_NOARGS, teecopy_doc},
 	{NULL,		NULL}		/* sentinel */
};

static PyTypeObject tee_type = {
	PyObject_HEAD_INIT(NULL)
	0,				/* ob_size */
	"itertools.tee",		/* tp_name */
	sizeof(teeobject),		/* tp_basicsize */
	0,				/* tp_itemsize */
	/* methods */
	(destructor)tee_dealloc,	/* tp_dealloc */
	0,				/* tp_print */
	0,				/* tp_getattr */
	0,				/* tp_setattr */
	0,				/* tp_compare */
	0,				/* tp_repr */
	0,				/* tp_as_number */
	0,				/* tp_as_sequence */
	0,				/* tp_as_mapping */
	0,				/* tp_hash */
	0,				/* tp_call */
	0,				/* tp_str */
	0,				/* tp_getattro */
	0,				/* tp_setattro */
	0,				/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,	/* tp_flags */
	teeobject_doc,			/* tp_doc */
	(traverseproc)tee_traverse,	/* tp_traverse */
	(inquiry)tee_clear,		/* tp_clear */
	0,				/* tp_richcompare */
	offsetof(teeobject, weakreflist),	/* tp_weaklistoffset */
	PyObject_SelfIter,		/* tp_iter */
	(iternextfunc)tee_next,		/* tp_iternext */
	tee_methods,			/* tp_methods */
	0,				/* tp_members */
	0,				/* tp_getset */
	0,				/* tp_base */
	0,				/* tp_dict */
	0,				/* tp_descr_get */
	0,				/* tp_descr_set */
	0,				/* tp_dictoffset */
	0,				/* tp_init */
	0,				/* tp_alloc */
	tee_new,			/* tp_new */
	PyObject_GC_Del,		/* tp_free */
};

static PyObject *
tee(PyObject *self, PyObject *args)
{
	Py_ssize_t i, n=2;
	PyObject *it, *iterable, *copyable, *result;

	if (!PyArg_ParseTuple(args, "O|n", &iterable, &n))
		return NULL;
	if (n < 0) {
		PyErr_SetString(PyExc_ValueError, "n must be >= 0");
		return NULL;
	}
	result = PyTuple_New(n);
	if (result == NULL)
		return NULL;
	if (n == 0)
		return result;
	it = PyObject_GetIter(iterable);
	if (it == NULL) {
		Py_DECREF(result);
		return NULL;
	}
	if (!PyObject_HasAttrString(it, "__copy__")) {
		copyable = tee_fromiterable(it);
		Py_DECREF(it);
		if (copyable == NULL) {
			Py_DECREF(result);
			return NULL;
		}
	} else
		copyable = it;
	PyTuple_SET_ITEM(result, 0, copyable);
	for (i=1 ; i<n ; i++) {
		copyable = PyObject_CallMethod(copyable, "__copy__", NULL);
		if (copyable == NULL) {
			Py_DECREF(result);
			return NULL;
		}
		PyTuple_SET_ITEM(result, i, copyable);
	}
	return result;
}

PyDoc_STRVAR(tee_doc,
"tee(iterable, n=2) --> tuple of n independent iterators.");


/* cycle object **********************************************************/

typedef struct {
	PyObject_HEAD
	PyObject *it;
	PyObject *saved;
	int firstpass;
} cycleobject;

static PyTypeObject cycle_type;

static PyObject *
cycle_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	PyObject *it;
	PyObject *iterable;
	PyObject *saved;
	cycleobject *lz;

	if (type == &cycle_type && !_PyArg_NoKeywords("cycle()", kwds))
		return NULL;

	if (!PyArg_UnpackTuple(args, "cycle", 1, 1, &iterable))
		return NULL;

	/* Get iterator. */
	it = PyObject_GetIter(iterable);
	if (it == NULL)
		return NULL;

	saved = PyList_New(0);
	if (saved == NULL) {
		Py_DECREF(it);
		return NULL;
	}

	/* create cycleobject structure */
	lz = (cycleobject *)type->tp_alloc(type, 0);
	if (lz == NULL) {
		Py_DECREF(it);
		Py_DECREF(saved);
		return NULL;
	}
	lz->it = it;
	lz->saved = saved;
	lz->firstpass = 0;

	return (PyObject *)lz;
}

static void
cycle_dealloc(cycleobject *lz)
{
	PyObject_GC_UnTrack(lz);
	Py_XDECREF(lz->saved);
	Py_XDECREF(lz->it);
	lz->ob_type->tp_free(lz);
}

static int
cycle_traverse(cycleobject *lz, visitproc visit, void *arg)
{
	Py_VISIT(lz->it);
	Py_VISIT(lz->saved);
	return 0;
}

static PyObject *
cycle_next(cycleobject *lz)
{
	PyObject *item;
	PyObject *it;
	PyObject *tmp;

	while (1) {
		item = PyIter_Next(lz->it);
		if (item != NULL) {
			if (!lz->firstpass)
				PyList_Append(lz->saved, item);
			return item;
		}
		if (PyErr_Occurred()) {
			if (PyErr_ExceptionMatches(PyExc_StopIteration))
				PyErr_Clear();
			else
				return NULL;
		}
		if (PyList_Size(lz->saved) == 0) 
			return NULL;
		it = PyObject_GetIter(lz->saved);
		if (it == NULL)
			return NULL;
		tmp = lz->it;
		lz->it = it;
		lz->firstpass = 1;
		Py_DECREF(tmp);
	}
}

PyDoc_STRVAR(cycle_doc,
"cycle(iterable) --> cycle object\n\
\n\
Return elements from the iterable until it is exhausted.\n\
Then repeat the sequence indefinitely.");

static PyTypeObject cycle_type = {
	PyObject_HEAD_INIT(NULL)
	0,				/* ob_size */
	"itertools.cycle",		/* tp_name */
	sizeof(cycleobject),		/* tp_basicsize */
	0,				/* tp_itemsize */
	/* methods */
	(destructor)cycle_dealloc,	/* tp_dealloc */
	0,				/* tp_print */
	0,				/* tp_getattr */
	0,				/* tp_setattr */
	0,				/* tp_compare */
	0,				/* tp_repr */
	0,				/* tp_as_number */
	0,				/* tp_as_sequence */
	0,				/* tp_as_mapping */
	0,				/* tp_hash */
	0,				/* tp_call */
	0,				/* tp_str */
	PyObject_GenericGetAttr,	/* tp_getattro */
	0,				/* tp_setattro */
	0,				/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
		Py_TPFLAGS_BASETYPE,	/* tp_flags */
	cycle_doc,			/* tp_doc */
	(traverseproc)cycle_traverse,	/* tp_traverse */
	0,				/* tp_clear */
	0,				/* tp_richcompare */
	0,				/* tp_weaklistoffset */
	PyObject_SelfIter,		/* tp_iter */
	(iternextfunc)cycle_next,	/* tp_iternext */
	0,				/* tp_methods */
	0,				/* tp_members */
	0,				/* tp_getset */
	0,				/* tp_base */
	0,				/* tp_dict */
	0,				/* tp_descr_get */
	0,				/* tp_descr_set */
	0,				/* tp_dictoffset */
	0,				/* tp_init */
	0,				/* tp_alloc */
	cycle_new,			/* tp_new */
	PyObject_GC_Del,		/* tp_free */
};


/* dropwhile object **********************************************************/

typedef struct {
	PyObject_HEAD
	PyObject *func;
	PyObject *it;
	long	 start;
} dropwhileobject;

static PyTypeObject dropwhile_type;

static PyObject *
dropwhile_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	PyObject *func, *seq;
	PyObject *it;
	dropwhileobject *lz;

	if (type == &dropwhile_type && !_PyArg_NoKeywords("dropwhile()", kwds))
		return NULL;

	if (!PyArg_UnpackTuple(args, "dropwhile", 2, 2, &func, &seq))
		return NULL;

	/* Get iterator. */
	it = PyObject_GetIter(seq);
	if (it == NULL)
		return NULL;

	/* create dropwhileobject structure */
	lz = (dropwhileobject *)type->tp_alloc(type, 0);
	if (lz == NULL) {
		Py_DECREF(it);
		return NULL;
	}
	Py_INCREF(func);
	lz->func = func;
	lz->it = it;
	lz->start = 0;

	return (PyObject *)lz;
}

static void
dropwhile_dealloc(dropwhileobject *lz)
{
	PyObject_GC_UnTrack(lz);
	Py_XDECREF(lz->func);
	Py_XDECREF(lz->it);
	lz->ob_type->tp_free(lz);
}

static int
dropwhile_traverse(dropwhileobject *lz, visitproc visit, void *arg)
{
	Py_VISIT(lz->it);
	Py_VISIT(lz->func);
	return 0;
}

static PyObject *
dropwhile_next(dropwhileobject *lz)
{
	PyObject *item, *good;
	PyObject *it = lz->it;
	long ok;
	PyObject *(*iternext)(PyObject *);

	assert(PyIter_Check(it));
	iternext = *it->ob_type->tp_iternext;
	for (;;) {
		item = iternext(it);
		if (item == NULL)
			return NULL;
		if (lz->start == 1)
			return item;

		good = PyObject_CallFunctionObjArgs(lz->func, item, NULL);
		if (good == NULL) {
			Py_DECREF(item);
			return NULL;
		}
		ok = PyObject_IsTrue(good);
		Py_DECREF(good);
		if (!ok) {
			lz->start = 1;
			return item;
		}
		Py_DECREF(item);
	}
}

PyDoc_STRVAR(dropwhile_doc,
"dropwhile(predicate, iterable) --> dropwhile object\n\
\n\
Drop items from the iterable while predicate(item) is true.\n\
Afterwards, return every element until the iterable is exhausted.");

static PyTypeObject dropwhile_type = {
	PyObject_HEAD_INIT(NULL)
	0,				/* ob_size */
	"itertools.dropwhile",		/* tp_name */
	sizeof(dropwhileobject),	/* tp_basicsize */
	0,				/* tp_itemsize */
	/* methods */
	(destructor)dropwhile_dealloc,	/* tp_dealloc */
	0,				/* tp_print */
	0,				/* tp_getattr */
	0,				/* tp_setattr */
	0,				/* tp_compare */
	0,				/* tp_repr */
	0,				/* tp_as_number */
	0,				/* tp_as_sequence */
	0,				/* tp_as_mapping */
	0,				/* tp_hash */
	0,				/* tp_call */
	0,				/* tp_str */
	PyObject_GenericGetAttr,	/* tp_getattro */
	0,				/* tp_setattro */
	0,				/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
		Py_TPFLAGS_BASETYPE,	/* tp_flags */
	dropwhile_doc,			/* tp_doc */
	(traverseproc)dropwhile_traverse,    /* tp_traverse */
	0,				/* tp_clear */
	0,				/* tp_richcompare */
	0,				/* tp_weaklistoffset */
	PyObject_SelfIter,		/* tp_iter */
	(iternextfunc)dropwhile_next,	/* tp_iternext */
	0,				/* tp_methods */
	0,				/* tp_members */
	0,				/* tp_getset */
	0,				/* tp_base */
	0,				/* tp_dict */
	0,				/* tp_descr_get */
	0,				/* tp_descr_set */
	0,				/* tp_dictoffset */
	0,				/* tp_init */
	0,				/* tp_alloc */
	dropwhile_new,			/* tp_new */
	PyObject_GC_Del,		/* tp_free */
};


/* takewhile object **********************************************************/

typedef struct {
	PyObject_HEAD
	PyObject *func;
	PyObject *it;
	long	 stop;
} takewhileobject;

static PyTypeObject takewhile_type;

static PyObject *
takewhile_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	PyObject *func, *seq;
	PyObject *it;
	takewhileobject *lz;

	if (type == &takewhile_type && !_PyArg_NoKeywords("takewhile()", kwds))
		return NULL;

	if (!PyArg_UnpackTuple(args, "takewhile", 2, 2, &func, &seq))
		return NULL;

	/* Get iterator. */
	it = PyObject_GetIter(seq);
	if (it == NULL)
		return NULL;

	/* create takewhileobject structure */
	lz = (takewhileobject *)type->tp_alloc(type, 0);
	if (lz == NULL) {
		Py_DECREF(it);
		return NULL;
	}
	Py_INCREF(func);
	lz->func = func;
	lz->it = it;
	lz->stop = 0;

	return (PyObject *)lz;
}

static void
takewhile_dealloc(takewhileobject *lz)
{
	PyObject_GC_UnTrack(lz);
	Py_XDECREF(lz->func);
	Py_XDECREF(lz->it);
	lz->ob_type->tp_free(lz);
}

static int
takewhile_traverse(takewhileobject *lz, visitproc visit, void *arg)
{
	Py_VISIT(lz->it);
	Py_VISIT(lz->func);
	return 0;
}

static PyObject *
takewhile_next(takewhileobject *lz)
{
	PyObject *item, *good;
	PyObject *it = lz->it;
	long ok;

	if (lz->stop == 1)
		return NULL;

	assert(PyIter_Check(it));
	item = (*it->ob_type->tp_iternext)(it);
	if (item == NULL)
		return NULL;

	good = PyObject_CallFunctionObjArgs(lz->func, item, NULL);
	if (good == NULL) {
		Py_DECREF(item);
		return NULL;
	}
	ok = PyObject_IsTrue(good);
	Py_DECREF(good);
	if (ok)
		return item;
	Py_DECREF(item);
	lz->stop = 1;
	return NULL;
}

PyDoc_STRVAR(takewhile_doc,
"takewhile(predicate, iterable) --> takewhile object\n\
\n\
Return successive entries from an iterable as long as the \n\
predicate evaluates to true for each entry.");

static PyTypeObject takewhile_type = {
	PyObject_HEAD_INIT(NULL)
	0,				/* ob_size */
	"itertools.takewhile",		/* tp_name */
	sizeof(takewhileobject),	/* tp_basicsize */
	0,				/* tp_itemsize */
	/* methods */
	(destructor)takewhile_dealloc,	/* tp_dealloc */
	0,				/* tp_print */
	0,				/* tp_getattr */
	0,				/* tp_setattr */
	0,				/* tp_compare */
	0,				/* tp_repr */
	0,				/* tp_as_number */
	0,				/* tp_as_sequence */
	0,				/* tp_as_mapping */
	0,				/* tp_hash */
	0,				/* tp_call */
	0,				/* tp_str */
	PyObject_GenericGetAttr,	/* tp_getattro */
	0,				/* tp_setattro */
	0,				/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
		Py_TPFLAGS_BASETYPE,	/* tp_flags */
	takewhile_doc,			/* tp_doc */
	(traverseproc)takewhile_traverse,    /* tp_traverse */
	0,				/* tp_clear */
	0,				/* tp_richcompare */
	0,				/* tp_weaklistoffset */
	PyObject_SelfIter,		/* tp_iter */
	(iternextfunc)takewhile_next,	/* tp_iternext */
	0,				/* tp_methods */
	0,				/* tp_members */
	0,				/* tp_getset */
	0,				/* tp_base */
	0,				/* tp_dict */
	0,				/* tp_descr_get */
	0,				/* tp_descr_set */
	0,				/* tp_dictoffset */
	0,				/* tp_init */
	0,				/* tp_alloc */
	takewhile_new,			/* tp_new */
	PyObject_GC_Del,		/* tp_free */
};


/* islice object ************************************************************/

typedef struct {
	PyObject_HEAD
	PyObject *it;
	Py_ssize_t next;
	Py_ssize_t stop;
	Py_ssize_t step;
	Py_ssize_t cnt;
} isliceobject;

static PyTypeObject islice_type;

static PyObject *
islice_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	PyObject *seq;
	Py_ssize_t start=0, stop=-1, step=1;
	PyObject *it, *a1=NULL, *a2=NULL, *a3=NULL;
	Py_ssize_t numargs;
	isliceobject *lz;

	if (type == &islice_type && !_PyArg_NoKeywords("islice()", kwds))
		return NULL;

	if (!PyArg_UnpackTuple(args, "islice", 2, 4, &seq, &a1, &a2, &a3))
		return NULL;

	numargs = PyTuple_Size(args);
	if (numargs == 2) {
		if (a1 != Py_None) {
			stop = PyInt_AsSsize_t(a1);
			if (stop == -1) {
				if (PyErr_Occurred())
					PyErr_Clear();
				PyErr_SetString(PyExc_ValueError,
				   "Stop argument for islice() must be a non-negative integer or None.");
				return NULL;
			}
		}
	} else {
		if (a1 != Py_None)
			start = PyInt_AsSsize_t(a1);
		if (start == -1 && PyErr_Occurred())
			PyErr_Clear();
		if (a2 != Py_None) {
			stop = PyInt_AsSsize_t(a2);
			if (stop == -1) {
				if (PyErr_Occurred())
					PyErr_Clear();
				PyErr_SetString(PyExc_ValueError,
				   "Stop argument for islice() must be a non-negative integer or None.");
				return NULL;
			}
		}
	}
	if (start<0 || stop<-1) {
		PyErr_SetString(PyExc_ValueError,
		   "Indices for islice() must be non-negative integers or None.");
		return NULL;
	}

	if (a3 != NULL) {
		if (a3 != Py_None)
			step = PyInt_AsSsize_t(a3);
		if (step == -1 && PyErr_Occurred())
			PyErr_Clear();
	}
	if (step<1) {
		PyErr_SetString(PyExc_ValueError,
		   "Step for islice() must be a positive integer or None.");
		return NULL;
	}

	/* Get iterator. */
	it = PyObject_GetIter(seq);
	if (it == NULL)
		return NULL;

	/* create isliceobject structure */
	lz = (isliceobject *)type->tp_alloc(type, 0);
	if (lz == NULL) {
		Py_DECREF(it);
		return NULL;
	}
	lz->it = it;
	lz->next = start;
	lz->stop = stop;
	lz->step = step;
	lz->cnt = 0L;

	return (PyObject *)lz;
}

static void
islice_dealloc(isliceobject *lz)
{
	PyObject_GC_UnTrack(lz);
	Py_XDECREF(lz->it);
	lz->ob_type->tp_free(lz);
}

static int
islice_traverse(isliceobject *lz, visitproc visit, void *arg)
{
	Py_VISIT(lz->it);
	return 0;
}

static PyObject *
islice_next(isliceobject *lz)
{
	PyObject *item;
	PyObject *it = lz->it;
	Py_ssize_t oldnext;
	PyObject *(*iternext)(PyObject *);

	assert(PyIter_Check(it));
	iternext = *it->ob_type->tp_iternext;
	while (lz->cnt < lz->next) {
		item = iternext(it);
		if (item == NULL)
			return NULL;
		Py_DECREF(item);
		lz->cnt++;
	}
	if (lz->stop != -1 && lz->cnt >= lz->stop)
		return NULL;
	assert(PyIter_Check(it));
	item = iternext(it);
	if (item == NULL)
		return NULL;
	lz->cnt++;
	oldnext = lz->next;
	lz->next += lz->step;
	if (lz->next < oldnext)	/* Check for overflow */
		lz->next = lz->stop;
	return item;
}

PyDoc_STRVAR(islice_doc,
"islice(iterable, [start,] stop [, step]) --> islice object\n\
\n\
Return an iterator whose next() method returns selected values from an\n\
iterable.  If start is specified, will skip all preceding elements;\n\
otherwise, start defaults to zero.  Step defaults to one.  If\n\
specified as another value, step determines how many values are \n\
skipped between successive calls.  Works like a slice() on a list\n\
but returns an iterator.");

static PyTypeObject islice_type = {
	PyObject_HEAD_INIT(NULL)
	0,				/* ob_size */
	"itertools.islice",		/* tp_name */
	sizeof(isliceobject),		/* tp_basicsize */
	0,				/* tp_itemsize */
	/* methods */
	(destructor)islice_dealloc,	/* tp_dealloc */
	0,				/* tp_print */
	0,				/* tp_getattr */
	0,				/* tp_setattr */
	0,				/* tp_compare */
	0,				/* tp_repr */
	0,				/* tp_as_number */
	0,				/* tp_as_sequence */
	0,				/* tp_as_mapping */
	0,				/* tp_hash */
	0,				/* tp_call */
	0,				/* tp_str */
	PyObject_GenericGetAttr,	/* tp_getattro */
	0,				/* tp_setattro */
	0,				/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
		Py_TPFLAGS_BASETYPE,	/* tp_flags */
	islice_doc,			/* tp_doc */
	(traverseproc)islice_traverse,	/* tp_traverse */
	0,				/* tp_clear */
	0,				/* tp_richcompare */
	0,				/* tp_weaklistoffset */
	PyObject_SelfIter,		/* tp_iter */
	(iternextfunc)islice_next,	/* tp_iternext */
	0,				/* tp_methods */
	0,				/* tp_members */
	0,				/* tp_getset */
	0,				/* tp_base */
	0,				/* tp_dict */
	0,				/* tp_descr_get */
	0,				/* tp_descr_set */
	0,				/* tp_dictoffset */
	0,				/* tp_init */
	0,				/* tp_alloc */
	islice_new,			/* tp_new */
	PyObject_GC_Del,		/* tp_free */
};


/* starmap object ************************************************************/

typedef struct {
	PyObject_HEAD
	PyObject *func;
	PyObject *it;
} starmapobject;

static PyTypeObject starmap_type;

static PyObject *
starmap_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	PyObject *func, *seq;
	PyObject *it;
	starmapobject *lz;

	if (type == &starmap_type && !_PyArg_NoKeywords("starmap()", kwds))
		return NULL;

	if (!PyArg_UnpackTuple(args, "starmap", 2, 2, &func, &seq))
		return NULL;

	/* Get iterator. */
	it = PyObject_GetIter(seq);
	if (it == NULL)
		return NULL;

	/* create starmapobject structure */
	lz = (starmapobject *)type->tp_alloc(type, 0);
	if (lz == NULL) {
		Py_DECREF(it);
		return NULL;
	}
	Py_INCREF(func);
	lz->func = func;
	lz->it = it;

	return (PyObject *)lz;
}

static void
starmap_dealloc(starmapobject *lz)
{
	PyObject_GC_UnTrack(lz);
	Py_XDECREF(lz->func);
	Py_XDECREF(lz->it);
	lz->ob_type->tp_free(lz);
}

static int
starmap_traverse(starmapobject *lz, visitproc visit, void *arg)
{
	Py_VISIT(lz->it);
	Py_VISIT(lz->func);
	return 0;
}

static PyObject *
starmap_next(starmapobject *lz)
{
	PyObject *args;
	PyObject *result;
	PyObject *it = lz->it;

	assert(PyIter_Check(it));
	args = (*it->ob_type->tp_iternext)(it);
	if (args == NULL)
		return NULL;
	if (!PyTuple_CheckExact(args)) {
		Py_DECREF(args);
		PyErr_SetString(PyExc_TypeError,
				"iterator must return a tuple");
		return NULL;
	}
	result = PyObject_Call(lz->func, args, NULL);
	Py_DECREF(args);
	return result;
}

PyDoc_STRVAR(starmap_doc,
"starmap(function, sequence) --> starmap object\n\
\n\
Return an iterator whose values are returned from the function evaluated\n\
with a argument tuple taken from the given sequence.");

static PyTypeObject starmap_type = {
	PyObject_HEAD_INIT(NULL)
	0,				/* ob_size */
	"itertools.starmap",		/* tp_name */
	sizeof(starmapobject),		/* tp_basicsize */
	0,				/* tp_itemsize */
	/* methods */
	(destructor)starmap_dealloc,	/* tp_dealloc */
	0,				/* tp_print */
	0,				/* tp_getattr */
	0,				/* tp_setattr */
	0,				/* tp_compare */
	0,				/* tp_repr */
	0,				/* tp_as_number */
	0,				/* tp_as_sequence */
	0,				/* tp_as_mapping */
	0,				/* tp_hash */
	0,				/* tp_call */
	0,				/* tp_str */
	PyObject_GenericGetAttr,	/* tp_getattro */
	0,				/* tp_setattro */
	0,				/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
		Py_TPFLAGS_BASETYPE,	/* tp_flags */
	starmap_doc,			/* tp_doc */
	(traverseproc)starmap_traverse,	/* tp_traverse */
	0,				/* tp_clear */
	0,				/* tp_richcompare */
	0,				/* tp_weaklistoffset */
	PyObject_SelfIter,		/* tp_iter */
	(iternextfunc)starmap_next,	/* tp_iternext */
	0,				/* tp_methods */
	0,				/* tp_members */
	0,				/* tp_getset */
	0,				/* tp_base */
	0,				/* tp_dict */
	0,				/* tp_descr_get */
	0,				/* tp_descr_set */
	0,				/* tp_dictoffset */
	0,				/* tp_init */
	0,				/* tp_alloc */
	starmap_new,			/* tp_new */
	PyObject_GC_Del,		/* tp_free */
};


/* imap object ************************************************************/

typedef struct {
	PyObject_HEAD
	PyObject *iters;
	PyObject *func;
} imapobject;

static PyTypeObject imap_type;

static PyObject *
imap_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	PyObject *it, *iters, *func;
	imapobject *lz;
	Py_ssize_t numargs, i;

	if (type == &imap_type && !_PyArg_NoKeywords("imap()", kwds))
		return NULL;

	numargs = PyTuple_Size(args);
	if (numargs < 2) {
		PyErr_SetString(PyExc_TypeError,
		   "imap() must have at least two arguments.");
		return NULL;
	}

	iters = PyTuple_New(numargs-1);
	if (iters == NULL)
		return NULL;

	for (i=1 ; i<numargs ; i++) {
		/* Get iterator. */
		it = PyObject_GetIter(PyTuple_GET_ITEM(args, i));
		if (it == NULL) {
			Py_DECREF(iters);
			return NULL;
		}
		PyTuple_SET_ITEM(iters, i-1, it);
	}

	/* create imapobject structure */
	lz = (imapobject *)type->tp_alloc(type, 0);
	if (lz == NULL) {
		Py_DECREF(iters);
		return NULL;
	}
	lz->iters = iters;
	func = PyTuple_GET_ITEM(args, 0);
	Py_INCREF(func);
	lz->func = func;

	return (PyObject *)lz;
}

static void
imap_dealloc(imapobject *lz)
{
	PyObject_GC_UnTrack(lz);
	Py_XDECREF(lz->iters);
	Py_XDECREF(lz->func);
	lz->ob_type->tp_free(lz);
}

static int
imap_traverse(imapobject *lz, visitproc visit, void *arg)
{
	Py_VISIT(lz->iters);
	Py_VISIT(lz->func);
	return 0;
}

/*	
imap() is an iterator version of __builtins__.map() except that it does
not have the None fill-in feature.  That was intentionally left out for
the following reasons:

  1) Itertools are designed to be easily combined and chained together.
     Having all tools stop with the shortest input is a unifying principle
     that makes it easier to combine finite iterators (supplying data) with
     infinite iterators like count() and repeat() (for supplying sequential
     or constant arguments to a function).

  2) In typical use cases for combining itertools, having one finite data 
     supplier run out before another is likely to be an error condition which
     should not pass silently by automatically supplying None.

  3) The use cases for automatic None fill-in are rare -- not many functions
     do something useful when a parameter suddenly switches type and becomes
     None.  

  4) If a need does arise, it can be met by __builtins__.map() or by 
     writing:  chain(iterable, repeat(None)).

  5) Similar toolsets in Haskell and SML do not have automatic None fill-in.
*/

static PyObject *
imap_next(imapobject *lz)
{
	PyObject *val;
	PyObject *argtuple;
	PyObject *result;
	Py_ssize_t numargs, i;

	numargs = PyTuple_Size(lz->iters);
	argtuple = PyTuple_New(numargs);
	if (argtuple == NULL)
		return NULL;

	for (i=0 ; i<numargs ; i++) {
		val = PyIter_Next(PyTuple_GET_ITEM(lz->iters, i));
		if (val == NULL) {
			Py_DECREF(argtuple);
			return NULL;
		}
		PyTuple_SET_ITEM(argtuple, i, val);
	}
	if (lz->func == Py_None) 
		return argtuple;
	result = PyObject_Call(lz->func, argtuple, NULL);
	Py_DECREF(argtuple);
	return result;
}

PyDoc_STRVAR(imap_doc,
"imap(func, *iterables) --> imap object\n\
\n\
Make an iterator that computes the function using arguments from\n\
each of the iterables.	Like map() except that it returns\n\
an iterator instead of a list and that it stops when the shortest\n\
iterable is exhausted instead of filling in None for shorter\n\
iterables.");

static PyTypeObject imap_type = {
	PyObject_HEAD_INIT(NULL)
	0,				/* ob_size */
	"itertools.imap",		/* tp_name */
	sizeof(imapobject),		/* tp_basicsize */
	0,				/* tp_itemsize */
	/* methods */
	(destructor)imap_dealloc,	/* tp_dealloc */
	0,				/* tp_print */
	0,				/* tp_getattr */
	0,				/* tp_setattr */
	0,				/* tp_compare */
	0,				/* tp_repr */
	0,				/* tp_as_number */
	0,				/* tp_as_sequence */
	0,				/* tp_as_mapping */
	0,				/* tp_hash */
	0,				/* tp_call */
	0,				/* tp_str */
	PyObject_GenericGetAttr,	/* tp_getattro */
	0,				/* tp_setattro */
	0,				/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
		Py_TPFLAGS_BASETYPE,	/* tp_flags */
	imap_doc,			/* tp_doc */
	(traverseproc)imap_traverse,	/* tp_traverse */
	0,				/* tp_clear */
	0,				/* tp_richcompare */
	0,				/* tp_weaklistoffset */
	PyObject_SelfIter,		/* tp_iter */
	(iternextfunc)imap_next,	/* tp_iternext */
	0,				/* tp_methods */
	0,				/* tp_members */
	0,				/* tp_getset */
	0,				/* tp_base */
	0,				/* tp_dict */
	0,				/* tp_descr_get */
	0,				/* tp_descr_set */
	0,				/* tp_dictoffset */
	0,				/* tp_init */
	0,				/* tp_alloc */
	imap_new,			/* tp_new */
	PyObject_GC_Del,		/* tp_free */
};


/* chain object ************************************************************/

typedef struct {
	PyObject_HEAD
	Py_ssize_t tuplesize;
	Py_ssize_t iternum;		/* which iterator is active */
	PyObject *ittuple;		/* tuple of iterators */
} chainobject;

static PyTypeObject chain_type;

static PyObject *
chain_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	chainobject *lz;
	Py_ssize_t tuplesize = PySequence_Length(args);
	Py_ssize_t i;
	PyObject *ittuple;

	if (type == &chain_type && !_PyArg_NoKeywords("chain()", kwds))
		return NULL;

	/* obtain iterators */
	assert(PyTuple_Check(args));
	ittuple = PyTuple_New(tuplesize);
	if (ittuple == NULL)
		return NULL;
	for (i=0; i < tuplesize; ++i) {
		PyObject *item = PyTuple_GET_ITEM(args, i);
		PyObject *it = PyObject_GetIter(item);
		if (it == NULL) {
			if (PyErr_ExceptionMatches(PyExc_TypeError))
				PyErr_Format(PyExc_TypeError,
				    "chain argument #%zd must support iteration",
				    i+1);
			Py_DECREF(ittuple);
			return NULL;
		}
		PyTuple_SET_ITEM(ittuple, i, it);
	}

	/* create chainobject structure */
	lz = (chainobject *)type->tp_alloc(type, 0);
	if (lz == NULL) {
		Py_DECREF(ittuple);
		return NULL;
	}

	lz->ittuple = ittuple;
	lz->iternum = 0;
	lz->tuplesize = tuplesize;

	return (PyObject *)lz;
}

static void
chain_dealloc(chainobject *lz)
{
	PyObject_GC_UnTrack(lz);
	Py_XDECREF(lz->ittuple);
	lz->ob_type->tp_free(lz);
}

static int
chain_traverse(chainobject *lz, visitproc visit, void *arg)
{
	Py_VISIT(lz->ittuple);
	return 0;
}

static PyObject *
chain_next(chainobject *lz)
{
	PyObject *it;
	PyObject *item;

	while (lz->iternum < lz->tuplesize) {
		it = PyTuple_GET_ITEM(lz->ittuple, lz->iternum);
		item = PyIter_Next(it);
		if (item != NULL)
			return item;
		if (PyErr_Occurred()) {
			if (PyErr_ExceptionMatches(PyExc_StopIteration))
				PyErr_Clear();
			else
				return NULL;
		}
		lz->iternum++;
	}
	return NULL;
}

PyDoc_STRVAR(chain_doc,
"chain(*iterables) --> chain object\n\
\n\
Return a chain object whose .next() method returns elements from the\n\
first iterable until it is exhausted, then elements from the next\n\
iterable, until all of the iterables are exhausted.");

static PyTypeObject chain_type = {
	PyObject_HEAD_INIT(NULL)
	0,				/* ob_size */
	"itertools.chain",		/* tp_name */
	sizeof(chainobject),		/* tp_basicsize */
	0,				/* tp_itemsize */
	/* methods */
	(destructor)chain_dealloc,	/* tp_dealloc */
	0,				/* tp_print */
	0,				/* tp_getattr */
	0,				/* tp_setattr */
	0,				/* tp_compare */
	0,				/* tp_repr */
	0,				/* tp_as_number */
	0,				/* tp_as_sequence */
	0,				/* tp_as_mapping */
	0,				/* tp_hash */
	0,				/* tp_call */
	0,				/* tp_str */
	PyObject_GenericGetAttr,	/* tp_getattro */
	0,				/* tp_setattro */
	0,				/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
		Py_TPFLAGS_BASETYPE,	/* tp_flags */
	chain_doc,			/* tp_doc */
	(traverseproc)chain_traverse,	/* tp_traverse */
	0,				/* tp_clear */
	0,				/* tp_richcompare */
	0,				/* tp_weaklistoffset */
	PyObject_SelfIter,		/* tp_iter */
	(iternextfunc)chain_next,	/* tp_iternext */
	0,				/* tp_methods */
	0,				/* tp_members */
	0,				/* tp_getset */
	0,				/* tp_base */
	0,				/* tp_dict */
	0,				/* tp_descr_get */
	0,				/* tp_descr_set */
	0,				/* tp_dictoffset */
	0,				/* tp_init */
	0,				/* tp_alloc */
	chain_new,			/* tp_new */
	PyObject_GC_Del,		/* tp_free */
};


/* ifilter object ************************************************************/

typedef struct {
	PyObject_HEAD
	PyObject *func;
	PyObject *it;
} ifilterobject;

static PyTypeObject ifilter_type;

static PyObject *
ifilter_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	PyObject *func, *seq;
	PyObject *it;
	ifilterobject *lz;

	if (type == &ifilter_type && !_PyArg_NoKeywords("ifilter()", kwds))
		return NULL;

	if (!PyArg_UnpackTuple(args, "ifilter", 2, 2, &func, &seq))
		return NULL;

	/* Get iterator. */
	it = PyObject_GetIter(seq);
	if (it == NULL)
		return NULL;

	/* create ifilterobject structure */
	lz = (ifilterobject *)type->tp_alloc(type, 0);
	if (lz == NULL) {
		Py_DECREF(it);
		return NULL;
	}
	Py_INCREF(func);
	lz->func = func;
	lz->it = it;

	return (PyObject *)lz;
}

static void
ifilter_dealloc(ifilterobject *lz)
{
	PyObject_GC_UnTrack(lz);
	Py_XDECREF(lz->func);
	Py_XDECREF(lz->it);
	lz->ob_type->tp_free(lz);
}

static int
ifilter_traverse(ifilterobject *lz, visitproc visit, void *arg)
{
	Py_VISIT(lz->it);
	Py_VISIT(lz->func);
	return 0;
}

static PyObject *
ifilter_next(ifilterobject *lz)
{
	PyObject *item;
	PyObject *it = lz->it;
	long ok;
	PyObject *(*iternext)(PyObject *);

	assert(PyIter_Check(it));
	iternext = *it->ob_type->tp_iternext;
	for (;;) {
		item = iternext(it);
		if (item == NULL)
			return NULL;

		if (lz->func == Py_None) {
			ok = PyObject_IsTrue(item);
		} else {
			PyObject *good;
			good = PyObject_CallFunctionObjArgs(lz->func,
							    item, NULL);
			if (good == NULL) {
				Py_DECREF(item);
				return NULL;
			}
			ok = PyObject_IsTrue(good);
			Py_DECREF(good);
		}
		if (ok)
			return item;
		Py_DECREF(item);
	}
}

PyDoc_STRVAR(ifilter_doc,
"ifilter(function or None, sequence) --> ifilter object\n\
\n\
Return those items of sequence for which function(item) is true.\n\
If function is None, return the items that are true.");

static PyTypeObject ifilter_type = {
	PyObject_HEAD_INIT(NULL)
	0,				/* ob_size */
	"itertools.ifilter",		/* tp_name */
	sizeof(ifilterobject),		/* tp_basicsize */
	0,				/* tp_itemsize */
	/* methods */
	(destructor)ifilter_dealloc,	/* tp_dealloc */
	0,				/* tp_print */
	0,				/* tp_getattr */
	0,				/* tp_setattr */
	0,				/* tp_compare */
	0,				/* tp_repr */
	0,				/* tp_as_number */
	0,				/* tp_as_sequence */
	0,				/* tp_as_mapping */
	0,				/* tp_hash */
	0,				/* tp_call */
	0,				/* tp_str */
	PyObject_GenericGetAttr,	/* tp_getattro */
	0,				/* tp_setattro */
	0,				/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
		Py_TPFLAGS_BASETYPE,	/* tp_flags */
	ifilter_doc,			/* tp_doc */
	(traverseproc)ifilter_traverse,	/* tp_traverse */
	0,				/* tp_clear */
	0,				/* tp_richcompare */
	0,				/* tp_weaklistoffset */
	PyObject_SelfIter,		/* tp_iter */
	(iternextfunc)ifilter_next,	/* tp_iternext */
	0,				/* tp_methods */
	0,				/* tp_members */
	0,				/* tp_getset */
	0,				/* tp_base */
	0,				/* tp_dict */
	0,				/* tp_descr_get */
	0,				/* tp_descr_set */
	0,				/* tp_dictoffset */
	0,				/* tp_init */
	0,				/* tp_alloc */
	ifilter_new,			/* tp_new */
	PyObject_GC_Del,		/* tp_free */
};


/* ifilterfalse object ************************************************************/

typedef struct {
	PyObject_HEAD
	PyObject *func;
	PyObject *it;
} ifilterfalseobject;

static PyTypeObject ifilterfalse_type;

static PyObject *
ifilterfalse_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	PyObject *func, *seq;
	PyObject *it;
	ifilterfalseobject *lz;

	if (type == &ifilterfalse_type &&
	    !_PyArg_NoKeywords("ifilterfalse()", kwds))
		return NULL;

	if (!PyArg_UnpackTuple(args, "ifilterfalse", 2, 2, &func, &seq))
		return NULL;

	/* Get iterator. */
	it = PyObject_GetIter(seq);
	if (it == NULL)
		return NULL;

	/* create ifilterfalseobject structure */
	lz = (ifilterfalseobject *)type->tp_alloc(type, 0);
	if (lz == NULL) {
		Py_DECREF(it);
		return NULL;
	}
	Py_INCREF(func);
	lz->func = func;
	lz->it = it;

	return (PyObject *)lz;
}

static void
ifilterfalse_dealloc(ifilterfalseobject *lz)
{
	PyObject_GC_UnTrack(lz);
	Py_XDECREF(lz->func);
	Py_XDECREF(lz->it);
	lz->ob_type->tp_free(lz);
}

static int
ifilterfalse_traverse(ifilterfalseobject *lz, visitproc visit, void *arg)
{
	Py_VISIT(lz->it);
	Py_VISIT(lz->func);
	return 0;
}

static PyObject *
ifilterfalse_next(ifilterfalseobject *lz)
{
	PyObject *item;
	PyObject *it = lz->it;
	long ok;
	PyObject *(*iternext)(PyObject *);

	assert(PyIter_Check(it));
	iternext = *it->ob_type->tp_iternext;
	for (;;) {
		item = iternext(it);
		if (item == NULL)
			return NULL;

		if (lz->func == Py_None) {
			ok = PyObject_IsTrue(item);
		} else {
			PyObject *good;
			good = PyObject_CallFunctionObjArgs(lz->func,
							    item, NULL);
			if (good == NULL) {
				Py_DECREF(item);
				return NULL;
			}
			ok = PyObject_IsTrue(good);
			Py_DECREF(good);
		}
		if (!ok)
			return item;
		Py_DECREF(item);
	}
}

PyDoc_STRVAR(ifilterfalse_doc,
"ifilterfalse(function or None, sequence) --> ifilterfalse object\n\
\n\
Return those items of sequence for which function(item) is false.\n\
If function is None, return the items that are false.");

static PyTypeObject ifilterfalse_type = {
	PyObject_HEAD_INIT(NULL)
	0,				/* ob_size */
	"itertools.ifilterfalse",	/* tp_name */
	sizeof(ifilterfalseobject),	/* tp_basicsize */
	0,				/* tp_itemsize */
	/* methods */
	(destructor)ifilterfalse_dealloc,	/* tp_dealloc */
	0,				/* tp_print */
	0,				/* tp_getattr */
	0,				/* tp_setattr */
	0,				/* tp_compare */
	0,				/* tp_repr */
	0,				/* tp_as_number */
	0,				/* tp_as_sequence */
	0,				/* tp_as_mapping */
	0,				/* tp_hash */
	0,				/* tp_call */
	0,				/* tp_str */
	PyObject_GenericGetAttr,	/* tp_getattro */
	0,				/* tp_setattro */
	0,				/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
		Py_TPFLAGS_BASETYPE,	/* tp_flags */
	ifilterfalse_doc,		/* tp_doc */
	(traverseproc)ifilterfalse_traverse,	/* tp_traverse */
	0,				/* tp_clear */
	0,				/* tp_richcompare */
	0,				/* tp_weaklistoffset */
	PyObject_SelfIter,		/* tp_iter */
	(iternextfunc)ifilterfalse_next,	/* tp_iternext */
	0,				/* tp_methods */
	0,				/* tp_members */
	0,				/* tp_getset */
	0,				/* tp_base */
	0,				/* tp_dict */
	0,				/* tp_descr_get */
	0,				/* tp_descr_set */
	0,				/* tp_dictoffset */
	0,				/* tp_init */
	0,				/* tp_alloc */
	ifilterfalse_new,		/* tp_new */
	PyObject_GC_Del,		/* tp_free */
};


/* count object ************************************************************/

typedef struct {
	PyObject_HEAD
	Py_ssize_t cnt;
} countobject;

static PyTypeObject count_type;

static PyObject *
count_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	countobject *lz;
	Py_ssize_t cnt = 0;

	if (type == &count_type && !_PyArg_NoKeywords("count()", kwds))
		return NULL;

	if (!PyArg_ParseTuple(args, "|n:count", &cnt))
		return NULL;

	/* create countobject structure */
	lz = (countobject *)PyObject_New(countobject, &count_type);
	if (lz == NULL)
		return NULL;
	lz->cnt = cnt;

	return (PyObject *)lz;
}

static PyObject *
count_next(countobject *lz)
{
        if (lz->cnt == LONG_MAX) {
                PyErr_SetString(PyExc_OverflowError,
                        "cannot count beyond LONG_MAX");                
                return NULL;         
        }
	return PyInt_FromSsize_t(lz->cnt++);
}

static PyObject *
count_repr(countobject *lz)
{
	return PyString_FromFormat("count(%zd)", lz->cnt);
}

PyDoc_STRVAR(count_doc,
"count([firstval]) --> count object\n\
\n\
Return a count object whose .next() method returns consecutive\n\
integers starting from zero or, if specified, from firstval.");

static PyTypeObject count_type = {
	PyObject_HEAD_INIT(NULL)
	0,				/* ob_size */
	"itertools.count",		/* tp_name */
	sizeof(countobject),		/* tp_basicsize */
	0,				/* tp_itemsize */
	/* methods */
	(destructor)PyObject_Del,	/* tp_dealloc */
	0,				/* tp_print */
	0,				/* tp_getattr */
	0,				/* tp_setattr */
	0,				/* tp_compare */
	(reprfunc)count_repr,		/* tp_repr */
	0,				/* tp_as_number */
	0,				/* tp_as_sequence */
	0,				/* tp_as_mapping */
	0,				/* tp_hash */
	0,				/* tp_call */
	0,				/* tp_str */
	PyObject_GenericGetAttr,	/* tp_getattro */
	0,				/* tp_setattro */
	0,				/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,		/* tp_flags */
	count_doc,			/* tp_doc */
	0,				/* tp_traverse */
	0,				/* tp_clear */
	0,				/* tp_richcompare */
	0,				/* tp_weaklistoffset */
	PyObject_SelfIter,		/* tp_iter */
	(iternextfunc)count_next,	/* tp_iternext */
	0,				/* tp_methods */
	0,				/* tp_members */
	0,				/* tp_getset */
	0,				/* tp_base */
	0,				/* tp_dict */
	0,				/* tp_descr_get */
	0,				/* tp_descr_set */
	0,				/* tp_dictoffset */
	0,				/* tp_init */
	0,				/* tp_alloc */
	count_new,			/* tp_new */
};


/* izip object ************************************************************/

#include "Python.h"

typedef struct {
	PyObject_HEAD
	Py_ssize_t	tuplesize;
	PyObject *ittuple;		/* tuple of iterators */
	PyObject *result;
} izipobject;

static PyTypeObject izip_type;

static PyObject *
izip_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	izipobject *lz;
	Py_ssize_t i;
	PyObject *ittuple;  /* tuple of iterators */
	PyObject *result;
	Py_ssize_t tuplesize = PySequence_Length(args);

	if (type == &izip_type && !_PyArg_NoKeywords("izip()", kwds))
		return NULL;

	/* args must be a tuple */
	assert(PyTuple_Check(args));

	/* obtain iterators */
	ittuple = PyTuple_New(tuplesize);
	if (ittuple == NULL)
		return NULL;
	for (i=0; i < tuplesize; ++i) {
		PyObject *item = PyTuple_GET_ITEM(args, i);
		PyObject *it = PyObject_GetIter(item);
		if (it == NULL) {
			if (PyErr_ExceptionMatches(PyExc_TypeError))
				PyErr_Format(PyExc_TypeError,
				    "izip argument #%zd must support iteration",
				    i+1);
			Py_DECREF(ittuple);
			return NULL;
		}
		PyTuple_SET_ITEM(ittuple, i, it);
	}

	/* create a result holder */
	result = PyTuple_New(tuplesize);
	if (result == NULL) {
		Py_DECREF(ittuple);
		return NULL;
	}
	for (i=0 ; i < tuplesize ; i++) {
		Py_INCREF(Py_None);
		PyTuple_SET_ITEM(result, i, Py_None);
	}

	/* create izipobject structure */
	lz = (izipobject *)type->tp_alloc(type, 0);
	if (lz == NULL) {
		Py_DECREF(ittuple);
		Py_DECREF(result);
		return NULL;
	}
	lz->ittuple = ittuple;
	lz->tuplesize = tuplesize;
	lz->result = result;

	return (PyObject *)lz;
}

static void
izip_dealloc(izipobject *lz)
{
	PyObject_GC_UnTrack(lz);
	Py_XDECREF(lz->ittuple);
	Py_XDECREF(lz->result);
	lz->ob_type->tp_free(lz);
}

static int
izip_traverse(izipobject *lz, visitproc visit, void *arg)
{
	Py_VISIT(lz->ittuple);
	Py_VISIT(lz->result);
	return 0;
}

static PyObject *
izip_next(izipobject *lz)
{
	Py_ssize_t i;
	Py_ssize_t tuplesize = lz->tuplesize;
	PyObject *result = lz->result;
	PyObject *it;
	PyObject *item;
	PyObject *olditem;

	if (tuplesize == 0)
		return NULL;
	if (result->ob_refcnt == 1) {
		Py_INCREF(result);
		for (i=0 ; i < tuplesize ; i++) {
			it = PyTuple_GET_ITEM(lz->ittuple, i);
			assert(PyIter_Check(it));
			item = (*it->ob_type->tp_iternext)(it);
			if (item == NULL) {
				Py_DECREF(result);
				return NULL;
			}
			olditem = PyTuple_GET_ITEM(result, i);
			PyTuple_SET_ITEM(result, i, item);
			Py_DECREF(olditem);
		}
	} else {
		result = PyTuple_New(tuplesize);
		if (result == NULL)
			return NULL;
		for (i=0 ; i < tuplesize ; i++) {
			it = PyTuple_GET_ITEM(lz->ittuple, i);
			assert(PyIter_Check(it));
			item = (*it->ob_type->tp_iternext)(it);
			if (item == NULL) {
				Py_DECREF(result);
				return NULL;
			}
			PyTuple_SET_ITEM(result, i, item);
		}
	}
	return result;
}

PyDoc_STRVAR(izip_doc,
"izip(iter1 [,iter2 [...]]) --> izip object\n\
\n\
Return a izip object whose .next() method returns a tuple where\n\
the i-th element comes from the i-th iterable argument.  The .next()\n\
method continues until the shortest iterable in the argument sequence\n\
is exhausted and then it raises StopIteration.  Works like the zip()\n\
function but consumes less memory by returning an iterator instead of\n\
a list.");

static PyTypeObject izip_type = {
	PyObject_HEAD_INIT(NULL)
	0,				/* ob_size */
	"itertools.izip",		/* tp_name */
	sizeof(izipobject),		/* tp_basicsize */
	0,				/* tp_itemsize */
	/* methods */
	(destructor)izip_dealloc,	/* tp_dealloc */
	0,				/* tp_print */
	0,				/* tp_getattr */
	0,				/* tp_setattr */
	0,				/* tp_compare */
	0,				/* tp_repr */
	0,				/* tp_as_number */
	0,				/* tp_as_sequence */
	0,				/* tp_as_mapping */
	0,				/* tp_hash */
	0,				/* tp_call */
	0,				/* tp_str */
	PyObject_GenericGetAttr,	/* tp_getattro */
	0,				/* tp_setattro */
	0,				/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
		Py_TPFLAGS_BASETYPE,	/* tp_flags */
	izip_doc,			/* tp_doc */
	(traverseproc)izip_traverse,    /* tp_traverse */
	0,				/* tp_clear */
	0,				/* tp_richcompare */
	0,				/* tp_weaklistoffset */
	PyObject_SelfIter,		/* tp_iter */
	(iternextfunc)izip_next,	/* tp_iternext */
	0,				/* tp_methods */
	0,				/* tp_members */
	0,				/* tp_getset */
	0,				/* tp_base */
	0,				/* tp_dict */
	0,				/* tp_descr_get */
	0,				/* tp_descr_set */
	0,				/* tp_dictoffset */
	0,				/* tp_init */
	0,				/* tp_alloc */
	izip_new,			/* tp_new */
	PyObject_GC_Del,		/* tp_free */
};


/* repeat object ************************************************************/

typedef struct {
	PyObject_HEAD
	PyObject *element;
	Py_ssize_t cnt;
} repeatobject;

static PyTypeObject repeat_type;

static PyObject *
repeat_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	repeatobject *ro;
	PyObject *element;
	Py_ssize_t cnt = -1;

	if (type == &repeat_type && !_PyArg_NoKeywords("repeat()", kwds))
		return NULL;

	if (!PyArg_ParseTuple(args, "O|n:repeat", &element, &cnt))
		return NULL;

	if (PyTuple_Size(args) == 2 && cnt < 0)
		cnt = 0;

	ro = (repeatobject *)type->tp_alloc(type, 0);
	if (ro == NULL)
		return NULL;
	Py_INCREF(element);
	ro->element = element;
	ro->cnt = cnt;
	return (PyObject *)ro;
}

static void
repeat_dealloc(repeatobject *ro)
{
	PyObject_GC_UnTrack(ro);
	Py_XDECREF(ro->element);
	ro->ob_type->tp_free(ro);
}

static int
repeat_traverse(repeatobject *ro, visitproc visit, void *arg)
{
	Py_VISIT(ro->element);
	return 0;
}

static PyObject *
repeat_next(repeatobject *ro)
{
	if (ro->cnt == 0)
		return NULL;
	if (ro->cnt > 0)
		ro->cnt--;
	Py_INCREF(ro->element);
	return ro->element;
}

static PyObject *
repeat_repr(repeatobject *ro)
{
	PyObject *result, *objrepr;

	objrepr = PyObject_Repr(ro->element);
	if (objrepr == NULL)
		return NULL;

	if (ro->cnt == -1)
		result = PyString_FromFormat("repeat(%s)",
			PyString_AS_STRING(objrepr));
	else
		result = PyString_FromFormat("repeat(%s, %zd)",
			PyString_AS_STRING(objrepr), ro->cnt);
	Py_DECREF(objrepr);
	return result;
}	

static PyObject *
repeat_len(repeatobject *ro)
{
        if (ro->cnt == -1) {
                PyErr_SetString(PyExc_TypeError, "len() of unsized object");
		return NULL;
	}
        return PyInt_FromSize_t(ro->cnt);
}

PyDoc_STRVAR(length_hint_doc, "Private method returning an estimate of len(list(it)).");

static PyMethodDef repeat_methods[] = {
	{"__length_hint__", (PyCFunction)repeat_len, METH_NOARGS, length_hint_doc},
 	{NULL,		NULL}		/* sentinel */
};

PyDoc_STRVAR(repeat_doc,
"repeat(element [,times]) -> create an iterator which returns the element\n\
for the specified number of times.  If not specified, returns the element\n\
endlessly.");

static PyTypeObject repeat_type = {
	PyObject_HEAD_INIT(NULL)
	0,				/* ob_size */
	"itertools.repeat",		/* tp_name */
	sizeof(repeatobject),		/* tp_basicsize */
	0,				/* tp_itemsize */
	/* methods */
	(destructor)repeat_dealloc,	/* tp_dealloc */
	0,				/* tp_print */
	0,				/* tp_getattr */
	0,				/* tp_setattr */
	0,				/* tp_compare */
	(reprfunc)repeat_repr,		/* tp_repr */
	0,				/* tp_as_number */
	0,				/* tp_as_sequence */
	0,				/* tp_as_mapping */
	0,				/* tp_hash */
	0,				/* tp_call */
	0,				/* tp_str */
	PyObject_GenericGetAttr,	/* tp_getattro */
	0,				/* tp_setattro */
	0,				/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
		Py_TPFLAGS_BASETYPE,	/* tp_flags */
	repeat_doc,			/* tp_doc */
	(traverseproc)repeat_traverse,	/* tp_traverse */
	0,				/* tp_clear */
	0,				/* tp_richcompare */
	0,				/* tp_weaklistoffset */
	PyObject_SelfIter,		/* tp_iter */
	(iternextfunc)repeat_next,	/* tp_iternext */
	repeat_methods,			/* tp_methods */
	0,				/* tp_members */
	0,				/* tp_getset */
	0,				/* tp_base */
	0,				/* tp_dict */
	0,				/* tp_descr_get */
	0,				/* tp_descr_set */
	0,				/* tp_dictoffset */
	0,				/* tp_init */
	0,				/* tp_alloc */
	repeat_new,			/* tp_new */
	PyObject_GC_Del,		/* tp_free */
};


/* module level code ********************************************************/

PyDoc_STRVAR(module_doc,
"Functional tools for creating and using iterators.\n\
\n\
Infinite iterators:\n\
count([n]) --> n, n+1, n+2, ...\n\
cycle(p) --> p0, p1, ... plast, p0, p1, ...\n\
repeat(elem [,n]) --> elem, elem, elem, ... endlessly or up to n times\n\
\n\
Iterators terminating on the shortest input sequence:\n\
izip(p, q, ...) --> (p[0], q[0]), (p[1], q[1]), ... \n\
ifilter(pred, seq) --> elements of seq where pred(elem) is True\n\
ifilterfalse(pred, seq) --> elements of seq where pred(elem) is False\n\
islice(seq, [start,] stop [, step]) --> elements from\n\
       seq[start:stop:step]\n\
imap(fun, p, q, ...) --> fun(p0, q0), fun(p1, q1), ...\n\
starmap(fun, seq) --> fun(*seq[0]), fun(*seq[1]), ...\n\
tee(it, n=2) --> (it1, it2 , ... itn) splits one iterator into n\n\
chain(p, q, ...) --> p0, p1, ... plast, q0, q1, ... \n\
takewhile(pred, seq) --> seq[0], seq[1], until pred fails\n\
dropwhile(pred, seq) --> seq[n], seq[n+1], starting when pred fails\n\
groupby(iterable[, keyfunc]) --> sub-iterators grouped by value of keyfunc(v)\n\
");


static PyMethodDef module_methods[] = {
	{"tee",	(PyCFunction)tee,	METH_VARARGS, tee_doc},
 	{NULL,		NULL}		/* sentinel */
};

PyMODINIT_FUNC
inititertools(void)
{
	int i;
	PyObject *m;
	char *name;
	PyTypeObject *typelist[] = {
		&cycle_type,
		&dropwhile_type,
		&takewhile_type,
		&islice_type,
		&starmap_type,
		&imap_type,
		&chain_type,
		&ifilter_type,
		&ifilterfalse_type,
		&count_type,
		&izip_type,
		&repeat_type,
		&groupby_type,
		NULL
	};

	teedataobject_type.ob_type = &PyType_Type;
	m = Py_InitModule3("itertools", module_methods, module_doc);
	if (m == NULL)
		return;

	for (i=0 ; typelist[i] != NULL ; i++) {
		if (PyType_Ready(typelist[i]) < 0)
			return;
		name = strchr(typelist[i]->tp_name, '.');
		assert (name != NULL);
		Py_INCREF(typelist[i]);
		PyModule_AddObject(m, name+1, (PyObject *)typelist[i]);
	}

	if (PyType_Ready(&teedataobject_type) < 0)
		return;
	if (PyType_Ready(&tee_type) < 0)
		return;
	if (PyType_Ready(&_grouper_type) < 0)
		return;
}
