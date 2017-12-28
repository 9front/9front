/*
 *  devtls - record layer for transport layer security 1.2 and secure sockets layer 3.0
 */
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	<libsec.h>

typedef struct OneWay	OneWay;
typedef struct Secret	Secret;
typedef struct TlsRec	TlsRec;
typedef struct TlsErrs	TlsErrs;

enum {
	Statlen=	1024,		/* max. length of status or stats message */
	/* buffer limits */
	MaxRecLen	= 1<<14,	/* max payload length of a record layer message */
	MaxCipherRecLen	= MaxRecLen + 2048,
	RecHdrLen	= 5,
	MaxMacLen	= SHA2_256dlen,

	/* protocol versions we can accept */
	SSL3Version	= 0x0300,
	TLS10Version	= 0x0301,
	TLS11Version	= 0x0302,
	TLS12Version	= 0x0303,
	MinProtoVersion	= 0x0300,	/* limits on version we accept */
	MaxProtoVersion	= 0x03ff,

	/* connection states */
	SHandshake	= 1 << 0,	/* doing handshake */
	SOpen		= 1 << 1,	/* application data can be sent */
	SRClose		= 1 << 2,	/* remote side has closed down */
	SLClose		= 1 << 3,	/* sent a close notify alert */
	SAlert		= 1 << 5,	/* sending or sent a fatal alert */
	SError		= 1 << 6,	/* some sort of error has occured */
	SClosed		= 1 << 7,	/* it is all over */

	/* record types */
	RChangeCipherSpec = 20,
	RAlert,
	RHandshake,
	RApplication,

	SSL2ClientHello = 1,
	HSSL2ClientHello = 9,  /* local convention;  see tlshand.c */

	/* alerts */
	ECloseNotify 			= 0,
	EUnexpectedMessage 	= 10,
	EBadRecordMac 		= 20,
	EDecryptionFailed 		= 21,
	ERecordOverflow 		= 22,
	EDecompressionFailure 	= 30,
	EHandshakeFailure 		= 40,
	ENoCertificate 			= 41,
	EBadCertificate 		= 42,
	EUnsupportedCertificate 	= 43,
	ECertificateRevoked 		= 44,
	ECertificateExpired 		= 45,
	ECertificateUnknown 	= 46,
	EIllegalParameter 		= 47,
	EUnknownCa 			= 48,
	EAccessDenied 		= 49,
	EDecodeError 			= 50,
	EDecryptError 			= 51,
	EExportRestriction 		= 60,
	EProtocolVersion 		= 70,
	EInsufficientSecurity 	= 71,
	EInternalError 			= 80,
	EUserCanceled 			= 90,
	ENoRenegotiation 		= 100,
	EUnrecognizedName		= 112,

	EMAX = 256
};

struct Secret
{
	char		*encalg;	/* name of encryption alg */
	char		*hashalg;	/* name of hash alg */

	int		(*aead_enc)(Secret*, uchar*, int, uchar*, uchar*, int);
	int		(*aead_dec)(Secret*, uchar*, int, uchar*, uchar*, int);

	int		(*enc)(Secret*, uchar*, int);
	int		(*dec)(Secret*, uchar*, int);
	int		(*unpad)(uchar*, int, int);
	DigestState*	(*mac)(uchar*, ulong, uchar*, ulong, uchar*, DigestState*);

	int		block;		/* encryption block len, 0 if none */
	int		maclen;		/* # bytes of record mac / authentication tag */
	int		recivlen;	/* # bytes of record iv for AEAD ciphers */
	void		*enckey;
	uchar		mackey[MaxMacLen];
};

struct OneWay
{
	QLock		io;		/* locks io access */
	QLock		seclock;	/* locks secret paramaters */
	u64int		seq;
	Secret		*sec;		/* cipher in use */
	Secret		*new;		/* cipher waiting for enable */
};

struct TlsRec
{
	Chan	*c;				/* io channel */
	int		ref;				/* serialized by tdlock for atomic destroy */
	int		version;			/* version of the protocol we are speaking */
	char		verset;			/* version has been set */
	char		opened;			/* opened command every issued? */
	char		err[ERRMAX];		/* error message to return to handshake requests */
	vlong	handin;			/* bytes communicated by the record layer */
	vlong	handout;
	vlong	datain;
	vlong	dataout;

	Lock		statelk;
	int		state;
	int		debug;

	/*
	 * function to genrate authenticated data blob for different
	 * protocol versions
	 */
	int		(*packAAD)(u64int, uchar*, uchar*);

	/* input side -- protected by in.io */
	OneWay		in;
	Block		*processed;	/* next bunch of application data */
	Block		*unprocessed;	/* data read from c but not parsed into records */

	/* handshake queue */
	Lock		hqlock;			/* protects hqref, alloc & free of handq, hprocessed */
	int		hqref;
	Queue		*handq;		/* queue of handshake messages */
	Block		*hprocessed;	/* remainder of last block read from handq */
	QLock		hqread;		/* protects reads for hprocessed, handq */

	/* output side */
	OneWay		out;

	/* protections */
	char		*user;
	int		perm;
};

struct TlsErrs{
	int	err;
	int	sslerr;
	int	tlserr;
	int	fatal;
	char	*msg;
};

static TlsErrs tlserrs[] = {
	{ECloseNotify,			ECloseNotify,			ECloseNotify,			0, 	"close notify"},
	{EUnexpectedMessage,	EUnexpectedMessage,	EUnexpectedMessage, 	1, "unexpected message"},
	{EBadRecordMac,		EBadRecordMac,		EBadRecordMac, 		1, "bad record mac"},
	{EDecryptionFailed,		EIllegalParameter,		EDecryptionFailed,		1, "decryption failed"},
	{ERecordOverflow,		EIllegalParameter,		ERecordOverflow,		1, "record too long"},
	{EDecompressionFailure,	EDecompressionFailure,	EDecompressionFailure,	1, "decompression failed"},
	{EHandshakeFailure,		EHandshakeFailure,		EHandshakeFailure,		1, "could not negotiate acceptable security parameters"},
	{ENoCertificate,		ENoCertificate,			ECertificateUnknown,	1, "no appropriate certificate available"},
	{EBadCertificate,		EBadCertificate,		EBadCertificate,		1, "corrupted or invalid certificate"},
	{EUnsupportedCertificate,	EUnsupportedCertificate,	EUnsupportedCertificate,	1, "unsupported certificate type"},
	{ECertificateRevoked,	ECertificateRevoked,		ECertificateRevoked,		1, "revoked certificate"},
	{ECertificateExpired,		ECertificateExpired,		ECertificateExpired,		1, "expired certificate"},
	{ECertificateUnknown,	ECertificateUnknown,	ECertificateUnknown,	1, "unacceptable certificate"},
	{EIllegalParameter,		EIllegalParameter,		EIllegalParameter,		1, "illegal parameter"},
	{EUnknownCa,			EHandshakeFailure,		EUnknownCa,			1, "unknown certificate authority"},
	{EAccessDenied,		EHandshakeFailure,		EAccessDenied,		1, "access denied"},
	{EDecodeError,			EIllegalParameter,		EDecodeError,			1, "error decoding message"},
	{EDecryptError,			EIllegalParameter,		EDecryptError,			1, "error decrypting message"},
	{EExportRestriction,		EHandshakeFailure,		EExportRestriction,		1, "export restriction violated"},
	{EProtocolVersion,		EIllegalParameter,		EProtocolVersion,		1, "protocol version not supported"},
	{EInsufficientSecurity,	EHandshakeFailure,		EInsufficientSecurity,	1, "stronger security routines required"},
	{EInternalError,			EHandshakeFailure,		EInternalError,			1, "internal error"},
	{EUserCanceled,		ECloseNotify,			EUserCanceled,			0, "handshake canceled by user"},
	{ENoRenegotiation,		EUnexpectedMessage,	ENoRenegotiation,		0, "no renegotiation"},
};

enum
{
	/* max. open tls connections */
	MaxTlsDevs	= 1024
};

static	Lock	tdlock;
static	int	tdhiwat;
static	int	maxtlsdevs = 128;
static	TlsRec	**tlsdevs;
static	char	**trnames;
static	char	*encalgs;
static	char	*hashalgs;

enum{
	Qtopdir		= 1,	/* top level directory */
	Qprotodir,
	Qclonus,
	Qencalgs,
	Qhashalgs,
	Qconvdir,		/* directory for a conversation */
	Qdata,
	Qctl,
	Qhand,
	Qstatus,
	Qstats,
};

#define TYPE(x) 	((x).path & 0xf)
#define CONV(x) 	(((x).path >> 5)&(MaxTlsDevs-1))
#define QID(c, y) 	(((c)<<5) | (y))

static void	checkstate(TlsRec *, int, int);
static void	ensure(TlsRec*, Block**, int);
static void	consume(Block**, uchar*, int);
static Chan*	buftochan(char*);
static void	tlshangup(TlsRec*);
static void	tlsError(TlsRec*, char *);
static void	alertHand(TlsRec*, char *);
static TlsRec	*newtls(Chan *c);
static TlsRec	*mktlsrec(void);
static DigestState*sslmac_md5(uchar *p, ulong len, uchar *key, ulong klen, uchar *digest, DigestState *s);
static DigestState*sslmac_sha1(uchar *p, ulong len, uchar *key, ulong klen, uchar *digest, DigestState *s);
static DigestState*nomac(uchar *p, ulong len, uchar *key, ulong klen, uchar *digest, DigestState *s);
static int	sslPackAAD(u64int, uchar*, uchar*);
static int	tlsPackAAD(u64int, uchar*, uchar*);
static void	packMac(Secret*, uchar*, int, uchar*, int, uchar*);
static void	put64(uchar *p, u64int);
static void	put32(uchar *p, u32int);
static void	put24(uchar *p, int);
static void	put16(uchar *p, int);
static int	get16(uchar *p);
static void	tlsSetState(TlsRec *tr, int new, int old);
static void	rcvAlert(TlsRec *tr, int err);
static void	sendAlert(TlsRec *tr, int err);
static void	rcvError(TlsRec *tr, int err, char *msg, ...);
static int	rc4enc(Secret *sec, uchar *buf, int n);
static int	des3enc(Secret *sec, uchar *buf, int n);
static int	des3dec(Secret *sec, uchar *buf, int n);
static int	aesenc(Secret *sec, uchar *buf, int n);
static int	aesdec(Secret *sec, uchar *buf, int n);
static int	ccpoly_aead_enc(Secret *sec, uchar *aad, int aadlen, uchar *reciv, uchar *data, int len);
static int	ccpoly_aead_dec(Secret *sec, uchar *aad, int aadlen, uchar *reciv, uchar *data, int len);
static int	aesgcm_aead_enc(Secret *sec, uchar *aad, int aadlen, uchar *reciv, uchar *data, int len);
static int	aesgcm_aead_dec(Secret *sec, uchar *aad, int aadlen, uchar *reciv, uchar *data, int len);
static int	noenc(Secret *sec, uchar *buf, int n);
static int	sslunpad(uchar *buf, int n, int block);
static int	tlsunpad(uchar *buf, int n, int block);
static void	freeSec(Secret *sec);
static char	*tlsstate(int s);
static void	pdump(int, void*, char*);

#pragma	varargck	argpos	rcvError	3

static char *tlsnames[] = {
[Qclonus]		"clone",
[Qencalgs]	"encalgs",
[Qhashalgs]	"hashalgs",
[Qdata]		"data",
[Qctl]		"ctl",
[Qhand]		"hand",
[Qstatus]		"status",
[Qstats]		"stats",
};

static int convdir[] = { Qctl, Qdata, Qhand, Qstatus, Qstats };

static int
tlsgen(Chan *c, char*, Dirtab *, int, int s, Dir *dp)
{
	Qid q;
	TlsRec *tr;
	char *name, *nm;
	int perm, t;

	q.vers = 0;
	q.type = QTFILE;

	t = TYPE(c->qid);
	switch(t) {
	case Qtopdir:
		if(s == DEVDOTDOT){
			q.path = QID(0, Qtopdir);
			q.type = QTDIR;
			devdir(c, q, "#a", 0, eve, 0555, dp);
			return 1;
		}
		if(s > 0)
			return -1;
		q.path = QID(0, Qprotodir);
		q.type = QTDIR;
		devdir(c, q, "tls", 0, eve, 0555, dp);
		return 1;
	case Qprotodir:
		if(s == DEVDOTDOT){
			q.path = QID(0, Qtopdir);
			q.type = QTDIR;
			devdir(c, q, ".", 0, eve, 0555, dp);
			return 1;
		}
		if(s < 3){
			switch(s) {
			default:
				return -1;
			case 0:
				q.path = QID(0, Qclonus);
				break;
			case 1:
				q.path = QID(0, Qencalgs);
				break;
			case 2:
				q.path = QID(0, Qhashalgs);
				break;
			}
			perm = 0444;
			if(TYPE(q) == Qclonus)
				perm = 0555;
			devdir(c, q, tlsnames[TYPE(q)], 0, eve, perm, dp);
			return 1;
		}
		s -= 3;
		if(s >= tdhiwat)
			return -1;
		q.path = QID(s, Qconvdir);
		q.type = QTDIR;
		lock(&tdlock);
		tr = tlsdevs[s];
		if(tr != nil)
			nm = tr->user;
		else
			nm = eve;
		if((name = trnames[s]) == nil){
			name = trnames[s] = smalloc(16);
			sprint(name, "%d", s);
		}
		devdir(c, q, name, 0, nm, 0555, dp);
		unlock(&tdlock);
		return 1;
	case Qconvdir:
		if(s == DEVDOTDOT){
			q.path = QID(0, Qprotodir);
			q.type = QTDIR;
			devdir(c, q, "tls", 0, eve, 0555, dp);
			return 1;
		}
		if(s < 0 || s >= nelem(convdir))
			return -1;
		lock(&tdlock);
		tr = tlsdevs[CONV(c->qid)];
		if(tr != nil){
			nm = tr->user;
			perm = tr->perm;
		}else{
			perm = 0;
			nm = eve;
		}
		t = convdir[s];
		if(t == Qstatus || t == Qstats)
			perm &= 0444;
		q.path = QID(CONV(c->qid), t);
		devdir(c, q, tlsnames[t], 0, nm, perm, dp);
		unlock(&tdlock);
		return 1;
	case Qclonus:
	case Qencalgs:
	case Qhashalgs:
		perm = 0444;
		if(t == Qclonus)
			perm = 0555;
		devdir(c, c->qid, tlsnames[t], 0, eve, perm, dp);
		return 1;
	default:
		lock(&tdlock);
		tr = tlsdevs[CONV(c->qid)];
		if(tr != nil){
			nm = tr->user;
			perm = tr->perm;
		}else{
			perm = 0;
			nm = eve;
		}
		if(t == Qstatus || t == Qstats)
			perm &= 0444;
		devdir(c, c->qid, tlsnames[t], 0, nm, perm, dp);
		unlock(&tdlock);
		return 1;
	}
}

static Chan*
tlsattach(char *spec)
{
	Chan *c;

	c = devattach('a', spec);
	c->qid.path = QID(0, Qtopdir);
	c->qid.type = QTDIR;
	c->qid.vers = 0;
	return c;
}

static Walkqid*
tlswalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, nil, 0, tlsgen);
}

static int
tlsstat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, nil, 0, tlsgen);
}

static Chan*
tlsopen(Chan *c, int omode)
{
	TlsRec *tr, **pp;
	int t;

	t = TYPE(c->qid);
	switch(t) {
	default:
		panic("tlsopen");
	case Qtopdir:
	case Qprotodir:
	case Qconvdir:
		if(omode != OREAD)
			error(Eperm);
		break;
	case Qclonus:
		tr = newtls(c);
		if(tr == nil)
			error(Enodev);
		break;
	case Qctl:
	case Qdata:
	case Qhand:
	case Qstatus:
	case Qstats:
		if((t == Qstatus || t == Qstats) && omode != OREAD)
			error(Eperm);
		if(waserror()) {
			unlock(&tdlock);
			nexterror();
		}
		lock(&tdlock);
		pp = &tlsdevs[CONV(c->qid)];
		tr = *pp;
		if(tr == nil)
			error("must open connection using clone");
		devpermcheck(tr->user, tr->perm, omode);
		if(t == Qhand){
			if(waserror()){
				unlock(&tr->hqlock);
				nexterror();
			}
			lock(&tr->hqlock);
			if(tr->handq != nil)
				error(Einuse);
			tr->handq = qopen(2 * MaxCipherRecLen, 0, nil, nil);
			if(tr->handq == nil)
				error("cannot allocate handshake queue");
			tr->hqref = 1;
			unlock(&tr->hqlock);
			poperror();
		}
		tr->ref++;
		unlock(&tdlock);
		poperror();
		break;
	case Qencalgs:
	case Qhashalgs:
		if(omode != OREAD)
			error(Eperm);
		break;
	}
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	c->iounit = MaxRecLen;
	return c;
}

static int
tlswstat(Chan *c, uchar *dp, int n)
{
	Dir *d;
	TlsRec *tr;
	int rv;

	d = nil;
	if(waserror()){
		free(d);
		unlock(&tdlock);
		nexterror();
	}

	lock(&tdlock);
	tr = tlsdevs[CONV(c->qid)];
	if(tr == nil)
		error(Ebadusefd);
	if(strcmp(tr->user, up->user) != 0)
		error(Eperm);

	d = smalloc(n + sizeof *d);
	rv = convM2D(dp, n, &d[0], (char*) &d[1]);
	if(rv == 0)
		error(Eshortstat);
	if(!emptystr(d->uid))
		kstrdup(&tr->user, d->uid);
	if(d->mode != -1)
		tr->perm = d->mode;

	free(d);
	poperror();
	unlock(&tdlock);

	return rv;
}

static void
dechandq(TlsRec *tr)
{
	lock(&tr->hqlock);
	if(--tr->hqref == 0){
		if(tr->handq != nil){
			qfree(tr->handq);
			tr->handq = nil;
		}
		if(tr->hprocessed != nil){
			freeb(tr->hprocessed);
			tr->hprocessed = nil;
		}
	}
	unlock(&tr->hqlock);
}

static void
tlsclose(Chan *c)
{
	TlsRec *tr;
	int t;

	t = TYPE(c->qid);
	switch(t) {
	case Qctl:
	case Qdata:
	case Qhand:
	case Qstatus:
	case Qstats:
		if((c->flag & COPEN) == 0)
			break;

		tr = tlsdevs[CONV(c->qid)];
		if(tr == nil)
			break;

		if(t == Qhand)
			dechandq(tr);

		lock(&tdlock);
		if(--tr->ref > 0) {
			unlock(&tdlock);
			return;
		}
		tlsdevs[CONV(c->qid)] = nil;
		unlock(&tdlock);

		if(tr->c != nil && !waserror()){
			checkstate(tr, 0, SOpen|SHandshake|SRClose);
			sendAlert(tr, ECloseNotify);
			poperror();
		}
		tlshangup(tr);
		if(tr->c != nil)
			cclose(tr->c);
		freeSec(tr->in.sec);
		freeSec(tr->in.new);
		freeSec(tr->out.sec);
		freeSec(tr->out.new);
		free(tr->user);
		free(tr);
		break;
	}
}

/*
 *  make sure we have at least 'n' bytes in list 'l'
 */
static void
ensure(TlsRec *s, Block **l, int n)
{
	int sofar, i;
	Block *b, *bl;

	sofar = 0;
	for(b = *l; b; b = b->next){
		sofar += BLEN(b);
		if(sofar >= n)
			return;
		l = &b->next;
	}

	while(sofar < n){
		bl = devtab[s->c->type]->bread(s->c, MaxCipherRecLen + RecHdrLen, 0);
		if(bl == 0)
			error(Ehungup);
		*l = bl;
		i = 0;
		for(b = bl; b; b = b->next){
			i += BLEN(b);
			l = &b->next;
		}
		if(i == 0)
			error(Ehungup);
		sofar += i;
	}
if(s->debug) pprint("ensure read %d\n", sofar);
}

/*
 *  copy 'n' bytes from 'l' into 'p' and free
 *  the bytes in 'l'
 */
static void
consume(Block **l, uchar *p, int n)
{
	Block *b;
	int i;

	for(; *l && n > 0; n -= i){
		b = *l;
		i = BLEN(b);
		if(i > n)
			i = n;
		memmove(p, b->rp, i);
		b->rp += i;
		p += i;
		if(BLEN(b) < 0)
			panic("consume");
		if(BLEN(b))
			break;
		*l = b->next;
		freeb(b);
	}
}

/*
 *  give back n bytes
 */
static void
regurgitate(TlsRec *s, uchar *p, int n)
{
	Block *b;

	if(n <= 0)
		return;
	b = s->unprocessed;
	if(s->unprocessed == nil || b->rp - b->base < n) {
		b = allocb(n);
		memmove(b->wp, p, n);
		b->wp += n;
		b->next = s->unprocessed;
		s->unprocessed = b;
	} else {
		b->rp -= n;
		memmove(b->rp, p, n);
	}
}

/*
 *  remove at most n bytes from the queue
 */
static Block*
qgrab(Block **l, int n)
{
	Block *bb, *b;
	int i;

	b = *l;
	if(BLEN(b) == n){
		*l = b->next;
		b->next = nil;
		return b;
	}

	i = 0;
	for(bb = b; bb != nil && i < n; bb = bb->next)
		i += BLEN(bb);
	if(i > n)
		i = n;

	bb = allocb(i);
	consume(l, bb->wp, i);
	bb->wp += i;
	return bb;
}

static void
tlsclosed(TlsRec *tr, int new)
{
	lock(&tr->statelk);
	if(tr->state == SOpen || tr->state == SHandshake)
		tr->state = new;
	else if((new | tr->state) == (SRClose|SLClose))
		tr->state = SClosed;
	unlock(&tr->statelk);
	alertHand(tr, "close notify");
}

/*
 *  read and process one tls record layer message
 *  must be called with tr->in.io held
 *  We can't let Eintrs lose data, since doing so will get
 *  us out of sync with the sender and break the reliablity
 *  of the channel.  Eintr only happens during the reads in
 *  consume.  Therefore we put back any bytes consumed before
 *  the last call to ensure.
 */
static void
tlsrecread(TlsRec *tr)
{
	OneWay *volatile in;
	Block *volatile b;
	uchar *p, aad[8+RecHdrLen], header[RecHdrLen], hmac[MaxMacLen];
	int volatile nconsumed;
	int len, type, ver, unpad_len, aadlen, ivlen;
	Secret *sec;

	nconsumed = 0;
	if(waserror()){
		if(strcmp(up->errstr, Eintr) == 0 && !waserror()){
			regurgitate(tr, header, nconsumed);
			poperror();
		}else
			tlsError(tr, "channel error");
		nexterror();
	}
	ensure(tr, &tr->unprocessed, RecHdrLen);
	consume(&tr->unprocessed, header, RecHdrLen);
if(tr->debug)pprint("consumed %d header\n", RecHdrLen);
	nconsumed = RecHdrLen;

	if((tr->handin == 0) && (header[0] & 0x80)){
		/* Cope with an SSL3 ClientHello expressed in SSL2 record format.
			This is sent by some clients that we must interoperate
			with, such as Java's JSSE and Microsoft's Internet Explorer. */
		len = (get16(header) & ~0x8000) - 3;
		type = header[2];
		ver = get16(header + 3);
		if(type != SSL2ClientHello || len < 22)
			rcvError(tr, EProtocolVersion, "invalid initial SSL2-like message");
	}else{  /* normal SSL3 record format */
		type = header[0];
		ver = get16(header+1);
		len = get16(header+3);
	}
	if(ver != tr->version && (tr->verset || ver < MinProtoVersion || ver > MaxProtoVersion))
		rcvError(tr, EProtocolVersion, "devtls expected ver=%x%s, saw (len=%d) type=%x ver=%x '%.12s'",
			tr->version, tr->verset?"/set":"", len, type, ver, (char*)header);
	if(len > MaxCipherRecLen || len < 0)
		rcvError(tr, ERecordOverflow, "record message too long %d", len);
	ensure(tr, &tr->unprocessed, len);
	nconsumed = 0;
	poperror();

	/*
	 * If an Eintr happens after this, we'll get out of sync.
	 * Make sure nothing we call can sleep.
	 * Errors are ok, as they kill the connection.
	 * Luckily, allocb won't sleep, it'll just error out.
	 */
	b = nil;
	if(waserror()){
		if(b != nil)
			freeb(b);
		tlsError(tr, "channel error");
		nexterror();
	}
	b = qgrab(&tr->unprocessed, len);
if(tr->debug) pprint("consumed unprocessed %d\n", len);

	in = &tr->in;
	if(waserror()){
		qunlock(&in->seclock);
		nexterror();
	}
	qlock(&in->seclock);
	p = b->rp;
	sec = in->sec;
	if(sec != nil) {
		/* to avoid Canvel-Hiltgen-Vaudenay-Vuagnoux attack, all errors here
		        should look alike, including timing of the response. */
		if(sec->aead_dec != nil)
			unpad_len = len;
		else {
			unpad_len = (*sec->dec)(sec, p, len);
if(tr->debug) pprint("decrypted %d\n", unpad_len);
if(tr->debug) pdump(unpad_len, p, "decrypted:");
		}

		ivlen = sec->recivlen;
		if(tr->version >= TLS11Version){
			if(ivlen == 0)
				ivlen = sec->block;
		}
		len -= ivlen;
		if(len < 0)
			rcvError(tr, EDecodeError, "runt record message");
		unpad_len -= ivlen;
		p += ivlen;

		if(unpad_len >= sec->maclen)
			len = unpad_len - sec->maclen;

		/* update length */
		put16(header+3, len);
		aadlen = (*tr->packAAD)(in->seq++, header, aad);
		if(sec->aead_dec != nil) {
			len = (*sec->aead_dec)(sec, aad, aadlen, p - ivlen, p, unpad_len);
			if(len < 0)
				rcvError(tr, EBadRecordMac, "record mac mismatch");
		} else {
			packMac(sec, aad, aadlen, p, len, hmac);
			if(unpad_len < sec->maclen)
				rcvError(tr, EBadRecordMac, "short record mac");
			if(tsmemcmp(hmac, p + len, sec->maclen) != 0)
				rcvError(tr, EBadRecordMac, "record mac mismatch");
		}
		b->rp = p;
		b->wp = p+len;
	}
	qunlock(&in->seclock);
	poperror();
	if(len < 0)
		rcvError(tr, EDecodeError, "runt record message");

	switch(type) {
	default:
		rcvError(tr, EIllegalParameter, "invalid record message %#x", type);
		break;
	case RChangeCipherSpec:
		if(len != 1 || p[0] != 1)
			rcvError(tr, EDecodeError, "invalid change cipher spec");
		qlock(&in->seclock);
		if(in->new == nil){
			qunlock(&in->seclock);
			rcvError(tr, EUnexpectedMessage, "unexpected change cipher spec");
		}
		freeSec(in->sec);
		in->sec = in->new;
		in->new = nil;
		in->seq = 0;
		qunlock(&in->seclock);
		break;
	case RAlert:
		if(len != 2)
			rcvError(tr, EDecodeError, "invalid alert");
		if(p[0] == 2)
			rcvAlert(tr, p[1]);
		if(p[0] != 1)
			rcvError(tr, EIllegalParameter, "invalid alert fatal code");

		/*
		 * propagate non-fatal alerts to handshaker
		 */
		switch(p[1]){
		case ECloseNotify:
			tlsclosed(tr, SRClose);
			if(tr->opened)
				error("tls hungup");
			error("close notify");
			break;
		case ENoRenegotiation:
			alertHand(tr, "no renegotiation");
			break;
		case EUserCanceled:
			alertHand(tr, "handshake canceled by user");
			break;
		case EUnrecognizedName:
			/* happens in response to SNI, can be ignored. */
			break;
		default:
			rcvError(tr, EIllegalParameter, "invalid alert code");
		}
		break;
	case RHandshake:
		/*
		 * don't worry about dropping the block
		 * qbwrite always queues even if flow controlled and interrupted.
		 *
		 * if there isn't any handshaker, ignore the request,
		 * but notify the other side we are doing so.
		 */
		lock(&tr->hqlock);
		if(tr->handq != nil){
			tr->hqref++;
			unlock(&tr->hqlock);
			if(waserror()){
				dechandq(tr);
				nexterror();
			}
			b = padblock(b, 1);
			*b->rp = RHandshake;
			qbwrite(tr->handq, b);
			b = nil;
			poperror();
			dechandq(tr);
		}else{
			unlock(&tr->hqlock);
			if(tr->verset && tr->version != SSL3Version && !waserror()){
				sendAlert(tr, ENoRenegotiation);
				poperror();
			}
		}
		break;
	case SSL2ClientHello:
		lock(&tr->hqlock);
		if(tr->handq != nil){
			tr->hqref++;
			unlock(&tr->hqlock);
			if(waserror()){
				dechandq(tr);
				nexterror();
			}
			/* Pass the SSL2 format data, so that the handshake code can compute
				the correct checksums.  HSSL2ClientHello = HandshakeType 9 is
				unused in RFC2246. */
			b = padblock(b, 8);
			b->rp[0] = RHandshake;
			b->rp[1] = HSSL2ClientHello;
			put24(&b->rp[2], len+3);
			b->rp[5] = SSL2ClientHello;
			put16(&b->rp[6], ver);
			qbwrite(tr->handq, b);
			b = nil;
			poperror();
			dechandq(tr);
		}else{
			unlock(&tr->hqlock);
			if(tr->verset && tr->version != SSL3Version && !waserror()){
				sendAlert(tr, ENoRenegotiation);
				poperror();
			}
		}
		break;
	case RApplication:
		if(!tr->opened)
			rcvError(tr, EUnexpectedMessage, "application message received before handshake completed");
		if(BLEN(b) > 0){
			tr->processed = b;
			b = nil;
		}
		break;
	}
	if(b != nil)
		freeb(b);
	poperror();
}

/*
 * got a fatal alert message
 */
static void
rcvAlert(TlsRec *tr, int err)
{
	char *s;
	int i;

	s = "unknown error";
	for(i=0; i < nelem(tlserrs); i++){
		if(tlserrs[i].err == err){
			s = tlserrs[i].msg;
			break;
		}
	}
if(tr->debug) pprint("rcvAlert: %s\n", s);

	tlsError(tr, s);
	if(!tr->opened)
		error(s);
	error("tls error");
}

/*
 * found an error while decoding the input stream
 */
static void
rcvError(TlsRec *tr, int err, char *fmt, ...)
{
	char msg[ERRMAX];
	va_list arg;

	va_start(arg, fmt);
	vseprint(msg, msg+sizeof(msg), fmt, arg);
	va_end(arg);
if(tr->debug) pprint("rcvError: %s\n", msg);

	sendAlert(tr, err);

	if(!tr->opened)
		error(msg);
	error("tls error");
}

/*
 * make sure the next hand operation returns with a 'msg' error
 */
static void
alertHand(TlsRec *tr, char *msg)
{
	Block *b;
	int n;

	lock(&tr->hqlock);
	if(tr->handq == nil){
		unlock(&tr->hqlock);
		return;
	}
	tr->hqref++;
	unlock(&tr->hqlock);

	n = strlen(msg);
	if(waserror()){
		dechandq(tr);
		nexterror();
	}
	b = allocb(n + 2);
	*b->wp++ = RAlert;
	memmove(b->wp, msg, n + 1);
	b->wp += n + 1;

	qbwrite(tr->handq, b);

	poperror();
	dechandq(tr);
}

static void
checkstate(TlsRec *tr, int ishand, int ok)
{
	int state;

	lock(&tr->statelk);
	state = tr->state;
	unlock(&tr->statelk);
	if(state & ok)
		return;
	switch(state){
	case SHandshake:
	case SOpen:
		break;
	case SError:
	case SAlert:
		if(ishand)
			error(tr->err);
		error("tls error");
	case SRClose:
	case SLClose:
	case SClosed:
		error("tls hungup");
	}
	error("tls improperly configured");
}

static Block*
tlsbread(Chan *c, long n, ulong offset)
{
	int ty;
	Block *b;
	TlsRec *volatile tr;

	ty = TYPE(c->qid);
	switch(ty) {
	default:
		return devbread(c, n, offset);
	case Qhand:
	case Qdata:
		break;
	}

	tr = tlsdevs[CONV(c->qid)];
	if(tr == nil)
		panic("tlsbread");

	if(waserror()){
		qunlock(&tr->in.io);
		nexterror();
	}
	qlock(&tr->in.io);
	if(ty == Qdata){
		checkstate(tr, 0, SOpen);
		while(tr->processed == nil)
			tlsrecread(tr);

		/* return at most what was asked for */
		b = qgrab(&tr->processed, n);
if(tr->debug) pprint("consumed processed %zd\n", BLEN(b));
if(tr->debug) pdump(BLEN(b), b->rp, "consumed:");
		qunlock(&tr->in.io);
		poperror();
		tr->datain += BLEN(b);
	}else{
		checkstate(tr, 1, SOpen|SHandshake|SLClose);

		/*
		 * it's ok to look at state without the lock
		 * since it only protects reading records,
		 * and we have that tr->in.io held.
		 */
		while(!tr->opened && tr->hprocessed == nil && !qcanread(tr->handq))
			tlsrecread(tr);

		qunlock(&tr->in.io);
		poperror();

		if(waserror()){
			qunlock(&tr->hqread);
			nexterror();
		}
		qlock(&tr->hqread);
		if(tr->hprocessed == nil){
			b = qbread(tr->handq, MaxRecLen + 1);
			if(*b->rp++ == RAlert){
				kstrcpy(up->errstr, (char*)b->rp, ERRMAX);
				freeb(b);
				nexterror();
			}
			tr->hprocessed = b;
		}
		b = qgrab(&tr->hprocessed, n);
		poperror();
		qunlock(&tr->hqread);
		tr->handin += BLEN(b);
	}

	return b;
}

static long
tlsread(Chan *c, void *a, long n, vlong off)
{
	Block *volatile b;
	Block *nb;
	uchar *va;
	int i, ty;
	char *buf, *s, *e;
	ulong offset = off;
	TlsRec * tr;

	if(c->qid.type & QTDIR)
		return devdirread(c, a, n, 0, 0, tlsgen);

	tr = tlsdevs[CONV(c->qid)];
	ty = TYPE(c->qid);
	switch(ty) {
	default:
		error(Ebadusefd);
	case Qstatus:
		buf = smalloc(Statlen);
		qlock(&tr->in.seclock);
		qlock(&tr->out.seclock);
		s = buf;
		e = buf + Statlen;
		s = seprint(s, e, "State: %s\n", tlsstate(tr->state));
		s = seprint(s, e, "Version: %#x\n", tr->version);
		if(tr->in.sec != nil)
			s = seprint(s, e, "EncIn: %s\nHashIn: %s\n", tr->in.sec->encalg, tr->in.sec->hashalg);
		if(tr->in.new != nil)
			s = seprint(s, e, "NewEncIn: %s\nNewHashIn: %s\n", tr->in.new->encalg, tr->in.new->hashalg);
		if(tr->out.sec != nil)
			s = seprint(s, e, "EncOut: %s\nHashOut: %s\n", tr->out.sec->encalg, tr->out.sec->hashalg);
		if(tr->out.new != nil)
			s = seprint(s, e, "NewEncOut: %s\nNewHashOut: %s\n", tr->out.new->encalg, tr->out.new->hashalg);
		if(tr->c != nil)
			seprint(s, e, "Chan: %s\n", chanpath(tr->c));
		qunlock(&tr->in.seclock);
		qunlock(&tr->out.seclock);
		n = readstr(offset, a, n, buf);
		free(buf);
		return n;
	case Qstats:
		buf = smalloc(Statlen);
		s = buf;
		e = buf + Statlen;
		s = seprint(s, e, "DataIn: %lld\n", tr->datain);
		s = seprint(s, e, "DataOut: %lld\n", tr->dataout);
		s = seprint(s, e, "HandIn: %lld\n", tr->handin);
		seprint(s, e, "HandOut: %lld\n", tr->handout);
		n = readstr(offset, a, n, buf);
		free(buf);
		return n;
	case Qctl:
		buf = smalloc(Statlen);
		snprint(buf, Statlen, "%llud", CONV(c->qid));
		n = readstr(offset, a, n, buf);
		free(buf);
		return n;
	case Qdata:
	case Qhand:
		b = tlsbread(c, n, offset);
		break;
	case Qencalgs:
		return readstr(offset, a, n, encalgs);
	case Qhashalgs:
		return readstr(offset, a, n, hashalgs);
	}

	if(waserror()){
		freeblist(b);
		nexterror();
	}

	n = 0;
	va = a;
	for(nb = b; nb; nb = nb->next){
		i = BLEN(nb);
		memmove(va+n, nb->rp, i);
		n += i;
	}

	freeblist(b);
	poperror();

	return n;
}

/*
 *  write a block in tls records
 */
static void
tlsrecwrite(TlsRec *tr, int type, Block *b)
{
	Block *volatile bb;
	Block *nb;
	uchar *p, aad[8+RecHdrLen];
	OneWay *volatile out;
	int n, ivlen, maclen, aadlen, pad, ok;
	Secret *sec;

	out = &tr->out;
	bb = b;
	if(waserror()){
		qunlock(&out->io);
		if(bb != nil)
			freeb(bb);
		nexterror();
	}
	qlock(&out->io);
if(tr->debug)pprint("send %zd\n", BLEN(b));
if(tr->debug)pdump(BLEN(b), b->rp, "sent:");


	ok = SHandshake|SOpen|SRClose;
	if(type == RAlert)
		ok |= SAlert;
	while(bb != nil){
		checkstate(tr, type != RApplication, ok);

		/*
		 * get at most one maximal record's input,
		 * with padding on the front for header and
		 * back for mac and maximal block padding.
		 */
		if(waserror()){
			qunlock(&out->seclock);
			nexterror();
		}
		qlock(&out->seclock);
		maclen = 0;
		pad = 0;
		ivlen = 0;
		sec = out->sec;
		if(sec != nil){
			maclen = sec->maclen;
			pad = maclen + sec->block;
			ivlen = sec->recivlen;
			if(tr->version >= TLS11Version){
				if(ivlen == 0)
					ivlen = sec->block;
			}
		}
		n = BLEN(bb);
		if(n > MaxRecLen){
			n = MaxRecLen;
			nb = allocb(RecHdrLen + ivlen + n + pad);
			memmove(nb->wp + RecHdrLen + ivlen, bb->rp, n);
			bb->rp += n;
		}else{
			/*
			 * carefully reuse bb so it will get freed if we're out of memory
			 */
			bb = padblock(bb, RecHdrLen + ivlen);
			if(pad)
				nb = padblock(bb, -pad);
			else
				nb = bb;
			bb = nil;
		}

		p = nb->rp;
		p[0] = type;
		put16(p+1, tr->version);
		put16(p+3, n);

		if(sec != nil){
			aadlen = (*tr->packAAD)(out->seq++, p, aad);
			if(sec->aead_enc != nil)
				n = (*sec->aead_enc)(sec, aad, aadlen, p + RecHdrLen, p + RecHdrLen + ivlen, n) + ivlen;
			else {
				if(ivlen > 0)
					prng(p + RecHdrLen, ivlen);
				packMac(sec, aad, aadlen, p + RecHdrLen + ivlen, n, p + RecHdrLen + ivlen + n);
				n = (*sec->enc)(sec, p + RecHdrLen, ivlen + n + maclen);
			}
			nb->wp = p + RecHdrLen + n;

			/* update length */
			put16(p+3, n);
		}
		if(type == RChangeCipherSpec){
			if(out->new == nil)
				error("change cipher without a new cipher");
			freeSec(out->sec);
			out->sec = out->new;
			out->new = nil;
			out->seq = 0;
		}
		qunlock(&out->seclock);
		poperror();

		/*
		 * if bwrite error's, we assume the block is queued.
		 * if not, we're out of sync with the receiver and will not recover.
		 */
		if(waserror()){
			if(strcmp(up->errstr, "interrupted") != 0)
				tlsError(tr, "channel error");
			nexterror();
		}
		devtab[tr->c->type]->bwrite(tr->c, nb, 0);
		poperror();
	}
	qunlock(&out->io);
	poperror();
}

static long
tlsbwrite(Chan *c, Block *b, ulong offset)
{
	int ty;
	ulong n;
	TlsRec *tr;

	n = BLEN(b);

	tr = tlsdevs[CONV(c->qid)];
	if(tr == nil)
		panic("tlsbwrite");

	ty = TYPE(c->qid);
	switch(ty) {
	default:
		return devbwrite(c, b, offset);
	case Qhand:
		tlsrecwrite(tr, RHandshake, b);
		tr->handout += n;
		break;
	case Qdata:
		checkstate(tr, 0, SOpen);
		tlsrecwrite(tr, RApplication, b);
		tr->dataout += n;
		break;
	}

	return n;
}

typedef struct Hashalg Hashalg;
struct Hashalg
{
	char	*name;
	int	maclen;
	void	(*initkey)(Hashalg *, int, Secret *, uchar*);
};

static void
initmd5key(Hashalg *ha, int version, Secret *s, uchar *p)
{
	s->maclen = ha->maclen;
	if(version == SSL3Version)
		s->mac = sslmac_md5;
	else
		s->mac = hmac_md5;
	memmove(s->mackey, p, ha->maclen);
}

static void
initclearmac(Hashalg *, int, Secret *s, uchar *)
{
	s->mac = nomac;
}

static void
initsha1key(Hashalg *ha, int version, Secret *s, uchar *p)
{
	s->maclen = ha->maclen;
	if(version == SSL3Version)
		s->mac = sslmac_sha1;
	else
		s->mac = hmac_sha1;
	memmove(s->mackey, p, ha->maclen);
}

static void
initsha2_256key(Hashalg *ha, int version, Secret *s, uchar *p)
{
	if(version == SSL3Version)
		error("sha256 cannot be used with SSL");
	s->maclen = ha->maclen;
	s->mac = hmac_sha2_256;
	memmove(s->mackey, p, ha->maclen);
}

static Hashalg hashtab[] =
{
	{ "clear",	0,		initclearmac, },
	{ "md5",	MD5dlen,	initmd5key, },
	{ "sha1",	SHA1dlen,	initsha1key, },
	{ "sha256",	SHA2_256dlen,	initsha2_256key, },
	{ 0 }
};

static Hashalg*
parsehashalg(char *p)
{
	Hashalg *ha;

	for(ha = hashtab; ha->name; ha++)
		if(strcmp(p, ha->name) == 0)
			return ha;
	error("unsupported hash algorithm");
	return nil;
}

typedef struct Encalg Encalg;
struct Encalg
{
	char	*name;
	int	keylen;
	int	ivlen;
	void	(*initkey)(Encalg *ea, Secret *, uchar*, uchar*);
};

static void
initRC4key(Encalg *ea, Secret *s, uchar *p, uchar *)
{
	s->enckey = secalloc(sizeof(RC4state));
	s->enc = rc4enc;
	s->dec = rc4enc;
	setupRC4state(s->enckey, p, ea->keylen);
}

static void
initDES3key(Encalg *, Secret *s, uchar *p, uchar *iv)
{
	s->enckey = secalloc(sizeof(DES3state));
	s->enc = des3enc;
	s->dec = des3dec;
	s->block = 8;
	setupDES3state(s->enckey, (uchar(*)[8])p, iv);
}

static void
initAESkey(Encalg *ea, Secret *s, uchar *p, uchar *iv)
{
	s->enckey = secalloc(sizeof(AESstate));
	s->enc = aesenc;
	s->dec = aesdec;
	s->block = 16;
	setupAESstate(s->enckey, p, ea->keylen, iv);
}

static void
initccpolykey(Encalg *ea, Secret *s, uchar *p, uchar *iv)
{
	s->enckey = secalloc(sizeof(Chachastate));
	s->aead_enc = ccpoly_aead_enc;
	s->aead_dec = ccpoly_aead_dec;
	s->maclen = Poly1305dlen;
	if(ea->ivlen == 0) {
		/* older draft version, iv is 64-bit sequence number */
		setupChachastate(s->enckey, p, ea->keylen, nil, 64/8, 20);
	} else {
		/* IETF standard, 96-bit iv xored with sequence number */
		memmove(s->mackey, iv, ea->ivlen);
		setupChachastate(s->enckey, p, ea->keylen, iv, ea->ivlen, 20);
	}
}

static void
initaesgcmkey(Encalg *ea, Secret *s, uchar *p, uchar *iv)
{
	s->enckey = secalloc(sizeof(AESGCMstate));
	s->aead_enc = aesgcm_aead_enc;
	s->aead_dec = aesgcm_aead_dec;
	s->maclen = 16;
	s->recivlen = 8;
	memmove(s->mackey, iv, ea->ivlen);
	prng(s->mackey + ea->ivlen, s->recivlen);
	setupAESGCMstate(s->enckey, p, ea->keylen, nil, 0);
}

static void
initclearenc(Encalg *, Secret *s, uchar *, uchar *)
{
	s->enc = noenc;
	s->dec = noenc;
}

static Encalg encrypttab[] =
{
	{ "clear", 0, 0, initclearenc },
	{ "rc4_128", 128/8, 0, initRC4key },
	{ "3des_ede_cbc", 3 * 8, 8, initDES3key },
	{ "aes_128_cbc", 128/8, 16, initAESkey },
	{ "aes_256_cbc", 256/8, 16, initAESkey },
	{ "ccpoly64_aead", 256/8, 0, initccpolykey },
	{ "ccpoly96_aead", 256/8, 96/8, initccpolykey },
	{ "aes_128_gcm_aead", 128/8, 4, initaesgcmkey },
	{ "aes_256_gcm_aead", 256/8, 4, initaesgcmkey },
	{ 0 }
};

static Encalg*
parseencalg(char *p)
{
	Encalg *ea;

	for(ea = encrypttab; ea->name; ea++)
		if(strcmp(p, ea->name) == 0)
			return ea;
	error("unsupported encryption algorithm");
	return nil;
}

static long
tlswrite(Chan *c, void *a, long n, vlong off)
{
	Encalg *ea;
	Hashalg *ha;
	TlsRec *volatile tr;
	Secret *volatile tos, *volatile toc;
	Block *volatile b;
	Cmdbuf *volatile cb;
	int m, ty;
	char *p, *e;
	uchar *volatile x;
	ulong offset = off;

	tr = tlsdevs[CONV(c->qid)];
	if(tr == nil)
		panic("tlswrite");

	ty = TYPE(c->qid);
	switch(ty){
	case Qdata:
	case Qhand:
		p = a;
		e = p + n;
		do{
			m = e - p;
			if(m > c->iounit)
				m = c->iounit;

			b = allocb(m);
			if(waserror()){
				freeb(b);
				nexterror();
			}
			memmove(b->wp, p, m);
			poperror();
			b->wp += m;

			tlsbwrite(c, b, offset);

			p += m;
		}while(p < e);
		return n;
	case Qctl:
		break;
	default:
		error(Ebadusefd);
		return -1;
	}

	cb = parsecmd(a, n);
	if(waserror()){
		free(cb);
		nexterror();
	}
	if(cb->nf < 1)
		error("short control request");

	/* mutex with operations using what we're about to change */
	if(waserror()){
		qunlock(&tr->in.seclock);
		qunlock(&tr->out.seclock);
		nexterror();
	}
	qlock(&tr->in.seclock);
	qlock(&tr->out.seclock);

	if(strcmp(cb->f[0], "fd") == 0){
		if(cb->nf != 3)
			error("usage: fd open-fd version");
		if(tr->c != nil)
			error(Einuse);
		m = strtol(cb->f[2], nil, 0);
		if(m < MinProtoVersion || m > MaxProtoVersion)
			error("unsupported version");
		tr->c = buftochan(cb->f[1]);
		tr->version = m;
		tlsSetState(tr, SHandshake, SClosed);
	}else if(strcmp(cb->f[0], "version") == 0){
		if(cb->nf != 2)
			error("usage: version vers");
		if(tr->c == nil)
			error("must set fd before version");
		if(tr->verset)
			error("version already set");
		m = strtol(cb->f[1], nil, 0);
		if(m < MinProtoVersion || m > MaxProtoVersion)
			error("unsupported version");
		if(m == SSL3Version)
			tr->packAAD = sslPackAAD;
		else
			tr->packAAD = tlsPackAAD;
		tr->verset = 1;
		tr->version = m;
	}else if(strcmp(cb->f[0], "secret") == 0){
		if(cb->nf != 5)
			error("usage: secret hashalg encalg isclient secretdata");
		if(tr->c == nil || !tr->verset)
			error("must set fd and version before secrets");

		if(tr->in.new != nil){
			freeSec(tr->in.new);
			tr->in.new = nil;
		}
		if(tr->out.new != nil){
			freeSec(tr->out.new);
			tr->out.new = nil;
		}

		ha = parsehashalg(cb->f[1]);
		ea = parseencalg(cb->f[2]);

		p = cb->f[4];
		m = (strlen(p)*3)/2 + 1;
		x = secalloc(m);
		tos = secalloc(sizeof(Secret));
		toc = secalloc(sizeof(Secret));
		if(waserror()){
			secfree(x);
			freeSec(tos);
			freeSec(toc);
			nexterror();
		}

		m = dec64(x, m, p, strlen(p));
		memset(p, 0, strlen(p));
		if(m < 2 * ha->maclen + 2 * ea->keylen + 2 * ea->ivlen)
			error("not enough secret data provided");

		if(!ha->initkey || !ea->initkey)
			error("misimplemented secret algorithm");

		(*ha->initkey)(ha, tr->version, tos, &x[0]);
		(*ha->initkey)(ha, tr->version, toc, &x[ha->maclen]);
		(*ea->initkey)(ea, tos, &x[2 * ha->maclen], &x[2 * ha->maclen + 2 * ea->keylen]);
		(*ea->initkey)(ea, toc, &x[2 * ha->maclen + ea->keylen], &x[2 * ha->maclen + 2 * ea->keylen + ea->ivlen]);

		if(!tos->aead_enc || !tos->aead_dec || !toc->aead_enc || !toc->aead_dec)
			if(!tos->mac || !tos->enc || !tos->dec || !toc->mac || !toc->enc || !toc->dec)
				error("missing algorithm implementations");

		if(strtol(cb->f[3], nil, 0) == 0){
			tr->in.new = tos;
			tr->out.new = toc;
		}else{
			tr->in.new = toc;
			tr->out.new = tos;
		}
		if(tr->version == SSL3Version){
			toc->unpad = sslunpad;
			tos->unpad = sslunpad;
		}else{
			toc->unpad = tlsunpad;
			tos->unpad = tlsunpad;
		}
		toc->encalg = ea->name;
		toc->hashalg = ha->name;
		tos->encalg = ea->name;
		tos->hashalg = ha->name;

		secfree(x);
		poperror();
	}else if(strcmp(cb->f[0], "changecipher") == 0){
		if(cb->nf != 1)
			error("usage: changecipher");
		if(tr->out.new == nil)
			error("cannot change cipher spec without setting secret");

		qunlock(&tr->in.seclock);
		qunlock(&tr->out.seclock);
		poperror();
		free(cb);
		poperror();

		/*
		 * the real work is done as the message is written
		 * so the stream is encrypted in sync.
		 */
		b = allocb(1);
		*b->wp++ = 1;
		tlsrecwrite(tr, RChangeCipherSpec, b);
		return n;
	}else if(strcmp(cb->f[0], "opened") == 0){
		if(cb->nf != 1)
			error("usage: opened");
		if(tr->in.sec == nil || tr->out.sec == nil)
			error("cipher must be configured before enabling data messages");
		lock(&tr->statelk);
		if(tr->state != SHandshake && tr->state != SOpen){
			unlock(&tr->statelk);
			error("cannot enable data messages");
		}
		tr->state = SOpen;
		unlock(&tr->statelk);
		tr->opened = 1;
	}else if(strcmp(cb->f[0], "alert") == 0){
		if(cb->nf != 2)
			error("usage: alert n");
		if(tr->c == nil)
			error("must set fd before sending alerts");
		m = strtol(cb->f[1], nil, 0);

		qunlock(&tr->in.seclock);
		qunlock(&tr->out.seclock);
		poperror();
		free(cb);
		poperror();

		sendAlert(tr, m);

		if(m == ECloseNotify)
			tlsclosed(tr, SLClose);

		return n;
	} else if(strcmp(cb->f[0], "debug") == 0){
		if(cb->nf == 2){
			if(strcmp(cb->f[1], "on") == 0)
				tr->debug = 1;
			else
				tr->debug = 0;
		} else
			tr->debug = 1;
	} else
		error(Ebadarg);

	qunlock(&tr->in.seclock);
	qunlock(&tr->out.seclock);
	poperror();
	free(cb);
	poperror();

	return n;
}

static void
tlsinit(void)
{
	struct Encalg *e;
	struct Hashalg *h;
	int n;
	char *cp;
	static int already;

	if(!already){
		fmtinstall('H', encodefmt);
		already = 1;
	}

	tlsdevs = smalloc(sizeof(TlsRec*) * maxtlsdevs);
	trnames = smalloc((sizeof *trnames) * maxtlsdevs);

	n = 1;
	for(e = encrypttab; e->name != nil; e++)
		n += strlen(e->name) + 1;
	cp = encalgs = smalloc(n);
	for(e = encrypttab;;){
		strcpy(cp, e->name);
		cp += strlen(e->name);
		e++;
		if(e->name == nil)
			break;
		*cp++ = ' ';
	}
	*cp = 0;

	n = 1;
	for(h = hashtab; h->name != nil; h++)
		n += strlen(h->name) + 1;
	cp = hashalgs = smalloc(n);
	for(h = hashtab;;){
		strcpy(cp, h->name);
		cp += strlen(h->name);
		h++;
		if(h->name == nil)
			break;
		*cp++ = ' ';
	}
	*cp = 0;
}

Dev tlsdevtab = {
	'a',
	"tls",

	devreset,
	tlsinit,
	devshutdown,
	tlsattach,
	tlswalk,
	tlsstat,
	tlsopen,
	devcreate,
	tlsclose,
	tlsread,
	tlsbread,
	tlswrite,
	tlsbwrite,
	devremove,
	tlswstat,
};

/* get channel associated with an fd */
static Chan*
buftochan(char *p)
{
	Chan *c;
	int fd;

	if(p == 0)
		error(Ebadarg);
	fd = strtoul(p, 0, 0);
	if(fd < 0)
		error(Ebadarg);
	c = fdtochan(fd, ORDWR, 1, 1);	/* error check and inc ref */
	return c;
}

static void
sendAlert(TlsRec *tr, int err)
{
	Block *b;
	int i, fatal;
	char *msg;

if(tr->debug)pprint("sendAlert %d\n", err);
	fatal = 1;
	msg = "tls unknown alert";
	for(i=0; i < nelem(tlserrs); i++) {
		if(tlserrs[i].err == err) {
			msg = tlserrs[i].msg;
			if(tr->version == SSL3Version)
				err = tlserrs[i].sslerr;
			else
				err = tlserrs[i].tlserr;
			fatal = tlserrs[i].fatal;
			break;
		}
	}

	if(!waserror()){
		b = allocb(2);
		*b->wp++ = fatal + 1;
		*b->wp++ = err;
		if(fatal)
			tlsSetState(tr, SAlert, SOpen|SHandshake|SRClose);
		tlsrecwrite(tr, RAlert, b);
		poperror();
	}
	if(fatal)
		tlsError(tr, msg);
}

static void
tlsError(TlsRec *tr, char *msg)
{
	int s;

if(tr->debug)pprint("tlsError %s\n", msg);
	lock(&tr->statelk);
	s = tr->state;
	tr->state = SError;
	if(s != SError){
		strncpy(tr->err, msg, ERRMAX - 1);
		tr->err[ERRMAX - 1] = '\0';
	}
	unlock(&tr->statelk);
	if(s != SError)
		alertHand(tr, msg);
}

static void
tlsSetState(TlsRec *tr, int new, int old)
{
	lock(&tr->statelk);
	if(tr->state & old)
		tr->state = new;
	unlock(&tr->statelk);
}

/* hand up a digest connection */
static void
tlshangup(TlsRec *tr)
{
	Block *b;

	qlock(&tr->in.io);
	for(b = tr->processed; b; b = tr->processed){
		tr->processed = b->next;
		freeb(b);
	}
	if(tr->unprocessed != nil){
		freeb(tr->unprocessed);
		tr->unprocessed = nil;
	}
	qunlock(&tr->in.io);

	tlsSetState(tr, SClosed, ~0);
}

static TlsRec*
newtls(Chan *ch)
{
	TlsRec **pp, **ep, **np;
	char **nmp;
	int t, newmax;

	if(waserror()) {
		unlock(&tdlock);
		nexterror();
	}
	lock(&tdlock);
	ep = &tlsdevs[maxtlsdevs];
	for(pp = tlsdevs; pp < ep; pp++)
		if(*pp == nil)
			break;
	if(pp >= ep) {
		if(maxtlsdevs >= MaxTlsDevs) {
			unlock(&tdlock);
			poperror();
			return nil;
		}
		newmax = 2 * maxtlsdevs;
		if(newmax > MaxTlsDevs)
			newmax = MaxTlsDevs;
		np = smalloc(sizeof(TlsRec*) * newmax);
		memmove(np, tlsdevs, sizeof(TlsRec*) * maxtlsdevs);
		tlsdevs = np;
		pp = &tlsdevs[maxtlsdevs];
		memset(pp, 0, sizeof(TlsRec*)*(newmax - maxtlsdevs));

		nmp = smalloc(sizeof *nmp * newmax);
		memmove(nmp, trnames, sizeof *nmp * maxtlsdevs);
		trnames = nmp;

		maxtlsdevs = newmax;
	}
	*pp = mktlsrec();
	if(pp - tlsdevs >= tdhiwat)
		tdhiwat++;
	t = TYPE(ch->qid);
	if(t == Qclonus)
		t = Qctl;
	ch->qid.path = QID(pp - tlsdevs, t);
	ch->qid.vers = 0;
	unlock(&tdlock);
	poperror();
	return *pp;
}

static TlsRec *
mktlsrec(void)
{
	TlsRec *tr;

	tr = mallocz(sizeof(*tr), 1);
	if(tr == nil)
		error(Enomem);
	tr->state = SClosed;
	tr->ref = 1;
	kstrdup(&tr->user, up->user);
	tr->perm = 0660;
	return tr;
}

static char*
tlsstate(int s)
{
	switch(s){
	case SHandshake:
		return "Handshaking";
	case SOpen:
		return "Established";
	case SRClose:
		return "RemoteClosed";
	case SLClose:
		return "LocalClosed";
	case SAlert:
		return "Alerting";
	case SError:
		return "Errored";
	case SClosed:
		return "Closed";
	}
	return "Unknown";
}

static void
freeSec(Secret *s)
{
	if(s == nil)
		return;
	secfree(s->enckey);
	secfree(s);
}

static int
noenc(Secret *, uchar *, int n)
{
	return n;
}

static int
rc4enc(Secret *sec, uchar *buf, int n)
{
	rc4(sec->enckey, buf, n);
	return n;
}

static int
tlsunpad(uchar *buf, int n, int block)
{
	int pad, nn;

	pad = buf[n - 1];
	nn = n - 1 - pad;
	if(nn <= 0 || n % block)
		return -1;
	while(--n > nn)
		if(pad != buf[n - 1])
			return -1;
	return nn;
}

static int
sslunpad(uchar *buf, int n, int block)
{
	int pad, nn;

	pad = buf[n - 1];
	nn = n - 1 - pad;
	if(nn <= 0 || n % block)
		return -1;
	return nn;
}

static int
blockpad(uchar *buf, int n, int block)
{
	int pad, nn;

	nn = n + block;
	nn -= nn % block;
	pad = nn - (n + 1);
	while(n < nn)
		buf[n++] = pad;
	return nn;
}
		
static int
des3enc(Secret *sec, uchar *buf, int n)
{
	n = blockpad(buf, n, 8);
	des3CBCencrypt(buf, n, sec->enckey);
	return n;
}

static int
des3dec(Secret *sec, uchar *buf, int n)
{
	des3CBCdecrypt(buf, n, sec->enckey);
	return (*sec->unpad)(buf, n, 8);
}

static int
aesenc(Secret *sec, uchar *buf, int n)
{
	n = blockpad(buf, n, 16);
	aesCBCencrypt(buf, n, sec->enckey);
	return n;
}

static int
aesdec(Secret *sec, uchar *buf, int n)
{
	aesCBCdecrypt(buf, n, sec->enckey);
	return (*sec->unpad)(buf, n, 16);
}

static void
ccpoly_aead_setiv(Secret *sec, uchar seq[8])
{
	uchar iv[ChachaIVlen];
	Chachastate *cs;
	int i;

	cs = (Chachastate*)sec->enckey;
	if(cs->ivwords == 2){
		chacha_setiv(cs, seq);
		return;
	}

	memmove(iv, sec->mackey, ChachaIVlen);
	for(i=0; i<8; i++)
		iv[i+(ChachaIVlen-8)] ^= seq[i];

	chacha_setiv(cs, iv);

	memset(iv, 0, sizeof(iv));
}

static int
ccpoly_aead_enc(Secret *sec, uchar *aad, int aadlen, uchar *reciv, uchar *data, int len)
{
	USED(reciv);
	ccpoly_aead_setiv(sec, aad);
	ccpoly_encrypt(data, len, aad, aadlen, data+len, sec->enckey);
	return len + sec->maclen;
}

static int
ccpoly_aead_dec(Secret *sec, uchar *aad, int aadlen, uchar *reciv, uchar *data, int len)
{
	USED(reciv);
	len -= sec->maclen;
	if(len < 0)
		return -1;
	ccpoly_aead_setiv(sec, aad);
	if(ccpoly_decrypt(data, len, aad, aadlen, data+len, sec->enckey) != 0)
		return -1;
	return len;
}

static int
aesgcm_aead_enc(Secret *sec, uchar *aad, int aadlen, uchar *reciv, uchar *data, int len)
{
	uchar iv[12];
	int i;

	memmove(iv, sec->mackey, 4+8);
	for(i=0; i<8; i++) iv[4+i] ^= aad[i];
	memmove(reciv, iv+4, 8);
	aesgcm_setiv(sec->enckey, iv, 12);
	memset(iv, 0, sizeof(iv));
	aesgcm_encrypt(data, len, aad, aadlen, data+len, sec->enckey);
	return len + sec->maclen;
}

static int
aesgcm_aead_dec(Secret *sec, uchar *aad, int aadlen, uchar *reciv, uchar *data, int len)
{
	uchar iv[12];

	len -= sec->maclen;
	if(len < 0)
		return -1;
	memmove(iv, sec->mackey, 4);
	memmove(iv+4, reciv, 8);
	aesgcm_setiv(sec->enckey, iv, 12);
	memset(iv, 0, sizeof(iv));
	if(aesgcm_decrypt(data, len, aad, aadlen, data+len, sec->enckey) != 0)
		return -1;
	return len;
}


static DigestState*
nomac(uchar *, ulong, uchar *, ulong, uchar *, DigestState *)
{
	return nil;
}

/*
 * sslmac: mac calculations for ssl 3.0 only; tls 1.0 uses the standard hmac.
 */
static DigestState*
sslmac_x(uchar *p, ulong len, uchar *key, ulong klen, uchar *digest, DigestState *s,
	DigestState*(*x)(uchar*, ulong, uchar*, DigestState*), int xlen, int padlen)
{
	int i;
	uchar pad[48], innerdigest[20];

	if(xlen > sizeof(innerdigest)
	|| padlen > sizeof(pad))
		return nil;

	if(klen>64)
		return nil;

	/* first time through */
	if(s == nil){
		for(i=0; i<padlen; i++)
			pad[i] = 0x36;
		s = (*x)(key, klen, nil, nil);
		s = (*x)(pad, padlen, nil, s);
		if(s == nil)
			return nil;
	}

	s = (*x)(p, len, nil, s);
	if(digest == nil)
		return s;

	/* last time through */
	for(i=0; i<padlen; i++)
		pad[i] = 0x5c;
	(*x)(nil, 0, innerdigest, s);
	s = (*x)(key, klen, nil, nil);
	s = (*x)(pad, padlen, nil, s);
	(*x)(innerdigest, xlen, digest, s);
	return nil;
}

static DigestState*
sslmac_sha1(uchar *p, ulong len, uchar *key, ulong klen, uchar *digest, DigestState *s)
{
	return sslmac_x(p, len, key, klen, digest, s, sha1, SHA1dlen, 40);
}

static DigestState*
sslmac_md5(uchar *p, ulong len, uchar *key, ulong klen, uchar *digest, DigestState *s)
{
	return sslmac_x(p, len, key, klen, digest, s, md5, MD5dlen, 48);
}

static int
sslPackAAD(u64int seq, uchar *hdr, uchar *aad)
{
	put64(aad, seq);
	aad[8] = hdr[0];
	aad[9] = hdr[3];
	aad[10] = hdr[4];
	return 11;
}

static int
tlsPackAAD(u64int seq, uchar *hdr, uchar *aad)
{
	put64(aad, seq);
	aad[8] = hdr[0];
	aad[9] = hdr[1];
	aad[10] = hdr[2];
	aad[11] = hdr[3];
	aad[12] = hdr[4];
	return 13;
}

static void
packMac(Secret *sec, uchar *aad, int aadlen, uchar *body, int bodylen, uchar *mac)
{
	DigestState *s;

	s = (*sec->mac)(aad, aadlen, sec->mackey, sec->maclen, nil, nil);
	(*sec->mac)(body, bodylen, sec->mackey, sec->maclen, mac, s);
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
put64(uchar *p, u64int x)
{
	put32(p, x >> 32);
	put32(p+4, x);
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

static int
get16(uchar *p)
{
	return (p[0]<<8)|p[1];
}

static char *charmap = "0123456789abcdef";

static void
pdump(int len, void *a, char *tag)
{
	uchar *p;
	int i;
	char buf[65+32];
	char *q;

	p = a;
	strcpy(buf, tag);
	while(len > 0){
		q = buf + strlen(tag);
		for(i = 0; len > 0 && i < 32; i++){
			if(*p >= ' ' && *p < 0x7f){
				*q++ = ' ';
				*q++ = *p;
			} else {
				*q++ = charmap[*p>>4];
				*q++ = charmap[*p & 0xf];
			}
			len--;
			p++;
		}
		*q = 0;

		if(len > 0)
			pprint("%s...\n", buf);
		else
			pprint("%s\n", buf);
	}
}
