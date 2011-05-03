

/* Long (arbitrary precision) integer object implementation */

/* XXX The functional organization of this file is terrible */

#include "Python.h"
#include "longintrepr.h"

#include <ctype.h>

/* For long multiplication, use the O(N**2) school algorithm unless
 * both operands contain more than KARATSUBA_CUTOFF digits (this
 * being an internal Python long digit, in base BASE).
 */
#define KARATSUBA_CUTOFF 70
#define KARATSUBA_SQUARE_CUTOFF (2 * KARATSUBA_CUTOFF)

/* For exponentiation, use the binary left-to-right algorithm
 * unless the exponent contains more than FIVEARY_CUTOFF digits.
 * In that case, do 5 bits at a time.  The potential drawback is that
 * a table of 2**5 intermediate results is computed.
 */
#define FIVEARY_CUTOFF 8

#define ABS(x) ((x) < 0 ? -(x) : (x))

#undef MIN
#undef MAX
#define MAX(x, y) ((x) < (y) ? (y) : (x))
#define MIN(x, y) ((x) > (y) ? (y) : (x))

/* Forward */
static PyLongObject *long_normalize(PyLongObject *);
static PyLongObject *mul1(PyLongObject *, wdigit);
static PyLongObject *muladd1(PyLongObject *, wdigit, wdigit);
static PyLongObject *divrem1(PyLongObject *, digit, digit *);
static PyObject *long_format(PyObject *aa, int base, int addL);

#define SIGCHECK(PyTryBlock) \
	if (--_Py_Ticker < 0) { \
		_Py_Ticker = _Py_CheckInterval; \
		if (PyErr_CheckSignals()) PyTryBlock \
	}

/* Normalize (remove leading zeros from) a long int object.
   Doesn't attempt to free the storage--in most cases, due to the nature
   of the algorithms used, this could save at most be one word anyway. */

static PyLongObject *
long_normalize(register PyLongObject *v)
{
	Py_ssize_t j = ABS(v->ob_size);
	Py_ssize_t i = j;

	while (i > 0 && v->ob_digit[i-1] == 0)
		--i;
	if (i != j)
		v->ob_size = (v->ob_size < 0) ? -(i) : i;
	return v;
}

/* Allocate a new long int object with size digits.
   Return NULL and set exception if we run out of memory. */

PyLongObject *
_PyLong_New(Py_ssize_t size)
{
	if (size > PY_SSIZE_T_MAX) {
		PyErr_NoMemory();
		return NULL;
	}
	return PyObject_NEW_VAR(PyLongObject, &PyLong_Type, size);
}

PyObject *
_PyLong_Copy(PyLongObject *src)
{
	PyLongObject *result;
	Py_ssize_t i;

	assert(src != NULL);
	i = src->ob_size;
	if (i < 0)
		i = -(i);
	result = _PyLong_New(i);
	if (result != NULL) {
		result->ob_size = src->ob_size;
		while (--i >= 0)
			result->ob_digit[i] = src->ob_digit[i];
	}
	return (PyObject *)result;
}

/* Create a new long int object from a C long int */

PyObject *
PyLong_FromLong(long ival)
{
	PyLongObject *v;
	unsigned long t;  /* unsigned so >> doesn't propagate sign bit */
	int ndigits = 0;
	int negative = 0;

	if (ival < 0) {
		ival = -ival;
		negative = 1;
	}

	/* Count the number of Python digits.
	   We used to pick 5 ("big enough for anything"), but that's a
	   waste of time and space given that 5*15 = 75 bits are rarely
	   needed. */
	t = (unsigned long)ival;
	while (t) {
		++ndigits;
		t >>= SHIFT;
	}
	v = _PyLong_New(ndigits);
	if (v != NULL) {
		digit *p = v->ob_digit;
		v->ob_size = negative ? -ndigits : ndigits;
		t = (unsigned long)ival;
		while (t) {
			*p++ = (digit)(t & MASK);
			t >>= SHIFT;
		}
	}
	return (PyObject *)v;
}

/* Create a new long int object from a C unsigned long int */

PyObject *
PyLong_FromUnsignedLong(unsigned long ival)
{
	PyLongObject *v;
	unsigned long t;
	int ndigits = 0;

	/* Count the number of Python digits. */
	t = (unsigned long)ival;
	while (t) {
		++ndigits;
		t >>= SHIFT;
	}
	v = _PyLong_New(ndigits);
	if (v != NULL) {
		digit *p = v->ob_digit;
		v->ob_size = ndigits;
		while (ival) {
			*p++ = (digit)(ival & MASK);
			ival >>= SHIFT;
		}
	}
	return (PyObject *)v;
}

/* Create a new long int object from a C double */

PyObject *
PyLong_FromDouble(double dval)
{
	PyLongObject *v;
	double frac;
	int i, ndig, expo, neg;
	neg = 0;
	if (Py_IS_INFINITY(dval)) {
		PyErr_SetString(PyExc_OverflowError,
			"cannot convert float infinity to long");
		return NULL;
	}
	if (dval < 0.0) {
		neg = 1;
		dval = -dval;
	}
	frac = frexp(dval, &expo); /* dval = frac*2**expo; 0.0 <= frac < 1.0 */
	if (expo <= 0)
		return PyLong_FromLong(0L);
	ndig = (expo-1) / SHIFT + 1; /* Number of 'digits' in result */
	v = _PyLong_New(ndig);
	if (v == NULL)
		return NULL;
	frac = ldexp(frac, (expo-1) % SHIFT + 1);
	for (i = ndig; --i >= 0; ) {
		long bits = (long)frac;
		v->ob_digit[i] = (digit) bits;
		frac = frac - (double)bits;
		frac = ldexp(frac, SHIFT);
	}
	if (neg)
		v->ob_size = -(v->ob_size);
	return (PyObject *)v;
}

/* Checking for overflow in PyLong_AsLong is a PITA since C doesn't define
 * anything about what happens when a signed integer operation overflows,
 * and some compilers think they're doing you a favor by being "clever"
 * then.  The bit pattern for the largest postive signed long is
 * (unsigned long)LONG_MAX, and for the smallest negative signed long
 * it is abs(LONG_MIN), which we could write -(unsigned long)LONG_MIN.
 * However, some other compilers warn about applying unary minus to an
 * unsigned operand.  Hence the weird "0-".
 */
#define PY_ABS_LONG_MIN		(0-(unsigned long)LONG_MIN)
#define PY_ABS_SSIZE_T_MIN	(0-(size_t)PY_SSIZE_T_MIN)

/* Get a C long int from a long int object.
   Returns -1 and sets an error condition if overflow occurs. */

long
PyLong_AsLong(PyObject *vv)
{
	/* This version by Tim Peters */
	register PyLongObject *v;
	unsigned long x, prev;
	Py_ssize_t i;
	int sign;

	if (vv == NULL || !PyLong_Check(vv)) {
		if (vv != NULL && PyInt_Check(vv))
			return PyInt_AsLong(vv);
		PyErr_BadInternalCall();
		return -1;
	}
	v = (PyLongObject *)vv;
	i = v->ob_size;
	sign = 1;
	x = 0;
	if (i < 0) {
		sign = -1;
		i = -(i);
	}
	while (--i >= 0) {
		prev = x;
		x = (x << SHIFT) + v->ob_digit[i];
		if ((x >> SHIFT) != prev)
			goto overflow;
	}
	/* Haven't lost any bits, but casting to long requires extra care
	 * (see comment above).
         */
	if (x <= (unsigned long)LONG_MAX) {
		return (long)x * sign;
	}
	else if (sign < 0 && x == PY_ABS_LONG_MIN) {
		return LONG_MIN;
	}
	/* else overflow */

 overflow:
	PyErr_SetString(PyExc_OverflowError,
			"long int too large to convert to int");
	return -1;
}

/* Get a Py_ssize_t from a long int object.
   Returns -1 and sets an error condition if overflow occurs. */

Py_ssize_t
_PyLong_AsSsize_t(PyObject *vv) {
	register PyLongObject *v;
	size_t x, prev;
	Py_ssize_t i;
	int sign;

	if (vv == NULL || !PyLong_Check(vv)) {
		PyErr_BadInternalCall();
		return -1;
	}
	v = (PyLongObject *)vv;
	i = v->ob_size;
	sign = 1;
	x = 0;
	if (i < 0) {
		sign = -1;
		i = -(i);
	}
	while (--i >= 0) {
		prev = x;
		x = (x << SHIFT) + v->ob_digit[i];
		if ((x >> SHIFT) != prev)
			goto overflow;
	}
	/* Haven't lost any bits, but casting to a signed type requires
	 * extra care (see comment above).
	 */
	if (x <= (size_t)PY_SSIZE_T_MAX) {
		return (Py_ssize_t)x * sign;
	}
	else if (sign < 0 && x == PY_ABS_SSIZE_T_MIN) {
		return PY_SSIZE_T_MIN;
	}
	/* else overflow */

 overflow:
	PyErr_SetString(PyExc_OverflowError,
			"long int too large to convert to int");
	return -1;
}

/* Get a C unsigned long int from a long int object.
   Returns -1 and sets an error condition if overflow occurs. */

unsigned long
PyLong_AsUnsignedLong(PyObject *vv)
{
	register PyLongObject *v;
	unsigned long x, prev;
	Py_ssize_t i;

	if (vv == NULL || !PyLong_Check(vv)) {
		if (vv != NULL && PyInt_Check(vv)) {
			long val = PyInt_AsLong(vv);
			if (val < 0) {
				PyErr_SetString(PyExc_OverflowError,
				"can't convert negative value to unsigned long");
				return (unsigned long) -1;
			}
			return val;
		}
		PyErr_BadInternalCall();
		return (unsigned long) -1;
	}
	v = (PyLongObject *)vv;
	i = v->ob_size;
	x = 0;
	if (i < 0) {
		PyErr_SetString(PyExc_OverflowError,
			   "can't convert negative value to unsigned long");
		return (unsigned long) -1;
	}
	while (--i >= 0) {
		prev = x;
		x = (x << SHIFT) + v->ob_digit[i];
		if ((x >> SHIFT) != prev) {
			PyErr_SetString(PyExc_OverflowError,
				"long int too large to convert");
			return (unsigned long) -1;
		}
	}
	return x;
}

/* Get a C unsigned long int from a long int object, ignoring the high bits.
   Returns -1 and sets an error condition if an error occurs. */

unsigned long
PyLong_AsUnsignedLongMask(PyObject *vv)
{
	register PyLongObject *v;
	unsigned long x;
	Py_ssize_t i;
	int sign;

	if (vv == NULL || !PyLong_Check(vv)) {
		if (vv != NULL && PyInt_Check(vv))
			return PyInt_AsUnsignedLongMask(vv);
		PyErr_BadInternalCall();
		return (unsigned long) -1;
	}
	v = (PyLongObject *)vv;
	i = v->ob_size;
	sign = 1;
	x = 0;
	if (i < 0) {
		sign = -1;
		i = -i;
	}
	while (--i >= 0) {
		x = (x << SHIFT) + v->ob_digit[i];
	}
	return x * sign;
}

int
_PyLong_Sign(PyObject *vv)
{
	PyLongObject *v = (PyLongObject *)vv;

	assert(v != NULL);
	assert(PyLong_Check(v));

	return v->ob_size == 0 ? 0 : (v->ob_size < 0 ? -1 : 1);
}

size_t
_PyLong_NumBits(PyObject *vv)
{
	PyLongObject *v = (PyLongObject *)vv;
	size_t result = 0;
	Py_ssize_t ndigits;

	assert(v != NULL);
	assert(PyLong_Check(v));
	ndigits = ABS(v->ob_size);
	assert(ndigits == 0 || v->ob_digit[ndigits - 1] != 0);
	if (ndigits > 0) {
		digit msd = v->ob_digit[ndigits - 1];

		result = (ndigits - 1) * SHIFT;
		if (result / SHIFT != (size_t)(ndigits - 1))
			goto Overflow;
		do {
			++result;
			if (result == 0)
				goto Overflow;
			msd >>= 1;
		} while (msd);
	}
	return result;

Overflow:
	PyErr_SetString(PyExc_OverflowError, "long has too many bits "
			"to express in a platform size_t");
	return (size_t)-1;
}

PyObject *
_PyLong_FromByteArray(const unsigned char* bytes, size_t n,
		      int little_endian, int is_signed)
{
	const unsigned char* pstartbyte;/* LSB of bytes */
	int incr;			/* direction to move pstartbyte */
	const unsigned char* pendbyte;	/* MSB of bytes */
	size_t numsignificantbytes;	/* number of bytes that matter */
	size_t ndigits;			/* number of Python long digits */
	PyLongObject* v;		/* result */
	int idigit = 0;  		/* next free index in v->ob_digit */

	if (n == 0)
		return PyLong_FromLong(0L);

	if (little_endian) {
		pstartbyte = bytes;
		pendbyte = bytes + n - 1;
		incr = 1;
	}
	else {
		pstartbyte = bytes + n - 1;
		pendbyte = bytes;
		incr = -1;
	}

	if (is_signed)
		is_signed = *pendbyte >= 0x80;

	/* Compute numsignificantbytes.  This consists of finding the most
	   significant byte.  Leading 0 bytes are insignficant if the number
	   is positive, and leading 0xff bytes if negative. */
	{
		size_t i;
		const unsigned char* p = pendbyte;
		const int pincr = -incr;  /* search MSB to LSB */
		const unsigned char insignficant = is_signed ? 0xff : 0x00;

		for (i = 0; i < n; ++i, p += pincr) {
			if (*p != insignficant)
				break;
		}
		numsignificantbytes = n - i;
		/* 2's-comp is a bit tricky here, e.g. 0xff00 == -0x0100, so
		   actually has 2 significant bytes.  OTOH, 0xff0001 ==
		   -0x00ffff, so we wouldn't *need* to bump it there; but we
		   do for 0xffff = -0x0001.  To be safe without bothering to
		   check every case, bump it regardless. */
		if (is_signed && numsignificantbytes < n)
			++numsignificantbytes;
	}

	/* How many Python long digits do we need?  We have
	   8*numsignificantbytes bits, and each Python long digit has SHIFT
	   bits, so it's the ceiling of the quotient. */
	ndigits = (numsignificantbytes * 8 + SHIFT - 1) / SHIFT;
	if (ndigits > (size_t)INT_MAX)
		return PyErr_NoMemory();
	v = _PyLong_New((int)ndigits);
	if (v == NULL)
		return NULL;

	/* Copy the bits over.  The tricky parts are computing 2's-comp on
	   the fly for signed numbers, and dealing with the mismatch between
	   8-bit bytes and (probably) 15-bit Python digits.*/
	{
		size_t i;
		twodigits carry = 1;		/* for 2's-comp calculation */
		twodigits accum = 0;		/* sliding register */
		unsigned int accumbits = 0; 	/* number of bits in accum */
		const unsigned char* p = pstartbyte;

		for (i = 0; i < numsignificantbytes; ++i, p += incr) {
			twodigits thisbyte = *p;
			/* Compute correction for 2's comp, if needed. */
			if (is_signed) {
				thisbyte = (0xff ^ thisbyte) + carry;
				carry = thisbyte >> 8;
				thisbyte &= 0xff;
			}
			/* Because we're going LSB to MSB, thisbyte is
			   more significant than what's already in accum,
			   so needs to be prepended to accum. */
			accum |= thisbyte << accumbits;
			accumbits += 8;
			if (accumbits >= SHIFT) {
				/* There's enough to fill a Python digit. */
				assert(idigit < (int)ndigits);
				v->ob_digit[idigit] = (digit)(accum & MASK);
				++idigit;
				accum >>= SHIFT;
				accumbits -= SHIFT;
				assert(accumbits < SHIFT);
			}
		}
		assert(accumbits < SHIFT);
		if (accumbits) {
			assert(idigit < (int)ndigits);
			v->ob_digit[idigit] = (digit)accum;
			++idigit;
		}
	}

	v->ob_size = is_signed ? -idigit : idigit;
	return (PyObject *)long_normalize(v);
}

int
_PyLong_AsByteArray(PyLongObject* v,
		    unsigned char* bytes, size_t n,
		    int little_endian, int is_signed)
{
	int i;			/* index into v->ob_digit */
	Py_ssize_t ndigits;		/* |v->ob_size| */
	twodigits accum;	/* sliding register */
	unsigned int accumbits; /* # bits in accum */
	int do_twos_comp;	/* store 2's-comp?  is_signed and v < 0 */
	twodigits carry;	/* for computing 2's-comp */
	size_t j;		/* # bytes filled */
	unsigned char* p;	/* pointer to next byte in bytes */
	int pincr;		/* direction to move p */

	assert(v != NULL && PyLong_Check(v));

	if (v->ob_size < 0) {
		ndigits = -(v->ob_size);
		if (!is_signed) {
			PyErr_SetString(PyExc_TypeError,
				"can't convert negative long to unsigned");
			return -1;
		}
		do_twos_comp = 1;
	}
	else {
		ndigits = v->ob_size;
		do_twos_comp = 0;
	}

	if (little_endian) {
		p = bytes;
		pincr = 1;
	}
	else {
		p = bytes + n - 1;
		pincr = -1;
	}

	/* Copy over all the Python digits.
	   It's crucial that every Python digit except for the MSD contribute
	   exactly SHIFT bits to the total, so first assert that the long is
	   normalized. */
	assert(ndigits == 0 || v->ob_digit[ndigits - 1] != 0);
	j = 0;
	accum = 0;
	accumbits = 0;
	carry = do_twos_comp ? 1 : 0;
	for (i = 0; i < ndigits; ++i) {
		twodigits thisdigit = v->ob_digit[i];
		if (do_twos_comp) {
			thisdigit = (thisdigit ^ MASK) + carry;
			carry = thisdigit >> SHIFT;
			thisdigit &= MASK;
		}
		/* Because we're going LSB to MSB, thisdigit is more
		   significant than what's already in accum, so needs to be
		   prepended to accum. */
		accum |= thisdigit << accumbits;
		accumbits += SHIFT;

		/* The most-significant digit may be (probably is) at least
		   partly empty. */
		if (i == ndigits - 1) {
			/* Count # of sign bits -- they needn't be stored,
			 * although for signed conversion we need later to
			 * make sure at least one sign bit gets stored.
			 * First shift conceptual sign bit to real sign bit.
			 */
			stwodigits s = (stwodigits)(thisdigit <<
				(8*sizeof(stwodigits) - SHIFT));
			unsigned int nsignbits = 0;
			while ((s < 0) == do_twos_comp && nsignbits < SHIFT) {
				++nsignbits;
				s <<= 1;
			}
			accumbits -= nsignbits;
		}

		/* Store as many bytes as possible. */
		while (accumbits >= 8) {
			if (j >= n)
				goto Overflow;
			++j;
			*p = (unsigned char)(accum & 0xff);
			p += pincr;
			accumbits -= 8;
			accum >>= 8;
		}
	}

	/* Store the straggler (if any). */
	assert(accumbits < 8);
	assert(carry == 0);  /* else do_twos_comp and *every* digit was 0 */
	if (accumbits > 0) {
		if (j >= n)
			goto Overflow;
		++j;
		if (do_twos_comp) {
			/* Fill leading bits of the byte with sign bits
			   (appropriately pretending that the long had an
			   infinite supply of sign bits). */
			accum |= (~(twodigits)0) << accumbits;
		}
		*p = (unsigned char)(accum & 0xff);
		p += pincr;
	}
	else if (j == n && n > 0 && is_signed) {
		/* The main loop filled the byte array exactly, so the code
		   just above didn't get to ensure there's a sign bit, and the
		   loop below wouldn't add one either.  Make sure a sign bit
		   exists. */
		unsigned char msb = *(p - pincr);
		int sign_bit_set = msb >= 0x80;
		assert(accumbits == 0);
		if (sign_bit_set == do_twos_comp)
			return 0;
		else
			goto Overflow;
	}

	/* Fill remaining bytes with copies of the sign bit. */
	{
		unsigned char signbyte = do_twos_comp ? 0xffU : 0U;
		for ( ; j < n; ++j, p += pincr)
			*p = signbyte;
	}

	return 0;

Overflow:
	PyErr_SetString(PyExc_OverflowError, "long too big to convert");
	return -1;

}

double
_PyLong_AsScaledDouble(PyObject *vv, int *exponent)
{
/* NBITS_WANTED should be > the number of bits in a double's precision,
   but small enough so that 2**NBITS_WANTED is within the normal double
   range.  nbitsneeded is set to 1 less than that because the most-significant
   Python digit contains at least 1 significant bit, but we don't want to
   bother counting them (catering to the worst case cheaply).

   57 is one more than VAX-D double precision; I (Tim) don't know of a double
   format with more precision than that; it's 1 larger so that we add in at
   least one round bit to stand in for the ignored least-significant bits.
*/
#define NBITS_WANTED 57
	PyLongObject *v;
	double x;
	const double multiplier = (double)(1L << SHIFT);
	Py_ssize_t i;
	int sign;
	int nbitsneeded;

	if (vv == NULL || !PyLong_Check(vv)) {
		PyErr_BadInternalCall();
		return -1;
	}
	v = (PyLongObject *)vv;
	i = v->ob_size;
	sign = 1;
	if (i < 0) {
		sign = -1;
		i = -(i);
	}
	else if (i == 0) {
		*exponent = 0;
		return 0.0;
	}
	--i;
	x = (double)v->ob_digit[i];
	nbitsneeded = NBITS_WANTED - 1;
	/* Invariant:  i Python digits remain unaccounted for. */
	while (i > 0 && nbitsneeded > 0) {
		--i;
		x = x * multiplier + (double)v->ob_digit[i];
		nbitsneeded -= SHIFT;
	}
	/* There are i digits we didn't shift in.  Pretending they're all
	   zeroes, the true value is x * 2**(i*SHIFT). */
	*exponent = i;
	assert(x > 0.0);
	return x * sign;
#undef NBITS_WANTED
}

/* Get a C double from a long int object. */

double
PyLong_AsDouble(PyObject *vv)
{
	int e = -1;
	double x;

	if (vv == NULL || !PyLong_Check(vv)) {
		PyErr_BadInternalCall();
		return -1;
	}
	x = _PyLong_AsScaledDouble(vv, &e);
	if (x == -1.0 && PyErr_Occurred())
		return -1.0;
	/* 'e' initialized to -1 to silence gcc-4.0.x, but it should be
	   set correctly after a successful _PyLong_AsScaledDouble() call */
	assert(e >= 0);
	if (e > INT_MAX / SHIFT)
		goto overflow;
	errno = 0;
	x = ldexp(x, e * SHIFT);
	if (Py_OVERFLOWED(x))
		goto overflow;
	return x;

overflow:
	PyErr_SetString(PyExc_OverflowError,
		"long int too large to convert to float");
	return -1.0;
}

/* Create a new long (or int) object from a C pointer */

PyObject *
PyLong_FromVoidPtr(void *p)
{
#if SIZEOF_VOID_P <= SIZEOF_LONG
	if ((long)p < 0)
		return PyLong_FromUnsignedLong((unsigned long)p);
	return PyInt_FromLong((long)p);
#else

#ifndef HAVE_LONG_LONG
#   error "PyLong_FromVoidPtr: sizeof(void*) > sizeof(long), but no long long"
#endif
#if SIZEOF_LONG_LONG < SIZEOF_VOID_P
#   error "PyLong_FromVoidPtr: sizeof(PY_LONG_LONG) < sizeof(void*)"
#endif
	/* optimize null pointers */
	if (p == NULL)
		return PyInt_FromLong(0);
	return PyLong_FromUnsignedLongLong((unsigned PY_LONG_LONG)p);

#endif /* SIZEOF_VOID_P <= SIZEOF_LONG */
}

/* Get a C pointer from a long object (or an int object in some cases) */

void *
PyLong_AsVoidPtr(PyObject *vv)
{
	/* This function will allow int or long objects. If vv is neither,
	   then the PyLong_AsLong*() functions will raise the exception:
	   PyExc_SystemError, "bad argument to internal function"
	*/
#if SIZEOF_VOID_P <= SIZEOF_LONG
	long x;

	if (PyInt_Check(vv))
		x = PyInt_AS_LONG(vv);
	else if (PyLong_Check(vv) && _PyLong_Sign(vv) < 0)
		x = PyLong_AsLong(vv);
	else
		x = PyLong_AsUnsignedLong(vv);
#else

#ifndef HAVE_LONG_LONG
#   error "PyLong_AsVoidPtr: sizeof(void*) > sizeof(long), but no long long"
#endif
#if SIZEOF_LONG_LONG < SIZEOF_VOID_P
#   error "PyLong_AsVoidPtr: sizeof(PY_LONG_LONG) < sizeof(void*)"
#endif
	PY_LONG_LONG x;

	if (PyInt_Check(vv))
		x = PyInt_AS_LONG(vv);
	else if (PyLong_Check(vv) && _PyLong_Sign(vv) < 0)
		x = PyLong_AsLongLong(vv);
	else
		x = PyLong_AsUnsignedLongLong(vv);

#endif /* SIZEOF_VOID_P <= SIZEOF_LONG */

	if (x == -1 && PyErr_Occurred())
		return NULL;
	return (void *)x;
}

#ifdef HAVE_LONG_LONG

/* Initial PY_LONG_LONG support by Chris Herborth (chrish@qnx.com), later
 * rewritten to use the newer PyLong_{As,From}ByteArray API.
 */

#define IS_LITTLE_ENDIAN (int)*(unsigned char*)&one

/* Create a new long int object from a C PY_LONG_LONG int. */

PyObject *
PyLong_FromLongLong(PY_LONG_LONG ival)
{
	PyLongObject *v;
	unsigned PY_LONG_LONG t;  /* unsigned so >> doesn't propagate sign bit */
	int ndigits = 0;
	int negative = 0;

	if (ival < 0) {
		ival = -ival;
		negative = 1;
	}

	/* Count the number of Python digits.
	   We used to pick 5 ("big enough for anything"), but that's a
	   waste of time and space given that 5*15 = 75 bits are rarely
	   needed. */
	t = (unsigned PY_LONG_LONG)ival;
	while (t) {
		++ndigits;
		t >>= SHIFT;
	}
	v = _PyLong_New(ndigits);
	if (v != NULL) {
		digit *p = v->ob_digit;
		v->ob_size = negative ? -ndigits : ndigits;
		t = (unsigned PY_LONG_LONG)ival;
		while (t) {
			*p++ = (digit)(t & MASK);
			t >>= SHIFT;
		}
	}
	return (PyObject *)v;
}

/* Create a new long int object from a C unsigned PY_LONG_LONG int. */

PyObject *
PyLong_FromUnsignedLongLong(unsigned PY_LONG_LONG ival)
{
	PyLongObject *v;
	unsigned PY_LONG_LONG t;
	int ndigits = 0;

	/* Count the number of Python digits. */
	t = (unsigned PY_LONG_LONG)ival;
	while (t) {
		++ndigits;
		t >>= SHIFT;
	}
	v = _PyLong_New(ndigits);
	if (v != NULL) {
		digit *p = v->ob_digit;
		v->ob_size = ndigits;
		while (ival) {
			*p++ = (digit)(ival & MASK);
			ival >>= SHIFT;
		}
	}
	return (PyObject *)v;
}

/* Create a new long int object from a C Py_ssize_t. */

PyObject *
_PyLong_FromSsize_t(Py_ssize_t ival)
{
	Py_ssize_t bytes = ival;
	int one = 1;
	return _PyLong_FromByteArray(
			(unsigned char *)&bytes,
			SIZEOF_SIZE_T, IS_LITTLE_ENDIAN, 0);
}

/* Create a new long int object from a C size_t. */

PyObject *
_PyLong_FromSize_t(size_t ival)
{
	size_t bytes = ival;
	int one = 1;
	return _PyLong_FromByteArray(
			(unsigned char *)&bytes,
			SIZEOF_SIZE_T, IS_LITTLE_ENDIAN, 0);
}

/* Get a C PY_LONG_LONG int from a long int object.
   Return -1 and set an error if overflow occurs. */

PY_LONG_LONG
PyLong_AsLongLong(PyObject *vv)
{
	PY_LONG_LONG bytes;
	int one = 1;
	int res;

	if (vv == NULL) {
		PyErr_BadInternalCall();
		return -1;
	}
	if (!PyLong_Check(vv)) {
		PyNumberMethods *nb;
		PyObject *io;
		if (PyInt_Check(vv))
			return (PY_LONG_LONG)PyInt_AsLong(vv);
		if ((nb = vv->ob_type->tp_as_number) == NULL ||
		    nb->nb_int == NULL) {
			PyErr_SetString(PyExc_TypeError, "an integer is required");
			return -1;
		}
		io = (*nb->nb_int) (vv);
		if (io == NULL)
			return -1;
		if (PyInt_Check(io)) {
			bytes = PyInt_AsLong(io);
			Py_DECREF(io);
			return bytes;
		}
		if (PyLong_Check(io)) {
			bytes = PyLong_AsLongLong(io);
			Py_DECREF(io);
			return bytes;
		}
		Py_DECREF(io);
		PyErr_SetString(PyExc_TypeError, "integer conversion failed");
		return -1;
	}

	res = _PyLong_AsByteArray(
			(PyLongObject *)vv, (unsigned char *)&bytes,
			SIZEOF_LONG_LONG, IS_LITTLE_ENDIAN, 1);

	/* Plan 9 can't handle PY_LONG_LONG in ? : expressions */
	if (res < 0)
		return (PY_LONG_LONG)-1;
	else
		return bytes;
}

/* Get a C unsigned PY_LONG_LONG int from a long int object.
   Return -1 and set an error if overflow occurs. */

unsigned PY_LONG_LONG
PyLong_AsUnsignedLongLong(PyObject *vv)
{
	unsigned PY_LONG_LONG bytes;
	int one = 1;
	int res;

	if (vv == NULL || !PyLong_Check(vv)) {
		PyErr_BadInternalCall();
		return (unsigned PY_LONG_LONG)-1;
	}

	res = _PyLong_AsByteArray(
			(PyLongObject *)vv, (unsigned char *)&bytes,
			SIZEOF_LONG_LONG, IS_LITTLE_ENDIAN, 0);

	/* Plan 9 can't handle PY_LONG_LONG in ? : expressions */
	if (res < 0)
		return (unsigned PY_LONG_LONG)res;
	else
		return bytes;
}

/* Get a C unsigned long int from a long int object, ignoring the high bits.
   Returns -1 and sets an error condition if an error occurs. */

unsigned PY_LONG_LONG
PyLong_AsUnsignedLongLongMask(PyObject *vv)
{
	register PyLongObject *v;
	unsigned PY_LONG_LONG x;
	Py_ssize_t i;
	int sign;

	if (vv == NULL || !PyLong_Check(vv)) {
		PyErr_BadInternalCall();
		return (unsigned long) -1;
	}
	v = (PyLongObject *)vv;
	i = v->ob_size;
	sign = 1;
	x = 0;
	if (i < 0) {
		sign = -1;
		i = -i;
	}
	while (--i >= 0) {
		x = (x << SHIFT) + v->ob_digit[i];
	}
	return x * sign;
}
#undef IS_LITTLE_ENDIAN

#endif /* HAVE_LONG_LONG */


static int
convert_binop(PyObject *v, PyObject *w, PyLongObject **a, PyLongObject **b) {
	if (PyLong_Check(v)) {
		*a = (PyLongObject *) v;
		Py_INCREF(v);
	}
	else if (PyInt_Check(v)) {
		*a = (PyLongObject *) PyLong_FromLong(PyInt_AS_LONG(v));
	}
	else {
		return 0;
	}
	if (PyLong_Check(w)) {
		*b = (PyLongObject *) w;
		Py_INCREF(w);
	}
	else if (PyInt_Check(w)) {
		*b = (PyLongObject *) PyLong_FromLong(PyInt_AS_LONG(w));
	}
	else {
		Py_DECREF(*a);
		return 0;
	}
	return 1;
}

#define CONVERT_BINOP(v, w, a, b) \
	if (!convert_binop(v, w, a, b)) { \
		Py_INCREF(Py_NotImplemented); \
		return Py_NotImplemented; \
	}

/* x[0:m] and y[0:n] are digit vectors, LSD first, m >= n required.  x[0:n]
 * is modified in place, by adding y to it.  Carries are propagated as far as
 * x[m-1], and the remaining carry (0 or 1) is returned.
 */
static digit
v_iadd(digit *x, Py_ssize_t m, digit *y, Py_ssize_t n)
{
	int i;
	digit carry = 0;

	assert(m >= n);
	for (i = 0; i < n; ++i) {
		carry += x[i] + y[i];
		x[i] = carry & MASK;
		carry >>= SHIFT;
		assert((carry & 1) == carry);
	}
	for (; carry && i < m; ++i) {
		carry += x[i];
		x[i] = carry & MASK;
		carry >>= SHIFT;
		assert((carry & 1) == carry);
	}
	return carry;
}

/* x[0:m] and y[0:n] are digit vectors, LSD first, m >= n required.  x[0:n]
 * is modified in place, by subtracting y from it.  Borrows are propagated as
 * far as x[m-1], and the remaining borrow (0 or 1) is returned.
 */
static digit
v_isub(digit *x, Py_ssize_t m, digit *y, Py_ssize_t n)
{
	int i;
	digit borrow = 0;

	assert(m >= n);
	for (i = 0; i < n; ++i) {
		borrow = x[i] - y[i] - borrow;
		x[i] = borrow & MASK;
		borrow >>= SHIFT;
		borrow &= 1;	/* keep only 1 sign bit */
	}
	for (; borrow && i < m; ++i) {
		borrow = x[i] - borrow;
		x[i] = borrow & MASK;
		borrow >>= SHIFT;
		borrow &= 1;
	}
	return borrow;
}

/* Multiply by a single digit, ignoring the sign. */

static PyLongObject *
mul1(PyLongObject *a, wdigit n)
{
	return muladd1(a, n, (digit)0);
}

/* Multiply by a single digit and add a single digit, ignoring the sign. */

static PyLongObject *
muladd1(PyLongObject *a, wdigit n, wdigit extra)
{
	Py_ssize_t size_a = ABS(a->ob_size);
	PyLongObject *z = _PyLong_New(size_a+1);
	twodigits carry = extra;
	Py_ssize_t i;

	if (z == NULL)
		return NULL;
	for (i = 0; i < size_a; ++i) {
		carry += (twodigits)a->ob_digit[i] * n;
		z->ob_digit[i] = (digit) (carry & MASK);
		carry >>= SHIFT;
	}
	z->ob_digit[i] = (digit) carry;
	return long_normalize(z);
}

/* Divide long pin, w/ size digits, by non-zero digit n, storing quotient
   in pout, and returning the remainder.  pin and pout point at the LSD.
   It's OK for pin == pout on entry, which saves oodles of mallocs/frees in
   long_format, but that should be done with great care since longs are
   immutable. */

static digit
inplace_divrem1(digit *pout, digit *pin, Py_ssize_t size, digit n)
{
	twodigits rem = 0;

	assert(n > 0 && n <= MASK);
	pin += size;
	pout += size;
	while (--size >= 0) {
		digit hi;
		rem = (rem << SHIFT) + *--pin;
		*--pout = hi = (digit)(rem / n);
		rem -= hi * n;
	}
	return (digit)rem;
}

/* Divide a long integer by a digit, returning both the quotient
   (as function result) and the remainder (through *prem).
   The sign of a is ignored; n should not be zero. */

static PyLongObject *
divrem1(PyLongObject *a, digit n, digit *prem)
{
	const Py_ssize_t size = ABS(a->ob_size);
	PyLongObject *z;

	assert(n > 0 && n <= MASK);
	z = _PyLong_New(size);
	if (z == NULL)
		return NULL;
	*prem = inplace_divrem1(z->ob_digit, a->ob_digit, size, n);
	return long_normalize(z);
}

/* Convert a long int object to a string, using a given conversion base.
   Return a string object.
   If base is 8 or 16, add the proper prefix '0' or '0x'. */

static PyObject *
long_format(PyObject *aa, int base, int addL)
{
	register PyLongObject *a = (PyLongObject *)aa;
	PyStringObject *str;
	Py_ssize_t i, j, sz;
	Py_ssize_t size_a;
	char *p;
	int bits;
	char sign = '\0';

	if (a == NULL || !PyLong_Check(a)) {
		PyErr_BadInternalCall();
		return NULL;
	}
	assert(base >= 2 && base <= 36);
	size_a = ABS(a->ob_size);

	/* Compute a rough upper bound for the length of the string */
	i = base;
	bits = 0;
	while (i > 1) {
		++bits;
		i >>= 1;
	}
	i = 5 + (addL ? 1 : 0);
	j = size_a*SHIFT + bits-1;
	sz = i + j / bits;
	if (j / SHIFT < size_a || sz < i) {
		PyErr_SetString(PyExc_OverflowError,
				"long is too large to format");
		return NULL;
	}
	str = (PyStringObject *) PyString_FromStringAndSize((char *)0, sz);
	if (str == NULL)
		return NULL;
	p = PyString_AS_STRING(str) + sz;
	*p = '\0';
        if (addL)
                *--p = 'L';
	if (a->ob_size < 0)
		sign = '-';

	if (a->ob_size == 0) {
		*--p = '0';
	}
	else if ((base & (base - 1)) == 0) {
		/* JRH: special case for power-of-2 bases */
		twodigits accum = 0;
		int accumbits = 0;	/* # of bits in accum */
		int basebits = 1;	/* # of bits in base-1 */
		i = base;
		while ((i >>= 1) > 1)
			++basebits;

		for (i = 0; i < size_a; ++i) {
			accum |= (twodigits)a->ob_digit[i] << accumbits;
			accumbits += SHIFT;
			assert(accumbits >= basebits);
			do {
				char cdigit = (char)(accum & (base - 1));
				cdigit += (cdigit < 10) ? '0' : 'a'-10;
				assert(p > PyString_AS_STRING(str));
				*--p = cdigit;
				accumbits -= basebits;
				accum >>= basebits;
			} while (i < size_a-1 ? accumbits >= basebits :
					 	accum > 0);
		}
	}
	else {
		/* Not 0, and base not a power of 2.  Divide repeatedly by
		   base, but for speed use the highest power of base that
		   fits in a digit. */
		Py_ssize_t size = size_a;
		digit *pin = a->ob_digit;
		PyLongObject *scratch;
		/* powbasw <- largest power of base that fits in a digit. */
		digit powbase = base;  /* powbase == base ** power */
		int power = 1;
		for (;;) {
			unsigned long newpow = powbase * (unsigned long)base;
			if (newpow >> SHIFT)  /* doesn't fit in a digit */
				break;
			powbase = (digit)newpow;
			++power;
		}

		/* Get a scratch area for repeated division. */
		scratch = _PyLong_New(size);
		if (scratch == NULL) {
			Py_DECREF(str);
			return NULL;
		}

		/* Repeatedly divide by powbase. */
		do {
			int ntostore = power;
			digit rem = inplace_divrem1(scratch->ob_digit,
						     pin, size, powbase);
			pin = scratch->ob_digit; /* no need to use a again */
			if (pin[size - 1] == 0)
				--size;
			SIGCHECK({
				Py_DECREF(scratch);
				Py_DECREF(str);
				return NULL;
			})

			/* Break rem into digits. */
			assert(ntostore > 0);
			do {
				digit nextrem = (digit)(rem / base);
				char c = (char)(rem - nextrem * base);
				assert(p > PyString_AS_STRING(str));
				c += (c < 10) ? '0' : 'a'-10;
				*--p = c;
				rem = nextrem;
				--ntostore;
				/* Termination is a bit delicate:  must not
				   store leading zeroes, so must get out if
				   remaining quotient and rem are both 0. */
			} while (ntostore && (size || rem));
		} while (size != 0);
		Py_DECREF(scratch);
	}

	if (base == 8) {
		if (size_a != 0)
			*--p = '0';
	}
	else if (base == 16) {
		*--p = 'x';
		*--p = '0';
	}
	else if (base != 10) {
		*--p = '#';
		*--p = '0' + base%10;
		if (base > 10)
			*--p = '0' + base/10;
	}
	if (sign)
		*--p = sign;
	if (p != PyString_AS_STRING(str)) {
		char *q = PyString_AS_STRING(str);
		assert(p > q);
		do {
		} while ((*q++ = *p++) != '\0');
		q--;
		_PyString_Resize((PyObject **)&str,
				 (Py_ssize_t) (q - PyString_AS_STRING(str)));
	}
	return (PyObject *)str;
}

/* Table of digit values for 8-bit string -> integer conversion.
 * '0' maps to 0, ..., '9' maps to 9.
 * 'a' and 'A' map to 10, ..., 'z' and 'Z' map to 35.
 * All other indices map to 37.
 * Note that when converting a base B string, a char c is a legitimate
 * base B digit iff _PyLong_DigitValue[Py_CHARMASK(c)] < B.
 */
int _PyLong_DigitValue[256] = {
	37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37,
	37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37,
	37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37,
	0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  37, 37, 37, 37, 37, 37,
	37, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
	25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 37, 37, 37, 37, 37,
	37, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
	25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 37, 37, 37, 37, 37,
	37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37,
	37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37,
	37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37,
	37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37,
	37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37,
	37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37,
	37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37,
	37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37,
};

/* *str points to the first digit in a string of base `base` digits.  base
 * is a power of 2 (2, 4, 8, 16, or 32).  *str is set to point to the first
 * non-digit (which may be *str!).  A normalized long is returned.
 * The point to this routine is that it takes time linear in the number of
 * string characters.
 */
static PyLongObject *
long_from_binary_base(char **str, int base)
{
	char *p = *str;
	char *start = p;
	int bits_per_char;
	Py_ssize_t n;
	PyLongObject *z;
	twodigits accum;
	int bits_in_accum;
	digit *pdigit;

	assert(base >= 2 && base <= 32 && (base & (base - 1)) == 0);
	n = base;
	for (bits_per_char = -1; n; ++bits_per_char)
		n >>= 1;
	/* n <- total # of bits needed, while setting p to end-of-string */
	n = 0;
	while (_PyLong_DigitValue[Py_CHARMASK(*p)] < base)
		++p;
	*str = p;
	/* n <- # of Python digits needed, = ceiling(n/SHIFT). */
	n = (p - start) * bits_per_char + SHIFT - 1;
	if (n / bits_per_char < p - start) {
		PyErr_SetString(PyExc_ValueError,
				"long string too large to convert");
		return NULL;
	}
	n = n / SHIFT;
	z = _PyLong_New(n);
	if (z == NULL)
		return NULL;
	/* Read string from right, and fill in long from left; i.e.,
	 * from least to most significant in both.
	 */
	accum = 0;
	bits_in_accum = 0;
	pdigit = z->ob_digit;
	while (--p >= start) {
		int k = _PyLong_DigitValue[Py_CHARMASK(*p)];
		assert(k >= 0 && k < base);
		accum |= (twodigits)(k << bits_in_accum);
		bits_in_accum += bits_per_char;
		if (bits_in_accum >= SHIFT) {
			*pdigit++ = (digit)(accum & MASK);
			assert(pdigit - z->ob_digit <= (int)n);
			accum >>= SHIFT;
			bits_in_accum -= SHIFT;
			assert(bits_in_accum < SHIFT);
		}
	}
	if (bits_in_accum) {
		assert(bits_in_accum <= SHIFT);
		*pdigit++ = (digit)accum;
		assert(pdigit - z->ob_digit <= (int)n);
	}
	while (pdigit - z->ob_digit < n)
		*pdigit++ = 0;
	return long_normalize(z);
}

PyObject *
PyLong_FromString(char *str, char **pend, int base)
{
	int sign = 1;
	char *start, *orig_str = str;
	PyLongObject *z;
	PyObject *strobj, *strrepr;
	Py_ssize_t slen;

	if ((base != 0 && base < 2) || base > 36) {
		PyErr_SetString(PyExc_ValueError,
				"long() arg 2 must be >= 2 and <= 36");
		return NULL;
	}
	while (*str != '\0' && isspace(Py_CHARMASK(*str)))
		str++;
	if (*str == '+')
		++str;
	else if (*str == '-') {
		++str;
		sign = -1;
	}
	while (*str != '\0' && isspace(Py_CHARMASK(*str)))
		str++;
	if (base == 0) {
		if (str[0] != '0')
			base = 10;
		else if (str[1] == 'x' || str[1] == 'X')
			base = 16;
		else
			base = 8;
	}
	if (base == 16 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X'))
		str += 2;

	start = str;
	if ((base & (base - 1)) == 0)
		z = long_from_binary_base(&str, base);
	else {
/***
Binary bases can be converted in time linear in the number of digits, because
Python's representation base is binary.  Other bases (including decimal!) use
the simple quadratic-time algorithm below, complicated by some speed tricks.

First some math:  the largest integer that can be expressed in N base-B digits
is B**N-1.  Consequently, if we have an N-digit input in base B, the worst-
case number of Python digits needed to hold it is the smallest integer n s.t.

    BASE**n-1 >= B**N-1  [or, adding 1 to both sides]
    BASE**n >= B**N      [taking logs to base BASE]
    n >= log(B**N)/log(BASE) = N * log(B)/log(BASE)

The static array log_base_BASE[base] == log(base)/log(BASE) so we can compute
this quickly.  A Python long with that much space is reserved near the start,
and the result is computed into it.

The input string is actually treated as being in base base**i (i.e., i digits
are processed at a time), where two more static arrays hold:

    convwidth_base[base] = the largest integer i such that base**i <= BASE
    convmultmax_base[base] = base ** convwidth_base[base]

The first of these is the largest i such that i consecutive input digits
must fit in a single Python digit.  The second is effectively the input
base we're really using.

Viewing the input as a sequence <c0, c1, ..., c_n-1> of digits in base
convmultmax_base[base], the result is "simply"

   (((c0*B + c1)*B + c2)*B + c3)*B + ... ))) + c_n-1

where B = convmultmax_base[base].

Error analysis:  as above, the number of Python digits `n` needed is worst-
case

    n >= N * log(B)/log(BASE)

where `N` is the number of input digits in base `B`.  This is computed via

    size_z = (Py_ssize_t)((scan - str) * log_base_BASE[base]) + 1;

below.  Two numeric concerns are how much space this can waste, and whether
the computed result can be too small.  To be concrete, assume BASE = 2**15,
which is the default (and it's unlikely anyone changes that).

Waste isn't a problem:  provided the first input digit isn't 0, the difference
between the worst-case input with N digits and the smallest input with N
digits is about a factor of B, but B is small compared to BASE so at most
one allocated Python digit can remain unused on that count.  If
N*log(B)/log(BASE) is mathematically an exact integer, then truncating that
and adding 1 returns a result 1 larger than necessary.  However, that can't
happen:  whenever B is a power of 2, long_from_binary_base() is called
instead, and it's impossible for B**i to be an integer power of 2**15 when
B is not a power of 2 (i.e., it's impossible for N*log(B)/log(BASE) to be
an exact integer when B is not a power of 2, since B**i has a prime factor
other than 2 in that case, but (2**15)**j's only prime factor is 2).

The computed result can be too small if the true value of N*log(B)/log(BASE)
is a little bit larger than an exact integer, but due to roundoff errors (in
computing log(B), log(BASE), their quotient, and/or multiplying that by N)
yields a numeric result a little less than that integer.  Unfortunately, "how
close can a transcendental function get to an integer over some range?"
questions are generally theoretically intractable.  Computer analysis via
continued fractions is practical:  expand log(B)/log(BASE) via continued
fractions, giving a sequence i/j of "the best" rational approximations.  Then
j*log(B)/log(BASE) is approximately equal to (the integer) i.  This shows that
we can get very close to being in trouble, but very rarely.  For example,
76573 is a denominator in one of the continued-fraction approximations to
log(10)/log(2**15), and indeed:

    >>> log(10)/log(2**15)*76573
    16958.000000654003

is very close to an integer.  If we were working with IEEE single-precision,
rounding errors could kill us.  Finding worst cases in IEEE double-precision
requires better-than-double-precision log() functions, and Tim didn't bother.
Instead the code checks to see whether the allocated space is enough as each
new Python digit is added, and copies the whole thing to a larger long if not.
This should happen extremely rarely, and in fact I don't have a test case
that triggers it(!).  Instead the code was tested by artificially allocating
just 1 digit at the start, so that the copying code was exercised for every
digit beyond the first.
***/
		register twodigits c;	/* current input character */
		Py_ssize_t size_z;
		int i;
		int convwidth;
		twodigits convmultmax, convmult;
		digit *pz, *pzstop;
		char* scan;

		static double log_base_BASE[37] = {0.0e0,};
		static int convwidth_base[37] = {0,};
		static twodigits convmultmax_base[37] = {0,};

		if (log_base_BASE[base] == 0.0) {
			twodigits convmax = base;
			int i = 1;

			log_base_BASE[base] = log((double)base) /
						log((double)BASE);
			for (;;) {
				twodigits next = convmax * base;
				if (next > BASE)
					break;
				convmax = next;
				++i;
			}
			convmultmax_base[base] = convmax;
			assert(i > 0);
			convwidth_base[base] = i;
		}

		/* Find length of the string of numeric characters. */
		scan = str;
		while (_PyLong_DigitValue[Py_CHARMASK(*scan)] < base)
			++scan;

		/* Create a long object that can contain the largest possible
		 * integer with this base and length.  Note that there's no
		 * need to initialize z->ob_digit -- no slot is read up before
		 * being stored into.
		 */
		size_z = (Py_ssize_t)((scan - str) * log_base_BASE[base]) + 1;
		/* Uncomment next line to test exceedingly rare copy code */
		/* size_z = 1; */
		assert(size_z > 0);
		z = _PyLong_New(size_z);
		if (z == NULL)
			return NULL;
		z->ob_size = 0;

		/* `convwidth` consecutive input digits are treated as a single
		 * digit in base `convmultmax`.
		 */
		convwidth = convwidth_base[base];
		convmultmax = convmultmax_base[base];

		/* Work ;-) */
		while (str < scan) {
			/* grab up to convwidth digits from the input string */
			c = (digit)_PyLong_DigitValue[Py_CHARMASK(*str++)];
			for (i = 1; i < convwidth && str != scan; ++i, ++str) {
				c = (twodigits)(c *  base +
					_PyLong_DigitValue[Py_CHARMASK(*str)]);
				assert(c < BASE);
			}

			convmult = convmultmax;
			/* Calculate the shift only if we couldn't get
			 * convwidth digits.
			 */
			if (i != convwidth) {
				convmult = base;
				for ( ; i > 1; --i)
					convmult *= base;
			}

			/* Multiply z by convmult, and add c. */
			pz = z->ob_digit;
			pzstop = pz + z->ob_size;
			for (; pz < pzstop; ++pz) {
				c += (twodigits)*pz * convmult;
				*pz = (digit)(c & MASK);
				c >>= SHIFT;
			}
			/* carry off the current end? */
			if (c) {
				assert(c < BASE);
				if (z->ob_size < size_z) {
					*pz = (digit)c;
					++z->ob_size;
				}
				else {
					PyLongObject *tmp;
					/* Extremely rare.  Get more space. */
					assert(z->ob_size == size_z);
					tmp = _PyLong_New(size_z + 1);
					if (tmp == NULL) {
						Py_DECREF(z);
						return NULL;
					}
					memcpy(tmp->ob_digit,
					       z->ob_digit,
					       sizeof(digit) * size_z);
					Py_DECREF(z);
					z = tmp;
					z->ob_digit[size_z] = (digit)c;
					++size_z;
				}
			}
		}
	}
	if (z == NULL)
		return NULL;
	if (str == start)
		goto onError;
	if (sign < 0)
		z->ob_size = -(z->ob_size);
	if (*str == 'L' || *str == 'l')
		str++;
	while (*str && isspace(Py_CHARMASK(*str)))
		str++;
	if (*str != '\0')
		goto onError;
	if (pend)
		*pend = str;
	return (PyObject *) z;

 onError:
	Py_XDECREF(z);
	slen = strlen(orig_str) < 200 ? strlen(orig_str) : 200;
	strobj = PyString_FromStringAndSize(orig_str, slen);
	if (strobj == NULL)
		return NULL;
	strrepr = PyObject_Repr(strobj);
	Py_DECREF(strobj);
	if (strrepr == NULL)
		return NULL;
	PyErr_Format(PyExc_ValueError,
		     "invalid literal for long() with base %d: %s",
		     base, PyString_AS_STRING(strrepr));
	Py_DECREF(strrepr);
	return NULL;
}

#ifdef Py_USING_UNICODE
PyObject *
PyLong_FromUnicode(Py_UNICODE *u, Py_ssize_t length, int base)
{
	PyObject *result;
	char *buffer = (char *)PyMem_MALLOC(length+1);

	if (buffer == NULL)
		return NULL;

	if (PyUnicode_EncodeDecimal(u, length, buffer, NULL)) {
		PyMem_FREE(buffer);
		return NULL;
	}
	result = PyLong_FromString(buffer, NULL, base);
	PyMem_FREE(buffer);
	return result;
}
#endif

/* forward */
static PyLongObject *x_divrem
	(PyLongObject *, PyLongObject *, PyLongObject **);
static PyObject *long_pos(PyLongObject *);
static int long_divrem(PyLongObject *, PyLongObject *,
	PyLongObject **, PyLongObject **);

/* Long division with remainder, top-level routine */

static int
long_divrem(PyLongObject *a, PyLongObject *b,
	    PyLongObject **pdiv, PyLongObject **prem)
{
	Py_ssize_t size_a = ABS(a->ob_size), size_b = ABS(b->ob_size);
	PyLongObject *z;

	if (size_b == 0) {
		PyErr_SetString(PyExc_ZeroDivisionError,
				"long division or modulo by zero");
		return -1;
	}
	if (size_a < size_b ||
	    (size_a == size_b &&
	     a->ob_digit[size_a-1] < b->ob_digit[size_b-1])) {
		/* |a| < |b|. */
		*pdiv = _PyLong_New(0);
		Py_INCREF(a);
		*prem = (PyLongObject *) a;
		return 0;
	}
	if (size_b == 1) {
		digit rem = 0;
		z = divrem1(a, b->ob_digit[0], &rem);
		if (z == NULL)
			return -1;
		*prem = (PyLongObject *) PyLong_FromLong((long)rem);
	}
	else {
		z = x_divrem(a, b, prem);
		if (z == NULL)
			return -1;
	}
	/* Set the signs.
	   The quotient z has the sign of a*b;
	   the remainder r has the sign of a,
	   so a = b*z + r. */
	if ((a->ob_size < 0) != (b->ob_size < 0))
		z->ob_size = -(z->ob_size);
	if (a->ob_size < 0 && (*prem)->ob_size != 0)
		(*prem)->ob_size = -((*prem)->ob_size);
	*pdiv = z;
	return 0;
}

/* Unsigned long division with remainder -- the algorithm */

static PyLongObject *
x_divrem(PyLongObject *v1, PyLongObject *w1, PyLongObject **prem)
{
	Py_ssize_t size_v = ABS(v1->ob_size), size_w = ABS(w1->ob_size);
	digit d = (digit) ((twodigits)BASE / (w1->ob_digit[size_w-1] + 1));
	PyLongObject *v = mul1(v1, d);
	PyLongObject *w = mul1(w1, d);
	PyLongObject *a;
	Py_ssize_t j, k;

	if (v == NULL || w == NULL) {
		Py_XDECREF(v);
		Py_XDECREF(w);
		return NULL;
	}

	assert(size_v >= size_w && size_w > 1); /* Assert checks by div() */
	assert(v->ob_refcnt == 1); /* Since v will be used as accumulator! */
	assert(size_w == ABS(w->ob_size)); /* That's how d was calculated */

	size_v = ABS(v->ob_size);
	k = size_v - size_w;
	a = _PyLong_New(k + 1);

	for (j = size_v; a != NULL && k >= 0; --j, --k) {
		digit vj = (j >= size_v) ? 0 : v->ob_digit[j];
		twodigits q;
		stwodigits carry = 0;
		int i;

		SIGCHECK({
			Py_DECREF(a);
			a = NULL;
			break;
		})
		if (vj == w->ob_digit[size_w-1])
			q = MASK;
		else
			q = (((twodigits)vj << SHIFT) + v->ob_digit[j-1]) /
				w->ob_digit[size_w-1];

		while (w->ob_digit[size_w-2]*q >
				((
					((twodigits)vj << SHIFT)
					+ v->ob_digit[j-1]
					- q*w->ob_digit[size_w-1]
								) << SHIFT)
				+ v->ob_digit[j-2])
			--q;

		for (i = 0; i < size_w && i+k < size_v; ++i) {
			twodigits z = w->ob_digit[i] * q;
			digit zz = (digit) (z >> SHIFT);
			carry += v->ob_digit[i+k] - z
				+ ((twodigits)zz << SHIFT);
			v->ob_digit[i+k] = (digit)(carry & MASK);
			carry = Py_ARITHMETIC_RIGHT_SHIFT(BASE_TWODIGITS_TYPE,
							  carry, SHIFT);
			carry -= zz;
		}

		if (i+k < size_v) {
			carry += v->ob_digit[i+k];
			v->ob_digit[i+k] = 0;
		}

		if (carry == 0)
			a->ob_digit[k] = (digit) q;
		else {
			assert(carry == -1);
			a->ob_digit[k] = (digit) q-1;
			carry = 0;
			for (i = 0; i < size_w && i+k < size_v; ++i) {
				carry += v->ob_digit[i+k] + w->ob_digit[i];
				v->ob_digit[i+k] = (digit)(carry & MASK);
				carry = Py_ARITHMETIC_RIGHT_SHIFT(
						BASE_TWODIGITS_TYPE,
						carry, SHIFT);
			}
		}
	} /* for j, k */

	if (a == NULL)
		*prem = NULL;
	else {
		a = long_normalize(a);
		*prem = divrem1(v, d, &d);
		/* d receives the (unused) remainder */
		if (*prem == NULL) {
			Py_DECREF(a);
			a = NULL;
		}
	}
	Py_DECREF(v);
	Py_DECREF(w);
	return a;
}

/* Methods */

static void
long_dealloc(PyObject *v)
{
	v->ob_type->tp_free(v);
}

static PyObject *
long_repr(PyObject *v)
{
	return long_format(v, 10, 1);
}

static PyObject *
long_str(PyObject *v)
{
	return long_format(v, 10, 0);
}

static int
long_compare(PyLongObject *a, PyLongObject *b)
{
	Py_ssize_t sign;

	if (a->ob_size != b->ob_size) {
		if (ABS(a->ob_size) == 0 && ABS(b->ob_size) == 0)
			sign = 0;
		else
			sign = a->ob_size - b->ob_size;
	}
	else {
		Py_ssize_t i = ABS(a->ob_size);
		while (--i >= 0 && a->ob_digit[i] == b->ob_digit[i])
			;
		if (i < 0)
			sign = 0;
		else {
			sign = (int)a->ob_digit[i] - (int)b->ob_digit[i];
			if (a->ob_size < 0)
				sign = -sign;
		}
	}
	return sign < 0 ? -1 : sign > 0 ? 1 : 0;
}

static long
long_hash(PyLongObject *v)
{
	long x;
	Py_ssize_t i;
	int sign;

	/* This is designed so that Python ints and longs with the
	   same value hash to the same value, otherwise comparisons
	   of mapping keys will turn out weird */
	i = v->ob_size;
	sign = 1;
	x = 0;
	if (i < 0) {
		sign = -1;
		i = -(i);
	}
#define LONG_BIT_SHIFT	(8*sizeof(long) - SHIFT)
	while (--i >= 0) {
		/* Force a native long #-bits (32 or 64) circular shift */
		x = ((x << SHIFT) & ~MASK) | ((x >> LONG_BIT_SHIFT) & MASK);
		x += v->ob_digit[i];
	}
#undef LONG_BIT_SHIFT
	x = x * sign;
	if (x == -1)
		x = -2;
	return x;
}


/* Add the absolute values of two long integers. */

static PyLongObject *
x_add(PyLongObject *a, PyLongObject *b)
{
	Py_ssize_t size_a = ABS(a->ob_size), size_b = ABS(b->ob_size);
	PyLongObject *z;
	int i;
	digit carry = 0;

	/* Ensure a is the larger of the two: */
	if (size_a < size_b) {
		{ PyLongObject *temp = a; a = b; b = temp; }
		{ Py_ssize_t size_temp = size_a;
		  size_a = size_b;
		  size_b = size_temp; }
	}
	z = _PyLong_New(size_a+1);
	if (z == NULL)
		return NULL;
	for (i = 0; i < size_b; ++i) {
		carry += a->ob_digit[i] + b->ob_digit[i];
		z->ob_digit[i] = carry & MASK;
		carry >>= SHIFT;
	}
	for (; i < size_a; ++i) {
		carry += a->ob_digit[i];
		z->ob_digit[i] = carry & MASK;
		carry >>= SHIFT;
	}
	z->ob_digit[i] = carry;
	return long_normalize(z);
}

/* Subtract the absolute values of two integers. */

static PyLongObject *
x_sub(PyLongObject *a, PyLongObject *b)
{
	Py_ssize_t size_a = ABS(a->ob_size), size_b = ABS(b->ob_size);
	PyLongObject *z;
	Py_ssize_t i;
	int sign = 1;
	digit borrow = 0;

	/* Ensure a is the larger of the two: */
	if (size_a < size_b) {
		sign = -1;
		{ PyLongObject *temp = a; a = b; b = temp; }
		{ Py_ssize_t size_temp = size_a;
		  size_a = size_b;
		  size_b = size_temp; }
	}
	else if (size_a == size_b) {
		/* Find highest digit where a and b differ: */
		i = size_a;
		while (--i >= 0 && a->ob_digit[i] == b->ob_digit[i])
			;
		if (i < 0)
			return _PyLong_New(0);
		if (a->ob_digit[i] < b->ob_digit[i]) {
			sign = -1;
			{ PyLongObject *temp = a; a = b; b = temp; }
		}
		size_a = size_b = i+1;
	}
	z = _PyLong_New(size_a);
	if (z == NULL)
		return NULL;
	for (i = 0; i < size_b; ++i) {
		/* The following assumes unsigned arithmetic
		   works module 2**N for some N>SHIFT. */
		borrow = a->ob_digit[i] - b->ob_digit[i] - borrow;
		z->ob_digit[i] = borrow & MASK;
		borrow >>= SHIFT;
		borrow &= 1; /* Keep only one sign bit */
	}
	for (; i < size_a; ++i) {
		borrow = a->ob_digit[i] - borrow;
		z->ob_digit[i] = borrow & MASK;
		borrow >>= SHIFT;
		borrow &= 1; /* Keep only one sign bit */
	}
	assert(borrow == 0);
	if (sign < 0)
		z->ob_size = -(z->ob_size);
	return long_normalize(z);
}

static PyObject *
long_add(PyLongObject *v, PyLongObject *w)
{
	PyLongObject *a, *b, *z;

	CONVERT_BINOP((PyObject *)v, (PyObject *)w, &a, &b);

	if (a->ob_size < 0) {
		if (b->ob_size < 0) {
			z = x_add(a, b);
			if (z != NULL && z->ob_size != 0)
				z->ob_size = -(z->ob_size);
		}
		else
			z = x_sub(b, a);
	}
	else {
		if (b->ob_size < 0)
			z = x_sub(a, b);
		else
			z = x_add(a, b);
	}
	Py_DECREF(a);
	Py_DECREF(b);
	return (PyObject *)z;
}

static PyObject *
long_sub(PyLongObject *v, PyLongObject *w)
{
	PyLongObject *a, *b, *z;

	CONVERT_BINOP((PyObject *)v, (PyObject *)w, &a, &b);

	if (a->ob_size < 0) {
		if (b->ob_size < 0)
			z = x_sub(a, b);
		else
			z = x_add(a, b);
		if (z != NULL && z->ob_size != 0)
			z->ob_size = -(z->ob_size);
	}
	else {
		if (b->ob_size < 0)
			z = x_add(a, b);
		else
			z = x_sub(a, b);
	}
	Py_DECREF(a);
	Py_DECREF(b);
	return (PyObject *)z;
}

/* Grade school multiplication, ignoring the signs.
 * Returns the absolute value of the product, or NULL if error.
 */
static PyLongObject *
x_mul(PyLongObject *a, PyLongObject *b)
{
	PyLongObject *z;
	Py_ssize_t size_a = ABS(a->ob_size);
	Py_ssize_t size_b = ABS(b->ob_size);
	Py_ssize_t i;

     	z = _PyLong_New(size_a + size_b);
	if (z == NULL)
		return NULL;

	memset(z->ob_digit, 0, z->ob_size * sizeof(digit));
	if (a == b) {
		/* Efficient squaring per HAC, Algorithm 14.16:
		 * http://www.cacr.math.uwaterloo.ca/hac/about/chap14.pdf
		 * Gives slightly less than a 2x speedup when a == b,
		 * via exploiting that each entry in the multiplication
		 * pyramid appears twice (except for the size_a squares).
		 */
		for (i = 0; i < size_a; ++i) {
			twodigits carry;
			twodigits f = a->ob_digit[i];
			digit *pz = z->ob_digit + (i << 1);
			digit *pa = a->ob_digit + i + 1;
			digit *paend = a->ob_digit + size_a;

			SIGCHECK({
				Py_DECREF(z);
				return NULL;
			})

			carry = *pz + f * f;
			*pz++ = (digit)(carry & MASK);
			carry >>= SHIFT;
			assert(carry <= MASK);

			/* Now f is added in twice in each column of the
			 * pyramid it appears.  Same as adding f<<1 once.
			 */
			f <<= 1;
			while (pa < paend) {
				carry += *pz + *pa++ * f;
				*pz++ = (digit)(carry & MASK);
				carry >>= SHIFT;
				assert(carry <= (MASK << 1));
			}
			if (carry) {
				carry += *pz;
				*pz++ = (digit)(carry & MASK);
				carry >>= SHIFT;
			}
			if (carry)
				*pz += (digit)(carry & MASK);
			assert((carry >> SHIFT) == 0);
		}
	}
	else {	/* a is not the same as b -- gradeschool long mult */
		for (i = 0; i < size_a; ++i) {
			twodigits carry = 0;
			twodigits f = a->ob_digit[i];
			digit *pz = z->ob_digit + i;
			digit *pb = b->ob_digit;
			digit *pbend = b->ob_digit + size_b;

			SIGCHECK({
				Py_DECREF(z);
				return NULL;
			})

			while (pb < pbend) {
				carry += *pz + *pb++ * f;
				*pz++ = (digit)(carry & MASK);
				carry >>= SHIFT;
				assert(carry <= MASK);
			}
			if (carry)
				*pz += (digit)(carry & MASK);
			assert((carry >> SHIFT) == 0);
		}
	}
	return long_normalize(z);
}

/* A helper for Karatsuba multiplication (k_mul).
   Takes a long "n" and an integer "size" representing the place to
   split, and sets low and high such that abs(n) == (high << size) + low,
   viewing the shift as being by digits.  The sign bit is ignored, and
   the return values are >= 0.
   Returns 0 on success, -1 on failure.
*/
static int
kmul_split(PyLongObject *n, Py_ssize_t size, PyLongObject **high, PyLongObject **low)
{
	PyLongObject *hi, *lo;
	Py_ssize_t size_lo, size_hi;
	const Py_ssize_t size_n = ABS(n->ob_size);

	size_lo = MIN(size_n, size);
	size_hi = size_n - size_lo;

	if ((hi = _PyLong_New(size_hi)) == NULL)
		return -1;
	if ((lo = _PyLong_New(size_lo)) == NULL) {
		Py_DECREF(hi);
		return -1;
	}

	memcpy(lo->ob_digit, n->ob_digit, size_lo * sizeof(digit));
	memcpy(hi->ob_digit, n->ob_digit + size_lo, size_hi * sizeof(digit));

	*high = long_normalize(hi);
	*low = long_normalize(lo);
	return 0;
}

static PyLongObject *k_lopsided_mul(PyLongObject *a, PyLongObject *b);

/* Karatsuba multiplication.  Ignores the input signs, and returns the
 * absolute value of the product (or NULL if error).
 * See Knuth Vol. 2 Chapter 4.3.3 (Pp. 294-295).
 */
static PyLongObject *
k_mul(PyLongObject *a, PyLongObject *b)
{
	Py_ssize_t asize = ABS(a->ob_size);
	Py_ssize_t bsize = ABS(b->ob_size);
	PyLongObject *ah = NULL;
	PyLongObject *al = NULL;
	PyLongObject *bh = NULL;
	PyLongObject *bl = NULL;
	PyLongObject *ret = NULL;
	PyLongObject *t1, *t2, *t3;
	Py_ssize_t shift;	/* the number of digits we split off */
	Py_ssize_t i;

	/* (ah*X+al)(bh*X+bl) = ah*bh*X*X + (ah*bl + al*bh)*X + al*bl
	 * Let k = (ah+al)*(bh+bl) = ah*bl + al*bh  + ah*bh + al*bl
	 * Then the original product is
	 *     ah*bh*X*X + (k - ah*bh - al*bl)*X + al*bl
	 * By picking X to be a power of 2, "*X" is just shifting, and it's
	 * been reduced to 3 multiplies on numbers half the size.
	 */

	/* We want to split based on the larger number; fiddle so that b
	 * is largest.
	 */
	if (asize > bsize) {
		t1 = a;
		a = b;
		b = t1;

		i = asize;
		asize = bsize;
		bsize = i;
	}

	/* Use gradeschool math when either number is too small. */
	i = a == b ? KARATSUBA_SQUARE_CUTOFF : KARATSUBA_CUTOFF;
	if (asize <= i) {
		if (asize == 0)
			return _PyLong_New(0);
		else
			return x_mul(a, b);
	}

	/* If a is small compared to b, splitting on b gives a degenerate
	 * case with ah==0, and Karatsuba may be (even much) less efficient
	 * than "grade school" then.  However, we can still win, by viewing
	 * b as a string of "big digits", each of width a->ob_size.  That
	 * leads to a sequence of balanced calls to k_mul.
	 */
	if (2 * asize <= bsize)
		return k_lopsided_mul(a, b);

	/* Split a & b into hi & lo pieces. */
	shift = bsize >> 1;
	if (kmul_split(a, shift, &ah, &al) < 0) goto fail;
	assert(ah->ob_size > 0);	/* the split isn't degenerate */

	if (a == b) {
		bh = ah;
		bl = al;
		Py_INCREF(bh);
		Py_INCREF(bl);
	}
	else if (kmul_split(b, shift, &bh, &bl) < 0) goto fail;

	/* The plan:
	 * 1. Allocate result space (asize + bsize digits:  that's always
	 *    enough).
	 * 2. Compute ah*bh, and copy into result at 2*shift.
	 * 3. Compute al*bl, and copy into result at 0.  Note that this
	 *    can't overlap with #2.
	 * 4. Subtract al*bl from the result, starting at shift.  This may
	 *    underflow (borrow out of the high digit), but we don't care:
	 *    we're effectively doing unsigned arithmetic mod
	 *    BASE**(sizea + sizeb), and so long as the *final* result fits,
	 *    borrows and carries out of the high digit can be ignored.
	 * 5. Subtract ah*bh from the result, starting at shift.
	 * 6. Compute (ah+al)*(bh+bl), and add it into the result starting
	 *    at shift.
	 */

	/* 1. Allocate result space. */
	ret = _PyLong_New(asize + bsize);
	if (ret == NULL) goto fail;
#ifdef Py_DEBUG
	/* Fill with trash, to catch reference to uninitialized digits. */
	memset(ret->ob_digit, 0xDF, ret->ob_size * sizeof(digit));
#endif

	/* 2. t1 <- ah*bh, and copy into high digits of result. */
	if ((t1 = k_mul(ah, bh)) == NULL) goto fail;
	assert(t1->ob_size >= 0);
	assert(2*shift + t1->ob_size <= ret->ob_size);
	memcpy(ret->ob_digit + 2*shift, t1->ob_digit,
	       t1->ob_size * sizeof(digit));

	/* Zero-out the digits higher than the ah*bh copy. */
	i = ret->ob_size - 2*shift - t1->ob_size;
	if (i)
		memset(ret->ob_digit + 2*shift + t1->ob_size, 0,
		       i * sizeof(digit));

	/* 3. t2 <- al*bl, and copy into the low digits. */
	if ((t2 = k_mul(al, bl)) == NULL) {
		Py_DECREF(t1);
		goto fail;
	}
	assert(t2->ob_size >= 0);
	assert(t2->ob_size <= 2*shift); /* no overlap with high digits */
	memcpy(ret->ob_digit, t2->ob_digit, t2->ob_size * sizeof(digit));

	/* Zero out remaining digits. */
	i = 2*shift - t2->ob_size;	/* number of uninitialized digits */
	if (i)
		memset(ret->ob_digit + t2->ob_size, 0, i * sizeof(digit));

	/* 4 & 5. Subtract ah*bh (t1) and al*bl (t2).  We do al*bl first
	 * because it's fresher in cache.
	 */
	i = ret->ob_size - shift;  /* # digits after shift */
	(void)v_isub(ret->ob_digit + shift, i, t2->ob_digit, t2->ob_size);
	Py_DECREF(t2);

	(void)v_isub(ret->ob_digit + shift, i, t1->ob_digit, t1->ob_size);
	Py_DECREF(t1);

	/* 6. t3 <- (ah+al)(bh+bl), and add into result. */
	if ((t1 = x_add(ah, al)) == NULL) goto fail;
	Py_DECREF(ah);
	Py_DECREF(al);
	ah = al = NULL;

	if (a == b) {
		t2 = t1;
		Py_INCREF(t2);
	}
	else if ((t2 = x_add(bh, bl)) == NULL) {
		Py_DECREF(t1);
		goto fail;
	}
	Py_DECREF(bh);
	Py_DECREF(bl);
	bh = bl = NULL;

	t3 = k_mul(t1, t2);
	Py_DECREF(t1);
	Py_DECREF(t2);
	if (t3 == NULL) goto fail;
	assert(t3->ob_size >= 0);

	/* Add t3.  It's not obvious why we can't run out of room here.
	 * See the (*) comment after this function.
	 */
	(void)v_iadd(ret->ob_digit + shift, i, t3->ob_digit, t3->ob_size);
	Py_DECREF(t3);

	return long_normalize(ret);

 fail:
 	Py_XDECREF(ret);
	Py_XDECREF(ah);
	Py_XDECREF(al);
	Py_XDECREF(bh);
	Py_XDECREF(bl);
	return NULL;
}

/* (*) Why adding t3 can't "run out of room" above.

Let f(x) mean the floor of x and c(x) mean the ceiling of x.  Some facts
to start with:

1. For any integer i, i = c(i/2) + f(i/2).  In particular,
   bsize = c(bsize/2) + f(bsize/2).
2. shift = f(bsize/2)
3. asize <= bsize
4. Since we call k_lopsided_mul if asize*2 <= bsize, asize*2 > bsize in this
   routine, so asize > bsize/2 >= f(bsize/2) in this routine.

We allocated asize + bsize result digits, and add t3 into them at an offset
of shift.  This leaves asize+bsize-shift allocated digit positions for t3
to fit into, = (by #1 and #2) asize + f(bsize/2) + c(bsize/2) - f(bsize/2) =
asize + c(bsize/2) available digit positions.

bh has c(bsize/2) digits, and bl at most f(size/2) digits.  So bh+hl has
at most c(bsize/2) digits + 1 bit.

If asize == bsize, ah has c(bsize/2) digits, else ah has at most f(bsize/2)
digits, and al has at most f(bsize/2) digits in any case.  So ah+al has at
most (asize == bsize ? c(bsize/2) : f(bsize/2)) digits + 1 bit.

The product (ah+al)*(bh+bl) therefore has at most

    c(bsize/2) + (asize == bsize ? c(bsize/2) : f(bsize/2)) digits + 2 bits

and we have asize + c(bsize/2) available digit positions.  We need to show
this is always enough.  An instance of c(bsize/2) cancels out in both, so
the question reduces to whether asize digits is enough to hold
(asize == bsize ? c(bsize/2) : f(bsize/2)) digits + 2 bits.  If asize < bsize,
then we're asking whether asize digits >= f(bsize/2) digits + 2 bits.  By #4,
asize is at least f(bsize/2)+1 digits, so this in turn reduces to whether 1
digit is enough to hold 2 bits.  This is so since SHIFT=15 >= 2.  If
asize == bsize, then we're asking whether bsize digits is enough to hold
c(bsize/2) digits + 2 bits, or equivalently (by #1) whether f(bsize/2) digits
is enough to hold 2 bits.  This is so if bsize >= 2, which holds because
bsize >= KARATSUBA_CUTOFF >= 2.

Note that since there's always enough room for (ah+al)*(bh+bl), and that's
clearly >= each of ah*bh and al*bl, there's always enough room to subtract
ah*bh and al*bl too.
*/

/* b has at least twice the digits of a, and a is big enough that Karatsuba
 * would pay off *if* the inputs had balanced sizes.  View b as a sequence
 * of slices, each with a->ob_size digits, and multiply the slices by a,
 * one at a time.  This gives k_mul balanced inputs to work with, and is
 * also cache-friendly (we compute one double-width slice of the result
 * at a time, then move on, never bactracking except for the helpful
 * single-width slice overlap between successive partial sums).
 */
static PyLongObject *
k_lopsided_mul(PyLongObject *a, PyLongObject *b)
{
	const Py_ssize_t asize = ABS(a->ob_size);
	Py_ssize_t bsize = ABS(b->ob_size);
	Py_ssize_t nbdone;	/* # of b digits already multiplied */
	PyLongObject *ret;
	PyLongObject *bslice = NULL;

	assert(asize > KARATSUBA_CUTOFF);
	assert(2 * asize <= bsize);

	/* Allocate result space, and zero it out. */
	ret = _PyLong_New(asize + bsize);
	if (ret == NULL)
		return NULL;
	memset(ret->ob_digit, 0, ret->ob_size * sizeof(digit));

	/* Successive slices of b are copied into bslice. */
	bslice = _PyLong_New(asize);
	if (bslice == NULL)
		goto fail;

	nbdone = 0;
	while (bsize > 0) {
		PyLongObject *product;
		const Py_ssize_t nbtouse = MIN(bsize, asize);

		/* Multiply the next slice of b by a. */
		memcpy(bslice->ob_digit, b->ob_digit + nbdone,
		       nbtouse * sizeof(digit));
		bslice->ob_size = nbtouse;
		product = k_mul(a, bslice);
		if (product == NULL)
			goto fail;

		/* Add into result. */
		(void)v_iadd(ret->ob_digit + nbdone, ret->ob_size - nbdone,
			     product->ob_digit, product->ob_size);
		Py_DECREF(product);

		bsize -= nbtouse;
		nbdone += nbtouse;
	}

	Py_DECREF(bslice);
	return long_normalize(ret);

 fail:
	Py_DECREF(ret);
	Py_XDECREF(bslice);
	return NULL;
}

static PyObject *
long_mul(PyLongObject *v, PyLongObject *w)
{
	PyLongObject *a, *b, *z;

	if (!convert_binop((PyObject *)v, (PyObject *)w, &a, &b)) {
		Py_INCREF(Py_NotImplemented);
		return Py_NotImplemented;
	}

	z = k_mul(a, b);
	/* Negate if exactly one of the inputs is negative. */
	if (((a->ob_size ^ b->ob_size) < 0) && z)
		z->ob_size = -(z->ob_size);
	Py_DECREF(a);
	Py_DECREF(b);
	return (PyObject *)z;
}

/* The / and % operators are now defined in terms of divmod().
   The expression a mod b has the value a - b*floor(a/b).
   The long_divrem function gives the remainder after division of
   |a| by |b|, with the sign of a.  This is also expressed
   as a - b*trunc(a/b), if trunc truncates towards zero.
   Some examples:
   	 a	 b	a rem b		a mod b
   	 13	 10	 3		 3
   	-13	 10	-3		 7
   	 13	-10	 3		-7
   	-13	-10	-3		-3
   So, to get from rem to mod, we have to add b if a and b
   have different signs.  We then subtract one from the 'div'
   part of the outcome to keep the invariant intact. */

/* Compute
 *     *pdiv, *pmod = divmod(v, w)
 * NULL can be passed for pdiv or pmod, in which case that part of
 * the result is simply thrown away.  The caller owns a reference to
 * each of these it requests (does not pass NULL for).
 */
static int
l_divmod(PyLongObject *v, PyLongObject *w,
	 PyLongObject **pdiv, PyLongObject **pmod)
{
	PyLongObject *div, *mod;

	if (long_divrem(v, w, &div, &mod) < 0)
		return -1;
	if ((mod->ob_size < 0 && w->ob_size > 0) ||
	    (mod->ob_size > 0 && w->ob_size < 0)) {
		PyLongObject *temp;
		PyLongObject *one;
		temp = (PyLongObject *) long_add(mod, w);
		Py_DECREF(mod);
		mod = temp;
		if (mod == NULL) {
			Py_DECREF(div);
			return -1;
		}
		one = (PyLongObject *) PyLong_FromLong(1L);
		if (one == NULL ||
		    (temp = (PyLongObject *) long_sub(div, one)) == NULL) {
			Py_DECREF(mod);
			Py_DECREF(div);
			Py_XDECREF(one);
			return -1;
		}
		Py_DECREF(one);
		Py_DECREF(div);
		div = temp;
	}
	if (pdiv != NULL)
		*pdiv = div;
	else
		Py_DECREF(div);

	if (pmod != NULL)
		*pmod = mod;
	else
		Py_DECREF(mod);

	return 0;
}

static PyObject *
long_div(PyObject *v, PyObject *w)
{
	PyLongObject *a, *b, *div;

	CONVERT_BINOP(v, w, &a, &b);
	if (l_divmod(a, b, &div, NULL) < 0)
		div = NULL;
	Py_DECREF(a);
	Py_DECREF(b);
	return (PyObject *)div;
}

static PyObject *
long_classic_div(PyObject *v, PyObject *w)
{
	PyLongObject *a, *b, *div;

	CONVERT_BINOP(v, w, &a, &b);
	if (Py_DivisionWarningFlag &&
	    PyErr_Warn(PyExc_DeprecationWarning, "classic long division") < 0)
		div = NULL;
	else if (l_divmod(a, b, &div, NULL) < 0)
		div = NULL;
	Py_DECREF(a);
	Py_DECREF(b);
	return (PyObject *)div;
}

static PyObject *
long_true_divide(PyObject *v, PyObject *w)
{
	PyLongObject *a, *b;
	double ad, bd;
	int failed, aexp = -1, bexp = -1;

	CONVERT_BINOP(v, w, &a, &b);
	ad = _PyLong_AsScaledDouble((PyObject *)a, &aexp);
	bd = _PyLong_AsScaledDouble((PyObject *)b, &bexp);
	failed = (ad == -1.0 || bd == -1.0) && PyErr_Occurred();
	Py_DECREF(a);
	Py_DECREF(b);
	if (failed)
		return NULL;
	/* 'aexp' and 'bexp' were initialized to -1 to silence gcc-4.0.x,
	   but should really be set correctly after sucessful calls to
	   _PyLong_AsScaledDouble() */
	assert(aexp >= 0 && bexp >= 0);

	if (bd == 0.0) {
		PyErr_SetString(PyExc_ZeroDivisionError,
			"long division or modulo by zero");
		return NULL;
	}

	/* True value is very close to ad/bd * 2**(SHIFT*(aexp-bexp)) */
	ad /= bd;	/* overflow/underflow impossible here */
	aexp -= bexp;
	if (aexp > INT_MAX / SHIFT)
		goto overflow;
	else if (aexp < -(INT_MAX / SHIFT))
		return PyFloat_FromDouble(0.0);	/* underflow to 0 */
	errno = 0;
	ad = ldexp(ad, aexp * SHIFT);
	if (Py_OVERFLOWED(ad)) /* ignore underflow to 0.0 */
		goto overflow;
	return PyFloat_FromDouble(ad);

overflow:
	PyErr_SetString(PyExc_OverflowError,
		"long/long too large for a float");
	return NULL;

}

static PyObject *
long_mod(PyObject *v, PyObject *w)
{
	PyLongObject *a, *b, *mod;

	CONVERT_BINOP(v, w, &a, &b);

	if (l_divmod(a, b, NULL, &mod) < 0)
		mod = NULL;
	Py_DECREF(a);
	Py_DECREF(b);
	return (PyObject *)mod;
}

static PyObject *
long_divmod(PyObject *v, PyObject *w)
{
	PyLongObject *a, *b, *div, *mod;
	PyObject *z;

	CONVERT_BINOP(v, w, &a, &b);

	if (l_divmod(a, b, &div, &mod) < 0) {
		Py_DECREF(a);
		Py_DECREF(b);
		return NULL;
	}
	z = PyTuple_New(2);
	if (z != NULL) {
		PyTuple_SetItem(z, 0, (PyObject *) div);
		PyTuple_SetItem(z, 1, (PyObject *) mod);
	}
	else {
		Py_DECREF(div);
		Py_DECREF(mod);
	}
	Py_DECREF(a);
	Py_DECREF(b);
	return z;
}

/* pow(v, w, x) */
static PyObject *
long_pow(PyObject *v, PyObject *w, PyObject *x)
{
	PyLongObject *a, *b, *c; /* a,b,c = v,w,x */
	int negativeOutput = 0;  /* if x<0 return negative output */

	PyLongObject *z = NULL;  /* accumulated result */
	Py_ssize_t i, j, k;             /* counters */
	PyLongObject *temp = NULL;

	/* 5-ary values.  If the exponent is large enough, table is
	 * precomputed so that table[i] == a**i % c for i in range(32).
	 */
	PyLongObject *table[32] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
				   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

	/* a, b, c = v, w, x */
	CONVERT_BINOP(v, w, &a, &b);
	if (PyLong_Check(x)) {
		c = (PyLongObject *)x;
		Py_INCREF(x);
	}
	else if (PyInt_Check(x)) {
		c = (PyLongObject *)PyLong_FromLong(PyInt_AS_LONG(x));
		if (c == NULL)
			goto Error;
	}
	else if (x == Py_None)
		c = NULL;
	else {
		Py_DECREF(a);
		Py_DECREF(b);
		Py_INCREF(Py_NotImplemented);
		return Py_NotImplemented;
	}

	if (b->ob_size < 0) {  /* if exponent is negative */
		if (c) {
			PyErr_SetString(PyExc_TypeError, "pow() 2nd argument "
			    "cannot be negative when 3rd argument specified");
			goto Error;
		}
		else {
			/* else return a float.  This works because we know
			   that this calls float_pow() which converts its
			   arguments to double. */
			Py_DECREF(a);
			Py_DECREF(b);
			return PyFloat_Type.tp_as_number->nb_power(v, w, x);
		}
	}

	if (c) {
		/* if modulus == 0:
		       raise ValueError() */
		if (c->ob_size == 0) {
			PyErr_SetString(PyExc_ValueError,
					"pow() 3rd argument cannot be 0");
			goto Error;
		}

		/* if modulus < 0:
		       negativeOutput = True
		       modulus = -modulus */
		if (c->ob_size < 0) {
			negativeOutput = 1;
			temp = (PyLongObject *)_PyLong_Copy(c);
			if (temp == NULL)
				goto Error;
			Py_DECREF(c);
			c = temp;
			temp = NULL;
			c->ob_size = - c->ob_size;
		}

		/* if modulus == 1:
		       return 0 */
		if ((c->ob_size == 1) && (c->ob_digit[0] == 1)) {
			z = (PyLongObject *)PyLong_FromLong(0L);
			goto Done;
		}

		/* if base < 0:
		       base = base % modulus
		   Having the base positive just makes things easier. */
		if (a->ob_size < 0) {
			if (l_divmod(a, c, NULL, &temp) < 0)
				goto Error;
			Py_DECREF(a);
			a = temp;
			temp = NULL;
		}
	}

	/* At this point a, b, and c are guaranteed non-negative UNLESS
	   c is NULL, in which case a may be negative. */

	z = (PyLongObject *)PyLong_FromLong(1L);
	if (z == NULL)
		goto Error;

	/* Perform a modular reduction, X = X % c, but leave X alone if c
	 * is NULL.
	 */
#define REDUCE(X)					\
	if (c != NULL) {				\
		if (l_divmod(X, c, NULL, &temp) < 0)	\
			goto Error;			\
		Py_XDECREF(X);				\
		X = temp;				\
		temp = NULL;				\
	}

	/* Multiply two values, then reduce the result:
	   result = X*Y % c.  If c is NULL, skip the mod. */
#define MULT(X, Y, result)				\
{							\
	temp = (PyLongObject *)long_mul(X, Y);		\
	if (temp == NULL)				\
		goto Error;				\
	Py_XDECREF(result);				\
	result = temp;					\
	temp = NULL;					\
	REDUCE(result)					\
}

	if (b->ob_size <= FIVEARY_CUTOFF) {
		/* Left-to-right binary exponentiation (HAC Algorithm 14.79) */
		/* http://www.cacr.math.uwaterloo.ca/hac/about/chap14.pdf    */
		for (i = b->ob_size - 1; i >= 0; --i) {
			digit bi = b->ob_digit[i];

			for (j = 1 << (SHIFT-1); j != 0; j >>= 1) {
				MULT(z, z, z)
				if (bi & j)
					MULT(z, a, z)
			}
		}
	}
	else {
		/* Left-to-right 5-ary exponentiation (HAC Algorithm 14.82) */
		Py_INCREF(z);	/* still holds 1L */
		table[0] = z;
		for (i = 1; i < 32; ++i)
			MULT(table[i-1], a, table[i])

		for (i = b->ob_size - 1; i >= 0; --i) {
			const digit bi = b->ob_digit[i];

			for (j = SHIFT - 5; j >= 0; j -= 5) {
				const int index = (bi >> j) & 0x1f;
				for (k = 0; k < 5; ++k)
					MULT(z, z, z)
				if (index)
					MULT(z, table[index], z)
			}
		}
	}

	if (negativeOutput && (z->ob_size != 0)) {
		temp = (PyLongObject *)long_sub(z, c);
		if (temp == NULL)
			goto Error;
		Py_DECREF(z);
		z = temp;
		temp = NULL;
	}
	goto Done;

 Error:
 	if (z != NULL) {
 		Py_DECREF(z);
 		z = NULL;
 	}
	/* fall through */
 Done:
	if (b->ob_size > FIVEARY_CUTOFF) {
		for (i = 0; i < 32; ++i)
			Py_XDECREF(table[i]);
	}
	Py_DECREF(a);
	Py_DECREF(b);
	Py_XDECREF(c);
	Py_XDECREF(temp);
	return (PyObject *)z;
}

static PyObject *
long_invert(PyLongObject *v)
{
	/* Implement ~x as -(x+1) */
	PyLongObject *x;
	PyLongObject *w;
	w = (PyLongObject *)PyLong_FromLong(1L);
	if (w == NULL)
		return NULL;
	x = (PyLongObject *) long_add(v, w);
	Py_DECREF(w);
	if (x == NULL)
		return NULL;
	x->ob_size = -(x->ob_size);
	return (PyObject *)x;
}

static PyObject *
long_pos(PyLongObject *v)
{
	if (PyLong_CheckExact(v)) {
		Py_INCREF(v);
		return (PyObject *)v;
	}
	else
		return _PyLong_Copy(v);
}

static PyObject *
long_neg(PyLongObject *v)
{
	PyLongObject *z;
	if (v->ob_size == 0 && PyLong_CheckExact(v)) {
		/* -0 == 0 */
		Py_INCREF(v);
		return (PyObject *) v;
	}
	z = (PyLongObject *)_PyLong_Copy(v);
	if (z != NULL)
		z->ob_size = -(v->ob_size);
	return (PyObject *)z;
}

static PyObject *
long_abs(PyLongObject *v)
{
	if (v->ob_size < 0)
		return long_neg(v);
	else
		return long_pos(v);
}

static int
long_nonzero(PyLongObject *v)
{
	return ABS(v->ob_size) != 0;
}

static PyObject *
long_rshift(PyLongObject *v, PyLongObject *w)
{
	PyLongObject *a, *b;
	PyLongObject *z = NULL;
	long shiftby;
	Py_ssize_t newsize, wordshift, loshift, hishift, i, j;
	digit lomask, himask;

	CONVERT_BINOP((PyObject *)v, (PyObject *)w, &a, &b);

	if (a->ob_size < 0) {
		/* Right shifting negative numbers is harder */
		PyLongObject *a1, *a2;
		a1 = (PyLongObject *) long_invert(a);
		if (a1 == NULL)
			goto rshift_error;
		a2 = (PyLongObject *) long_rshift(a1, b);
		Py_DECREF(a1);
		if (a2 == NULL)
			goto rshift_error;
		z = (PyLongObject *) long_invert(a2);
		Py_DECREF(a2);
	}
	else {

		shiftby = PyLong_AsLong((PyObject *)b);
		if (shiftby == -1L && PyErr_Occurred())
			goto rshift_error;
		if (shiftby < 0) {
			PyErr_SetString(PyExc_ValueError,
					"negative shift count");
			goto rshift_error;
		}
		wordshift = shiftby / SHIFT;
		newsize = ABS(a->ob_size) - wordshift;
		if (newsize <= 0) {
			z = _PyLong_New(0);
			Py_DECREF(a);
			Py_DECREF(b);
			return (PyObject *)z;
		}
		loshift = shiftby % SHIFT;
		hishift = SHIFT - loshift;
		lomask = ((digit)1 << hishift) - 1;
		himask = MASK ^ lomask;
		z = _PyLong_New(newsize);
		if (z == NULL)
			goto rshift_error;
		if (a->ob_size < 0)
			z->ob_size = -(z->ob_size);
		for (i = 0, j = wordshift; i < newsize; i++, j++) {
			z->ob_digit[i] = (a->ob_digit[j] >> loshift) & lomask;
			if (i+1 < newsize)
				z->ob_digit[i] |=
				  (a->ob_digit[j+1] << hishift) & himask;
		}
		z = long_normalize(z);
	}
rshift_error:
	Py_DECREF(a);
	Py_DECREF(b);
	return (PyObject *) z;

}

static PyObject *
long_lshift(PyObject *v, PyObject *w)
{
	/* This version due to Tim Peters */
	PyLongObject *a, *b;
	PyLongObject *z = NULL;
	long shiftby;
	Py_ssize_t oldsize, newsize, wordshift, remshift, i, j;
	twodigits accum;

	CONVERT_BINOP(v, w, &a, &b);

	shiftby = PyLong_AsLong((PyObject *)b);
	if (shiftby == -1L && PyErr_Occurred())
		goto lshift_error;
	if (shiftby < 0) {
		PyErr_SetString(PyExc_ValueError, "negative shift count");
		goto lshift_error;
	}
	if ((long)(int)shiftby != shiftby) {
		PyErr_SetString(PyExc_ValueError,
				"outrageous left shift count");
		goto lshift_error;
	}
	/* wordshift, remshift = divmod(shiftby, SHIFT) */
	wordshift = (int)shiftby / SHIFT;
	remshift  = (int)shiftby - wordshift * SHIFT;

	oldsize = ABS(a->ob_size);
	newsize = oldsize + wordshift;
	if (remshift)
		++newsize;
	z = _PyLong_New(newsize);
	if (z == NULL)
		goto lshift_error;
	if (a->ob_size < 0)
		z->ob_size = -(z->ob_size);
	for (i = 0; i < wordshift; i++)
		z->ob_digit[i] = 0;
	accum = 0;
	for (i = wordshift, j = 0; j < oldsize; i++, j++) {
		accum |= (twodigits)a->ob_digit[j] << remshift;
		z->ob_digit[i] = (digit)(accum & MASK);
		accum >>= SHIFT;
	}
	if (remshift)
		z->ob_digit[newsize-1] = (digit)accum;
	else
		assert(!accum);
	z = long_normalize(z);
lshift_error:
	Py_DECREF(a);
	Py_DECREF(b);
	return (PyObject *) z;
}


/* Bitwise and/xor/or operations */

static PyObject *
long_bitwise(PyLongObject *a,
	     int op,  /* '&', '|', '^' */
	     PyLongObject *b)
{
	digit maska, maskb; /* 0 or MASK */
	int negz;
	Py_ssize_t size_a, size_b, size_z;
	PyLongObject *z;
	int i;
	digit diga, digb;
	PyObject *v;

	if (a->ob_size < 0) {
		a = (PyLongObject *) long_invert(a);
		if (a == NULL)
			return NULL;
		maska = MASK;
	}
	else {
		Py_INCREF(a);
		maska = 0;
	}
	if (b->ob_size < 0) {
		b = (PyLongObject *) long_invert(b);
		if (b == NULL) {
			Py_DECREF(a);
			return NULL;
		}
		maskb = MASK;
	}
	else {
		Py_INCREF(b);
		maskb = 0;
	}

	negz = 0;
	switch (op) {
	case '^':
		if (maska != maskb) {
			maska ^= MASK;
			negz = -1;
		}
		break;
	case '&':
		if (maska && maskb) {
			op = '|';
			maska ^= MASK;
			maskb ^= MASK;
			negz = -1;
		}
		break;
	case '|':
		if (maska || maskb) {
			op = '&';
			maska ^= MASK;
			maskb ^= MASK;
			negz = -1;
		}
		break;
	}

	/* JRH: The original logic here was to allocate the result value (z)
	   as the longer of the two operands.  However, there are some cases
	   where the result is guaranteed to be shorter than that: AND of two
	   positives, OR of two negatives: use the shorter number.  AND with
	   mixed signs: use the positive number.  OR with mixed signs: use the
	   negative number.  After the transformations above, op will be '&'
	   iff one of these cases applies, and mask will be non-0 for operands
	   whose length should be ignored.
	*/

	size_a = a->ob_size;
	size_b = b->ob_size;
	size_z = op == '&'
		? (maska
		   ? size_b
		   : (maskb ? size_a : MIN(size_a, size_b)))
		: MAX(size_a, size_b);
	z = _PyLong_New(size_z);
	if (z == NULL) {
		Py_DECREF(a);
		Py_DECREF(b);
		return NULL;
	}

	for (i = 0; i < size_z; ++i) {
		diga = (i < size_a ? a->ob_digit[i] : 0) ^ maska;
		digb = (i < size_b ? b->ob_digit[i] : 0) ^ maskb;
		switch (op) {
		case '&': z->ob_digit[i] = diga & digb; break;
		case '|': z->ob_digit[i] = diga | digb; break;
		case '^': z->ob_digit[i] = diga ^ digb; break;
		}
	}

	Py_DECREF(a);
	Py_DECREF(b);
	z = long_normalize(z);
	if (negz == 0)
		return (PyObject *) z;
	v = long_invert(z);
	Py_DECREF(z);
	return v;
}

static PyObject *
long_and(PyObject *v, PyObject *w)
{
	PyLongObject *a, *b;
	PyObject *c;
	CONVERT_BINOP(v, w, &a, &b);
	c = long_bitwise(a, '&', b);
	Py_DECREF(a);
	Py_DECREF(b);
	return c;
}

static PyObject *
long_xor(PyObject *v, PyObject *w)
{
	PyLongObject *a, *b;
	PyObject *c;
	CONVERT_BINOP(v, w, &a, &b);
	c = long_bitwise(a, '^', b);
	Py_DECREF(a);
	Py_DECREF(b);
	return c;
}

static PyObject *
long_or(PyObject *v, PyObject *w)
{
	PyLongObject *a, *b;
	PyObject *c;
	CONVERT_BINOP(v, w, &a, &b);
	c = long_bitwise(a, '|', b);
	Py_DECREF(a);
	Py_DECREF(b);
	return c;
}

static int
long_coerce(PyObject **pv, PyObject **pw)
{
	if (PyInt_Check(*pw)) {
		*pw = PyLong_FromLong(PyInt_AS_LONG(*pw));
		Py_INCREF(*pv);
		return 0;
	}
	else if (PyLong_Check(*pw)) {
		Py_INCREF(*pv);
		Py_INCREF(*pw);
		return 0;
	}
	return 1; /* Can't do it */
}

static PyObject *
long_long(PyObject *v)
{
	if (PyLong_CheckExact(v))
		Py_INCREF(v);
	else
		v = _PyLong_Copy((PyLongObject *)v);
	return v;
}

static PyObject *
long_int(PyObject *v)
{
	long x;
	x = PyLong_AsLong(v);
	if (PyErr_Occurred()) {
		if (PyErr_ExceptionMatches(PyExc_OverflowError)) {
				PyErr_Clear();
				if (PyLong_CheckExact(v)) {
					Py_INCREF(v);
					return v;
				}
				else
					return _PyLong_Copy((PyLongObject *)v);
		}
		else
			return NULL;
	}
	return PyInt_FromLong(x);
}

static PyObject *
long_float(PyObject *v)
{
	double result;
	result = PyLong_AsDouble(v);
	if (result == -1.0 && PyErr_Occurred())
		return NULL;
	return PyFloat_FromDouble(result);
}

static PyObject *
long_oct(PyObject *v)
{
	return long_format(v, 8, 1);
}

static PyObject *
long_hex(PyObject *v)
{
	return long_format(v, 16, 1);
}

static PyObject *
long_subtype_new(PyTypeObject *type, PyObject *args, PyObject *kwds);

static PyObject *
long_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	PyObject *x = NULL;
	int base = -909;		     /* unlikely! */
	static char *kwlist[] = {"x", "base", 0};

	if (type != &PyLong_Type)
		return long_subtype_new(type, args, kwds); /* Wimp out */
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|Oi:long", kwlist,
					 &x, &base))
		return NULL;
	if (x == NULL)
		return PyLong_FromLong(0L);
	if (base == -909)
		return PyNumber_Long(x);
	else if (PyString_Check(x)) {
		/* Since PyLong_FromString doesn't have a length parameter,
		 * check here for possible NULs in the string. */
		char *string = PyString_AS_STRING(x);
		if (strlen(string) != PyString_Size(x)) {
			/* create a repr() of the input string,
			 * just like PyLong_FromString does. */
			PyObject *srepr;
			srepr = PyObject_Repr(x);
			if (srepr == NULL)
				return NULL;
			PyErr_Format(PyExc_ValueError,
			     "invalid literal for long() with base %d: %s",
			     base, PyString_AS_STRING(srepr));
			Py_DECREF(srepr);
			return NULL;
		}
		return PyLong_FromString(PyString_AS_STRING(x), NULL, base);
	}
#ifdef Py_USING_UNICODE
	else if (PyUnicode_Check(x))
		return PyLong_FromUnicode(PyUnicode_AS_UNICODE(x),
					  PyUnicode_GET_SIZE(x),
					  base);
#endif
	else {
		PyErr_SetString(PyExc_TypeError,
			"long() can't convert non-string with explicit base");
		return NULL;
	}
}

/* Wimpy, slow approach to tp_new calls for subtypes of long:
   first create a regular long from whatever arguments we got,
   then allocate a subtype instance and initialize it from
   the regular long.  The regular long is then thrown away.
*/
static PyObject *
long_subtype_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	PyLongObject *tmp, *newobj;
	Py_ssize_t i, n;

	assert(PyType_IsSubtype(type, &PyLong_Type));
	tmp = (PyLongObject *)long_new(&PyLong_Type, args, kwds);
	if (tmp == NULL)
		return NULL;
	assert(PyLong_CheckExact(tmp));
	n = tmp->ob_size;
	if (n < 0)
		n = -n;
	newobj = (PyLongObject *)type->tp_alloc(type, n);
	if (newobj == NULL) {
		Py_DECREF(tmp);
		return NULL;
	}
	assert(PyLong_Check(newobj));
	newobj->ob_size = tmp->ob_size;
	for (i = 0; i < n; i++)
		newobj->ob_digit[i] = tmp->ob_digit[i];
	Py_DECREF(tmp);
	return (PyObject *)newobj;
}

static PyObject *
long_getnewargs(PyLongObject *v)
{
	return Py_BuildValue("(N)", _PyLong_Copy(v));
}

static PyMethodDef long_methods[] = {
	{"__getnewargs__",	(PyCFunction)long_getnewargs,	METH_NOARGS},
	{NULL,		NULL}		/* sentinel */
};

PyDoc_STRVAR(long_doc,
"long(x[, base]) -> integer\n\
\n\
Convert a string or number to a long integer, if possible.  A floating\n\
point argument will be truncated towards zero (this does not include a\n\
string representation of a floating point number!)  When converting a\n\
string, use the optional base.  It is an error to supply a base when\n\
converting a non-string.");

static PyNumberMethods long_as_number = {
	(binaryfunc)	long_add,	/*nb_add*/
	(binaryfunc)	long_sub,	/*nb_subtract*/
	(binaryfunc)	long_mul,	/*nb_multiply*/
			long_classic_div, /*nb_divide*/
			long_mod,	/*nb_remainder*/
			long_divmod,	/*nb_divmod*/
			long_pow,	/*nb_power*/
	(unaryfunc) 	long_neg,	/*nb_negative*/
	(unaryfunc) 	long_pos,	/*tp_positive*/
	(unaryfunc) 	long_abs,	/*tp_absolute*/
	(inquiry)	long_nonzero,	/*tp_nonzero*/
	(unaryfunc)	long_invert,	/*nb_invert*/
			long_lshift,	/*nb_lshift*/
	(binaryfunc)	long_rshift,	/*nb_rshift*/
			long_and,	/*nb_and*/
			long_xor,	/*nb_xor*/
			long_or,	/*nb_or*/
			long_coerce,	/*nb_coerce*/
			long_int,	/*nb_int*/
			long_long,	/*nb_long*/
			long_float,	/*nb_float*/
			long_oct,	/*nb_oct*/
			long_hex,	/*nb_hex*/
	0,				/* nb_inplace_add */
	0,				/* nb_inplace_subtract */
	0,				/* nb_inplace_multiply */
	0,				/* nb_inplace_divide */
	0,				/* nb_inplace_remainder */
	0,				/* nb_inplace_power */
	0,				/* nb_inplace_lshift */
	0,				/* nb_inplace_rshift */
	0,				/* nb_inplace_and */
	0,				/* nb_inplace_xor */
	0,				/* nb_inplace_or */
	long_div,			/* nb_floor_divide */
	long_true_divide,		/* nb_true_divide */
	0,				/* nb_inplace_floor_divide */
	0,				/* nb_inplace_true_divide */
	long_long,			/* nb_index */
};

PyTypeObject PyLong_Type = {
	PyObject_HEAD_INIT(&PyType_Type)
	0,					/* ob_size */
	"long",					/* tp_name */
	sizeof(PyLongObject) - sizeof(digit),	/* tp_basicsize */
	sizeof(digit),				/* tp_itemsize */
	long_dealloc,				/* tp_dealloc */
	0,					/* tp_print */
	0,					/* tp_getattr */
	0,					/* tp_setattr */
	(cmpfunc)long_compare,			/* tp_compare */
	long_repr,				/* tp_repr */
	&long_as_number,			/* tp_as_number */
	0,					/* tp_as_sequence */
	0,					/* tp_as_mapping */
	(hashfunc)long_hash,			/* tp_hash */
        0,              			/* tp_call */
        long_str,				/* tp_str */
	PyObject_GenericGetAttr,		/* tp_getattro */
	0,					/* tp_setattro */
	0,					/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_CHECKTYPES |
		Py_TPFLAGS_BASETYPE,		/* tp_flags */
	long_doc,				/* tp_doc */
	0,					/* tp_traverse */
	0,					/* tp_clear */
	0,					/* tp_richcompare */
	0,					/* tp_weaklistoffset */
	0,					/* tp_iter */
	0,					/* tp_iternext */
	long_methods,				/* tp_methods */
	0,					/* tp_members */
	0,					/* tp_getset */
	0,					/* tp_base */
	0,					/* tp_dict */
	0,					/* tp_descr_get */
	0,					/* tp_descr_set */
	0,					/* tp_dictoffset */
	0,					/* tp_init */
	0,					/* tp_alloc */
	long_new,				/* tp_new */
	PyObject_Del,                           /* tp_free */
};
