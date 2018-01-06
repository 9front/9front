#pragma	lib	"libsec.a"
#pragma	src	"/sys/src/libsec"


#ifndef _MPINT
typedef struct mpint mpint;
#endif

/*
 * AES definitions
 */

enum
{
	AESbsize=	16,
	AESmaxkey=	32,
	AESmaxrounds=	14
};

typedef struct AESstate AESstate;
struct AESstate
{
	ulong	setup;
	ulong	offset;
	int	rounds;
	int	keybytes;
	void	*ekey;				/* expanded encryption round key */
	void	*dkey;				/* expanded decryption round key */
	uchar	key[AESmaxkey];			/* unexpanded key */
	uchar	ivec[AESbsize];			/* initialization vector */
	uchar	storage[512];			/* storage for expanded keys */
};

/* block ciphers */
extern void (*aes_encrypt)(ulong rk[], int Nr, uchar pt[16], uchar ct[16]);
extern void (*aes_decrypt)(ulong rk[], int Nr, uchar ct[16], uchar pt[16]);

void	setupAESstate(AESstate *s, uchar key[], int nkey, uchar *ivec);

void	aesCBCencrypt(uchar *p, int len, AESstate *s);
void	aesCBCdecrypt(uchar *p, int len, AESstate *s);
void	aesCFBencrypt(uchar *p, int len, AESstate *s);
void	aesCFBdecrypt(uchar *p, int len, AESstate *s);
void	aesOFBencrypt(uchar *p, int len, AESstate *s);

typedef struct AESGCMstate AESGCMstate;
struct AESGCMstate
{
	AESstate;

	ulong	H[4];
	ulong	M[16][256][4];
};

void	setupAESGCMstate(AESGCMstate *s, uchar *key, int keylen, uchar *iv, int ivlen);
void	aesgcm_setiv(AESGCMstate *s, uchar *iv, int ivlen);
void	aesgcm_encrypt(uchar *dat, ulong ndat, uchar *aad, ulong naad, uchar tag[16], AESGCMstate *s);
int	aesgcm_decrypt(uchar *dat, ulong ndat, uchar *aad, ulong naad, uchar tag[16], AESGCMstate *s);

/*
 * Blowfish Definitions
 */

enum
{
	BFbsize	= 8,
	BFrounds= 16
};

/* 16-round Blowfish */
typedef struct BFstate BFstate;
struct BFstate
{
	ulong	setup;

	uchar	key[56];
	uchar	ivec[8];

	u32int 	pbox[BFrounds+2];
	u32int	sbox[1024];
};

void	setupBFstate(BFstate *s, uchar key[], int keybytes, uchar *ivec);
void	bfCBCencrypt(uchar*, int, BFstate*);
void	bfCBCdecrypt(uchar*, int, BFstate*);
void	bfECBencrypt(uchar*, int, BFstate*);
void	bfECBdecrypt(uchar*, int, BFstate*);

/*
 * Chacha definitions
 */

enum
{
	ChachaBsize=	64,
	ChachaKeylen=	256/8,
	ChachaIVlen=	96/8,
	XChachaIVlen=	192/8,
};

typedef struct Chachastate Chachastate;
struct Chachastate
{
	union{
		u32int	input[16];
		struct {
			u32int	constant[4];
			u32int	key[8];
			u32int	counter;
			u32int	iv[3];
		};
	};
	u32int	xkey[8];
	int	rounds;
	int	ivwords;
};

void	setupChachastate(Chachastate*, uchar*, ulong, uchar*, ulong, int);
void	chacha_setiv(Chachastate *, uchar*);
void	chacha_setblock(Chachastate*, u64int);
void	chacha_encrypt(uchar*, ulong, Chachastate*);
void	chacha_encrypt2(uchar*, uchar*, ulong, Chachastate*);

void	hchacha(uchar h[32], uchar *key, ulong keylen, uchar nonce[16], int rounds);

void	ccpoly_encrypt(uchar *dat, ulong ndat, uchar *aad, ulong naad, uchar tag[16], Chachastate *cs);
int	ccpoly_decrypt(uchar *dat, ulong ndat, uchar *aad, ulong naad, uchar tag[16], Chachastate *cs);

/*
 * Salsa definitions
 */
enum
{
	SalsaBsize=	64,
	SalsaKeylen=	256/8,
	SalsaIVlen=	64/8,
	XSalsaIVlen=	192/8,
};

typedef struct Salsastate Salsastate;
struct Salsastate
{
	u32int	input[16];
	u32int	xkey[8];
	int	rounds;
	int	ivwords;
};

void	setupSalsastate(Salsastate*, uchar*, ulong, uchar*, ulong, int);
void	salsa_setiv(Salsastate*, uchar*);
void	salsa_setblock(Salsastate*, u64int);
void	salsa_encrypt(uchar*, ulong, Salsastate*);
void	salsa_encrypt2(uchar*, uchar*, ulong, Salsastate*);

void	salsa_core(u32int in[16], u32int out[16], int rounds);

void	hsalsa(uchar h[32], uchar *key, ulong keylen, uchar nonce[16], int rounds);

/*
 * DES definitions
 */

enum
{
	DESbsize=	8
};

/* single des */
typedef struct DESstate DESstate;
struct DESstate
{
	ulong	setup;
	uchar	key[8];		/* unexpanded key */
	ulong	expanded[32];	/* expanded key */
	uchar	ivec[8];	/* initialization vector */
};

void	setupDESstate(DESstate *s, uchar key[8], uchar *ivec);
void	des_key_setup(uchar[8], ulong[32]);
void	block_cipher(ulong*, uchar*, int);
void	desCBCencrypt(uchar*, int, DESstate*);
void	desCBCdecrypt(uchar*, int, DESstate*);
void	desECBencrypt(uchar*, int, DESstate*);
void	desECBdecrypt(uchar*, int, DESstate*);

/* for backward compatibility with 7-byte DES key format */
void	des56to64(uchar *k56, uchar *k64);
void	des64to56(uchar *k64, uchar *k56);
void	key_setup(uchar[7], ulong[32]);

/* triple des encrypt/decrypt orderings */
enum {
	DES3E=		0,
	DES3D=		1,
	DES3EEE=	0,
	DES3EDE=	2,
	DES3DED=	5,
	DES3DDD=	7
};

typedef struct DES3state DES3state;
struct DES3state
{
	ulong	setup;
	uchar	key[3][8];		/* unexpanded key */
	ulong	expanded[3][32];	/* expanded key */
	uchar	ivec[8];		/* initialization vector */
};

void	setupDES3state(DES3state *s, uchar key[3][8], uchar *ivec);
void	triple_block_cipher(ulong keys[3][32], uchar*, int);
void	des3CBCencrypt(uchar*, int, DES3state*);
void	des3CBCdecrypt(uchar*, int, DES3state*);
void	des3ECBencrypt(uchar*, int, DES3state*);
void	des3ECBdecrypt(uchar*, int, DES3state*);

/*
 * digests
 */

enum
{
	SHA1dlen=	20,	/* SHA digest length */
	SHA2_224dlen=	28,	/* SHA-224 digest length */
	SHA2_256dlen=	32,	/* SHA-256 digest length */
	SHA2_384dlen=	48,	/* SHA-384 digest length */
	SHA2_512dlen=	64,	/* SHA-512 digest length */
	MD4dlen=	16,	/* MD4 digest length */
	MD5dlen=	16,	/* MD5 digest length */
	Poly1305dlen=	16,	/* Poly1305 digest length */

	Hmacblksz	= 64,	/* in bytes; from rfc2104 */
};

typedef struct DigestState DigestState;
struct DigestState
{
	uvlong	len;
	union {
		u32int	state[16];
		u64int	bstate[8];
	};
	uchar	buf[256];
	int	blen;
	char	malloced;
	char	seeded;
};
typedef struct DigestState SHAstate;	/* obsolete name */
typedef struct DigestState SHA1state;
typedef struct DigestState SHA2_224state;
typedef struct DigestState SHA2_256state;
typedef struct DigestState SHA2_384state;
typedef struct DigestState SHA2_512state;
typedef struct DigestState MD5state;
typedef struct DigestState MD4state;

DigestState*	md4(uchar*, ulong, uchar*, DigestState*);
DigestState*	md5(uchar*, ulong, uchar*, DigestState*);
DigestState*	sha1(uchar*, ulong, uchar*, DigestState*);
DigestState*	sha2_224(uchar*, ulong, uchar*, DigestState*);
DigestState*	sha2_256(uchar*, ulong, uchar*, DigestState*);
DigestState*	sha2_384(uchar*, ulong, uchar*, DigestState*);
DigestState*	sha2_512(uchar*, ulong, uchar*, DigestState*);
DigestState*	hmac_x(uchar *p, ulong len, uchar *key, ulong klen,
			uchar *digest, DigestState *s,
			DigestState*(*x)(uchar*, ulong, uchar*, DigestState*),
			int xlen);
DigestState*	hmac_md5(uchar*, ulong, uchar*, ulong, uchar*, DigestState*);
DigestState*	hmac_sha1(uchar*, ulong, uchar*, ulong, uchar*, DigestState*);
DigestState*	hmac_sha2_224(uchar*, ulong, uchar*, ulong, uchar*, DigestState*);
DigestState*	hmac_sha2_256(uchar*, ulong, uchar*, ulong, uchar*, DigestState*);
DigestState*	hmac_sha2_384(uchar*, ulong, uchar*, ulong, uchar*, DigestState*);
DigestState*	hmac_sha2_512(uchar*, ulong, uchar*, ulong, uchar*, DigestState*);
char*		md5pickle(MD5state*);
MD5state*	md5unpickle(char*);
char*		sha1pickle(SHA1state*);
SHA1state*	sha1unpickle(char*);

DigestState*	poly1305(uchar*, ulong, uchar*, ulong, uchar*, DigestState*);

/*
 * random number generation
 */
void	genrandom(uchar *buf, int nbytes);
void	prng(uchar *buf, int nbytes);
ulong	fastrand(void);
ulong	nfastrand(ulong);

/*
 * primes
 */
void	genprime(mpint *p, int n, int accuracy); /* generate n-bit probable prime */
void	gensafeprime(mpint *p, mpint *alpha, int n, int accuracy); /* prime & generator */
void	genstrongprime(mpint *p, int n, int accuracy); /* generate n-bit strong prime */
void	DSAprimes(mpint *q, mpint *p, uchar seed[SHA1dlen]);
int	probably_prime(mpint *n, int nrep);	/* miller-rabin test */
int	smallprimetest(mpint *p);  /* returns -1 if not prime, 0 otherwise */

/*
 * rc4
 */
typedef struct RC4state RC4state;
struct RC4state
{
	 uchar	state[256];
	 uchar	x;
	 uchar	y;
};

void	setupRC4state(RC4state*, uchar*, int);
void	rc4(RC4state*, uchar*, int);
void	rc4skip(RC4state*, int);
void	rc4back(RC4state*, int);

/*
 * rsa
 */
typedef struct RSApub RSApub;
typedef struct RSApriv RSApriv;
typedef struct PEMChain PEMChain;

/* public/encryption key */
struct RSApub
{
	mpint	*n;	/* modulus */
	mpint	*ek;	/* exp (encryption key) */
};

/* private/decryption key */
struct RSApriv
{
	RSApub	pub;

	mpint	*dk;	/* exp (decryption key) */

	/* precomputed values to help with chinese remainder theorem calc */
	mpint	*p;
	mpint	*q;
	mpint	*kp;	/* dk mod p-1 */
	mpint	*kq;	/* dk mod q-1 */
	mpint	*c2;	/* (inv p) mod q */
};

struct PEMChain{
	PEMChain*next;
	uchar	*pem;
	int	pemlen;
};

RSApriv*	rsagen(int nlen, int elen, int rounds);
RSApriv*	rsafill(mpint *n, mpint *e, mpint *d, mpint *p, mpint *q);
mpint*		rsaencrypt(RSApub *k, mpint *in, mpint *out);
mpint*		rsadecrypt(RSApriv *k, mpint *in, mpint *out);
RSApub*		rsapuballoc(void);
void		rsapubfree(RSApub*);
RSApriv*	rsaprivalloc(void);
void		rsaprivfree(RSApriv*);
RSApub*		rsaprivtopub(RSApriv*);
RSApub*		X509toRSApub(uchar*, int, char*, int);
RSApub*		asn1toRSApub(uchar*, int);
RSApriv*	asn1toRSApriv(uchar*, int);
void		asn1dump(uchar *der, int len);
uchar*		decodePEM(char *s, char *type, int *len, char **new_s);
PEMChain*	decodepemchain(char *s, char *type);
uchar*		X509rsagen(RSApriv *priv, char *subj, ulong valid[2], int *certlen);
uchar*		X509rsareq(RSApriv *priv, char *subj, int *certlen);
char*		X509rsaverify(uchar *cert, int ncert, RSApub *pk);
char*		X509rsaverifydigest(uchar *sig, int siglen, uchar *edigest, int edigestlen, RSApub *pk);

void		X509dump(uchar *cert, int ncert);

mpint*		pkcs1padbuf(uchar *buf, int len, mpint *modulus, int blocktype);
int		pkcs1unpadbuf(uchar *buf, int len, mpint *modulus, int blocktype);
int		asn1encodeRSApub(RSApub *pk, uchar *buf, int len);
int		asn1encodedigest(DigestState* (*fun)(uchar*, ulong, uchar*, DigestState*),
			uchar *digest, uchar *buf, int len);

int		X509digestSPKI(uchar *, int, DigestState* (*)(uchar*, ulong, uchar*, DigestState*), uchar *);

/*
 * elgamal
 */
typedef struct EGpub EGpub;
typedef struct EGpriv EGpriv;
typedef struct EGsig EGsig;

/* public/encryption key */
struct EGpub
{
	mpint	*p;	/* modulus */
	mpint	*alpha;	/* generator */
	mpint	*key;	/* (encryption key) alpha**secret mod p */
};

/* private/decryption key */
struct EGpriv
{
	EGpub	pub;
	mpint	*secret;	/* (decryption key) */
};

/* signature */
struct EGsig
{
	mpint	*r, *s;
};

EGpriv*		eggen(int nlen, int rounds);
mpint*		egencrypt(EGpub *k, mpint *in, mpint *out);	/* deprecated */
mpint*		egdecrypt(EGpriv *k, mpint *in, mpint *out);
EGsig*		egsign(EGpriv *k, mpint *m);
int		egverify(EGpub *k, EGsig *sig, mpint *m);
EGpub*		egpuballoc(void);
void		egpubfree(EGpub*);
EGpriv*		egprivalloc(void);
void		egprivfree(EGpriv*);
EGsig*		egsigalloc(void);
void		egsigfree(EGsig*);
EGpub*		egprivtopub(EGpriv*);

/*
 * dsa
 */
typedef struct DSApub DSApub;
typedef struct DSApriv DSApriv;
typedef struct DSAsig DSAsig;

/* public/encryption key */
struct DSApub
{
	mpint	*p;	/* modulus */
	mpint	*q;	/* group order, q divides p-1 */
	mpint	*alpha;	/* group generator */
	mpint	*key;	/* (encryption key) alpha**secret mod p */
};

/* private/decryption key */
struct DSApriv
{
	DSApub	pub;
	mpint	*secret;	/* (decryption key) */
};

/* signature */
struct DSAsig
{
	mpint	*r, *s;
};

DSApriv*	dsagen(DSApub *opub);	/* opub not checked for consistency! */
DSAsig*		dsasign(DSApriv *k, mpint *m);
int		dsaverify(DSApub *k, DSAsig *sig, mpint *m);
DSApub*		dsapuballoc(void);
void		dsapubfree(DSApub*);
DSApriv*	dsaprivalloc(void);
void		dsaprivfree(DSApriv*);
DSAsig*		dsasigalloc(void);
void		dsasigfree(DSAsig*);
DSApub*		dsaprivtopub(DSApriv*);

/*
 * TLS
 */
typedef struct Thumbprint{
	struct Thumbprint *next;
	uchar	hash[SHA2_256dlen];
	uchar	len;
} Thumbprint;

typedef struct TLSconn{
	char	dir[40];	/* connection directory */
	uchar	*cert;	/* certificate (local on input, remote on output) */
	uchar	*sessionID;
	uchar	*psk;
	int	certlen;
	int	sessionIDlen;
	int	psklen;
	int	(*trace)(char*fmt, ...);
	PEMChain*chain;	/* optional extra certificate evidence for servers to present */
	char	*sessionType;
	uchar	*sessionKey;
	int	sessionKeylen;
	char	*sessionConst;
	char	*serverName;
	char	*pskID;
} TLSconn;

/* tlshand.c */
int tlsClient(int fd, TLSconn *c);
int tlsServer(int fd, TLSconn *c);

/* thumb.c */
Thumbprint* initThumbprints(char *ok, char *crl, char *tag);
void	freeThumbprints(Thumbprint *ok);
int	okThumbprint(uchar *hash, int len, Thumbprint *ok);
int	okCertificate(uchar *cert, int len, Thumbprint *ok);

/* readcert.c */
uchar	*readcert(char *filename, int *pcertlen);
PEMChain*readcertchain(char *filename);

/* aes_xts.c */
void aes_xts_encrypt(AESstate *tweak, AESstate *ecb, uvlong sectorNumber, uchar *input, uchar *output, ulong len);
void aes_xts_decrypt(AESstate *tweak, AESstate *ecb, uvlong sectorNumber, uchar *input, uchar *output, ulong len);

typedef struct ECpoint{
	int inf;
	mpint *x;
	mpint *y;
	mpint *z;	/* nil when using affine coordinates */
} ECpoint;

typedef ECpoint ECpub;
typedef struct ECpriv{
	ECpoint;
	mpint *d;
} ECpriv;

typedef struct ECdomain{
	mpint *p;
	mpint *a;
	mpint *b;
	ECpoint G;
	mpint *n;
	mpint *h;
} ECdomain;

void	ecdominit(ECdomain *, void (*init)(mpint *p, mpint *a, mpint *b, mpint *x, mpint *y, mpint *n, mpint *h));
void	ecdomfree(ECdomain *);

void	ecassign(ECdomain *, ECpoint *old, ECpoint *new);
void	ecadd(ECdomain *, ECpoint *a, ECpoint *b, ECpoint *s);
void	ecmul(ECdomain *, ECpoint *a, mpint *k, ECpoint *s);
ECpoint*	strtoec(ECdomain *, char *, char **, ECpoint *);
ECpriv*	ecgen(ECdomain *, ECpriv*);
int	ecverify(ECdomain *, ECpoint *);
int	ecpubverify(ECdomain *, ECpub *);
void	ecdsasign(ECdomain *, ECpriv *, uchar *, int, mpint *, mpint *);
int	ecdsaverify(ECdomain *, ECpub *, uchar *, int, mpint *, mpint *);
void	base58enc(uchar *, char *, int);
int	base58dec(char *, uchar *, int);

ECpub*	ecdecodepub(ECdomain *dom, uchar *, int);
int	ecencodepub(ECdomain *dom, ECpub *, uchar *, int);
void	ecpubfree(ECpub *);

ECpub*	X509toECpub(uchar *cert, int ncert, char *name, int nname, ECdomain *dom);
char*	X509ecdsaverify(uchar *cert, int ncert, ECdomain *dom, ECpub *pub);
char*	X509ecdsaverifydigest(uchar *sig, int siglen, uchar *edigest, int edigestlen, ECdomain *dom, ECpub *pub);

/* curves */
void	secp256r1(mpint *p, mpint *a, mpint *b, mpint *x, mpint *y, mpint *n, mpint *h);
void	secp256k1(mpint *p, mpint *a, mpint *b, mpint *x, mpint *y, mpint *n, mpint *h);
void	secp384r1(mpint *p, mpint *a, mpint *b, mpint *x, mpint *y, mpint *n, mpint *h);

DigestState*	ripemd160(uchar *, ulong, uchar *, DigestState *);

/*
 * Diffie-Hellman key exchange
 */

typedef struct DHstate DHstate;
struct DHstate
{
	mpint	*g;	/* base g */
	mpint	*p;	/* large prime */
	mpint	*q;	/* subgroup prime */
	mpint	*x;	/* random secret */
	mpint	*y;	/* public key y = g**x % p */
};

/* generate new public key: y = g**x % p */
mpint* dh_new(DHstate *dh, mpint *p, mpint *q, mpint *g);

/* calculate shared key: k = y**x % p */
mpint* dh_finish(DHstate *dh, mpint *y);

/* Curve25519 elliptic curve, public key function */
void curve25519(uchar mypublic[32], uchar secret[32], uchar basepoint[32]);

/* Curve25519 diffie hellman */
void curve25519_dh_new(uchar x[32], uchar y[32]);
void curve25519_dh_finish(uchar x[32], uchar y[32], uchar z[32]);

/* password-based key derivation function 2 (rfc2898) */
void pbkdf2_x(uchar *p, ulong plen, uchar *s, ulong slen, ulong rounds, uchar *d, ulong dlen,
	DigestState* (*x)(uchar*, ulong, uchar*, ulong, uchar*, DigestState*), int xlen);

/* scrypt password-based key derivation function */
char* scrypt(uchar *p, ulong plen, uchar *s, ulong slen,
	ulong N, ulong R, ulong P,
	uchar *d, ulong dlen);

/* hmac-based key derivation function (rfc5869) */
void hkdf_x(uchar *salt, ulong nsalt, uchar *info, ulong ninfo, uchar *key, ulong nkey, uchar *d, ulong dlen,
	DigestState* (*x)(uchar*, ulong, uchar*, ulong, uchar*, DigestState*), int xlen);

/* timing safe memcmp() */
int tsmemcmp(void*, void*, ulong);
