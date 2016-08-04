enum
{
	ANAMELEN=	28,	/* name max size in previous proto */
	AERRLEN=	64,	/* errstr max size in previous proto */
	DOMLEN=		48,	/* authentication domain name length */
	DESKEYLEN=	7,	/* encrypt/decrypt des key length */
	AESKEYLEN=	16,	/* encrypt/decrypt aes key length */

	CHALLEN=	8,	/* plan9 sk1 challenge length */
	NETCHLEN=	16,	/* max network challenge length (used in AS protocol) */
	CONFIGLEN=	14,
	PASSWDLEN=	28,
	SECRETLEN=	32,	/* secret max size */

	NONCELEN=	32,

	KEYDBOFF=	8,	/* bytes of random data at key file's start */
	OKEYDBLEN=	ANAMELEN+DESKEYLEN+4+2,	/* old key file entry length */
	KEYDBLEN=	OKEYDBLEN+SECRETLEN,	/* key file entry length */
	OMD5LEN=	16,

	/* AuthPAK constants */
	PAKKEYLEN=	32,
	PAKSLEN=	(448+7)/8,	/* ed448 scalar */
	PAKPLEN=	4*PAKSLEN,	/* point in extended format X,Y,Z,T */
	PAKHASHLEN=	2*PAKPLEN,	/* hashed points PM,PN */
	PAKXLEN=	PAKSLEN,	/* random scalar secret key */ 
	PAKYLEN=	PAKSLEN,	/* decaf encoded public key */
};

typedef struct	Authkey		Authkey;
struct	Authkey
{
	char	des[DESKEYLEN];		/* DES key from password */
	uchar	aes[AESKEYLEN];		/* AES key from password */
	uchar	pakkey[PAKKEYLEN];	/* shared key from AuthPAK exchange (see authpak_finish()) */
	uchar	pakhash[PAKHASHLEN];	/* secret hash from AES key and user name (see authpak_hash()) */
};

/*
 *  convert ascii password to auth key
 */
extern	void	passtokey(Authkey*, char*);

extern	void	passtodeskey(char key[DESKEYLEN], char *p);
extern	void	passtoaeskey(uchar key[AESKEYLEN], char *p);
