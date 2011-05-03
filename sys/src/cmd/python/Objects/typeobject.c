/* Type object implementation */

#include "Python.h"
#include "structmember.h"

#include <ctype.h>

static PyMemberDef type_members[] = {
	{"__basicsize__", T_INT, offsetof(PyTypeObject,tp_basicsize),READONLY},
	{"__itemsize__", T_INT, offsetof(PyTypeObject, tp_itemsize), READONLY},
	{"__flags__", T_LONG, offsetof(PyTypeObject, tp_flags), READONLY},
	{"__weakrefoffset__", T_LONG,
	 offsetof(PyTypeObject, tp_weaklistoffset), READONLY},
	{"__base__", T_OBJECT, offsetof(PyTypeObject, tp_base), READONLY},
	{"__dictoffset__", T_LONG,
	 offsetof(PyTypeObject, tp_dictoffset), READONLY},
	{"__mro__", T_OBJECT, offsetof(PyTypeObject, tp_mro), READONLY},
	{0}
};

static PyObject *
type_name(PyTypeObject *type, void *context)
{
	const char *s;

	if (type->tp_flags & Py_TPFLAGS_HEAPTYPE) {
		PyHeapTypeObject* et = (PyHeapTypeObject*)type;

		Py_INCREF(et->ht_name);
		return et->ht_name;
	}
	else {
		s = strrchr(type->tp_name, '.');
		if (s == NULL)
			s = type->tp_name;
		else
			s++;
		return PyString_FromString(s);
	}
}

static int
type_set_name(PyTypeObject *type, PyObject *value, void *context)
{
	PyHeapTypeObject* et;

	if (!(type->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
		PyErr_Format(PyExc_TypeError,
			     "can't set %s.__name__", type->tp_name);
		return -1;
	}
	if (!value) {
		PyErr_Format(PyExc_TypeError,
			     "can't delete %s.__name__", type->tp_name);
		return -1;
	}
	if (!PyString_Check(value)) {
		PyErr_Format(PyExc_TypeError,
			     "can only assign string to %s.__name__, not '%s'",
			     type->tp_name, value->ob_type->tp_name);
		return -1;
	}
	if (strlen(PyString_AS_STRING(value))
	    != (size_t)PyString_GET_SIZE(value)) {
		PyErr_Format(PyExc_ValueError,
			     "__name__ must not contain null bytes");
		return -1;
	}

	et = (PyHeapTypeObject*)type;

	Py_INCREF(value);

	Py_DECREF(et->ht_name);
	et->ht_name = value;

	type->tp_name = PyString_AS_STRING(value);

	return 0;
}

static PyObject *
type_module(PyTypeObject *type, void *context)
{
	PyObject *mod;
	char *s;

	if (type->tp_flags & Py_TPFLAGS_HEAPTYPE) {
		mod = PyDict_GetItemString(type->tp_dict, "__module__");
		if (!mod) { 
			PyErr_Format(PyExc_AttributeError, "__module__");
			return 0;
		}
		Py_XINCREF(mod);
		return mod;
	}
	else {
		s = strrchr(type->tp_name, '.');
		if (s != NULL)
			return PyString_FromStringAndSize(
			    type->tp_name, (Py_ssize_t)(s - type->tp_name));
		return PyString_FromString("__builtin__");
	}
}

static int
type_set_module(PyTypeObject *type, PyObject *value, void *context)
{
	if (!(type->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
		PyErr_Format(PyExc_TypeError,
			     "can't set %s.__module__", type->tp_name);
		return -1;
	}
	if (!value) {
		PyErr_Format(PyExc_TypeError,
			     "can't delete %s.__module__", type->tp_name);
		return -1;
	}

	return PyDict_SetItemString(type->tp_dict, "__module__", value);
}

static PyObject *
type_get_bases(PyTypeObject *type, void *context)
{
	Py_INCREF(type->tp_bases);
	return type->tp_bases;
}

static PyTypeObject *best_base(PyObject *);
static int mro_internal(PyTypeObject *);
static int compatible_for_assignment(PyTypeObject *, PyTypeObject *, char *);
static int add_subclass(PyTypeObject*, PyTypeObject*);
static void remove_subclass(PyTypeObject *, PyTypeObject *);
static void update_all_slots(PyTypeObject *);

typedef int (*update_callback)(PyTypeObject *, void *);
static int update_subclasses(PyTypeObject *type, PyObject *name,
			     update_callback callback, void *data);
static int recurse_down_subclasses(PyTypeObject *type, PyObject *name,
				   update_callback callback, void *data);

static int
mro_subclasses(PyTypeObject *type, PyObject* temp)
{
	PyTypeObject *subclass;
	PyObject *ref, *subclasses, *old_mro;
	Py_ssize_t i, n;

	subclasses = type->tp_subclasses;
	if (subclasses == NULL)
		return 0;
	assert(PyList_Check(subclasses));
	n = PyList_GET_SIZE(subclasses);
	for (i = 0; i < n; i++) {
		ref = PyList_GET_ITEM(subclasses, i);
		assert(PyWeakref_CheckRef(ref));
		subclass = (PyTypeObject *)PyWeakref_GET_OBJECT(ref);
		assert(subclass != NULL);
		if ((PyObject *)subclass == Py_None)
			continue;
		assert(PyType_Check(subclass));
		old_mro = subclass->tp_mro;
		if (mro_internal(subclass) < 0) {
			subclass->tp_mro = old_mro;
			return -1;
		}
		else {
			PyObject* tuple;
			tuple = PyTuple_Pack(2, subclass, old_mro);
			Py_DECREF(old_mro);
			if (!tuple)
				return -1;
			if (PyList_Append(temp, tuple) < 0)
				return -1;
			Py_DECREF(tuple);
		}
		if (mro_subclasses(subclass, temp) < 0)
			return -1;
	}
	return 0;
}

static int
type_set_bases(PyTypeObject *type, PyObject *value, void *context)
{
	Py_ssize_t i;
	int r = 0;
	PyObject *ob, *temp;
	PyTypeObject *new_base, *old_base;
	PyObject *old_bases, *old_mro;

	if (!(type->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
		PyErr_Format(PyExc_TypeError,
			     "can't set %s.__bases__", type->tp_name);
		return -1;
	}
	if (!value) {
		PyErr_Format(PyExc_TypeError,
			     "can't delete %s.__bases__", type->tp_name);
		return -1;
	}
	if (!PyTuple_Check(value)) {
		PyErr_Format(PyExc_TypeError,
		     "can only assign tuple to %s.__bases__, not %s",
			     type->tp_name, value->ob_type->tp_name);
		return -1;
	}
	if (PyTuple_GET_SIZE(value) == 0) {
		PyErr_Format(PyExc_TypeError,
		     "can only assign non-empty tuple to %s.__bases__, not ()",
			     type->tp_name);
		return -1;
	}
	for (i = 0; i < PyTuple_GET_SIZE(value); i++) {
		ob = PyTuple_GET_ITEM(value, i);
		if (!PyClass_Check(ob) && !PyType_Check(ob)) {
			PyErr_Format(
				PyExc_TypeError,
	"%s.__bases__ must be tuple of old- or new-style classes, not '%s'",
				type->tp_name, ob->ob_type->tp_name);
			return -1;
		}
		if (PyType_Check(ob)) {
			if (PyType_IsSubtype((PyTypeObject*)ob, type)) {
				PyErr_SetString(PyExc_TypeError,
			"a __bases__ item causes an inheritance cycle");
				return -1;
			}
		}
	}

	new_base = best_base(value);

	if (!new_base) {
		return -1;
	}

	if (!compatible_for_assignment(type->tp_base, new_base, "__bases__"))
		return -1;

	Py_INCREF(new_base);
	Py_INCREF(value);

	old_bases = type->tp_bases;
	old_base = type->tp_base;
	old_mro = type->tp_mro;

	type->tp_bases = value;
	type->tp_base = new_base;

	if (mro_internal(type) < 0) {
		goto bail;
	}

	temp = PyList_New(0);
	if (!temp)
		goto bail;

	r = mro_subclasses(type, temp);

	if (r < 0) {
		for (i = 0; i < PyList_Size(temp); i++) {
			PyTypeObject* cls;
			PyObject* mro;
			PyArg_UnpackTuple(PyList_GET_ITEM(temp, i),
					 "", 2, 2, &cls, &mro);
			Py_DECREF(cls->tp_mro);
			cls->tp_mro = mro;
			Py_INCREF(cls->tp_mro);
		}
		Py_DECREF(temp);
		goto bail;
	}

	Py_DECREF(temp);

	/* any base that was in __bases__ but now isn't, we
	   need to remove |type| from its tp_subclasses.
	   conversely, any class now in __bases__ that wasn't
	   needs to have |type| added to its subclasses. */

	/* for now, sod that: just remove from all old_bases,
	   add to all new_bases */

	for (i = PyTuple_GET_SIZE(old_bases) - 1; i >= 0; i--) {
		ob = PyTuple_GET_ITEM(old_bases, i);
		if (PyType_Check(ob)) {
			remove_subclass(
				(PyTypeObject*)ob, type);
		}
	}

	for (i = PyTuple_GET_SIZE(value) - 1; i >= 0; i--) {
		ob = PyTuple_GET_ITEM(value, i);
		if (PyType_Check(ob)) {
			if (add_subclass((PyTypeObject*)ob, type) < 0)
				r = -1;
		}
	}

	update_all_slots(type);

	Py_DECREF(old_bases);
	Py_DECREF(old_base);
	Py_DECREF(old_mro);

	return r;

  bail:
	Py_DECREF(type->tp_bases);
	Py_DECREF(type->tp_base);
	if (type->tp_mro != old_mro) {
		Py_DECREF(type->tp_mro);
	}

	type->tp_bases = old_bases;
	type->tp_base = old_base;
	type->tp_mro = old_mro;

	return -1;
}

static PyObject *
type_dict(PyTypeObject *type, void *context)
{
	if (type->tp_dict == NULL) {
		Py_INCREF(Py_None);
		return Py_None;
	}
	return PyDictProxy_New(type->tp_dict);
}

static PyObject *
type_get_doc(PyTypeObject *type, void *context)
{
	PyObject *result;
	if (!(type->tp_flags & Py_TPFLAGS_HEAPTYPE) && type->tp_doc != NULL)
		return PyString_FromString(type->tp_doc);
	result = PyDict_GetItemString(type->tp_dict, "__doc__");
	if (result == NULL) {
		result = Py_None;
		Py_INCREF(result);
	}
	else if (result->ob_type->tp_descr_get) {
		result = result->ob_type->tp_descr_get(result, NULL,
						       (PyObject *)type);
	}
	else {
		Py_INCREF(result);
	}
	return result;
}

static PyGetSetDef type_getsets[] = {
	{"__name__", (getter)type_name, (setter)type_set_name, NULL},
	{"__bases__", (getter)type_get_bases, (setter)type_set_bases, NULL},
	{"__module__", (getter)type_module, (setter)type_set_module, NULL},
	{"__dict__",  (getter)type_dict,  NULL, NULL},
	{"__doc__", (getter)type_get_doc, NULL, NULL},
	{0}
};

static int
type_compare(PyObject *v, PyObject *w)
{
	/* This is called with type objects only. So we
	   can just compare the addresses. */
	Py_uintptr_t vv = (Py_uintptr_t)v;
	Py_uintptr_t ww = (Py_uintptr_t)w;
	return (vv < ww) ? -1 : (vv > ww) ? 1 : 0;
}

static PyObject *
type_repr(PyTypeObject *type)
{
	PyObject *mod, *name, *rtn;
	char *kind;

	mod = type_module(type, NULL);
	if (mod == NULL)
		PyErr_Clear();
	else if (!PyString_Check(mod)) {
		Py_DECREF(mod);
		mod = NULL;
	}
	name = type_name(type, NULL);
	if (name == NULL)
		return NULL;

	if (type->tp_flags & Py_TPFLAGS_HEAPTYPE)
		kind = "class";
	else
		kind = "type";

	if (mod != NULL && strcmp(PyString_AS_STRING(mod), "__builtin__")) {
		rtn = PyString_FromFormat("<%s '%s.%s'>",
					  kind,
					  PyString_AS_STRING(mod),
					  PyString_AS_STRING(name));
	}
	else
		rtn = PyString_FromFormat("<%s '%s'>", kind, type->tp_name);

	Py_XDECREF(mod);
	Py_DECREF(name);
	return rtn;
}

static PyObject *
type_call(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	PyObject *obj;

	if (type->tp_new == NULL) {
		PyErr_Format(PyExc_TypeError,
			     "cannot create '%.100s' instances",
			     type->tp_name);
		return NULL;
	}

	obj = type->tp_new(type, args, kwds);
	if (obj != NULL) {
		/* Ugly exception: when the call was type(something),
		   don't call tp_init on the result. */
		if (type == &PyType_Type &&
		    PyTuple_Check(args) && PyTuple_GET_SIZE(args) == 1 &&
		    (kwds == NULL ||
		     (PyDict_Check(kwds) && PyDict_Size(kwds) == 0)))
			return obj;
		/* If the returned object is not an instance of type,
		   it won't be initialized. */
		if (!PyType_IsSubtype(obj->ob_type, type))
			return obj;
		type = obj->ob_type;
		if (PyType_HasFeature(type, Py_TPFLAGS_HAVE_CLASS) &&
		    type->tp_init != NULL &&
		    type->tp_init(obj, args, kwds) < 0) {
			Py_DECREF(obj);
			obj = NULL;
		}
	}
	return obj;
}

PyObject *
PyType_GenericAlloc(PyTypeObject *type, Py_ssize_t nitems)
{
	PyObject *obj;
	const size_t size = _PyObject_VAR_SIZE(type, nitems+1);
	/* note that we need to add one, for the sentinel */

	if (PyType_IS_GC(type))
		obj = _PyObject_GC_Malloc(size);
	else
		obj = (PyObject *)PyObject_MALLOC(size);

	if (obj == NULL)
		return PyErr_NoMemory();

	memset(obj, '\0', size);

	if (type->tp_flags & Py_TPFLAGS_HEAPTYPE)
		Py_INCREF(type);

	if (type->tp_itemsize == 0)
		PyObject_INIT(obj, type);
	else
		(void) PyObject_INIT_VAR((PyVarObject *)obj, type, nitems);

	if (PyType_IS_GC(type))
		_PyObject_GC_TRACK(obj);
	return obj;
}

PyObject *
PyType_GenericNew(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	return type->tp_alloc(type, 0);
}

/* Helpers for subtyping */

static int
traverse_slots(PyTypeObject *type, PyObject *self, visitproc visit, void *arg)
{
	Py_ssize_t i, n;
	PyMemberDef *mp;

	n = type->ob_size;
	mp = PyHeapType_GET_MEMBERS((PyHeapTypeObject *)type);
	for (i = 0; i < n; i++, mp++) {
		if (mp->type == T_OBJECT_EX) {
			char *addr = (char *)self + mp->offset;
			PyObject *obj = *(PyObject **)addr;
			if (obj != NULL) {
				int err = visit(obj, arg);
				if (err)
					return err;
			}
		}
	}
	return 0;
}

static int
subtype_traverse(PyObject *self, visitproc visit, void *arg)
{
	PyTypeObject *type, *base;
	traverseproc basetraverse;

	/* Find the nearest base with a different tp_traverse,
	   and traverse slots while we're at it */
	type = self->ob_type;
	base = type;
	while ((basetraverse = base->tp_traverse) == subtype_traverse) {
		if (base->ob_size) {
			int err = traverse_slots(base, self, visit, arg);
			if (err)
				return err;
		}
		base = base->tp_base;
		assert(base);
	}

	if (type->tp_dictoffset != base->tp_dictoffset) {
		PyObject **dictptr = _PyObject_GetDictPtr(self);
		if (dictptr && *dictptr)
			Py_VISIT(*dictptr);
	}

	if (type->tp_flags & Py_TPFLAGS_HEAPTYPE)
		/* For a heaptype, the instances count as references
		   to the type.  Traverse the type so the collector
		   can find cycles involving this link. */
		Py_VISIT(type);

	if (basetraverse)
		return basetraverse(self, visit, arg);
	return 0;
}

static void
clear_slots(PyTypeObject *type, PyObject *self)
{
	Py_ssize_t i, n;
	PyMemberDef *mp;

	n = type->ob_size;
	mp = PyHeapType_GET_MEMBERS((PyHeapTypeObject *)type);
	for (i = 0; i < n; i++, mp++) {
		if (mp->type == T_OBJECT_EX && !(mp->flags & READONLY)) {
			char *addr = (char *)self + mp->offset;
			PyObject *obj = *(PyObject **)addr;
			if (obj != NULL) {
				*(PyObject **)addr = NULL;
				Py_DECREF(obj);
			}
		}
	}
}

static int
subtype_clear(PyObject *self)
{
	PyTypeObject *type, *base;
	inquiry baseclear;

	/* Find the nearest base with a different tp_clear
	   and clear slots while we're at it */
	type = self->ob_type;
	base = type;
	while ((baseclear = base->tp_clear) == subtype_clear) {
		if (base->ob_size)
			clear_slots(base, self);
		base = base->tp_base;
		assert(base);
	}

	/* There's no need to clear the instance dict (if any);
	   the collector will call its tp_clear handler. */

	if (baseclear)
		return baseclear(self);
	return 0;
}

static void
subtype_dealloc(PyObject *self)
{
	PyTypeObject *type, *base;
	destructor basedealloc;

	/* Extract the type; we expect it to be a heap type */
	type = self->ob_type;
	assert(type->tp_flags & Py_TPFLAGS_HEAPTYPE);

	/* Test whether the type has GC exactly once */

	if (!PyType_IS_GC(type)) {
		/* It's really rare to find a dynamic type that doesn't have
		   GC; it can only happen when deriving from 'object' and not
		   adding any slots or instance variables.  This allows
		   certain simplifications: there's no need to call
		   clear_slots(), or DECREF the dict, or clear weakrefs. */

		/* Maybe call finalizer; exit early if resurrected */
		if (type->tp_del) {
			type->tp_del(self);
			if (self->ob_refcnt > 0)
				return;
		}

		/* Find the nearest base with a different tp_dealloc */
		base = type;
		while ((basedealloc = base->tp_dealloc) == subtype_dealloc) {
			assert(base->ob_size == 0);
			base = base->tp_base;
			assert(base);
		}

		/* Call the base tp_dealloc() */
		assert(basedealloc);
		basedealloc(self);

		/* Can't reference self beyond this point */
		Py_DECREF(type);

		/* Done */
		return;
	}

	/* We get here only if the type has GC */

	/* UnTrack and re-Track around the trashcan macro, alas */
	/* See explanation at end of function for full disclosure */
	PyObject_GC_UnTrack(self);
	++_PyTrash_delete_nesting;
	Py_TRASHCAN_SAFE_BEGIN(self);
	--_PyTrash_delete_nesting;
	/* DO NOT restore GC tracking at this point.  weakref callbacks
	 * (if any, and whether directly here or indirectly in something we
	 * call) may trigger GC, and if self is tracked at that point, it
	 * will look like trash to GC and GC will try to delete self again.
	 */

	/* Find the nearest base with a different tp_dealloc */
	base = type;
	while ((basedealloc = base->tp_dealloc) == subtype_dealloc) {
		base = base->tp_base;
		assert(base);
	}

	/* If we added a weaklist, we clear it.  Do this *before* calling
	   the finalizer (__del__), clearing slots, or clearing the instance
	   dict. */

	if (type->tp_weaklistoffset && !base->tp_weaklistoffset)
		PyObject_ClearWeakRefs(self);

	/* Maybe call finalizer; exit early if resurrected */
	if (type->tp_del) {
		_PyObject_GC_TRACK(self);
		type->tp_del(self);
		if (self->ob_refcnt > 0)
			goto endlabel;	/* resurrected */
		else
			_PyObject_GC_UNTRACK(self);
		/* New weakrefs could be created during the finalizer call.
		    If this occurs, clear them out without calling their
		    finalizers since they might rely on part of the object
		    being finalized that has already been destroyed. */
		if (type->tp_weaklistoffset && !base->tp_weaklistoffset) {
			/* Modeled after GET_WEAKREFS_LISTPTR() */
			PyWeakReference **list = (PyWeakReference **) \
				PyObject_GET_WEAKREFS_LISTPTR(self);
			while (*list)
				_PyWeakref_ClearRef(*list);
		}
	}

	/*  Clear slots up to the nearest base with a different tp_dealloc */
	base = type;
	while ((basedealloc = base->tp_dealloc) == subtype_dealloc) {
		if (base->ob_size)
			clear_slots(base, self);
		base = base->tp_base;
		assert(base);
	}

	/* If we added a dict, DECREF it */
	if (type->tp_dictoffset && !base->tp_dictoffset) {
		PyObject **dictptr = _PyObject_GetDictPtr(self);
		if (dictptr != NULL) {
			PyObject *dict = *dictptr;
			if (dict != NULL) {
				Py_DECREF(dict);
				*dictptr = NULL;
			}
		}
	}

	/* Call the base tp_dealloc(); first retrack self if
	 * basedealloc knows about gc.
	 */
	if (PyType_IS_GC(base))
		_PyObject_GC_TRACK(self);
	assert(basedealloc);
	basedealloc(self);

	/* Can't reference self beyond this point */
	Py_DECREF(type);

  endlabel:
	++_PyTrash_delete_nesting;
	Py_TRASHCAN_SAFE_END(self);
	--_PyTrash_delete_nesting;

	/* Explanation of the weirdness around the trashcan macros:

	   Q. What do the trashcan macros do?

	   A. Read the comment titled "Trashcan mechanism" in object.h.
	      For one, this explains why there must be a call to GC-untrack
	      before the trashcan begin macro.  Without understanding the
	      trashcan code, the answers to the following questions don't make
	      sense.

	   Q. Why do we GC-untrack before the trashcan and then immediately
	      GC-track again afterward?

	   A. In the case that the base class is GC-aware, the base class
	      probably GC-untracks the object.  If it does that using the
	      UNTRACK macro, this will crash when the object is already
	      untracked.  Because we don't know what the base class does, the
	      only safe thing is to make sure the object is tracked when we
	      call the base class dealloc.  But...  The trashcan begin macro
	      requires that the object is *untracked* before it is called.  So
	      the dance becomes:

	         GC untrack
		 trashcan begin
		 GC track

           Q. Why did the last question say "immediately GC-track again"?
              It's nowhere near immediately.

           A. Because the code *used* to re-track immediately.  Bad Idea.
              self has a refcount of 0, and if gc ever gets its hands on it
              (which can happen if any weakref callback gets invoked), it
              looks like trash to gc too, and gc also tries to delete self
              then.  But we're already deleting self.  Double dealloction is
              a subtle disaster.

	   Q. Why the bizarre (net-zero) manipulation of
	      _PyTrash_delete_nesting around the trashcan macros?

	   A. Some base classes (e.g. list) also use the trashcan mechanism.
	      The following scenario used to be possible:

	      - suppose the trashcan level is one below the trashcan limit

	      - subtype_dealloc() is called

	      - the trashcan limit is not yet reached, so the trashcan level
	        is incremented and the code between trashcan begin and end is
	        executed

	      - this destroys much of the object's contents, including its
	        slots and __dict__

	      - basedealloc() is called; this is really list_dealloc(), or
	        some other type which also uses the trashcan macros

	      - the trashcan limit is now reached, so the object is put on the
	        trashcan's to-be-deleted-later list

	      - basedealloc() returns

	      - subtype_dealloc() decrefs the object's type

	      - subtype_dealloc() returns

	      - later, the trashcan code starts deleting the objects from its
	        to-be-deleted-later list

	      - subtype_dealloc() is called *AGAIN* for the same object

	      - at the very least (if the destroyed slots and __dict__ don't
	        cause problems) the object's type gets decref'ed a second
	        time, which is *BAD*!!!

	      The remedy is to make sure that if the code between trashcan
	      begin and end in subtype_dealloc() is called, the code between
	      trashcan begin and end in basedealloc() will also be called.
	      This is done by decrementing the level after passing into the
	      trashcan block, and incrementing it just before leaving the
	      block.

	      But now it's possible that a chain of objects consisting solely
	      of objects whose deallocator is subtype_dealloc() will defeat
	      the trashcan mechanism completely: the decremented level means
	      that the effective level never reaches the limit.  Therefore, we
	      *increment* the level *before* entering the trashcan block, and
	      matchingly decrement it after leaving.  This means the trashcan
	      code will trigger a little early, but that's no big deal.

	   Q. Are there any live examples of code in need of all this
	      complexity?

	   A. Yes.  See SF bug 668433 for code that crashed (when Python was
	      compiled in debug mode) before the trashcan level manipulations
	      were added.  For more discussion, see SF patches 581742, 575073
	      and bug 574207.
	*/
}

static PyTypeObject *solid_base(PyTypeObject *type);

/* type test with subclassing support */

int
PyType_IsSubtype(PyTypeObject *a, PyTypeObject *b)
{
	PyObject *mro;

	if (!(a->tp_flags & Py_TPFLAGS_HAVE_CLASS))
		return b == a || b == &PyBaseObject_Type;

	mro = a->tp_mro;
	if (mro != NULL) {
		/* Deal with multiple inheritance without recursion
		   by walking the MRO tuple */
		Py_ssize_t i, n;
		assert(PyTuple_Check(mro));
		n = PyTuple_GET_SIZE(mro);
		for (i = 0; i < n; i++) {
			if (PyTuple_GET_ITEM(mro, i) == (PyObject *)b)
				return 1;
		}
		return 0;
	}
	else {
		/* a is not completely initilized yet; follow tp_base */
		do {
			if (a == b)
				return 1;
			a = a->tp_base;
		} while (a != NULL);
		return b == &PyBaseObject_Type;
	}
}

/* Internal routines to do a method lookup in the type
   without looking in the instance dictionary
   (so we can't use PyObject_GetAttr) but still binding
   it to the instance.  The arguments are the object,
   the method name as a C string, and the address of a
   static variable used to cache the interned Python string.

   Two variants:

   - lookup_maybe() returns NULL without raising an exception
     when the _PyType_Lookup() call fails;

   - lookup_method() always raises an exception upon errors.
*/

static PyObject *
lookup_maybe(PyObject *self, char *attrstr, PyObject **attrobj)
{
	PyObject *res;

	if (*attrobj == NULL) {
		*attrobj = PyString_InternFromString(attrstr);
		if (*attrobj == NULL)
			return NULL;
	}
	res = _PyType_Lookup(self->ob_type, *attrobj);
	if (res != NULL) {
		descrgetfunc f;
		if ((f = res->ob_type->tp_descr_get) == NULL)
			Py_INCREF(res);
		else
			res = f(res, self, (PyObject *)(self->ob_type));
	}
	return res;
}

static PyObject *
lookup_method(PyObject *self, char *attrstr, PyObject **attrobj)
{
	PyObject *res = lookup_maybe(self, attrstr, attrobj);
	if (res == NULL && !PyErr_Occurred())
		PyErr_SetObject(PyExc_AttributeError, *attrobj);
	return res;
}

/* A variation of PyObject_CallMethod that uses lookup_method()
   instead of PyObject_GetAttrString().  This uses the same convention
   as lookup_method to cache the interned name string object. */

static PyObject *
call_method(PyObject *o, char *name, PyObject **nameobj, char *format, ...)
{
	va_list va;
	PyObject *args, *func = 0, *retval;
	va_start(va, format);

	func = lookup_maybe(o, name, nameobj);
	if (func == NULL) {
		va_end(va);
		if (!PyErr_Occurred())
			PyErr_SetObject(PyExc_AttributeError, *nameobj);
		return NULL;
	}

	if (format && *format)
		args = Py_VaBuildValue(format, va);
	else
		args = PyTuple_New(0);

	va_end(va);

	if (args == NULL)
		return NULL;

	assert(PyTuple_Check(args));
	retval = PyObject_Call(func, args, NULL);

	Py_DECREF(args);
	Py_DECREF(func);

	return retval;
}

/* Clone of call_method() that returns NotImplemented when the lookup fails. */

static PyObject *
call_maybe(PyObject *o, char *name, PyObject **nameobj, char *format, ...)
{
	va_list va;
	PyObject *args, *func = 0, *retval;
	va_start(va, format);

	func = lookup_maybe(o, name, nameobj);
	if (func == NULL) {
		va_end(va);
		if (!PyErr_Occurred()) {
			Py_INCREF(Py_NotImplemented);
			return Py_NotImplemented;
		}
		return NULL;
	}

	if (format && *format)
		args = Py_VaBuildValue(format, va);
	else
		args = PyTuple_New(0);

	va_end(va);

	if (args == NULL)
		return NULL;

	assert(PyTuple_Check(args));
	retval = PyObject_Call(func, args, NULL);

	Py_DECREF(args);
	Py_DECREF(func);

	return retval;
}

static int
fill_classic_mro(PyObject *mro, PyObject *cls)
{
	PyObject *bases, *base;
	Py_ssize_t i, n;

	assert(PyList_Check(mro));
	assert(PyClass_Check(cls));
	i = PySequence_Contains(mro, cls);
	if (i < 0)
		return -1;
	if (!i) {
		if (PyList_Append(mro, cls) < 0)
			return -1;
	}
	bases = ((PyClassObject *)cls)->cl_bases;
	assert(bases && PyTuple_Check(bases));
	n = PyTuple_GET_SIZE(bases);
	for (i = 0; i < n; i++) {
		base = PyTuple_GET_ITEM(bases, i);
		if (fill_classic_mro(mro, base) < 0)
			return -1;
	}
	return 0;
}

static PyObject *
classic_mro(PyObject *cls)
{
	PyObject *mro;

	assert(PyClass_Check(cls));
	mro = PyList_New(0);
	if (mro != NULL) {
		if (fill_classic_mro(mro, cls) == 0)
			return mro;
		Py_DECREF(mro);
	}
	return NULL;
}

/*
    Method resolution order algorithm C3 described in
    "A Monotonic Superclass Linearization for Dylan",
    by Kim Barrett, Bob Cassel, Paul Haahr,
    David A. Moon, Keith Playford, and P. Tucker Withington.
    (OOPSLA 1996)

    Some notes about the rules implied by C3:

    No duplicate bases.
    It isn't legal to repeat a class in a list of base classes.

    The next three properties are the 3 constraints in "C3".

    Local precendece order.
    If A precedes B in C's MRO, then A will precede B in the MRO of all
    subclasses of C.

    Monotonicity.
    The MRO of a class must be an extension without reordering of the
    MRO of each of its superclasses.

    Extended Precedence Graph (EPG).
    Linearization is consistent if there is a path in the EPG from
    each class to all its successors in the linearization.  See
    the paper for definition of EPG.
 */

static int
tail_contains(PyObject *list, int whence, PyObject *o) {
	Py_ssize_t j, size;
	size = PyList_GET_SIZE(list);

	for (j = whence+1; j < size; j++) {
		if (PyList_GET_ITEM(list, j) == o)
			return 1;
	}
	return 0;
}

static PyObject *
class_name(PyObject *cls)
{
	PyObject *name = PyObject_GetAttrString(cls, "__name__");
	if (name == NULL) {
		PyErr_Clear();
		Py_XDECREF(name);
		name = PyObject_Repr(cls);
	}
	if (name == NULL)
		return NULL;
	if (!PyString_Check(name)) {
		Py_DECREF(name);
		return NULL;
	}
	return name;
}

static int
check_duplicates(PyObject *list)
{
	Py_ssize_t i, j, n;
	/* Let's use a quadratic time algorithm,
	   assuming that the bases lists is short.
	*/
	n = PyList_GET_SIZE(list);
	for (i = 0; i < n; i++) {
		PyObject *o = PyList_GET_ITEM(list, i);
		for (j = i + 1; j < n; j++) {
			if (PyList_GET_ITEM(list, j) == o) {
				o = class_name(o);
				PyErr_Format(PyExc_TypeError,
					     "duplicate base class %s",
					     o ? PyString_AS_STRING(o) : "?");
				Py_XDECREF(o);
				return -1;
			}
		}
	}
	return 0;
}

/* Raise a TypeError for an MRO order disagreement.

   It's hard to produce a good error message.  In the absence of better
   insight into error reporting, report the classes that were candidates
   to be put next into the MRO.  There is some conflict between the
   order in which they should be put in the MRO, but it's hard to
   diagnose what constraint can't be satisfied.
*/

static void
set_mro_error(PyObject *to_merge, int *remain)
{
	Py_ssize_t i, n, off, to_merge_size;
	char buf[1000];
	PyObject *k, *v;
	PyObject *set = PyDict_New();
	if (!set) return;

	to_merge_size = PyList_GET_SIZE(to_merge);
	for (i = 0; i < to_merge_size; i++) {
		PyObject *L = PyList_GET_ITEM(to_merge, i);
		if (remain[i] < PyList_GET_SIZE(L)) {
			PyObject *c = PyList_GET_ITEM(L, remain[i]);
			if (PyDict_SetItem(set, c, Py_None) < 0) {
				Py_DECREF(set);
				return;
			}
		}
	}
	n = PyDict_Size(set);

	off = PyOS_snprintf(buf, sizeof(buf), "Cannot create a \
consistent method resolution\norder (MRO) for bases");
	i = 0;
	while (PyDict_Next(set, &i, &k, &v) && (size_t)off < sizeof(buf)) {
		PyObject *name = class_name(k);
		off += PyOS_snprintf(buf + off, sizeof(buf) - off, " %s",
				     name ? PyString_AS_STRING(name) : "?");
		Py_XDECREF(name);
		if (--n && (size_t)(off+1) < sizeof(buf)) {
			buf[off++] = ',';
			buf[off] = '\0';
		}
	}
	PyErr_SetString(PyExc_TypeError, buf);
	Py_DECREF(set);
}

static int
pmerge(PyObject *acc, PyObject* to_merge) {
	Py_ssize_t i, j, to_merge_size, empty_cnt;
	int *remain;
	int ok;

	to_merge_size = PyList_GET_SIZE(to_merge);

	/* remain stores an index into each sublist of to_merge.
	   remain[i] is the index of the next base in to_merge[i]
	   that is not included in acc.
	*/
	remain = (int *)PyMem_MALLOC(SIZEOF_INT*to_merge_size);
	if (remain == NULL)
		return -1;
	for (i = 0; i < to_merge_size; i++)
		remain[i] = 0;

  again:
	empty_cnt = 0;
	for (i = 0; i < to_merge_size; i++) {
		PyObject *candidate;

		PyObject *cur_list = PyList_GET_ITEM(to_merge, i);

		if (remain[i] >= PyList_GET_SIZE(cur_list)) {
			empty_cnt++;
			continue;
                }

		/* Choose next candidate for MRO.

		   The input sequences alone can determine the choice.
		   If not, choose the class which appears in the MRO
		   of the earliest direct superclass of the new class.
		*/

		candidate = PyList_GET_ITEM(cur_list, remain[i]);
		for (j = 0; j < to_merge_size; j++) {
			PyObject *j_lst = PyList_GET_ITEM(to_merge, j);
			if (tail_contains(j_lst, remain[j], candidate)) {
				goto skip; /* continue outer loop */
			}
		}
		ok = PyList_Append(acc, candidate);
		if (ok < 0) {
			PyMem_Free(remain);
			return -1;
		}
		for (j = 0; j < to_merge_size; j++) {
			PyObject *j_lst = PyList_GET_ITEM(to_merge, j);
			if (remain[j] < PyList_GET_SIZE(j_lst) &&
			    PyList_GET_ITEM(j_lst, remain[j]) == candidate) {
				remain[j]++;
			}
		}
		goto again;
	  skip: ;
	}

	if (empty_cnt == to_merge_size) {
		PyMem_FREE(remain);
		return 0;
	}
	set_mro_error(to_merge, remain);
	PyMem_FREE(remain);
	return -1;
}

static PyObject *
mro_implementation(PyTypeObject *type)
{
	Py_ssize_t i, n;
	int ok;
	PyObject *bases, *result;
	PyObject *to_merge, *bases_aslist;

	if(type->tp_dict == NULL) {
		if(PyType_Ready(type) < 0)
			return NULL;
	}

	/* Find a superclass linearization that honors the constraints
	   of the explicit lists of bases and the constraints implied by
	   each base class.

	   to_merge is a list of lists, where each list is a superclass
	   linearization implied by a base class.  The last element of
	   to_merge is the declared list of bases.
	*/

	bases = type->tp_bases;
	n = PyTuple_GET_SIZE(bases);

	to_merge = PyList_New(n+1);
	if (to_merge == NULL)
		return NULL;

	for (i = 0; i < n; i++) {
		PyObject *base = PyTuple_GET_ITEM(bases, i);
		PyObject *parentMRO;
		if (PyType_Check(base))
			parentMRO = PySequence_List(
				((PyTypeObject*)base)->tp_mro);
		else
			parentMRO = classic_mro(base);
		if (parentMRO == NULL) {
			Py_DECREF(to_merge);
			return NULL;
	        }

		PyList_SET_ITEM(to_merge, i, parentMRO);
	}

	bases_aslist = PySequence_List(bases);
	if (bases_aslist == NULL) {
		Py_DECREF(to_merge);
		return NULL;
	}
	/* This is just a basic sanity check. */
	if (check_duplicates(bases_aslist) < 0) {
		Py_DECREF(to_merge);
		Py_DECREF(bases_aslist);
		return NULL;
	}
	PyList_SET_ITEM(to_merge, n, bases_aslist);

	result = Py_BuildValue("[O]", (PyObject *)type);
	if (result == NULL) {
		Py_DECREF(to_merge);
		return NULL;
	}

	ok = pmerge(result, to_merge);
	Py_DECREF(to_merge);
	if (ok < 0) {
		Py_DECREF(result);
		return NULL;
	}

	return result;
}

static PyObject *
mro_external(PyObject *self)
{
	PyTypeObject *type = (PyTypeObject *)self;

	return mro_implementation(type);
}

static int
mro_internal(PyTypeObject *type)
{
	PyObject *mro, *result, *tuple;
	int checkit = 0;

	if (type->ob_type == &PyType_Type) {
		result = mro_implementation(type);
	}
	else {
		static PyObject *mro_str;
		checkit = 1;
		mro = lookup_method((PyObject *)type, "mro", &mro_str);
		if (mro == NULL)
			return -1;
		result = PyObject_CallObject(mro, NULL);
		Py_DECREF(mro);
	}
	if (result == NULL)
		return -1;
	tuple = PySequence_Tuple(result);
	Py_DECREF(result);
	if (tuple == NULL)
		return -1;
	if (checkit) {
		Py_ssize_t i, len;
		PyObject *cls;
		PyTypeObject *solid;

		solid = solid_base(type);

		len = PyTuple_GET_SIZE(tuple);

		for (i = 0; i < len; i++) {
			PyTypeObject *t;
			cls = PyTuple_GET_ITEM(tuple, i);
			if (PyClass_Check(cls)) 
				continue;
			else if (!PyType_Check(cls)) {
				PyErr_Format(PyExc_TypeError,
			     "mro() returned a non-class ('%.500s')",
					     cls->ob_type->tp_name);
				Py_DECREF(tuple);
				return -1;
			}
			t = (PyTypeObject*)cls;
			if (!PyType_IsSubtype(solid, solid_base(t))) {
				PyErr_Format(PyExc_TypeError,
		     "mro() returned base with unsuitable layout ('%.500s')",
					     t->tp_name);
				Py_DECREF(tuple);
				return -1;
			}
		}
	}
	type->tp_mro = tuple;
	return 0;
}


/* Calculate the best base amongst multiple base classes.
   This is the first one that's on the path to the "solid base". */

static PyTypeObject *
best_base(PyObject *bases)
{
	Py_ssize_t i, n;
	PyTypeObject *base, *winner, *candidate, *base_i;
	PyObject *base_proto;

	assert(PyTuple_Check(bases));
	n = PyTuple_GET_SIZE(bases);
	assert(n > 0);
	base = NULL;
	winner = NULL;
	for (i = 0; i < n; i++) {
		base_proto = PyTuple_GET_ITEM(bases, i);
		if (PyClass_Check(base_proto))
			continue;
		if (!PyType_Check(base_proto)) {
			PyErr_SetString(
				PyExc_TypeError,
				"bases must be types");
			return NULL;
		}
		base_i = (PyTypeObject *)base_proto;
		if (base_i->tp_dict == NULL) {
			if (PyType_Ready(base_i) < 0)
				return NULL;
		}
		candidate = solid_base(base_i);
		if (winner == NULL) {
			winner = candidate;
			base = base_i;
		}
		else if (PyType_IsSubtype(winner, candidate))
			;
		else if (PyType_IsSubtype(candidate, winner)) {
			winner = candidate;
			base = base_i;
		}
		else {
			PyErr_SetString(
				PyExc_TypeError,
				"multiple bases have "
				"instance lay-out conflict");
			return NULL;
		}
	}
	if (base == NULL)
		PyErr_SetString(PyExc_TypeError,
			"a new-style class can't have only classic bases");
	return base;
}

static int
extra_ivars(PyTypeObject *type, PyTypeObject *base)
{
	size_t t_size = type->tp_basicsize;
	size_t b_size = base->tp_basicsize;

	assert(t_size >= b_size); /* Else type smaller than base! */
	if (type->tp_itemsize || base->tp_itemsize) {
		/* If itemsize is involved, stricter rules */
		return t_size != b_size ||
			type->tp_itemsize != base->tp_itemsize;
	}
	if (type->tp_weaklistoffset && base->tp_weaklistoffset == 0 &&
	    type->tp_weaklistoffset + sizeof(PyObject *) == t_size)
		t_size -= sizeof(PyObject *);
	if (type->tp_dictoffset && base->tp_dictoffset == 0 &&
	    type->tp_dictoffset + sizeof(PyObject *) == t_size)
		t_size -= sizeof(PyObject *);

	return t_size != b_size;
}

static PyTypeObject *
solid_base(PyTypeObject *type)
{
	PyTypeObject *base;

	if (type->tp_base)
		base = solid_base(type->tp_base);
	else
		base = &PyBaseObject_Type;
	if (extra_ivars(type, base))
		return type;
	else
		return base;
}

static void object_dealloc(PyObject *);
static int object_init(PyObject *, PyObject *, PyObject *);
static int update_slot(PyTypeObject *, PyObject *);
static void fixup_slot_dispatchers(PyTypeObject *);

static PyObject *
subtype_dict(PyObject *obj, void *context)
{
	PyObject **dictptr = _PyObject_GetDictPtr(obj);
	PyObject *dict;

	if (dictptr == NULL) {
		PyErr_SetString(PyExc_AttributeError,
				"This object has no __dict__");
		return NULL;
	}
	dict = *dictptr;
	if (dict == NULL)
		*dictptr = dict = PyDict_New();
	Py_XINCREF(dict);
	return dict;
}

static int
subtype_setdict(PyObject *obj, PyObject *value, void *context)
{
	PyObject **dictptr = _PyObject_GetDictPtr(obj);
	PyObject *dict;

	if (dictptr == NULL) {
		PyErr_SetString(PyExc_AttributeError,
				"This object has no __dict__");
		return -1;
	}
	if (value != NULL && !PyDict_Check(value)) {
		PyErr_Format(PyExc_TypeError,
			     "__dict__ must be set to a dictionary, "
			     "not a '%.200s'", value->ob_type->tp_name);
		return -1;
	}
	dict = *dictptr;
	Py_XINCREF(value);
	*dictptr = value;
	Py_XDECREF(dict);
	return 0;
}

static PyObject *
subtype_getweakref(PyObject *obj, void *context)
{
	PyObject **weaklistptr;
	PyObject *result;

	if (obj->ob_type->tp_weaklistoffset == 0) {
		PyErr_SetString(PyExc_AttributeError,
				"This object has no __weakref__");
		return NULL;
	}
	assert(obj->ob_type->tp_weaklistoffset > 0);
	assert(obj->ob_type->tp_weaklistoffset + sizeof(PyObject *) <=
	       (size_t)(obj->ob_type->tp_basicsize));
	weaklistptr = (PyObject **)
		((char *)obj + obj->ob_type->tp_weaklistoffset);
	if (*weaklistptr == NULL)
		result = Py_None;
	else
		result = *weaklistptr;
	Py_INCREF(result);
	return result;
}

/* Three variants on the subtype_getsets list. */

static PyGetSetDef subtype_getsets_full[] = {
	{"__dict__", subtype_dict, subtype_setdict,
	 PyDoc_STR("dictionary for instance variables (if defined)")},
	{"__weakref__", subtype_getweakref, NULL,
	 PyDoc_STR("list of weak references to the object (if defined)")},
	{0}
};

static PyGetSetDef subtype_getsets_dict_only[] = {
	{"__dict__", subtype_dict, subtype_setdict,
	 PyDoc_STR("dictionary for instance variables (if defined)")},
	{0}
};

static PyGetSetDef subtype_getsets_weakref_only[] = {
	{"__weakref__", subtype_getweakref, NULL,
	 PyDoc_STR("list of weak references to the object (if defined)")},
	{0}
};

static int
valid_identifier(PyObject *s)
{
	unsigned char *p;
	Py_ssize_t i, n;

	if (!PyString_Check(s)) {
		PyErr_Format(PyExc_TypeError,
			     "__slots__ items must be strings, not '%.200s'",
			     s->ob_type->tp_name);
		return 0;
	}
	p = (unsigned char *) PyString_AS_STRING(s);
	n = PyString_GET_SIZE(s);
	/* We must reject an empty name.  As a hack, we bump the
	   length to 1 so that the loop will balk on the trailing \0. */
	if (n == 0)
		n = 1;
	for (i = 0; i < n; i++, p++) {
		if (!(i == 0 ? isalpha(*p) : isalnum(*p)) && *p != '_') {
			PyErr_SetString(PyExc_TypeError,
					"__slots__ must be identifiers");
			return 0;
		}
	}
	return 1;
}

#ifdef Py_USING_UNICODE
/* Replace Unicode objects in slots.  */

static PyObject *
_unicode_to_string(PyObject *slots, Py_ssize_t nslots)
{
	PyObject *tmp = NULL;
	PyObject *slot_name, *new_name;
	Py_ssize_t i;

	for (i = 0; i < nslots; i++) {
		if (PyUnicode_Check(slot_name = PyTuple_GET_ITEM(slots, i))) {
			if (tmp == NULL) {
				tmp = PySequence_List(slots);
				if (tmp == NULL)
					return NULL;
			}
			new_name = _PyUnicode_AsDefaultEncodedString(slot_name,
								     NULL);
			if (new_name == NULL) {
				Py_DECREF(tmp);
				return NULL;
			}
			Py_INCREF(new_name);
			PyList_SET_ITEM(tmp, i, new_name);
			Py_DECREF(slot_name);
		}
	}
	if (tmp != NULL) {
		slots = PyList_AsTuple(tmp);
		Py_DECREF(tmp);
	}
	return slots;
}
#endif

static PyObject *
type_new(PyTypeObject *metatype, PyObject *args, PyObject *kwds)
{
	PyObject *name, *bases, *dict;
	static char *kwlist[] = {"name", "bases", "dict", 0};
	PyObject *slots, *tmp, *newslots;
	PyTypeObject *type, *base, *tmptype, *winner;
	PyHeapTypeObject *et;
	PyMemberDef *mp;
	Py_ssize_t i, nbases, nslots, slotoffset, add_dict, add_weak;
	int j, may_add_dict, may_add_weak;

	assert(args != NULL && PyTuple_Check(args));
	assert(kwds == NULL || PyDict_Check(kwds));

	/* Special case: type(x) should return x->ob_type */
	{
		const Py_ssize_t nargs = PyTuple_GET_SIZE(args);
		const Py_ssize_t nkwds = kwds == NULL ? 0 : PyDict_Size(kwds);

		if (PyType_CheckExact(metatype) && nargs == 1 && nkwds == 0) {
			PyObject *x = PyTuple_GET_ITEM(args, 0);
			Py_INCREF(x->ob_type);
			return (PyObject *) x->ob_type;
		}

		/* SF bug 475327 -- if that didn't trigger, we need 3
		   arguments. but PyArg_ParseTupleAndKeywords below may give
		   a msg saying type() needs exactly 3. */
		if (nargs + nkwds != 3) {
			PyErr_SetString(PyExc_TypeError,
					"type() takes 1 or 3 arguments");
			return NULL;
		}
	}

	/* Check arguments: (name, bases, dict) */
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "SO!O!:type", kwlist,
					 &name,
					 &PyTuple_Type, &bases,
					 &PyDict_Type, &dict))
		return NULL;

	/* Determine the proper metatype to deal with this,
	   and check for metatype conflicts while we're at it.
	   Note that if some other metatype wins to contract,
	   it's possible that its instances are not types. */
	nbases = PyTuple_GET_SIZE(bases);
	winner = metatype;
	for (i = 0; i < nbases; i++) {
		tmp = PyTuple_GET_ITEM(bases, i);
		tmptype = tmp->ob_type;
		if (tmptype == &PyClass_Type)
			continue; /* Special case classic classes */
		if (PyType_IsSubtype(winner, tmptype))
			continue;
		if (PyType_IsSubtype(tmptype, winner)) {
			winner = tmptype;
			continue;
		}
		PyErr_SetString(PyExc_TypeError,
				"metaclass conflict: "
				"the metaclass of a derived class "
				"must be a (non-strict) subclass "
				"of the metaclasses of all its bases");
		return NULL;
	}
	if (winner != metatype) {
		if (winner->tp_new != type_new) /* Pass it to the winner */
			return winner->tp_new(winner, args, kwds);
		metatype = winner;
	}

	/* Adjust for empty tuple bases */
	if (nbases == 0) {
		bases = PyTuple_Pack(1, &PyBaseObject_Type);
		if (bases == NULL)
			return NULL;
		nbases = 1;
	}
	else
		Py_INCREF(bases);

	/* XXX From here until type is allocated, "return NULL" leaks bases! */

	/* Calculate best base, and check that all bases are type objects */
	base = best_base(bases);
	if (base == NULL) {
		Py_DECREF(bases);
		return NULL;
	}
	if (!PyType_HasFeature(base, Py_TPFLAGS_BASETYPE)) {
		PyErr_Format(PyExc_TypeError,
			     "type '%.100s' is not an acceptable base type",
			     base->tp_name);
		Py_DECREF(bases);
		return NULL;
	}

	/* Check for a __slots__ sequence variable in dict, and count it */
	slots = PyDict_GetItemString(dict, "__slots__");
	nslots = 0;
	add_dict = 0;
	add_weak = 0;
	may_add_dict = base->tp_dictoffset == 0;
	may_add_weak = base->tp_weaklistoffset == 0 && base->tp_itemsize == 0;
	if (slots == NULL) {
		if (may_add_dict) {
			add_dict++;
		}
		if (may_add_weak) {
			add_weak++;
		}
	}
	else {
		/* Have slots */

		/* Make it into a tuple */
		if (PyString_Check(slots))
			slots = PyTuple_Pack(1, slots);
		else
			slots = PySequence_Tuple(slots);
		if (slots == NULL) {
			Py_DECREF(bases);
			return NULL;
		}
		assert(PyTuple_Check(slots));

		/* Are slots allowed? */
		nslots = PyTuple_GET_SIZE(slots);
		if (nslots > 0 && base->tp_itemsize != 0) {
			PyErr_Format(PyExc_TypeError,
				     "nonempty __slots__ "
				     "not supported for subtype of '%s'",
				     base->tp_name);
		  bad_slots:
			Py_DECREF(bases);
			Py_DECREF(slots);
			return NULL;
		}

#ifdef Py_USING_UNICODE
		tmp = _unicode_to_string(slots, nslots);
		if (tmp == NULL)
			goto bad_slots;
		if (tmp != slots) {
			Py_DECREF(slots);
			slots = tmp;
		}
#endif
		/* Check for valid slot names and two special cases */
		for (i = 0; i < nslots; i++) {
			PyObject *tmp = PyTuple_GET_ITEM(slots, i);
			char *s;
			if (!valid_identifier(tmp))
				goto bad_slots;
			assert(PyString_Check(tmp));
			s = PyString_AS_STRING(tmp);
			if (strcmp(s, "__dict__") == 0) {
				if (!may_add_dict || add_dict) {
					PyErr_SetString(PyExc_TypeError,
						"__dict__ slot disallowed: "
						"we already got one");
					goto bad_slots;
				}
				add_dict++;
			}
			if (strcmp(s, "__weakref__") == 0) {
				if (!may_add_weak || add_weak) {
					PyErr_SetString(PyExc_TypeError,
						"__weakref__ slot disallowed: "
						"either we already got one, "
						"or __itemsize__ != 0");
					goto bad_slots;
				}
				add_weak++;
			}
		}

		/* Copy slots into yet another tuple, demangling names */
		newslots = PyTuple_New(nslots - add_dict - add_weak);
		if (newslots == NULL)
			goto bad_slots;
		for (i = j = 0; i < nslots; i++) {
			char *s;
			tmp = PyTuple_GET_ITEM(slots, i);
			s = PyString_AS_STRING(tmp);
			if ((add_dict && strcmp(s, "__dict__") == 0) ||
			    (add_weak && strcmp(s, "__weakref__") == 0))
				continue;
			tmp =_Py_Mangle(name, tmp);
                        if (!tmp)
                            goto bad_slots;
			PyTuple_SET_ITEM(newslots, j, tmp);
			j++;
		}
		assert(j == nslots - add_dict - add_weak);
		nslots = j;
		Py_DECREF(slots);
		slots = newslots;

		/* Secondary bases may provide weakrefs or dict */
		if (nbases > 1 &&
		    ((may_add_dict && !add_dict) ||
		     (may_add_weak && !add_weak))) {
			for (i = 0; i < nbases; i++) {
				tmp = PyTuple_GET_ITEM(bases, i);
				if (tmp == (PyObject *)base)
					continue; /* Skip primary base */
				if (PyClass_Check(tmp)) {
					/* Classic base class provides both */
					if (may_add_dict && !add_dict)
						add_dict++;
					if (may_add_weak && !add_weak)
						add_weak++;
					break;
				}
				assert(PyType_Check(tmp));
				tmptype = (PyTypeObject *)tmp;
				if (may_add_dict && !add_dict &&
				    tmptype->tp_dictoffset != 0)
					add_dict++;
				if (may_add_weak && !add_weak &&
				    tmptype->tp_weaklistoffset != 0)
					add_weak++;
				if (may_add_dict && !add_dict)
					continue;
				if (may_add_weak && !add_weak)
					continue;
				/* Nothing more to check */
				break;
			}
		}
	}

	/* XXX From here until type is safely allocated,
	   "return NULL" may leak slots! */

	/* Allocate the type object */
	type = (PyTypeObject *)metatype->tp_alloc(metatype, nslots);
	if (type == NULL) {
		Py_XDECREF(slots);
		Py_DECREF(bases);
		return NULL;
	}

	/* Keep name and slots alive in the extended type object */
	et = (PyHeapTypeObject *)type;
	Py_INCREF(name);
	et->ht_name = name;
	et->ht_slots = slots;

	/* Initialize tp_flags */
	type->tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HEAPTYPE |
		Py_TPFLAGS_BASETYPE;
	if (base->tp_flags & Py_TPFLAGS_HAVE_GC)
		type->tp_flags |= Py_TPFLAGS_HAVE_GC;

	/* It's a new-style number unless it specifically inherits any
	   old-style numeric behavior */
	if ((base->tp_flags & Py_TPFLAGS_CHECKTYPES) ||
	    (base->tp_as_number == NULL))
		type->tp_flags |= Py_TPFLAGS_CHECKTYPES;

	/* Initialize essential fields */
	type->tp_as_number = &et->as_number;
	type->tp_as_sequence = &et->as_sequence;
	type->tp_as_mapping = &et->as_mapping;
	type->tp_as_buffer = &et->as_buffer;
	type->tp_name = PyString_AS_STRING(name);

	/* Set tp_base and tp_bases */
	type->tp_bases = bases;
	Py_INCREF(base);
	type->tp_base = base;

	/* Initialize tp_dict from passed-in dict */
	type->tp_dict = dict = PyDict_Copy(dict);
	if (dict == NULL) {
		Py_DECREF(type);
		return NULL;
	}

	/* Set __module__ in the dict */
	if (PyDict_GetItemString(dict, "__module__") == NULL) {
		tmp = PyEval_GetGlobals();
		if (tmp != NULL) {
			tmp = PyDict_GetItemString(tmp, "__name__");
			if (tmp != NULL) {
				if (PyDict_SetItemString(dict, "__module__",
							 tmp) < 0)
					return NULL;
			}
		}
	}

	/* Set tp_doc to a copy of dict['__doc__'], if the latter is there
	   and is a string.  The __doc__ accessor will first look for tp_doc;
	   if that fails, it will still look into __dict__.
	*/
	{
		PyObject *doc = PyDict_GetItemString(dict, "__doc__");
		if (doc != NULL && PyString_Check(doc)) {
			const size_t n = (size_t)PyString_GET_SIZE(doc);
                        char *tp_doc = (char *)PyObject_MALLOC(n+1);
			if (tp_doc == NULL) {
				Py_DECREF(type);
				return NULL;
			}
			memcpy(tp_doc, PyString_AS_STRING(doc), n+1);
                        type->tp_doc = tp_doc;
		}
	}

	/* Special-case __new__: if it's a plain function,
	   make it a static function */
	tmp = PyDict_GetItemString(dict, "__new__");
	if (tmp != NULL && PyFunction_Check(tmp)) {
		tmp = PyStaticMethod_New(tmp);
		if (tmp == NULL) {
			Py_DECREF(type);
			return NULL;
		}
		PyDict_SetItemString(dict, "__new__", tmp);
		Py_DECREF(tmp);
	}

	/* Add descriptors for custom slots from __slots__, or for __dict__ */
	mp = PyHeapType_GET_MEMBERS(et);
	slotoffset = base->tp_basicsize;
	if (slots != NULL) {
		for (i = 0; i < nslots; i++, mp++) {
			mp->name = PyString_AS_STRING(
				PyTuple_GET_ITEM(slots, i));
			mp->type = T_OBJECT_EX;
			mp->offset = slotoffset;

			/* __dict__ and __weakref__ are already filtered out */
			assert(strcmp(mp->name, "__dict__") != 0);
			assert(strcmp(mp->name, "__weakref__") != 0);

			slotoffset += sizeof(PyObject *);
		}
	}
	if (add_dict) {
		if (base->tp_itemsize)
			type->tp_dictoffset = -(long)sizeof(PyObject *);
		else
			type->tp_dictoffset = slotoffset;
		slotoffset += sizeof(PyObject *);
	}
	if (add_weak) {
		assert(!base->tp_itemsize);
		type->tp_weaklistoffset = slotoffset;
		slotoffset += sizeof(PyObject *);
	}
	type->tp_basicsize = slotoffset;
	type->tp_itemsize = base->tp_itemsize;
	type->tp_members = PyHeapType_GET_MEMBERS(et);

	if (type->tp_weaklistoffset && type->tp_dictoffset)
		type->tp_getset = subtype_getsets_full;
	else if (type->tp_weaklistoffset && !type->tp_dictoffset)
		type->tp_getset = subtype_getsets_weakref_only;
	else if (!type->tp_weaklistoffset && type->tp_dictoffset)
		type->tp_getset = subtype_getsets_dict_only;
	else
		type->tp_getset = NULL;

	/* Special case some slots */
	if (type->tp_dictoffset != 0 || nslots > 0) {
		if (base->tp_getattr == NULL && base->tp_getattro == NULL)
			type->tp_getattro = PyObject_GenericGetAttr;
		if (base->tp_setattr == NULL && base->tp_setattro == NULL)
			type->tp_setattro = PyObject_GenericSetAttr;
	}
	type->tp_dealloc = subtype_dealloc;

	/* Enable GC unless there are really no instance variables possible */
	if (!(type->tp_basicsize == sizeof(PyObject) &&
	      type->tp_itemsize == 0))
		type->tp_flags |= Py_TPFLAGS_HAVE_GC;

	/* Always override allocation strategy to use regular heap */
	type->tp_alloc = PyType_GenericAlloc;
	if (type->tp_flags & Py_TPFLAGS_HAVE_GC) {
		type->tp_free = PyObject_GC_Del;
		type->tp_traverse = subtype_traverse;
		type->tp_clear = subtype_clear;
	}
	else
		type->tp_free = PyObject_Del;

	/* Initialize the rest */
	if (PyType_Ready(type) < 0) {
		Py_DECREF(type);
		return NULL;
	}

	/* Put the proper slots in place */
	fixup_slot_dispatchers(type);

	return (PyObject *)type;
}

/* Internal API to look for a name through the MRO.
   This returns a borrowed reference, and doesn't set an exception! */
PyObject *
_PyType_Lookup(PyTypeObject *type, PyObject *name)
{
	Py_ssize_t i, n;
	PyObject *mro, *res, *base, *dict;

	/* Look in tp_dict of types in MRO */
	mro = type->tp_mro;

	/* If mro is NULL, the type is either not yet initialized
	   by PyType_Ready(), or already cleared by type_clear().
	   Either way the safest thing to do is to return NULL. */
	if (mro == NULL)
		return NULL;

	assert(PyTuple_Check(mro));
	n = PyTuple_GET_SIZE(mro);
	for (i = 0; i < n; i++) {
		base = PyTuple_GET_ITEM(mro, i);
		if (PyClass_Check(base))
			dict = ((PyClassObject *)base)->cl_dict;
		else {
			assert(PyType_Check(base));
			dict = ((PyTypeObject *)base)->tp_dict;
		}
		assert(dict && PyDict_Check(dict));
		res = PyDict_GetItem(dict, name);
		if (res != NULL)
			return res;
	}
	return NULL;
}

/* This is similar to PyObject_GenericGetAttr(),
   but uses _PyType_Lookup() instead of just looking in type->tp_dict. */
static PyObject *
type_getattro(PyTypeObject *type, PyObject *name)
{
	PyTypeObject *metatype = type->ob_type;
	PyObject *meta_attribute, *attribute;
	descrgetfunc meta_get;

	/* Initialize this type (we'll assume the metatype is initialized) */
	if (type->tp_dict == NULL) {
		if (PyType_Ready(type) < 0)
			return NULL;
	}

	/* No readable descriptor found yet */
	meta_get = NULL;

	/* Look for the attribute in the metatype */
	meta_attribute = _PyType_Lookup(metatype, name);

	if (meta_attribute != NULL) {
		meta_get = meta_attribute->ob_type->tp_descr_get;

		if (meta_get != NULL && PyDescr_IsData(meta_attribute)) {
			/* Data descriptors implement tp_descr_set to intercept
			 * writes. Assume the attribute is not overridden in
			 * type's tp_dict (and bases): call the descriptor now.
			 */
			return meta_get(meta_attribute, (PyObject *)type,
					(PyObject *)metatype);
		}
		Py_INCREF(meta_attribute);
	}

	/* No data descriptor found on metatype. Look in tp_dict of this
	 * type and its bases */
	attribute = _PyType_Lookup(type, name);
	if (attribute != NULL) {
		/* Implement descriptor functionality, if any */
		descrgetfunc local_get = attribute->ob_type->tp_descr_get;

		Py_XDECREF(meta_attribute);

		if (local_get != NULL) {
			/* NULL 2nd argument indicates the descriptor was
			 * found on the target object itself (or a base)  */
			return local_get(attribute, (PyObject *)NULL,
					 (PyObject *)type);
		}

		Py_INCREF(attribute);
		return attribute;
	}

	/* No attribute found in local __dict__ (or bases): use the
	 * descriptor from the metatype, if any */
	if (meta_get != NULL) {
		PyObject *res;
		res = meta_get(meta_attribute, (PyObject *)type,
			       (PyObject *)metatype);
		Py_DECREF(meta_attribute);
		return res;
	}

	/* If an ordinary attribute was found on the metatype, return it now */
	if (meta_attribute != NULL) {
		return meta_attribute;
	}

	/* Give up */
	PyErr_Format(PyExc_AttributeError,
			 "type object '%.50s' has no attribute '%.400s'",
			 type->tp_name, PyString_AS_STRING(name));
	return NULL;
}

static int
type_setattro(PyTypeObject *type, PyObject *name, PyObject *value)
{
	if (!(type->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
		PyErr_Format(
			PyExc_TypeError,
			"can't set attributes of built-in/extension type '%s'",
			type->tp_name);
		return -1;
	}
	/* XXX Example of how I expect this to be used...
	if (update_subclasses(type, name, invalidate_cache, NULL) < 0)
		return -1;
	*/
	if (PyObject_GenericSetAttr((PyObject *)type, name, value) < 0)
		return -1;
	return update_slot(type, name);
}

static void
type_dealloc(PyTypeObject *type)
{
	PyHeapTypeObject *et;

	/* Assert this is a heap-allocated type object */
	assert(type->tp_flags & Py_TPFLAGS_HEAPTYPE);
	_PyObject_GC_UNTRACK(type);
	PyObject_ClearWeakRefs((PyObject *)type);
	et = (PyHeapTypeObject *)type;
	Py_XDECREF(type->tp_base);
	Py_XDECREF(type->tp_dict);
	Py_XDECREF(type->tp_bases);
	Py_XDECREF(type->tp_mro);
	Py_XDECREF(type->tp_cache);
	Py_XDECREF(type->tp_subclasses);
        /* A type's tp_doc is heap allocated, unlike the tp_doc slots
         * of most other objects.  It's okay to cast it to char *.
         */
	PyObject_Free((char *)type->tp_doc);
	Py_XDECREF(et->ht_name);
	Py_XDECREF(et->ht_slots);
	type->ob_type->tp_free((PyObject *)type);
}

static PyObject *
type_subclasses(PyTypeObject *type, PyObject *args_ignored)
{
	PyObject *list, *raw, *ref;
	Py_ssize_t i, n;

	list = PyList_New(0);
	if (list == NULL)
		return NULL;
	raw = type->tp_subclasses;
	if (raw == NULL)
		return list;
	assert(PyList_Check(raw));
	n = PyList_GET_SIZE(raw);
	for (i = 0; i < n; i++) {
		ref = PyList_GET_ITEM(raw, i);
		assert(PyWeakref_CheckRef(ref));
		ref = PyWeakref_GET_OBJECT(ref);
		if (ref != Py_None) {
			if (PyList_Append(list, ref) < 0) {
				Py_DECREF(list);
				return NULL;
			}
		}
	}
	return list;
}

static PyMethodDef type_methods[] = {
	{"mro", (PyCFunction)mro_external, METH_NOARGS,
	 PyDoc_STR("mro() -> list\nreturn a type's method resolution order")},
	{"__subclasses__", (PyCFunction)type_subclasses, METH_NOARGS,
	 PyDoc_STR("__subclasses__() -> list of immediate subclasses")},
	{0}
};

PyDoc_STRVAR(type_doc,
"type(object) -> the object's type\n"
"type(name, bases, dict) -> a new type");

static int
type_traverse(PyTypeObject *type, visitproc visit, void *arg)
{
	/* Because of type_is_gc(), the collector only calls this
	   for heaptypes. */
	assert(type->tp_flags & Py_TPFLAGS_HEAPTYPE);

	Py_VISIT(type->tp_dict);
	Py_VISIT(type->tp_cache);
	Py_VISIT(type->tp_mro);
	Py_VISIT(type->tp_bases);
	Py_VISIT(type->tp_base);

	/* There's no need to visit type->tp_subclasses or
	   ((PyHeapTypeObject *)type)->ht_slots, because they can't be involved
	   in cycles; tp_subclasses is a list of weak references,
	   and slots is a tuple of strings. */

	return 0;
}

static int
type_clear(PyTypeObject *type)
{
	/* Because of type_is_gc(), the collector only calls this
	   for heaptypes. */
	assert(type->tp_flags & Py_TPFLAGS_HEAPTYPE);

	/* The only field we need to clear is tp_mro, which is part of a
	   hard cycle (its first element is the class itself) that won't
	   be broken otherwise (it's a tuple and tuples don't have a
	   tp_clear handler).  None of the other fields need to be
	   cleared, and here's why:

	   tp_dict:
	       It is a dict, so the collector will call its tp_clear.

	   tp_cache:
	       Not used; if it were, it would be a dict.

	   tp_bases, tp_base:
	       If these are involved in a cycle, there must be at least
	       one other, mutable object in the cycle, e.g. a base
	       class's dict; the cycle will be broken that way.

	   tp_subclasses:
	       A list of weak references can't be part of a cycle; and
	       lists have their own tp_clear.

	   slots (in PyHeapTypeObject):
	       A tuple of strings can't be part of a cycle.
	*/

	Py_CLEAR(type->tp_mro);

	return 0;
}

static int
type_is_gc(PyTypeObject *type)
{
	return type->tp_flags & Py_TPFLAGS_HEAPTYPE;
}

PyTypeObject PyType_Type = {
	PyObject_HEAD_INIT(&PyType_Type)
	0,					/* ob_size */
	"type",					/* tp_name */
	sizeof(PyHeapTypeObject),		/* tp_basicsize */
	sizeof(PyMemberDef),			/* tp_itemsize */
	(destructor)type_dealloc,		/* tp_dealloc */
	0,					/* tp_print */
	0,			 		/* tp_getattr */
	0,					/* tp_setattr */
	type_compare,				/* tp_compare */
	(reprfunc)type_repr,			/* tp_repr */
	0,					/* tp_as_number */
	0,					/* tp_as_sequence */
	0,					/* tp_as_mapping */
	(hashfunc)_Py_HashPointer,		/* tp_hash */
	(ternaryfunc)type_call,			/* tp_call */
	0,					/* tp_str */
	(getattrofunc)type_getattro,		/* tp_getattro */
	(setattrofunc)type_setattro,		/* tp_setattro */
	0,					/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
		Py_TPFLAGS_BASETYPE,		/* tp_flags */
	type_doc,				/* tp_doc */
	(traverseproc)type_traverse,		/* tp_traverse */
	(inquiry)type_clear,			/* tp_clear */
	0,					/* tp_richcompare */
	offsetof(PyTypeObject, tp_weaklist),	/* tp_weaklistoffset */
	0,					/* tp_iter */
	0,					/* tp_iternext */
	type_methods,				/* tp_methods */
	type_members,				/* tp_members */
	type_getsets,				/* tp_getset */
	0,					/* tp_base */
	0,					/* tp_dict */
	0,					/* tp_descr_get */
	0,					/* tp_descr_set */
	offsetof(PyTypeObject, tp_dict),	/* tp_dictoffset */
	0,					/* tp_init */
	0,					/* tp_alloc */
	type_new,				/* tp_new */
	PyObject_GC_Del,        		/* tp_free */
	(inquiry)type_is_gc,			/* tp_is_gc */
};


/* The base type of all types (eventually)... except itself. */

static int
object_init(PyObject *self, PyObject *args, PyObject *kwds)
{
	return 0;
}

/* If we don't have a tp_new for a new-style class, new will use this one.
   Therefore this should take no arguments/keywords.  However, this new may
   also be inherited by objects that define a tp_init but no tp_new.  These
   objects WILL pass argumets to tp_new, because it gets the same args as
   tp_init.  So only allow arguments if we aren't using the default init, in
   which case we expect init to handle argument parsing. */
static PyObject *
object_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	if (type->tp_init == object_init && (PyTuple_GET_SIZE(args) ||
	     (kwds && PyDict_Check(kwds) && PyDict_Size(kwds)))) {
		PyErr_SetString(PyExc_TypeError,
				"default __new__ takes no parameters");
		return NULL;
	}
	return type->tp_alloc(type, 0);
}

static void
object_dealloc(PyObject *self)
{
	self->ob_type->tp_free(self);
}

static PyObject *
object_repr(PyObject *self)
{
	PyTypeObject *type;
	PyObject *mod, *name, *rtn;

	type = self->ob_type;
	mod = type_module(type, NULL);
	if (mod == NULL)
		PyErr_Clear();
	else if (!PyString_Check(mod)) {
		Py_DECREF(mod);
		mod = NULL;
	}
	name = type_name(type, NULL);
	if (name == NULL)
		return NULL;
	if (mod != NULL && strcmp(PyString_AS_STRING(mod), "__builtin__"))
		rtn = PyString_FromFormat("<%s.%s object at %p>",
					  PyString_AS_STRING(mod),
					  PyString_AS_STRING(name),
					  self);
	else
		rtn = PyString_FromFormat("<%s object at %p>",
					  type->tp_name, self);
	Py_XDECREF(mod);
	Py_DECREF(name);
	return rtn;
}

static PyObject *
object_str(PyObject *self)
{
	unaryfunc f;

	f = self->ob_type->tp_repr;
	if (f == NULL)
		f = object_repr;
	return f(self);
}

static long
object_hash(PyObject *self)
{
	return _Py_HashPointer(self);
}

static PyObject *
object_get_class(PyObject *self, void *closure)
{
	Py_INCREF(self->ob_type);
	return (PyObject *)(self->ob_type);
}

static int
equiv_structs(PyTypeObject *a, PyTypeObject *b)
{
	return a == b ||
	       (a != NULL &&
		b != NULL &&
		a->tp_basicsize == b->tp_basicsize &&
		a->tp_itemsize == b->tp_itemsize &&
		a->tp_dictoffset == b->tp_dictoffset &&
		a->tp_weaklistoffset == b->tp_weaklistoffset &&
		((a->tp_flags & Py_TPFLAGS_HAVE_GC) ==
		 (b->tp_flags & Py_TPFLAGS_HAVE_GC)));
}

static int
same_slots_added(PyTypeObject *a, PyTypeObject *b)
{
	PyTypeObject *base = a->tp_base;
	Py_ssize_t size;

	if (base != b->tp_base)
		return 0;
	if (equiv_structs(a, base) && equiv_structs(b, base))
		return 1;
	size = base->tp_basicsize;
	if (a->tp_dictoffset == size && b->tp_dictoffset == size)
		size += sizeof(PyObject *);
	if (a->tp_weaklistoffset == size && b->tp_weaklistoffset == size)
		size += sizeof(PyObject *);
	return size == a->tp_basicsize && size == b->tp_basicsize;
}

static int
compatible_for_assignment(PyTypeObject* oldto, PyTypeObject* newto, char* attr)
{
	PyTypeObject *newbase, *oldbase;

	if (newto->tp_dealloc != oldto->tp_dealloc ||
	    newto->tp_free != oldto->tp_free)
	{
		PyErr_Format(PyExc_TypeError,
			     "%s assignment: "
			     "'%s' deallocator differs from '%s'",
			     attr,
			     newto->tp_name,
			     oldto->tp_name);
		return 0;
	}
	newbase = newto;
	oldbase = oldto;
	while (equiv_structs(newbase, newbase->tp_base))
		newbase = newbase->tp_base;
	while (equiv_structs(oldbase, oldbase->tp_base))
		oldbase = oldbase->tp_base;
	if (newbase != oldbase &&
	    (newbase->tp_base != oldbase->tp_base ||
	     !same_slots_added(newbase, oldbase))) {
		PyErr_Format(PyExc_TypeError,
			     "%s assignment: "
			     "'%s' object layout differs from '%s'",
			     attr,
			     newto->tp_name,
			     oldto->tp_name);
		return 0;
	}

	return 1;
}

static int
object_set_class(PyObject *self, PyObject *value, void *closure)
{
	PyTypeObject *oldto = self->ob_type;
	PyTypeObject *newto;

	if (value == NULL) {
		PyErr_SetString(PyExc_TypeError,
				"can't delete __class__ attribute");
		return -1;
	}
	if (!PyType_Check(value)) {
		PyErr_Format(PyExc_TypeError,
		  "__class__ must be set to new-style class, not '%s' object",
		  value->ob_type->tp_name);
		return -1;
	}
	newto = (PyTypeObject *)value;
	if (!(newto->tp_flags & Py_TPFLAGS_HEAPTYPE) ||
	    !(oldto->tp_flags & Py_TPFLAGS_HEAPTYPE))
	{
		PyErr_Format(PyExc_TypeError,
			     "__class__ assignment: only for heap types");
		return -1;
	}
	if (compatible_for_assignment(newto, oldto, "__class__")) {
		Py_INCREF(newto);
		self->ob_type = newto;
		Py_DECREF(oldto);
		return 0;
	}
	else {
		return -1;
	}
}

static PyGetSetDef object_getsets[] = {
	{"__class__", object_get_class, object_set_class,
	 PyDoc_STR("the object's class")},
	{0}
};


/* Stuff to implement __reduce_ex__ for pickle protocols >= 2.
   We fall back to helpers in copy_reg for:
   - pickle protocols < 2
   - calculating the list of slot names (done only once per class)
   - the __newobj__ function (which is used as a token but never called)
*/

static PyObject *
import_copy_reg(void)
{
	static PyObject *copy_reg_str;

	if (!copy_reg_str) {
		copy_reg_str = PyString_InternFromString("copy_reg");
		if (copy_reg_str == NULL)
			return NULL;
	}

	return PyImport_Import(copy_reg_str);
}

static PyObject *
slotnames(PyObject *cls)
{
	PyObject *clsdict;
	PyObject *copy_reg;
	PyObject *slotnames;

	if (!PyType_Check(cls)) {
		Py_INCREF(Py_None);
		return Py_None;
	}

	clsdict = ((PyTypeObject *)cls)->tp_dict;
	slotnames = PyDict_GetItemString(clsdict, "__slotnames__");
	if (slotnames != NULL && PyList_Check(slotnames)) {
		Py_INCREF(slotnames);
		return slotnames;
	}

	copy_reg = import_copy_reg();
	if (copy_reg == NULL)
		return NULL;

	slotnames = PyObject_CallMethod(copy_reg, "_slotnames", "O", cls);
	Py_DECREF(copy_reg);
	if (slotnames != NULL &&
	    slotnames != Py_None &&
	    !PyList_Check(slotnames))
	{
		PyErr_SetString(PyExc_TypeError,
			"copy_reg._slotnames didn't return a list or None");
		Py_DECREF(slotnames);
		slotnames = NULL;
	}

	return slotnames;
}

static PyObject *
reduce_2(PyObject *obj)
{
	PyObject *cls, *getnewargs;
	PyObject *args = NULL, *args2 = NULL;
	PyObject *getstate = NULL, *state = NULL, *names = NULL;
	PyObject *slots = NULL, *listitems = NULL, *dictitems = NULL;
	PyObject *copy_reg = NULL, *newobj = NULL, *res = NULL;
	Py_ssize_t i, n;

	cls = PyObject_GetAttrString(obj, "__class__");
	if (cls == NULL)
		return NULL;

	getnewargs = PyObject_GetAttrString(obj, "__getnewargs__");
	if (getnewargs != NULL) {
		args = PyObject_CallObject(getnewargs, NULL);
		Py_DECREF(getnewargs);
		if (args != NULL && !PyTuple_Check(args)) {
			PyErr_Format(PyExc_TypeError,
				"__getnewargs__ should return a tuple, "
				"not '%.200s'", args->ob_type->tp_name);
			goto end;
		}
	}
	else {
		PyErr_Clear();
		args = PyTuple_New(0);
	}
	if (args == NULL)
		goto end;

	getstate = PyObject_GetAttrString(obj, "__getstate__");
	if (getstate != NULL) {
		state = PyObject_CallObject(getstate, NULL);
		Py_DECREF(getstate);
		if (state == NULL)
			goto end;
	}
	else {
		PyErr_Clear();
		state = PyObject_GetAttrString(obj, "__dict__");
		if (state == NULL) {
			PyErr_Clear();
			state = Py_None;
			Py_INCREF(state);
		}
		names = slotnames(cls);
		if (names == NULL)
			goto end;
		if (names != Py_None) {
			assert(PyList_Check(names));
			slots = PyDict_New();
			if (slots == NULL)
				goto end;
			n = 0;
			/* Can't pre-compute the list size; the list
			   is stored on the class so accessible to other
			   threads, which may be run by DECREF */
			for (i = 0; i < PyList_GET_SIZE(names); i++) {
				PyObject *name, *value;
				name = PyList_GET_ITEM(names, i);
				value = PyObject_GetAttr(obj, name);
				if (value == NULL)
					PyErr_Clear();
				else {
					int err = PyDict_SetItem(slots, name,
								 value);
					Py_DECREF(value);
					if (err)
						goto end;
					n++;
				}
			}
			if (n) {
				state = Py_BuildValue("(NO)", state, slots);
				if (state == NULL)
					goto end;
			}
		}
	}

	if (!PyList_Check(obj)) {
		listitems = Py_None;
		Py_INCREF(listitems);
	}
	else {
		listitems = PyObject_GetIter(obj);
		if (listitems == NULL)
			goto end;
	}

	if (!PyDict_Check(obj)) {
		dictitems = Py_None;
		Py_INCREF(dictitems);
	}
	else {
		dictitems = PyObject_CallMethod(obj, "iteritems", "");
		if (dictitems == NULL)
			goto end;
	}

	copy_reg = import_copy_reg();
	if (copy_reg == NULL)
		goto end;
	newobj = PyObject_GetAttrString(copy_reg, "__newobj__");
	if (newobj == NULL)
		goto end;

	n = PyTuple_GET_SIZE(args);
	args2 = PyTuple_New(n+1);
	if (args2 == NULL)
		goto end;
	PyTuple_SET_ITEM(args2, 0, cls);
	cls = NULL;
	for (i = 0; i < n; i++) {
		PyObject *v = PyTuple_GET_ITEM(args, i);
		Py_INCREF(v);
		PyTuple_SET_ITEM(args2, i+1, v);
	}

	res = PyTuple_Pack(5, newobj, args2, state, listitems, dictitems);

  end:
	Py_XDECREF(cls);
	Py_XDECREF(args);
	Py_XDECREF(args2);
	Py_XDECREF(slots);
	Py_XDECREF(state);
	Py_XDECREF(names);
	Py_XDECREF(listitems);
	Py_XDECREF(dictitems);
	Py_XDECREF(copy_reg);
	Py_XDECREF(newobj);
	return res;
}

/*
 * There were two problems when object.__reduce__ and object.__reduce_ex__
 * were implemented in the same function:
 *  - trying to pickle an object with a custom __reduce__ method that
 *    fell back to object.__reduce__ in certain circumstances led to
 *    infinite recursion at Python level and eventual RuntimeError.
 *  - Pickling objects that lied about their type by overwriting the
 *    __class__ descriptor could lead to infinite recursion at C level
 *    and eventual segfault.
 *
 * Because of backwards compatibility, the two methods still have to
 * behave in the same way, even if this is not required by the pickle
 * protocol. This common functionality was moved to the _common_reduce
 * function.
 */
static PyObject *
_common_reduce(PyObject *self, int proto)
{
	PyObject *copy_reg, *res;

	if (proto >= 2)
		return reduce_2(self);

	copy_reg = import_copy_reg();
	if (!copy_reg)
		return NULL;

	res = PyEval_CallMethod(copy_reg, "_reduce_ex", "(Oi)", self, proto);
	Py_DECREF(copy_reg);

	return res;
}

static PyObject *
object_reduce(PyObject *self, PyObject *args)
{
	int proto = 0;

	if (!PyArg_ParseTuple(args, "|i:__reduce__", &proto))
		return NULL;

	return _common_reduce(self, proto);
}

static PyObject *
object_reduce_ex(PyObject *self, PyObject *args)
{
	PyObject *reduce, *res;
	int proto = 0;

	if (!PyArg_ParseTuple(args, "|i:__reduce_ex__", &proto))
		return NULL;

	reduce = PyObject_GetAttrString(self, "__reduce__");
	if (reduce == NULL)
		PyErr_Clear();
	else {
		PyObject *cls, *clsreduce, *objreduce;
		int override;
		cls = PyObject_GetAttrString(self, "__class__");
		if (cls == NULL) {
			Py_DECREF(reduce);
			return NULL;
		}
		clsreduce = PyObject_GetAttrString(cls, "__reduce__");
		Py_DECREF(cls);
		if (clsreduce == NULL) {
			Py_DECREF(reduce);
			return NULL;
		}
		objreduce = PyDict_GetItemString(PyBaseObject_Type.tp_dict,
						 "__reduce__");
		override = (clsreduce != objreduce);
		Py_DECREF(clsreduce);
		if (override) {
			res = PyObject_CallObject(reduce, NULL);
			Py_DECREF(reduce);
			return res;
		}
		else
			Py_DECREF(reduce);
	}

	return _common_reduce(self, proto);
}

static PyMethodDef object_methods[] = {
	{"__reduce_ex__", object_reduce_ex, METH_VARARGS,
	 PyDoc_STR("helper for pickle")},
	{"__reduce__", object_reduce, METH_VARARGS,
	 PyDoc_STR("helper for pickle")},
	{0}
};


PyTypeObject PyBaseObject_Type = {
	PyObject_HEAD_INIT(&PyType_Type)
 	0,					/* ob_size */
	"object",				/* tp_name */
	sizeof(PyObject),			/* tp_basicsize */
	0,					/* tp_itemsize */
	object_dealloc,				/* tp_dealloc */
	0,					/* tp_print */
	0,			 		/* tp_getattr */
	0,					/* tp_setattr */
	0,					/* tp_compare */
	object_repr,				/* tp_repr */
	0,					/* tp_as_number */
	0,					/* tp_as_sequence */
	0,					/* tp_as_mapping */
	object_hash,				/* tp_hash */
	0,					/* tp_call */
	object_str,				/* tp_str */
	PyObject_GenericGetAttr,		/* tp_getattro */
	PyObject_GenericSetAttr,		/* tp_setattro */
	0,					/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /* tp_flags */
	PyDoc_STR("The most base type"),	/* tp_doc */
	0,					/* tp_traverse */
	0,					/* tp_clear */
	0,					/* tp_richcompare */
	0,					/* tp_weaklistoffset */
	0,					/* tp_iter */
	0,					/* tp_iternext */
	object_methods,				/* tp_methods */
	0,					/* tp_members */
	object_getsets,				/* tp_getset */
	0,					/* tp_base */
	0,					/* tp_dict */
	0,					/* tp_descr_get */
	0,					/* tp_descr_set */
	0,					/* tp_dictoffset */
	object_init,				/* tp_init */
	PyType_GenericAlloc,			/* tp_alloc */
	object_new,				/* tp_new */
	PyObject_Del,           		/* tp_free */
};


/* Initialize the __dict__ in a type object */

static int
add_methods(PyTypeObject *type, PyMethodDef *meth)
{
	PyObject *dict = type->tp_dict;

	for (; meth->ml_name != NULL; meth++) {
		PyObject *descr;
		if (PyDict_GetItemString(dict, meth->ml_name) &&
			!(meth->ml_flags & METH_COEXIST))
				continue;
		if (meth->ml_flags & METH_CLASS) {
			if (meth->ml_flags & METH_STATIC) {
				PyErr_SetString(PyExc_ValueError,
				     "method cannot be both class and static");
				return -1;
			}
			descr = PyDescr_NewClassMethod(type, meth);
		}
		else if (meth->ml_flags & METH_STATIC) {
			PyObject *cfunc = PyCFunction_New(meth, NULL);
			if (cfunc == NULL)
				return -1;
			descr = PyStaticMethod_New(cfunc);
			Py_DECREF(cfunc);
		}
		else {
			descr = PyDescr_NewMethod(type, meth);
		}
		if (descr == NULL)
			return -1;
		if (PyDict_SetItemString(dict, meth->ml_name, descr) < 0)
			return -1;
		Py_DECREF(descr);
	}
	return 0;
}

static int
add_members(PyTypeObject *type, PyMemberDef *memb)
{
	PyObject *dict = type->tp_dict;

	for (; memb->name != NULL; memb++) {
		PyObject *descr;
		if (PyDict_GetItemString(dict, memb->name))
			continue;
		descr = PyDescr_NewMember(type, memb);
		if (descr == NULL)
			return -1;
		if (PyDict_SetItemString(dict, memb->name, descr) < 0)
			return -1;
		Py_DECREF(descr);
	}
	return 0;
}

static int
add_getset(PyTypeObject *type, PyGetSetDef *gsp)
{
	PyObject *dict = type->tp_dict;

	for (; gsp->name != NULL; gsp++) {
		PyObject *descr;
		if (PyDict_GetItemString(dict, gsp->name))
			continue;
		descr = PyDescr_NewGetSet(type, gsp);

		if (descr == NULL)
			return -1;
		if (PyDict_SetItemString(dict, gsp->name, descr) < 0)
			return -1;
		Py_DECREF(descr);
	}
	return 0;
}

static void
inherit_special(PyTypeObject *type, PyTypeObject *base)
{
	Py_ssize_t oldsize, newsize;

	/* Special flag magic */
	if (!type->tp_as_buffer && base->tp_as_buffer) {
		type->tp_flags &= ~Py_TPFLAGS_HAVE_GETCHARBUFFER;
		type->tp_flags |=
			base->tp_flags & Py_TPFLAGS_HAVE_GETCHARBUFFER;
	}
	if (!type->tp_as_sequence && base->tp_as_sequence) {
		type->tp_flags &= ~Py_TPFLAGS_HAVE_SEQUENCE_IN;
		type->tp_flags |= base->tp_flags & Py_TPFLAGS_HAVE_SEQUENCE_IN;
	}
	if ((type->tp_flags & Py_TPFLAGS_HAVE_INPLACEOPS) !=
	    (base->tp_flags & Py_TPFLAGS_HAVE_INPLACEOPS)) {
		if ((!type->tp_as_number && base->tp_as_number) ||
		    (!type->tp_as_sequence && base->tp_as_sequence)) {
			type->tp_flags &= ~Py_TPFLAGS_HAVE_INPLACEOPS;
			if (!type->tp_as_number && !type->tp_as_sequence) {
				type->tp_flags |= base->tp_flags &
					Py_TPFLAGS_HAVE_INPLACEOPS;
			}
		}
		/* Wow */
	}
	if (!type->tp_as_number && base->tp_as_number) {
		type->tp_flags &= ~Py_TPFLAGS_CHECKTYPES;
		type->tp_flags |= base->tp_flags & Py_TPFLAGS_CHECKTYPES;
	}

	/* Copying basicsize is connected to the GC flags */
	oldsize = base->tp_basicsize;
	newsize = type->tp_basicsize ? type->tp_basicsize : oldsize;
	if (!(type->tp_flags & Py_TPFLAGS_HAVE_GC) &&
	    (base->tp_flags & Py_TPFLAGS_HAVE_GC) &&
	    (type->tp_flags & Py_TPFLAGS_HAVE_RICHCOMPARE/*GC slots exist*/) &&
	    (!type->tp_traverse && !type->tp_clear)) {
		type->tp_flags |= Py_TPFLAGS_HAVE_GC;
		if (type->tp_traverse == NULL)
			type->tp_traverse = base->tp_traverse;
		if (type->tp_clear == NULL)
			type->tp_clear = base->tp_clear;
	}
	if (type->tp_flags & base->tp_flags & Py_TPFLAGS_HAVE_CLASS) {
		/* The condition below could use some explanation.
		   It appears that tp_new is not inherited for static types
		   whose base class is 'object'; this seems to be a precaution
		   so that old extension types don't suddenly become
		   callable (object.__new__ wouldn't insure the invariants
		   that the extension type's own factory function ensures).
		   Heap types, of course, are under our control, so they do
		   inherit tp_new; static extension types that specify some
		   other built-in type as the default are considered
		   new-style-aware so they also inherit object.__new__. */
		if (base != &PyBaseObject_Type ||
		    (type->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
			if (type->tp_new == NULL)
				type->tp_new = base->tp_new;
		}
	}
	type->tp_basicsize = newsize;

	/* Copy other non-function slots */

#undef COPYVAL
#define COPYVAL(SLOT) \
	if (type->SLOT == 0) type->SLOT = base->SLOT

	COPYVAL(tp_itemsize);
	if (type->tp_flags & base->tp_flags & Py_TPFLAGS_HAVE_WEAKREFS) {
		COPYVAL(tp_weaklistoffset);
	}
	if (type->tp_flags & base->tp_flags & Py_TPFLAGS_HAVE_CLASS) {
		COPYVAL(tp_dictoffset);
	}
}

static void
inherit_slots(PyTypeObject *type, PyTypeObject *base)
{
	PyTypeObject *basebase;

#undef SLOTDEFINED
#undef COPYSLOT
#undef COPYNUM
#undef COPYSEQ
#undef COPYMAP
#undef COPYBUF

#define SLOTDEFINED(SLOT) \
	(base->SLOT != 0 && \
	 (basebase == NULL || base->SLOT != basebase->SLOT))

#define COPYSLOT(SLOT) \
	if (!type->SLOT && SLOTDEFINED(SLOT)) type->SLOT = base->SLOT

#define COPYNUM(SLOT) COPYSLOT(tp_as_number->SLOT)
#define COPYSEQ(SLOT) COPYSLOT(tp_as_sequence->SLOT)
#define COPYMAP(SLOT) COPYSLOT(tp_as_mapping->SLOT)
#define COPYBUF(SLOT) COPYSLOT(tp_as_buffer->SLOT)

	/* This won't inherit indirect slots (from tp_as_number etc.)
	   if type doesn't provide the space. */

	if (type->tp_as_number != NULL && base->tp_as_number != NULL) {
		basebase = base->tp_base;
		if (basebase->tp_as_number == NULL)
			basebase = NULL;
		COPYNUM(nb_add);
		COPYNUM(nb_subtract);
		COPYNUM(nb_multiply);
		COPYNUM(nb_divide);
		COPYNUM(nb_remainder);
		COPYNUM(nb_divmod);
		COPYNUM(nb_power);
		COPYNUM(nb_negative);
		COPYNUM(nb_positive);
		COPYNUM(nb_absolute);
		COPYNUM(nb_nonzero);
		COPYNUM(nb_invert);
		COPYNUM(nb_lshift);
		COPYNUM(nb_rshift);
		COPYNUM(nb_and);
		COPYNUM(nb_xor);
		COPYNUM(nb_or);
		COPYNUM(nb_coerce);
		COPYNUM(nb_int);
		COPYNUM(nb_long);
		COPYNUM(nb_float);
		COPYNUM(nb_oct);
		COPYNUM(nb_hex);
		COPYNUM(nb_inplace_add);
		COPYNUM(nb_inplace_subtract);
		COPYNUM(nb_inplace_multiply);
		COPYNUM(nb_inplace_divide);
		COPYNUM(nb_inplace_remainder);
		COPYNUM(nb_inplace_power);
		COPYNUM(nb_inplace_lshift);
		COPYNUM(nb_inplace_rshift);
		COPYNUM(nb_inplace_and);
		COPYNUM(nb_inplace_xor);
		COPYNUM(nb_inplace_or);
		if (base->tp_flags & Py_TPFLAGS_CHECKTYPES) {
			COPYNUM(nb_true_divide);
			COPYNUM(nb_floor_divide);
			COPYNUM(nb_inplace_true_divide);
			COPYNUM(nb_inplace_floor_divide);
		}
		if (base->tp_flags & Py_TPFLAGS_HAVE_INDEX) {
			COPYNUM(nb_index);
		}
	}

	if (type->tp_as_sequence != NULL && base->tp_as_sequence != NULL) {
		basebase = base->tp_base;
		if (basebase->tp_as_sequence == NULL)
			basebase = NULL;
		COPYSEQ(sq_length);
		COPYSEQ(sq_concat);
		COPYSEQ(sq_repeat);
		COPYSEQ(sq_item);
		COPYSEQ(sq_slice);
		COPYSEQ(sq_ass_item);
		COPYSEQ(sq_ass_slice);
		COPYSEQ(sq_contains);
		COPYSEQ(sq_inplace_concat);
		COPYSEQ(sq_inplace_repeat);
	}

	if (type->tp_as_mapping != NULL && base->tp_as_mapping != NULL) {
		basebase = base->tp_base;
		if (basebase->tp_as_mapping == NULL)
			basebase = NULL;
		COPYMAP(mp_length);
		COPYMAP(mp_subscript);
		COPYMAP(mp_ass_subscript);
	}

	if (type->tp_as_buffer != NULL && base->tp_as_buffer != NULL) {
		basebase = base->tp_base;
		if (basebase->tp_as_buffer == NULL)
			basebase = NULL;
		COPYBUF(bf_getreadbuffer);
		COPYBUF(bf_getwritebuffer);
		COPYBUF(bf_getsegcount);
		COPYBUF(bf_getcharbuffer);
	}

	basebase = base->tp_base;

	COPYSLOT(tp_dealloc);
	COPYSLOT(tp_print);
	if (type->tp_getattr == NULL && type->tp_getattro == NULL) {
		type->tp_getattr = base->tp_getattr;
		type->tp_getattro = base->tp_getattro;
	}
	if (type->tp_setattr == NULL && type->tp_setattro == NULL) {
		type->tp_setattr = base->tp_setattr;
		type->tp_setattro = base->tp_setattro;
	}
	/* tp_compare see tp_richcompare */
	COPYSLOT(tp_repr);
	/* tp_hash see tp_richcompare */
	COPYSLOT(tp_call);
	COPYSLOT(tp_str);
	if (type->tp_flags & base->tp_flags & Py_TPFLAGS_HAVE_RICHCOMPARE) {
		if (type->tp_compare == NULL &&
		    type->tp_richcompare == NULL &&
		    type->tp_hash == NULL)
		{
			type->tp_compare = base->tp_compare;
			type->tp_richcompare = base->tp_richcompare;
			type->tp_hash = base->tp_hash;
		}
	}
	else {
		COPYSLOT(tp_compare);
	}
	if (type->tp_flags & base->tp_flags & Py_TPFLAGS_HAVE_ITER) {
		COPYSLOT(tp_iter);
		COPYSLOT(tp_iternext);
	}
	if (type->tp_flags & base->tp_flags & Py_TPFLAGS_HAVE_CLASS) {
		COPYSLOT(tp_descr_get);
		COPYSLOT(tp_descr_set);
		COPYSLOT(tp_dictoffset);
		COPYSLOT(tp_init);
		COPYSLOT(tp_alloc);
		COPYSLOT(tp_is_gc);
		if ((type->tp_flags & Py_TPFLAGS_HAVE_GC) ==
		    (base->tp_flags & Py_TPFLAGS_HAVE_GC)) {
			/* They agree about gc. */
			COPYSLOT(tp_free);
		}
		else if ((type->tp_flags & Py_TPFLAGS_HAVE_GC) &&
			 type->tp_free == NULL &&
			 base->tp_free == _PyObject_Del) {
			/* A bit of magic to plug in the correct default
			 * tp_free function when a derived class adds gc,
			 * didn't define tp_free, and the base uses the
			 * default non-gc tp_free.
			 */
			type->tp_free = PyObject_GC_Del;
		}
		/* else they didn't agree about gc, and there isn't something
		 * obvious to be done -- the type is on its own.
		 */
	}
}

static int add_operators(PyTypeObject *);

int
PyType_Ready(PyTypeObject *type)
{
	PyObject *dict, *bases;
	PyTypeObject *base;
	Py_ssize_t i, n;

	if (type->tp_flags & Py_TPFLAGS_READY) {
		assert(type->tp_dict != NULL);
		return 0;
	}
	assert((type->tp_flags & Py_TPFLAGS_READYING) == 0);

	type->tp_flags |= Py_TPFLAGS_READYING;

#ifdef Py_TRACE_REFS
	/* PyType_Ready is the closest thing we have to a choke point
	 * for type objects, so is the best place I can think of to try
	 * to get type objects into the doubly-linked list of all objects.
	 * Still, not all type objects go thru PyType_Ready.
	 */
	_Py_AddToAllObjects((PyObject *)type, 0);
#endif

	/* Initialize tp_base (defaults to BaseObject unless that's us) */
	base = type->tp_base;
	if (base == NULL && type != &PyBaseObject_Type) {
		base = type->tp_base = &PyBaseObject_Type;
		Py_INCREF(base);
	}

        /* Now the only way base can still be NULL is if type is
         * &PyBaseObject_Type.
         */

	/* Initialize the base class */
	if (base && base->tp_dict == NULL) {
		if (PyType_Ready(base) < 0)
			goto error;
	}

	/* Initialize ob_type if NULL.  This means extensions that want to be
	   compilable separately on Windows can call PyType_Ready() instead of
	   initializing the ob_type field of their type objects. */
        /* The test for base != NULL is really unnecessary, since base is only
           NULL when type is &PyBaseObject_Type, and we know its ob_type is
           not NULL (it's initialized to &PyType_Type).  But coverity doesn't
           know that. */
	if (type->ob_type == NULL && base != NULL)
		type->ob_type = base->ob_type;

	/* Initialize tp_bases */
	bases = type->tp_bases;
	if (bases == NULL) {
		if (base == NULL)
			bases = PyTuple_New(0);
		else
			bases = PyTuple_Pack(1, base);
		if (bases == NULL)
			goto error;
		type->tp_bases = bases;
	}

	/* Initialize tp_dict */
	dict = type->tp_dict;
	if (dict == NULL) {
		dict = PyDict_New();
		if (dict == NULL)
			goto error;
		type->tp_dict = dict;
	}

	/* Add type-specific descriptors to tp_dict */
	if (add_operators(type) < 0)
		goto error;
	if (type->tp_methods != NULL) {
		if (add_methods(type, type->tp_methods) < 0)
			goto error;
	}
	if (type->tp_members != NULL) {
		if (add_members(type, type->tp_members) < 0)
			goto error;
	}
	if (type->tp_getset != NULL) {
		if (add_getset(type, type->tp_getset) < 0)
			goto error;
	}

	/* Calculate method resolution order */
	if (mro_internal(type) < 0) {
		goto error;
	}

	/* Inherit special flags from dominant base */
	if (type->tp_base != NULL)
		inherit_special(type, type->tp_base);

	/* Initialize tp_dict properly */
	bases = type->tp_mro;
	assert(bases != NULL);
	assert(PyTuple_Check(bases));
	n = PyTuple_GET_SIZE(bases);
	for (i = 1; i < n; i++) {
		PyObject *b = PyTuple_GET_ITEM(bases, i);
		if (PyType_Check(b))
			inherit_slots(type, (PyTypeObject *)b);
	}

	/* Sanity check for tp_free. */
	if (PyType_IS_GC(type) && (type->tp_flags & Py_TPFLAGS_BASETYPE) &&
	    (type->tp_free == NULL || type->tp_free == PyObject_Del)) {
	    	/* This base class needs to call tp_free, but doesn't have
	    	 * one, or its tp_free is for non-gc'ed objects.
	    	 */
		PyErr_Format(PyExc_TypeError, "type '%.100s' participates in "
			     "gc and is a base type but has inappropriate "
			     "tp_free slot",
			     type->tp_name);
		goto error;
	}

	/* if the type dictionary doesn't contain a __doc__, set it from
	   the tp_doc slot.
	 */
	if (PyDict_GetItemString(type->tp_dict, "__doc__") == NULL) {
		if (type->tp_doc != NULL) {
			PyObject *doc = PyString_FromString(type->tp_doc);
			if (doc == NULL)
				goto error;
			PyDict_SetItemString(type->tp_dict, "__doc__", doc);
			Py_DECREF(doc);
		} else {
			PyDict_SetItemString(type->tp_dict,
					     "__doc__", Py_None);
		}
	}

	/* Some more special stuff */
	base = type->tp_base;
	if (base != NULL) {
		if (type->tp_as_number == NULL)
			type->tp_as_number = base->tp_as_number;
		if (type->tp_as_sequence == NULL)
			type->tp_as_sequence = base->tp_as_sequence;
		if (type->tp_as_mapping == NULL)
			type->tp_as_mapping = base->tp_as_mapping;
		if (type->tp_as_buffer == NULL)
			type->tp_as_buffer = base->tp_as_buffer;
	}

	/* Link into each base class's list of subclasses */
	bases = type->tp_bases;
	n = PyTuple_GET_SIZE(bases);
	for (i = 0; i < n; i++) {
		PyObject *b = PyTuple_GET_ITEM(bases, i);
		if (PyType_Check(b) &&
		    add_subclass((PyTypeObject *)b, type) < 0)
			goto error;
	}

	/* All done -- set the ready flag */
	assert(type->tp_dict != NULL);
	type->tp_flags =
		(type->tp_flags & ~Py_TPFLAGS_READYING) | Py_TPFLAGS_READY;
	return 0;

  error:
	type->tp_flags &= ~Py_TPFLAGS_READYING;
	return -1;
}

static int
add_subclass(PyTypeObject *base, PyTypeObject *type)
{
	Py_ssize_t i;
	int result;
	PyObject *list, *ref, *newobj;

	list = base->tp_subclasses;
	if (list == NULL) {
		base->tp_subclasses = list = PyList_New(0);
		if (list == NULL)
			return -1;
	}
	assert(PyList_Check(list));
	newobj = PyWeakref_NewRef((PyObject *)type, NULL);
	i = PyList_GET_SIZE(list);
	while (--i >= 0) {
		ref = PyList_GET_ITEM(list, i);
		assert(PyWeakref_CheckRef(ref));
		if (PyWeakref_GET_OBJECT(ref) == Py_None)
			return PyList_SetItem(list, i, newobj);
	}
	result = PyList_Append(list, newobj);
	Py_DECREF(newobj);
	return result;
}

static void
remove_subclass(PyTypeObject *base, PyTypeObject *type)
{
	Py_ssize_t i;
	PyObject *list, *ref;

	list = base->tp_subclasses;
	if (list == NULL) {
		return;
	}
	assert(PyList_Check(list));
	i = PyList_GET_SIZE(list);
	while (--i >= 0) {
		ref = PyList_GET_ITEM(list, i);
		assert(PyWeakref_CheckRef(ref));
		if (PyWeakref_GET_OBJECT(ref) == (PyObject*)type) {
			/* this can't fail, right? */
			PySequence_DelItem(list, i);
			return;
		}
	}
}

static int
check_num_args(PyObject *ob, int n)
{
	if (!PyTuple_CheckExact(ob)) {
		PyErr_SetString(PyExc_SystemError,
		    "PyArg_UnpackTuple() argument list is not a tuple");
		return 0;
	}
	if (n == PyTuple_GET_SIZE(ob))
		return 1;
	PyErr_Format(
	    PyExc_TypeError, 
	    "expected %d arguments, got %zd", n, PyTuple_GET_SIZE(ob));
	return 0;
}

/* Generic wrappers for overloadable 'operators' such as __getitem__ */

/* There's a wrapper *function* for each distinct function typedef used
   for type object slots (e.g. binaryfunc, ternaryfunc, etc.).  There's a
   wrapper *table* for each distinct operation (e.g. __len__, __add__).
   Most tables have only one entry; the tables for binary operators have two
   entries, one regular and one with reversed arguments. */

static PyObject *
wrap_lenfunc(PyObject *self, PyObject *args, void *wrapped)
{
	lenfunc func = (lenfunc)wrapped;
	Py_ssize_t res;

	if (!check_num_args(args, 0))
		return NULL;
	res = (*func)(self);
	if (res == -1 && PyErr_Occurred())
		return NULL;
	return PyInt_FromLong((long)res);
}

static PyObject *
wrap_inquirypred(PyObject *self, PyObject *args, void *wrapped)
{
	inquiry func = (inquiry)wrapped;
	int res;

	if (!check_num_args(args, 0))
		return NULL;
	res = (*func)(self);
	if (res == -1 && PyErr_Occurred())
		return NULL;
	return PyBool_FromLong((long)res);
}

static PyObject *
wrap_binaryfunc(PyObject *self, PyObject *args, void *wrapped)
{
	binaryfunc func = (binaryfunc)wrapped;
	PyObject *other;

	if (!check_num_args(args, 1))
		return NULL;
	other = PyTuple_GET_ITEM(args, 0);
	return (*func)(self, other);
}

static PyObject *
wrap_binaryfunc_l(PyObject *self, PyObject *args, void *wrapped)
{
	binaryfunc func = (binaryfunc)wrapped;
	PyObject *other;

	if (!check_num_args(args, 1))
		return NULL;
	other = PyTuple_GET_ITEM(args, 0);
	if (!(self->ob_type->tp_flags & Py_TPFLAGS_CHECKTYPES) &&
	    !PyType_IsSubtype(other->ob_type, self->ob_type)) {
		Py_INCREF(Py_NotImplemented);
		return Py_NotImplemented;
	}
	return (*func)(self, other);
}

static PyObject *
wrap_binaryfunc_r(PyObject *self, PyObject *args, void *wrapped)
{
	binaryfunc func = (binaryfunc)wrapped;
	PyObject *other;

	if (!check_num_args(args, 1))
		return NULL;
	other = PyTuple_GET_ITEM(args, 0);
	if (!(self->ob_type->tp_flags & Py_TPFLAGS_CHECKTYPES) &&
	    !PyType_IsSubtype(other->ob_type, self->ob_type)) {
		Py_INCREF(Py_NotImplemented);
		return Py_NotImplemented;
	}
	return (*func)(other, self);
}

static PyObject *
wrap_coercefunc(PyObject *self, PyObject *args, void *wrapped)
{
	coercion func = (coercion)wrapped;
	PyObject *other, *res;
	int ok;

	if (!check_num_args(args, 1))
		return NULL;
	other = PyTuple_GET_ITEM(args, 0);
	ok = func(&self, &other);
	if (ok < 0)
		return NULL;
	if (ok > 0) {
		Py_INCREF(Py_NotImplemented);
		return Py_NotImplemented;
	}
	res = PyTuple_New(2);
	if (res == NULL) {
		Py_DECREF(self);
		Py_DECREF(other);
		return NULL;
	}
	PyTuple_SET_ITEM(res, 0, self);
	PyTuple_SET_ITEM(res, 1, other);
	return res;
}

static PyObject *
wrap_ternaryfunc(PyObject *self, PyObject *args, void *wrapped)
{
	ternaryfunc func = (ternaryfunc)wrapped;
	PyObject *other;
	PyObject *third = Py_None;

	/* Note: This wrapper only works for __pow__() */

	if (!PyArg_UnpackTuple(args, "", 1, 2, &other, &third))
		return NULL;
	return (*func)(self, other, third);
}

static PyObject *
wrap_ternaryfunc_r(PyObject *self, PyObject *args, void *wrapped)
{
	ternaryfunc func = (ternaryfunc)wrapped;
	PyObject *other;
	PyObject *third = Py_None;

	/* Note: This wrapper only works for __pow__() */

	if (!PyArg_UnpackTuple(args, "", 1, 2, &other, &third))
		return NULL;
	return (*func)(other, self, third);
}

static PyObject *
wrap_unaryfunc(PyObject *self, PyObject *args, void *wrapped)
{
	unaryfunc func = (unaryfunc)wrapped;

	if (!check_num_args(args, 0))
		return NULL;
	return (*func)(self);
}

static PyObject *
wrap_indexargfunc(PyObject *self, PyObject *args, void *wrapped)
{
	ssizeargfunc func = (ssizeargfunc)wrapped;
	PyObject* o;
	Py_ssize_t i;

	if (!PyArg_UnpackTuple(args, "", 1, 1, &o))
		return NULL;
	i = PyNumber_AsSsize_t(o, PyExc_OverflowError);
	if (i == -1 && PyErr_Occurred())
		return NULL;
	return (*func)(self, i);
}

static Py_ssize_t
getindex(PyObject *self, PyObject *arg)
{
	Py_ssize_t i;

	i = PyNumber_AsSsize_t(arg, PyExc_OverflowError);
	if (i == -1 && PyErr_Occurred())
		return -1;
	if (i < 0) {
		PySequenceMethods *sq = self->ob_type->tp_as_sequence;
		if (sq && sq->sq_length) {
			Py_ssize_t n = (*sq->sq_length)(self);
			if (n < 0)
				return -1;
			i += n;
		}
	}
	return i;
}

static PyObject *
wrap_sq_item(PyObject *self, PyObject *args, void *wrapped)
{
	ssizeargfunc func = (ssizeargfunc)wrapped;
	PyObject *arg;
	Py_ssize_t i;

	if (PyTuple_GET_SIZE(args) == 1) {
		arg = PyTuple_GET_ITEM(args, 0);
		i = getindex(self, arg);
		if (i == -1 && PyErr_Occurred())
			return NULL;
		return (*func)(self, i);
	}
	check_num_args(args, 1);
	assert(PyErr_Occurred());
	return NULL;
}

static PyObject *
wrap_ssizessizeargfunc(PyObject *self, PyObject *args, void *wrapped)
{
	ssizessizeargfunc func = (ssizessizeargfunc)wrapped;
	Py_ssize_t i, j;

	if (!PyArg_ParseTuple(args, "nn", &i, &j))
		return NULL;
	return (*func)(self, i, j);
}

static PyObject *
wrap_sq_setitem(PyObject *self, PyObject *args, void *wrapped)
{
	ssizeobjargproc func = (ssizeobjargproc)wrapped;
	Py_ssize_t i;
	int res;
	PyObject *arg, *value;

	if (!PyArg_UnpackTuple(args, "", 2, 2, &arg, &value))
		return NULL;
	i = getindex(self, arg);
	if (i == -1 && PyErr_Occurred())
		return NULL;
	res = (*func)(self, i, value);
	if (res == -1 && PyErr_Occurred())
		return NULL;
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *
wrap_sq_delitem(PyObject *self, PyObject *args, void *wrapped)
{
	ssizeobjargproc func = (ssizeobjargproc)wrapped;
	Py_ssize_t i;
	int res;
	PyObject *arg;

	if (!check_num_args(args, 1))
		return NULL;
	arg = PyTuple_GET_ITEM(args, 0);
	i = getindex(self, arg);
	if (i == -1 && PyErr_Occurred())
		return NULL;
	res = (*func)(self, i, NULL);
	if (res == -1 && PyErr_Occurred())
		return NULL;
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *
wrap_ssizessizeobjargproc(PyObject *self, PyObject *args, void *wrapped)
{
	ssizessizeobjargproc func = (ssizessizeobjargproc)wrapped;
	Py_ssize_t i, j;
	int res;
	PyObject *value;

	if (!PyArg_ParseTuple(args, "nnO", &i, &j, &value))
		return NULL;
	res = (*func)(self, i, j, value);
	if (res == -1 && PyErr_Occurred())
		return NULL;
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *
wrap_delslice(PyObject *self, PyObject *args, void *wrapped)
{
	ssizessizeobjargproc func = (ssizessizeobjargproc)wrapped;
	Py_ssize_t i, j;
	int res;

	if (!PyArg_ParseTuple(args, "nn", &i, &j))
		return NULL;
	res = (*func)(self, i, j, NULL);
	if (res == -1 && PyErr_Occurred())
		return NULL;
	Py_INCREF(Py_None);
	return Py_None;
}

/* XXX objobjproc is a misnomer; should be objargpred */
static PyObject *
wrap_objobjproc(PyObject *self, PyObject *args, void *wrapped)
{
	objobjproc func = (objobjproc)wrapped;
	int res;
	PyObject *value;

	if (!check_num_args(args, 1))
		return NULL;
	value = PyTuple_GET_ITEM(args, 0);
	res = (*func)(self, value);
	if (res == -1 && PyErr_Occurred())
		return NULL;
	else
		return PyBool_FromLong(res);
}

static PyObject *
wrap_objobjargproc(PyObject *self, PyObject *args, void *wrapped)
{
	objobjargproc func = (objobjargproc)wrapped;
	int res;
	PyObject *key, *value;

	if (!PyArg_UnpackTuple(args, "", 2, 2, &key, &value))
		return NULL;
	res = (*func)(self, key, value);
	if (res == -1 && PyErr_Occurred())
		return NULL;
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *
wrap_delitem(PyObject *self, PyObject *args, void *wrapped)
{
	objobjargproc func = (objobjargproc)wrapped;
	int res;
	PyObject *key;

	if (!check_num_args(args, 1))
		return NULL;
	key = PyTuple_GET_ITEM(args, 0);
	res = (*func)(self, key, NULL);
	if (res == -1 && PyErr_Occurred())
		return NULL;
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *
wrap_cmpfunc(PyObject *self, PyObject *args, void *wrapped)
{
	cmpfunc func = (cmpfunc)wrapped;
	int res;
	PyObject *other;

	if (!check_num_args(args, 1))
		return NULL;
	other = PyTuple_GET_ITEM(args, 0);
	if (other->ob_type->tp_compare != func &&
	    !PyType_IsSubtype(other->ob_type, self->ob_type)) {
		PyErr_Format(
			PyExc_TypeError,
			"%s.__cmp__(x,y) requires y to be a '%s', not a '%s'",
			self->ob_type->tp_name,
			self->ob_type->tp_name,
			other->ob_type->tp_name);
		return NULL;
	}
	res = (*func)(self, other);
	if (PyErr_Occurred())
		return NULL;
	return PyInt_FromLong((long)res);
}

/* Helper to check for object.__setattr__ or __delattr__ applied to a type.
   This is called the Carlo Verre hack after its discoverer. */
static int
hackcheck(PyObject *self, setattrofunc func, char *what)
{
	PyTypeObject *type = self->ob_type;
	while (type && type->tp_flags & Py_TPFLAGS_HEAPTYPE)
		type = type->tp_base;
        /* If type is NULL now, this is a really weird type.
           In the same of backwards compatibility (?), just shut up. */
	if (type && type->tp_setattro != func) {
		PyErr_Format(PyExc_TypeError,
			     "can't apply this %s to %s object",
			     what,
			     type->tp_name);
		return 0;
	}
	return 1;
}

static PyObject *
wrap_setattr(PyObject *self, PyObject *args, void *wrapped)
{
	setattrofunc func = (setattrofunc)wrapped;
	int res;
	PyObject *name, *value;

	if (!PyArg_UnpackTuple(args, "", 2, 2, &name, &value))
		return NULL;
	if (!hackcheck(self, func, "__setattr__"))
		return NULL;
	res = (*func)(self, name, value);
	if (res < 0)
		return NULL;
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *
wrap_delattr(PyObject *self, PyObject *args, void *wrapped)
{
	setattrofunc func = (setattrofunc)wrapped;
	int res;
	PyObject *name;

	if (!check_num_args(args, 1))
		return NULL;
	name = PyTuple_GET_ITEM(args, 0);
	if (!hackcheck(self, func, "__delattr__"))
		return NULL;
	res = (*func)(self, name, NULL);
	if (res < 0)
		return NULL;
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *
wrap_hashfunc(PyObject *self, PyObject *args, void *wrapped)
{
	hashfunc func = (hashfunc)wrapped;
	long res;

	if (!check_num_args(args, 0))
		return NULL;
	res = (*func)(self);
	if (res == -1 && PyErr_Occurred())
		return NULL;
	return PyInt_FromLong(res);
}

static PyObject *
wrap_call(PyObject *self, PyObject *args, void *wrapped, PyObject *kwds)
{
	ternaryfunc func = (ternaryfunc)wrapped;

	return (*func)(self, args, kwds);
}

static PyObject *
wrap_richcmpfunc(PyObject *self, PyObject *args, void *wrapped, int op)
{
	richcmpfunc func = (richcmpfunc)wrapped;
	PyObject *other;

	if (!check_num_args(args, 1))
		return NULL;
	other = PyTuple_GET_ITEM(args, 0);
	return (*func)(self, other, op);
}

#undef RICHCMP_WRAPPER
#define RICHCMP_WRAPPER(NAME, OP) \
static PyObject * \
richcmp_##NAME(PyObject *self, PyObject *args, void *wrapped) \
{ \
	return wrap_richcmpfunc(self, args, wrapped, OP); \
}

RICHCMP_WRAPPER(lt, Py_LT)
RICHCMP_WRAPPER(le, Py_LE)
RICHCMP_WRAPPER(eq, Py_EQ)
RICHCMP_WRAPPER(ne, Py_NE)
RICHCMP_WRAPPER(gt, Py_GT)
RICHCMP_WRAPPER(ge, Py_GE)

static PyObject *
wrap_next(PyObject *self, PyObject *args, void *wrapped)
{
	unaryfunc func = (unaryfunc)wrapped;
	PyObject *res;

	if (!check_num_args(args, 0))
		return NULL;
	res = (*func)(self);
	if (res == NULL && !PyErr_Occurred())
		PyErr_SetNone(PyExc_StopIteration);
	return res;
}

static PyObject *
wrap_descr_get(PyObject *self, PyObject *args, void *wrapped)
{
	descrgetfunc func = (descrgetfunc)wrapped;
	PyObject *obj;
	PyObject *type = NULL;

	if (!PyArg_UnpackTuple(args, "", 1, 2, &obj, &type))
		return NULL;
	if (obj == Py_None)
		obj = NULL;
	if (type == Py_None)
		type = NULL;
	if (type == NULL &&obj == NULL) {
		PyErr_SetString(PyExc_TypeError,
				"__get__(None, None) is invalid");
		return NULL;
	}
	return (*func)(self, obj, type);
}

static PyObject *
wrap_descr_set(PyObject *self, PyObject *args, void *wrapped)
{
	descrsetfunc func = (descrsetfunc)wrapped;
	PyObject *obj, *value;
	int ret;

	if (!PyArg_UnpackTuple(args, "", 2, 2, &obj, &value))
		return NULL;
	ret = (*func)(self, obj, value);
	if (ret < 0)
		return NULL;
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *
wrap_descr_delete(PyObject *self, PyObject *args, void *wrapped)
{
	descrsetfunc func = (descrsetfunc)wrapped;
	PyObject *obj;
	int ret;

	if (!check_num_args(args, 1))
		return NULL;
	obj = PyTuple_GET_ITEM(args, 0);
	ret = (*func)(self, obj, NULL);
	if (ret < 0)
		return NULL;
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *
wrap_init(PyObject *self, PyObject *args, void *wrapped, PyObject *kwds)
{
	initproc func = (initproc)wrapped;

	if (func(self, args, kwds) < 0)
		return NULL;
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *
tp_new_wrapper(PyObject *self, PyObject *args, PyObject *kwds)
{
	PyTypeObject *type, *subtype, *staticbase;
	PyObject *arg0, *res;

	if (self == NULL || !PyType_Check(self))
		Py_FatalError("__new__() called with non-type 'self'");
	type = (PyTypeObject *)self;
	if (!PyTuple_Check(args) || PyTuple_GET_SIZE(args) < 1) {
		PyErr_Format(PyExc_TypeError,
			     "%s.__new__(): not enough arguments",
			     type->tp_name);
		return NULL;
	}
	arg0 = PyTuple_GET_ITEM(args, 0);
	if (!PyType_Check(arg0)) {
		PyErr_Format(PyExc_TypeError,
			     "%s.__new__(X): X is not a type object (%s)",
			     type->tp_name,
			     arg0->ob_type->tp_name);
		return NULL;
	}
	subtype = (PyTypeObject *)arg0;
	if (!PyType_IsSubtype(subtype, type)) {
		PyErr_Format(PyExc_TypeError,
			     "%s.__new__(%s): %s is not a subtype of %s",
			     type->tp_name,
			     subtype->tp_name,
			     subtype->tp_name,
			     type->tp_name);
		return NULL;
	}

	/* Check that the use doesn't do something silly and unsafe like
	   object.__new__(dict).  To do this, we check that the
	   most derived base that's not a heap type is this type. */
	staticbase = subtype;
	while (staticbase && (staticbase->tp_flags & Py_TPFLAGS_HEAPTYPE))
		staticbase = staticbase->tp_base;
        /* If staticbase is NULL now, it is a really weird type.
           In the same of backwards compatibility (?), just shut up. */
	if (staticbase && staticbase->tp_new != type->tp_new) {
		PyErr_Format(PyExc_TypeError,
			     "%s.__new__(%s) is not safe, use %s.__new__()",
			     type->tp_name,
			     subtype->tp_name,
			     staticbase == NULL ? "?" : staticbase->tp_name);
		return NULL;
	}

	args = PyTuple_GetSlice(args, 1, PyTuple_GET_SIZE(args));
	if (args == NULL)
		return NULL;
	res = type->tp_new(subtype, args, kwds);
	Py_DECREF(args);
	return res;
}

static struct PyMethodDef tp_new_methoddef[] = {
	{"__new__", (PyCFunction)tp_new_wrapper, METH_KEYWORDS,
	 PyDoc_STR("T.__new__(S, ...) -> "
	 	   "a new object with type S, a subtype of T")},
	{0}
};

static int
add_tp_new_wrapper(PyTypeObject *type)
{
	PyObject *func;

	if (PyDict_GetItemString(type->tp_dict, "__new__") != NULL)
		return 0;
	func = PyCFunction_New(tp_new_methoddef, (PyObject *)type);
	if (func == NULL)
		return -1;
	if (PyDict_SetItemString(type->tp_dict, "__new__", func)) {
		Py_DECREF(func);
		return -1;
	}
	Py_DECREF(func);
	return 0;
}

/* Slot wrappers that call the corresponding __foo__ slot.  See comments
   below at override_slots() for more explanation. */

#define SLOT0(FUNCNAME, OPSTR) \
static PyObject * \
FUNCNAME(PyObject *self) \
{ \
	static PyObject *cache_str; \
	return call_method(self, OPSTR, &cache_str, "()"); \
}

#define SLOT1(FUNCNAME, OPSTR, ARG1TYPE, ARGCODES) \
static PyObject * \
FUNCNAME(PyObject *self, ARG1TYPE arg1) \
{ \
	static PyObject *cache_str; \
	return call_method(self, OPSTR, &cache_str, "(" ARGCODES ")", arg1); \
}

/* Boolean helper for SLOT1BINFULL().
   right.__class__ is a nontrivial subclass of left.__class__. */
static int
method_is_overloaded(PyObject *left, PyObject *right, char *name)
{
	PyObject *a, *b;
	int ok;

	b = PyObject_GetAttrString((PyObject *)(right->ob_type), name);
	if (b == NULL) {
		PyErr_Clear();
		/* If right doesn't have it, it's not overloaded */
		return 0;
	}

	a = PyObject_GetAttrString((PyObject *)(left->ob_type), name);
	if (a == NULL) {
		PyErr_Clear();
		Py_DECREF(b);
		/* If right has it but left doesn't, it's overloaded */
		return 1;
	}

	ok = PyObject_RichCompareBool(a, b, Py_NE);
	Py_DECREF(a);
	Py_DECREF(b);
	if (ok < 0) {
		PyErr_Clear();
		return 0;
	}

	return ok;
}


#define SLOT1BINFULL(FUNCNAME, TESTFUNC, SLOTNAME, OPSTR, ROPSTR) \
static PyObject * \
FUNCNAME(PyObject *self, PyObject *other) \
{ \
	static PyObject *cache_str, *rcache_str; \
	int do_other = self->ob_type != other->ob_type && \
	    other->ob_type->tp_as_number != NULL && \
	    other->ob_type->tp_as_number->SLOTNAME == TESTFUNC; \
	if (self->ob_type->tp_as_number != NULL && \
	    self->ob_type->tp_as_number->SLOTNAME == TESTFUNC) { \
		PyObject *r; \
		if (do_other && \
		    PyType_IsSubtype(other->ob_type, self->ob_type) && \
		    method_is_overloaded(self, other, ROPSTR)) { \
			r = call_maybe( \
				other, ROPSTR, &rcache_str, "(O)", self); \
			if (r != Py_NotImplemented) \
				return r; \
			Py_DECREF(r); \
			do_other = 0; \
		} \
		r = call_maybe( \
			self, OPSTR, &cache_str, "(O)", other); \
		if (r != Py_NotImplemented || \
		    other->ob_type == self->ob_type) \
			return r; \
		Py_DECREF(r); \
	} \
	if (do_other) { \
		return call_maybe( \
			other, ROPSTR, &rcache_str, "(O)", self); \
	} \
	Py_INCREF(Py_NotImplemented); \
	return Py_NotImplemented; \
}

#define SLOT1BIN(FUNCNAME, SLOTNAME, OPSTR, ROPSTR) \
	SLOT1BINFULL(FUNCNAME, FUNCNAME, SLOTNAME, OPSTR, ROPSTR)

#define SLOT2(FUNCNAME, OPSTR, ARG1TYPE, ARG2TYPE, ARGCODES) \
static PyObject * \
FUNCNAME(PyObject *self, ARG1TYPE arg1, ARG2TYPE arg2) \
{ \
	static PyObject *cache_str; \
	return call_method(self, OPSTR, &cache_str, \
			   "(" ARGCODES ")", arg1, arg2); \
}

static Py_ssize_t
slot_sq_length(PyObject *self)
{
	static PyObject *len_str;
	PyObject *res = call_method(self, "__len__", &len_str, "()");
	Py_ssize_t len;

	if (res == NULL)
		return -1;
	len = PyInt_AsSsize_t(res);
	Py_DECREF(res);
	if (len < 0) {
		if (!PyErr_Occurred())
			PyErr_SetString(PyExc_ValueError,
					"__len__() should return >= 0");
		return -1;
	}
	return len;
}

/* Super-optimized version of slot_sq_item.
   Other slots could do the same... */
static PyObject *
slot_sq_item(PyObject *self, Py_ssize_t i)
{
	static PyObject *getitem_str;
	PyObject *func, *args = NULL, *ival = NULL, *retval = NULL;
	descrgetfunc f;

	if (getitem_str == NULL) {
		getitem_str = PyString_InternFromString("__getitem__");
		if (getitem_str == NULL)
			return NULL;
	}
	func = _PyType_Lookup(self->ob_type, getitem_str);
	if (func != NULL) {
		if ((f = func->ob_type->tp_descr_get) == NULL)
			Py_INCREF(func);
		else {
			func = f(func, self, (PyObject *)(self->ob_type));
			if (func == NULL) {
				return NULL;
			}
		}
		ival = PyInt_FromSsize_t(i);
		if (ival != NULL) {
			args = PyTuple_New(1);
			if (args != NULL) {
				PyTuple_SET_ITEM(args, 0, ival);
				retval = PyObject_Call(func, args, NULL);
				Py_XDECREF(args);
				Py_XDECREF(func);
				return retval;
			}
		}
	}
	else {
		PyErr_SetObject(PyExc_AttributeError, getitem_str);
	}
	Py_XDECREF(args);
	Py_XDECREF(ival);
	Py_XDECREF(func);
	return NULL;
}

SLOT2(slot_sq_slice, "__getslice__", Py_ssize_t, Py_ssize_t, "nn")

static int
slot_sq_ass_item(PyObject *self, Py_ssize_t index, PyObject *value)
{
	PyObject *res;
	static PyObject *delitem_str, *setitem_str;

	if (value == NULL)
		res = call_method(self, "__delitem__", &delitem_str,
				  "(n)", index);
	else
		res = call_method(self, "__setitem__", &setitem_str,
				  "(nO)", index, value);
	if (res == NULL)
		return -1;
	Py_DECREF(res);
	return 0;
}

static int
slot_sq_ass_slice(PyObject *self, Py_ssize_t i, Py_ssize_t j, PyObject *value)
{
	PyObject *res;
	static PyObject *delslice_str, *setslice_str;

	if (value == NULL)
		res = call_method(self, "__delslice__", &delslice_str,
				  "(nn)", i, j);
	else
		res = call_method(self, "__setslice__", &setslice_str,
				  "(nnO)", i, j, value);
	if (res == NULL)
		return -1;
	Py_DECREF(res);
	return 0;
}

static int
slot_sq_contains(PyObject *self, PyObject *value)
{
	PyObject *func, *res, *args;
	int result = -1;

	static PyObject *contains_str;

	func = lookup_maybe(self, "__contains__", &contains_str);
	if (func != NULL) {
		args = PyTuple_Pack(1, value);
		if (args == NULL)
			res = NULL;
		else {
			res = PyObject_Call(func, args, NULL);
			Py_DECREF(args);
		}
		Py_DECREF(func);
		if (res != NULL) {
			result = PyObject_IsTrue(res);
			Py_DECREF(res);
		}
	}
	else if (! PyErr_Occurred()) {
		/* Possible results: -1 and 1 */
		result = (int)_PySequence_IterSearch(self, value,
						 PY_ITERSEARCH_CONTAINS);
	}
	return result;
}

#define slot_mp_length slot_sq_length

SLOT1(slot_mp_subscript, "__getitem__", PyObject *, "O")

static int
slot_mp_ass_subscript(PyObject *self, PyObject *key, PyObject *value)
{
	PyObject *res;
	static PyObject *delitem_str, *setitem_str;

	if (value == NULL)
		res = call_method(self, "__delitem__", &delitem_str,
				  "(O)", key);
	else
		res = call_method(self, "__setitem__", &setitem_str,
				 "(OO)", key, value);
	if (res == NULL)
		return -1;
	Py_DECREF(res);
	return 0;
}

SLOT1BIN(slot_nb_add, nb_add, "__add__", "__radd__")
SLOT1BIN(slot_nb_subtract, nb_subtract, "__sub__", "__rsub__")
SLOT1BIN(slot_nb_multiply, nb_multiply, "__mul__", "__rmul__")
SLOT1BIN(slot_nb_divide, nb_divide, "__div__", "__rdiv__")
SLOT1BIN(slot_nb_remainder, nb_remainder, "__mod__", "__rmod__")
SLOT1BIN(slot_nb_divmod, nb_divmod, "__divmod__", "__rdivmod__")

static PyObject *slot_nb_power(PyObject *, PyObject *, PyObject *);

SLOT1BINFULL(slot_nb_power_binary, slot_nb_power,
	     nb_power, "__pow__", "__rpow__")

static PyObject *
slot_nb_power(PyObject *self, PyObject *other, PyObject *modulus)
{
	static PyObject *pow_str;

	if (modulus == Py_None)
		return slot_nb_power_binary(self, other);
	/* Three-arg power doesn't use __rpow__.  But ternary_op
	   can call this when the second argument's type uses
	   slot_nb_power, so check before calling self.__pow__. */
	if (self->ob_type->tp_as_number != NULL &&
	    self->ob_type->tp_as_number->nb_power == slot_nb_power) {
		return call_method(self, "__pow__", &pow_str,
				   "(OO)", other, modulus);
	}
	Py_INCREF(Py_NotImplemented);
	return Py_NotImplemented;
}

SLOT0(slot_nb_negative, "__neg__")
SLOT0(slot_nb_positive, "__pos__")
SLOT0(slot_nb_absolute, "__abs__")

static int
slot_nb_nonzero(PyObject *self)
{
	PyObject *func, *args;
	static PyObject *nonzero_str, *len_str;
	int result = -1;

	func = lookup_maybe(self, "__nonzero__", &nonzero_str);
	if (func == NULL) {
		if (PyErr_Occurred())
			return -1;
		func = lookup_maybe(self, "__len__", &len_str);
		if (func == NULL)
			return PyErr_Occurred() ? -1 : 1;
 	}
	args = PyTuple_New(0);
	if (args != NULL) {
		PyObject *temp = PyObject_Call(func, args, NULL);
		Py_DECREF(args);
		if (temp != NULL) {
			if (PyInt_CheckExact(temp) || PyBool_Check(temp))
				result = PyObject_IsTrue(temp);
			else {
				PyErr_Format(PyExc_TypeError,
					     "__nonzero__ should return "
					     "bool or int, returned %s",
					     temp->ob_type->tp_name);
				result = -1;
			}
			Py_DECREF(temp);
		}
	}
	Py_DECREF(func);
	return result;
}


static PyObject *
slot_nb_index(PyObject *self)
{
	static PyObject *index_str;
	return call_method(self, "__index__", &index_str, "()");
}


SLOT0(slot_nb_invert, "__invert__")
SLOT1BIN(slot_nb_lshift, nb_lshift, "__lshift__", "__rlshift__")
SLOT1BIN(slot_nb_rshift, nb_rshift, "__rshift__", "__rrshift__")
SLOT1BIN(slot_nb_and, nb_and, "__and__", "__rand__")
SLOT1BIN(slot_nb_xor, nb_xor, "__xor__", "__rxor__")
SLOT1BIN(slot_nb_or, nb_or, "__or__", "__ror__")

static int
slot_nb_coerce(PyObject **a, PyObject **b)
{
	static PyObject *coerce_str;
	PyObject *self = *a, *other = *b;

	if (self->ob_type->tp_as_number != NULL &&
	    self->ob_type->tp_as_number->nb_coerce == slot_nb_coerce) {
		PyObject *r;
		r = call_maybe(
			self, "__coerce__", &coerce_str, "(O)", other);
		if (r == NULL)
			return -1;
		if (r == Py_NotImplemented) {
			Py_DECREF(r);
		}
		else {
			if (!PyTuple_Check(r) || PyTuple_GET_SIZE(r) != 2) {
				PyErr_SetString(PyExc_TypeError,
					"__coerce__ didn't return a 2-tuple");
				Py_DECREF(r);
				return -1;
			}
			*a = PyTuple_GET_ITEM(r, 0);
			Py_INCREF(*a);
			*b = PyTuple_GET_ITEM(r, 1);
			Py_INCREF(*b);
			Py_DECREF(r);
			return 0;
		}
	}
	if (other->ob_type->tp_as_number != NULL &&
	    other->ob_type->tp_as_number->nb_coerce == slot_nb_coerce) {
		PyObject *r;
		r = call_maybe(
			other, "__coerce__", &coerce_str, "(O)", self);
		if (r == NULL)
			return -1;
		if (r == Py_NotImplemented) {
			Py_DECREF(r);
			return 1;
		}
		if (!PyTuple_Check(r) || PyTuple_GET_SIZE(r) != 2) {
			PyErr_SetString(PyExc_TypeError,
					"__coerce__ didn't return a 2-tuple");
			Py_DECREF(r);
			return -1;
		}
		*a = PyTuple_GET_ITEM(r, 1);
		Py_INCREF(*a);
		*b = PyTuple_GET_ITEM(r, 0);
		Py_INCREF(*b);
		Py_DECREF(r);
		return 0;
	}
	return 1;
}

SLOT0(slot_nb_int, "__int__")
SLOT0(slot_nb_long, "__long__")
SLOT0(slot_nb_float, "__float__")
SLOT0(slot_nb_oct, "__oct__")
SLOT0(slot_nb_hex, "__hex__")
SLOT1(slot_nb_inplace_add, "__iadd__", PyObject *, "O")
SLOT1(slot_nb_inplace_subtract, "__isub__", PyObject *, "O")
SLOT1(slot_nb_inplace_multiply, "__imul__", PyObject *, "O")
SLOT1(slot_nb_inplace_divide, "__idiv__", PyObject *, "O")
SLOT1(slot_nb_inplace_remainder, "__imod__", PyObject *, "O")
/* Can't use SLOT1 here, because nb_inplace_power is ternary */
static PyObject * 
slot_nb_inplace_power(PyObject *self, PyObject * arg1, PyObject *arg2) 
{ 
  static PyObject *cache_str; 
  return call_method(self, "__ipow__", &cache_str, "(" "O" ")", arg1); 
}
SLOT1(slot_nb_inplace_lshift, "__ilshift__", PyObject *, "O")
SLOT1(slot_nb_inplace_rshift, "__irshift__", PyObject *, "O")
SLOT1(slot_nb_inplace_and, "__iand__", PyObject *, "O")
SLOT1(slot_nb_inplace_xor, "__ixor__", PyObject *, "O")
SLOT1(slot_nb_inplace_or, "__ior__", PyObject *, "O")
SLOT1BIN(slot_nb_floor_divide, nb_floor_divide,
	 "__floordiv__", "__rfloordiv__")
SLOT1BIN(slot_nb_true_divide, nb_true_divide, "__truediv__", "__rtruediv__")
SLOT1(slot_nb_inplace_floor_divide, "__ifloordiv__", PyObject *, "O")
SLOT1(slot_nb_inplace_true_divide, "__itruediv__", PyObject *, "O")

static int
half_compare(PyObject *self, PyObject *other)
{
	PyObject *func, *args, *res;
	static PyObject *cmp_str;
	Py_ssize_t c;

	func = lookup_method(self, "__cmp__", &cmp_str);
	if (func == NULL) {
		PyErr_Clear();
	}
	else {
		args = PyTuple_Pack(1, other);
		if (args == NULL)
			res = NULL;
		else {
			res = PyObject_Call(func, args, NULL);
			Py_DECREF(args);
		}
		Py_DECREF(func);
		if (res != Py_NotImplemented) {
			if (res == NULL)
				return -2;
			c = PyInt_AsLong(res);
			Py_DECREF(res);
			if (c == -1 && PyErr_Occurred())
				return -2;
			return (c < 0) ? -1 : (c > 0) ? 1 : 0;
		}
		Py_DECREF(res);
	}
	return 2;
}

/* This slot is published for the benefit of try_3way_compare in object.c */
int
_PyObject_SlotCompare(PyObject *self, PyObject *other)
{
	int c;

	if (self->ob_type->tp_compare == _PyObject_SlotCompare) {
		c = half_compare(self, other);
		if (c <= 1)
			return c;
	}
	if (other->ob_type->tp_compare == _PyObject_SlotCompare) {
		c = half_compare(other, self);
		if (c < -1)
			return -2;
		if (c <= 1)
			return -c;
	}
	return (void *)self < (void *)other ? -1 :
		(void *)self > (void *)other ? 1 : 0;
}

static PyObject *
slot_tp_repr(PyObject *self)
{
	PyObject *func, *res;
	static PyObject *repr_str;

	func = lookup_method(self, "__repr__", &repr_str);
	if (func != NULL) {
		res = PyEval_CallObject(func, NULL);
		Py_DECREF(func);
		return res;
	}
	PyErr_Clear();
	return PyString_FromFormat("<%s object at %p>",
				   self->ob_type->tp_name, self);
}

static PyObject *
slot_tp_str(PyObject *self)
{
	PyObject *func, *res;
	static PyObject *str_str;

	func = lookup_method(self, "__str__", &str_str);
	if (func != NULL) {
		res = PyEval_CallObject(func, NULL);
		Py_DECREF(func);
		return res;
	}
	else {
		PyErr_Clear();
		return slot_tp_repr(self);
	}
}

static long
slot_tp_hash(PyObject *self)
{
	PyObject *func;
	static PyObject *hash_str, *eq_str, *cmp_str;
	long h;

	func = lookup_method(self, "__hash__", &hash_str);

	if (func != NULL) {
		PyObject *res = PyEval_CallObject(func, NULL);
		Py_DECREF(func);
		if (res == NULL)
			return -1;
		if (PyLong_Check(res))
			h = PyLong_Type.tp_hash(res);
		else
			h = PyInt_AsLong(res);
		Py_DECREF(res);
	}
	else {
		PyErr_Clear();
		func = lookup_method(self, "__eq__", &eq_str);
		if (func == NULL) {
			PyErr_Clear();
			func = lookup_method(self, "__cmp__", &cmp_str);
		}
		if (func != NULL) {
			PyErr_Format(PyExc_TypeError, "unhashable type: '%.200s'",
				     self->ob_type->tp_name);
			Py_DECREF(func);
			return -1;
		}
		PyErr_Clear();
		h = _Py_HashPointer((void *)self);
	}
	if (h == -1 && !PyErr_Occurred())
		h = -2;
	return h;
}

static PyObject *
slot_tp_call(PyObject *self, PyObject *args, PyObject *kwds)
{
	static PyObject *call_str;
	PyObject *meth = lookup_method(self, "__call__", &call_str);
	PyObject *res;

	if (meth == NULL)
		return NULL;

	/* PyObject_Call() will end up calling slot_tp_call() again if
	   the object returned for __call__ has __call__ itself defined
	   upon it.  This can be an infinite recursion if you set
	   __call__ in a class to an instance of it. */
	if (Py_EnterRecursiveCall(" in __call__")) {
		Py_DECREF(meth);
		return NULL;
	}
	res = PyObject_Call(meth, args, kwds);
	Py_LeaveRecursiveCall();

	Py_DECREF(meth);
	return res;
}

/* There are two slot dispatch functions for tp_getattro.

   - slot_tp_getattro() is used when __getattribute__ is overridden
     but no __getattr__ hook is present;

   - slot_tp_getattr_hook() is used when a __getattr__ hook is present.

   The code in update_one_slot() always installs slot_tp_getattr_hook(); this
   detects the absence of __getattr__ and then installs the simpler slot if
   necessary. */

static PyObject *
slot_tp_getattro(PyObject *self, PyObject *name)
{
	static PyObject *getattribute_str = NULL;
	return call_method(self, "__getattribute__", &getattribute_str,
			   "(O)", name);
}

static PyObject *
slot_tp_getattr_hook(PyObject *self, PyObject *name)
{
	PyTypeObject *tp = self->ob_type;
	PyObject *getattr, *getattribute, *res;
	static PyObject *getattribute_str = NULL;
	static PyObject *getattr_str = NULL;

	if (getattr_str == NULL) {
		getattr_str = PyString_InternFromString("__getattr__");
		if (getattr_str == NULL)
			return NULL;
	}
	if (getattribute_str == NULL) {
		getattribute_str =
			PyString_InternFromString("__getattribute__");
		if (getattribute_str == NULL)
			return NULL;
	}
	getattr = _PyType_Lookup(tp, getattr_str);
	if (getattr == NULL) {
		/* No __getattr__ hook: use a simpler dispatcher */
		tp->tp_getattro = slot_tp_getattro;
		return slot_tp_getattro(self, name);
	}
	getattribute = _PyType_Lookup(tp, getattribute_str);
	if (getattribute == NULL ||
	    (getattribute->ob_type == &PyWrapperDescr_Type &&
	     ((PyWrapperDescrObject *)getattribute)->d_wrapped ==
	     (void *)PyObject_GenericGetAttr))
		res = PyObject_GenericGetAttr(self, name);
	else
		res = PyObject_CallFunctionObjArgs(getattribute, self, name, NULL);
	if (res == NULL && PyErr_ExceptionMatches(PyExc_AttributeError)) {
		PyErr_Clear();
		res = PyObject_CallFunctionObjArgs(getattr, self, name, NULL);
	}
	return res;
}

static int
slot_tp_setattro(PyObject *self, PyObject *name, PyObject *value)
{
	PyObject *res;
	static PyObject *delattr_str, *setattr_str;

	if (value == NULL)
		res = call_method(self, "__delattr__", &delattr_str,
				  "(O)", name);
	else
		res = call_method(self, "__setattr__", &setattr_str,
				  "(OO)", name, value);
	if (res == NULL)
		return -1;
	Py_DECREF(res);
	return 0;
}

/* Map rich comparison operators to their __xx__ namesakes */
static char *name_op[] = {
	"__lt__",
	"__le__",
	"__eq__",
	"__ne__",
	"__gt__",
	"__ge__",
};

static PyObject *
half_richcompare(PyObject *self, PyObject *other, int op)
{
	PyObject *func, *args, *res;
	static PyObject *op_str[6];

	func = lookup_method(self, name_op[op], &op_str[op]);
	if (func == NULL) {
		PyErr_Clear();
		Py_INCREF(Py_NotImplemented);
		return Py_NotImplemented;
	}
	args = PyTuple_Pack(1, other);
	if (args == NULL)
		res = NULL;
	else {
		res = PyObject_Call(func, args, NULL);
		Py_DECREF(args);
	}
	Py_DECREF(func);
	return res;
}

static PyObject *
slot_tp_richcompare(PyObject *self, PyObject *other, int op)
{
	PyObject *res;

	if (self->ob_type->tp_richcompare == slot_tp_richcompare) {
		res = half_richcompare(self, other, op);
		if (res != Py_NotImplemented)
			return res;
		Py_DECREF(res);
	}
	if (other->ob_type->tp_richcompare == slot_tp_richcompare) {
		res = half_richcompare(other, self, _Py_SwappedOp[op]);
		if (res != Py_NotImplemented) {
			return res;
		}
		Py_DECREF(res);
	}
	Py_INCREF(Py_NotImplemented);
	return Py_NotImplemented;
}

static PyObject *
slot_tp_iter(PyObject *self)
{
	PyObject *func, *res;
	static PyObject *iter_str, *getitem_str;

	func = lookup_method(self, "__iter__", &iter_str);
	if (func != NULL) {
		PyObject *args;
		args = res = PyTuple_New(0);
		if (args != NULL) {
			res = PyObject_Call(func, args, NULL);
			Py_DECREF(args);
		}
		Py_DECREF(func);
		return res;
	}
	PyErr_Clear();
	func = lookup_method(self, "__getitem__", &getitem_str);
	if (func == NULL) {
		PyErr_Format(PyExc_TypeError,
			     "'%.200s' object is not iterable",
			     self->ob_type->tp_name);
		return NULL;
	}
	Py_DECREF(func);
	return PySeqIter_New(self);
}

static PyObject *
slot_tp_iternext(PyObject *self)
{
	static PyObject *next_str;
	return call_method(self, "next", &next_str, "()");
}

static PyObject *
slot_tp_descr_get(PyObject *self, PyObject *obj, PyObject *type)
{
	PyTypeObject *tp = self->ob_type;
	PyObject *get;
	static PyObject *get_str = NULL;

	if (get_str == NULL) {
		get_str = PyString_InternFromString("__get__");
		if (get_str == NULL)
			return NULL;
	}
	get = _PyType_Lookup(tp, get_str);
	if (get == NULL) {
		/* Avoid further slowdowns */
		if (tp->tp_descr_get == slot_tp_descr_get)
			tp->tp_descr_get = NULL;
		Py_INCREF(self);
		return self;
	}
	if (obj == NULL)
		obj = Py_None;
	if (type == NULL)
		type = Py_None;
	return PyObject_CallFunctionObjArgs(get, self, obj, type, NULL);
}

static int
slot_tp_descr_set(PyObject *self, PyObject *target, PyObject *value)
{
	PyObject *res;
	static PyObject *del_str, *set_str;

	if (value == NULL)
		res = call_method(self, "__delete__", &del_str,
				  "(O)", target);
	else
		res = call_method(self, "__set__", &set_str,
				  "(OO)", target, value);
	if (res == NULL)
		return -1;
	Py_DECREF(res);
	return 0;
}

static int
slot_tp_init(PyObject *self, PyObject *args, PyObject *kwds)
{
	static PyObject *init_str;
	PyObject *meth = lookup_method(self, "__init__", &init_str);
	PyObject *res;

	if (meth == NULL)
		return -1;
	res = PyObject_Call(meth, args, kwds);
	Py_DECREF(meth);
	if (res == NULL)
		return -1;
	if (res != Py_None) {
		PyErr_Format(PyExc_TypeError,
			     "__init__() should return None, not '%.200s'",
			     res->ob_type->tp_name);
		Py_DECREF(res);
		return -1;
	}
	Py_DECREF(res);
	return 0;
}

static PyObject *
slot_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	static PyObject *new_str;
	PyObject *func;
	PyObject *newargs, *x;
	Py_ssize_t i, n;

	if (new_str == NULL) {
		new_str = PyString_InternFromString("__new__");
		if (new_str == NULL)
			return NULL;
	}
	func = PyObject_GetAttr((PyObject *)type, new_str);
	if (func == NULL)
		return NULL;
	assert(PyTuple_Check(args));
	n = PyTuple_GET_SIZE(args);
	newargs = PyTuple_New(n+1);
	if (newargs == NULL)
		return NULL;
	Py_INCREF(type);
	PyTuple_SET_ITEM(newargs, 0, (PyObject *)type);
	for (i = 0; i < n; i++) {
		x = PyTuple_GET_ITEM(args, i);
		Py_INCREF(x);
		PyTuple_SET_ITEM(newargs, i+1, x);
	}
	x = PyObject_Call(func, newargs, kwds);
	Py_DECREF(newargs);
	Py_DECREF(func);
	return x;
}

static void
slot_tp_del(PyObject *self)
{
	static PyObject *del_str = NULL;
	PyObject *del, *res;
	PyObject *error_type, *error_value, *error_traceback;

	/* Temporarily resurrect the object. */
	assert(self->ob_refcnt == 0);
	self->ob_refcnt = 1;

	/* Save the current exception, if any. */
	PyErr_Fetch(&error_type, &error_value, &error_traceback);

	/* Execute __del__ method, if any. */
	del = lookup_maybe(self, "__del__", &del_str);
	if (del != NULL) {
		res = PyEval_CallObject(del, NULL);
		if (res == NULL)
			PyErr_WriteUnraisable(del);
		else
			Py_DECREF(res);
		Py_DECREF(del);
	}

	/* Restore the saved exception. */
	PyErr_Restore(error_type, error_value, error_traceback);

	/* Undo the temporary resurrection; can't use DECREF here, it would
	 * cause a recursive call.
	 */
	assert(self->ob_refcnt > 0);
	if (--self->ob_refcnt == 0)
		return;	/* this is the normal path out */

	/* __del__ resurrected it!  Make it look like the original Py_DECREF
	 * never happened.
	 */
	{
		Py_ssize_t refcnt = self->ob_refcnt;
		_Py_NewReference(self);
		self->ob_refcnt = refcnt;
	}
	assert(!PyType_IS_GC(self->ob_type) ||
	       _Py_AS_GC(self)->gc.gc_refs != _PyGC_REFS_UNTRACKED);
	/* If Py_REF_DEBUG, _Py_NewReference bumped _Py_RefTotal, so
	 * we need to undo that. */
	_Py_DEC_REFTOTAL;
	/* If Py_TRACE_REFS, _Py_NewReference re-added self to the object
	 * chain, so no more to do there.
	 * If COUNT_ALLOCS, the original decref bumped tp_frees, and
	 * _Py_NewReference bumped tp_allocs:  both of those need to be
	 * undone.
	 */
#ifdef COUNT_ALLOCS
	--self->ob_type->tp_frees;
	--self->ob_type->tp_allocs;
#endif
}


/* Table mapping __foo__ names to tp_foo offsets and slot_tp_foo wrapper
   functions.  The offsets here are relative to the 'PyHeapTypeObject'
   structure, which incorporates the additional structures used for numbers,
   sequences and mappings.
   Note that multiple names may map to the same slot (e.g. __eq__,
   __ne__ etc. all map to tp_richcompare) and one name may map to multiple
   slots (e.g. __str__ affects tp_str as well as tp_repr). The table is
   terminated with an all-zero entry.  (This table is further initialized and
   sorted in init_slotdefs() below.) */

typedef struct wrapperbase slotdef;

#undef TPSLOT
#undef FLSLOT
#undef ETSLOT
#undef SQSLOT
#undef MPSLOT
#undef NBSLOT
#undef UNSLOT
#undef IBSLOT
#undef BINSLOT
#undef RBINSLOT

#define TPSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
	{NAME, offsetof(PyTypeObject, SLOT), (void *)(FUNCTION), WRAPPER, \
	 PyDoc_STR(DOC)}
#define FLSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC, FLAGS) \
	{NAME, offsetof(PyTypeObject, SLOT), (void *)(FUNCTION), WRAPPER, \
	 PyDoc_STR(DOC), FLAGS}
#define ETSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
	{NAME, offsetof(PyHeapTypeObject, SLOT), (void *)(FUNCTION), WRAPPER, \
	 PyDoc_STR(DOC)}
#define SQSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
	ETSLOT(NAME, as_sequence.SLOT, FUNCTION, WRAPPER, DOC)
#define MPSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
	ETSLOT(NAME, as_mapping.SLOT, FUNCTION, WRAPPER, DOC)
#define NBSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
	ETSLOT(NAME, as_number.SLOT, FUNCTION, WRAPPER, DOC)
#define UNSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
	ETSLOT(NAME, as_number.SLOT, FUNCTION, WRAPPER, \
	       "x." NAME "() <==> " DOC)
#define IBSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
	ETSLOT(NAME, as_number.SLOT, FUNCTION, WRAPPER, \
	       "x." NAME "(y) <==> x" DOC "y")
#define BINSLOT(NAME, SLOT, FUNCTION, DOC) \
	ETSLOT(NAME, as_number.SLOT, FUNCTION, wrap_binaryfunc_l, \
	       "x." NAME "(y) <==> x" DOC "y")
#define RBINSLOT(NAME, SLOT, FUNCTION, DOC) \
	ETSLOT(NAME, as_number.SLOT, FUNCTION, wrap_binaryfunc_r, \
	       "x." NAME "(y) <==> y" DOC "x")
#define BINSLOTNOTINFIX(NAME, SLOT, FUNCTION, DOC) \
	ETSLOT(NAME, as_number.SLOT, FUNCTION, wrap_binaryfunc_l, \
	       "x." NAME "(y) <==> " DOC)
#define RBINSLOTNOTINFIX(NAME, SLOT, FUNCTION, DOC) \
	ETSLOT(NAME, as_number.SLOT, FUNCTION, wrap_binaryfunc_r, \
	       "x." NAME "(y) <==> " DOC)

static slotdef slotdefs[] = {
	SQSLOT("__len__", sq_length, slot_sq_length, wrap_lenfunc,
	       "x.__len__() <==> len(x)"),
	/* Heap types defining __add__/__mul__ have sq_concat/sq_repeat == NULL.
	   The logic in abstract.c always falls back to nb_add/nb_multiply in
	   this case.  Defining both the nb_* and the sq_* slots to call the
	   user-defined methods has unexpected side-effects, as shown by
	   test_descr.notimplemented() */
	SQSLOT("__add__", sq_concat, NULL, wrap_binaryfunc,
          "x.__add__(y) <==> x+y"),
	SQSLOT("__mul__", sq_repeat, NULL, wrap_indexargfunc,
          "x.__mul__(n) <==> x*n"),
	SQSLOT("__rmul__", sq_repeat, NULL, wrap_indexargfunc,
          "x.__rmul__(n) <==> n*x"),
	SQSLOT("__getitem__", sq_item, slot_sq_item, wrap_sq_item,
	       "x.__getitem__(y) <==> x[y]"),
	SQSLOT("__getslice__", sq_slice, slot_sq_slice, wrap_ssizessizeargfunc,
	       "x.__getslice__(i, j) <==> x[i:j]\n\
               \n\
               Use of negative indices is not supported."),
	SQSLOT("__setitem__", sq_ass_item, slot_sq_ass_item, wrap_sq_setitem,
	       "x.__setitem__(i, y) <==> x[i]=y"),
	SQSLOT("__delitem__", sq_ass_item, slot_sq_ass_item, wrap_sq_delitem,
	       "x.__delitem__(y) <==> del x[y]"),
	SQSLOT("__setslice__", sq_ass_slice, slot_sq_ass_slice,
	       wrap_ssizessizeobjargproc,
	       "x.__setslice__(i, j, y) <==> x[i:j]=y\n\
               \n\
               Use  of negative indices is not supported."),
	SQSLOT("__delslice__", sq_ass_slice, slot_sq_ass_slice, wrap_delslice,
	       "x.__delslice__(i, j) <==> del x[i:j]\n\
               \n\
               Use of negative indices is not supported."),
	SQSLOT("__contains__", sq_contains, slot_sq_contains, wrap_objobjproc,
	       "x.__contains__(y) <==> y in x"),
	SQSLOT("__iadd__", sq_inplace_concat, NULL,
          wrap_binaryfunc, "x.__iadd__(y) <==> x+=y"),
	SQSLOT("__imul__", sq_inplace_repeat, NULL,
          wrap_indexargfunc, "x.__imul__(y) <==> x*=y"),

	MPSLOT("__len__", mp_length, slot_mp_length, wrap_lenfunc,
	       "x.__len__() <==> len(x)"),
	MPSLOT("__getitem__", mp_subscript, slot_mp_subscript,
	       wrap_binaryfunc,
	       "x.__getitem__(y) <==> x[y]"),
	MPSLOT("__setitem__", mp_ass_subscript, slot_mp_ass_subscript,
	       wrap_objobjargproc,
	       "x.__setitem__(i, y) <==> x[i]=y"),
	MPSLOT("__delitem__", mp_ass_subscript, slot_mp_ass_subscript,
	       wrap_delitem,
	       "x.__delitem__(y) <==> del x[y]"),

	BINSLOT("__add__", nb_add, slot_nb_add,
		"+"),
	RBINSLOT("__radd__", nb_add, slot_nb_add,
		 "+"),
	BINSLOT("__sub__", nb_subtract, slot_nb_subtract,
		"-"),
	RBINSLOT("__rsub__", nb_subtract, slot_nb_subtract,
		 "-"),
	BINSLOT("__mul__", nb_multiply, slot_nb_multiply,
		"*"),
	RBINSLOT("__rmul__", nb_multiply, slot_nb_multiply,
		 "*"),
	BINSLOT("__div__", nb_divide, slot_nb_divide,
		"/"),
	RBINSLOT("__rdiv__", nb_divide, slot_nb_divide,
		 "/"),
	BINSLOT("__mod__", nb_remainder, slot_nb_remainder,
		"%"),
	RBINSLOT("__rmod__", nb_remainder, slot_nb_remainder,
		 "%"),
	BINSLOTNOTINFIX("__divmod__", nb_divmod, slot_nb_divmod,
		"divmod(x, y)"),
	RBINSLOTNOTINFIX("__rdivmod__", nb_divmod, slot_nb_divmod,
		 "divmod(y, x)"),
	NBSLOT("__pow__", nb_power, slot_nb_power, wrap_ternaryfunc,
	       "x.__pow__(y[, z]) <==> pow(x, y[, z])"),
	NBSLOT("__rpow__", nb_power, slot_nb_power, wrap_ternaryfunc_r,
	       "y.__rpow__(x[, z]) <==> pow(x, y[, z])"),
	UNSLOT("__neg__", nb_negative, slot_nb_negative, wrap_unaryfunc, "-x"),
	UNSLOT("__pos__", nb_positive, slot_nb_positive, wrap_unaryfunc, "+x"),
	UNSLOT("__abs__", nb_absolute, slot_nb_absolute, wrap_unaryfunc,
	       "abs(x)"),
	UNSLOT("__nonzero__", nb_nonzero, slot_nb_nonzero, wrap_inquirypred,
	       "x != 0"),
	UNSLOT("__invert__", nb_invert, slot_nb_invert, wrap_unaryfunc, "~x"),
	BINSLOT("__lshift__", nb_lshift, slot_nb_lshift, "<<"),
	RBINSLOT("__rlshift__", nb_lshift, slot_nb_lshift, "<<"),
	BINSLOT("__rshift__", nb_rshift, slot_nb_rshift, ">>"),
	RBINSLOT("__rrshift__", nb_rshift, slot_nb_rshift, ">>"),
	BINSLOT("__and__", nb_and, slot_nb_and, "&"),
	RBINSLOT("__rand__", nb_and, slot_nb_and, "&"),
	BINSLOT("__xor__", nb_xor, slot_nb_xor, "^"),
	RBINSLOT("__rxor__", nb_xor, slot_nb_xor, "^"),
	BINSLOT("__or__", nb_or, slot_nb_or, "|"),
	RBINSLOT("__ror__", nb_or, slot_nb_or, "|"),
	NBSLOT("__coerce__", nb_coerce, slot_nb_coerce, wrap_coercefunc,
	       "x.__coerce__(y) <==> coerce(x, y)"),
	UNSLOT("__int__", nb_int, slot_nb_int, wrap_unaryfunc,
	       "int(x)"),
	UNSLOT("__long__", nb_long, slot_nb_long, wrap_unaryfunc,
	       "long(x)"),
	UNSLOT("__float__", nb_float, slot_nb_float, wrap_unaryfunc,
	       "float(x)"),
	UNSLOT("__oct__", nb_oct, slot_nb_oct, wrap_unaryfunc,
	       "oct(x)"),
	UNSLOT("__hex__", nb_hex, slot_nb_hex, wrap_unaryfunc,
	       "hex(x)"),
	NBSLOT("__index__", nb_index, slot_nb_index, wrap_unaryfunc, 
	       "x[y:z] <==> x[y.__index__():z.__index__()]"),
	IBSLOT("__iadd__", nb_inplace_add, slot_nb_inplace_add,
	       wrap_binaryfunc, "+"),
	IBSLOT("__isub__", nb_inplace_subtract, slot_nb_inplace_subtract,
	       wrap_binaryfunc, "-"),
	IBSLOT("__imul__", nb_inplace_multiply, slot_nb_inplace_multiply,
	       wrap_binaryfunc, "*"),
	IBSLOT("__idiv__", nb_inplace_divide, slot_nb_inplace_divide,
	       wrap_binaryfunc, "/"),
	IBSLOT("__imod__", nb_inplace_remainder, slot_nb_inplace_remainder,
	       wrap_binaryfunc, "%"),
	IBSLOT("__ipow__", nb_inplace_power, slot_nb_inplace_power,
	       wrap_binaryfunc, "**"),
	IBSLOT("__ilshift__", nb_inplace_lshift, slot_nb_inplace_lshift,
	       wrap_binaryfunc, "<<"),
	IBSLOT("__irshift__", nb_inplace_rshift, slot_nb_inplace_rshift,
	       wrap_binaryfunc, ">>"),
	IBSLOT("__iand__", nb_inplace_and, slot_nb_inplace_and,
	       wrap_binaryfunc, "&"),
	IBSLOT("__ixor__", nb_inplace_xor, slot_nb_inplace_xor,
	       wrap_binaryfunc, "^"),
	IBSLOT("__ior__", nb_inplace_or, slot_nb_inplace_or,
	       wrap_binaryfunc, "|"),
	BINSLOT("__floordiv__", nb_floor_divide, slot_nb_floor_divide, "//"),
	RBINSLOT("__rfloordiv__", nb_floor_divide, slot_nb_floor_divide, "//"),
	BINSLOT("__truediv__", nb_true_divide, slot_nb_true_divide, "/"),
	RBINSLOT("__rtruediv__", nb_true_divide, slot_nb_true_divide, "/"),
	IBSLOT("__ifloordiv__", nb_inplace_floor_divide,
	       slot_nb_inplace_floor_divide, wrap_binaryfunc, "//"),
	IBSLOT("__itruediv__", nb_inplace_true_divide,
	       slot_nb_inplace_true_divide, wrap_binaryfunc, "/"),

	TPSLOT("__str__", tp_str, slot_tp_str, wrap_unaryfunc,
	       "x.__str__() <==> str(x)"),
	TPSLOT("__str__", tp_print, NULL, NULL, ""),
	TPSLOT("__repr__", tp_repr, slot_tp_repr, wrap_unaryfunc,
	       "x.__repr__() <==> repr(x)"),
	TPSLOT("__repr__", tp_print, NULL, NULL, ""),
	TPSLOT("__cmp__", tp_compare, _PyObject_SlotCompare, wrap_cmpfunc,
	       "x.__cmp__(y) <==> cmp(x,y)"),
	TPSLOT("__hash__", tp_hash, slot_tp_hash, wrap_hashfunc,
	       "x.__hash__() <==> hash(x)"),
	FLSLOT("__call__", tp_call, slot_tp_call, (wrapperfunc)wrap_call,
	       "x.__call__(...) <==> x(...)", PyWrapperFlag_KEYWORDS),
	TPSLOT("__getattribute__", tp_getattro, slot_tp_getattr_hook,
	       wrap_binaryfunc, "x.__getattribute__('name') <==> x.name"),
	TPSLOT("__getattribute__", tp_getattr, NULL, NULL, ""),
	TPSLOT("__getattr__", tp_getattro, slot_tp_getattr_hook, NULL, ""),
	TPSLOT("__getattr__", tp_getattr, NULL, NULL, ""),
	TPSLOT("__setattr__", tp_setattro, slot_tp_setattro, wrap_setattr,
	       "x.__setattr__('name', value) <==> x.name = value"),
	TPSLOT("__setattr__", tp_setattr, NULL, NULL, ""),
	TPSLOT("__delattr__", tp_setattro, slot_tp_setattro, wrap_delattr,
	       "x.__delattr__('name') <==> del x.name"),
	TPSLOT("__delattr__", tp_setattr, NULL, NULL, ""),
	TPSLOT("__lt__", tp_richcompare, slot_tp_richcompare, richcmp_lt,
	       "x.__lt__(y) <==> x<y"),
	TPSLOT("__le__", tp_richcompare, slot_tp_richcompare, richcmp_le,
	       "x.__le__(y) <==> x<=y"),
	TPSLOT("__eq__", tp_richcompare, slot_tp_richcompare, richcmp_eq,
	       "x.__eq__(y) <==> x==y"),
	TPSLOT("__ne__", tp_richcompare, slot_tp_richcompare, richcmp_ne,
	       "x.__ne__(y) <==> x!=y"),
	TPSLOT("__gt__", tp_richcompare, slot_tp_richcompare, richcmp_gt,
	       "x.__gt__(y) <==> x>y"),
	TPSLOT("__ge__", tp_richcompare, slot_tp_richcompare, richcmp_ge,
	       "x.__ge__(y) <==> x>=y"),
	TPSLOT("__iter__", tp_iter, slot_tp_iter, wrap_unaryfunc,
	       "x.__iter__() <==> iter(x)"),
	TPSLOT("next", tp_iternext, slot_tp_iternext, wrap_next,
	       "x.next() -> the next value, or raise StopIteration"),
	TPSLOT("__get__", tp_descr_get, slot_tp_descr_get, wrap_descr_get,
	       "descr.__get__(obj[, type]) -> value"),
	TPSLOT("__set__", tp_descr_set, slot_tp_descr_set, wrap_descr_set,
	       "descr.__set__(obj, value)"),
	TPSLOT("__delete__", tp_descr_set, slot_tp_descr_set,
	       wrap_descr_delete, "descr.__delete__(obj)"),
	FLSLOT("__init__", tp_init, slot_tp_init, (wrapperfunc)wrap_init,
	       "x.__init__(...) initializes x; "
	       "see x.__class__.__doc__ for signature",
	       PyWrapperFlag_KEYWORDS),
	TPSLOT("__new__", tp_new, slot_tp_new, NULL, ""),
	TPSLOT("__del__", tp_del, slot_tp_del, NULL, ""),
	{NULL}
};

/* Given a type pointer and an offset gotten from a slotdef entry, return a
   pointer to the actual slot.  This is not quite the same as simply adding
   the offset to the type pointer, since it takes care to indirect through the
   proper indirection pointer (as_buffer, etc.); it returns NULL if the
   indirection pointer is NULL. */
static void **
slotptr(PyTypeObject *type, int ioffset)
{
	char *ptr;
	long offset = ioffset;

	/* Note: this depends on the order of the members of PyHeapTypeObject! */
	assert(offset >= 0);
	assert((size_t)offset < offsetof(PyHeapTypeObject, as_buffer));
	if ((size_t)offset >= offsetof(PyHeapTypeObject, as_sequence)) {
		ptr = (char *)type->tp_as_sequence;
		offset -= offsetof(PyHeapTypeObject, as_sequence);
	}
	else if ((size_t)offset >= offsetof(PyHeapTypeObject, as_mapping)) {
		ptr = (char *)type->tp_as_mapping;
		offset -= offsetof(PyHeapTypeObject, as_mapping);
	}
	else if ((size_t)offset >= offsetof(PyHeapTypeObject, as_number)) {
		ptr = (char *)type->tp_as_number;
		offset -= offsetof(PyHeapTypeObject, as_number);
	}
	else {
		ptr = (char *)type;
	}
	if (ptr != NULL)
		ptr += offset;
	return (void **)ptr;
}

/* Length of array of slotdef pointers used to store slots with the
   same __name__.  There should be at most MAX_EQUIV-1 slotdef entries with
   the same __name__, for any __name__. Since that's a static property, it is
   appropriate to declare fixed-size arrays for this. */
#define MAX_EQUIV 10

/* Return a slot pointer for a given name, but ONLY if the attribute has
   exactly one slot function.  The name must be an interned string. */
static void **
resolve_slotdups(PyTypeObject *type, PyObject *name)
{
	/* XXX Maybe this could be optimized more -- but is it worth it? */

	/* pname and ptrs act as a little cache */
	static PyObject *pname;
	static slotdef *ptrs[MAX_EQUIV];
	slotdef *p, **pp;
	void **res, **ptr;

	if (pname != name) {
		/* Collect all slotdefs that match name into ptrs. */
		pname = name;
		pp = ptrs;
		for (p = slotdefs; p->name_strobj; p++) {
			if (p->name_strobj == name)
				*pp++ = p;
		}
		*pp = NULL;
	}

	/* Look in all matching slots of the type; if exactly one of these has
	   a filled-in slot, return its value.  Otherwise return NULL. */
	res = NULL;
	for (pp = ptrs; *pp; pp++) {
		ptr = slotptr(type, (*pp)->offset);
		if (ptr == NULL || *ptr == NULL)
			continue;
		if (res != NULL)
			return NULL;
		res = ptr;
	}
	return res;
}

/* Common code for update_slots_callback() and fixup_slot_dispatchers().  This
   does some incredibly complex thinking and then sticks something into the
   slot.  (It sees if the adjacent slotdefs for the same slot have conflicting
   interests, and then stores a generic wrapper or a specific function into
   the slot.)  Return a pointer to the next slotdef with a different offset,
   because that's convenient  for fixup_slot_dispatchers(). */
static slotdef *
update_one_slot(PyTypeObject *type, slotdef *p)
{
	PyObject *descr;
	PyWrapperDescrObject *d;
	void *generic = NULL, *specific = NULL;
	int use_generic = 0;
	int offset = p->offset;
	void **ptr = slotptr(type, offset);

	if (ptr == NULL) {
		do {
			++p;
		} while (p->offset == offset);
		return p;
	}
	do {
		descr = _PyType_Lookup(type, p->name_strobj);
		if (descr == NULL)
			continue;
		if (descr->ob_type == &PyWrapperDescr_Type) {
			void **tptr = resolve_slotdups(type, p->name_strobj);
			if (tptr == NULL || tptr == ptr)
				generic = p->function;
			d = (PyWrapperDescrObject *)descr;
			if (d->d_base->wrapper == p->wrapper &&
			    PyType_IsSubtype(type, d->d_type))
			{
				if (specific == NULL ||
				    specific == d->d_wrapped)
					specific = d->d_wrapped;
				else
					use_generic = 1;
			}
		}
		else if (descr->ob_type == &PyCFunction_Type &&
			 PyCFunction_GET_FUNCTION(descr) ==
			 (PyCFunction)tp_new_wrapper &&
			 strcmp(p->name, "__new__") == 0)
		{
			/* The __new__ wrapper is not a wrapper descriptor,
			   so must be special-cased differently.
			   If we don't do this, creating an instance will
			   always use slot_tp_new which will look up
			   __new__ in the MRO which will call tp_new_wrapper
			   which will look through the base classes looking
			   for a static base and call its tp_new (usually
			   PyType_GenericNew), after performing various
			   sanity checks and constructing a new argument
			   list.  Cut all that nonsense short -- this speeds
			   up instance creation tremendously. */
			specific = (void *)type->tp_new;
			/* XXX I'm not 100% sure that there isn't a hole
			   in this reasoning that requires additional
			   sanity checks.  I'll buy the first person to
			   point out a bug in this reasoning a beer. */
		}
		else {
			use_generic = 1;
			generic = p->function;
		}
	} while ((++p)->offset == offset);
	if (specific && !use_generic)
		*ptr = specific;
	else
		*ptr = generic;
	return p;
}

/* In the type, update the slots whose slotdefs are gathered in the pp array.
   This is a callback for update_subclasses(). */
static int
update_slots_callback(PyTypeObject *type, void *data)
{
	slotdef **pp = (slotdef **)data;

	for (; *pp; pp++)
		update_one_slot(type, *pp);
	return 0;
}

/* Comparison function for qsort() to compare slotdefs by their offset, and
   for equal offset by their address (to force a stable sort). */
static int
slotdef_cmp(const void *aa, const void *bb)
{
	const slotdef *a = (const slotdef *)aa, *b = (const slotdef *)bb;
	int c = a->offset - b->offset;
	if (c != 0)
		return c;
	else
		/* Cannot use a-b, as this gives off_t, 
		   which may lose precision when converted to int. */
		return (a > b) ? 1 : (a < b) ? -1 : 0;
}

/* Initialize the slotdefs table by adding interned string objects for the
   names and sorting the entries. */
static void
init_slotdefs(void)
{
	slotdef *p;
	static int initialized = 0;

	if (initialized)
		return;
	for (p = slotdefs; p->name; p++) {
		p->name_strobj = PyString_InternFromString(p->name);
		if (!p->name_strobj)
			Py_FatalError("Out of memory interning slotdef names");
	}
	qsort((void *)slotdefs, (size_t)(p-slotdefs), sizeof(slotdef),
	      slotdef_cmp);
	initialized = 1;
}

/* Update the slots after assignment to a class (type) attribute. */
static int
update_slot(PyTypeObject *type, PyObject *name)
{
	slotdef *ptrs[MAX_EQUIV];
	slotdef *p;
	slotdef **pp;
	int offset;

	init_slotdefs();
	pp = ptrs;
	for (p = slotdefs; p->name; p++) {
		/* XXX assume name is interned! */
		if (p->name_strobj == name)
			*pp++ = p;
	}
	*pp = NULL;
	for (pp = ptrs; *pp; pp++) {
		p = *pp;
		offset = p->offset;
		while (p > slotdefs && (p-1)->offset == offset)
			--p;
		*pp = p;
	}
	if (ptrs[0] == NULL)
		return 0; /* Not an attribute that affects any slots */
	return update_subclasses(type, name,
				 update_slots_callback, (void *)ptrs);
}

/* Store the proper functions in the slot dispatches at class (type)
   definition time, based upon which operations the class overrides in its
   dict. */
static void
fixup_slot_dispatchers(PyTypeObject *type)
{
	slotdef *p;

	init_slotdefs();
	for (p = slotdefs; p->name; )
		p = update_one_slot(type, p);
}

static void
update_all_slots(PyTypeObject* type)
{
	slotdef *p;

	init_slotdefs();
	for (p = slotdefs; p->name; p++) {
		/* update_slot returns int but can't actually fail */
		update_slot(type, p->name_strobj);
	}
}

/* recurse_down_subclasses() and update_subclasses() are mutually
   recursive functions to call a callback for all subclasses,
   but refraining from recursing into subclasses that define 'name'. */

static int
update_subclasses(PyTypeObject *type, PyObject *name,
		  update_callback callback, void *data)
{
	if (callback(type, data) < 0)
		return -1;
	return recurse_down_subclasses(type, name, callback, data);
}

static int
recurse_down_subclasses(PyTypeObject *type, PyObject *name,
			update_callback callback, void *data)
{
	PyTypeObject *subclass;
	PyObject *ref, *subclasses, *dict;
	Py_ssize_t i, n;

	subclasses = type->tp_subclasses;
	if (subclasses == NULL)
		return 0;
	assert(PyList_Check(subclasses));
	n = PyList_GET_SIZE(subclasses);
	for (i = 0; i < n; i++) {
		ref = PyList_GET_ITEM(subclasses, i);
		assert(PyWeakref_CheckRef(ref));
		subclass = (PyTypeObject *)PyWeakref_GET_OBJECT(ref);
		assert(subclass != NULL);
		if ((PyObject *)subclass == Py_None)
			continue;
		assert(PyType_Check(subclass));
		/* Avoid recursing down into unaffected classes */
		dict = subclass->tp_dict;
		if (dict != NULL && PyDict_Check(dict) &&
		    PyDict_GetItem(dict, name) != NULL)
			continue;
		if (update_subclasses(subclass, name, callback, data) < 0)
			return -1;
	}
	return 0;
}

/* This function is called by PyType_Ready() to populate the type's
   dictionary with method descriptors for function slots.  For each
   function slot (like tp_repr) that's defined in the type, one or more
   corresponding descriptors are added in the type's tp_dict dictionary
   under the appropriate name (like __repr__).  Some function slots
   cause more than one descriptor to be added (for example, the nb_add
   slot adds both __add__ and __radd__ descriptors) and some function
   slots compete for the same descriptor (for example both sq_item and
   mp_subscript generate a __getitem__ descriptor).

   In the latter case, the first slotdef entry encoutered wins.  Since
   slotdef entries are sorted by the offset of the slot in the
   PyHeapTypeObject, this gives us some control over disambiguating
   between competing slots: the members of PyHeapTypeObject are listed
   from most general to least general, so the most general slot is
   preferred.  In particular, because as_mapping comes before as_sequence,
   for a type that defines both mp_subscript and sq_item, mp_subscript
   wins.

   This only adds new descriptors and doesn't overwrite entries in
   tp_dict that were previously defined.  The descriptors contain a
   reference to the C function they must call, so that it's safe if they
   are copied into a subtype's __dict__ and the subtype has a different
   C function in its slot -- calling the method defined by the
   descriptor will call the C function that was used to create it,
   rather than the C function present in the slot when it is called.
   (This is important because a subtype may have a C function in the
   slot that calls the method from the dictionary, and we want to avoid
   infinite recursion here.) */

static int
add_operators(PyTypeObject *type)
{
	PyObject *dict = type->tp_dict;
	slotdef *p;
	PyObject *descr;
	void **ptr;

	init_slotdefs();
	for (p = slotdefs; p->name; p++) {
		if (p->wrapper == NULL)
			continue;
		ptr = slotptr(type, p->offset);
		if (!ptr || !*ptr)
			continue;
		if (PyDict_GetItem(dict, p->name_strobj))
			continue;
		descr = PyDescr_NewWrapper(type, p, *ptr);
		if (descr == NULL)
			return -1;
		if (PyDict_SetItem(dict, p->name_strobj, descr) < 0)
			return -1;
		Py_DECREF(descr);
	}
	if (type->tp_new != NULL) {
		if (add_tp_new_wrapper(type) < 0)
			return -1;
	}
	return 0;
}


/* Cooperative 'super' */

typedef struct {
	PyObject_HEAD
	PyTypeObject *type;
	PyObject *obj;
	PyTypeObject *obj_type;
} superobject;

static PyMemberDef super_members[] = {
	{"__thisclass__", T_OBJECT, offsetof(superobject, type), READONLY,
	 "the class invoking super()"},
	{"__self__",  T_OBJECT, offsetof(superobject, obj), READONLY,
	 "the instance invoking super(); may be None"},
	{"__self_class__", T_OBJECT, offsetof(superobject, obj_type), READONLY,
	 "the type of the instance invoking super(); may be None"},
	{0}
};

static void
super_dealloc(PyObject *self)
{
	superobject *su = (superobject *)self;

	_PyObject_GC_UNTRACK(self);
	Py_XDECREF(su->obj);
	Py_XDECREF(su->type);
	Py_XDECREF(su->obj_type);
	self->ob_type->tp_free(self);
}

static PyObject *
super_repr(PyObject *self)
{
	superobject *su = (superobject *)self;

	if (su->obj_type)
		return PyString_FromFormat(
			"<super: <class '%s'>, <%s object>>",
			su->type ? su->type->tp_name : "NULL",
			su->obj_type->tp_name);
	else
		return PyString_FromFormat(
			"<super: <class '%s'>, NULL>",
			su->type ? su->type->tp_name : "NULL");
}

static PyObject *
super_getattro(PyObject *self, PyObject *name)
{
	superobject *su = (superobject *)self;
	int skip = su->obj_type == NULL;

	if (!skip) {
		/* We want __class__ to return the class of the super object
		   (i.e. super, or a subclass), not the class of su->obj. */
		skip = (PyString_Check(name) &&
			PyString_GET_SIZE(name) == 9 &&
			strcmp(PyString_AS_STRING(name), "__class__") == 0);
	}

	if (!skip) {
		PyObject *mro, *res, *tmp, *dict;
		PyTypeObject *starttype;
		descrgetfunc f;
		Py_ssize_t i, n;

		starttype = su->obj_type;
		mro = starttype->tp_mro;

		if (mro == NULL)
			n = 0;
		else {
			assert(PyTuple_Check(mro));
			n = PyTuple_GET_SIZE(mro);
		}
		for (i = 0; i < n; i++) {
			if ((PyObject *)(su->type) == PyTuple_GET_ITEM(mro, i))
				break;
		}
		i++;
		res = NULL;
		for (; i < n; i++) {
			tmp = PyTuple_GET_ITEM(mro, i);
			if (PyType_Check(tmp))
				dict = ((PyTypeObject *)tmp)->tp_dict;
			else if (PyClass_Check(tmp))
				dict = ((PyClassObject *)tmp)->cl_dict;
			else
				continue;
			res = PyDict_GetItem(dict, name);
			if (res != NULL) {
				Py_INCREF(res);
				f = res->ob_type->tp_descr_get;
				if (f != NULL) {
					tmp = f(res,
						/* Only pass 'obj' param if
						   this is instance-mode super 
						   (See SF ID #743627)
						*/
						(su->obj == (PyObject *)
							    su->obj_type 
							? (PyObject *)NULL 
							: su->obj),
						(PyObject *)starttype);
					Py_DECREF(res);
					res = tmp;
				}
				return res;
			}
		}
	}
	return PyObject_GenericGetAttr(self, name);
}

static PyTypeObject *
supercheck(PyTypeObject *type, PyObject *obj)
{
	/* Check that a super() call makes sense.  Return a type object.

	   obj can be a new-style class, or an instance of one:

	   - If it is a class, it must be a subclass of 'type'.  This case is
	     used for class methods; the return value is obj.

	   - If it is an instance, it must be an instance of 'type'.  This is
	     the normal case; the return value is obj.__class__.

	   But... when obj is an instance, we want to allow for the case where
	   obj->ob_type is not a subclass of type, but obj.__class__ is!
	   This will allow using super() with a proxy for obj.
	*/

	/* Check for first bullet above (special case) */
	if (PyType_Check(obj) && PyType_IsSubtype((PyTypeObject *)obj, type)) {
		Py_INCREF(obj);
		return (PyTypeObject *)obj;
	}

	/* Normal case */
	if (PyType_IsSubtype(obj->ob_type, type)) {
		Py_INCREF(obj->ob_type);
		return obj->ob_type;
	}
	else {
		/* Try the slow way */
		static PyObject *class_str = NULL;
		PyObject *class_attr;

		if (class_str == NULL) {
			class_str = PyString_FromString("__class__");
			if (class_str == NULL)
				return NULL;
		}

		class_attr = PyObject_GetAttr(obj, class_str);

		if (class_attr != NULL &&
		    PyType_Check(class_attr) &&
		    (PyTypeObject *)class_attr != obj->ob_type)
		{
			int ok = PyType_IsSubtype(
				(PyTypeObject *)class_attr, type);
			if (ok)
				return (PyTypeObject *)class_attr;
		}

		if (class_attr == NULL)
			PyErr_Clear();
		else
			Py_DECREF(class_attr);
	}

  	PyErr_SetString(PyExc_TypeError,
			"super(type, obj): "
			"obj must be an instance or subtype of type");
	return NULL;
}

static PyObject *
super_descr_get(PyObject *self, PyObject *obj, PyObject *type)
{
	superobject *su = (superobject *)self;
	superobject *newobj;

	if (obj == NULL || obj == Py_None || su->obj != NULL) {
		/* Not binding to an object, or already bound */
		Py_INCREF(self);
		return self;
	}
	if (su->ob_type != &PySuper_Type)
		/* If su is an instance of a (strict) subclass of super,
		   call its type */
		return PyObject_CallFunctionObjArgs((PyObject *)su->ob_type,
					            su->type, obj, NULL);
	else {
		/* Inline the common case */
		PyTypeObject *obj_type = supercheck(su->type, obj);
		if (obj_type == NULL)
			return NULL;
		newobj = (superobject *)PySuper_Type.tp_new(&PySuper_Type,
							 NULL, NULL);
		if (newobj == NULL)
			return NULL;
		Py_INCREF(su->type);
		Py_INCREF(obj);
		newobj->type = su->type;
		newobj->obj = obj;
		newobj->obj_type = obj_type;
		return (PyObject *)newobj;
	}
}

static int
super_init(PyObject *self, PyObject *args, PyObject *kwds)
{
	superobject *su = (superobject *)self;
	PyTypeObject *type;
	PyObject *obj = NULL;
	PyTypeObject *obj_type = NULL;

	if (!_PyArg_NoKeywords("super", kwds))
		return -1;
	if (!PyArg_ParseTuple(args, "O!|O:super", &PyType_Type, &type, &obj))
		return -1;
	if (obj == Py_None)
		obj = NULL;
	if (obj != NULL) {
		obj_type = supercheck(type, obj);
		if (obj_type == NULL)
			return -1;
		Py_INCREF(obj);
	}
	Py_INCREF(type);
	su->type = type;
	su->obj = obj;
	su->obj_type = obj_type;
	return 0;
}

PyDoc_STRVAR(super_doc,
"super(type) -> unbound super object\n"
"super(type, obj) -> bound super object; requires isinstance(obj, type)\n"
"super(type, type2) -> bound super object; requires issubclass(type2, type)\n"
"Typical use to call a cooperative superclass method:\n"
"class C(B):\n"
"    def meth(self, arg):\n"
"        super(C, self).meth(arg)");

static int
super_traverse(PyObject *self, visitproc visit, void *arg)
{
	superobject *su = (superobject *)self;

	Py_VISIT(su->obj);
	Py_VISIT(su->type);
	Py_VISIT(su->obj_type);

	return 0;
}

PyTypeObject PySuper_Type = {
	PyObject_HEAD_INIT(&PyType_Type)
	0,					/* ob_size */
	"super",				/* tp_name */
	sizeof(superobject),			/* tp_basicsize */
	0,					/* tp_itemsize */
	/* methods */
	super_dealloc,		 		/* tp_dealloc */
	0,					/* tp_print */
	0,					/* tp_getattr */
	0,					/* tp_setattr */
	0,					/* tp_compare */
	super_repr,				/* tp_repr */
	0,					/* tp_as_number */
	0,					/* tp_as_sequence */
	0,		       			/* tp_as_mapping */
	0,					/* tp_hash */
	0,					/* tp_call */
	0,					/* tp_str */
	super_getattro,				/* tp_getattro */
	0,					/* tp_setattro */
	0,					/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
		Py_TPFLAGS_BASETYPE,		/* tp_flags */
 	super_doc,				/* tp_doc */
 	super_traverse,				/* tp_traverse */
 	0,					/* tp_clear */
	0,					/* tp_richcompare */
	0,					/* tp_weaklistoffset */
	0,					/* tp_iter */
	0,					/* tp_iternext */
	0,					/* tp_methods */
	super_members,				/* tp_members */
	0,					/* tp_getset */
	0,					/* tp_base */
	0,					/* tp_dict */
	super_descr_get,			/* tp_descr_get */
	0,					/* tp_descr_set */
	0,					/* tp_dictoffset */
	super_init,				/* tp_init */
	PyType_GenericAlloc,			/* tp_alloc */
	PyType_GenericNew,			/* tp_new */
	PyObject_GC_Del,        		/* tp_free */
};
