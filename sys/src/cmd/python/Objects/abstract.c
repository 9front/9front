/* Abstract Object Interface (many thanks to Jim Fulton) */

#include "Python.h"
#include <ctype.h>
#include "structmember.h" /* we need the offsetof() macro from there */
#include "longintrepr.h"

#define NEW_STYLE_NUMBER(o) PyType_HasFeature((o)->ob_type, \
				Py_TPFLAGS_CHECKTYPES)


/* Shorthands to return certain errors */

static PyObject *
type_error(const char *msg, PyObject *obj)
{
	PyErr_Format(PyExc_TypeError, msg, obj->ob_type->tp_name);
	return NULL;
}

static PyObject *
null_error(void)
{
	if (!PyErr_Occurred())
		PyErr_SetString(PyExc_SystemError,
				"null argument to internal routine");
	return NULL;
}

/* Operations on any object */

int
PyObject_Cmp(PyObject *o1, PyObject *o2, int *result)
{
	int r;

	if (o1 == NULL || o2 == NULL) {
		null_error();
		return -1;
	}
	r = PyObject_Compare(o1, o2);
	if (PyErr_Occurred())
		return -1;
	*result = r;
	return 0;
}

PyObject *
PyObject_Type(PyObject *o)
{
	PyObject *v;

	if (o == NULL)
		return null_error();
	v = (PyObject *)o->ob_type;
	Py_INCREF(v);
	return v;
}

Py_ssize_t
PyObject_Size(PyObject *o)
{
	PySequenceMethods *m;

	if (o == NULL) {
		null_error();
		return -1;
	}

	m = o->ob_type->tp_as_sequence;
	if (m && m->sq_length)
		return m->sq_length(o);

	return PyMapping_Size(o);
}

#undef PyObject_Length
Py_ssize_t
PyObject_Length(PyObject *o)
{
	return PyObject_Size(o);
}
#define PyObject_Length PyObject_Size

Py_ssize_t
_PyObject_LengthHint(PyObject *o)
{
	Py_ssize_t rv = PyObject_Size(o);
	if (rv != -1)
		return rv;
	if (PyErr_ExceptionMatches(PyExc_TypeError) ||
	    PyErr_ExceptionMatches(PyExc_AttributeError)) {
		PyObject *err_type, *err_value, *err_tb, *ro;

		PyErr_Fetch(&err_type, &err_value, &err_tb);
		ro = PyObject_CallMethod(o, "__length_hint__", NULL);
		if (ro != NULL) {
			rv = PyInt_AsLong(ro);
			Py_DECREF(ro);
			Py_XDECREF(err_type);
			Py_XDECREF(err_value);
			Py_XDECREF(err_tb);
			return rv;
		}
		PyErr_Restore(err_type, err_value, err_tb);
	}
	return -1;
}

PyObject *
PyObject_GetItem(PyObject *o, PyObject *key)
{
	PyMappingMethods *m;

	if (o == NULL || key == NULL)
		return null_error();

	m = o->ob_type->tp_as_mapping;
	if (m && m->mp_subscript)
		return m->mp_subscript(o, key);

	if (o->ob_type->tp_as_sequence) {
		if (PyIndex_Check(key)) {
			Py_ssize_t key_value;
			key_value = PyNumber_AsSsize_t(key, PyExc_IndexError);
			if (key_value == -1 && PyErr_Occurred())
				return NULL;
			return PySequence_GetItem(o, key_value);
		}
		else if (o->ob_type->tp_as_sequence->sq_item)
			return type_error("sequence index must "
					  "be integer, not '%.200s'", key);
	}

	return type_error("'%.200s' object is unsubscriptable", o);
}

int
PyObject_SetItem(PyObject *o, PyObject *key, PyObject *value)
{
	PyMappingMethods *m;

	if (o == NULL || key == NULL || value == NULL) {
		null_error();
		return -1;
	}
	m = o->ob_type->tp_as_mapping;
	if (m && m->mp_ass_subscript)
		return m->mp_ass_subscript(o, key, value);

	if (o->ob_type->tp_as_sequence) {
		if (PyIndex_Check(key)) {
			Py_ssize_t key_value;
			key_value = PyNumber_AsSsize_t(key, PyExc_IndexError);
			if (key_value == -1 && PyErr_Occurred())
				return -1;
			return PySequence_SetItem(o, key_value, value);
		}
		else if (o->ob_type->tp_as_sequence->sq_ass_item) {
			type_error("sequence index must be "
				   "integer, not '%.200s'", key);
			return -1;
		}
	}

	type_error("'%.200s' object does not support item assignment", o);
	return -1;
}

int
PyObject_DelItem(PyObject *o, PyObject *key)
{
	PyMappingMethods *m;

	if (o == NULL || key == NULL) {
		null_error();
		return -1;
	}
	m = o->ob_type->tp_as_mapping;
	if (m && m->mp_ass_subscript)
		return m->mp_ass_subscript(o, key, (PyObject*)NULL);

	if (o->ob_type->tp_as_sequence) {
		if (PyIndex_Check(key)) {
			Py_ssize_t key_value;
			key_value = PyNumber_AsSsize_t(key, PyExc_IndexError);
			if (key_value == -1 && PyErr_Occurred())
				return -1;
			return PySequence_DelItem(o, key_value);
		}
		else if (o->ob_type->tp_as_sequence->sq_ass_item) {
			type_error("sequence index must be "
				   "integer, not '%.200s'", key);
			return -1;
		}
	}

	type_error("'%.200s' object does not support item deletion", o);
	return -1;
}

int
PyObject_DelItemString(PyObject *o, char *key)
{
	PyObject *okey;
	int ret;

	if (o == NULL || key == NULL) {
		null_error();
		return -1;
	}
	okey = PyString_FromString(key);
	if (okey == NULL)
		return -1;
	ret = PyObject_DelItem(o, okey);
	Py_DECREF(okey);
	return ret;
}

int
PyObject_AsCharBuffer(PyObject *obj,
			  const char **buffer,
			  Py_ssize_t *buffer_len)
{
	PyBufferProcs *pb;
	char *pp;
	Py_ssize_t len;

	if (obj == NULL || buffer == NULL || buffer_len == NULL) {
		null_error();
		return -1;
	}
	pb = obj->ob_type->tp_as_buffer;
	if (pb == NULL ||
	     pb->bf_getcharbuffer == NULL ||
	     pb->bf_getsegcount == NULL) {
		PyErr_SetString(PyExc_TypeError,
				"expected a character buffer object");
		return -1;
	}
	if ((*pb->bf_getsegcount)(obj,NULL) != 1) {
		PyErr_SetString(PyExc_TypeError,
				"expected a single-segment buffer object");
		return -1;
	}
	len = (*pb->bf_getcharbuffer)(obj, 0, &pp);
	if (len < 0)
		return -1;
	*buffer = pp;
	*buffer_len = len;
	return 0;
}

int
PyObject_CheckReadBuffer(PyObject *obj)
{
	PyBufferProcs *pb = obj->ob_type->tp_as_buffer;

	if (pb == NULL ||
	    pb->bf_getreadbuffer == NULL ||
	    pb->bf_getsegcount == NULL ||
	    (*pb->bf_getsegcount)(obj, NULL) != 1)
		return 0;
	return 1;
}

int PyObject_AsReadBuffer(PyObject *obj,
			  const void **buffer,
			  Py_ssize_t *buffer_len)
{
	PyBufferProcs *pb;
	void *pp;
	Py_ssize_t len;

	if (obj == NULL || buffer == NULL || buffer_len == NULL) {
		null_error();
		return -1;
	}
	pb = obj->ob_type->tp_as_buffer;
	if (pb == NULL ||
	     pb->bf_getreadbuffer == NULL ||
	     pb->bf_getsegcount == NULL) {
		PyErr_SetString(PyExc_TypeError,
				"expected a readable buffer object");
		return -1;
	}
	if ((*pb->bf_getsegcount)(obj, NULL) != 1) {
		PyErr_SetString(PyExc_TypeError,
				"expected a single-segment buffer object");
		return -1;
	}
	len = (*pb->bf_getreadbuffer)(obj, 0, &pp);
	if (len < 0)
		return -1;
	*buffer = pp;
	*buffer_len = len;
	return 0;
}

int PyObject_AsWriteBuffer(PyObject *obj,
			   void **buffer,
			   Py_ssize_t *buffer_len)
{
	PyBufferProcs *pb;
	void*pp;
	Py_ssize_t len;

	if (obj == NULL || buffer == NULL || buffer_len == NULL) {
		null_error();
		return -1;
	}
	pb = obj->ob_type->tp_as_buffer;
	if (pb == NULL ||
	     pb->bf_getwritebuffer == NULL ||
	     pb->bf_getsegcount == NULL) {
		PyErr_SetString(PyExc_TypeError,
				"expected a writeable buffer object");
		return -1;
	}
	if ((*pb->bf_getsegcount)(obj, NULL) != 1) {
		PyErr_SetString(PyExc_TypeError,
				"expected a single-segment buffer object");
		return -1;
	}
	len = (*pb->bf_getwritebuffer)(obj,0,&pp);
	if (len < 0)
		return -1;
	*buffer = pp;
	*buffer_len = len;
	return 0;
}

/* Operations on numbers */

int
PyNumber_Check(PyObject *o)
{
	return o && o->ob_type->tp_as_number &&
	       (o->ob_type->tp_as_number->nb_int ||
		o->ob_type->tp_as_number->nb_float);
}

/* Binary operators */

/* New style number protocol support */

#define NB_SLOT(x) offsetof(PyNumberMethods, x)
#define NB_BINOP(nb_methods, slot) \
		(*(binaryfunc*)(& ((char*)nb_methods)[slot]))
#define NB_TERNOP(nb_methods, slot) \
		(*(ternaryfunc*)(& ((char*)nb_methods)[slot]))

/*
  Calling scheme used for binary operations:

  v	w	Action
  -------------------------------------------------------------------
  new	new	w.op(v,w)[*], v.op(v,w), w.op(v,w)
  new	old	v.op(v,w), coerce(v,w), v.op(v,w)
  old	new	w.op(v,w), coerce(v,w), v.op(v,w)
  old	old	coerce(v,w), v.op(v,w)

  [*] only when v->ob_type != w->ob_type && w->ob_type is a subclass of
      v->ob_type

  Legend:
  -------
  * new == new style number
  * old == old style number
  * Action indicates the order in which operations are tried until either
    a valid result is produced or an error occurs.

 */

static PyObject *
binary_op1(PyObject *v, PyObject *w, const int op_slot)
{
	PyObject *x;
	binaryfunc slotv = NULL;
	binaryfunc slotw = NULL;

	if (v->ob_type->tp_as_number != NULL && NEW_STYLE_NUMBER(v))
		slotv = NB_BINOP(v->ob_type->tp_as_number, op_slot);
	if (w->ob_type != v->ob_type &&
	    w->ob_type->tp_as_number != NULL && NEW_STYLE_NUMBER(w)) {
		slotw = NB_BINOP(w->ob_type->tp_as_number, op_slot);
		if (slotw == slotv)
			slotw = NULL;
	}
	if (slotv) {
		if (slotw && PyType_IsSubtype(w->ob_type, v->ob_type)) {
			x = slotw(v, w);
			if (x != Py_NotImplemented)
				return x;
			Py_DECREF(x); /* can't do it */
			slotw = NULL;
		}
		x = slotv(v, w);
		if (x != Py_NotImplemented)
			return x;
		Py_DECREF(x); /* can't do it */
	}
	if (slotw) {
		x = slotw(v, w);
		if (x != Py_NotImplemented)
			return x;
		Py_DECREF(x); /* can't do it */
	}
	if (!NEW_STYLE_NUMBER(v) || !NEW_STYLE_NUMBER(w)) {
		int err = PyNumber_CoerceEx(&v, &w);
		if (err < 0) {
			return NULL;
		}
		if (err == 0) {
			PyNumberMethods *mv = v->ob_type->tp_as_number;
			if (mv) {
				binaryfunc slot;
				slot = NB_BINOP(mv, op_slot);
				if (slot) {
					x = slot(v, w);
					Py_DECREF(v);
					Py_DECREF(w);
					return x;
				}
			}
			/* CoerceEx incremented the reference counts */
			Py_DECREF(v);
			Py_DECREF(w);
		}
	}
	Py_INCREF(Py_NotImplemented);
	return Py_NotImplemented;
}

static PyObject *
binop_type_error(PyObject *v, PyObject *w, const char *op_name)
{
	PyErr_Format(PyExc_TypeError,
		     "unsupported operand type(s) for %.100s: "
		     "'%.100s' and '%.100s'",
		     op_name,
		     v->ob_type->tp_name,
		     w->ob_type->tp_name);
	return NULL;
}

static PyObject *
binary_op(PyObject *v, PyObject *w, const int op_slot, const char *op_name)
{
	PyObject *result = binary_op1(v, w, op_slot);
	if (result == Py_NotImplemented) {
		Py_DECREF(result);
		return binop_type_error(v, w, op_name);
	}
	return result;
}


/*
  Calling scheme used for ternary operations:

  *** In some cases, w.op is called before v.op; see binary_op1. ***

  v	w	z	Action
  -------------------------------------------------------------------
  new	new	new	v.op(v,w,z), w.op(v,w,z), z.op(v,w,z)
  new	old	new	v.op(v,w,z), z.op(v,w,z), coerce(v,w,z), v.op(v,w,z)
  old	new	new	w.op(v,w,z), z.op(v,w,z), coerce(v,w,z), v.op(v,w,z)
  old	old	new	z.op(v,w,z), coerce(v,w,z), v.op(v,w,z)
  new	new	old	v.op(v,w,z), w.op(v,w,z), coerce(v,w,z), v.op(v,w,z)
  new	old	old	v.op(v,w,z), coerce(v,w,z), v.op(v,w,z)
  old	new	old	w.op(v,w,z), coerce(v,w,z), v.op(v,w,z)
  old	old	old	coerce(v,w,z), v.op(v,w,z)

  Legend:
  -------
  * new == new style number
  * old == old style number
  * Action indicates the order in which operations are tried until either
    a valid result is produced or an error occurs.
  * coerce(v,w,z) actually does: coerce(v,w), coerce(v,z), coerce(w,z) and
    only if z != Py_None; if z == Py_None, then it is treated as absent
    variable and only coerce(v,w) is tried.

 */

static PyObject *
ternary_op(PyObject *v,
	   PyObject *w,
	   PyObject *z,
	   const int op_slot,
	   const char *op_name)
{
	PyNumberMethods *mv, *mw, *mz;
	PyObject *x = NULL;
	ternaryfunc slotv = NULL;
	ternaryfunc slotw = NULL;
	ternaryfunc slotz = NULL;

	mv = v->ob_type->tp_as_number;
	mw = w->ob_type->tp_as_number;
	if (mv != NULL && NEW_STYLE_NUMBER(v))
		slotv = NB_TERNOP(mv, op_slot);
	if (w->ob_type != v->ob_type &&
	    mw != NULL && NEW_STYLE_NUMBER(w)) {
		slotw = NB_TERNOP(mw, op_slot);
		if (slotw == slotv)
			slotw = NULL;
	}
	if (slotv) {
		if (slotw && PyType_IsSubtype(w->ob_type, v->ob_type)) {
			x = slotw(v, w, z);
			if (x != Py_NotImplemented)
				return x;
			Py_DECREF(x); /* can't do it */
			slotw = NULL;
		}
		x = slotv(v, w, z);
		if (x != Py_NotImplemented)
			return x;
		Py_DECREF(x); /* can't do it */
	}
	if (slotw) {
		x = slotw(v, w, z);
		if (x != Py_NotImplemented)
			return x;
		Py_DECREF(x); /* can't do it */
	}
	mz = z->ob_type->tp_as_number;
	if (mz != NULL && NEW_STYLE_NUMBER(z)) {
		slotz = NB_TERNOP(mz, op_slot);
		if (slotz == slotv || slotz == slotw)
			slotz = NULL;
		if (slotz) {
			x = slotz(v, w, z);
			if (x != Py_NotImplemented)
				return x;
			Py_DECREF(x); /* can't do it */
		}
	}

	if (!NEW_STYLE_NUMBER(v) || !NEW_STYLE_NUMBER(w) ||
			(z != Py_None && !NEW_STYLE_NUMBER(z))) {
		/* we have an old style operand, coerce */
		PyObject *v1, *z1, *w2, *z2;
		int c;

		c = PyNumber_Coerce(&v, &w);
		if (c != 0)
			goto error3;

		/* Special case: if the third argument is None, it is
		   treated as absent argument and not coerced. */
		if (z == Py_None) {
			if (v->ob_type->tp_as_number) {
				slotz = NB_TERNOP(v->ob_type->tp_as_number,
						  op_slot);
				if (slotz)
					x = slotz(v, w, z);
				else
					c = -1;
			}
			else
				c = -1;
			goto error2;
		}
		v1 = v;
		z1 = z;
		c = PyNumber_Coerce(&v1, &z1);
		if (c != 0)
			goto error2;
		w2 = w;
		z2 = z1;
		c = PyNumber_Coerce(&w2, &z2);
		if (c != 0)
			goto error1;

		if (v1->ob_type->tp_as_number != NULL) {
			slotv = NB_TERNOP(v1->ob_type->tp_as_number,
					  op_slot);
			if (slotv)
				x = slotv(v1, w2, z2);
			else
				c = -1;
		}
		else
			c = -1;

		Py_DECREF(w2);
		Py_DECREF(z2);
	error1:
		Py_DECREF(v1);
		Py_DECREF(z1);
	error2:
		Py_DECREF(v);
		Py_DECREF(w);
	error3:
		if (c >= 0)
			return x;
	}

	if (z == Py_None)
		PyErr_Format(
			PyExc_TypeError,
			"unsupported operand type(s) for ** or pow(): "
			"'%.100s' and '%.100s'",
			v->ob_type->tp_name,
			w->ob_type->tp_name);
	else
		PyErr_Format(
			PyExc_TypeError,
			"unsupported operand type(s) for pow(): "
			"'%.100s', '%.100s', '%.100s'",
			v->ob_type->tp_name,
			w->ob_type->tp_name,
			z->ob_type->tp_name);
	return NULL;
}

#define BINARY_FUNC(func, op, op_name) \
    PyObject * \
    func(PyObject *v, PyObject *w) { \
	    return binary_op(v, w, NB_SLOT(op), op_name); \
    }

BINARY_FUNC(PyNumber_Or, nb_or, "|")
BINARY_FUNC(PyNumber_Xor, nb_xor, "^")
BINARY_FUNC(PyNumber_And, nb_and, "&")
BINARY_FUNC(PyNumber_Lshift, nb_lshift, "<<")
BINARY_FUNC(PyNumber_Rshift, nb_rshift, ">>")
BINARY_FUNC(PyNumber_Subtract, nb_subtract, "-")
BINARY_FUNC(PyNumber_Divide, nb_divide, "/")
BINARY_FUNC(PyNumber_Divmod, nb_divmod, "divmod()")

PyObject *
PyNumber_Add(PyObject *v, PyObject *w)
{
	PyObject *result = binary_op1(v, w, NB_SLOT(nb_add));
	if (result == Py_NotImplemented) {
		PySequenceMethods *m = v->ob_type->tp_as_sequence;
		Py_DECREF(result);
		if (m && m->sq_concat) {
			return (*m->sq_concat)(v, w);
		}
		result = binop_type_error(v, w, "+");
	}
	return result;
}

static PyObject *
sequence_repeat(ssizeargfunc repeatfunc, PyObject *seq, PyObject *n)
{
	Py_ssize_t count;
	if (PyIndex_Check(n)) {
		count = PyNumber_AsSsize_t(n, PyExc_OverflowError);
		if (count == -1 && PyErr_Occurred())
			return NULL;
	}
	else {
		return type_error("can't multiply sequence by "
				  "non-int of type '%.200s'", n);
	}
	return (*repeatfunc)(seq, count);
}

PyObject *
PyNumber_Multiply(PyObject *v, PyObject *w)
{
	PyObject *result = binary_op1(v, w, NB_SLOT(nb_multiply));
	if (result == Py_NotImplemented) {
		PySequenceMethods *mv = v->ob_type->tp_as_sequence;
		PySequenceMethods *mw = w->ob_type->tp_as_sequence;
		Py_DECREF(result);
		if  (mv && mv->sq_repeat) {
			return sequence_repeat(mv->sq_repeat, v, w);
		}
		else if (mw && mw->sq_repeat) {
			return sequence_repeat(mw->sq_repeat, w, v);
		}
		result = binop_type_error(v, w, "*");
	}
	return result;
}

PyObject *
PyNumber_FloorDivide(PyObject *v, PyObject *w)
{
	/* XXX tp_flags test */
	return binary_op(v, w, NB_SLOT(nb_floor_divide), "//");
}

PyObject *
PyNumber_TrueDivide(PyObject *v, PyObject *w)
{
	/* XXX tp_flags test */
	return binary_op(v, w, NB_SLOT(nb_true_divide), "/");
}

PyObject *
PyNumber_Remainder(PyObject *v, PyObject *w)
{
	return binary_op(v, w, NB_SLOT(nb_remainder), "%");
}

PyObject *
PyNumber_Power(PyObject *v, PyObject *w, PyObject *z)
{
	return ternary_op(v, w, z, NB_SLOT(nb_power), "** or pow()");
}

/* Binary in-place operators */

/* The in-place operators are defined to fall back to the 'normal',
   non in-place operations, if the in-place methods are not in place.

   - If the left hand object has the appropriate struct members, and
     they are filled, call the appropriate function and return the
     result.  No coercion is done on the arguments; the left-hand object
     is the one the operation is performed on, and it's up to the
     function to deal with the right-hand object.

   - Otherwise, in-place modification is not supported. Handle it exactly as
     a non in-place operation of the same kind.

   */

#define HASINPLACE(t) \
	PyType_HasFeature((t)->ob_type, Py_TPFLAGS_HAVE_INPLACEOPS)

static PyObject *
binary_iop1(PyObject *v, PyObject *w, const int iop_slot, const int op_slot)
{
	PyNumberMethods *mv = v->ob_type->tp_as_number;
	if (mv != NULL && HASINPLACE(v)) {
		binaryfunc slot = NB_BINOP(mv, iop_slot);
		if (slot) {
			PyObject *x = (slot)(v, w);
			if (x != Py_NotImplemented) {
				return x;
			}
			Py_DECREF(x);
		}
	}
	return binary_op1(v, w, op_slot);
}

static PyObject *
binary_iop(PyObject *v, PyObject *w, const int iop_slot, const int op_slot,
		const char *op_name)
{
	PyObject *result = binary_iop1(v, w, iop_slot, op_slot);
	if (result == Py_NotImplemented) {
		Py_DECREF(result);
		return binop_type_error(v, w, op_name);
	}
	return result;
}

#define INPLACE_BINOP(func, iop, op, op_name) \
	PyObject * \
	func(PyObject *v, PyObject *w) { \
		return binary_iop(v, w, NB_SLOT(iop), NB_SLOT(op), op_name); \
	}

INPLACE_BINOP(PyNumber_InPlaceOr, nb_inplace_or, nb_or, "|=")
INPLACE_BINOP(PyNumber_InPlaceXor, nb_inplace_xor, nb_xor, "^=")
INPLACE_BINOP(PyNumber_InPlaceAnd, nb_inplace_and, nb_and, "&=")
INPLACE_BINOP(PyNumber_InPlaceLshift, nb_inplace_lshift, nb_lshift, "<<=")
INPLACE_BINOP(PyNumber_InPlaceRshift, nb_inplace_rshift, nb_rshift, ">>=")
INPLACE_BINOP(PyNumber_InPlaceSubtract, nb_inplace_subtract, nb_subtract, "-=")
INPLACE_BINOP(PyNumber_InPlaceDivide, nb_inplace_divide, nb_divide, "/=")

PyObject *
PyNumber_InPlaceFloorDivide(PyObject *v, PyObject *w)
{
	/* XXX tp_flags test */
	return binary_iop(v, w, NB_SLOT(nb_inplace_floor_divide),
			  NB_SLOT(nb_floor_divide), "//=");
}

PyObject *
PyNumber_InPlaceTrueDivide(PyObject *v, PyObject *w)
{
	/* XXX tp_flags test */
	return binary_iop(v, w, NB_SLOT(nb_inplace_true_divide),
			  NB_SLOT(nb_true_divide), "/=");
}

PyObject *
PyNumber_InPlaceAdd(PyObject *v, PyObject *w)
{
	PyObject *result = binary_iop1(v, w, NB_SLOT(nb_inplace_add),
				       NB_SLOT(nb_add));
	if (result == Py_NotImplemented) {
		PySequenceMethods *m = v->ob_type->tp_as_sequence;
		Py_DECREF(result);
		if (m != NULL) {
			binaryfunc f = NULL;
			if (HASINPLACE(v))
				f = m->sq_inplace_concat;
			if (f == NULL)
				f = m->sq_concat;
			if (f != NULL)
				return (*f)(v, w);
		}
		result = binop_type_error(v, w, "+=");
	}
	return result;
}

PyObject *
PyNumber_InPlaceMultiply(PyObject *v, PyObject *w)
{
	PyObject *result = binary_iop1(v, w, NB_SLOT(nb_inplace_multiply),
				       NB_SLOT(nb_multiply));
	if (result == Py_NotImplemented) {
		ssizeargfunc f = NULL;
		PySequenceMethods *mv = v->ob_type->tp_as_sequence;
		PySequenceMethods *mw = w->ob_type->tp_as_sequence;
		Py_DECREF(result);
		if (mv != NULL) {
			if (HASINPLACE(v))
				f = mv->sq_inplace_repeat;
			if (f == NULL)
				f = mv->sq_repeat;
			if (f != NULL)
				return sequence_repeat(f, v, w);
		}
		else if (mw != NULL) {
			/* Note that the right hand operand should not be
			 * mutated in this case so sq_inplace_repeat is not
			 * used. */
			if (mw->sq_repeat)
				return sequence_repeat(mw->sq_repeat, w, v);
		}
		result = binop_type_error(v, w, "*=");
	}
	return result;
}

PyObject *
PyNumber_InPlaceRemainder(PyObject *v, PyObject *w)
{
	return binary_iop(v, w, NB_SLOT(nb_inplace_remainder),
				NB_SLOT(nb_remainder), "%=");
}

PyObject *
PyNumber_InPlacePower(PyObject *v, PyObject *w, PyObject *z)
{
	if (HASINPLACE(v) && v->ob_type->tp_as_number &&
	    v->ob_type->tp_as_number->nb_inplace_power != NULL) {
		return ternary_op(v, w, z, NB_SLOT(nb_inplace_power), "**=");
	}
	else {
		return ternary_op(v, w, z, NB_SLOT(nb_power), "**=");
	}
}


/* Unary operators and functions */

PyObject *
PyNumber_Negative(PyObject *o)
{
	PyNumberMethods *m;

	if (o == NULL)
		return null_error();
	m = o->ob_type->tp_as_number;
	if (m && m->nb_negative)
		return (*m->nb_negative)(o);

	return type_error("bad operand type for unary -: '%.200s'", o);
}

PyObject *
PyNumber_Positive(PyObject *o)
{
	PyNumberMethods *m;

	if (o == NULL)
		return null_error();
	m = o->ob_type->tp_as_number;
	if (m && m->nb_positive)
		return (*m->nb_positive)(o);

	return type_error("bad operand type for unary +: '%.200s'", o);
}

PyObject *
PyNumber_Invert(PyObject *o)
{
	PyNumberMethods *m;

	if (o == NULL)
		return null_error();
	m = o->ob_type->tp_as_number;
	if (m && m->nb_invert)
		return (*m->nb_invert)(o);

	return type_error("bad operand type for unary ~: '%.200s'", o);
}

PyObject *
PyNumber_Absolute(PyObject *o)
{
	PyNumberMethods *m;

	if (o == NULL)
		return null_error();
	m = o->ob_type->tp_as_number;
	if (m && m->nb_absolute)
		return m->nb_absolute(o);

	return type_error("bad operand type for abs(): '%.200s'", o);
}

/* Add a check for embedded NULL-bytes in the argument. */
static PyObject *
int_from_string(const char *s, Py_ssize_t len)
{
	char *end;
	PyObject *x;

	x = PyInt_FromString((char*)s, &end, 10);
	if (x == NULL)
		return NULL;
	if (end != s + len) {
		PyErr_SetString(PyExc_ValueError,
				"null byte in argument for int()");
		Py_DECREF(x);
		return NULL;
	}
	return x;
}

/* Return a Python Int or Long from the object item 
   Raise TypeError if the result is not an int-or-long
   or if the object cannot be interpreted as an index. 
*/
PyObject *
PyNumber_Index(PyObject *item)
{
	PyObject *result = NULL;
	if (item == NULL)
		return null_error();
	if (PyInt_Check(item) || PyLong_Check(item)) {
		Py_INCREF(item);
		return item;
	}
	if (PyIndex_Check(item)) {
		result = item->ob_type->tp_as_number->nb_index(item);
		if (result &&
		    !PyInt_Check(result) && !PyLong_Check(result)) {
			PyErr_Format(PyExc_TypeError,
				     "__index__ returned non-(int,long) " \
				     "(type %.200s)",
				     result->ob_type->tp_name);
			Py_DECREF(result);
			return NULL;
		}
	}
	else {
		PyErr_Format(PyExc_TypeError,
			     "'%.200s' object cannot be interpreted "
			     "as an index", item->ob_type->tp_name);
	}
	return result;
}

/* Return an error on Overflow only if err is not NULL*/

Py_ssize_t
PyNumber_AsSsize_t(PyObject *item, PyObject *err)
{
	Py_ssize_t result;
	PyObject *runerr;
	PyObject *value = PyNumber_Index(item);
	if (value == NULL)
		return -1;

	/* We're done if PyInt_AsSsize_t() returns without error. */
	result = PyInt_AsSsize_t(value);
	if (result != -1 || !(runerr = PyErr_Occurred()))
		goto finish;

	/* Error handling code -- only manage OverflowError differently */
	if (!PyErr_GivenExceptionMatches(runerr, PyExc_OverflowError)) 
		goto finish;

	PyErr_Clear();
	/* If no error-handling desired then the default clipping 
	   is sufficient.
	 */
	if (!err) {
		assert(PyLong_Check(value));
		/* Whether or not it is less than or equal to 
		   zero is determined by the sign of ob_size
		*/
		if (_PyLong_Sign(value) < 0) 
			result = PY_SSIZE_T_MIN;
		else
			result = PY_SSIZE_T_MAX;
	}
	else {
		/* Otherwise replace the error with caller's error object. */
		PyErr_Format(err,
			     "cannot fit '%.200s' into an index-sized integer", 
			     item->ob_type->tp_name); 
	}
	
 finish:
	Py_DECREF(value);
	return result;
}


PyObject *
PyNumber_Int(PyObject *o)
{
	PyNumberMethods *m;
	const char *buffer;
	Py_ssize_t buffer_len;

	if (o == NULL)
		return null_error();
	if (PyInt_CheckExact(o)) {
		Py_INCREF(o);
		return o;
	}
	m = o->ob_type->tp_as_number;
	if (m && m->nb_int) { /* This should include subclasses of int */
		PyObject *res = m->nb_int(o);
		if (res && (!PyInt_Check(res) && !PyLong_Check(res))) {
			PyErr_Format(PyExc_TypeError,
				     "__int__ returned non-int (type %.200s)",
				     res->ob_type->tp_name);
			Py_DECREF(res);
			return NULL;
		}
		return res;
	}
	if (PyInt_Check(o)) { /* A int subclass without nb_int */
		PyIntObject *io = (PyIntObject*)o;
		return PyInt_FromLong(io->ob_ival);
	}
	if (PyString_Check(o))
		return int_from_string(PyString_AS_STRING(o),
				       PyString_GET_SIZE(o));
#ifdef Py_USING_UNICODE
	if (PyUnicode_Check(o))
		return PyInt_FromUnicode(PyUnicode_AS_UNICODE(o),
					 PyUnicode_GET_SIZE(o),
					 10);
#endif
	if (!PyObject_AsCharBuffer(o, &buffer, &buffer_len))
		return int_from_string((char*)buffer, buffer_len);

	return type_error("int() argument must be a string or a "
			  "number, not '%.200s'", o);
}

/* Add a check for embedded NULL-bytes in the argument. */
static PyObject *
long_from_string(const char *s, Py_ssize_t len)
{
	char *end;
	PyObject *x;

	x = PyLong_FromString((char*)s, &end, 10);
	if (x == NULL)
		return NULL;
	if (end != s + len) {
		PyErr_SetString(PyExc_ValueError,
				"null byte in argument for long()");
		Py_DECREF(x);
		return NULL;
	}
	return x;
}

PyObject *
PyNumber_Long(PyObject *o)
{
	PyNumberMethods *m;
	const char *buffer;
	Py_ssize_t buffer_len;

	if (o == NULL)
		return null_error();
	m = o->ob_type->tp_as_number;
	if (m && m->nb_long) { /* This should include subclasses of long */
		PyObject *res = m->nb_long(o);
		if (res && (!PyInt_Check(res) && !PyLong_Check(res))) {
			PyErr_Format(PyExc_TypeError,
				     "__long__ returned non-long (type %.200s)",
				     res->ob_type->tp_name);
			Py_DECREF(res);
			return NULL;
		}
		return res;
	}
	if (PyLong_Check(o)) /* A long subclass without nb_long */
		return _PyLong_Copy((PyLongObject *)o);
	if (PyString_Check(o))
		/* need to do extra error checking that PyLong_FromString()
		 * doesn't do.  In particular long('9.5') must raise an
		 * exception, not truncate the float.
		 */
		return long_from_string(PyString_AS_STRING(o),
					PyString_GET_SIZE(o));
#ifdef Py_USING_UNICODE
	if (PyUnicode_Check(o))
		/* The above check is done in PyLong_FromUnicode(). */
		return PyLong_FromUnicode(PyUnicode_AS_UNICODE(o),
					  PyUnicode_GET_SIZE(o),
					  10);
#endif
	if (!PyObject_AsCharBuffer(o, &buffer, &buffer_len))
		return long_from_string(buffer, buffer_len);

	return type_error("long() argument must be a string or a "
			  "number, not '%.200s'", o);
}

PyObject *
PyNumber_Float(PyObject *o)
{
	PyNumberMethods *m;

	if (o == NULL)
		return null_error();
	m = o->ob_type->tp_as_number;
	if (m && m->nb_float) { /* This should include subclasses of float */
		PyObject *res = m->nb_float(o);
		if (res && !PyFloat_Check(res)) {
			PyErr_Format(PyExc_TypeError,
		          "__float__ returned non-float (type %.200s)",
		          res->ob_type->tp_name);
			Py_DECREF(res);
			return NULL;
		}
		return res;
	}
	if (PyFloat_Check(o)) { /* A float subclass with nb_float == NULL */
		PyFloatObject *po = (PyFloatObject *)o;
		return PyFloat_FromDouble(po->ob_fval);
	}
	return PyFloat_FromString(o, NULL);
}

/* Operations on sequences */

int
PySequence_Check(PyObject *s)
{
	if (s && PyInstance_Check(s))
		return PyObject_HasAttrString(s, "__getitem__");
	if (PyObject_IsInstance(s, (PyObject *)&PyDict_Type))
		return 0;
	return s != NULL && s->ob_type->tp_as_sequence &&
		s->ob_type->tp_as_sequence->sq_item != NULL;
}

Py_ssize_t
PySequence_Size(PyObject *s)
{
	PySequenceMethods *m;

	if (s == NULL) {
		null_error();
		return -1;
	}

	m = s->ob_type->tp_as_sequence;
	if (m && m->sq_length)
		return m->sq_length(s);

	type_error("object of type '%.200s' has no len()", s);
	return -1;
}

#undef PySequence_Length
Py_ssize_t
PySequence_Length(PyObject *s)
{
	return PySequence_Size(s);
}
#define PySequence_Length PySequence_Size

PyObject *
PySequence_Concat(PyObject *s, PyObject *o)
{
	PySequenceMethods *m;

	if (s == NULL || o == NULL)
		return null_error();

	m = s->ob_type->tp_as_sequence;
	if (m && m->sq_concat)
		return m->sq_concat(s, o);

	/* Instances of user classes defining an __add__() method only
	   have an nb_add slot, not an sq_concat slot.  So we fall back
	   to nb_add if both arguments appear to be sequences. */
	if (PySequence_Check(s) && PySequence_Check(o)) {
		PyObject *result = binary_op1(s, o, NB_SLOT(nb_add));
		if (result != Py_NotImplemented)
			return result;
		Py_DECREF(result);
	}
	return type_error("'%.200s' object can't be concatenated", s);
}

PyObject *
PySequence_Repeat(PyObject *o, Py_ssize_t count)
{
	PySequenceMethods *m;

	if (o == NULL)
		return null_error();

	m = o->ob_type->tp_as_sequence;
	if (m && m->sq_repeat)
		return m->sq_repeat(o, count);

	/* Instances of user classes defining a __mul__() method only
	   have an nb_multiply slot, not an sq_repeat slot. so we fall back
	   to nb_multiply if o appears to be a sequence. */
	if (PySequence_Check(o)) {
		PyObject *n, *result;
		n = PyInt_FromSsize_t(count);
		if (n == NULL)
			return NULL;
		result = binary_op1(o, n, NB_SLOT(nb_multiply));
		Py_DECREF(n);
		if (result != Py_NotImplemented)
			return result;
		Py_DECREF(result);
	}
	return type_error("'%.200s' object can't be repeated", o);
}

PyObject *
PySequence_InPlaceConcat(PyObject *s, PyObject *o)
{
	PySequenceMethods *m;

	if (s == NULL || o == NULL)
		return null_error();

	m = s->ob_type->tp_as_sequence;
	if (m && HASINPLACE(s) && m->sq_inplace_concat)
		return m->sq_inplace_concat(s, o);
	if (m && m->sq_concat)
		return m->sq_concat(s, o);

	if (PySequence_Check(s) && PySequence_Check(o)) {
		PyObject *result = binary_iop1(s, o, NB_SLOT(nb_inplace_add),
					       NB_SLOT(nb_add));
		if (result != Py_NotImplemented)
			return result;
		Py_DECREF(result);
	}
	return type_error("'%.200s' object can't be concatenated", s);
}

PyObject *
PySequence_InPlaceRepeat(PyObject *o, Py_ssize_t count)
{
	PySequenceMethods *m;

	if (o == NULL)
		return null_error();

	m = o->ob_type->tp_as_sequence;
	if (m && HASINPLACE(o) && m->sq_inplace_repeat)
		return m->sq_inplace_repeat(o, count);
	if (m && m->sq_repeat)
		return m->sq_repeat(o, count);

	if (PySequence_Check(o)) {
		PyObject *n, *result;
		n = PyInt_FromSsize_t(count);
		if (n == NULL)
			return NULL;
		result = binary_iop1(o, n, NB_SLOT(nb_inplace_multiply),
				     NB_SLOT(nb_multiply));
		Py_DECREF(n);
		if (result != Py_NotImplemented)
			return result;
		Py_DECREF(result);
	}
	return type_error("'%.200s' object can't be repeated", o);
}

PyObject *
PySequence_GetItem(PyObject *s, Py_ssize_t i)
{
	PySequenceMethods *m;

	if (s == NULL)
		return null_error();

	m = s->ob_type->tp_as_sequence;
	if (m && m->sq_item) {
		if (i < 0) {
			if (m->sq_length) {
				Py_ssize_t l = (*m->sq_length)(s);
				if (l < 0)
					return NULL;
				i += l;
			}
		}
		return m->sq_item(s, i);
	}

	return type_error("'%.200s' object is unindexable", s);
}

PyObject *
PySequence_GetSlice(PyObject *s, Py_ssize_t i1, Py_ssize_t i2)
{
	PySequenceMethods *m;
	PyMappingMethods *mp;

	if (!s) return null_error();

	m = s->ob_type->tp_as_sequence;
	if (m && m->sq_slice) {
		if (i1 < 0 || i2 < 0) {
			if (m->sq_length) {
				Py_ssize_t l = (*m->sq_length)(s);
				if (l < 0)
					return NULL;
				if (i1 < 0)
					i1 += l;
				if (i2 < 0)
					i2 += l;
			}
		}
		return m->sq_slice(s, i1, i2);
	} else if ((mp = s->ob_type->tp_as_mapping) && mp->mp_subscript) {
		PyObject *res;
		PyObject *slice = _PySlice_FromIndices(i1, i2);
		if (!slice)
			return NULL;
		res = mp->mp_subscript(s, slice);
		Py_DECREF(slice);
		return res;
	}

	return type_error("'%.200s' object is unsliceable", s);
}

int
PySequence_SetItem(PyObject *s, Py_ssize_t i, PyObject *o)
{
	PySequenceMethods *m;

	if (s == NULL) {
		null_error();
		return -1;
	}

	m = s->ob_type->tp_as_sequence;
	if (m && m->sq_ass_item) {
		if (i < 0) {
			if (m->sq_length) {
				Py_ssize_t l = (*m->sq_length)(s);
				if (l < 0)
					return -1;
				i += l;
			}
		}
		return m->sq_ass_item(s, i, o);
	}

	type_error("'%.200s' object does not support item assignment", s);
	return -1;
}

int
PySequence_DelItem(PyObject *s, Py_ssize_t i)
{
	PySequenceMethods *m;

	if (s == NULL) {
		null_error();
		return -1;
	}

	m = s->ob_type->tp_as_sequence;
	if (m && m->sq_ass_item) {
		if (i < 0) {
			if (m->sq_length) {
				Py_ssize_t l = (*m->sq_length)(s);
				if (l < 0)
					return -1;
				i += l;
			}
		}
		return m->sq_ass_item(s, i, (PyObject *)NULL);
	}

	type_error("'%.200s' object doesn't support item deletion", s);
	return -1;
}

int
PySequence_SetSlice(PyObject *s, Py_ssize_t i1, Py_ssize_t i2, PyObject *o)
{
	PySequenceMethods *m;
	PyMappingMethods *mp;

	if (s == NULL) {
		null_error();
		return -1;
	}

	m = s->ob_type->tp_as_sequence;
	if (m && m->sq_ass_slice) {
		if (i1 < 0 || i2 < 0) {
			if (m->sq_length) {
				Py_ssize_t l = (*m->sq_length)(s);
				if (l < 0)
					return -1;
				if (i1 < 0)
					i1 += l;
				if (i2 < 0)
					i2 += l;
			}
		}
		return m->sq_ass_slice(s, i1, i2, o);
	} else if ((mp = s->ob_type->tp_as_mapping) && mp->mp_ass_subscript) {
		int res;
		PyObject *slice = _PySlice_FromIndices(i1, i2);
		if (!slice)
			return -1;
		res = mp->mp_ass_subscript(s, slice, o);
		Py_DECREF(slice);
		return res;
	}

	type_error("'%.200s' object doesn't support slice assignment", s);
	return -1;
}

int
PySequence_DelSlice(PyObject *s, Py_ssize_t i1, Py_ssize_t i2)
{
	PySequenceMethods *m;

	if (s == NULL) {
		null_error();
		return -1;
	}

	m = s->ob_type->tp_as_sequence;
	if (m && m->sq_ass_slice) {
		if (i1 < 0 || i2 < 0) {
			if (m->sq_length) {
				Py_ssize_t l = (*m->sq_length)(s);
				if (l < 0)
					return -1;
				if (i1 < 0)
					i1 += l;
				if (i2 < 0)
					i2 += l;
			}
		}
		return m->sq_ass_slice(s, i1, i2, (PyObject *)NULL);
	}
	type_error("'%.200s' object doesn't support slice deletion", s);
	return -1;
}

PyObject *
PySequence_Tuple(PyObject *v)
{
	PyObject *it;  /* iter(v) */
	Py_ssize_t n;         /* guess for result tuple size */
	PyObject *result;
	Py_ssize_t j;

	if (v == NULL)
		return null_error();

	/* Special-case the common tuple and list cases, for efficiency. */
	if (PyTuple_CheckExact(v)) {
		/* Note that we can't know whether it's safe to return
		   a tuple *subclass* instance as-is, hence the restriction
		   to exact tuples here.  In contrast, lists always make
		   a copy, so there's no need for exactness below. */
		Py_INCREF(v);
		return v;
	}
	if (PyList_Check(v))
		return PyList_AsTuple(v);

	/* Get iterator. */
	it = PyObject_GetIter(v);
	if (it == NULL)
		return NULL;

	/* Guess result size and allocate space. */
	n = _PyObject_LengthHint(v);
	if (n < 0) {
		if (!PyErr_ExceptionMatches(PyExc_TypeError)  &&
		    !PyErr_ExceptionMatches(PyExc_AttributeError)) {
			Py_DECREF(it);
			return NULL;
		}
		PyErr_Clear();
		n = 10;  /* arbitrary */
	}
	result = PyTuple_New(n);
	if (result == NULL)
		goto Fail;

	/* Fill the tuple. */
	for (j = 0; ; ++j) {
		PyObject *item = PyIter_Next(it);
		if (item == NULL) {
			if (PyErr_Occurred())
				goto Fail;
			break;
		}
		if (j >= n) {
			Py_ssize_t oldn = n;
			/* The over-allocation strategy can grow a bit faster
			   than for lists because unlike lists the 
			   over-allocation isn't permanent -- we reclaim
			   the excess before the end of this routine.
			   So, grow by ten and then add 25%.
			*/
			n += 10;
			n += n >> 2;
			if (n < oldn) {
				/* Check for overflow */
				PyErr_NoMemory();
				Py_DECREF(item);
				goto Fail; 
			}
			if (_PyTuple_Resize(&result, n) != 0) {
				Py_DECREF(item);
				goto Fail;
			}
		}
		PyTuple_SET_ITEM(result, j, item);
	}

	/* Cut tuple back if guess was too large. */
	if (j < n &&
	    _PyTuple_Resize(&result, j) != 0)
		goto Fail;

	Py_DECREF(it);
	return result;

Fail:
	Py_XDECREF(result);
	Py_DECREF(it);
	return NULL;
}

PyObject *
PySequence_List(PyObject *v)
{
	PyObject *result;  /* result list */
	PyObject *rv;      /* return value from PyList_Extend */

	if (v == NULL)
		return null_error();

	result = PyList_New(0);
	if (result == NULL)
		return NULL;

	rv = _PyList_Extend((PyListObject *)result, v);
	if (rv == NULL) {
		Py_DECREF(result);
		return NULL;
	}
	Py_DECREF(rv);
	return result;
}

PyObject *
PySequence_Fast(PyObject *v, const char *m)
{
	PyObject *it;

	if (v == NULL)
		return null_error();

	if (PyList_CheckExact(v) || PyTuple_CheckExact(v)) {
		Py_INCREF(v);
		return v;
	}

 	it = PyObject_GetIter(v);
	if (it == NULL) {
		if (PyErr_ExceptionMatches(PyExc_TypeError))
			PyErr_SetString(PyExc_TypeError, m);
		return NULL;
	}

	v = PySequence_List(it);
	Py_DECREF(it);

	return v;
}

/* Iterate over seq.  Result depends on the operation:
   PY_ITERSEARCH_COUNT:  -1 if error, else # of times obj appears in seq.
   PY_ITERSEARCH_INDEX:  0-based index of first occurence of obj in seq;
   	set ValueError and return -1 if none found; also return -1 on error.
   Py_ITERSEARCH_CONTAINS:  return 1 if obj in seq, else 0; -1 on error.
*/
Py_ssize_t
_PySequence_IterSearch(PyObject *seq, PyObject *obj, int operation)
{
	Py_ssize_t n;
	int wrapped;  /* for PY_ITERSEARCH_INDEX, true iff n wrapped around */
	PyObject *it;  /* iter(seq) */

	if (seq == NULL || obj == NULL) {
		null_error();
		return -1;
	}

	it = PyObject_GetIter(seq);
	if (it == NULL) {
		type_error("argument of type '%.200s' is not iterable", seq);
		return -1;
	}

	n = wrapped = 0;
	for (;;) {
		int cmp;
		PyObject *item = PyIter_Next(it);
		if (item == NULL) {
			if (PyErr_Occurred())
				goto Fail;
			break;
		}

		cmp = PyObject_RichCompareBool(obj, item, Py_EQ);
		Py_DECREF(item);
		if (cmp < 0)
			goto Fail;
		if (cmp > 0) {
			switch (operation) {
			case PY_ITERSEARCH_COUNT:
				if (n == PY_SSIZE_T_MAX) {
					PyErr_SetString(PyExc_OverflowError,
					       "count exceeds C integer size");
					goto Fail;
				}
				++n;
				break;

			case PY_ITERSEARCH_INDEX:
				if (wrapped) {
					PyErr_SetString(PyExc_OverflowError,
					       "index exceeds C integer size");
					goto Fail;
				}
				goto Done;

			case PY_ITERSEARCH_CONTAINS:
				n = 1;
				goto Done;

			default:
				assert(!"unknown operation");
			}
		}

		if (operation == PY_ITERSEARCH_INDEX) {
			if (n == PY_SSIZE_T_MAX)
				wrapped = 1;
			++n;
		}
	}

	if (operation != PY_ITERSEARCH_INDEX)
		goto Done;

	PyErr_SetString(PyExc_ValueError,
		        "sequence.index(x): x not in sequence");
	/* fall into failure code */
Fail:
	n = -1;
	/* fall through */
Done:
	Py_DECREF(it);
	return n;

}

/* Return # of times o appears in s. */
Py_ssize_t
PySequence_Count(PyObject *s, PyObject *o)
{
	return _PySequence_IterSearch(s, o, PY_ITERSEARCH_COUNT);
}

/* Return -1 if error; 1 if ob in seq; 0 if ob not in seq.
 * Use sq_contains if possible, else defer to _PySequence_IterSearch().
 */
int
PySequence_Contains(PyObject *seq, PyObject *ob)
{
	Py_ssize_t result;
	if (PyType_HasFeature(seq->ob_type, Py_TPFLAGS_HAVE_SEQUENCE_IN)) {
		PySequenceMethods *sqm = seq->ob_type->tp_as_sequence;
	        if (sqm != NULL && sqm->sq_contains != NULL)
			return (*sqm->sq_contains)(seq, ob);
	}
	result = _PySequence_IterSearch(seq, ob, PY_ITERSEARCH_CONTAINS);
	return Py_SAFE_DOWNCAST(result, Py_ssize_t, int);
}

/* Backwards compatibility */
#undef PySequence_In
int
PySequence_In(PyObject *w, PyObject *v)
{
	return PySequence_Contains(w, v);
}

Py_ssize_t
PySequence_Index(PyObject *s, PyObject *o)
{
	return _PySequence_IterSearch(s, o, PY_ITERSEARCH_INDEX);
}

/* Operations on mappings */

int
PyMapping_Check(PyObject *o)
{
	if (o && PyInstance_Check(o))
		return PyObject_HasAttrString(o, "__getitem__");

	return  o && o->ob_type->tp_as_mapping &&
		o->ob_type->tp_as_mapping->mp_subscript &&
		!(o->ob_type->tp_as_sequence && 
		  o->ob_type->tp_as_sequence->sq_slice);
}

Py_ssize_t
PyMapping_Size(PyObject *o)
{
	PyMappingMethods *m;

	if (o == NULL) {
		null_error();
		return -1;
	}

	m = o->ob_type->tp_as_mapping;
	if (m && m->mp_length)
		return m->mp_length(o);

	type_error("object of type '%.200s' has no len()", o);
	return -1;
}

#undef PyMapping_Length
Py_ssize_t
PyMapping_Length(PyObject *o)
{
	return PyMapping_Size(o);
}
#define PyMapping_Length PyMapping_Size

PyObject *
PyMapping_GetItemString(PyObject *o, char *key)
{
	PyObject *okey, *r;

	if (key == NULL)
		return null_error();

	okey = PyString_FromString(key);
	if (okey == NULL)
		return NULL;
	r = PyObject_GetItem(o, okey);
	Py_DECREF(okey);
	return r;
}

int
PyMapping_SetItemString(PyObject *o, char *key, PyObject *value)
{
	PyObject *okey;
	int r;

	if (key == NULL) {
		null_error();
		return -1;
	}

	okey = PyString_FromString(key);
	if (okey == NULL)
		return -1;
	r = PyObject_SetItem(o, okey, value);
	Py_DECREF(okey);
	return r;
}

int
PyMapping_HasKeyString(PyObject *o, char *key)
{
	PyObject *v;

	v = PyMapping_GetItemString(o, key);
	if (v) {
		Py_DECREF(v);
		return 1;
	}
	PyErr_Clear();
	return 0;
}

int
PyMapping_HasKey(PyObject *o, PyObject *key)
{
	PyObject *v;

	v = PyObject_GetItem(o, key);
	if (v) {
		Py_DECREF(v);
		return 1;
	}
	PyErr_Clear();
	return 0;
}

/* Operations on callable objects */

/* XXX PyCallable_Check() is in object.c */

PyObject *
PyObject_CallObject(PyObject *o, PyObject *a)
{
	return PyEval_CallObjectWithKeywords(o, a, NULL);
}

PyObject *
PyObject_Call(PyObject *func, PyObject *arg, PyObject *kw)
{
        ternaryfunc call;

	if ((call = func->ob_type->tp_call) != NULL) {
		PyObject *result = (*call)(func, arg, kw);
		if (result == NULL && !PyErr_Occurred())
			PyErr_SetString(
				PyExc_SystemError,
				"NULL result without error in PyObject_Call");
		return result;
	}
	PyErr_Format(PyExc_TypeError, "'%.200s' object is not callable",
		     func->ob_type->tp_name);
	return NULL;
}

static PyObject*
call_function_tail(PyObject *callable, PyObject *args)
{
	PyObject *retval;

	if (args == NULL)
		return NULL;

	if (!PyTuple_Check(args)) {
		PyObject *a;

		a = PyTuple_New(1);
		if (a == NULL) {
			Py_DECREF(args);
			return NULL;
		}
		PyTuple_SET_ITEM(a, 0, args);
		args = a;
	}
	retval = PyObject_Call(callable, args, NULL);

	Py_DECREF(args);

	return retval;
}

PyObject *
PyObject_CallFunction(PyObject *callable, char *format, ...)
{
	va_list va;
	PyObject *args;

	if (callable == NULL)
		return null_error();

	if (format && *format) {
		va_start(va, format);
		args = Py_VaBuildValue(format, va);
		va_end(va);
	}
	else
		args = PyTuple_New(0);

	return call_function_tail(callable, args);
}

PyObject *
_PyObject_CallFunction_SizeT(PyObject *callable, char *format, ...)
{
	va_list va;
	PyObject *args;

	if (callable == NULL)
		return null_error();

	if (format && *format) {
		va_start(va, format);
		args = _Py_VaBuildValue_SizeT(format, va);
		va_end(va);
	}
	else
		args = PyTuple_New(0);

	return call_function_tail(callable, args);
}

PyObject *
PyObject_CallMethod(PyObject *o, char *name, char *format, ...)
{
	va_list va;
	PyObject *args;
	PyObject *func = NULL;
	PyObject *retval = NULL;

	if (o == NULL || name == NULL)
		return null_error();

	func = PyObject_GetAttrString(o, name);
	if (func == NULL) {
		PyErr_SetString(PyExc_AttributeError, name);
		return 0;
	}

	if (!PyCallable_Check(func)) {
		type_error("attribute of type '%.200s' is not callable", func); 
		goto exit;
	}

	if (format && *format) {
		va_start(va, format);
		args = Py_VaBuildValue(format, va);
		va_end(va);
	}
	else
		args = PyTuple_New(0);

	retval = call_function_tail(func, args);

  exit:
	/* args gets consumed in call_function_tail */
	Py_XDECREF(func);

	return retval;
}

PyObject *
_PyObject_CallMethod_SizeT(PyObject *o, char *name, char *format, ...)
{
	va_list va;
	PyObject *args;
	PyObject *func = NULL;
	PyObject *retval = NULL;

	if (o == NULL || name == NULL)
		return null_error();

	func = PyObject_GetAttrString(o, name);
	if (func == NULL) {
		PyErr_SetString(PyExc_AttributeError, name);
		return 0;
	}

	if (!PyCallable_Check(func)) {
		type_error("attribute of type '%.200s' is not callable", func); 
		goto exit;
	}

	if (format && *format) {
		va_start(va, format);
		args = _Py_VaBuildValue_SizeT(format, va);
		va_end(va);
	}
	else
		args = PyTuple_New(0);

	retval = call_function_tail(func, args);

  exit:
	/* args gets consumed in call_function_tail */
	Py_XDECREF(func);

	return retval;
}


static PyObject *
objargs_mktuple(va_list va)
{
	int i, n = 0;
	va_list countva;
	PyObject *result, *tmp;

#ifdef VA_LIST_IS_ARRAY
	memcpy(countva, va, sizeof(va_list));
#else
#ifdef __va_copy
	__va_copy(countva, va);
#else
	countva = va;
#endif
#endif

	while (((PyObject *)va_arg(countva, PyObject *)) != NULL)
		++n;
	result = PyTuple_New(n);
	if (result != NULL && n > 0) {
		for (i = 0; i < n; ++i) {
			tmp = (PyObject *)va_arg(va, PyObject *);
			PyTuple_SET_ITEM(result, i, tmp);
			Py_INCREF(tmp);
		}
	}
	return result;
}

PyObject *
PyObject_CallMethodObjArgs(PyObject *callable, PyObject *name, ...)
{
	PyObject *args, *tmp;
	va_list vargs;

	if (callable == NULL || name == NULL)
		return null_error();

	callable = PyObject_GetAttr(callable, name);
	if (callable == NULL)
		return NULL;

	/* count the args */
	va_start(vargs, name);
	args = objargs_mktuple(vargs);
	va_end(vargs);
	if (args == NULL) {
		Py_DECREF(callable);
		return NULL;
	}
	tmp = PyObject_Call(callable, args, NULL);
	Py_DECREF(args);
	Py_DECREF(callable);

	return tmp;
}

PyObject *
PyObject_CallFunctionObjArgs(PyObject *callable, ...)
{
	PyObject *args, *tmp;
	va_list vargs;

	if (callable == NULL)
		return null_error();

	/* count the args */
	va_start(vargs, callable);
	args = objargs_mktuple(vargs);
	va_end(vargs);
	if (args == NULL)
		return NULL;
	tmp = PyObject_Call(callable, args, NULL);
	Py_DECREF(args);

	return tmp;
}


/* isinstance(), issubclass() */

/* abstract_get_bases() has logically 4 return states, with a sort of 0th
 * state that will almost never happen.
 *
 * 0. creating the __bases__ static string could get a MemoryError
 * 1. getattr(cls, '__bases__') could raise an AttributeError
 * 2. getattr(cls, '__bases__') could raise some other exception
 * 3. getattr(cls, '__bases__') could return a tuple
 * 4. getattr(cls, '__bases__') could return something other than a tuple
 *
 * Only state #3 is a non-error state and only it returns a non-NULL object
 * (it returns the retrieved tuple).
 *
 * Any raised AttributeErrors are masked by clearing the exception and
 * returning NULL.  If an object other than a tuple comes out of __bases__,
 * then again, the return value is NULL.  So yes, these two situations
 * produce exactly the same results: NULL is returned and no error is set.
 *
 * If some exception other than AttributeError is raised, then NULL is also
 * returned, but the exception is not cleared.  That's because we want the
 * exception to be propagated along.
 *
 * Callers are expected to test for PyErr_Occurred() when the return value
 * is NULL to decide whether a valid exception should be propagated or not.
 * When there's no exception to propagate, it's customary for the caller to
 * set a TypeError.
 */
static PyObject *
abstract_get_bases(PyObject *cls)
{
	static PyObject *__bases__ = NULL;
	PyObject *bases;

	if (__bases__ == NULL) {
		__bases__ = PyString_FromString("__bases__");
		if (__bases__ == NULL)
			return NULL;
	}
	bases = PyObject_GetAttr(cls, __bases__);
	if (bases == NULL) {
		if (PyErr_ExceptionMatches(PyExc_AttributeError))
			PyErr_Clear();
		return NULL;
	}
	if (!PyTuple_Check(bases)) {
	        Py_DECREF(bases);
		return NULL;
	}
	return bases;
}


static int
abstract_issubclass(PyObject *derived, PyObject *cls)
{
	PyObject *bases;
	Py_ssize_t i, n;
	int r = 0;


	if (derived == cls)
		return 1;

	if (PyTuple_Check(cls)) {
		/* Not a general sequence -- that opens up the road to
		   recursion and stack overflow. */
		n = PyTuple_GET_SIZE(cls);
		for (i = 0; i < n; i++) {
			if (derived == PyTuple_GET_ITEM(cls, i))
				return 1;
		}
	}
	bases = abstract_get_bases(derived);
	if (bases == NULL) {
		if (PyErr_Occurred())
			return -1;
		return 0;
	}
	n = PyTuple_GET_SIZE(bases);
	for (i = 0; i < n; i++) {
		r = abstract_issubclass(PyTuple_GET_ITEM(bases, i), cls);
		if (r != 0)
			break;
	}

	Py_DECREF(bases);

	return r;
}

static int
check_class(PyObject *cls, const char *error)
{
	PyObject *bases = abstract_get_bases(cls);
	if (bases == NULL) {
		/* Do not mask errors. */
		if (!PyErr_Occurred())
			PyErr_SetString(PyExc_TypeError, error);
		return 0;
	}
	Py_DECREF(bases);
	return -1;
}

static int
recursive_isinstance(PyObject *inst, PyObject *cls, int recursion_depth)
{
	PyObject *icls;
	static PyObject *__class__ = NULL;
	int retval = 0;

	if (__class__ == NULL) {
		__class__ = PyString_FromString("__class__");
		if (__class__ == NULL)
			return -1;
	}

	if (PyClass_Check(cls) && PyInstance_Check(inst)) {
		PyObject *inclass =
			(PyObject*)((PyInstanceObject*)inst)->in_class;
		retval = PyClass_IsSubclass(inclass, cls);
	}
	else if (PyType_Check(cls)) {
		retval = PyObject_TypeCheck(inst, (PyTypeObject *)cls);
		if (retval == 0) {
			PyObject *c = PyObject_GetAttr(inst, __class__);
			if (c == NULL) {
				PyErr_Clear();
			}
			else {
				if (c != (PyObject *)(inst->ob_type) &&
				    PyType_Check(c))
					retval = PyType_IsSubtype(
						(PyTypeObject *)c,
						(PyTypeObject *)cls);
				Py_DECREF(c);
			}
		}
	}
	else if (PyTuple_Check(cls)) {
		Py_ssize_t i, n;

                if (!recursion_depth) {
                    PyErr_SetString(PyExc_RuntimeError,
                                    "nest level of tuple too deep");
                    return -1;
                }

		n = PyTuple_GET_SIZE(cls);
		for (i = 0; i < n; i++) {
			retval = recursive_isinstance(
                                    inst,
                                    PyTuple_GET_ITEM(cls, i),
                                    recursion_depth-1);
			if (retval != 0)
				break;
		}
	}
	else {
		if (!check_class(cls,
			"isinstance() arg 2 must be a class, type,"
			" or tuple of classes and types"))
			return -1;
		icls = PyObject_GetAttr(inst, __class__);
		if (icls == NULL) {
			PyErr_Clear();
			retval = 0;
		}
		else {
			retval = abstract_issubclass(icls, cls);
			Py_DECREF(icls);
		}
	}

	return retval;
}

int
PyObject_IsInstance(PyObject *inst, PyObject *cls)
{
    return recursive_isinstance(inst, cls, Py_GetRecursionLimit());
}

static  int
recursive_issubclass(PyObject *derived, PyObject *cls, int recursion_depth)
{
	int retval;

	if (!PyClass_Check(derived) || !PyClass_Check(cls)) {
		if (!check_class(derived,
				 "issubclass() arg 1 must be a class"))
			return -1;

		if (PyTuple_Check(cls)) {
			Py_ssize_t i;
			Py_ssize_t n = PyTuple_GET_SIZE(cls);

                        if (!recursion_depth) {
                            PyErr_SetString(PyExc_RuntimeError,
                                            "nest level of tuple too deep");
                            return -1;
                        }
			for (i = 0; i < n; ++i) {
				retval = recursive_issubclass(
                                            derived,
                                            PyTuple_GET_ITEM(cls, i),
                                            recursion_depth-1);
				if (retval != 0) {
					/* either found it, or got an error */
					return retval;
				}
			}
			return 0;
		}
		else {
			if (!check_class(cls,
					"issubclass() arg 2 must be a class"
					" or tuple of classes"))
				return -1;
		}

		retval = abstract_issubclass(derived, cls);
	}
	else {
		/* shortcut */
	  	if (!(retval = (derived == cls)))
			retval = PyClass_IsSubclass(derived, cls);
	}

	return retval;
}

int
PyObject_IsSubclass(PyObject *derived, PyObject *cls)
{
    return recursive_issubclass(derived, cls, Py_GetRecursionLimit());
}


PyObject *
PyObject_GetIter(PyObject *o)
{
	PyTypeObject *t = o->ob_type;
	getiterfunc f = NULL;
	if (PyType_HasFeature(t, Py_TPFLAGS_HAVE_ITER))
		f = t->tp_iter;
	if (f == NULL) {
		if (PySequence_Check(o))
			return PySeqIter_New(o);
		return type_error("'%.200s' object is not iterable", o);
	}
	else {
		PyObject *res = (*f)(o);
		if (res != NULL && !PyIter_Check(res)) {
			PyErr_Format(PyExc_TypeError,
				     "iter() returned non-iterator "
				     "of type '%.100s'",
				     res->ob_type->tp_name);
			Py_DECREF(res);
			res = NULL;
		}
		return res;
	}
}

/* Return next item.
 * If an error occurs, return NULL.  PyErr_Occurred() will be true.
 * If the iteration terminates normally, return NULL and clear the
 * PyExc_StopIteration exception (if it was set).  PyErr_Occurred()
 * will be false.
 * Else return the next object.  PyErr_Occurred() will be false.
 */
PyObject *
PyIter_Next(PyObject *iter)
{
	PyObject *result;
	assert(PyIter_Check(iter));
	result = (*iter->ob_type->tp_iternext)(iter);
	if (result == NULL &&
	    PyErr_Occurred() &&
	    PyErr_ExceptionMatches(PyExc_StopIteration))
		PyErr_Clear();
	return result;
}
