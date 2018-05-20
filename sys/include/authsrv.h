#pragma	src	"/sys/src/libauthsrv"
#pragma	lib	"libauthsrv.a"

/*
 * Interface for talking to authentication server.
 */
typedef struct	Ticket		Ticket;
typedef struct	Ticketreq	Ticketreq;
typedef struct	Authenticator	Authenticator;
typedef struct	Nvrsafe		Nvrsafe;
typedef struct	Passwordreq	Passwordreq;
typedef struct	OChapreply	OChapreply;
typedef struct	OMSchapreply	OMSchapreply;
typedef struct	NTLMreply	NTLMreply;

typedef struct	Authkey		Authkey;

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
	SECRETLEN=	32,	/* secret max size */
	PASSWDLEN=	28,	/* password max size */

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

/* encryption numberings (anti-replay) */
enum
{
	AuthTreq=1,	/* ticket request */
	AuthChal=2,	/* challenge box request */
	AuthPass=3,	/* change password */
	AuthOK=4,	/* fixed length reply follows */
	AuthErr=5,	/* error follows */
	AuthMod=6,	/* modify user */
	AuthApop=7,	/* apop authentication for pop3 */
	AuthOKvar=9,	/* variable length reply follows */
	AuthChap=10,	/* chap authentication for ppp */
	AuthMSchap=11,	/* MS chap authentication for ppp */
	AuthCram=12,	/* CRAM verification for IMAP (RFC2195 & rfc2104) */
	AuthHttp=13,	/* http domain login */
	AuthVNC=14,	/* VNC server login (deprecated) */
	AuthPAK=19,	/* authenticated diffie hellman key agreement */
	AuthMSchapv2=21,/* MS chap v2 authentication for ppp */
	AuthNTLM=22,	/* NTLM authentication for cifs */
	AuthTs=64,	/* ticket encrypted with server's key */
	AuthTc,		/* ticket encrypted with client's key */
	AuthAs,		/* server generated authenticator */
	AuthAc,		/* client generated authenticator */
	AuthTp,		/* ticket encrypted with client's key for password change */
	AuthHr,		/* http reply */
};

struct Ticketreq
{
	char	type;
	char	authid[ANAMELEN];	/* server's encryption id */
	char	authdom[DOMLEN];	/* server's authentication domain */
	char	chal[CHALLEN];		/* challenge from server */
	char	hostid[ANAMELEN];	/* host's encryption id */
	char	uid[ANAMELEN];		/* uid of requesting user on host */
};
#define	TICKREQLEN	(3*ANAMELEN+CHALLEN+DOMLEN+1)

struct Ticket
{
	char	num;			/* replay protection */
	char	chal[CHALLEN];		/* server challenge */
	char	cuid[ANAMELEN];		/* uid on client */
	char	suid[ANAMELEN];		/* uid on server */
	uchar	key[NONCELEN];		/* nonce key */

	char	form;			/* (not transmitted) format (0 = des, 1 = ccpoly) */
};
#define	MAXTICKETLEN	(12+CHALLEN+2*ANAMELEN+NONCELEN+16)

struct Authenticator
{
	char	num;			/* replay protection */
	char	chal[CHALLEN];		/* server/client challenge */
	uchar	rand[NONCELEN];		/* server/client nonce */
};
#define	MAXAUTHENTLEN	(12+CHALLEN+NONCELEN+16)

struct Passwordreq
{
	char	num;
	char	old[PASSWDLEN];
	char	new[PASSWDLEN];
	char	changesecret;
	char	secret[SECRETLEN];	/* new secret */
};
#define	MAXPASSREQLEN	(12+2*PASSWDLEN+1+SECRETLEN+16)

struct	OChapreply
{
	uchar	id;
	char	uid[ANAMELEN];
	char	resp[OMD5LEN];
};
#define OCHAPREPLYLEN	(1+ANAMELEN+OMD5LEN)

struct	OMSchapreply
{
	char	uid[ANAMELEN];
	char	LMresp[24];		/* Lan Manager response */
	char	NTresp[24];		/* NT response */
};
#define OMSCHAPREPLYLEN	(ANAMELEN+24+24)

struct	NTLMreply
{
	uchar	len[2];			/* size of structure (lsb first) */
	char	uid[ANAMELEN];
	char	dom[DOMLEN];
	char	LMresp[24];		/* Lan Manager response */
	char	NTresp[24];		/* NT response (variable length) */
};
#define NTLMREPLYLEN	(2+ANAMELEN+DOMLEN+24+24)

struct	Authkey
{
	char	des[DESKEYLEN];		/* DES key from password */
	uchar	aes[AESKEYLEN];		/* AES key from password */
	uchar	pakkey[PAKKEYLEN];	/* shared key from AuthPAK exchange (see authpak_finish()) */
	uchar	pakhash[PAKHASHLEN];	/* secret hash from AES key and user name (see authpak_hash()) */
};

/*
 *  convert to/from wire format
 */
extern	int	convT2M(Ticket*, char*, int, Authkey*);
extern	int	convM2T(char*, int, Ticket*, Authkey*);
extern	int	convA2M(Authenticator*, char*, int, Ticket*);
extern	int	convM2A(char*, int, Authenticator*, Ticket*);
extern	int	convTR2M(Ticketreq*, char*, int);
extern	int	convM2TR(char*, int, Ticketreq*);
extern	int	convPR2M(Passwordreq*, char*, int, Ticket*);
extern	int	convM2PR(char*, int, Passwordreq*, Ticket*);

/*
 *  convert ascii password to auth key
 */
extern	void	passtokey(Authkey*, char*);

extern	void	passtodeskey(char key[DESKEYLEN], char *p);
extern	void	passtoaeskey(uchar key[AESKEYLEN], char *p);

/*
 *  Nvram interface
 */
enum {
	NVread		= 0,	/* just read */
	NVwrite		= 1<<0,	/* always prompt and rewrite nvram */
	NVwriteonerr	= 1<<1,	/* prompt and rewrite nvram when corrupt */
	NVwritemem	= 1<<2,	/* don't prompt, write nvram from argument */
};

/* storage layout */
struct Nvrsafe
{
	char	machkey[DESKEYLEN];	/* file server's authid's des key */
	uchar	machsum;
	char	authkey[DESKEYLEN];	/* authid's des key from password */
	uchar	authsum;
	/*
	 * file server config string of device holding full configuration;
	 * secstore key on non-file-servers.
	 */
	char	config[CONFIGLEN];
	uchar	configsum;
	char	authid[ANAMELEN];	/* auth userid, e.g., bootes */
	uchar	authidsum;
	char	authdom[DOMLEN];	/* auth domain, e.g., cs.bell-labs.com */
	uchar	authdomsum;

	uchar	aesmachkey[AESKEYLEN];
	uchar	aesmachsum;
};

extern	uchar	nvcsum(void*, int);
extern	int	readnvram(Nvrsafe*, int);
extern	char*	readcons(char*, char*, int);

/*
 *  call up auth server
 */
extern	int	authdial(char *netroot, char *authdom);

/*
 *  exchange messages with auth server
 */
extern	int	_asgetpakkey(int, Ticketreq*, Authkey*);
extern	int	_asgetticket(int, Ticketreq*, char*, int);
extern	int	_asrequest(int, Ticketreq*);
extern	int	_asgetresp(int, Ticket*, Authenticator*, Authkey *);
extern	int	_asrdresp(int, char*, int);

/*
 *  AuthPAK protocol
 */
typedef struct PAKpriv PAKpriv;
struct PAKpriv
{
	int	isclient;
	uchar	x[PAKXLEN];
	uchar	y[PAKYLEN];
};

extern	void	authpak_hash(Authkey *k, char *u);
extern	void	authpak_new(PAKpriv *p, Authkey *k, uchar y[PAKYLEN], int isclient);
extern	int	authpak_finish(PAKpriv *p, Authkey *k, uchar y[PAKYLEN]);
