
/* Complex object implementation */

/* Borrows heavily from floatobject.c */

/* Submitted by Jim Hugunin */

#include "Python.h"
#include "structmember.h"

#ifndef WITHOUT_COMPLEX

/* Precisions used by repr() and str(), respectively.

   The repr() precision (17 significant decimal digits) is the minimal number
   that is guaranteed to have enough precision so that if the number is read
   back in the exact same binary value is recreated.  This is true for IEEE
   floating point by design, and also happens to work for all other modern
   hardware.

   The str() precision is chosen so that in most cases, the rounding noise
   created by various operations is suppressed, while giving plenty of
   precision for practical use.
*/

#define PREC_REPR	17
#define PREC_STR	12

/* elementary operations on complex numbers */

static Py_complex c_1 = {1., 0.};

Py_complex
c_sum(Py_complex a, Py_complex b)
{
	Py_complex r;
	r.real = a.real + b.real;
	r.imag = a.imag + b.imag;
	return r;
}

Py_complex
c_diff(Py_complex a, Py_complex b)
{
	Py_complex r;
	r.real = a.real - b.real;
	r.imag = a.imag - b.imag;
	return r;
}

Py_complex
c_neg(Py_complex a)
{
	Py_complex r;
	r.real = -a.real;
	r.imag = -a.imag;
	return r;
}

Py_complex
c_prod(Py_complex a, Py_complex b)
{
	Py_complex r;
	r.real = a.real*b.real - a.imag*b.imag;
	r.imag = a.real*b.imag + a.imag*b.real;
	return r;
}

Py_complex
c_quot(Py_complex a, Py_complex b)
{
	/******************************************************************
	This was the original algorithm.  It's grossly prone to spurious
	overflow and underflow errors.  It also merrily divides by 0 despite
	checking for that(!).  The code still serves a doc purpose here, as
	the algorithm following is a simple by-cases transformation of this
	one:

	Py_complex r;
	double d = b.real*b.real + b.imag*b.imag;
	if (d == 0.)
		errno = EDOM;
	r.real = (a.real*b.real + a.imag*b.imag)/d;
	r.imag = (a.imag*b.real - a.real*b.imag)/d;
	return r;
	******************************************************************/

	/* This algorithm is better, and is pretty obvious:  first divide the
	 * numerators and denominator by whichever of {b.real, b.imag} has
	 * larger magnitude.  The earliest reference I found was to CACM
	 * Algorithm 116 (Complex Division, Robert L. Smith, Stanford
	 * University).  As usual, though, we're still ignoring all IEEE
	 * endcases.
	 */
	 Py_complex r;	/* the result */
 	 const double abs_breal = b.real < 0 ? -b.real : b.real;
	 const double abs_bimag = b.imag < 0 ? -b.imag : b.imag;

	 if (abs_breal >= abs_bimag) {
 		/* divide tops and bottom by b.real */
	 	if (abs_breal == 0.0) {
	 		errno = EDOM;
	 		r.real = r.imag = 0.0;
	 	}
	 	else {
	 		const double ratio = b.imag / b.real;
	 		const double denom = b.real + b.imag * ratio;
	 		r.real = (a.real + a.imag * ratio) / denom;
	 		r.imag = (a.imag - a.real * ratio) / denom;
	 	}
	}
	else {
		/* divide tops and bottom by b.imag */
		const double ratio = b.real / b.imag;
		const double denom = b.real * ratio + b.imag;
		assert(b.imag != 0.0);
		r.real = (a.real * ratio + a.imag) / denom;
		r.imag = (a.imag * ratio - a.real) / denom;
	}
	return r;
}

Py_complex
c_pow(Py_complex a, Py_complex b)
{
	Py_complex r;
	double vabs,len,at,phase;
	if (b.real == 0. && b.imag == 0.) {
		r.real = 1.;
		r.imag = 0.;
	}
	else if (a.real == 0. && a.imag == 0.) {
		if (b.imag != 0. || b.real < 0.)
			errno = EDOM;
		r.real = 0.;
		r.imag = 0.;
	}
	else {
		vabs = hypot(a.real,a.imag);
		len = pow(vabs,b.real);
		at = atan2(a.imag, a.real);
		phase = at*b.real;
		if (b.imag != 0.0) {
			len /= exp(at*b.imag);
			phase += b.imag*log(vabs);
		}
		r.real = len*cos(phase);
		r.imag = len*sin(phase);
	}
	return r;
}

static Py_complex
c_powu(Py_complex x, long n)
{
	Py_complex r, p;
	long mask = 1;
	r = c_1;
	p = x;
	while (mask > 0 && n >= mask) {
		if (n & mask)
			r = c_prod(r,p);
		mask <<= 1;
		p = c_prod(p,p);
	}
	return r;
}

static Py_complex
c_powi(Py_complex x, long n)
{
	Py_complex cn;

	if (n > 100 || n < -100) {
		cn.real = (double) n;
		cn.imag = 0.;
		return c_pow(x,cn);
	}
	else if (n > 0)
		return c_powu(x,n);
	else
		return c_quot(c_1,c_powu(x,-n));

}

static PyObject *
complex_subtype_from_c_complex(PyTypeObject *type, Py_complex cval)
{
	PyObject *op;

	op = type->tp_alloc(type, 0);
	if (op != NULL)
		((PyComplexObject *)op)->cval = cval;
	return op;
}

PyObject *
PyComplex_FromCComplex(Py_complex cval)
{
	register PyComplexObject *op;

	/* Inline PyObject_New */
	op = (PyComplexObject *) PyObject_MALLOC(sizeof(PyComplexObject));
	if (op == NULL)
		return PyErr_NoMemory();
	PyObject_INIT(op, &PyComplex_Type);
	op->cval = cval;
	return (PyObject *) op;
}

static PyObject *
complex_subtype_from_doubles(PyTypeObject *type, double real, double imag)
{
	Py_complex c;
	c.real = real;
	c.imag = imag;
	return complex_subtype_from_c_complex(type, c);
}

PyObject *
PyComplex_FromDoubles(double real, double imag)
{
	Py_complex c;
	c.real = real;
	c.imag = imag;
	return PyComplex_FromCComplex(c);
}

double
PyComplex_RealAsDouble(PyObject *op)
{
	if (PyComplex_Check(op)) {
		return ((PyComplexObject *)op)->cval.real;
	}
	else {
		return PyFloat_AsDouble(op);
	}
}

double
PyComplex_ImagAsDouble(PyObject *op)
{
	if (PyComplex_Check(op)) {
		return ((PyComplexObject *)op)->cval.imag;
	}
	else {
		return 0.0;
	}
}

Py_complex
PyComplex_AsCComplex(PyObject *op)
{
	Py_complex cv;
	if (PyComplex_Check(op)) {
		return ((PyComplexObject *)op)->cval;
	}
	else {
		cv.real = PyFloat_AsDouble(op);
		cv.imag = 0.;
		return cv;
	}
}

static void
complex_dealloc(PyObject *op)
{
	op->ob_type->tp_free(op);
}


static void
complex_to_buf(char *buf, int bufsz, PyComplexObject *v, int precision)
{
	char format[32];
	if (v->cval.real == 0.) {
		PyOS_snprintf(format, sizeof(format), "%%.%ig", precision);
		PyOS_ascii_formatd(buf, bufsz - 1, format, v->cval.imag);
		strncat(buf, "j", 1);
	} else {
		char re[64], im[64];
		/* Format imaginary part with sign, real part without */
		PyOS_snprintf(format, sizeof(format), "%%.%ig", precision);
		PyOS_ascii_formatd(re, sizeof(re), format, v->cval.real);
		PyOS_snprintf(format, sizeof(format), "%%+.%ig", precision);
		PyOS_ascii_formatd(im, sizeof(im), format, v->cval.imag);
		PyOS_snprintf(buf, bufsz, "(%s%sj)", re, im);
	}
}

static int
complex_print(PyComplexObject *v, FILE *fp, int flags)
{
	char buf[100];
	complex_to_buf(buf, sizeof(buf), v,
		       (flags & Py_PRINT_RAW) ? PREC_STR : PREC_REPR);
	fputs(buf, fp);
	return 0;
}

static PyObject *
complex_repr(PyComplexObject *v)
{
	char buf[100];
	complex_to_buf(buf, sizeof(buf), v, PREC_REPR);
	return PyString_FromString(buf);
}

static PyObject *
complex_str(PyComplexObject *v)
{
	char buf[100];
	complex_to_buf(buf, sizeof(buf), v, PREC_STR);
	return PyString_FromString(buf);
}

static long
complex_hash(PyComplexObject *v)
{
	long hashreal, hashimag, combined;
	hashreal = _Py_HashDouble(v->cval.real);
	if (hashreal == -1)
		return -1;
	hashimag = _Py_HashDouble(v->cval.imag);
	if (hashimag == -1)
		return -1;
	/* Note:  if the imaginary part is 0, hashimag is 0 now,
	 * so the following returns hashreal unchanged.  This is
	 * important because numbers of different types that
	 * compare equal must have the same hash value, so that
	 * hash(x + 0*j) must equal hash(x).
	 */
	combined = hashreal + 1000003 * hashimag;
	if (combined == -1)
		combined = -2;
	return combined;
}

static PyObject *
complex_add(PyComplexObject *v, PyComplexObject *w)
{
	Py_complex result;
	PyFPE_START_PROTECT("complex_add", return 0)
	result = c_sum(v->cval,w->cval);
	PyFPE_END_PROTECT(result)
	return PyComplex_FromCComplex(result);
}

static PyObject *
complex_sub(PyComplexObject *v, PyComplexObject *w)
{
	Py_complex result;
	PyFPE_START_PROTECT("complex_sub", return 0)
	result = c_diff(v->cval,w->cval);
	PyFPE_END_PROTECT(result)
	return PyComplex_FromCComplex(result);
}

static PyObject *
complex_mul(PyComplexObject *v, PyComplexObject *w)
{
	Py_complex result;
	PyFPE_START_PROTECT("complex_mul", return 0)
	result = c_prod(v->cval,w->cval);
	PyFPE_END_PROTECT(result)
	return PyComplex_FromCComplex(result);
}

static PyObject *
complex_div(PyComplexObject *v, PyComplexObject *w)
{
	Py_complex quot;
	PyFPE_START_PROTECT("complex_div", return 0)
	errno = 0;
	quot = c_quot(v->cval,w->cval);
	PyFPE_END_PROTECT(quot)
	if (errno == EDOM) {
		PyErr_SetString(PyExc_ZeroDivisionError, "complex division");
		return NULL;
	}
	return PyComplex_FromCComplex(quot);
}

static PyObject *
complex_classic_div(PyComplexObject *v, PyComplexObject *w)
{
	Py_complex quot;

	if (Py_DivisionWarningFlag >= 2 &&
	    PyErr_Warn(PyExc_DeprecationWarning,
		       "classic complex division") < 0)
		return NULL;

	PyFPE_START_PROTECT("complex_classic_div", return 0)
	errno = 0;
	quot = c_quot(v->cval,w->cval);
	PyFPE_END_PROTECT(quot)
	if (errno == EDOM) {
		PyErr_SetString(PyExc_ZeroDivisionError, "complex division");
		return NULL;
	}
	return PyComplex_FromCComplex(quot);
}

static PyObject *
complex_remainder(PyComplexObject *v, PyComplexObject *w)
{
        Py_complex div, mod;

	if (PyErr_Warn(PyExc_DeprecationWarning,
		       "complex divmod(), // and % are deprecated") < 0)
		return NULL;

	errno = 0;
	div = c_quot(v->cval,w->cval); /* The raw divisor value. */
	if (errno == EDOM) {
		PyErr_SetString(PyExc_ZeroDivisionError, "complex remainder");
		return NULL;
	}
	div.real = floor(div.real); /* Use the floor of the real part. */
	div.imag = 0.0;
	mod = c_diff(v->cval, c_prod(w->cval, div));

	return PyComplex_FromCComplex(mod);
}


static PyObject *
complex_divmod(PyComplexObject *v, PyComplexObject *w)
{
        Py_complex div, mod;
	PyObject *d, *m, *z;

	if (PyErr_Warn(PyExc_DeprecationWarning,
		       "complex divmod(), // and % are deprecated") < 0)
		return NULL;

	errno = 0;
	div = c_quot(v->cval,w->cval); /* The raw divisor value. */
	if (errno == EDOM) {
		PyErr_SetString(PyExc_ZeroDivisionError, "complex divmod()");
		return NULL;
	}
	div.real = floor(div.real); /* Use the floor of the real part. */
	div.imag = 0.0;
	mod = c_diff(v->cval, c_prod(w->cval, div));
	d = PyComplex_FromCComplex(div);
	m = PyComplex_FromCComplex(mod);
	z = PyTuple_Pack(2, d, m);
	Py_XDECREF(d);
	Py_XDECREF(m);
	return z;
}

static PyObject *
complex_pow(PyComplexObject *v, PyObject *w, PyComplexObject *z)
{
	Py_complex p;
	Py_complex exponent;
	long int_exponent;

 	if ((PyObject *)z!=Py_None) {
		PyErr_SetString(PyExc_ValueError, "complex modulo");
		return NULL;
	}
	PyFPE_START_PROTECT("complex_pow", return 0)
	errno = 0;
	exponent = ((PyComplexObject*)w)->cval;
	int_exponent = (long)exponent.real;
	if (exponent.imag == 0. && exponent.real == int_exponent)
		p = c_powi(v->cval,int_exponent);
	else
		p = c_pow(v->cval,exponent);

	PyFPE_END_PROTECT(p)
	Py_ADJUST_ERANGE2(p.real, p.imag);
	if (errno == EDOM) {
		PyErr_SetString(PyExc_ZeroDivisionError,
				"0.0 to a negative or complex power");
		return NULL;
	}
	else if (errno == ERANGE) {
		PyErr_SetString(PyExc_OverflowError,
				"complex exponentiation");
		return NULL;
	}
	return PyComplex_FromCComplex(p);
}

static PyObject *
complex_int_div(PyComplexObject *v, PyComplexObject *w)
{
	PyObject *t, *r;
	
	t = complex_divmod(v, w);
	if (t != NULL) {
		r = PyTuple_GET_ITEM(t, 0);
		Py_INCREF(r);
		Py_DECREF(t);
		return r;
	}
	return NULL;
}

static PyObject *
complex_neg(PyComplexObject *v)
{
	Py_complex neg;
	neg.real = -v->cval.real;
	neg.imag = -v->cval.imag;
	return PyComplex_FromCComplex(neg);
}

static PyObject *
complex_pos(PyComplexObject *v)
{
	if (PyComplex_CheckExact(v)) {
		Py_INCREF(v);
		return (PyObject *)v;
	}
	else
		return PyComplex_FromCComplex(v->cval);
}

static PyObject *
complex_abs(PyComplexObject *v)
{
	double result;
	PyFPE_START_PROTECT("complex_abs", return 0)
	result = hypot(v->cval.real,v->cval.imag);
	PyFPE_END_PROTECT(result)
	return PyFloat_FromDouble(result);
}

static int
complex_nonzero(PyComplexObject *v)
{
	return v->cval.real != 0.0 || v->cval.imag != 0.0;
}

static int
complex_coerce(PyObject **pv, PyObject **pw)
{
	Py_complex cval;
	cval.imag = 0.;
	if (PyInt_Check(*pw)) {
		cval.real = (double)PyInt_AsLong(*pw);
		*pw = PyComplex_FromCComplex(cval);
		Py_INCREF(*pv);
		return 0;
	}
	else if (PyLong_Check(*pw)) {
		cval.real = PyLong_AsDouble(*pw);
		if (cval.real == -1.0 && PyErr_Occurred())
			return -1;
		*pw = PyComplex_FromCComplex(cval);
		Py_INCREF(*pv);
		return 0;
	}
	else if (PyFloat_Check(*pw)) {
		cval.real = PyFloat_AsDouble(*pw);
		*pw = PyComplex_FromCComplex(cval);
		Py_INCREF(*pv);
		return 0;
	}
	else if (PyComplex_Check(*pw)) {
		Py_INCREF(*pv);
		Py_INCREF(*pw);
		return 0;
	}
	return 1; /* Can't do it */
}

static PyObject *
complex_richcompare(PyObject *v, PyObject *w, int op)
{
	int c;
	Py_complex i, j;
	PyObject *res;

	c = PyNumber_CoerceEx(&v, &w);
	if (c < 0)
		return NULL;
	if (c > 0) {
		Py_INCREF(Py_NotImplemented);
		return Py_NotImplemented;
	}
	/* Make sure both arguments are complex. */
	if (!(PyComplex_Check(v) && PyComplex_Check(w))) {
		Py_DECREF(v);
		Py_DECREF(w);
		Py_INCREF(Py_NotImplemented);
		return Py_NotImplemented;
	}

	i = ((PyComplexObject *)v)->cval;
	j = ((PyComplexObject *)w)->cval;
	Py_DECREF(v);
	Py_DECREF(w);

	if (op != Py_EQ && op != Py_NE) {
		PyErr_SetString(PyExc_TypeError,
			"no ordering relation is defined for complex numbers");
		return NULL;
	}

	if ((i.real == j.real && i.imag == j.imag) == (op == Py_EQ))
		res = Py_True;
	else
		res = Py_False;

	Py_INCREF(res);
	return res;
}

static PyObject *
complex_int(PyObject *v)
{
	PyErr_SetString(PyExc_TypeError,
		   "can't convert complex to int; use int(abs(z))");
	return NULL;
}

static PyObject *
complex_long(PyObject *v)
{
	PyErr_SetString(PyExc_TypeError,
		   "can't convert complex to long; use long(abs(z))");
	return NULL;
}

static PyObject *
complex_float(PyObject *v)
{
	PyErr_SetString(PyExc_TypeError,
		   "can't convert complex to float; use abs(z)");
	return NULL;
}

static PyObject *
complex_conjugate(PyObject *self)
{
	Py_complex c;
	c = ((PyComplexObject *)self)->cval;
	c.imag = -c.imag;
	return PyComplex_FromCComplex(c);
}

static PyObject *
complex_getnewargs(PyComplexObject *v)
{
	return Py_BuildValue("(D)", &v->cval);
}

static PyMethodDef complex_methods[] = {
	{"conjugate",	(PyCFunction)complex_conjugate,	METH_NOARGS},
	{"__getnewargs__",	(PyCFunction)complex_getnewargs,	METH_NOARGS},
	{NULL,		NULL}		/* sentinel */
};

static PyMemberDef complex_members[] = {
	{"real", T_DOUBLE, offsetof(PyComplexObject, cval.real), READONLY,
	 "the real part of a complex number"},
	{"imag", T_DOUBLE, offsetof(PyComplexObject, cval.imag), READONLY,
	 "the imaginary part of a complex number"},
	{0},
};

static PyObject *
complex_subtype_from_string(PyTypeObject *type, PyObject *v)
{
	const char *s, *start;
	char *end;
	double x=0.0, y=0.0, z;
	int got_re=0, got_im=0, done=0;
	int digit_or_dot;
	int sw_error=0;
	int sign;
	char buffer[256]; /* For errors */
#ifdef Py_USING_UNICODE
	char s_buffer[256];
#endif
	Py_ssize_t len;

	if (PyString_Check(v)) {
		s = PyString_AS_STRING(v);
		len = PyString_GET_SIZE(v);
	}
#ifdef Py_USING_UNICODE
	else if (PyUnicode_Check(v)) {
		if (PyUnicode_GET_SIZE(v) >= (Py_ssize_t)sizeof(s_buffer)) {
			PyErr_SetString(PyExc_ValueError,
				 "complex() literal too large to convert");
			return NULL;
		}
		if (PyUnicode_EncodeDecimal(PyUnicode_AS_UNICODE(v),
					    PyUnicode_GET_SIZE(v),
					    s_buffer,
					    NULL))
			return NULL;
		s = s_buffer;
		len = strlen(s);
	}
#endif
	else if (PyObject_AsCharBuffer(v, &s, &len)) {
		PyErr_SetString(PyExc_TypeError,
				"complex() arg is not a string");
		return NULL;
	}

	/* position on first nonblank */
	start = s;
	while (*s && isspace(Py_CHARMASK(*s)))
		s++;
	if (s[0] == '\0') {
		PyErr_SetString(PyExc_ValueError,
				"complex() arg is an empty string");
		return NULL;
	}

	z = -1.0;
	sign = 1;
	do {

		switch (*s) {

		case '\0':
			if (s-start != len) {
				PyErr_SetString(
					PyExc_ValueError,
					"complex() arg contains a null byte");
				return NULL;
			}
			if(!done) sw_error=1;
			break;

		case '-':
			sign = -1;
				/* Fallthrough */
		case '+':
			if (done)  sw_error=1;
			s++;
			if  (  *s=='\0'||*s=='+'||*s=='-'  ||
			       isspace(Py_CHARMASK(*s))  )  sw_error=1;
			break;

		case 'J':
		case 'j':
			if (got_im || done) {
				sw_error = 1;
				break;
			}
			if  (z<0.0) {
				y=sign;
			}
			else{
				y=sign*z;
			}
			got_im=1;
			s++;
			if  (*s!='+' && *s!='-' )
				done=1;
			break;

		default:
			if (isspace(Py_CHARMASK(*s))) {
				while (*s && isspace(Py_CHARMASK(*s)))
					s++;
				if (s[0] != '\0')
					sw_error=1;
				else
					done = 1;
				break;
			}
			digit_or_dot =
				(*s=='.' || isdigit(Py_CHARMASK(*s)));
			if  (done||!digit_or_dot) {
				sw_error=1;
				break;
			}
			errno = 0;
			PyFPE_START_PROTECT("strtod", return 0)
				z = PyOS_ascii_strtod(s, &end) ;
			PyFPE_END_PROTECT(z)
				if (errno != 0) {
					PyOS_snprintf(buffer, sizeof(buffer),
					  "float() out of range: %.150s", s);
					PyErr_SetString(
						PyExc_ValueError,
						buffer);
					return NULL;
				}
			s=end;
			if  (*s=='J' || *s=='j') {

				break;
			}
			if  (got_re) {
				sw_error=1;
				break;
			}

				/* accept a real part */
			x=sign*z;
			got_re=1;
			if  (got_im)  done=1;
			z = -1.0;
			sign = 1;
			break;

		}  /* end of switch  */

	} while (s - start < len && !sw_error);

	if (sw_error) {
		PyErr_SetString(PyExc_ValueError,
				"complex() arg is a malformed string");
		return NULL;
	}

	return complex_subtype_from_doubles(type, x, y);
}

static PyObject *
complex_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	PyObject *r, *i, *tmp, *f;
	PyNumberMethods *nbr, *nbi = NULL;
	Py_complex cr, ci;
	int own_r = 0;
	static PyObject *complexstr;
	static char *kwlist[] = {"real", "imag", 0};

	r = Py_False;
	i = NULL;
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OO:complex", kwlist,
					 &r, &i))
		return NULL;

	/* Special-case for single argument that is already complex */
	if (PyComplex_CheckExact(r) && i == NULL &&
	    type == &PyComplex_Type) {
		/* Note that we can't know whether it's safe to return
		   a complex *subclass* instance as-is, hence the restriction
		   to exact complexes here.  */
		Py_INCREF(r);
		return r;
	}
	if (PyString_Check(r) || PyUnicode_Check(r)) {
		if (i != NULL) {
			PyErr_SetString(PyExc_TypeError,
					"complex() can't take second arg"
					" if first is a string");
			return NULL;
                }
		return complex_subtype_from_string(type, r);
	}
	if (i != NULL && (PyString_Check(i) || PyUnicode_Check(i))) {
		PyErr_SetString(PyExc_TypeError,
				"complex() second arg can't be a string");
		return NULL;
	}

	/* XXX Hack to support classes with __complex__ method */
	if (complexstr == NULL) {
		complexstr = PyString_InternFromString("__complex__");
		if (complexstr == NULL)
			return NULL;
	}
	f = PyObject_GetAttr(r, complexstr);
	if (f == NULL)
		PyErr_Clear();
	else {
		PyObject *args = PyTuple_New(0);
		if (args == NULL)
			return NULL;
		r = PyEval_CallObject(f, args);
		Py_DECREF(args);
		Py_DECREF(f);
		if (r == NULL)
			return NULL;
		own_r = 1;
	}
	nbr = r->ob_type->tp_as_number;
	if (i != NULL)
		nbi = i->ob_type->tp_as_number;
	if (nbr == NULL || nbr->nb_float == NULL ||
	    ((i != NULL) && (nbi == NULL || nbi->nb_float == NULL))) {
		PyErr_SetString(PyExc_TypeError,
			   "complex() argument must be a string or a number");
		if (own_r) {
			Py_DECREF(r);
		}
		return NULL;
	}
	if (PyComplex_Check(r)) {
		/* Note that if r is of a complex subtype, we're only
		   retaining its real & imag parts here, and the return
		   value is (properly) of the builtin complex type. */
		cr = ((PyComplexObject*)r)->cval;
		if (own_r) {
			Py_DECREF(r);
		}
	}
	else {
		tmp = PyNumber_Float(r);
		if (own_r) {
			Py_DECREF(r);
		}
		if (tmp == NULL)
			return NULL;
		if (!PyFloat_Check(tmp)) {
			PyErr_SetString(PyExc_TypeError,
					"float(r) didn't return a float");
			Py_DECREF(tmp);
			return NULL;
		}
		cr.real = PyFloat_AsDouble(tmp);
		Py_DECREF(tmp);
		cr.imag = 0.0;
	}
	if (i == NULL) {
		ci.real = 0.0;
		ci.imag = 0.0;
	}
	else if (PyComplex_Check(i))
		ci = ((PyComplexObject*)i)->cval;
	else {
		tmp = (*nbi->nb_float)(i);
		if (tmp == NULL)
			return NULL;
		ci.real = PyFloat_AsDouble(tmp);
		Py_DECREF(tmp);
		ci.imag = 0.;
	}
	cr.real -= ci.imag;
	cr.imag += ci.real;
	return complex_subtype_from_c_complex(type, cr);
}

PyDoc_STRVAR(complex_doc,
"complex(real[, imag]) -> complex number\n"
"\n"
"Create a complex number from a real part and an optional imaginary part.\n"
"This is equivalent to (real + imag*1j) where imag defaults to 0.");

static PyNumberMethods complex_as_number = {
	(binaryfunc)complex_add, 		/* nb_add */
	(binaryfunc)complex_sub, 		/* nb_subtract */
	(binaryfunc)complex_mul, 		/* nb_multiply */
	(binaryfunc)complex_classic_div,	/* nb_divide */
	(binaryfunc)complex_remainder,		/* nb_remainder */
	(binaryfunc)complex_divmod,		/* nb_divmod */
	(ternaryfunc)complex_pow,		/* nb_power */
	(unaryfunc)complex_neg,			/* nb_negative */
	(unaryfunc)complex_pos,			/* nb_positive */
	(unaryfunc)complex_abs,			/* nb_absolute */
	(inquiry)complex_nonzero,		/* nb_nonzero */
	0,					/* nb_invert */
	0,					/* nb_lshift */
	0,					/* nb_rshift */
	0,					/* nb_and */
	0,					/* nb_xor */
	0,					/* nb_or */
	complex_coerce,				/* nb_coerce */
	complex_int,				/* nb_int */
	complex_long,				/* nb_long */
	complex_float,				/* nb_float */
	0,					/* nb_oct */
	0,					/* nb_hex */
	0,					/* nb_inplace_add */
	0,					/* nb_inplace_subtract */
	0,					/* nb_inplace_multiply*/
	0,					/* nb_inplace_divide */
	0,					/* nb_inplace_remainder */
	0, 					/* nb_inplace_power */
	0,					/* nb_inplace_lshift */
	0,					/* nb_inplace_rshift */
	0,					/* nb_inplace_and */
	0,					/* nb_inplace_xor */
	0,					/* nb_inplace_or */
	(binaryfunc)complex_int_div,		/* nb_floor_divide */
	(binaryfunc)complex_div,		/* nb_true_divide */
	0,					/* nb_inplace_floor_divide */
	0,					/* nb_inplace_true_divide */
};

PyTypeObject PyComplex_Type = {
	PyObject_HEAD_INIT(&PyType_Type)
	0,
	"complex",
	sizeof(PyComplexObject),
	0,
	complex_dealloc,			/* tp_dealloc */
	(printfunc)complex_print,		/* tp_print */
	0,					/* tp_getattr */
	0,					/* tp_setattr */
	0,					/* tp_compare */
	(reprfunc)complex_repr,			/* tp_repr */
	&complex_as_number,    			/* tp_as_number */
	0,					/* tp_as_sequence */
	0,					/* tp_as_mapping */
	(hashfunc)complex_hash, 		/* tp_hash */
	0,					/* tp_call */
	(reprfunc)complex_str,			/* tp_str */
	PyObject_GenericGetAttr,		/* tp_getattro */
	0,					/* tp_setattro */
	0,					/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /* tp_flags */
	complex_doc,				/* tp_doc */
	0,					/* tp_traverse */
	0,					/* tp_clear */
	complex_richcompare,			/* tp_richcompare */
	0,					/* tp_weaklistoffset */
	0,					/* tp_iter */
	0,					/* tp_iternext */
	complex_methods,			/* tp_methods */
	complex_members,			/* tp_members */
	0,					/* tp_getset */
	0,					/* tp_base */
	0,					/* tp_dict */
	0,					/* tp_descr_get */
	0,					/* tp_descr_set */
	0,					/* tp_dictoffset */
	0,					/* tp_init */
	PyType_GenericAlloc,			/* tp_alloc */
	complex_new,				/* tp_new */
	PyObject_Del,           		/* tp_free */
};

#endif
