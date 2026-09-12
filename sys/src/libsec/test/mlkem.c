#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>

/* expose some internals for testing */
enum{
	K512 = 2,
	K768,
	K1024,
};

int mlk_kem_keypair_x(int level, u8int *pk, u8int *sk, u8int *coins);
int mlk_kem_enc_x(int level, u8int *ct, u8int *ss, const u8int *pk, u8int *coins);

#include "mlkemvectors"

static void
example_mlkem512_keygen(void)
{
	uchar pk[MLKEM512_publicbytes];
	uchar sk[MLKEM512_secretbytes];
	uchar coins[2 * MLKEM_bytes];

	memcpy(coins, test_vector_d, MLKEM_bytes);
	memcpy(coins + MLKEM_bytes, test_vector_z, MLKEM_bytes);
	assert(mlk_kem_keypair_x(K512, pk, sk, coins) == 0);
	assert(memcmp(pk, test_vector_pk_512, MLKEM512_publicbytes) == 0);
	assert(memcmp(sk, test_vector_sk_512, MLKEM512_secretbytes) == 0);
}

static void
example_mlkem768_keygen(void)
{
	uchar pk[MLKEM768_publicbytes];
	uchar sk[MLKEM768_secretbytes];
	uchar coins[2 * MLKEM_bytes];

	memcpy(coins, test_vector_d, MLKEM_bytes);
	memcpy(coins + MLKEM_bytes, test_vector_z, MLKEM_bytes);
	assert(mlk_kem_keypair_x(K768, pk, sk, coins) == 0);
	assert(memcmp(pk, test_vector_pk_768, MLKEM768_publicbytes) == 0);
	assert(memcmp(sk, test_vector_sk_768, MLKEM768_secretbytes) == 0);
}

static void
example_mlkem1024_keygen(void)
{
	uchar pk[MLKEM1024_publicbytes];
	uchar sk[MLKEM1024_secretbytes];
	uchar coins[2 * MLKEM_bytes];

	memcpy(coins, test_vector_d, MLKEM_bytes);
	memcpy(coins + MLKEM_bytes, test_vector_z, MLKEM_bytes);
	assert(mlk_kem_keypair_x(K1024, pk, sk, coins) == 0);
	assert(memcmp(pk, test_vector_pk_1024, MLKEM1024_publicbytes) == 0);
	assert(memcmp(sk, test_vector_sk_1024, MLKEM1024_secretbytes) == 0);
}

static void
example_mlkem512_encaps(void)
{
	uchar ct[MLKEM512_cipherbytes];
	uchar ss[MLKEM_bytes];

	assert(mlk_kem_enc_x(K512, ct, ss, test_vector_pk_512, test_vector_m) == 0);
	assert(memcmp(ct, test_vector_ct_512, MLKEM512_cipherbytes) == 0);
	assert(memcmp(ss, test_vector_ss_512, MLKEM_bytes) == 0);
}

static void
example_mlkem768_encaps(void)
{
	uchar ct[MLKEM768_cipherbytes];
	uchar ss[MLKEM_bytes];

	assert(mlk_kem_enc_x(K768, ct, ss, test_vector_pk_768, test_vector_m) == 0);
	assert(memcmp(ct, test_vector_ct_768, MLKEM768_cipherbytes) == 0);
	assert(memcmp(ss, test_vector_ss_768, MLKEM_bytes) == 0);
}

static void
example_mlkem1024_encaps(void)
{
	uchar ct[MLKEM1024_cipherbytes];
	uchar ss[MLKEM_bytes];

	assert(mlk_kem_enc_x(K1024, ct, ss, test_vector_pk_1024, test_vector_m) == 0);
	assert(memcmp(ct, test_vector_ct_1024, MLKEM1024_cipherbytes) == 0);
	assert(memcmp(ss, test_vector_ss_1024, MLKEM_bytes) == 0);
}

static void
example_mlkem512_decaps(void)
{
	uchar ss[MLKEM_bytes];

	assert(mlkem512_dec(ss, test_vector_ct_512, test_vector_sk_512) == 0);
	assert(memcmp(ss, test_vector_ss_512, MLKEM_bytes) == 0);
}

static void
example_mlkem768_decaps(void)
{
	uchar ss[MLKEM_bytes];

	assert(mlkem768_dec(ss, test_vector_ct_768, test_vector_sk_768) == 0);
	assert(memcmp(ss, test_vector_ss_768, MLKEM_bytes) == 0);
}

static void
example_mlkem1024_decaps(void)
{
	uchar ss[MLKEM_bytes];

	assert(mlkem1024_dec(ss, test_vector_ct_1024, test_vector_sk_1024) == 0);
	assert(memcmp(ss, test_vector_ss_1024, MLKEM_bytes) == 0);
}

void
main(void)
{
	example_mlkem512_keygen();
	example_mlkem512_encaps();
	example_mlkem512_decaps();

	example_mlkem768_keygen();
	example_mlkem768_encaps();
	example_mlkem768_decaps();

	example_mlkem1024_keygen();
	example_mlkem1024_encaps();
	example_mlkem1024_decaps();
	exits(nil);
}
