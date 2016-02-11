#include <u.h>
#include <libc.h>
#include <bio.h>
#include <auth.h>
#include <mp.h>
#include <libsec.h>

// The main groups of functions are:
//		client/server - main handshake protocol definition
//		message functions - formating handshake messages
//		cipher choices - catalog of digest and encrypt algorithms
//		security functions - PKCS#1, sslHMAC, session keygen
//		general utility functions - malloc, serialization
// The handshake protocol builds on the TLS/SSL3 record layer protocol,
// which is implemented in kernel device #a.  See also /lib/rfc/rfc2246.

enum {
	TLSFinishedLen = 12,
	SSL3FinishedLen = MD5dlen+SHA1dlen,
	MaxKeyData = 160,	// amount of secret we may need
	MaxChunk = 1<<15,
	MAXdlen = SHA2_512dlen,
	RandomSize = 32,
	SidSize = 32,
	MasterSecretSize = 48,
	AQueue = 0,
	AFlush = 1,
};

typedef struct TlsSec TlsSec;

typedef struct Bytes{
	int len;
	uchar data[1];  // [len]
} Bytes;

typedef struct Ints{
	int len;
	int data[1];  // [len]
} Ints;

typedef struct Algs{
	char *enc;
	char *digest;
	int nsecret;
	int tlsid;
	int ok;
} Algs;

typedef struct Namedcurve{
	int tlsid;
	void (*init)(mpint *p, mpint *a, mpint *b, mpint *x, mpint *y, mpint *n, mpint *h);
} Namedcurve;

typedef struct Finished{
	uchar verify[SSL3FinishedLen];
	int n;
} Finished;

typedef struct HandshakeHash {
	MD5state	md5;
	SHAstate	sha1;
	SHA2_256state	sha2_256;
} HandshakeHash;

typedef struct TlsConnection{
	TlsSec *sec;	// security management goo
	int hand, ctl;	// record layer file descriptors
	int erred;		// set when tlsError called
	int (*trace)(char*fmt, ...); // for debugging
	int version;	// protocol we are speaking
	int verset;		// version has been set
	int ver2hi;		// server got a version 2 hello
	int isClient;	// is this the client or server?
	Bytes *sid;		// SessionID
	Bytes *cert;	// only last - no chain

	Lock statelk;
	int state;		// must be set using setstate

	// input buffer for handshake messages
	uchar recvbuf[MaxChunk];
	uchar *rp, *ep;

	// output buffer
	uchar sendbuf[MaxChunk];
	uchar *sendp;

	uchar crandom[RandomSize];	// client random
	uchar srandom[RandomSize];	// server random
	int clientVersion;	// version in ClientHello
	int cipher;
	char *digest;	// name of digest algorithm to use
	char *enc;		// name of encryption algorithm to use
	int nsecret;	// amount of secret data to init keys

	// for finished messages
	HandshakeHash	handhash;
	Finished	finished;
} TlsConnection;

typedef struct Msg{
	int tag;
	union {
		struct {
			int version;
			uchar 	random[RandomSize];
			Bytes*	sid;
			Ints*	ciphers;
			Bytes*	compressors;
			Bytes*	extensions;
		} clientHello;
		struct {
			int version;
			uchar	random[RandomSize];
			Bytes*	sid;
			int	cipher;
			int	compressor;
			Bytes*	extensions;
		} serverHello;
		struct {
			int ncert;
			Bytes **certs;
		} certificate;
		struct {
			Bytes *types;
			Ints *sigalgs;
			int nca;
			Bytes **cas;
		} certificateRequest;
		struct {
			Bytes *pskid;
			Bytes *key;
		} clientKeyExchange;
		struct {
			Bytes *pskid;
			Bytes *dh_p;
			Bytes *dh_g;
			Bytes *dh_Ys;
			Bytes *dh_parameters;
			Bytes *dh_signature;
			int sigalg;
			int curve;
		} serverKeyExchange;
		struct {
			int sigalg;
			Bytes *signature;
		} certificateVerify;		
		Finished finished;
	} u;
} Msg;

typedef struct TlsSec{
	char *server;	// name of remote; nil for server
	int ok;	// <0 killed; == 0 in progress; >0 reusable
	RSApub *rsapub;
	AuthRpc *rpc;	// factotum for rsa private key
	uchar *psk;	// pre-shared key
	int psklen;
	uchar sec[MasterSecretSize];	// master secret
	uchar crandom[RandomSize];	// client random
	uchar srandom[RandomSize];	// server random
	int clientVers;		// version in ClientHello
	int vers;			// final version
	// byte generation and handshake checksum
	void (*prf)(uchar*, int, uchar*, int, char*, uchar*, int, uchar*, int);
	void (*setFinished)(TlsSec*, HandshakeHash, uchar*, int);
	int nfin;
} TlsSec;


enum {
	SSL3Version	= 0x0300,
	TLS10Version	= 0x0301,
	TLS11Version	= 0x0302,
	TLS12Version	= 0x0303,
	ProtocolVersion	= TLS12Version,	// maximum version we speak
	MinProtoVersion	= 0x0300,	// limits on version we accept
	MaxProtoVersion	= 0x03ff,
};

// handshake type
enum {
	HHelloRequest,
	HClientHello,
	HServerHello,
	HSSL2ClientHello = 9,  /* local convention;  see devtls.c */
	HCertificate = 11,
	HServerKeyExchange,
	HCertificateRequest,
	HServerHelloDone,
	HCertificateVerify,
	HClientKeyExchange,
	HFinished = 20,
	HMax
};

// alerts
enum {
	ECloseNotify = 0,
	EUnexpectedMessage = 10,
	EBadRecordMac = 20,
	EDecryptionFailed = 21,
	ERecordOverflow = 22,
	EDecompressionFailure = 30,
	EHandshakeFailure = 40,
	ENoCertificate = 41,
	EBadCertificate = 42,
	EUnsupportedCertificate = 43,
	ECertificateRevoked = 44,
	ECertificateExpired = 45,
	ECertificateUnknown = 46,
	EIllegalParameter = 47,
	EUnknownCa = 48,
	EAccessDenied = 49,
	EDecodeError = 50,
	EDecryptError = 51,
	EExportRestriction = 60,
	EProtocolVersion = 70,
	EInsufficientSecurity = 71,
	EInternalError = 80,
	EUserCanceled = 90,
	ENoRenegotiation = 100,
	EUnknownPSKidentity = 115,
	EMax = 256
};

// cipher suites
enum {
	TLS_NULL_WITH_NULL_NULL			= 0x0000,
	TLS_RSA_WITH_NULL_MD5			= 0x0001,
	TLS_RSA_WITH_NULL_SHA			= 0x0002,
	TLS_RSA_EXPORT_WITH_RC4_40_MD5		= 0x0003,
	TLS_RSA_WITH_RC4_128_MD5		= 0x0004,
	TLS_RSA_WITH_RC4_128_SHA		= 0x0005,
	TLS_RSA_EXPORT_WITH_RC2_CBC_40_MD5	= 0X0006,
	TLS_RSA_WITH_IDEA_CBC_SHA		= 0X0007,
	TLS_RSA_EXPORT_WITH_DES40_CBC_SHA	= 0X0008,
	TLS_RSA_WITH_DES_CBC_SHA		= 0X0009,
	TLS_RSA_WITH_3DES_EDE_CBC_SHA		= 0X000A,
	TLS_DH_DSS_EXPORT_WITH_DES40_CBC_SHA	= 0X000B,
	TLS_DH_DSS_WITH_DES_CBC_SHA		= 0X000C,
	TLS_DH_DSS_WITH_3DES_EDE_CBC_SHA	= 0X000D,
	TLS_DH_RSA_EXPORT_WITH_DES40_CBC_SHA	= 0X000E,
	TLS_DH_RSA_WITH_DES_CBC_SHA		= 0X000F,
	TLS_DH_RSA_WITH_3DES_EDE_CBC_SHA	= 0X0010,
	TLS_DHE_DSS_EXPORT_WITH_DES40_CBC_SHA	= 0X0011,
	TLS_DHE_DSS_WITH_DES_CBC_SHA		= 0X0012,
	TLS_DHE_DSS_WITH_3DES_EDE_CBC_SHA	= 0X0013,	// ZZZ must be implemented for tls1.0 compliance
	TLS_DHE_RSA_EXPORT_WITH_DES40_CBC_SHA	= 0X0014,
	TLS_DHE_RSA_WITH_DES_CBC_SHA		= 0X0015,
	TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA	= 0X0016,
	TLS_DH_anon_EXPORT_WITH_RC4_40_MD5	= 0x0017,
	TLS_DH_anon_WITH_RC4_128_MD5		= 0x0018,
	TLS_DH_anon_EXPORT_WITH_DES40_CBC_SHA	= 0X0019,
	TLS_DH_anon_WITH_DES_CBC_SHA		= 0X001A,
	TLS_DH_anon_WITH_3DES_EDE_CBC_SHA	= 0X001B,
	TLS_RSA_WITH_AES_128_CBC_SHA		= 0X002F,	// aes, aka rijndael with 128 bit blocks
	TLS_DH_DSS_WITH_AES_128_CBC_SHA		= 0X0030,
	TLS_DH_RSA_WITH_AES_128_CBC_SHA		= 0X0031,
	TLS_DHE_DSS_WITH_AES_128_CBC_SHA	= 0X0032,
	TLS_DHE_RSA_WITH_AES_128_CBC_SHA	= 0X0033,
	TLS_DH_anon_WITH_AES_128_CBC_SHA	= 0X0034,
	TLS_RSA_WITH_AES_256_CBC_SHA		= 0X0035,
	TLS_DH_DSS_WITH_AES_256_CBC_SHA		= 0X0036,
	TLS_DH_RSA_WITH_AES_256_CBC_SHA		= 0X0037,
	TLS_DHE_DSS_WITH_AES_256_CBC_SHA	= 0X0038,
	TLS_DHE_RSA_WITH_AES_256_CBC_SHA	= 0X0039,
	TLS_DH_anon_WITH_AES_256_CBC_SHA	= 0X003A,
	TLS_RSA_WITH_AES_128_CBC_SHA256		= 0X003C,
	TLS_RSA_WITH_AES_256_CBC_SHA256		= 0X003D,
	TLS_DHE_RSA_WITH_AES_128_CBC_SHA256	= 0X0067,
	TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA	= 0XC013,
	TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA	= 0XC014,
	TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256	= 0xC027,
	TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256	= 0xC023,

	TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305	= 0xCCA8,
	TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305	= 0xCCA9,
	TLS_DHE_RSA_WITH_CHACHA20_POLY1305	= 0xCCAA,

	GOOGLE_ECDHE_RSA_WITH_CHACHA20_POLY1305		= 0xCC13,
	GOOGLE_ECDHE_ECDSA_WITH_CHACHA20_POLY1305	= 0xCC14,
	GOOGLE_DHE_RSA_WITH_CHACHA20_POLY1305		= 0xCC15,

	TLS_PSK_WITH_CHACHA20_POLY1305		= 0xCCAB,
	TLS_PSK_WITH_AES_128_CBC_SHA256		= 0x00AE,
	TLS_PSK_WITH_AES_128_CBC_SHA		= 0x008C,
};

// compression methods
enum {
	CompressionNull = 0,
	CompressionMax
};

static Algs cipherAlgs[] = {
	{"ccpoly96_aead", "clear", 2*(32+12), TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305},
	{"ccpoly96_aead", "clear", 2*(32+12), TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305},
	{"ccpoly96_aead", "clear", 2*(32+12), TLS_DHE_RSA_WITH_CHACHA20_POLY1305},

	{"ccpoly64_aead", "clear", 2*32, GOOGLE_ECDHE_RSA_WITH_CHACHA20_POLY1305},
	{"ccpoly64_aead", "clear", 2*32, GOOGLE_ECDHE_ECDSA_WITH_CHACHA20_POLY1305},
	{"ccpoly64_aead", "clear", 2*32, GOOGLE_DHE_RSA_WITH_CHACHA20_POLY1305},

	{"aes_128_cbc", "sha256", 2*(16+16+SHA2_256dlen), TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256},
	{"aes_128_cbc", "sha256", 2*(16+16+SHA2_256dlen), TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256},
	{"aes_128_cbc", "sha1", 2*(16+16+SHA1dlen), TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA},
	{"aes_256_cbc", "sha1", 2*(32+16+SHA1dlen), TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA},
	{"aes_128_cbc", "sha256", 2*(16+16+SHA2_256dlen), TLS_DHE_RSA_WITH_AES_128_CBC_SHA256},
	{"aes_128_cbc", "sha1", 2*(16+16+SHA1dlen), TLS_DHE_RSA_WITH_AES_128_CBC_SHA},
	{"aes_256_cbc", "sha1", 2*(32+16+SHA1dlen), TLS_DHE_RSA_WITH_AES_256_CBC_SHA},
	{"aes_128_cbc", "sha256", 2*(16+16+SHA2_256dlen), TLS_RSA_WITH_AES_128_CBC_SHA256},
	{"aes_256_cbc", "sha256", 2*(32+16+SHA2_256dlen), TLS_RSA_WITH_AES_256_CBC_SHA256},
	{"aes_128_cbc", "sha1", 2*(16+16+SHA1dlen), TLS_RSA_WITH_AES_128_CBC_SHA},
	{"aes_256_cbc", "sha1", 2*(32+16+SHA1dlen), TLS_RSA_WITH_AES_256_CBC_SHA},
	{"3des_ede_cbc","sha1",	2*(4*8+SHA1dlen), TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA},
	{"3des_ede_cbc","sha1",	2*(4*8+SHA1dlen), TLS_RSA_WITH_3DES_EDE_CBC_SHA},

	// PSK cipher suits
	{"ccpoly96_aead", "clear", 2*(32+12), TLS_PSK_WITH_CHACHA20_POLY1305},
	{"aes_128_cbc", "sha256", 2*(16+16+SHA2_256dlen), TLS_PSK_WITH_AES_128_CBC_SHA256},
	{"aes_128_cbc", "sha1", 2*(16+16+SHA1dlen), TLS_PSK_WITH_AES_128_CBC_SHA},
};

static uchar compressors[] = {
	CompressionNull,
};

static Namedcurve namedcurves[] = {
	0x0017, secp256r1,
};

static uchar pointformats[] = {
	CompressionNull /* support of uncompressed point format is mandatory */
};

static struct {
	DigestState* (*fun)(uchar*, ulong, uchar*, DigestState*);
	int len;
} hashfun[] = {
	[0x01]	{md5,		MD5dlen},
	[0x02]	{sha1,		SHA1dlen},
	[0x03]	{sha2_224,	SHA2_224dlen},
	[0x04]	{sha2_256,	SHA2_256dlen},
	[0x05]	{sha2_384,	SHA2_384dlen},
	[0x06]	{sha2_512,	SHA2_512dlen},
};

// signature algorithms (only RSA and ECDSA at the moment)
static int sigalgs[] = {
	0x0603,		/* SHA512 ECDSA */
	0x0503,		/* SHA384 ECDSA */
	0x0403,		/* SHA256 ECDSA */
	0x0203,		/* SHA1 ECDSA */

	0x0601,		/* SHA512 RSA */
	0x0501,		/* SHA384 RSA */
	0x0401,		/* SHA256 RSA */
	0x0201,		/* SHA1 RSA */
};

static TlsConnection *tlsServer2(int ctl, int hand,
	uchar *cert, int certlen,
	char *pskid, uchar *psk, int psklen,
	int (*trace)(char*fmt, ...), PEMChain *chain);
static TlsConnection *tlsClient2(int ctl, int hand,
	uchar *csid, int ncsid,
	uchar *cert, int certlen,
	char *pskid, uchar *psk, int psklen,
	uchar *ext, int extlen, int (*trace)(char*fmt, ...));
static void	msgClear(Msg *m);
static char* msgPrint(char *buf, int n, Msg *m);
static int	msgRecv(TlsConnection *c, Msg *m);
static int	msgSend(TlsConnection *c, Msg *m, int act);
static void	tlsError(TlsConnection *c, int err, char *msg, ...);
#pragma	varargck argpos	tlsError 3
static int setVersion(TlsConnection *c, int version);
static int finishedMatch(TlsConnection *c, Finished *f);
static void tlsConnectionFree(TlsConnection *c);

static int setAlgs(TlsConnection *c, int a);
static int okCipher(Ints *cv, int ispsk);
static int okCompression(Bytes *cv);
static int initCiphers(void);
static Ints* makeciphers(int ispsk);

static TlsSec*	tlsSecInits(int cvers, uchar *csid, int ncsid, uchar *crandom, uchar *ssid, int *nssid, uchar *srandom);
static int	tlsSecRSAs(TlsSec *sec, int vers, Bytes *epm);
static int	tlsSecPSKs(TlsSec *sec, int vers);
static TlsSec*	tlsSecInitc(int cvers, uchar *crandom);
static Bytes*	tlsSecRSAc(TlsSec *sec, uchar *sid, int nsid, uchar *srandom, uchar *cert, int ncert, int vers);
static int	tlsSecPSKc(TlsSec *sec, uchar *srandom, int vers);
static Bytes*	tlsSecDHEc(TlsSec *sec, uchar *srandom, int vers, Bytes *p, Bytes *g, Bytes *Ys);
static Bytes*	tlsSecECDHEc(TlsSec *sec, uchar *srandom, int vers, int curve, Bytes *Ys);
static int	tlsSecFinished(TlsSec *sec, HandshakeHash hsh, uchar *fin, int nfin, int isclient);
static void	tlsSecOk(TlsSec *sec);
static void	tlsSecKill(TlsSec *sec);
static void	tlsSecClose(TlsSec *sec);
static void	setMasterSecret(TlsSec *sec, Bytes *pm);
static void	setSecrets(TlsSec *sec, uchar *kd, int nkd);
static Bytes*	pkcs1_encrypt(Bytes* data, RSApub* key, int blocktype);
static Bytes*	pkcs1_decrypt(TlsSec *sec, Bytes *cipher);
static void	tls10SetFinished(TlsSec *sec, HandshakeHash hsh, uchar *finished, int isClient);
static void	tls12SetFinished(TlsSec *sec, HandshakeHash hsh, uchar *finished, int isClient);
static void	sslSetFinished(TlsSec *sec, HandshakeHash hsh, uchar *finished, int isClient);
static void	sslPRF(uchar *buf, int nbuf, uchar *key, int nkey, char *label,
			uchar *seed0, int nseed0, uchar *seed1, int nseed1);
static int setVers(TlsSec *sec, int version);

static AuthRpc* factotum_rsa_open(uchar *cert, int certlen);
static mpint* factotum_rsa_decrypt(AuthRpc *rpc, mpint *cipher);
static void factotum_rsa_close(AuthRpc*rpc);

static void* emalloc(int);
static void* erealloc(void*, int);
static void put32(uchar *p, u32int);
static void put24(uchar *p, int);
static void put16(uchar *p, int);
static u32int get32(uchar *p);
static int get24(uchar *p);
static int get16(uchar *p);
static Bytes* newbytes(int len);
static Bytes* makebytes(uchar* buf, int len);
static Bytes* mptobytes(mpint* big);
static mpint* bytestomp(Bytes* bytes);
static void freebytes(Bytes* b);
static Ints* newints(int len);
static void freeints(Ints* b);

/* x509.c */
extern mpint*	pkcs1padbuf(uchar *buf, int len, mpint *modulus);
extern int	asn1encodedigest(DigestState* (*fun)(uchar*, ulong, uchar*, DigestState*), uchar *digest, uchar *buf, int len);

//================= client/server ========================

//	push TLS onto fd, returning new (application) file descriptor
//		or -1 if error.
int
tlsServer(int fd, TLSconn *conn)
{
	char buf[8];
	char dname[64];
	int n, data, ctl, hand;
	TlsConnection *tls;

	if(conn == nil)
		return -1;
	ctl = open("#a/tls/clone", ORDWR);
	if(ctl < 0)
		return -1;
	n = read(ctl, buf, sizeof(buf)-1);
	if(n < 0){
		close(ctl);
		return -1;
	}
	buf[n] = 0;
	snprint(conn->dir, sizeof(conn->dir), "#a/tls/%s", buf);
	snprint(dname, sizeof(dname), "#a/tls/%s/hand", buf);
	hand = open(dname, ORDWR);
	if(hand < 0){
		close(ctl);
		return -1;
	}
	fprint(ctl, "fd %d 0x%x", fd, ProtocolVersion);
	tls = tlsServer2(ctl, hand,
		conn->cert, conn->certlen,
		conn->pskID, conn->psk, conn->psklen,
		conn->trace, conn->chain);
	snprint(dname, sizeof(dname), "#a/tls/%s/data", buf);
	data = open(dname, ORDWR);
	close(hand);
	close(ctl);
	if(data < 0 || tls == nil){
		if(tls != nil)
			tlsConnectionFree(tls);
		return -1;
	}
	free(conn->cert);
	conn->cert = nil;  // client certificates are not yet implemented
	conn->certlen = 0;
	conn->sessionIDlen = tls->sid->len;
	conn->sessionID = emalloc(conn->sessionIDlen);
	memcpy(conn->sessionID, tls->sid->data, conn->sessionIDlen);
	if(conn->sessionKey != nil
	&& conn->sessionType != nil
	&& strcmp(conn->sessionType, "ttls") == 0)
		tls->sec->prf(
			conn->sessionKey, conn->sessionKeylen,
			tls->sec->sec, MasterSecretSize,
			conn->sessionConst, 
			tls->sec->crandom, RandomSize,
			tls->sec->srandom, RandomSize);
	tlsConnectionFree(tls);
	close(fd);
	return data;
}

static uchar*
tlsClientExtensions(TLSconn *conn, int *plen)
{
	uchar *b, *p;
	int i, n, m;

	p = b = nil;

	// RFC6066 - Server Name Identification
	if(conn->serverName != nil){
		n = strlen(conn->serverName);

		m = p - b;
		b = erealloc(b, m + 2+2+2+1+2+n);
		p = b + m;

		put16(p, 0), p += 2;		/* Type: server_name */
		put16(p, 2+1+2+n), p += 2;	/* Length */
		put16(p, 1+2+n), p += 2;	/* Server Name list length */
		*p++ = 0;			/* Server Name Type: host_name */
		put16(p, n), p += 2;		/* Server Name length */
		memmove(p, conn->serverName, n);
		p += n;
	}

	// ECDHE
	if(1){
		m = p - b;
		b = erealloc(b, m + 2+2+2+nelem(namedcurves)*2 + 2+2+1+nelem(pointformats));
		p = b + m;

		n = nelem(namedcurves);
		put16(p, 0x000a), p += 2;	/* Type: elliptic_curves */
		put16(p, (n+1)*2), p += 2;	/* Length */
		put16(p, n*2), p += 2;		/* Elliptic Curves Length */
		for(i=0; i < n; i++){		/* Elliptic curves */
			put16(p, namedcurves[i].tlsid);
			p += 2;
		}

		n = nelem(pointformats);
		put16(p, 0x000b), p += 2;	/* Type: ec_point_formats */
		put16(p, n+1), p += 2;		/* Length */
		*p++ = n;			/* EC point formats Length */
		for(i=0; i < n; i++)		/* Elliptic curves point formats */
			*p++ = pointformats[i];
	}

	// signature algorithms
	if(ProtocolVersion >= TLS12Version){
		n = nelem(sigalgs);

		m = p - b;
		b = erealloc(b, m + 2+2+2+n*2);
		p = b + m;

		put16(p, 0x000d), p += 2;
		put16(p, n*2 + 2), p += 2;
		put16(p, n*2), p += 2;
		for(i=0; i < n; i++){
			put16(p, sigalgs[i]);
			p += 2;
		}
	}
	
	*plen = p - b;
	return b;
}

//	push TLS onto fd, returning new (application) file descriptor
//		or -1 if error.
int
tlsClient(int fd, TLSconn *conn)
{
	char buf[8];
	char dname[64];
	int n, data, ctl, hand;
	TlsConnection *tls;
	uchar *ext;

	if(conn == nil)
		return -1;
	ctl = open("#a/tls/clone", ORDWR);
	if(ctl < 0)
		return -1;
	n = read(ctl, buf, sizeof(buf)-1);
	if(n < 0){
		close(ctl);
		return -1;
	}
	buf[n] = 0;
	snprint(conn->dir, sizeof(conn->dir), "#a/tls/%s", buf);
	snprint(dname, sizeof(dname), "#a/tls/%s/hand", buf);
	hand = open(dname, ORDWR);
	if(hand < 0){
		close(ctl);
		return -1;
	}
	snprint(dname, sizeof(dname), "#a/tls/%s/data", buf);
	data = open(dname, ORDWR);
	if(data < 0){
		close(hand);
		close(ctl);
		return -1;
	}
	fprint(ctl, "fd %d 0x%x", fd, ProtocolVersion);
	ext = tlsClientExtensions(conn, &n);
	tls = tlsClient2(ctl, hand,
		conn->sessionID, conn->sessionIDlen,
		conn->cert, conn->certlen, 
		conn->pskID, conn->psk, conn->psklen,
		ext, n, conn->trace);
	free(ext);
	close(hand);
	close(ctl);
	if(tls == nil){
		close(data);
		return -1;
	}
	if(tls->cert != nil){
		conn->certlen = tls->cert->len;
		conn->cert = emalloc(conn->certlen);
		memcpy(conn->cert, tls->cert->data, conn->certlen);
	} else {
		conn->certlen = 0;
		conn->cert = nil;
	}
	conn->sessionIDlen = tls->sid->len;
	conn->sessionID = emalloc(conn->sessionIDlen);
	memcpy(conn->sessionID, tls->sid->data, conn->sessionIDlen);
	if(conn->sessionKey != nil
	&& conn->sessionType != nil
	&& strcmp(conn->sessionType, "ttls") == 0)
		tls->sec->prf(
			conn->sessionKey, conn->sessionKeylen,
			tls->sec->sec, MasterSecretSize,
			conn->sessionConst, 
			tls->sec->crandom, RandomSize,
			tls->sec->srandom, RandomSize);
	tlsConnectionFree(tls);
	close(fd);
	return data;
}

static int
countchain(PEMChain *p)
{
	int i = 0;

	while (p) {
		i++;
		p = p->next;
	}
	return i;
}

static TlsConnection *
tlsServer2(int ctl, int hand,
	uchar *cert, int certlen,
	char *pskid, uchar *psk, int psklen,
	int (*trace)(char*fmt, ...), PEMChain *chp)
{
	TlsConnection *c;
	Msg m;
	Bytes *csid;
	uchar sid[SidSize], kd[MaxKeyData];
	char *secrets;
	int cipher, compressor, nsid, rv, numcerts, i;

	if(trace)
		trace("tlsServer2\n");
	if(!initCiphers())
		return nil;
	c = emalloc(sizeof(TlsConnection));
	c->ctl = ctl;
	c->hand = hand;
	c->trace = trace;
	c->version = ProtocolVersion;

	memset(&m, 0, sizeof(m));
	if(!msgRecv(c, &m)){
		if(trace)
			trace("initial msgRecv failed\n");
		goto Err;
	}
	if(m.tag != HClientHello) {
		tlsError(c, EUnexpectedMessage, "expected a client hello");
		goto Err;
	}
	c->clientVersion = m.u.clientHello.version;
	if(trace)
		trace("ClientHello version %x\n", c->clientVersion);
	if(setVersion(c, c->clientVersion) < 0) {
		tlsError(c, EIllegalParameter, "incompatible version");
		goto Err;
	}

	memmove(c->crandom, m.u.clientHello.random, RandomSize);
	cipher = okCipher(m.u.clientHello.ciphers, psklen > 0);
	if(cipher < 0) {
		// reply with EInsufficientSecurity if we know that's the case
		if(cipher == -2)
			tlsError(c, EInsufficientSecurity, "cipher suites too weak");
		else
			tlsError(c, EHandshakeFailure, "no matching cipher suite");
		goto Err;
	}
	if(!setAlgs(c, cipher)){
		tlsError(c, EHandshakeFailure, "no matching cipher suite");
		goto Err;
	}
	compressor = okCompression(m.u.clientHello.compressors);
	if(compressor < 0) {
		tlsError(c, EHandshakeFailure, "no matching compressor");
		goto Err;
	}

	csid = m.u.clientHello.sid;
	if(trace)
		trace("  cipher %x, compressor %x, csidlen %d\n", cipher, compressor, csid->len);
	c->sec = tlsSecInits(c->clientVersion, csid->data, csid->len, c->crandom, sid, &nsid, c->srandom);
	if(c->sec == nil){
		tlsError(c, EHandshakeFailure, "can't initialize security: %r");
		goto Err;
	}
	if(psklen > 0){
		c->sec->psk = psk;
		c->sec->psklen = psklen;
	}
	if(certlen > 0){
		c->sec->rpc = factotum_rsa_open(cert, certlen);
		if(c->sec->rpc == nil){
			tlsError(c, EHandshakeFailure, "factotum_rsa_open: %r");
			goto Err;
		}
		c->sec->rsapub = X509toRSApub(cert, certlen, nil, 0);
		if(c->sec->rsapub == nil){
			tlsError(c, EHandshakeFailure, "invalid X509/rsa certificate");
			goto Err;
		}
	}
	msgClear(&m);

	m.tag = HServerHello;
	m.u.serverHello.version = c->version;
	memmove(m.u.serverHello.random, c->srandom, RandomSize);
	m.u.serverHello.cipher = cipher;
	m.u.serverHello.compressor = compressor;
	c->sid = makebytes(sid, nsid);
	m.u.serverHello.sid = makebytes(c->sid->data, c->sid->len);
	if(!msgSend(c, &m, AQueue))
		goto Err;
	msgClear(&m);

	if(certlen > 0){
		m.tag = HCertificate;
		numcerts = countchain(chp);
		m.u.certificate.ncert = 1 + numcerts;
		m.u.certificate.certs = emalloc(m.u.certificate.ncert * sizeof(Bytes*));
		m.u.certificate.certs[0] = makebytes(cert, certlen);
		for (i = 0; i < numcerts && chp; i++, chp = chp->next)
			m.u.certificate.certs[i+1] = makebytes(chp->pem, chp->pemlen);
		if(!msgSend(c, &m, AQueue))
			goto Err;
		msgClear(&m);
	}

	m.tag = HServerHelloDone;
	if(!msgSend(c, &m, AFlush))
		goto Err;
	msgClear(&m);

	if(!msgRecv(c, &m))
		goto Err;
	if(m.tag != HClientKeyExchange) {
		tlsError(c, EUnexpectedMessage, "expected a client key exchange");
		goto Err;
	}
	if(pskid != nil){
		if(m.u.clientKeyExchange.pskid == nil
		|| m.u.clientKeyExchange.pskid->len != strlen(pskid)
		|| memcmp(pskid, m.u.clientKeyExchange.pskid->data, m.u.clientKeyExchange.pskid->len) != 0){
			tlsError(c, EUnknownPSKidentity, "unknown or missing pskid");
			goto Err;
		}
	}
	if(certlen > 0){
		if(tlsSecRSAs(c->sec, c->version, m.u.clientKeyExchange.key) < 0){
			tlsError(c, EHandshakeFailure, "couldn't set secrets: %r");
			goto Err;
		}
	} else if(psklen > 0){
		if(tlsSecPSKs(c->sec, c->version) < 0){
			tlsError(c, EHandshakeFailure, "couldn't set secrets: %r");
			goto Err;
		}
	} else {
		tlsError(c, EInternalError, "no psk or certificate");
		goto Err;
	}

	setSecrets(c->sec, kd, c->nsecret);
	if(trace)
		trace("tls secrets\n");
	secrets = (char*)emalloc(2*c->nsecret);
	enc64(secrets, 2*c->nsecret, kd, c->nsecret);
	rv = fprint(c->ctl, "secret %s %s 0 %s", c->digest, c->enc, secrets);
	memset(secrets, 0, 2*c->nsecret);
	free(secrets);
	memset(kd, 0, c->nsecret);
	if(rv < 0){
		tlsError(c, EHandshakeFailure, "can't set keys: %r");
		goto Err;
	}
	msgClear(&m);

	/* no CertificateVerify; skip to Finished */
	if(tlsSecFinished(c->sec, c->handhash, c->finished.verify, c->finished.n, 1) < 0){
		tlsError(c, EInternalError, "can't set finished: %r");
		goto Err;
	}
	if(!msgRecv(c, &m))
		goto Err;
	if(m.tag != HFinished) {
		tlsError(c, EUnexpectedMessage, "expected a finished");
		goto Err;
	}
	if(!finishedMatch(c, &m.u.finished)) {
		tlsError(c, EHandshakeFailure, "finished verification failed");
		goto Err;
	}
	msgClear(&m);

	/* change cipher spec */
	if(fprint(c->ctl, "changecipher") < 0){
		tlsError(c, EInternalError, "can't enable cipher: %r");
		goto Err;
	}

	if(tlsSecFinished(c->sec, c->handhash, c->finished.verify, c->finished.n, 0) < 0){
		tlsError(c, EInternalError, "can't set finished: %r");
		goto Err;
	}
	m.tag = HFinished;
	m.u.finished = c->finished;
	if(!msgSend(c, &m, AFlush))
		goto Err;
	msgClear(&m);
	if(trace)
		trace("tls finished\n");

	if(fprint(c->ctl, "opened") < 0)
		goto Err;
	tlsSecOk(c->sec);
	return c;

Err:
	msgClear(&m);
	tlsConnectionFree(c);
	return 0;
}

static int
isDHE(int tlsid)
{
	switch(tlsid){
	case TLS_DHE_RSA_WITH_AES_128_CBC_SHA256:
 	case TLS_DHE_RSA_WITH_AES_128_CBC_SHA:
 	case TLS_DHE_RSA_WITH_AES_256_CBC_SHA:
 	case TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA:
	case TLS_DHE_RSA_WITH_CHACHA20_POLY1305:
	case GOOGLE_DHE_RSA_WITH_CHACHA20_POLY1305:
		return 1;
	}
	return 0;
}

static int
isECDHE(int tlsid)
{
	switch(tlsid){
	case TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305:
	case TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305:

	case GOOGLE_ECDHE_ECDSA_WITH_CHACHA20_POLY1305:
	case GOOGLE_ECDHE_RSA_WITH_CHACHA20_POLY1305:

	case TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256:
	case TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256:
	case TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA:
	case TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA:
		return 1;
	}
	return 0;
}

static int
isPSK(int tlsid)
{
	switch(tlsid){
	case TLS_PSK_WITH_CHACHA20_POLY1305:
	case TLS_PSK_WITH_AES_128_CBC_SHA256:
	case TLS_PSK_WITH_AES_128_CBC_SHA:
		return 1;
	}
	return 0;
}

static Bytes*
tlsSecDHEc(TlsSec *sec, uchar *srandom, int vers, 
	Bytes *p, Bytes *g, Bytes *Ys)
{
	mpint *G, *P, *Y, *K;
	Bytes *epm;
	DHstate dh;

	if(p == nil || g == nil || Ys == nil)
		return nil;

	memmove(sec->srandom, srandom, RandomSize);
	if(setVers(sec, vers) < 0)
		return nil;

	epm = nil;
	P = bytestomp(p);
	G = bytestomp(g);
	Y = bytestomp(Ys);
	K = nil;

	if(P == nil || G == nil || Y == nil || dh_new(&dh, P, nil, G) == nil)
		goto Out;
	epm = mptobytes(dh.y);
	K = dh_finish(&dh, Y);
	if(K == nil){
		freebytes(epm);
		epm = nil;
		goto Out;
	}
	setMasterSecret(sec, mptobytes(K));

Out:
	mpfree(K);
	mpfree(Y);
	mpfree(G);
	mpfree(P);

	return epm;
}

static Bytes*
tlsSecECDHEc(TlsSec *sec, uchar *srandom, int vers, int curve, Bytes *Ys)
{
	Namedcurve *nc, *enc;
	Bytes *epm;
	ECdomain dom;
	ECpub *pub;
	ECpoint K;
	ECpriv Q;

	if(Ys == nil)
		return nil;

	enc = &namedcurves[nelem(namedcurves)];
	for(nc = namedcurves; nc != enc; nc++)
		if(nc->tlsid == curve)
			break;

	if(nc == enc)
		return nil;
		
	memmove(sec->srandom, srandom, RandomSize);
	if(setVers(sec, vers) < 0)
		return nil;
	
	ecdominit(&dom, nc->init);
	pub = ecdecodepub(&dom, Ys->data, Ys->len);
	if(pub == nil){
		ecdomfree(&dom);
		return nil;
	}

	memset(&Q, 0, sizeof(Q));
	Q.x = mpnew(0);
	Q.y = mpnew(0);
	Q.d = mpnew(0);

	memset(&K, 0, sizeof(K));
	K.x = mpnew(0);
	K.y = mpnew(0);

	epm = nil;
	if(ecgen(&dom, &Q) != nil){
		ecmul(&dom, pub, Q.d, &K);
		setMasterSecret(sec, mptobytes(K.x));
		epm = newbytes(1 + 2*((mpsignif(dom.p)+7)/8));
		epm->len = ecencodepub(&dom, &Q, epm->data, epm->len);
	}

	mpfree(K.x);
	mpfree(K.y);
	mpfree(Q.x);
	mpfree(Q.y);
	mpfree(Q.d);

	ecpubfree(pub);
	ecdomfree(&dom);

	return epm;
}

static char*
verifyDHparams(TlsConnection *c, Bytes *par, Bytes *sig, int sigalg)
{
	uchar digest[MAXdlen];
	int digestlen;
	ECdomain dom;
	ECpub *ecpk;
	RSApub *rsapk;
	Bytes *blob;
	char *err;

	if(par == nil || par->len <= 0)
		return "no dh parameters";

	if(sig == nil || sig->len <= 0){
		if(c->sec->psklen > 0)
			return nil;
		return "no signature";
	}

	if(c->cert == nil)
		return "no certificate";

	blob = newbytes(2*RandomSize + par->len);
	memmove(blob->data+0*RandomSize, c->crandom, RandomSize);
	memmove(blob->data+1*RandomSize, c->srandom, RandomSize);
	memmove(blob->data+2*RandomSize, par->data, par->len);
	if(c->version < TLS12Version){
		digestlen = MD5dlen + SHA1dlen;
		md5(blob->data, blob->len, digest, nil);
		sha1(blob->data, blob->len, digest+MD5dlen, nil);
	} else {
		int hashalg = (sigalg>>8) & 0xFF;
		digestlen = -1;
		if(hashalg < nelem(hashfun) && hashfun[hashalg].fun != nil){
			digestlen = hashfun[hashalg].len;
			(*hashfun[hashalg].fun)(blob->data, blob->len, digest, nil);
		}
	}
	freebytes(blob);

	if(digestlen <= 0)
		return "unknown signature digest algorithm";
	
	switch(sigalg & 0xFF){
	case 0x01:
		rsapk = X509toRSApub(c->cert->data, c->cert->len, nil, 0);
		if(rsapk == nil)
			return "bad certificate";
		err = X509rsaverifydigest(sig->data, sig->len, digest, digestlen, rsapk);
		rsapubfree(rsapk);
		break;
	case 0x03:
		ecpk = X509toECpub(c->cert->data, c->cert->len, &dom);
		if(ecpk == nil)
			return "bad certificate";
		err = X509ecdsaverifydigest(sig->data, sig->len, digest, digestlen, &dom, ecpk);
		ecdomfree(&dom);
		ecpubfree(ecpk);
		break;
	default:
		err = "signaure algorithm not RSA or ECDSA";
	}

	return err;
}

static TlsConnection *
tlsClient2(int ctl, int hand,
	uchar *csid, int ncsid, 
	uchar *cert, int certlen,
	char *pskid, uchar *psk, int psklen,
	uchar *ext, int extlen,
	int (*trace)(char*fmt, ...))
{
	TlsConnection *c;
	Msg m;
	uchar kd[MaxKeyData];
	char *secrets;
	int creq, dhx, rv, cipher;
	Bytes *epm;

	if(!initCiphers())
		return nil;
	epm = nil;
	c = emalloc(sizeof(TlsConnection));
	c->version = ProtocolVersion;

	c->ctl = ctl;
	c->hand = hand;
	c->trace = trace;
	c->isClient = 1;
	c->clientVersion = c->version;
	c->cert = nil;

	c->sec = tlsSecInitc(c->clientVersion, c->crandom);
	if(c->sec == nil)
		goto Err;

	if(psklen > 0){
		c->sec->psk = psk;
		c->sec->psklen = psklen;
	}

	/* client hello */
	memset(&m, 0, sizeof(m));
	m.tag = HClientHello;
	m.u.clientHello.version = c->clientVersion;
	memmove(m.u.clientHello.random, c->crandom, RandomSize);
	m.u.clientHello.sid = makebytes(csid, ncsid);
	m.u.clientHello.ciphers = makeciphers(psklen > 0);
	m.u.clientHello.compressors = makebytes(compressors,sizeof(compressors));
	m.u.clientHello.extensions = makebytes(ext, extlen);
	if(!msgSend(c, &m, AFlush))
		goto Err;
	msgClear(&m);

	/* server hello */
	if(!msgRecv(c, &m))
		goto Err;
	if(m.tag != HServerHello) {
		tlsError(c, EUnexpectedMessage, "expected a server hello");
		goto Err;
	}
	if(setVersion(c, m.u.serverHello.version) < 0) {
		tlsError(c, EIllegalParameter, "incompatible version: %r");
		goto Err;
	}
	memmove(c->srandom, m.u.serverHello.random, RandomSize);
	c->sid = makebytes(m.u.serverHello.sid->data, m.u.serverHello.sid->len);
	if(c->sid->len != 0 && c->sid->len != SidSize) {
		tlsError(c, EIllegalParameter, "invalid server session identifier");
		goto Err;
	}
	cipher = m.u.serverHello.cipher;
	if((psklen > 0) != isPSK(cipher) || !setAlgs(c, cipher)) {
		tlsError(c, EIllegalParameter, "invalid cipher suite");
		goto Err;
	}
	if(m.u.serverHello.compressor != CompressionNull) {
		tlsError(c, EIllegalParameter, "invalid compression");
		goto Err;
	}
	msgClear(&m);

	dhx = isDHE(cipher) || isECDHE(cipher);
	if(!msgRecv(c, &m))
		goto Err;
	if(m.tag == HCertificate){
		if(m.u.certificate.ncert < 1) {
			tlsError(c, EIllegalParameter, "runt certificate");
			goto Err;
		}
		c->cert = makebytes(m.u.certificate.certs[0]->data, m.u.certificate.certs[0]->len);
		msgClear(&m);
		if(!msgRecv(c, &m))
			goto Err;
	} else if(psklen == 0) {
		tlsError(c, EUnexpectedMessage, "expected a certificate");
		goto Err;
	}
	if(m.tag == HServerKeyExchange) {
		if(dhx){
			char *err = verifyDHparams(c,
				m.u.serverKeyExchange.dh_parameters,
				m.u.serverKeyExchange.dh_signature,
				m.u.serverKeyExchange.sigalg);
			if(err != nil){
				tlsError(c, EBadCertificate, "can't verify dh parameters: %s", err);
				goto Err;
			}
			if(isECDHE(cipher))
				epm = tlsSecECDHEc(c->sec, c->srandom, c->version,
					m.u.serverKeyExchange.curve,
					m.u.serverKeyExchange.dh_Ys);
			else
				epm = tlsSecDHEc(c->sec, c->srandom, c->version,
					m.u.serverKeyExchange.dh_p, 
					m.u.serverKeyExchange.dh_g,
					m.u.serverKeyExchange.dh_Ys);
			if(epm == nil)
				goto Badcert;
		} else if(psklen == 0){
			tlsError(c, EUnexpectedMessage, "got an server key exchange");
			goto Err;
		}
		msgClear(&m);
		if(!msgRecv(c, &m))
			goto Err;
	} else if(dhx){
		tlsError(c, EUnexpectedMessage, "expected server key exchange");
		goto Err;
	}

	/* certificate request (optional) */
	creq = 0;
	if(m.tag == HCertificateRequest) {
		creq = 1;
		msgClear(&m);
		if(!msgRecv(c, &m))
			goto Err;
	}

	if(m.tag != HServerHelloDone) {
		tlsError(c, EUnexpectedMessage, "expected a server hello done");
		goto Err;
	}
	msgClear(&m);

	if(!dhx){
		if(c->cert != nil){
			epm = tlsSecRSAc(c->sec, c->sid->data, c->sid->len, c->srandom,
				c->cert->data, c->cert->len, c->version);
			if(epm == nil){
			Badcert:
				tlsError(c, EBadCertificate, "bad certificate: %r");
				goto Err;
			}
		} else if(psklen > 0) {
			if(tlsSecPSKc(c->sec, c->srandom, c->version) < 0)
				goto Badcert;
		} else {
			tlsError(c, EInternalError, "no psk or certificate");
			goto Err;
		}
	}

	setSecrets(c->sec, kd, c->nsecret);
	secrets = (char*)emalloc(2*c->nsecret);
	enc64(secrets, 2*c->nsecret, kd, c->nsecret);
	rv = fprint(c->ctl, "secret %s %s 1 %s", c->digest, c->enc, secrets);
	memset(secrets, 0, 2*c->nsecret);
	free(secrets);
	memset(kd, 0, c->nsecret);
	if(rv < 0){
		tlsError(c, EHandshakeFailure, "can't set keys: %r");
		goto Err;
	}

	if(creq) {
		if(cert != nil && certlen > 0){
			m.u.certificate.ncert = 1;
			m.u.certificate.certs = emalloc(m.u.certificate.ncert * sizeof(Bytes*));
			m.u.certificate.certs[0] = makebytes(cert, certlen);
		}		
		m.tag = HCertificate;
		if(!msgSend(c, &m, AFlush))
			goto Err;
		msgClear(&m);
	}

	/* client key exchange */
	m.tag = HClientKeyExchange;
	if(psklen > 0){
		if(pskid == nil)
			pskid = "";
		m.u.clientKeyExchange.pskid = makebytes((uchar*)pskid, strlen(pskid));
	}
	m.u.clientKeyExchange.key = epm;
	epm = nil;
	 
	if(!msgSend(c, &m, AFlush))
		goto Err;
	msgClear(&m);

	/* certificate verify */
	if(creq && cert != nil && certlen > 0) {
		mpint *signedMP, *paddedHashes;
		HandshakeHash hsave;
		uchar buf[512];
		int buflen;

		c->sec->rpc = factotum_rsa_open(cert, certlen);
		if(c->sec->rpc == nil){
			tlsError(c, EHandshakeFailure, "factotum_rsa_open: %r");
			goto Err;
		}
		c->sec->rsapub = X509toRSApub(cert, certlen, nil, 0);
		if(c->sec->rsapub == nil){
			tlsError(c, EHandshakeFailure, "invalid X509/rsa certificate");
			goto Err;
		}

		/* save the state for the Finish message */
		hsave = c->handhash;
		if(c->version >= TLS12Version){
			uchar digest[SHA2_256dlen];

			m.u.certificateVerify.sigalg = 0x0401;	/* RSA SHA256 */
			sha2_256(nil, 0, digest, &c->handhash.sha2_256);
			buflen = asn1encodedigest(sha2_256, digest, buf, sizeof(buf));
		} else {
			md5(nil, 0, buf, &c->handhash.md5);
			sha1(nil, 0, buf+MD5dlen, &c->handhash.sha1);
			buflen = MD5dlen+SHA1dlen;
		}
		c->handhash = hsave;

		if(buflen <= 0){
			tlsError(c, EInternalError, "can't encode handshake hashes");
			goto Err;
		}
		
		paddedHashes = pkcs1padbuf(buf, buflen, c->sec->rsapub->n);
		signedMP = factotum_rsa_decrypt(c->sec->rpc, paddedHashes);
		if(signedMP == nil){
			tlsError(c, EHandshakeFailure, "factotum_rsa_decrypt: %r");
			goto Err;
		}
		m.u.certificateVerify.signature = mptobytes(signedMP);
		mpfree(signedMP);

		m.tag = HCertificateVerify;
		if(!msgSend(c, &m, AFlush))
			goto Err;
		msgClear(&m);
	} 

	/* change cipher spec */
	if(fprint(c->ctl, "changecipher") < 0){
		tlsError(c, EInternalError, "can't enable cipher: %r");
		goto Err;
	}

	// Cipherchange must occur immediately before Finished to avoid
	// potential hole;  see section 4.3 of Wagner Schneier 1996.
	if(tlsSecFinished(c->sec, c->handhash, c->finished.verify, c->finished.n, 1) < 0){
		tlsError(c, EInternalError, "can't set finished 1: %r");
		goto Err;
	}
	m.tag = HFinished;
	m.u.finished = c->finished;
	if(!msgSend(c, &m, AFlush)) {
		tlsError(c, EInternalError, "can't flush after client Finished: %r");
		goto Err;
	}
	msgClear(&m);

	if(tlsSecFinished(c->sec, c->handhash, c->finished.verify, c->finished.n, 0) < 0){
		tlsError(c, EInternalError, "can't set finished 0: %r");
		goto Err;
	}
	if(!msgRecv(c, &m)) {
		tlsError(c, EInternalError, "can't read server Finished: %r");
		goto Err;
	}
	if(m.tag != HFinished) {
		tlsError(c, EUnexpectedMessage, "expected a Finished msg from server");
		goto Err;
	}

	if(!finishedMatch(c, &m.u.finished)) {
		tlsError(c, EHandshakeFailure, "finished verification failed");
		goto Err;
	}
	msgClear(&m);

	if(fprint(c->ctl, "opened") < 0){
		if(trace)
			trace("unable to do final open: %r\n");
		goto Err;
	}
	tlsSecOk(c->sec);
	return c;

Err:
	free(epm);
	msgClear(&m);
	tlsConnectionFree(c);
	return 0;
}


//================= message functions ========================

static void
msgHash(TlsConnection *c, uchar *p, int n)
{
	md5(p, n, 0, &c->handhash.md5);
	sha1(p, n, 0, &c->handhash.sha1);
	if(c->version >= TLS12Version)
		sha2_256(p, n, 0, &c->handhash.sha2_256);
}

static int
msgSend(TlsConnection *c, Msg *m, int act)
{
	uchar *p; // sendp = start of new message;  p = write pointer
	int nn, n, i;

	if(c->sendp == nil)
		c->sendp = c->sendbuf;
	p = c->sendp;
	if(c->trace)
		c->trace("send %s", msgPrint((char*)p, (sizeof(c->sendbuf)) - (p - c->sendbuf), m));

	p[0] = m->tag;	// header - fill in size later
	p += 4;

	switch(m->tag) {
	default:
		tlsError(c, EInternalError, "can't encode a %d", m->tag);
		goto Err;
	case HClientHello:
		// version
		put16(p, m->u.clientHello.version);
		p += 2;

		// random
		memmove(p, m->u.clientHello.random, RandomSize);
		p += RandomSize;

		// sid
		n = m->u.clientHello.sid->len;
		assert(n < 256);
		p[0] = n;
		memmove(p+1, m->u.clientHello.sid->data, n);
		p += n+1;

		n = m->u.clientHello.ciphers->len;
		assert(n > 0 && n < 200);
		put16(p, n*2);
		p += 2;
		for(i=0; i<n; i++) {
			put16(p, m->u.clientHello.ciphers->data[i]);
			p += 2;
		}

		n = m->u.clientHello.compressors->len;
		assert(n > 0);
		p[0] = n;
		memmove(p+1, m->u.clientHello.compressors->data, n);
		p += n+1;

		if(m->u.clientHello.extensions == nil)
			break;
		n = m->u.clientHello.extensions->len;
		if(n == 0)
			break;
		put16(p, n);
		memmove(p+2, m->u.clientHello.extensions->data, n);
		p += n+2;
		break;
	case HServerHello:
		put16(p, m->u.serverHello.version);
		p += 2;

		// random
		memmove(p, m->u.serverHello.random, RandomSize);
		p += RandomSize;

		// sid
		n = m->u.serverHello.sid->len;
		assert(n < 256);
		p[0] = n;
		memmove(p+1, m->u.serverHello.sid->data, n);
		p += n+1;

		put16(p, m->u.serverHello.cipher);
		p += 2;
		p[0] = m->u.serverHello.compressor;
		p += 1;

		if(m->u.serverHello.extensions == nil)
			break;
		n = m->u.serverHello.extensions->len;
		if(n == 0)
			break;
		put16(p, n);
		memmove(p+2, m->u.serverHello.extensions->data, n);
		p += n+2;
		break;
	case HServerHelloDone:
		break;
	case HCertificate:
		nn = 0;
		for(i = 0; i < m->u.certificate.ncert; i++)
			nn += 3 + m->u.certificate.certs[i]->len;
		if(p + 3 + nn - c->sendbuf > sizeof(c->sendbuf)) {
			tlsError(c, EInternalError, "output buffer too small for certificate");
			goto Err;
		}
		put24(p, nn);
		p += 3;
		for(i = 0; i < m->u.certificate.ncert; i++){
			put24(p, m->u.certificate.certs[i]->len);
			p += 3;
			memmove(p, m->u.certificate.certs[i]->data, m->u.certificate.certs[i]->len);
			p += m->u.certificate.certs[i]->len;
		}
		break;
	case HCertificateVerify:
		if(m->u.certificateVerify.sigalg != 0){
			put16(p, m->u.certificateVerify.sigalg);
			p += 2;
		}
		put16(p, m->u.certificateVerify.signature->len);
		p += 2;
		memmove(p, m->u.certificateVerify.signature->data, m->u.certificateVerify.signature->len);
		p += m->u.certificateVerify.signature->len;
		break;
	case HClientKeyExchange:
		if(m->u.clientKeyExchange.pskid != nil){
			n = m->u.clientKeyExchange.pskid->len;
			put16(p, n);
			p += 2;
			memmove(p, m->u.clientKeyExchange.pskid->data, n);
			p += n;
		}
		if(m->u.clientKeyExchange.key == nil)
			break;
		n = m->u.clientKeyExchange.key->len;
		if(c->version != SSL3Version){
			if(isECDHE(c->cipher))
				*p++ = n;
			else
				put16(p, n), p += 2;
		}
		memmove(p, m->u.clientKeyExchange.key->data, n);
		p += n;
		break;
	case HFinished:
		memmove(p, m->u.finished.verify, m->u.finished.n);
		p += m->u.finished.n;
		break;
	}

	// go back and fill in size
	n = p - c->sendp;
	assert(p <= c->sendbuf + sizeof(c->sendbuf));
	put24(c->sendp+1, n-4);

	// remember hash of Handshake messages
	if(m->tag != HHelloRequest)
		msgHash(c, c->sendp, n);

	c->sendp = p;
	if(act == AFlush){
		c->sendp = c->sendbuf;
		if(write(c->hand, c->sendbuf, p - c->sendbuf) < 0){
			fprint(2, "write error: %r\n");
			goto Err;
		}
	}
	msgClear(m);
	return 1;
Err:
	msgClear(m);
	return 0;
}

static uchar*
tlsReadN(TlsConnection *c, int n)
{
	uchar *p;
	int nn, nr;

	nn = c->ep - c->rp;
	if(nn < n){
		if(c->rp != c->recvbuf){
			memmove(c->recvbuf, c->rp, nn);
			c->rp = c->recvbuf;
			c->ep = &c->recvbuf[nn];
		}
		for(; nn < n; nn += nr) {
			nr = read(c->hand, &c->rp[nn], n - nn);
			if(nr <= 0)
				return nil;
			c->ep += nr;
		}
	}
	p = c->rp;
	c->rp += n;
	return p;
}

static int
msgRecv(TlsConnection *c, Msg *m)
{
	uchar *p, *s;
	int type, n, nn, i, nsid, nrandom, nciph;

	for(;;) {
		p = tlsReadN(c, 4);
		if(p == nil)
			return 0;
		type = p[0];
		n = get24(p+1);

		if(type != HHelloRequest)
			break;
		if(n != 0) {
			tlsError(c, EDecodeError, "invalid hello request during handshake");
			return 0;
		}
	}

	if(n > sizeof(c->recvbuf)) {
		tlsError(c, EDecodeError, "handshake message too long %d %d", n, sizeof(c->recvbuf));
		return 0;
	}

	if(type == HSSL2ClientHello){
		/* Cope with an SSL3 ClientHello expressed in SSL2 record format.
			This is sent by some clients that we must interoperate
			with, such as Java's JSSE and Microsoft's Internet Explorer. */
		p = tlsReadN(c, n);
		if(p == nil)
			return 0;
		msgHash(c, p, n);
		m->tag = HClientHello;
		if(n < 22)
			goto Short;
		m->u.clientHello.version = get16(p+1);
		p += 3;
		n -= 3;
		nn = get16(p); /* cipher_spec_len */
		nsid = get16(p + 2);
		nrandom = get16(p + 4);
		p += 6;
		n -= 6;
		if(nsid != 0 	/* no sid's, since shouldn't restart using ssl2 header */
				|| nrandom < 16 || nn % 3)
			goto Err;
		if(c->trace && (n - nrandom != nn))
			c->trace("n-nrandom!=nn: n=%d nrandom=%d nn=%d\n", n, nrandom, nn);
		/* ignore ssl2 ciphers and look for {0x00, ssl3 cipher} */
		nciph = 0;
		for(i = 0; i < nn; i += 3)
			if(p[i] == 0)
				nciph++;
		m->u.clientHello.ciphers = newints(nciph);
		nciph = 0;
		for(i = 0; i < nn; i += 3)
			if(p[i] == 0)
				m->u.clientHello.ciphers->data[nciph++] = get16(&p[i + 1]);
		p += nn;
		m->u.clientHello.sid = makebytes(nil, 0);
		if(nrandom > RandomSize)
			nrandom = RandomSize;
		memset(m->u.clientHello.random, 0, RandomSize - nrandom);
		memmove(&m->u.clientHello.random[RandomSize - nrandom], p, nrandom);
		m->u.clientHello.compressors = newbytes(1);
		m->u.clientHello.compressors->data[0] = CompressionNull;
		goto Ok;
	}
	msgHash(c, p, 4);

	p = tlsReadN(c, n);
	if(p == nil)
		return 0;

	msgHash(c, p, n);

	m->tag = type;

	switch(type) {
	default:
		tlsError(c, EUnexpectedMessage, "can't decode a %d", type);
		goto Err;
	case HClientHello:
		if(n < 2)
			goto Short;
		m->u.clientHello.version = get16(p);
		p += 2;
		n -= 2;

		if(n < RandomSize)
			goto Short;
		memmove(m->u.clientHello.random, p, RandomSize);
		p += RandomSize;
		n -= RandomSize;
		if(n < 1 || n < p[0]+1)
			goto Short;
		m->u.clientHello.sid = makebytes(p+1, p[0]);
		p += m->u.clientHello.sid->len+1;
		n -= m->u.clientHello.sid->len+1;

		if(n < 2)
			goto Short;
		nn = get16(p);
		p += 2;
		n -= 2;

		if((nn & 1) || n < nn || nn < 2)
			goto Short;
		m->u.clientHello.ciphers = newints(nn >> 1);
		for(i = 0; i < nn; i += 2)
			m->u.clientHello.ciphers->data[i >> 1] = get16(&p[i]);
		p += nn;
		n -= nn;

		if(n < 1 || n < p[0]+1 || p[0] == 0)
			goto Short;
		nn = p[0];
		m->u.clientHello.compressors = makebytes(p+1, nn);
		p += nn + 1;
		n -= nn + 1;

		if(n < 2)
			break;
		nn = get16(p);
		if(nn > n-2)
			goto Short;
		m->u.clientHello.extensions = makebytes(p+2, nn);
		n -= nn + 2;
		break;
	case HServerHello:
		if(n < 2)
			goto Short;
		m->u.serverHello.version = get16(p);
		p += 2;
		n -= 2;

		if(n < RandomSize)
			goto Short;
		memmove(m->u.serverHello.random, p, RandomSize);
		p += RandomSize;
		n -= RandomSize;

		if(n < 1 || n < p[0]+1)
			goto Short;
		m->u.serverHello.sid = makebytes(p+1, p[0]);
		p += m->u.serverHello.sid->len+1;
		n -= m->u.serverHello.sid->len+1;

		if(n < 3)
			goto Short;
		m->u.serverHello.cipher = get16(p);
		m->u.serverHello.compressor = p[2];
		p += 3;
		n -= 3;

		if(n < 2)
			break;
		nn = get16(p);
		if(nn > n-2)
			goto Short;
		m->u.serverHello.extensions = makebytes(p+2, nn);
		n -= nn + 2;
		break;
	case HCertificate:
		if(n < 3)
			goto Short;
		nn = get24(p);
		p += 3;
		n -= 3;
		if(nn == 0 && n > 0)
			goto Short;
		/* certs */
		i = 0;
		while(n > 0) {
			if(n < 3)
				goto Short;
			nn = get24(p);
			p += 3;
			n -= 3;
			if(nn > n)
				goto Short;
			m->u.certificate.ncert = i+1;
			m->u.certificate.certs = erealloc(m->u.certificate.certs, (i+1)*sizeof(Bytes*));
			m->u.certificate.certs[i] = makebytes(p, nn);
			p += nn;
			n -= nn;
			i++;
		}
		break;
	case HCertificateRequest:
		if(n < 1)
			goto Short;
		nn = p[0];
		p += 1;
		n -= 1;
		if(nn > n)
			goto Short;
		m->u.certificateRequest.types = makebytes(p, nn);
		p += nn;
		n -= nn;
		if(c->version >= TLS12Version){
			if(n < 2)
				goto Short;
			nn = get16(p);
			p += 2;
			n -= 2;
			if(nn & 1)
				goto Short;
			m->u.certificateRequest.sigalgs = newints(nn>>1);
			for(i = 0; i < nn; i += 2)
				m->u.certificateRequest.sigalgs->data[i >> 1] = get16(&p[i]);
			p += nn;
			n -= nn;

		}
		if(n < 2)
			goto Short;
		nn = get16(p);
		p += 2;
		n -= 2;
		/* nn == 0 can happen; yahoo's servers do it */
		if(nn != n)
			goto Short;
		/* cas */
		i = 0;
		while(n > 0) {
			if(n < 2)
				goto Short;
			nn = get16(p);
			p += 2;
			n -= 2;
			if(nn < 1 || nn > n)
				goto Short;
			m->u.certificateRequest.nca = i+1;
			m->u.certificateRequest.cas = erealloc(
				m->u.certificateRequest.cas, (i+1)*sizeof(Bytes*));
			m->u.certificateRequest.cas[i] = makebytes(p, nn);
			p += nn;
			n -= nn;
			i++;
		}
		break;
	case HServerHelloDone:
		break;
	case HServerKeyExchange:
		if(isPSK(c->cipher)){
			if(n < 2)
				goto Short;
			nn = get16(p);
			p += 2, n -= 2;
			if(nn > n)
				goto Short;
			m->u.serverKeyExchange.pskid = makebytes(p, nn);
			p += nn, n -= nn;
			if(n == 0)
				break;
		}
		if(n < 2)
			goto Short;
		s = p;
		if(isECDHE(c->cipher)){
			nn = *p;
			p++, n--;
			if(nn != 3 || nn > n) /* not a named curve */
				goto Short;
			nn = get16(p);
			p += 2, n -= 2;
			m->u.serverKeyExchange.curve = nn;

			nn = *p++, n--;
			if(nn < 1 || nn > n)
				goto Short;
			m->u.serverKeyExchange.dh_Ys = makebytes(p, nn);
			p += nn, n -= nn;
		}else if(isDHE(c->cipher)){
			nn = get16(p);
			p += 2, n -= 2;
			if(nn < 1 || nn > n)
				goto Short;
			m->u.serverKeyExchange.dh_p = makebytes(p, nn);
			p += nn, n -= nn;
	
			if(n < 2)
				goto Short;
			nn = get16(p);
			p += 2, n -= 2;
			if(nn < 1 || nn > n)
				goto Short;
			m->u.serverKeyExchange.dh_g = makebytes(p, nn);
			p += nn, n -= nn;
	
			if(n < 2)
				goto Short;
			nn = get16(p);
			p += 2, n -= 2;
			if(nn < 1 || nn > n)
				goto Short;
			m->u.serverKeyExchange.dh_Ys = makebytes(p, nn);
			p += nn, n -= nn;
		} else {
			/* should not happen */
			goto Short;
		}
		m->u.serverKeyExchange.dh_parameters = makebytes(s, p - s);
		if(n >= 2){
			m->u.serverKeyExchange.sigalg = 0;
			if(c->version >= TLS12Version){
				m->u.serverKeyExchange.sigalg = get16(p);
				p += 2, n -= 2;
				if(n < 2)
					goto Short;
			}
			nn = get16(p);
			p += 2, n -= 2;
			if(nn > 0 && nn <= n){
				m->u.serverKeyExchange.dh_signature = makebytes(p, nn);
				n -= nn;
			}
		}
		break;		
	case HClientKeyExchange:
		/*
		 * this message depends upon the encryption selected
		 * assume rsa.
		 */
		if(isPSK(c->cipher)){
			if(n < 2)
				goto Short;
			nn = get16(p);
			p += 2, n -= 2;
			if(nn > n)
				goto Short;
			m->u.clientKeyExchange.pskid = makebytes(p, nn);
			p += nn, n -= nn;
			if(n == 0)
				break;
		}
		if(c->version == SSL3Version)
			nn = n;
		else{
			if(n < 2)
				goto Short;
			nn = get16(p);
			p += 2;
			n -= 2;
		}
		if(n < nn)
			goto Short;
		m->u.clientKeyExchange.key = makebytes(p, nn);
		n -= nn;
		break;
	case HFinished:
		m->u.finished.n = c->finished.n;
		if(n < m->u.finished.n)
			goto Short;
		memmove(m->u.finished.verify, p, m->u.finished.n);
		n -= m->u.finished.n;
		break;
	}

	if(type != HClientHello && type != HServerHello && n != 0)
		goto Short;
Ok:
	if(c->trace){
		char *buf;
		buf = emalloc(8000);
		c->trace("recv %s", msgPrint(buf, 8000, m));
		free(buf);
	}
	return 1;
Short:
	tlsError(c, EDecodeError, "handshake message (%d) has invalid length", type);
Err:
	msgClear(m);
	return 0;
}

static void
msgClear(Msg *m)
{
	int i;

	switch(m->tag) {
	default:
		sysfatal("msgClear: unknown message type: %d", m->tag);
	case HHelloRequest:
		break;
	case HClientHello:
		freebytes(m->u.clientHello.sid);
		freeints(m->u.clientHello.ciphers);
		freebytes(m->u.clientHello.compressors);
		freebytes(m->u.clientHello.extensions);
		break;
	case HServerHello:
		freebytes(m->u.serverHello.sid);
		freebytes(m->u.serverHello.extensions);
		break;
	case HCertificate:
		for(i=0; i<m->u.certificate.ncert; i++)
			freebytes(m->u.certificate.certs[i]);
		free(m->u.certificate.certs);
		break;
	case HCertificateRequest:
		freebytes(m->u.certificateRequest.types);
		freeints(m->u.certificateRequest.sigalgs);
		for(i=0; i<m->u.certificateRequest.nca; i++)
			freebytes(m->u.certificateRequest.cas[i]);
		free(m->u.certificateRequest.cas);
		break;
	case HCertificateVerify:
		freebytes(m->u.certificateVerify.signature);
		break;
	case HServerHelloDone:
		break;
	case HServerKeyExchange:
		freebytes(m->u.serverKeyExchange.pskid);
		freebytes(m->u.serverKeyExchange.dh_p);
		freebytes(m->u.serverKeyExchange.dh_g);
		freebytes(m->u.serverKeyExchange.dh_Ys);
		freebytes(m->u.serverKeyExchange.dh_parameters);
		freebytes(m->u.serverKeyExchange.dh_signature);
		break;
	case HClientKeyExchange:
		freebytes(m->u.clientKeyExchange.pskid);
		freebytes(m->u.clientKeyExchange.key);
		break;
	case HFinished:
		break;
	}
	memset(m, 0, sizeof(Msg));
}

static char *
bytesPrint(char *bs, char *be, char *s0, Bytes *b, char *s1)
{
	int i;

	if(s0)
		bs = seprint(bs, be, "%s", s0);
	if(b == nil)
		bs = seprint(bs, be, "nil");
	else {
		bs = seprint(bs, be, "<%d> [", b->len);
		for(i=0; i<b->len; i++)
			bs = seprint(bs, be, "%.2x ", b->data[i]);
	}
	bs = seprint(bs, be, "]");
	if(s1)
		bs = seprint(bs, be, "%s", s1);
	return bs;
}

static char *
intsPrint(char *bs, char *be, char *s0, Ints *b, char *s1)
{
	int i;

	if(s0)
		bs = seprint(bs, be, "%s", s0);
	bs = seprint(bs, be, "[");
	if(b == nil)
		bs = seprint(bs, be, "nil");
	else
		for(i=0; i<b->len; i++)
			bs = seprint(bs, be, "%x ", b->data[i]);
	bs = seprint(bs, be, "]");
	if(s1)
		bs = seprint(bs, be, "%s", s1);
	return bs;
}

static char*
msgPrint(char *buf, int n, Msg *m)
{
	int i;
	char *bs = buf, *be = buf+n;

	switch(m->tag) {
	default:
		bs = seprint(bs, be, "unknown %d\n", m->tag);
		break;
	case HClientHello:
		bs = seprint(bs, be, "ClientHello\n");
		bs = seprint(bs, be, "\tversion: %.4x\n", m->u.clientHello.version);
		bs = seprint(bs, be, "\trandom: ");
		for(i=0; i<RandomSize; i++)
			bs = seprint(bs, be, "%.2x", m->u.clientHello.random[i]);
		bs = seprint(bs, be, "\n");
		bs = bytesPrint(bs, be, "\tsid: ", m->u.clientHello.sid, "\n");
		bs = intsPrint(bs, be, "\tciphers: ", m->u.clientHello.ciphers, "\n");
		bs = bytesPrint(bs, be, "\tcompressors: ", m->u.clientHello.compressors, "\n");
		if(m->u.clientHello.extensions != nil)
			bs = bytesPrint(bs, be, "\textensions: ", m->u.clientHello.extensions, "\n");
		break;
	case HServerHello:
		bs = seprint(bs, be, "ServerHello\n");
		bs = seprint(bs, be, "\tversion: %.4x\n", m->u.serverHello.version);
		bs = seprint(bs, be, "\trandom: ");
		for(i=0; i<RandomSize; i++)
			bs = seprint(bs, be, "%.2x", m->u.serverHello.random[i]);
		bs = seprint(bs, be, "\n");
		bs = bytesPrint(bs, be, "\tsid: ", m->u.serverHello.sid, "\n");
		bs = seprint(bs, be, "\tcipher: %.4x\n", m->u.serverHello.cipher);
		bs = seprint(bs, be, "\tcompressor: %.2x\n", m->u.serverHello.compressor);
		if(m->u.serverHello.extensions != nil)
			bs = bytesPrint(bs, be, "\textensions: ", m->u.serverHello.extensions, "\n");
		break;
	case HCertificate:
		bs = seprint(bs, be, "Certificate\n");
		for(i=0; i<m->u.certificate.ncert; i++)
			bs = bytesPrint(bs, be, "\t", m->u.certificate.certs[i], "\n");
		break;
	case HCertificateRequest:
		bs = seprint(bs, be, "CertificateRequest\n");
		bs = bytesPrint(bs, be, "\ttypes: ", m->u.certificateRequest.types, "\n");
		if(m->u.certificateRequest.sigalgs != nil)
			bs = intsPrint(bs, be, "\tsigalgs: ", m->u.certificateRequest.sigalgs, "\n");
		bs = seprint(bs, be, "\tcertificateauthorities\n");
		for(i=0; i<m->u.certificateRequest.nca; i++)
			bs = bytesPrint(bs, be, "\t\t", m->u.certificateRequest.cas[i], "\n");
		break;
	case HCertificateVerify:
		bs = seprint(bs, be, "HCertificateVerify\n");
		if(m->u.certificateVerify.sigalg != 0)
			bs = seprint(bs, be, "\tsigalg: %.4x\n", m->u.certificateVerify.sigalg);
		bs = bytesPrint(bs, be, "\tsignature: ", m->u.certificateVerify.signature,"\n");
		break;	
	case HServerHelloDone:
		bs = seprint(bs, be, "ServerHelloDone\n");
		break;
	case HServerKeyExchange:
		bs = seprint(bs, be, "HServerKeyExchange\n");
		if(m->u.serverKeyExchange.pskid != nil)
			bs = bytesPrint(bs, be, "\tpskid: ", m->u.serverKeyExchange.pskid, "\n");
		if(m->u.serverKeyExchange.dh_parameters == nil)
			break;
		if(m->u.serverKeyExchange.curve != 0){
			bs = seprint(bs, be, "\tcurve: %.4x\n", m->u.serverKeyExchange.curve);
		} else {
			bs = bytesPrint(bs, be, "\tdh_p: ", m->u.serverKeyExchange.dh_p, "\n");
			bs = bytesPrint(bs, be, "\tdh_g: ", m->u.serverKeyExchange.dh_g, "\n");
		}
		bs = bytesPrint(bs, be, "\tdh_Ys: ", m->u.serverKeyExchange.dh_Ys, "\n");
		if(m->u.serverKeyExchange.sigalg != 0)
			bs = seprint(bs, be, "\tsigalg: %.4x\n", m->u.serverKeyExchange.sigalg);
		bs = bytesPrint(bs, be, "\tdh_parameters: ", m->u.serverKeyExchange.dh_parameters, "\n");
		bs = bytesPrint(bs, be, "\tdh_signature: ", m->u.serverKeyExchange.dh_signature, "\n");
		break;
	case HClientKeyExchange:
		bs = seprint(bs, be, "HClientKeyExchange\n");
		if(m->u.clientKeyExchange.pskid != nil)
			bs = bytesPrint(bs, be, "\tpskid: ", m->u.clientKeyExchange.pskid, "\n");
		if(m->u.clientKeyExchange.key != nil)
			bs = bytesPrint(bs, be, "\tkey: ", m->u.clientKeyExchange.key, "\n");
		break;
	case HFinished:
		bs = seprint(bs, be, "HFinished\n");
		for(i=0; i<m->u.finished.n; i++)
			bs = seprint(bs, be, "%.2x", m->u.finished.verify[i]);
		bs = seprint(bs, be, "\n");
		break;
	}
	USED(bs);
	return buf;
}

static void
tlsError(TlsConnection *c, int err, char *fmt, ...)
{
	char msg[512];
	va_list arg;

	va_start(arg, fmt);
	vseprint(msg, msg+sizeof(msg), fmt, arg);
	va_end(arg);
	if(c->trace)
		c->trace("tlsError: %s\n", msg);
	else if(c->erred)
		fprint(2, "double error: %r, %s", msg);
	else
		werrstr("tls: local %s", msg);
	c->erred = 1;
	fprint(c->ctl, "alert %d", err);
}

// commit to specific version number
static int
setVersion(TlsConnection *c, int version)
{
	if(c->verset || version > MaxProtoVersion || version < MinProtoVersion)
		return -1;
	if(version > c->version)
		version = c->version;
	if(version == SSL3Version) {
		c->version = version;
		c->finished.n = SSL3FinishedLen;
	}else {
		c->version = version;
		c->finished.n = TLSFinishedLen;
	}
	c->verset = 1;
	return fprint(c->ctl, "version 0x%x", version);
}

// confirm that received Finished message matches the expected value
static int
finishedMatch(TlsConnection *c, Finished *f)
{
	return tsmemcmp(f->verify, c->finished.verify, f->n) == 0;
}

// free memory associated with TlsConnection struct
//		(but don't close the TLS channel itself)
static void
tlsConnectionFree(TlsConnection *c)
{
	tlsSecClose(c->sec);
	freebytes(c->sid);
	freebytes(c->cert);
	memset(c, 0, sizeof(c));
	free(c);
}


//================= cipher choices ========================

static char weakCipher[] =
{
[TLS_NULL_WITH_NULL_NULL]		1,
[TLS_RSA_WITH_NULL_MD5]			1,
[TLS_RSA_WITH_NULL_SHA]			1,
[TLS_RSA_EXPORT_WITH_RC4_40_MD5]	1,
[TLS_RSA_WITH_RC4_128_MD5]		1,
[TLS_RSA_WITH_RC4_128_SHA]		1,
[TLS_RSA_EXPORT_WITH_RC2_CBC_40_MD5]	1,
[TLS_RSA_WITH_IDEA_CBC_SHA]		0,
[TLS_RSA_EXPORT_WITH_DES40_CBC_SHA]	1,
[TLS_RSA_WITH_DES_CBC_SHA]		0,
[TLS_RSA_WITH_3DES_EDE_CBC_SHA]		0,
[TLS_DH_DSS_EXPORT_WITH_DES40_CBC_SHA]	1,
[TLS_DH_DSS_WITH_DES_CBC_SHA]		0,
[TLS_DH_DSS_WITH_3DES_EDE_CBC_SHA]	0,
[TLS_DH_RSA_EXPORT_WITH_DES40_CBC_SHA]	1,
[TLS_DH_RSA_WITH_DES_CBC_SHA]		0,
[TLS_DH_RSA_WITH_3DES_EDE_CBC_SHA]	0,
[TLS_DHE_DSS_EXPORT_WITH_DES40_CBC_SHA]	1,
[TLS_DHE_DSS_WITH_DES_CBC_SHA]		0,
[TLS_DHE_DSS_WITH_3DES_EDE_CBC_SHA]	0,
[TLS_DHE_RSA_EXPORT_WITH_DES40_CBC_SHA]	1,
[TLS_DHE_RSA_WITH_DES_CBC_SHA]		0,
[TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA]	0,
[TLS_DH_anon_EXPORT_WITH_RC4_40_MD5]	1,
[TLS_DH_anon_WITH_RC4_128_MD5]		1,
[TLS_DH_anon_EXPORT_WITH_DES40_CBC_SHA]	1,
[TLS_DH_anon_WITH_DES_CBC_SHA]		1,
[TLS_DH_anon_WITH_3DES_EDE_CBC_SHA]	1,
};

static int
setAlgs(TlsConnection *c, int a)
{
	int i;

	for(i = 0; i < nelem(cipherAlgs); i++){
		if(cipherAlgs[i].tlsid == a){
			c->cipher = a;
			c->enc = cipherAlgs[i].enc;
			c->digest = cipherAlgs[i].digest;
			c->nsecret = cipherAlgs[i].nsecret;
			if(c->nsecret > MaxKeyData)
				return 0;
			return 1;
		}
	}
	return 0;
}

static int
okCipher(Ints *cv, int ispsk)
{
	int weak, i, j, c;

	weak = 1;
	for(i = 0; i < cv->len; i++) {
		c = cv->data[i];
		if(c >= nelem(weakCipher))
			weak = 0;
		else
			weak &= weakCipher[c];
		if(isPSK(c) != ispsk)
			continue;
		if(isDHE(c) || isECDHE(c))
			continue;	/* TODO: not implemented for server */
		for(j = 0; j < nelem(cipherAlgs); j++)
			if(cipherAlgs[j].ok && cipherAlgs[j].tlsid == c)
				return c;
	}
	if(weak)
		return -2;
	return -1;
}

static int
okCompression(Bytes *cv)
{
	int i, j, c;

	for(i = 0; i < cv->len; i++) {
		c = cv->data[i];
		for(j = 0; j < nelem(compressors); j++) {
			if(compressors[j] == c)
				return c;
		}
	}
	return -1;
}

static Lock	ciphLock;
static int	nciphers;

static int
initCiphers(void)
{
	enum {MaxAlgF = 1024, MaxAlgs = 10};
	char s[MaxAlgF], *flds[MaxAlgs];
	int i, j, n, ok;

	lock(&ciphLock);
	if(nciphers){
		unlock(&ciphLock);
		return nciphers;
	}
	j = open("#a/tls/encalgs", OREAD);
	if(j < 0){
		werrstr("can't open #a/tls/encalgs: %r");
		goto out;
	}
	n = read(j, s, MaxAlgF-1);
	close(j);
	if(n <= 0){
		werrstr("nothing in #a/tls/encalgs: %r");
		goto out;
	}
	s[n] = 0;
	n = getfields(s, flds, MaxAlgs, 1, " \t\r\n");
	for(i = 0; i < nelem(cipherAlgs); i++){
		ok = 0;
		for(j = 0; j < n; j++){
			if(strcmp(cipherAlgs[i].enc, flds[j]) == 0){
				ok = 1;
				break;
			}
		}
		cipherAlgs[i].ok = ok;
	}

	j = open("#a/tls/hashalgs", OREAD);
	if(j < 0){
		werrstr("can't open #a/tls/hashalgs: %r");
		goto out;
	}
	n = read(j, s, MaxAlgF-1);
	close(j);
	if(n <= 0){
		werrstr("nothing in #a/tls/hashalgs: %r");
		goto out;
	}
	s[n] = 0;
	n = getfields(s, flds, MaxAlgs, 1, " \t\r\n");
	for(i = 0; i < nelem(cipherAlgs); i++){
		ok = 0;
		for(j = 0; j < n; j++){
			if(strcmp(cipherAlgs[i].digest, flds[j]) == 0){
				ok = 1;
				break;
			}
		}
		cipherAlgs[i].ok &= ok;
		if(cipherAlgs[i].ok)
			nciphers++;
	}
out:
	unlock(&ciphLock);
	return nciphers;
}

static Ints*
makeciphers(int ispsk)
{
	Ints *is;
	int i, j;

	is = newints(nciphers);
	j = 0;
	for(i = 0; i < nelem(cipherAlgs); i++)
		if(cipherAlgs[i].ok && isPSK(cipherAlgs[i].tlsid) == ispsk)
			is->data[j++] = cipherAlgs[i].tlsid;
	is->len = j;
	return is;
}



//================= security functions ========================

// given X.509 certificate, set up connection to factotum
//	for using corresponding private key
static AuthRpc*
factotum_rsa_open(uchar *cert, int certlen)
{
	int afd;
	char *s;
	mpint *pub = nil;
	RSApub *rsapub;
	AuthRpc *rpc;

	// start talking to factotum
	if((afd = open("/mnt/factotum/rpc", ORDWR)) < 0)
		return nil;
	if((rpc = auth_allocrpc(afd)) == nil){
		close(afd);
		return nil;
	}
	s = "proto=rsa service=tls role=client";
	if(auth_rpc(rpc, "start", s, strlen(s)) != ARok){
		factotum_rsa_close(rpc);
		return nil;
	}

	// roll factotum keyring around to match certificate
	rsapub = X509toRSApub(cert, certlen, nil, 0);
	while(1){
		if(auth_rpc(rpc, "read", nil, 0) != ARok){
			factotum_rsa_close(rpc);
			rpc = nil;
			goto done;
		}
		pub = strtomp(rpc->arg, nil, 16, nil);
		assert(pub != nil);
		if(mpcmp(pub,rsapub->n) == 0)
			break;
	}
done:
	mpfree(pub);
	rsapubfree(rsapub);
	return rpc;
}

static mpint*
factotum_rsa_decrypt(AuthRpc *rpc, mpint *cipher)
{
	char *p;
	int rv;

	p = mptoa(cipher, 16, nil, 0);
	mpfree(cipher);
	if(p == nil)
		return nil;
	rv = auth_rpc(rpc, "write", p, strlen(p));
	free(p);
	if(rv != ARok || auth_rpc(rpc, "read", nil, 0) != ARok)
		return nil;
	return strtomp(rpc->arg, nil, 16, nil);
}

static void
factotum_rsa_close(AuthRpc *rpc)
{
	if(rpc == nil)
		return;
	close(rpc->afd);
	auth_freerpc(rpc);
}

static void
tlsPmd5(uchar *buf, int nbuf, uchar *key, int nkey, uchar *label, int nlabel, uchar *seed0, int nseed0, uchar *seed1, int nseed1)
{
	uchar ai[MD5dlen], tmp[MD5dlen];
	int i, n;
	MD5state *s;

	// generate a1
	s = hmac_md5(label, nlabel, key, nkey, nil, nil);
	s = hmac_md5(seed0, nseed0, key, nkey, nil, s);
	hmac_md5(seed1, nseed1, key, nkey, ai, s);

	while(nbuf > 0) {
		s = hmac_md5(ai, MD5dlen, key, nkey, nil, nil);
		s = hmac_md5(label, nlabel, key, nkey, nil, s);
		s = hmac_md5(seed0, nseed0, key, nkey, nil, s);
		hmac_md5(seed1, nseed1, key, nkey, tmp, s);
		n = MD5dlen;
		if(n > nbuf)
			n = nbuf;
		for(i = 0; i < n; i++)
			buf[i] ^= tmp[i];
		buf += n;
		nbuf -= n;
		hmac_md5(ai, MD5dlen, key, nkey, tmp, nil);
		memmove(ai, tmp, MD5dlen);
	}
}

static void
tlsPsha1(uchar *buf, int nbuf, uchar *key, int nkey, uchar *label, int nlabel, uchar *seed0, int nseed0, uchar *seed1, int nseed1)
{
	uchar ai[SHA1dlen], tmp[SHA1dlen];
	int i, n;
	SHAstate *s;

	// generate a1
	s = hmac_sha1(label, nlabel, key, nkey, nil, nil);
	s = hmac_sha1(seed0, nseed0, key, nkey, nil, s);
	hmac_sha1(seed1, nseed1, key, nkey, ai, s);

	while(nbuf > 0) {
		s = hmac_sha1(ai, SHA1dlen, key, nkey, nil, nil);
		s = hmac_sha1(label, nlabel, key, nkey, nil, s);
		s = hmac_sha1(seed0, nseed0, key, nkey, nil, s);
		hmac_sha1(seed1, nseed1, key, nkey, tmp, s);
		n = SHA1dlen;
		if(n > nbuf)
			n = nbuf;
		for(i = 0; i < n; i++)
			buf[i] ^= tmp[i];
		buf += n;
		nbuf -= n;
		hmac_sha1(ai, SHA1dlen, key, nkey, tmp, nil);
		memmove(ai, tmp, SHA1dlen);
	}
}

static void
p_sha256(uchar *buf, int nbuf, uchar *key, int nkey, uchar *label, int nlabel, uchar *seed, int nseed)
{
	uchar ai[SHA2_256dlen], tmp[SHA2_256dlen];
	SHAstate *s;
	int n;

	// generate a1
	s = hmac_sha2_256(label, nlabel, key, nkey, nil, nil);
	hmac_sha2_256(seed, nseed, key, nkey, ai, s);

	while(nbuf > 0) {
		s = hmac_sha2_256(ai, SHA2_256dlen, key, nkey, nil, nil);
		s = hmac_sha2_256(label, nlabel, key, nkey, nil, s);
		hmac_sha2_256(seed, nseed, key, nkey, tmp, s);
		n = SHA2_256dlen;
		if(n > nbuf)
			n = nbuf;
		memmove(buf, tmp, n);
		buf += n;
		nbuf -= n;
		hmac_sha2_256(ai, SHA2_256dlen, key, nkey, tmp, nil);
		memmove(ai, tmp, SHA2_256dlen);
	}
}

// fill buf with md5(args)^sha1(args)
static void
tls10PRF(uchar *buf, int nbuf, uchar *key, int nkey, char *label, uchar *seed0, int nseed0, uchar *seed1, int nseed1)
{
	int nlabel = strlen(label);
	int n = (nkey + 1) >> 1;

	memset(buf, 0, nbuf);
	tlsPmd5(buf, nbuf, key, n, (uchar*)label, nlabel, seed0, nseed0, seed1, nseed1);
	tlsPsha1(buf, nbuf, key+nkey-n, n, (uchar*)label, nlabel, seed0, nseed0, seed1, nseed1);
}

static void
tls12PRF(uchar *buf, int nbuf, uchar *key, int nkey, char *label, uchar *seed0, int nseed0, uchar *seed1, int nseed1)
{
	uchar seed[2*RandomSize];

	assert(nseed0+nseed1 <= sizeof(seed));
	memmove(seed, seed0, nseed0);
	memmove(seed+nseed0, seed1, nseed1);
	p_sha256(buf, nbuf, key, nkey, (uchar*)label, strlen(label), seed, nseed0+nseed1);
}

/*
 * for setting server session id's
 */
static Lock	sidLock;
static long	maxSid = 1;

/* the keys are verified to have the same public components
 * and to function correctly with pkcs 1 encryption and decryption. */
static TlsSec*
tlsSecInits(int cvers, uchar *csid, int ncsid, uchar *crandom, uchar *ssid, int *nssid, uchar *srandom)
{
	TlsSec *sec = emalloc(sizeof(*sec));

	USED(csid); USED(ncsid);  // ignore csid for now

	memmove(sec->crandom, crandom, RandomSize);
	sec->clientVers = cvers;

	put32(sec->srandom, time(0));
	genrandom(sec->srandom+4, RandomSize-4);
	memmove(srandom, sec->srandom, RandomSize);

	/*
	 * make up a unique sid: use our pid, and and incrementing id
	 * can signal no sid by setting nssid to 0.
	 */
	memset(ssid, 0, SidSize);
	put32(ssid, getpid());
	lock(&sidLock);
	put32(ssid+4, maxSid++);
	unlock(&sidLock);
	*nssid = SidSize;
	return sec;
}

static int
tlsSecRSAs(TlsSec *sec, int vers, Bytes *epm)
{
	Bytes *pm;

	if(setVers(sec, vers) < 0)
		goto Err;
	if(epm == nil){
		werrstr("no encrypted premaster secret");
		goto Err;
	}
	// if the client messed up, just continue as if everything is ok,
	// to prevent attacks to check for correctly formatted messages.
	// Hence the fprint(2,) can't be replaced by tlsError(), which sends an Alert msg to the client.
	pm = pkcs1_decrypt(sec, epm);
	if(sec->ok < 0 || pm == nil || pm->len != MasterSecretSize || get16(pm->data) != sec->clientVers){
		fprint(2, "tlsSecRSAs failed ok=%d pm=%p pmvers=%x cvers=%x nepm=%d\n",
			sec->ok, pm, pm != nil ? get16(pm->data) : -1, sec->clientVers, epm->len);
		sec->ok = -1;
		freebytes(pm);
		pm = newbytes(MasterSecretSize);
		genrandom(pm->data, MasterSecretSize);
	}
	setMasterSecret(sec, pm);
	return 0;
Err:
	sec->ok = -1;
	return -1;
}

static int
tlsSecPSKs(TlsSec *sec, int vers)
{
	if(setVers(sec, vers) < 0){
		sec->ok = -1;
		return -1;
	}
	setMasterSecret(sec, newbytes(sec->psklen));
	return 0;
}

static TlsSec*
tlsSecInitc(int cvers, uchar *crandom)
{
	TlsSec *sec = emalloc(sizeof(*sec));
	sec->clientVers = cvers;
	put32(sec->crandom, time(0));
	genrandom(sec->crandom+4, RandomSize-4);
	memmove(crandom, sec->crandom, RandomSize);
	return sec;
}

static int
tlsSecPSKc(TlsSec *sec, uchar *srandom, int vers)
{
	memmove(sec->srandom, srandom, RandomSize);
	if(setVers(sec, vers) < 0){
		sec->ok = -1;
		return -1;
	}
	setMasterSecret(sec, newbytes(sec->psklen));
	return 0;
}

static Bytes*
tlsSecRSAc(TlsSec *sec, uchar *sid, int nsid, uchar *srandom, uchar *cert, int ncert, int vers)
{
	RSApub *pub;
	Bytes *pm, *epm;

	USED(sid);
	USED(nsid);
	
	memmove(sec->srandom, srandom, RandomSize);
	if(setVers(sec, vers) < 0)
		goto Err;
	pub = X509toRSApub(cert, ncert, nil, 0);
	if(pub == nil){
		werrstr("invalid x509/rsa certificate");
		goto Err;
	}
	pm = newbytes(MasterSecretSize);
	put16(pm->data, sec->clientVers);
	genrandom(pm->data+2, MasterSecretSize - 2);
	epm = pkcs1_encrypt(pm, pub, 2);
	setMasterSecret(sec, pm);
	rsapubfree(pub);
	if(epm != nil)
		return epm;
Err:
	sec->ok = -1;
	return nil;
}

static int
tlsSecFinished(TlsSec *sec, HandshakeHash hsh, uchar *fin, int nfin, int isclient)
{
	if(sec->nfin != nfin){
		sec->ok = -1;
		werrstr("invalid finished exchange");
		return -1;
	}
	hsh.md5.malloced = 0;
	hsh.sha1.malloced = 0;
	hsh.sha2_256.malloced = 0;
	(*sec->setFinished)(sec, hsh, fin, isclient);
	return 1;
}

static void
tlsSecOk(TlsSec *sec)
{
	if(sec->ok == 0)
		sec->ok = 1;
}

static void
tlsSecKill(TlsSec *sec)
{
	if(!sec)
		return;
	factotum_rsa_close(sec->rpc);
	sec->ok = -1;
}

static void
tlsSecClose(TlsSec *sec)
{
	if(sec == nil)
		return;
	factotum_rsa_close(sec->rpc);
	free(sec->server);
	free(sec);
}

static int
setVers(TlsSec *sec, int v)
{
	if(v == SSL3Version){
		sec->setFinished = sslSetFinished;
		sec->nfin = SSL3FinishedLen;
		sec->prf = sslPRF;
	}else if(v < TLS12Version) {
		sec->setFinished = tls10SetFinished;
		sec->nfin = TLSFinishedLen;
		sec->prf = tls10PRF;
	}else {
		sec->setFinished = tls12SetFinished;
		sec->nfin = TLSFinishedLen;
		sec->prf = tls12PRF;
	}
	sec->vers = v;
	return 0;
}

/*
 * generate secret keys from the master secret.
 *
 * different crypto selections will require different amounts
 * of key expansion and use of key expansion data,
 * but it's all generated using the same function.
 */
static void
setSecrets(TlsSec *sec, uchar *kd, int nkd)
{
	(*sec->prf)(kd, nkd, sec->sec, MasterSecretSize, "key expansion",
			sec->srandom, RandomSize, sec->crandom, RandomSize);
}

/*
 * set the master secret from the pre-master secret,
 * destroys premaster.
 */
static void
setMasterSecret(TlsSec *sec, Bytes *pm)
{
	if(sec->psklen > 0){
		Bytes *opm = pm;
		uchar *p;

		/* concatenate psk to pre-master secret */
		pm = newbytes(4 + opm->len + sec->psklen);
		p = pm->data;
		put16(p, opm->len), p += 2;
		memmove(p, opm->data, opm->len), p += opm->len;
		put16(p, sec->psklen), p += 2;
		memmove(p, sec->psk, sec->psklen);

		memset(opm->data, 0, opm->len);
		freebytes(opm);
	}

	(*sec->prf)(sec->sec, MasterSecretSize, pm->data, pm->len, "master secret",
			sec->crandom, RandomSize, sec->srandom, RandomSize);

	memset(pm->data, 0, pm->len);	
	freebytes(pm);
}

static void
sslSetFinished(TlsSec *sec, HandshakeHash hsh, uchar *finished, int isClient)
{
	DigestState *s;
	uchar h0[MD5dlen], h1[SHA1dlen], pad[48];
	char *label;

	if(isClient)
		label = "CLNT";
	else
		label = "SRVR";

	md5((uchar*)label, 4, nil, &hsh.md5);
	md5(sec->sec, MasterSecretSize, nil, &hsh.md5);
	memset(pad, 0x36, 48);
	md5(pad, 48, nil, &hsh.md5);
	md5(nil, 0, h0, &hsh.md5);
	memset(pad, 0x5C, 48);
	s = md5(sec->sec, MasterSecretSize, nil, nil);
	s = md5(pad, 48, nil, s);
	md5(h0, MD5dlen, finished, s);

	sha1((uchar*)label, 4, nil, &hsh.sha1);
	sha1(sec->sec, MasterSecretSize, nil, &hsh.sha1);
	memset(pad, 0x36, 40);
	sha1(pad, 40, nil, &hsh.sha1);
	sha1(nil, 0, h1, &hsh.sha1);
	memset(pad, 0x5C, 40);
	s = sha1(sec->sec, MasterSecretSize, nil, nil);
	s = sha1(pad, 40, nil, s);
	sha1(h1, SHA1dlen, finished + MD5dlen, s);
}

// fill "finished" arg with md5(args)^sha1(args)
static void
tls10SetFinished(TlsSec *sec, HandshakeHash hsh, uchar *finished, int isClient)
{
	uchar h0[MD5dlen], h1[SHA1dlen];
	char *label;

	// get current hash value, but allow further messages to be hashed in
	md5(nil, 0, h0, &hsh.md5);
	sha1(nil, 0, h1, &hsh.sha1);

	if(isClient)
		label = "client finished";
	else
		label = "server finished";
	tls10PRF(finished, TLSFinishedLen, sec->sec, MasterSecretSize, label, h0, MD5dlen, h1, SHA1dlen);
}

static void
tls12SetFinished(TlsSec *sec, HandshakeHash hsh, uchar *finished, int isClient)
{
	uchar seed[SHA2_256dlen];
	char *label;

	// get current hash value, but allow further messages to be hashed in
	sha2_256(nil, 0, seed, &hsh.sha2_256);

	if(isClient)
		label = "client finished";
	else
		label = "server finished";
	p_sha256(finished, TLSFinishedLen, sec->sec, MasterSecretSize, (uchar*)label, strlen(label), seed, SHA2_256dlen);
}

static void
sslPRF(uchar *buf, int nbuf, uchar *key, int nkey, char *label, uchar *seed0, int nseed0, uchar *seed1, int nseed1)
{
	uchar sha1dig[SHA1dlen], md5dig[MD5dlen], tmp[26];
	DigestState *s;
	int i, n, len;

	USED(label);
	len = 1;
	while(nbuf > 0){
		if(len > 26)
			return;
		for(i = 0; i < len; i++)
			tmp[i] = 'A' - 1 + len;
		s = sha1(tmp, len, nil, nil);
		s = sha1(key, nkey, nil, s);
		s = sha1(seed0, nseed0, nil, s);
		sha1(seed1, nseed1, sha1dig, s);
		s = md5(key, nkey, nil, nil);
		md5(sha1dig, SHA1dlen, md5dig, s);
		n = MD5dlen;
		if(n > nbuf)
			n = nbuf;
		memmove(buf, md5dig, n);
		buf += n;
		nbuf -= n;
		len++;
	}
}

static mpint*
bytestomp(Bytes* bytes)
{
	return betomp(bytes->data, bytes->len, nil);
}

/*
 * Convert mpint* to Bytes, putting high order byte first.
 */
static Bytes*
mptobytes(mpint* big)
{
	Bytes* ans;
	int n;

	n = (mpsignif(big)+7)/8;
	if(n == 0) n = 1;
	ans = newbytes(n);
	mptober(big, ans->data, ans->len);
	return ans;
}

// Do RSA computation on block according to key, and pad
// result on left with zeros to make it modlen long.
static Bytes*
rsacomp(Bytes* block, RSApub* key, int modlen)
{
	mpint *x, *y;
	Bytes *a, *ybytes;
	int ylen;

	x = bytestomp(block);
	y = rsaencrypt(key, x, nil);
	mpfree(x);
	ybytes = mptobytes(y);
	ylen = ybytes->len;
	mpfree(y);

	if(ylen < modlen) {
		a = newbytes(modlen);
		memset(a->data, 0, modlen-ylen);
		memmove(a->data+modlen-ylen, ybytes->data, ylen);
		freebytes(ybytes);
		ybytes = a;
	}
	else if(ylen > modlen) {
		// assume it has leading zeros (mod should make it so)
		a = newbytes(modlen);
		memmove(a->data, ybytes->data, modlen);
		freebytes(ybytes);
		ybytes = a;
	}
	return ybytes;
}

// encrypt data according to PKCS#1, /lib/rfc/rfc2437 9.1.2.1
static Bytes*
pkcs1_encrypt(Bytes* data, RSApub* key, int blocktype)
{
	Bytes *pad, *eb, *ans;
	int i, dlen, padlen, modlen;

	modlen = (mpsignif(key->n)+7)/8;
	dlen = data->len;
	if(modlen < 12 || dlen > modlen - 11)
		return nil;
	padlen = modlen - 3 - dlen;
	pad = newbytes(padlen);
	genrandom(pad->data, padlen);
	for(i = 0; i < padlen; i++) {
		if(blocktype == 0)
			pad->data[i] = 0;
		else if(blocktype == 1)
			pad->data[i] = 255;
		else if(pad->data[i] == 0)
			pad->data[i] = 1;
	}
	eb = newbytes(modlen);
	eb->data[0] = 0;
	eb->data[1] = blocktype;
	memmove(eb->data+2, pad->data, padlen);
	eb->data[padlen+2] = 0;
	memmove(eb->data+padlen+3, data->data, dlen);
	ans = rsacomp(eb, key, modlen);
	freebytes(eb);
	freebytes(pad);
	return ans;
}

// decrypt data according to PKCS#1, with given key.
// expect a block type of 2.
static Bytes*
pkcs1_decrypt(TlsSec *sec, Bytes *cipher)
{
	Bytes *eb;
	int i, modlen;
	mpint *x, *y;

	modlen = (mpsignif(sec->rsapub->n)+7)/8;
	if(cipher->len != modlen)
		return nil;
	x = bytestomp(cipher);
	y = factotum_rsa_decrypt(sec->rpc, x);
	if(y == nil)
		return nil;
	eb = newbytes(modlen);
	mptober(y, eb->data, eb->len);
	mpfree(y);
	if(eb->data[0] == 0 && eb->data[1] == 2) {
		for(i = 2; i < eb->len; i++)
			if(eb->data[i] == 0)
				break;
		if(i < eb->len - 1){
			eb->len -= i+1;
			memmove(eb->data, eb->data+i+1, eb->len);
			return eb;
		}
	}
	freebytes(eb);
	return nil;
}


//================= general utility functions ========================

static void *
emalloc(int n)
{
	void *p;
	if(n==0)
		n=1;
	p = malloc(n);
	if(p == nil)
		sysfatal("out of memory");
	memset(p, 0, n);
	setmalloctag(p, getcallerpc(&n));
	return p;
}

static void *
erealloc(void *ReallocP, int ReallocN)
{
	if(ReallocN == 0)
		ReallocN = 1;
	if(ReallocP == nil)
		ReallocP = emalloc(ReallocN);
	else if((ReallocP = realloc(ReallocP, ReallocN)) == nil)
		sysfatal("out of memory");
	setrealloctag(ReallocP, getcallerpc(&ReallocP));
	return(ReallocP);
}

static void
put32(uchar *p, u32int x)
{
	p[0] = x>>24;
	p[1] = x>>16;
	p[2] = x>>8;
	p[3] = x;
}

static void
put24(uchar *p, int x)
{
	p[0] = x>>16;
	p[1] = x>>8;
	p[2] = x;
}

static void
put16(uchar *p, int x)
{
	p[0] = x>>8;
	p[1] = x;
}

static u32int
get32(uchar *p)
{
	return (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
}

static int
get24(uchar *p)
{
	return (p[0]<<16)|(p[1]<<8)|p[2];
}

static int
get16(uchar *p)
{
	return (p[0]<<8)|p[1];
}

static Bytes*
newbytes(int len)
{
	Bytes* ans;

	if(len < 0)
		abort();
	ans = emalloc(sizeof(Bytes) + len);
	ans->len = len;
	return ans;
}

/*
 * newbytes(len), with data initialized from buf
 */
static Bytes*
makebytes(uchar* buf, int len)
{
	Bytes* ans;

	ans = newbytes(len);
	memmove(ans->data, buf, len);
	return ans;
}

static void
freebytes(Bytes* b)
{
	free(b);
}

/* len is number of ints */
static Ints*
newints(int len)
{
	Ints* ans;

	if(len < 0 || len > ((uint)-1>>1)/sizeof(int))
		abort();
	ans = emalloc(sizeof(Ints) + len*sizeof(int));
	ans->len = len;
	return ans;
}

static void
freeints(Ints* b)
{
	free(b);
}
