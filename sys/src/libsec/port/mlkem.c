#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>

enum{
	K512 = 2,
	K768,
	K1024,
};

#define MLKEM_N 256
#define MLKEM_Q 3329
#define MLKEM_Q_HALF ((MLKEM_Q + 1) / 2) /* 1665 */

#define MLKEM_SYMBYTES 32 /* size in bytes of hashes, and seeds */

#define MLKEM_POLYBYTES 384

#define MLKEM_POLYCOMPRESSEDBYTES_D4 128
#define MLKEM_POLYCOMPRESSEDBYTES_D5 160
#define MLKEM_POLYCOMPRESSEDBYTES_D10 320
#define MLKEM_POLYCOMPRESSEDBYTES_D11 352

/* Macros denoting FIPS 203 specific Hash functions */

/* Hash function H, @[FIPS203, Section 4.1, Eq (4.4)] */
#define mlk_hash_h(OUT, IN, INBYTES) sha3_256(IN, INBYTES, OUT, nil)

/* Hash function G, @[FIPS203, Section 4.1, Eq (4.5)] */
#define mlk_hash_g(OUT, IN, INBYTES) sha3_512(IN, INBYTES, OUT, nil)

/* Hash function J, @[FIPS203, Section 4.1, Eq (4.4)] */
#define mlk_hash_j(OUT, IN, INBYTES) \
  shake_256(IN, INBYTES, OUT, MLKEM_SYMBYTES)

/* PRF function, @[FIPS203, Section 4.1, Eq (4.3)]
 * Referring to (eq 4.3), `OUT` is assumed to contain `s || b`. */
#define mlk_prf_eta(ETA, OUT, IN) \
  shake_256(IN, MLKEM_SYMBYTES + 1, OUT, (ETA) * MLKEM_N / 4)

#define SHAKE128_RATE 168
#define MLK_XOF_RATE SHAKE128_RATE

ulong tslsel(ulong, ulong, ulong);
void tsmemsel(void*, void*, void*, ulong, ulong);

/**
 * Element of R_q = Z_q[X]/(X^n + 1). Represents polynomial
 * coeffs[0] + X*coeffs[1] + X^2*coeffs[2] + ... + X^{n-1}*coeffs[n-1].
 */
typedef struct {
	short coeffs[MLKEM_N]; /**< Polynomial coefficients. */
} mlk_poly;

/**
 * INTERNAL representation of precomputed data speeding up
 * the base multiplication of two polynomials in NTT domain.
 */
typedef struct {
	short coeffs[MLKEM_N >> 1]; /**< Cached coefficients. */
} mlk_poly_mulcache;

/* Sized up to the max it can be for 1024 FIXME(?) */
typedef struct {
	mlk_poly vec[K1024];
} mlk_polyvec;

typedef struct {
	mlk_polyvec vec[K1024];
} mlk_polymat;

typedef struct {
	mlk_poly_mulcache vec[K1024];
} mlk_polyvec_mulcache;

#define MLKEM_POLYVECBYTES(lvl) (lvl * MLKEM_POLYBYTES)
#define MLKEM_PUBLICKEYBYTES(lvl) (MLKEM_POLYVECBYTES(lvl) + MLKEM_SYMBYTES)

/* 32 bytes of additional space to save H(pk) */
#define MLKEM_SECRETKEYBYTES(lvl) (MLKEM_POLYVECBYTES(lvl) + MLKEM_PUBLICKEYBYTES(lvl) + 2 * MLKEM_SYMBYTES)

#define MLKEM512_IND_BYTES (MLKEM_POLYCOMPRESSEDBYTES_D4 + MLKEM_POLYCOMPRESSEDBYTES_D10 * K512)
#define MLKEM768_IND_BYTES (MLKEM_POLYCOMPRESSEDBYTES_D4 + MLKEM_POLYCOMPRESSEDBYTES_D10 * K768)
#define MLKEM1024_IND_BYTES (MLKEM_POLYCOMPRESSEDBYTES_D5 + MLKEM_POLYCOMPRESSEDBYTES_D11 * K1024)

/**
 * Generic Montgomery reduction; given a 32-bit integer a, computes a 16-bit
 * integer congruent to a * R^-1 mod MLKEM_Q, where R=2^16.
 */
static short
mlk_montgomery_reduce(long a)
{
	/* check-magic: 62209 == unsigned_mod(pow(MLKEM_Q, -1, 2^16), 2^16) */
	const ulong QINV = 62209;

	/* Compute a*q^{-1} mod 2^16 in unsigned representatives. */
	const u16int a_reduced = (u16int)(a & (long)0xffff);
	const u16int a_inverted = (a_reduced * QINV) & 0xffff;

	/* Lift to signed canonical representative mod 2^16. */
	const short t = (short)a_inverted;

	long r;

	r = a - ((long)t * MLKEM_Q);

	/*
	 * PORTABILITY: Right-shift on a signed integer is, strictly-speaking,
	 * implementation-defined for negative left argument. Here,
	 * we assume it's sign-preserving "arithmetic" shift right. (C99 6.5.7 (5))
	 */
	r = r >> 16;

	/* Bounds: |r >> 16| <= ceil(|r| / 2^16)
	 *	<= ceil(|a| / 2^16 + MLKEM_Q / 2)
	 *	<= ceil(|a| / 2^16) + (MLKEM_Q + 1) / 2
	 * (Note that |a >> n| = ceil(|a| / 2^16) for negative a)
	 */
	return r;
}

/**
 * Barrett reduction; given a 16-bit integer a, computes the centered
 * representative congruent to a mod MLKEM_Q in [-(MLKEM_Q-1)/2, (MLKEM_Q-1)/2].
 *
 */
static short
mlk_barrett_reduce(short a)
{
	/* Barrett reduction approximates
	 * ```
	 *		 round(a/MLKEM_Q)
	 *	 = round(a*(2^N/MLKEM_Q))/2^N)
	 *	~= round(a*round(2^N/MLKEM_Q)/2^N)
	 * ```
	 * Here, we pick N=26.
	 * PORTABILITY: Right-shift on a signed integer is
	 * implementation-defined for negative left argument.
	 * Here, we assume it's sign-preserving "arithmetic" shift right.
	 * See (C99 6.5.7 (5))
	 */
	const long t = (20159 * a + ((long)1 << 25)) >> 26;

	/*
	 * t is in -10 .. +10, so we need 32-bit math to
	 * evaluate t * MLKEM_Q and the subsequent subtraction
	 */
	return a - t * MLKEM_Q;
}

/* Reference: `poly_tomont()` in the reference implementation @[REF]. */
static void
mlk_poly_tomont(mlk_poly *r)
{
	unsigned i;
	for(i = 0; i < MLKEM_N; i++)
		r->coeffs[i] = mlk_montgomery_reduce((long)r->coeffs[i] * 1353);
}

/**
 * Constant-time conversion of signed representatives modulo MLKEM_Q within
 * range [-(MLKEM_Q-1), MLKEM_Q-1] into unsigned representatives within
 * range [0, MLKEM_Q-1].
 */
static short
mlk_scalar_signed_to_unsigned_q(short c)
{

	/* Add MLKEM_Q if c is negative, but in constant time.
	 *
	 * Note that c + MLKEM_Q does not overflow in short,
	 * so the cast to u16int is safe. */
	c = tslsel(c + MLKEM_Q, c, (u16int)(((long)c)>>16));

	return c;
}

/* Reference: `poly_reduce()` in the reference implementation @[REF]
 * - We use _unsigned_ canonical outputs, while the reference
 *	implementation uses _signed_ canonical outputs.
 *	Accordingly, we need a conditional addition of MLKEM_Q
 *	here to go from signed to unsigned representatives.
 *	This conditional addition is then dropped from all
 *	polynomial compression functions instead (see `compress.c`). */
static void
mlk_poly_reduce(mlk_poly *r)
{
	unsigned i;

	for(i = 0; i < MLKEM_N; i++){
		/* Barrett reduction, giving signed canonical representative */
		short t = mlk_barrett_reduce(r->coeffs[i]);
		/* Conditional addition to get unsigned canonical representative */
		r->coeffs[i] = mlk_scalar_signed_to_unsigned_q(t);
	}

}

/* Reference: `poly_add()` in the reference implementation @[REF].
 * - We use destructive version (output=first input) to avoid
 *	reasoning about aliasing in the CBMC specification */
static void
mlk_poly_add(mlk_poly *r, const mlk_poly *b)
{
	unsigned i;
	for(i = 0; i < MLKEM_N; i++){
		/* The preconditions imply that the addition stays within short. */
		r->coeffs[i] = (short)(r->coeffs[i] + b->coeffs[i]);
	}
}

/* Reference: `poly_sub()` in the reference implementation @[REF].
 * - We use destructive version (output=first input) to avoid
 *	reasoning about aliasing in the CBMC specification */
static void
mlk_poly_sub(mlk_poly *r, const mlk_poly *b)
{
	unsigned i;
	for(i = 0; i < MLKEM_N; i++){
		/* The preconditions imply that the subtraction stays within short. */
		r->coeffs[i] = (short)(r->coeffs[i] - b->coeffs[i]);
	}
}

static int mlk_zetas[128] = {
	 -1044, -758, -359, -1517, 1493, 1422, 287,  202, -171, 622,  1577,
	 182,  962,  -1202, -1474, 1468, 573,  -1325, 264, 383,  -829, 1458,
	 -1602, -130, -681, 1017, 732,  608,  -1542, 411, -205, -1571, 1223,
	 652,  -552, 1015, -1293, 1491, -282, -1544, 516, -8,  -320, -666,
	 -1618, -1162, 126,  1469, -853, -90,  -271, 830, 107,  -1421, -247,
	 -951, -398, 961,  -1508, -725, 448,  -1065, 677, -1275, -1103, 430,
	 555,  843,  -1251, 871,  1550, 105,  422,  587, 177,  -235, -291,
	 -460, 1574, 1653, -246, 778,  1159, -147, -777, 1483, -602, 1119,
	 -1590, 644,  -872, 349,  418,  329,  -156, -75, 817,  1097, 603,
	 610,  1322, -1285, -1465, 384,  -1215, -136, 1218, -1335, -874, 220,
	 -1187, -1659, -1185, -1530, -1278, 794,  -1510, -854, -870, 478,  -108,
	 -308, 996,  991,  958,  -1460, 1522, 1628,
};

/* Reference: Does not exist in the reference implementation @[REF].
 * - The reference implementation does not use a
 *	multiplication cache ('mulcache'). This idea originates
 *	from @[NeonNTT] and is used at the C level here. */
static void
mlk_poly_mulcache_compute(mlk_poly_mulcache *x, const mlk_poly *a)
{
	unsigned i;
	for(i = 0; i < MLKEM_N / 4; i++){
		x->coeffs[2 * i + 0] = mlk_montgomery_reduce(a->coeffs[4 * i + 1] * mlk_zetas[64 + i]);
		/* The values in zeta table are <= MLKEM_Q in absolute value,
		 * so the negation in short is safe. */
		x->coeffs[2 * i + 1] = mlk_montgomery_reduce(a->coeffs[4 * i + 3] * -mlk_zetas[64 + i]);
	}
}

/* manually inlined compared to upstream mlk_poly_ntt */
static void
mlk_poly_ntt(mlk_poly *p)
{
	unsigned layer;
	short *r;
	unsigned start, k, len;
	unsigned j;

	r = p->coeffs;

	for(layer = 1; layer <= 7; layer++){
		/* Twiddle factors for layer n are at indices 2^(n-1)..2^n-1. */
		k = 1u << (layer - 1);
		len = (unsigned)MLKEM_N >> layer;
		for(start = 0; start < MLKEM_N; start += 2 * len){
			short zeta = mlk_zetas[k++];
			for(j = start; j < start + len; j++){
				short t;
				t = mlk_montgomery_reduce((long)r[j + len] * (long)zeta);
				/* The precondition implies that the arithmetic does not overflow. */
				r[j + len] = r[j] - t;
				r[j] = r[j] + t;
			}
		}
	}
}

/* Reference: `invntt()` in the reference implementation @[REF]
 * - We normalize at the beginning of the inverse NTT,
 *	while the reference implementation normalizes at
 *	the end. This allows us to drop a call to `poly_reduce()`
 *	from the base multiplication. */
static void
mlk_poly_invntt_tomont(mlk_poly *p)
{
	unsigned j, layer;
	unsigned start, k, len;
	short zeta;
	short *r = p->coeffs;

	/*
	 * Scale input polynomial to account for Montgomery factor
	 * and NTT twist. This also brings coefficients down to
	 * absolute value < MLKEM_Q.
	 */
	for(j = 0; j < MLKEM_N; j++)
		r[j] = mlk_montgomery_reduce((long)r[j] * 1441);

	/* Run the invNTT layers */
	for(layer = 7; layer > 0; layer--){
		len = (unsigned)MLKEM_N >> layer;
		k = (1u << layer) - 1;

		for(start = 0; start < MLKEM_N; start += 2 * len){
			zeta = mlk_zetas[k--];

			for(j = start; j < start + len; j++){
				short t = r[j];
				/* The preconditions imply that the arithmetic does not overflow. */
				r[j] = mlk_barrett_reduce((short)(t + r[j + len]));
				r[j + len] = r[j + len] - t;
				r[j + len] = mlk_montgomery_reduce(r[j + len] * zeta);
			}
		}
	}
}

/**
 * Run rejection sampling on uniform random bytes to generate uniform random
 * integers mod MLKEM_Q.
 *
 * @reference{`rej_uniform()` in the reference implementation @[REF]. Our
 * signature differs from the reference in that it adds the offset and always
 * expects the base of the target buffer; this avoids shifting the buffer
 * base in the caller, which is tricky to reason about.
 */

/* Reference: `rej_uniform()` in the reference implementation @[REF].
 * - Our signature differs from the reference implementation
 *	in that it adds the offset and always expects the base of the
 *	target buffer. This avoids shifting the buffer base in the
 *	caller, which appears tricky to reason about. */
static unsigned
mlk_rej_uniform(short *r, unsigned target, unsigned offset, const uchar *buf, unsigned buflen)
{
	unsigned ctr, pos;
	short val0, val1;

	ctr = offset;
	pos = 0;
	/* pos + 3 cannot overflow due to the assumption buflen <= 4096 */
	while(ctr < target && pos + 3 <= buflen){
		val0 = ((buf[pos + 0] >> 0) | (buf[pos + 1] << 8)) & 0xFFF;
		val1 = ((buf[pos + 1] >> 4) | (buf[pos + 2] << 4)) & 0xFFF;
		pos += 3;

		if(val0 < MLKEM_Q)
			r[ctr++] = val0;
		if(ctr < target && val1 < MLKEM_Q)
			r[ctr++] = val1;
	}

	return ctr;
}

#define MLKEM_GEN_MATRIX_NBLOCKS \
	((12 * MLKEM_N / 8 * ((ulong)1 << 12) / MLKEM_Q + MLK_XOF_RATE) / \
	 MLK_XOF_RATE)

static void
mlk_poly_rej_uniform(mlk_poly *entry, uchar seed[MLKEM_SYMBYTES + 2])
{
	struct {
		DigestState d;
		XOFState x;
	} state;
	uchar buf[MLKEM_GEN_MATRIX_NBLOCKS * MLK_XOF_RATE];
	unsigned ctr, buflen;

	memset(&state, 0, sizeof state);
	shake_128_in(seed, MLKEM_SYMBYTES + 2, &state.d);
	shake_128_conv(&state.x, &state.d);

	/* Initially, squeeze + sample heuristic number of MLKEM_GEN_MATRIX_NBLOCKS. */
	/* This should generate the matrix entry with high probability. */
	shake_128_out(buf, MLKEM_GEN_MATRIX_NBLOCKS * SHAKE128_RATE, &state.x);
	buflen = MLKEM_GEN_MATRIX_NBLOCKS * MLK_XOF_RATE;
	ctr = mlk_rej_uniform(entry->coeffs, MLKEM_N, 0, buf, buflen);

	/* Squeeze + sample one more block a time until we're done */
	buflen = MLK_XOF_RATE;
	while(ctr < MLKEM_N){
		shake_128_out(buf, SHAKE128_RATE, &state.x);
		ctr = mlk_rej_uniform(entry->coeffs, MLKEM_N, ctr, buf, buflen);
	}

	memset(&state, 0, sizeof state);
	memset(buf, 0, sizeof(buf));
}

/**
 * Load 4 bytes into a 32-bit integer in little-endian order.
 *
 * @reference{`load32_littleendian()` in the reference implementation @[REF].}
 *
 * @param[in] x Input byte array.
 *
 * @return 32-bit unsigned integer loaded from @p x.
 */
static ulong
mlk_load32_littleendian(const uchar x[4])
{
	ulong r;
	r = (ulong)x[0];
	r |= (ulong)x[1] << 8;
	r |= (ulong)x[2] << 16;
	r |= (ulong)x[3] << 24;
	return r;
}

/* Reference: `cbd2()` in the reference implementation @[REF]. */
static void
mlk_poly_cbd2(mlk_poly *r, const uchar buf[2 * MLKEM_N / 4])
{
	unsigned i;
	for(i = 0; i < MLKEM_N / 8; i++){
		unsigned j;
		ulong t = mlk_load32_littleendian(buf + 4 * i);
		ulong d = t & 0x55555555;
		d += (t >> 1) & 0x55555555;

		for(j = 0; j < 8; j++){
			const short a = (d >> (4 * j + 0)) & 0x3;
			const short b = (d >> (4 * j + 2)) & 0x3;
			r->coeffs[8 * i + j] = (short)(a - b);
		}
	}
}

/**
 * Load 3 bytes into a 32-bit integer in little-endian order.
 *
 * This function is only needed for ML-KEM-512.
 *
 * @reference{`load24_littleendian()` in the reference implementation @[REF].}
 *
 * @param[in] x Input byte array.
 *
 * @return 32-bit unsigned integer loaded from @p x (most significant byte is zero).
 */
static ulong
mlk_load24_littleendian(const uchar x[3])
{
	ulong r;
	r = (ulong)x[0];
	r |= (ulong)x[1] << 8;
	r |= (ulong)x[2] << 16;
	return r;
}

/* Reference: `cbd3()` in the reference implementation @[REF]. */
static void
mlk_poly_cbd3(mlk_poly *r, const uchar buf[3 * MLKEM_N / 4])
{
	unsigned i;
	for(i = 0; i < MLKEM_N / 4; i++){
		unsigned j;
		const ulong t = mlk_load24_littleendian(buf + 3 * i);
		ulong d = t & 0x00249249;
		d += (t >> 1) & 0x00249249;
		d += (t >> 2) & 0x00249249;

		for(j = 0; j < 4; j++){
			const short a = (d >> (6 * j + 0)) & 0x7;
			const short b = (d >> (6 * j + 3)) & 0x7;
			r->coeffs[4 * i + j] = (short)(a - b);
		}
	}
}
/**
 * Compute round(u * 2 / MLKEM_Q).
 */
static uchar
mlk_scalar_compress_d1(short u)
{
	/* Compute as follows:
	 * ```
	 * round(u * 2 / MLKEM_Q)
	 *	 = round(u * 2 * (2^31 / MLKEM_Q) / 2^31)
	 *	~= round(u * 2 * round(2^31 / MLKEM_Q) / 2^31)
	 * ```
	 */
	/* check-magic: 1290168 == 2*round(2^31 / MLKEM_Q) */
	ulong d0 = (ulong)u * 1290168;
	/* Unsigned shifting by 31 positions leaves only the top bit. */
	return (uchar)((d0 + ((ulong)1u << 30)) >> 31);
}

/*
 * The multiplication in this routine will exceed UINT32_MAX
 * and wrap around for large values of u. This is expected and required.
 *
 * Compute round(u * 16 / MLKEM_Q) % 16.
 */
static uchar
mlk_scalar_compress_d4(short u)
{
	/* Compute as follows:
	 * ```
	 * round(u * 16 / MLKEM_Q)
	 *	 = round(u * 16 * (2^28 / MLKEM_Q) / 2^28)
	 *	~= round(u * 16 * round(2^28 / MLKEM_Q) / 2^28)
	 * ```
	 */
	/* check-magic: 1290160 == 16 * round(2^28 / MLKEM_Q) */
	ulong d0 = (ulong)u * 1290160;
	/* The return value is < 16, so not altered by the conversion to uchar. */
	return (d0 + ((ulong)1u << 27)) >> 28; /* round(d0/2^28) */
}

/**
 * Compute round(u * MLKEM_Q / 16).
 *
 */
static short
mlk_scalar_decompress_d4(ulong u)
{
	/* The return value is in 0..MLKEM_Q-1, hence not altered by the
	 * conversion to short. */
	return (u * MLKEM_Q + 8) >> 4;
}

/*
 * The multiplication in this routine will exceed UINT32_MAX
 * and wrap around for large values of u. This is expected and required.
 */

/**
 * Compute round(u * 32 / MLKEM_Q) % 32.
 *
 */
static uchar
mlk_scalar_compress_d5(short u)
{
	/* Compute as follows:
	 * ```
	 * round(u * 32 / MLKEM_Q)
	 *	 = round(u * 32 * (2^27 / MLKEM_Q) / 2^27)
	 *	~= round(u * 32 * round(2^27 / MLKEM_Q) / 2^27)
	 * ```
	 */
	/* check-magic: 1290176 == 2^5 * round(2^27 / MLKEM_Q) */
	ulong d0 = (ulong)u * 1290176;
	/* The return value is < 32, so not altered by the conversion to uchar. */
	return (d0 + ((ulong)1u << 26)) >> 27; /* round(d0/2^27) */
}

/**
 * Compute round(u * MLKEM_Q / 32).
 */
static short
mlk_scalar_decompress_d5(ulong u)
{
	/* The return value is in 0..MLKEM_Q-1, hence not altered by the
	 * conversion to short. */
	return ((u * MLKEM_Q) + 16) >> 5;
}

/*
 * The multiplication in this routine will exceed UINT32_MAX
 * and wrap around for large values of u. This is expected and required.
 */

/**
 * Compute round(u * 2**10 / MLKEM_Q) % 2**10.
 *
 */
static u16int
mlk_scalar_compress_d10(short u)
{
	/* Compute as follows:
	 * ```
	 * round(u * 1024 / MLKEM_Q)
	 *	 = round(u * 1024 * (2^33 / MLKEM_Q) / 2^33)
	 *	~= round(u * 1024 * round(2^33 / MLKEM_Q) / 2^33)
	 * ```
	 */
	/* check-magic: 2642263040 == 2^10 * round(2^33 / MLKEM_Q) */
	uvlong d0 = (uvlong)u * 2642263040ULL;
	d0 = (d0 + ((uvlong)1u << 32)) >> 33; /* round(d0/2^33) */
	return (d0 & 0x3FF);
}

/**
 * Compute round(u * MLKEM_Q / 1024).
 */
static short
mlk_scalar_decompress_d10(u16int u)
{
	/* The return value is in 0..MLKEM_Q-1, hence not altered by the
	 * conversion to short. */
	return (short)((((ulong)u * MLKEM_Q) + 512) >> 10);
}

/*
 * The multiplication in this routine will exceed UINT32_MAX
 * and wrap around for large values of u. This is expected and required.
 */

/**
 * Compute round(u * 2**11 / MLKEM_Q) % 2**11.
 *
 */
static u16int
mlk_scalar_compress_d11(short u)
{
	/* Compute as follows:
	 * ```
	 * round(u * 2048 / MLKEM_Q)
	 *	 = round(u * 2048 * (2^33 / MLKEM_Q) / 2^33)
	 *	~= round(u * 2048 * round(2^33 / MLKEM_Q) / 2^33)
	 * ```
	 */
	/* check-magic: 5284526080 == 2^11 * round(2^33 / MLKEM_Q) */
	uvlong d0 = (uvlong)u * 5284526080;
	d0 = (d0 + ((uvlong)1u << 32)) >> 33; /* round(d0/2^33) */
	return (d0 & 0x7FF);
}

/**
 * Compute round(u * MLKEM_Q / 2048).
 */
static short
mlk_scalar_decompress_d11(u16int u)
{
	/* The return value is in 0..MLKEM_Q-1, hence not altered by the
	 * conversion to short. */
	return (short)((((ulong)u * MLKEM_Q) + 1024) >> 11);
}

/* Reference: `poly_compress()` in the reference implementation @[REF], for ML-KEM-{512,768}.
 * - In contrast to the reference implementation, we assume
 *	unsigned canonical coefficients here.
 *	The reference implementation works with coefficients
 *	in the range [-(MLKEM_Q-1), MLKEM_Q-1]. */
static void
mlk_poly_compress_d4(uchar r[MLKEM_POLYCOMPRESSEDBYTES_D4], const mlk_poly *a)
{
	unsigned i;

	for(i = 0; i < MLKEM_N / 8; i++){
		unsigned j;
		uchar t[8] = {0};
		for(j = 0; j < 8; j++)
			t[j] = mlk_scalar_compress_d4(a->coeffs[8 * i + j]);

		/* All t[i] are 4-bit wide, so the truncations don't alter the value. */
		r[i * 4] = (uchar)(t[0] | (t[1] << 4));
		r[i * 4 + 1] = (uchar)(t[2] | (t[3] << 4));
		r[i * 4 + 2] = (uchar)(t[4] | (t[5] << 4));
		r[i * 4 + 3] = (uchar)(t[6] | (t[7] << 4));
	}
}

/* Reference: Embedded into `polyvec_compress()` in the reference implementation, for ML-KEM-{512,768}.
 * - In contrast to the reference implementation, we assume
 * 	unsigned canonical coefficients here.
 *	The reference implementation works with coefficients
 *	in the range [-(MLKEM_Q-1), MLKEM_Q-1]. */
static void
mlk_poly_compress_d10(uchar r[MLKEM_POLYCOMPRESSEDBYTES_D10], const mlk_poly *a)
{
	unsigned j;
	for(j = 0; j < MLKEM_N / 4; j++){
		unsigned k;
		u16int t[4];
		for(k = 0; k < 4; k++)
			t[k] = mlk_scalar_compress_d10(a->coeffs[4 * j + k]);

		/*
		 * Make all implicit truncation explicit. No data is being
		 * truncated for the LHS's since each t[i] is 10-bit in size.
		 */
		r[5 * j + 0] = (uchar)((t[0] >> 0) & 0xFF);
		r[5 * j + 1] = (uchar)((t[0] >> 8) | ((t[1] << 2) & 0xFF));
		r[5 * j + 2] = (uchar)((t[1] >> 6) | ((t[2] << 4) & 0xFF));
		r[5 * j + 3] = (uchar)((t[2] >> 4) | ((t[3] << 6) & 0xFF));
		r[5 * j + 4] = (uchar)(t[3] >> 2);
	}
}

/* Reference: `poly_decompress()` in the reference implementation @[REF], for ML-KEM-{512,768}. */
static void
mlk_poly_decompress_d4(mlk_poly *r, const uchar a[MLKEM_POLYCOMPRESSEDBYTES_D4])
{
	unsigned i;
	for(i = 0; i < MLKEM_N / 2; i++){
		r->coeffs[2 * i + 0] = mlk_scalar_decompress_d4((a[i] >> 0) & 0xF);
		r->coeffs[2 * i + 1] = mlk_scalar_decompress_d4((a[i] >> 4) & 0xF);
	}

}

/* Reference: Embedded into `polyvec_decompress()` in the reference implementation, for ML-KEM-{512,768}. */
static void
mlk_poly_decompress_d10(mlk_poly *r, const uchar a[MLKEM_POLYCOMPRESSEDBYTES_D10])
{
	unsigned j;
	for(j = 0; j < MLKEM_N / 4; j++){
		unsigned k;
		u16int t[4];
		uchar const *base = &a[5 * j];

		t[0] = 0x3FF & ((base[0] >> 0) | ((u16int)base[1] << 8));
		t[1] = 0x3FF & ((base[1] >> 2) | ((u16int)base[2] << 6));
		t[2] = 0x3FF & ((base[2] >> 4) | ((u16int)base[3] << 4));
		t[3] = 0x3FF & ((base[3] >> 6) | ((u16int)base[4] << 2));

		for(k = 0; k < 4; k++)
			r->coeffs[4 * j + k] = mlk_scalar_decompress_d10(t[k]);
	}

}

/* Reference: `poly_compress()` in the reference implementation @[REF], for ML-KEM-1024.
 * - In contrast to the reference implementation, we assume
 *	unsigned canonical coefficients here.
 *	The reference implementation works with coefficients
 *	in the range [-(MLKEM_Q-1), MLKEM_Q-1]. */
static void
mlk_poly_compress_d5(uchar r[MLKEM_POLYCOMPRESSEDBYTES_D5], const mlk_poly *a)
{
	unsigned i;

	for(i = 0; i < MLKEM_N / 8; i++){
		unsigned j;
		uchar t[8] = {0};
		for(j = 0; j < 8; j++)
			t[j] = mlk_scalar_compress_d5(a->coeffs[8 * i + j]);

		r[i * 5] = (uchar)(0xFF & ((t[0] >> 0) | (t[1] << 5)));
		r[i * 5 + 1] = (uchar)(0xFF & ((t[1] >> 3) | (t[2] << 2) | (t[3] << 7)));
		r[i * 5 + 2] = (uchar)(0xFF & ((t[3] >> 1) | (t[4] << 4)));
		r[i * 5 + 3] = (uchar)(0xFF & ((t[4] >> 4) | (t[5] << 1) | (t[6] << 6)));
		r[i * 5 + 4] = (uchar)(0xFF & ((t[6] >> 2) | (t[7] << 3)));
	}
}

/* Reference: Embedded into `polyvec_compress()` in the reference implementation, for ML-KEM-1024.
 * - In contrast to the reference implementation, we assume
 *	unsigned canonical coefficients here.
 *	The reference implementation works with coefficients
 *	in the range [-(MLKEM_Q-1), MLKEM_Q-1]. */
static void
mlk_poly_compress_d11(uchar r[MLKEM_POLYCOMPRESSEDBYTES_D11], const mlk_poly *a)
{
	unsigned j;

	for(j = 0; j < MLKEM_N / 8; j++){
		unsigned k;
		u16int t[8];
		for(k = 0; k < 8; k++)
			t[k] = mlk_scalar_compress_d11(a->coeffs[8 * j + k]);

		/*
		 * Make all implicit truncation explicit. No data is being
		 * truncated for the LHS's since each t[i] is 11-bit in size.
		 */
		r[11 * j + 0] = (uchar)((t[0] >> 0) & 0xFF);
		r[11 * j + 1] = (uchar)((t[0] >> 8) | ((t[1] << 3) & 0xFF));
		r[11 * j + 2] = (uchar)((t[1] >> 5) | ((t[2] << 6) & 0xFF));
		r[11 * j + 3] = (uchar)((t[2] >> 2) & 0xFF);
		r[11 * j + 4] = (uchar)((t[2] >> 10) | ((t[3] << 1) & 0xFF));
		r[11 * j + 5] = (uchar)((t[3] >> 7) | ((t[4] << 4) & 0xFF));
		r[11 * j + 6] = (uchar)((t[4] >> 4) | ((t[5] << 7) & 0xFF));
		r[11 * j + 7] = (uchar)((t[5] >> 1) & 0xFF);
		r[11 * j + 8] = (uchar)((t[5] >> 9) | ((t[6] << 2) & 0xFF));
		r[11 * j + 9] = (uchar)((t[6] >> 6) | ((t[7] << 5) & 0xFF));
		r[11 * j + 10] = (uchar)(t[7] >> 3);
	}
}

/* Reference: `poly_decompress()` in the reference implementation @[REF], for ML-KEM-1024. */
static void
mlk_poly_decompress_d5(mlk_poly *r, const uchar a[MLKEM_POLYCOMPRESSEDBYTES_D5])
{
	unsigned i;
	for(i = 0; i < MLKEM_N / 8; i++){
		unsigned j;
		uchar t[8];
		const unsigned offset = i * 5;
		/*
		 * Explicitly truncate to avoid warning about
		 * implicit truncation in CBMC and unwind loop for ease
		 * of proof.
		 */

		/*
		 * Decompress 5 8-bit bytes (so 40 bits) into
		 * 8 5-bit values stored in t[]
		 */
		t[0] = 0x1F & (a[offset + 0] >> 0);
		t[1] = 0x1F & ((a[offset + 0] >> 5) | (a[offset + 1] << 3));
		t[2] = 0x1F & (a[offset + 1] >> 2);
		t[3] = 0x1F & ((a[offset + 1] >> 7) | (a[offset + 2] << 1));
		t[4] = 0x1F & ((a[offset + 2] >> 4) | (a[offset + 3] << 4));
		t[5] = 0x1F & (a[offset + 3] >> 1);
		t[6] = 0x1F & ((a[offset + 3] >> 6) | (a[offset + 4] << 2));
		t[7] = 0x1F & (a[offset + 4] >> 3);

		/* and copy to the correct slice in r[] */
		for(j = 0; j < 8; j++)
			r->coeffs[8 * i + j] = mlk_scalar_decompress_d5(t[j]);
	}

}

/* Reference: Embedded into `polyvec_decompress()` in the reference implementation, for ML-KEM-1024. */
static void
mlk_poly_decompress_d11(mlk_poly *r, const uchar a[MLKEM_POLYCOMPRESSEDBYTES_D11])
{
	unsigned j;
	for(j = 0; j < MLKEM_N / 8; j++){
		unsigned k;
		u16int t[8];
		uchar const *base = &a[11 * j];
		t[0] = 0x7FF & ((base[0] >> 0) | ((u16int)base[1] << 8));
		t[1] = 0x7FF & ((base[1] >> 3) | ((u16int)base[2] << 5));
		t[2] = 0x7FF & ((base[2] >> 6) | ((u16int)base[3] << 2) | ((u16int)base[4] << 10));
		t[3] = 0x7FF & ((base[4] >> 1) | ((u16int)base[5] << 7));
		t[4] = 0x7FF & ((base[5] >> 4) | ((u16int)base[6] << 4));
		t[5] = 0x7FF & ((base[6] >> 7) | ((u16int)base[7] << 1) | ((u16int)base[8] << 9));
		t[6] = 0x7FF & ((base[8] >> 2) | ((u16int)base[9] << 6));
		t[7] = 0x7FF & ((base[9] >> 5) | ((u16int)base[10] << 3));

		for(k = 0; k < 8; k++)
			r->coeffs[8 * j + k] = mlk_scalar_decompress_d11(t[k]);
	}

}

/* Reference: `poly_tobytes()` in the reference implementation @[REF].
 * - In contrast to the reference implementation, we assume
 *	unsigned canonical coefficients here.
 *	The reference implementation works with coefficients
 *	in the range [-(MLKEM_Q-1), MLKEM_Q-1]. */
static void
mlk_poly_tobytes(uchar r[MLKEM_POLYBYTES], const mlk_poly *a)
{
	unsigned i;

	for(i = 0; i < MLKEM_N / 2; i++){
		/* The conversion to u16int is safe since we assume that
		 * the coefficients of `a` are non-negative. */
		const u16int t0 = a->coeffs[2 * i];
		const u16int t1 = a->coeffs[2 * i + 1];
		/*
		 * t0 and t1 are both < MLKEM_Q, so contain at most 12 bits each of
		 * significant data, so these can be packed into 24 bits or exactly
		 * 3 bytes, as follows.
		 */

		/* Least significant bits 0 - 7 of t0. */
		r[3 * i + 0] = t0 & 0xFF;

		/*
		 * Most significant bits 8 - 11 of t0 become the least significant
		 * nibble of the second byte. The least significant 4 bits
		 * of t1 become the upper nibble of the second byte.
		 *
		 * The conversion to uchar does not alter the value.
		 */
		r[3 * i + 1] = (uchar)((t0 >> 8) | ((t1 << 4) & 0xF0));

		/* Bits 4 - 11 of t1 become the third byte. The conversion to uchar
		 * does not alter the value because t1 is 12-bit wide. */
		r[3 * i + 2] = (uchar)(t1 >> 4);
	}
}

/* Reference: `poly_frombytes()` in the reference implementation @[REF]. */
static void
mlk_poly_frombytes(mlk_poly *r, const uchar a[MLKEM_POLYBYTES])
{
	unsigned i;
	for(i = 0; i < MLKEM_N / 2; i++){
		const uchar t0 = a[3 * i + 0];
		const uchar t1 = a[3 * i + 1];
		const uchar t2 = a[3 * i + 2];
		r->coeffs[2 * i + 0] = (short)(t0 | ((t1 << 8) & 0xFFF));
		r->coeffs[2 * i + 1] = (short)((t1 >> 4) | (t2 << 4));
	}

	/* Note that the coefficients are not canonical */
}

/* Reference: `poly_frommsg()` in the reference implementation @[REF].
 * - We use a value barrier around the bit-selection mask to
 *	reduce the risk of compiler-introduced branches.
 *	The reference implementation contains the expression
 *	`(msg[i] >> j) & 1` which the compiler can reason must
 *	be either 0 or 1. */
static void
mlk_poly_frommsg(mlk_poly *r, const uchar *msg)
{
	unsigned i;

	for(i = 0; i < MLKEM_N / 8; i++){
		unsigned j;
		for(j = 0; j < 8; j++){
			/* mlk_ct_sel_int(MLKEM_Q_HALF, 0, b) is `Decompress_1(b != 0)`
			 * as per @[FIPS203, Eq (4.8)]. */
			/* Assumes the compiler does not change this to a bit selection */
			uchar mask = 1u << j;
			r->coeffs[8 * i + j] = tslsel(MLKEM_Q_HALF, 0, msg[i] & mask);
		}
	}
}

/* Reference: `poly_tomsg()` in the reference implementation @[REF].
 * - In contrast to the reference implementation, we assume
 *	unsigned canonical coefficients here.
 *	The reference implementation works with coefficients
 *	in the range [-(MLKEM_Q-1), MLKEM_Q-1].
 */
static void
mlk_poly_tomsg(uchar *msg, const mlk_poly *a)
{
	unsigned i;

	for(i = 0; i < MLKEM_N / 8; i++){
		unsigned j;
		msg[i] = 0;
		for(j = 0; j < 8; j++){
			ulong t = mlk_scalar_compress_d1(a->coeffs[8 * i + j]);
			msg[i] |= (uchar)(t << j);
		}
	}
}

/* Reference: `polyvec_tobytes()` in the reference implementation @[REF].
 * - In contrast to the reference implementation, we assume
 *	unsigned canonical coefficients here.
 *	The reference implementation works with coefficients
 *	in the range [-(MLKEM_Q-1), MLKEM_Q-1]. */
static void
mlk_polyvec_tobytes(int level, uchar *r, const mlk_polyvec *a)
{
	unsigned i;

	for(i = 0; i < level; i++)
		mlk_poly_tobytes(&r[i * MLKEM_POLYBYTES], &a->vec[i]);
}

/* Reference: `polyvec_frombytes()` in the reference implementation @[REF]. */
static void
mlk_polyvec_frombytes(int level, mlk_polyvec *r, const uchar *a)
{
	unsigned i;
	for(i = 0; i < level; i++)
		mlk_poly_frombytes(&r->vec[i], a + i * MLKEM_POLYBYTES);

}

/* Reference: `polyvec_ntt()` in the reference implementation @[REF]. */
static void
mlk_polyvec_ntt(int level, mlk_polyvec *r)
{
	unsigned i;
	for(i = 0; i < level; i++)
		mlk_poly_ntt(&r->vec[i]);

}

/* Reference: `polyvec_invntt_tomont()` in the reference implementation @[REF].
 * - We normalize at the beginning of the inverse NTT,
 *	while the reference implementation normalizes at
 *	the end. This allows us to drop a call to `poly_reduce()`
 *	from the base multiplication. */
static void
mlk_polyvec_invntt_tomont(int level, mlk_polyvec *r)
{
	unsigned i;
	for(i = 0; i < level; i++)
		mlk_poly_invntt_tomont(&r->vec[i]);

}

/* Reference: `polyvec_tomont()` in the reference implementation @[REF]. */
static void
mlk_polyvec_tomont(int level, mlk_polyvec *r)
{
	unsigned i;
	for(i = 0; i < level; i++)
		mlk_poly_tomont(&r->vec[i]);

}

/* Reference: Does not exist in the reference implementation @[REF].
 * - The reference implementation does not use a
 *	multiplication cache ('mulcache'). This idea originates
 *	from @[NeonNTT] and is used at the C level here. */
static void
mlk_polyvec_mulcache_compute(int level, mlk_polyvec_mulcache *x, const mlk_polyvec *a)
{
	unsigned i;
	for(i = 0; i < level; i++)
		mlk_poly_mulcache_compute(&x->vec[i], &a->vec[i]);
}

/* Reference: `polyvec_reduce()` in the reference implementation @[REF].
 * - We use _unsigned_ canonical outputs, while the reference
 *	implementation uses _signed_ canonical outputs.
 *	Accordingly, we need a conditional addition of MLKEM_Q
 *	here to go from signed to unsigned representatives.
 *	This conditional addition is then dropped from all
 *	polynomial compression functions instead (see `compress.c`). */
static void
mlk_polyvec_reduce(int level, mlk_polyvec *r)
{
	unsigned i;
	for(i = 0; i < level; i++)
		mlk_poly_reduce(&r->vec[i]);

}

/* Reference: `polyvec_add()` in the reference implementation @[REF].
 * - We use destructive version (output=first input) to avoid
 *	reasoning about aliasing in the CBMC specification */
static void
mlk_polyvec_add(int level, mlk_polyvec *r, const mlk_polyvec *b)
{
	unsigned i;
	for(i = 0; i < level; i++)
		mlk_poly_add(&r->vec[i], &b->vec[i]);
}

/* Reference: `polyvec_basemul_acc_montgomery()` in the reference implementation @[REF].
 * - We use a multiplication cache ('mulcache') here
 *	which is not present in the reference implementation @[REF].
 *	This idea originates from @[NeonNTT] and is used
 *	at the C level here.
 * - We compute the coefficients of the scalar product in 32-bit
 *	coefficients and perform only a single modular reduction
 *	at the end. The reference implementation uses 2 * MLKEM_K
 *	more modular reductions since it reduces after every modular
 *	multiplication. */
static void
mlk_polyvec_basemul_acc_montgomery_cached(int level, mlk_poly *r, const mlk_polyvec *a, const mlk_polyvec *b, const mlk_polyvec_mulcache *b_cache)
{
	unsigned i;

	for(i = 0; i < MLKEM_N / 2; i++){
		unsigned k;
		long t[2] = {0};
		for(k = 0; k < level; k++){
			t[0] += (long)a->vec[k].coeffs[2 * i + 1] * b_cache->vec[k].coeffs[i];
			t[0] += (long)a->vec[k].coeffs[2 * i] * b->vec[k].coeffs[2 * i];
			t[1] += (long)a->vec[k].coeffs[2 * i] * b->vec[k].coeffs[2 * i + 1];
			t[1] += (long)a->vec[k].coeffs[2 * i + 1] * b->vec[k].coeffs[2 * i];
		}
		r->coeffs[2 * i + 0] = mlk_montgomery_reduce(t[0]);
		r->coeffs[2 * i + 1] = mlk_montgomery_reduce(t[1]);
	}
}

/**
 * Serialize the ciphertext as the concatenation of the compressed and
 * serialized vector of polynomials b and the compressed and serialized
 * polynomial v.
 */
static void
mlk_pack_ciphertext(int level, uchar *r, const mlk_polyvec *b, mlk_poly *v)
{
	uint i;

	switch(level){
	case K512:
		for(i = 0; i < level; i++)
			mlk_poly_compress_d10(r + i * MLKEM_POLYCOMPRESSEDBYTES_D10, &b->vec[i]);
		mlk_poly_compress_d4(r + level*MLKEM_POLYCOMPRESSEDBYTES_D10, v);
		break;
	case K768:
		for(i = 0; i < level; i++)
			mlk_poly_compress_d10(r + i * MLKEM_POLYCOMPRESSEDBYTES_D10, &b->vec[i]);
		mlk_poly_compress_d4(r + level*MLKEM_POLYCOMPRESSEDBYTES_D10, v);
		break;
	case K1024:
		for(i = 0; i < level; i++)
			mlk_poly_compress_d11(r + i * MLKEM_POLYCOMPRESSEDBYTES_D11, &b->vec[i]);
		mlk_poly_compress_d5(r + level*MLKEM_POLYCOMPRESSEDBYTES_D11, v);
		break;
	}
}

/**
 * De-serialize and decompress ciphertext from a byte array; approximate
 * inverse of mlk_pack_ciphertext.
 */
static void
mlk_unpack_ciphertext(int level, mlk_polyvec *b, mlk_poly *v, const uchar *c)
{
	uint i;

	switch(level){
	case K512:
		for(i = 0; i < level; i++)
			mlk_poly_decompress_d10(&b->vec[i], c + i * MLKEM_POLYCOMPRESSEDBYTES_D10);
		mlk_poly_decompress_d4(v, c + level*MLKEM_POLYCOMPRESSEDBYTES_D10);
		break;
	case K768:
		for(i = 0; i < level; i++)
			mlk_poly_decompress_d10(&b->vec[i], c + i * MLKEM_POLYCOMPRESSEDBYTES_D10);
		mlk_poly_decompress_d4(v, c + level*MLKEM_POLYCOMPRESSEDBYTES_D10);
		break;
	case K1024:
		for(i = 0; i < level; i++)
			mlk_poly_decompress_d11(&b->vec[i], c + i * MLKEM_POLYCOMPRESSEDBYTES_D11);
		mlk_poly_decompress_d5(v, c + level*MLKEM_POLYCOMPRESSEDBYTES_D11);
		break;
	}
}

/* Reference: `gen_matrix()` in the reference implementation @[REF]. */
static void
mlk_gen_matrix(int level, mlk_polymat *a, const uchar seed[MLKEM_SYMBYTES], int transposed)
{
	unsigned i;
	uchar seed_ext[MLKEM_SYMBYTES + 2];

	memcpy(seed_ext, seed, MLKEM_SYMBYTES);
	i = 0;
	for(; i < level * level; i++){
		uchar x, y;
		x = (i / level);
		y = (i % level);

		if(transposed){
			seed_ext[MLKEM_SYMBYTES + 0] = x;
			seed_ext[MLKEM_SYMBYTES + 1] = y;
		} else {
			seed_ext[MLKEM_SYMBYTES + 0] = y;
			seed_ext[MLKEM_SYMBYTES + 1] = x;
		}

		mlk_poly_rej_uniform(&a->vec[i / level].vec[i % level], seed_ext);
	}

	memset(seed_ext, 0, sizeof(seed_ext));
}

/**
 * Compute matrix-vector product in NTT domain, via Montgomery multiplication.
 */
static void
mlk_matvec_mul(int level, mlk_polyvec *out, const mlk_polymat *a, const mlk_polyvec *v, const mlk_polyvec_mulcache *vc)
{
	unsigned i;
	for(i = 0; i < level; i++)
		mlk_polyvec_basemul_acc_montgomery_cached(level, &out->vec[i], &a->vec[i], v, vc);
}

/**
 * Compute and fill the pv and e polyvec structures needed by
 * mlk_keypair_derand().
 */
static void
mlk_keypair_getnoise_eta1(int level, mlk_polyvec *pv, mlk_polyvec *e, const uchar seed[MLKEM_SYMBYTES])
{
	unsigned i;
	uchar buf[3 * MLKEM_N / 4];
	uchar extkey[MLKEM_SYMBYTES + 1];

	memcpy(extkey, seed, MLKEM_SYMBYTES);
	switch(level){
	case K512:
		for(i = 0; i < 2; i++){
			extkey[MLKEM_SYMBYTES] = i;
			mlk_prf_eta(3, buf, extkey);
			mlk_poly_cbd3(&pv->vec[i], buf);
		}
		for(i = 0; i < 2; i++){
			extkey[MLKEM_SYMBYTES] = 2 + i;
			mlk_prf_eta(3, buf, extkey);
			mlk_poly_cbd3(&e->vec[i], buf);
		}
		break;
	case K768:
		for(i = 0; i < 3; i++){
			extkey[MLKEM_SYMBYTES] = i;
			mlk_prf_eta(2, buf, extkey);
			mlk_poly_cbd2(&pv->vec[i], buf);
		}
		for(i = 0; i < 3; i++){
			extkey[MLKEM_SYMBYTES] = 3 + i;
			mlk_prf_eta(2, buf, extkey);
			mlk_poly_cbd2(&e->vec[i], buf);
		}
		break;
	case K1024:
		for(i = 0; i < 4; i++){
			extkey[MLKEM_SYMBYTES] = i;
			mlk_prf_eta(2, buf, extkey);
			mlk_poly_cbd2(&pv->vec[i], buf);
		}
		for(i = 0; i < 4; i++){
			extkey[MLKEM_SYMBYTES] = 4 + i;
			mlk_prf_eta(2, buf, extkey);
			mlk_poly_cbd2(&e->vec[i], buf);
		}
		break;
	default:
		abort();
	}
}

/**
 * Compute and fill the sp, ep, and epp polynomial structures needed by
 * mlk_indcpa_enc().
 */
static void
mlk_enc_getnoise_eta1_eta2(int level, mlk_polyvec *sp, mlk_polyvec *ep, mlk_poly *epp, const uchar coins[MLKEM_SYMBYTES])
{
	unsigned i;
	uchar buf[3 * MLKEM_N / 4];
	uchar extkey[MLKEM_SYMBYTES + 1];

	memcpy(extkey, coins, MLKEM_SYMBYTES);
	switch(level){
	case K512:
		for(i = 0; i < 2; i++){
			extkey[MLKEM_SYMBYTES] = i;
			mlk_prf_eta(3, buf, extkey);
			mlk_poly_cbd3(&sp->vec[i], buf);
		}
		for(i = 0; i < 2; i++){
			extkey[MLKEM_SYMBYTES] = 2 + i;
			mlk_prf_eta(2, buf, extkey);
			mlk_poly_cbd2(&ep->vec[i], buf);
		}
		break;
	case K768:
		for(i = 0; i < 3; i++){
			extkey[MLKEM_SYMBYTES] = i;
			mlk_prf_eta(2, buf, extkey);
			mlk_poly_cbd2(&sp->vec[i], buf);
		}
		for(i = 0; i < 3; i++){
			extkey[MLKEM_SYMBYTES] = 3 + i;
			mlk_prf_eta(2, buf, extkey);
			mlk_poly_cbd2(&ep->vec[i], buf);
		}
		break;
	case K1024:
		for(i = 0; i < 4; i++){
			extkey[MLKEM_SYMBYTES] = i;
			mlk_prf_eta(2, buf, extkey);
			mlk_poly_cbd2(&sp->vec[i], buf);
		}
		for(i = 0; i < 4; i++){
			extkey[MLKEM_SYMBYTES] = 4 + i;
			mlk_prf_eta(2, buf, extkey);
			mlk_poly_cbd2(&ep->vec[i], buf);
		}
		break;
	default:
		abort();
	}
	extkey[MLKEM_SYMBYTES]++;
	mlk_prf_eta(2, buf, extkey);
	mlk_poly_cbd2(epp, buf);
}

typedef struct MLKEMstate MLKEMstate;
struct MLKEMstate {
	mlk_polyvec_mulcache cache;
	mlk_polyvec pkpv, skpv;
	mlk_polyvec tmp[3];
	mlk_polymat a;
};


/* Reference: `indcpa_keypair_derand()` in the reference implementation @[REF].
 * - We use a mulcache to speed up matrix-vector multiplication.
 * - We include buffer zeroization.
 */
static void
mlk_indcpa_keypair_derand(int level, MLKEMstate *state, uchar *pk, uchar *sk, const uchar coins[MLKEM_SYMBYTES])
{
	const uchar *publicseed;
	const uchar *noiseseed;
	uchar buf[2 * MLKEM_SYMBYTES];
	uchar coins_with_domain_separator[MLKEM_SYMBYTES + 1];
	mlk_polyvec *e;

	publicseed = buf;
	noiseseed = buf + MLKEM_SYMBYTES;
	e = &state->tmp[0];

	/* Concatenate coins with MLKEM_K for domain separation of security levels */
	memcpy(coins_with_domain_separator, coins, MLKEM_SYMBYTES);
	coins_with_domain_separator[MLKEM_SYMBYTES] = level;

	mlk_hash_g(buf, coins_with_domain_separator, MLKEM_SYMBYTES + 1);

	mlk_gen_matrix(level, &state->a, publicseed, 0 /* no transpose */);

	mlk_keypair_getnoise_eta1(level, &state->skpv, e, noiseseed);

	mlk_polyvec_ntt(level, &state->skpv);
	mlk_polyvec_ntt(level, e);

	mlk_polyvec_mulcache_compute(level, &state->cache, &state->skpv);
	mlk_matvec_mul(level, &state->pkpv, &state->a, &state->skpv, &state->cache);
	mlk_polyvec_tomont(level, &state->pkpv);

	mlk_polyvec_add(level, &state->pkpv, e);
	mlk_polyvec_reduce(level, &state->pkpv);
	mlk_polyvec_reduce(level, &state->skpv);

	mlk_polyvec_tobytes(level, sk, &state->skpv);
	mlk_polyvec_tobytes(level, pk, &state->pkpv);
	memcpy(pk + MLKEM_POLYVECBYTES(level), publicseed, MLKEM_SYMBYTES);

	memset(coins_with_domain_separator, 0, sizeof coins_with_domain_separator);
	memset(buf, 0, sizeof buf);
}

/* Reference: `indcpa_enc()` in the reference implementation @[REF].
 *	- We use a mulcache to speed up matrix-vector multiplication.
 *	- We include buffer zeroization.
 */
static void
mlk_indcpa_enc(int level, MLKEMstate *state, uchar *c, const uchar *m, const uchar *pk, const uchar coins[MLKEM_SYMBYTES])
{
	uchar seed[MLKEM_SYMBYTES];
	mlk_polyvec *sp, *ep, *b;
	mlk_poly v;
	mlk_poly k;
	mlk_poly epp;

	sp = &state->tmp[0];
	ep = &state->tmp[1];
	b = &state->tmp[2];

	mlk_polyvec_frombytes(level, &state->pkpv, pk);
	memcpy(seed, pk + MLKEM_POLYVECBYTES(level), MLKEM_SYMBYTES);
	mlk_poly_frommsg(&k, m);

	mlk_gen_matrix(level, &state->a, seed, 1 /* transpose */);
	memset(seed, 0, sizeof seed);

	mlk_enc_getnoise_eta1_eta2(level, sp, ep, &epp, coins);

	mlk_polyvec_ntt(level, sp);

	mlk_polyvec_mulcache_compute(level, &state->cache, sp);
	mlk_matvec_mul(level, b, &state->a, sp, &state->cache);
	mlk_polyvec_basemul_acc_montgomery_cached(level, &v, &state->pkpv, sp, &state->cache);

	mlk_polyvec_invntt_tomont(level, b);
	mlk_poly_invntt_tomont(&v);

	mlk_polyvec_add(level, b, ep);
	mlk_poly_add(&v, &epp);
	mlk_poly_add(&v, &k);

	mlk_polyvec_reduce(level, b);
	mlk_poly_reduce(&v);

	mlk_pack_ciphertext(level, c, b, &v);
}

static void
mlk_indcpa_dec(int level, MLKEMstate *state, uchar *m, const uchar *c, const uchar *sk)
{
	mlk_poly v, sb;
	mlk_polyvec *b;

	b = &state->tmp[0];

	mlk_unpack_ciphertext(level, b, &v, c);
	mlk_polyvec_frombytes(level, &state->skpv, sk);

	mlk_polyvec_ntt(level, b);
	mlk_polyvec_mulcache_compute(level, &state->cache, b);
	mlk_polyvec_basemul_acc_montgomery_cached(level, &sb, &state->skpv, b, &state->cache);
	mlk_poly_invntt_tomont(&sb);

	mlk_poly_sub(&v, &sb);
	mlk_poly_reduce(&v);

	mlk_poly_tomsg(m, &v);
}

static int
mlk_kem_check_pk(int level, const uchar *pk)
{
	mlk_polyvec p;
	uchar p_reencoded[MLKEM_POLYVECBYTES(K1024)];

	mlk_polyvec_frombytes(level, &p, pk);
	mlk_polyvec_reduce(level, &p);
	mlk_polyvec_tobytes(level, p_reencoded, &p);
	return tsmemcmp(pk, p_reencoded, MLKEM_POLYVECBYTES(level)) ? -1 : 0;
}

static int
mlk_kem_check_sk(int level, const uchar *sk)
{
	uchar test[MLKEM_SYMBYTES];

	/*
	 * The parts of `sk` being hashed and compared here are public, so
	 * no private information is leaked through the runtime or the return value
	 * of this function.
	 */

	mlk_hash_h(test, sk + MLKEM_POLYVECBYTES(level), MLKEM_PUBLICKEYBYTES(level));
	return memcmp(sk + MLKEM_SECRETKEYBYTES(level) - 2 * MLKEM_SYMBYTES, test, MLKEM_SYMBYTES) ? -1 : 0;
}

/* the following _x functions are exposed in their 'derand' variants for testing */

int
mlk_kem_keypair_x(int level, uchar *pk, uchar *sk, uchar *coins)
{
	int ret;
	MLKEMstate *state;

	state = malloc(sizeof *state);
	if(state == nil)
		return -1;
	mlk_indcpa_keypair_derand(level, state, pk, sk, coins);
	memset(state, 0, sizeof *state);
	free(state);

	memcpy(sk + MLKEM_POLYVECBYTES(level), pk, MLKEM_PUBLICKEYBYTES(level));
	mlk_hash_h(sk + MLKEM_SECRETKEYBYTES(level) - 2 * MLKEM_SYMBYTES, pk, MLKEM_PUBLICKEYBYTES(level));
	/* Value z for pseudo-random output on reject */
	memcpy(sk + MLKEM_SECRETKEYBYTES(level) - MLKEM_SYMBYTES, coins + MLKEM_SYMBYTES, MLKEM_SYMBYTES);
	return 0;
}

int
mlk_kem_enc_x(int level, uchar *ct, uchar *ss, const uchar *pk, uchar *coins)
{
	int ret;
	uchar buf[2 * MLKEM_SYMBYTES];
	uchar kr[2 * MLKEM_SYMBYTES];
	MLKEMstate *state;

	state = malloc(sizeof *state);
	if(state == nil)
		return -1;

	/* Specification: Implements @[FIPS203, Section 7.2, Modulus check] */
	ret = mlk_kem_check_pk(level, pk);
	if(ret != 0)
		goto cleanup;

	memcpy(buf, coins, MLKEM_SYMBYTES);

	/* Multitarget countermeasure for coins + contributory KEM */
	mlk_hash_h(buf + MLKEM_SYMBYTES, pk, MLKEM_PUBLICKEYBYTES(level));
	mlk_hash_g(kr, buf, 2 * MLKEM_SYMBYTES);

	/* coins are in kr+MLKEM_SYMBYTES */
	mlk_indcpa_enc(level, state, ct, buf, pk, kr + MLKEM_SYMBYTES);
	memcpy(ss, kr, MLKEM_SYMBYTES);

cleanup:
	memset(kr, 0, sizeof buf);
	memset(buf, 0, sizeof buf);
	memset(state, 0, sizeof *state);
	free(state);
	return ret;
}

int
mlk_kem_dec_x(int level, uchar *ss, const uchar *ct, const uchar *sk)
{
	int ret;
	uchar fail;
	const uchar *pk = sk + MLKEM_POLYVECBYTES(level);
	uchar buf[2 * MLKEM_SYMBYTES];
	uchar kr[2 * MLKEM_SYMBYTES];
	uchar tmp[MLKEM_SYMBYTES + MLKEM1024_IND_BYTES];
	MLKEMstate *state;

	state = malloc(sizeof *state);
	if(state == nil)
		return -1;

	/* Specification: Implements @[FIPS203, Section 7.3, Hash check] */
	ret = mlk_kem_check_sk(level, sk);
	if(ret != 0)
		goto cleanup;

	mlk_indcpa_dec(level, state, buf, ct, sk);

	/* Multitarget countermeasure for coins + contributory KEM */
	memcpy(buf + MLKEM_SYMBYTES, sk + MLKEM_SECRETKEYBYTES(level) - 2 * MLKEM_SYMBYTES, MLKEM_SYMBYTES);
	mlk_hash_g(kr, buf, 2 * MLKEM_SYMBYTES);

	/* Recompute and compare ciphertext */
	/* coins are in kr+MLKEM_SYMBYTES */
	mlk_indcpa_enc(level, state, tmp, buf, pk, kr + MLKEM_SYMBYTES);

	switch(level){
	case K512:
		fail = tsmemcmp(ct, tmp, MLKEM512_IND_BYTES);
		/* Compute rejection key */
		memcpy(tmp, sk + MLKEM_SECRETKEYBYTES(K512) - MLKEM_SYMBYTES, MLKEM_SYMBYTES);
		memcpy(tmp + MLKEM_SYMBYTES, ct, MLKEM512_IND_BYTES);
		mlk_hash_j(ss, tmp, MLKEM_SYMBYTES + MLKEM512_IND_BYTES);
		break;
	case K768:
		fail = tsmemcmp(ct, tmp, MLKEM768_IND_BYTES);
		/* Compute rejection key */
		memcpy(tmp, sk + MLKEM_SECRETKEYBYTES(K768) - MLKEM_SYMBYTES, MLKEM_SYMBYTES);
		memcpy(tmp + MLKEM_SYMBYTES, ct, MLKEM768_IND_BYTES);
		mlk_hash_j(ss, tmp, MLKEM_SYMBYTES + MLKEM768_IND_BYTES);
		break;
	case K1024:
		fail = tsmemcmp(ct, tmp, MLKEM1024_IND_BYTES);
		/* Compute rejection key */
		memcpy(tmp, sk + MLKEM_SECRETKEYBYTES(K1024) - MLKEM_SYMBYTES, MLKEM_SYMBYTES);
		memcpy(tmp + MLKEM_SYMBYTES, ct, MLKEM1024_IND_BYTES);
		mlk_hash_j(ss, tmp, MLKEM_SYMBYTES + MLKEM1024_IND_BYTES);
		break;
	default:
		abort();
	}

	/* constant time conditional memcpy using fail as the conditional */
	tsmemsel(ss, kr, ss, MLKEM_SYMBYTES, fail);

cleanup:
	memset(tmp, 0, sizeof tmp);
	memset(kr, 0, sizeof kr);
	memset(buf, 0, sizeof buf);
	memset(state, 0, sizeof state);
	free(state);

	return ret;
}

int
mlkem512_keypair(uchar *pk, uchar *sk)
{
	uchar coins[2 * MLKEM_SYMBYTES];
	int r;

	genrandom(coins, sizeof coins);
	r = mlk_kem_keypair_x(K512, pk, sk, coins);
	memset(coins, 0, sizeof coins);
	return r;
}

int
mlkem768_keypair(uchar *pk, uchar *sk)
{
	uchar coins[2 * MLKEM_SYMBYTES];
	int r;

	genrandom(coins, sizeof coins);
	r = mlk_kem_keypair_x(K768, pk, sk, coins);
	memset(coins, 0, sizeof coins);
	return r;
}

int
mlkem1024_keypair(uchar *pk, uchar *sk)
{
	uchar coins[2 * MLKEM_SYMBYTES];
	int r;

	genrandom(coins, sizeof coins);
	r = mlk_kem_keypair_x(K1024, pk, sk, coins);
	memset(coins, 0, sizeof coins);
	return r;
}

int
mlkem512_enc(uchar *ct, uchar *ss, const uchar *pk)
{
	uchar coins[MLKEM_SYMBYTES];
	int r;

	genrandom(coins, sizeof coins);
	r = mlk_kem_enc_x(K512, ct, ss, pk, coins);
	memset(coins, 0, sizeof coins);
	return r;
}

int
mlkem768_enc(uchar *ct, uchar *ss, const uchar *pk)
{
	uchar coins[MLKEM_SYMBYTES];
	int r;

	genrandom(coins, sizeof coins);
	r = mlk_kem_enc_x(K768, ct, ss, pk, coins);
	memset(coins, 0, sizeof coins);
	return r;
}

int
mlkem1024_enc(uchar *ct, uchar *ss, const uchar *pk)
{
	uchar coins[MLKEM_SYMBYTES];
	int r;

	genrandom(coins, sizeof coins);
	r = mlk_kem_enc_x(K1024, ct, ss, pk, coins);
	memset(coins, 0, sizeof coins);
	return r;
}

int
mlkem512_dec(uchar *ss, const uchar *ct, const uchar *sk)
{
	return mlk_kem_dec_x(K512, ss, ct, sk);
}

int
mlkem768_dec(uchar *ss, const uchar *ct, const uchar *sk)
{
	return mlk_kem_dec_x(K768, ss, ct, sk);
}

int
mlkem1024_dec(uchar *ss, const uchar *ct, const uchar *sk)
{
	return mlk_kem_dec_x(K1024, ss, ct, sk);
}
