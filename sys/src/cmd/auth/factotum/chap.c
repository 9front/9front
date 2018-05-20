/*
 * CHAP, MSCHAP
 * 
 * The client does not authenticate the server
 *
 * Client protocol:
 *	write Chapchal 
 *	read response Chapreply or MSchaprely structure
 *
 * Server protocol:
 *	read challenge: 8 bytes binary (or 16 bytes for mschapv2)
 *	write user: utf8
 *	write dom: utf8 (ntlm)
 *	write response: Chapreply or MSchapreply structure
 *	... retry another user
 */

#include <ctype.h>
#include "dat.h"

enum {
	ChapChallen = 8,
	ChapResplen = 16,

	/* Microsoft auth constants */
	MShashlen = 16,
	MSchallen = 8,
	MSresplen = 24,

	MSchallenv2 = 16,

	Chapreplylen = MD5LEN+1,
	MSchapreplylen = 24+24,
};

static int dochal(State *s);
static int doreply(State *s, uchar *reply, int nreply);
static int dochap(char *passwd, int id, char chal[ChapChallen], uchar *resp, int resplen);
static int domschap(char *passwd, uchar chal[MSchallen], uchar *resp, int resplen);
static int dontlmv2(char *passwd, char *user, char *dom, uchar chal[MSchallen], uchar *resp, int resplen);
static void nthash(uchar hash[MShashlen], char *passwd);

struct State
{
	int astype;
	int asfd;
	Key *key;
	Authkey k;
	Ticket t;
	Ticketreq tr;
	int nchal;
	char chal[16];
	int nresp;
	uchar resp[4096];
	char err[ERRMAX];
	char user[64];
	char dom[DOMLEN];
	uchar secret[16+20];	/* for mschap: MPPE Master secret + authenticator (v2) */
	int nsecret;
};

enum
{
	CNeedChal,
	CHaveResp,

	SHaveChal,
	SNeedUser,
	SNeedDom,
	SNeedResp,

	Maxphase
};

static char *phasenames[Maxphase] =
{
[CNeedChal]	"CNeedChal",
[CHaveResp]	"CHaveResp",

[SHaveChal]	"SHaveChal",
[SNeedUser]	"SNeedUser",
[SNeedDom]	"SNeedDom",
[SNeedResp]	"SNeedResp",
};

static int
chapinit(Proto *p, Fsstate *fss)
{
	int iscli, ret;
	State *s;

	if((iscli = isclient(_strfindattr(fss->attr, "role"))) < 0)
		return failure(fss, nil);

	s = emalloc(sizeof *s);
	s->nresp = 0;
	s->nsecret = 0;
	strcpy(s->user, "none");
	fss->phasename = phasenames;
	fss->maxphase = Maxphase;
	s->asfd = -1;
	if(p == &mschapv2) {
		s->nchal = MSchallenv2;
		s->astype = AuthMSchapv2;
	} else if(p == &mschap){
		s->nchal = MSchallen;
		s->astype = AuthMSchap;
	} else if(p == &ntlm || p == &ntlmv2){
		s->nchal = MSchallen;
		s->astype = AuthNTLM;
	} else {
		s->nchal = ChapChallen;
		s->astype = AuthChap;
	}
	if(iscli)
		fss->phase = CNeedChal;
	else{
		if((ret = findp9authkey(&s->key, fss)) != RpcOk){
			free(s);
			return ret;
		}
		if(dochal(s) < 0){
			free(s);
			return failure(fss, nil);
		}
		fss->phase = SHaveChal;
	}

	fss->ps = s;
	return RpcOk;
}

static void
chapclose(Fsstate *fss)
{
	State *s;

	s = fss->ps;
	if(s->asfd >= 0){
		close(s->asfd);
		s->asfd = -1;
	}
	free(s);
}

static int
chapwrite(Fsstate *fss, void *va, uint n)
{
	int ret, nreply;
	char *a, *pass;
	Key *k;
	Keyinfo ki;
	State *s;
	Chapreply *cr;
	MSchapreply *mcr;
	OChapreply *ocr;
	OMSchapreply *omcr;
	NTLMreply *ntcr;
	uchar pchal[MSchallenv2];
	uchar digest[SHA1dlen];
	uchar reply[4096];
	char *user, *dom;
	DigestState *ds;

	s = fss->ps;
	a = va;
	switch(fss->phase){
	default:
		return phaseerror(fss, "write");

	case CNeedChal:
		ret = findkey(&k, mkkeyinfo(&ki, fss, nil), "%s", fss->proto->keyprompt);
		if(ret != RpcOk)
			return ret;
		user = nil;
		pass = _strfindattr(k->privattr, "!password");
		if(pass == nil){
			closekey(k);
			return failure(fss, "key has no password");
		}
		s->nsecret = 0;
		s->nresp = 0;
		memset(s->resp, 0, sizeof(s->resp));
		setattrs(fss->attr, k->attr);
		switch(s->astype){
		case AuthNTLM:
			if(n < MSchallen)
				break;
			if(fss->proto == &ntlmv2){
				user = _strfindattr(fss->attr, "user");
				if(user == nil)
					break;
				dom = _strfindattr(fss->attr, "windom");
				if(dom == nil)
					dom = "";
				s->nresp = dontlmv2(pass, user, dom, (uchar*)a, s->resp, sizeof(s->resp));
			} else {
				s->nresp = domschap(pass, (uchar*)a, s->resp, sizeof(s->resp));
			}
			break;
		case AuthMSchap:
			if(n < MSchallen)
				break;
			s->nresp = domschap(pass, (uchar*)a, s->resp, sizeof(s->resp));
			nthash(digest, pass);
			md4(digest, 16, digest, nil);
			ds = sha1(digest, 16, nil, nil);
			sha1(digest, 16, nil, ds);
			sha1((uchar*)a, 8, s->secret, ds);
			s->nsecret = 16;
			break;
		case AuthMSchapv2:
			if(n < MSchallenv2)
				break;
			user = _strfindattr(fss->attr, "user");
			if(user == nil)
				break;
			genrandom((uchar*)pchal, MSchallenv2);

			/* ChallengeHash() */
			ds = sha1(pchal, MSchallenv2, nil, nil);
			ds = sha1((uchar*)a, MSchallenv2, nil, ds);
			sha1((uchar*)user, strlen(user), reply, ds);

			s->nresp = domschap(pass, reply, s->resp, sizeof(s->resp));
			if(s->nresp <= 0)
				break;

			mcr = (MSchapreply*)s->resp;
			memset(mcr->LMresp, 0, sizeof(mcr->LMresp));
			memmove(mcr->LMresp, pchal, MSchallenv2);

			nthash(digest, pass);
			md4(digest, 16, digest, nil);
			ds = sha1(digest, 16, nil, nil);
			sha1((uchar*)mcr->NTresp, MSresplen, nil, ds);
			sha1((uchar*)"This is the MPPE Master Key", 27, s->secret, ds);

			ds = sha1(digest, 16, nil, nil);
			sha1((uchar*)mcr->NTresp, MSresplen, nil, ds);
			sha1((uchar*)"Magic server to client signing constant", 39, digest, ds);

			ds = sha1(digest, 20, nil, nil);
			sha1(reply, 8, nil, ds);	/* ChallngeHash */
			sha1((uchar*)"Pad to make it do more than one iteration", 41, s->secret+16, ds);
			s->nsecret = 16+20;
			break;
		case AuthChap:
			if(n < ChapChallen+1)
				break;
			s->nresp = dochap(pass, *a, a+1, s->resp, sizeof(s->resp));
			break;
		}
		closekey(k);
		if(s->nresp <= 0)
			return failure(fss, "chap botch");
		if(user != nil)
			safecpy(s->user, user, sizeof s->user);
		fss->phase = CHaveResp;
		return RpcOk;

	case SNeedUser:
		if(n >= sizeof s->user)
			return failure(fss, "user name too long");
		memmove(s->user, va, n);
		s->user[n] = '\0';
		fss->phase = (s->astype == AuthNTLM)? SNeedDom: SNeedResp;
		return RpcOk;

	case SNeedDom:
		if(n >= sizeof s->dom)
			return failure(fss, "domain name too long");
		memmove(s->dom, va, n);
		s->dom[n] = '\0';
		fss->phase = SNeedResp;
		return RpcOk;

	case SNeedResp:
		switch(s->astype){
		default:
			return failure(fss, "chap internal botch");
		case AuthChap:
			if(n < Chapreplylen)
				return failure(fss, "did not get Chapreply");
			cr = (Chapreply*)va;
			nreply = OCHAPREPLYLEN;
			memset(reply, 0, nreply);
			ocr = (OChapreply*)reply;
			strecpy(ocr->uid, ocr->uid+sizeof(ocr->uid), s->user);
			ocr->id = cr->id;
			memmove(ocr->resp, cr->resp, sizeof(ocr->resp));
			break;
		case AuthMSchap:
		case AuthMSchapv2:
			if(n < MSchapreplylen)
				return failure(fss, "did not get MSchapreply");
			mcr = (MSchapreply*)va;
			nreply = OMSCHAPREPLYLEN;
			memset(reply, 0, nreply);
			omcr = (OMSchapreply*)reply;
			strecpy(omcr->uid, omcr->uid+sizeof(omcr->uid), s->user);
			memmove(omcr->LMresp, mcr->LMresp, sizeof(omcr->LMresp));
			memmove(omcr->NTresp, mcr->NTresp, sizeof(mcr->NTresp));
			break;
		case AuthNTLM:
			if(n < MSchapreplylen)
				return failure(fss, "did not get MSchapreply");
			if(n > sizeof(reply)+MSchapreplylen-NTLMREPLYLEN)
				return failure(fss, "MSchapreply too long");
			mcr = (MSchapreply*)va;
			nreply = n+NTLMREPLYLEN-MSchapreplylen;
			memset(reply, 0, nreply);
			ntcr = (NTLMreply*)reply;
			ntcr->len[0] = nreply;
			ntcr->len[1] = nreply>>8;
			strecpy(ntcr->uid, ntcr->uid+sizeof(ntcr->uid), s->user);
			strecpy(ntcr->dom, ntcr->dom+sizeof(ntcr->dom), s->dom);
			memmove(ntcr->LMresp, mcr->LMresp, sizeof(ntcr->LMresp));
			memmove(ntcr->NTresp, mcr->NTresp, n+sizeof(mcr->NTresp)-MSchapreplylen);
			break;
		}
		if(doreply(s, reply, nreply) < 0){
			fss->phase = SNeedUser;
			return failure(fss, nil);
		}
		fss->phase = Established;
		fss->ai.cuid = s->t.cuid;
		fss->ai.suid = s->t.suid;
		fss->ai.secret = s->secret;
		fss->ai.nsecret = s->nsecret;
		fss->haveai = 1;
		return RpcOk;
	}
}

static int
chapread(Fsstate *fss, void *va, uint *n)
{
	State *s;

	s = fss->ps;
	switch(fss->phase){
	default:
		return phaseerror(fss, "read");

	case CHaveResp:
		if(*n > s->nresp)
			*n = s->nresp;
		memmove(va, s->resp, *n);
		fss->phase = Established;
		if(s->nsecret == 0){
			fss->haveai = 0;
			return RpcOk;
		}
		fss->ai.cuid = s->user;
		fss->ai.suid = "none";	/* server not authenticated */
		fss->ai.secret = s->secret;
		fss->ai.nsecret = s->nsecret;
		fss->haveai = 1;
		return RpcOk;

	case SHaveChal:
		if(*n > s->nchal)
			*n = s->nchal;
		memmove(va, s->chal, *n);
		fss->phase = SNeedUser;
		return RpcOk;
	}
}

static int
dochal(State *s)
{
	char *dom, *user;
	int n;

	s->asfd = -1;

	/* send request to authentication server and get challenge */
	if((dom = _strfindattr(s->key->attr, "dom")) == nil
	|| (user = _strfindattr(s->key->attr, "user")) == nil){
		werrstr("chap/dochal cannot happen");
		goto err;
	}
	memmove(&s->k, s->key->priv, sizeof(Authkey));

	memset(&s->tr, 0, sizeof(s->tr));
	safecpy(s->tr.authdom, dom, sizeof(s->tr.authdom));
	safecpy(s->tr.hostid, user, sizeof(s->tr.hostid));
	s->tr.type = s->astype;

	s->asfd = _authreq(&s->tr, &s->k);
	if(s->asfd < 0)
		goto err;
	
	alarm(30*1000);
	n = readn(s->asfd, s->chal, s->nchal);
	alarm(0);
	if(n != s->nchal)
		goto err;

	return 0;

err:
	if(s->asfd >= 0)
		close(s->asfd);
	s->asfd = -1;
	return -1;
}

static int
doreply(State *s, uchar *reply, int nreply)
{
	int n;
	Authenticator a;

	alarm(30*1000);
	if(write(s->asfd, reply, nreply) != nreply){
		alarm(0);
		goto err;
	}
	n = _asgetresp(s->asfd, &s->t, &a, &s->k);
	alarm(0);
	if(n < 0){
		/* leave connection open so we can try again */
		return -1;
	}
	close(s->asfd);
	s->asfd = -1;

	if(s->t.num != AuthTs
	|| tsmemcmp(s->t.chal, s->tr.chal, sizeof(s->t.chal)) != 0){
		if(s->key->successes == 0)
			disablekey(s->key);
		werrstr(Easproto);
		return -1;
	}
	if(a.num != AuthAc
	|| tsmemcmp(a.chal, s->tr.chal, sizeof(a.chal)) != 0){
		werrstr(Easproto);
		return -1;
	}
	s->key->successes++;
	s->nsecret = 0;
	if(s->t.form != 0){
		if(s->astype == AuthMSchap || s->astype == AuthMSchapv2){
			memmove(s->secret, s->t.key, 16);
			if(s->astype == AuthMSchapv2){
				memmove(s->secret+16, a.rand, 20);
				s->nsecret = 16+20;
			} else
				s->nsecret = 16;
		}
	}
	return 0;
err:
	if(s->asfd >= 0)
		close(s->asfd);
	s->asfd = -1;
	return -1;
}

Proto chap = {
.name=	"chap",
.init=	chapinit,
.write=	chapwrite,
.read=	chapread,
.close=	chapclose,
.addkey= replacekey,
.keyprompt= "!password?"
};

Proto mschap = {
.name=	"mschap",
.init=	chapinit,
.write=	chapwrite,
.read=	chapread,
.close=	chapclose,
.addkey= replacekey,
.keyprompt= "!password?"
};

Proto mschapv2 = {
.name=	"mschapv2",
.init=	chapinit,
.write=	chapwrite,
.read=	chapread,
.close=	chapclose,
.addkey= replacekey,
.keyprompt= "user? !password?"
};

Proto ntlm = {
.name=	"ntlm",
.init=	chapinit,
.write=	chapwrite,
.read=	chapread,
.close=	chapclose,
.addkey= replacekey,
.keyprompt= "user? !password?"
};

Proto ntlmv2 = {
.name=	"ntlmv2",
.init=	chapinit,
.write=	chapwrite,
.read=	chapread,
.close=	chapclose,
.addkey= replacekey,
.keyprompt= "user? windom? !password?"
};

static void
nthash(uchar hash[MShashlen], char *passwd)
{
	DigestState *ds;
	uchar b[2];
	Rune r;

	ds = md4(nil, 0, nil, nil);
	while(*passwd){
		passwd += chartorune(&r, passwd);
		b[0] = r & 0xff;
		b[1] = r >> 8;
		md4(b, 2, nil, ds);
	}
	md4(nil, 0, hash, ds);
}

static void
ntv2hash(uchar hash[MShashlen], char *passwd, char *user, char *dom)
{
	uchar v1hash[MShashlen];
	DigestState *ds;
	uchar b[2];
	Rune r;

	nthash(v1hash, passwd);

	/*
	 * Some documentation insists that the username must be forced to
	 * uppercase, but the domain name should not be. Other shows both
	 * being forced to uppercase. I am pretty sure this is irrevevant as the
	 * domain name passed from the remote server always seems to be in
	 * uppercase already.
	 */
        ds = hmac_md5(nil, 0, v1hash, sizeof(v1hash), nil, nil);
	while(*user){
		user += chartorune(&r, user);
		r = toupperrune(r);
		b[0] = r & 0xff;
		b[1] = r >> 8;
        	hmac_md5(b, 2, v1hash, sizeof(v1hash), nil, ds);
	}
	while(*dom){
		dom += chartorune(&r, dom);
		b[0] = r & 0xff;
		b[1] = r >> 8;
        	hmac_md5(b, 2, v1hash, sizeof(v1hash), nil, ds);
	}
        hmac_md5(nil, 0, v1hash, sizeof(v1hash), hash, ds);
}

static void
desencrypt(uchar data[8], uchar key[7])
{
	ulong ekey[32];

	key_setup(key, ekey);
	block_cipher(ekey, data, 0);
}

static void
lmhash(uchar hash[MShashlen], char *passwd)
{
	uchar buf[14];
	char *stdtext = "KGS!@#$%";
	int i;

	memset(buf, 0, sizeof(buf));
	strncpy((char*)buf, passwd, sizeof(buf));
	for(i=0; i<sizeof(buf); i++)
		if(buf[i] >= 'a' && buf[i] <= 'z')
			buf[i] += 'A' - 'a';

	memcpy(hash, stdtext, 8);
	memcpy(hash+8, stdtext, 8);

	desencrypt(hash, buf);
	desencrypt(hash+8, buf+7);
}

static void
mschalresp(uchar resp[MSresplen], uchar hash[MShashlen], uchar chal[MSchallen])
{
	int i;
	uchar buf[21];

	memset(buf, 0, sizeof(buf));
	memcpy(buf, hash, MShashlen);

	for(i=0; i<3; i++) {
		memmove(resp+i*MSchallen, chal, MSchallen);
		desencrypt(resp+i*MSchallen, buf+i*7);
	}
}

static int
domschap(char *passwd, uchar chal[MSchallen], uchar *resp, int resplen)
{
	uchar hash[MShashlen];
	MSchapreply *r;

	r = (MSchapreply*)resp;
	if(resplen < MSchapreplylen)
		return 0;

	lmhash(hash, passwd);
	mschalresp((uchar*)r->LMresp, hash, chal);

	nthash(hash, passwd);
	mschalresp((uchar*)r->NTresp, hash, chal);

	return MSchapreplylen;
}

static int
dontlmv2(char *passwd, char *user, char *dom, uchar chal[MSchallen], uchar *resp, int resplen)
{
	uchar hash[MShashlen], *p, *e;
	MSchapreply *r;
	DigestState *s;
	uvlong t;
	Rune rr;
	int nb;

	ntv2hash(hash, passwd, user, dom);

	r = (MSchapreply*)resp;
	p = (uchar*)r->NTresp+16;
	e = resp + resplen;

	if(p+2+2+4+8+8+4+4+4+4 > e)
		return 0;	

	*p++ = 1;		/* 8bit: response type */
	*p++ = 1;		/* 8bit: max response type understood by client */

	*p++ = 0;		/* 16bit: reserved */
	*p++ = 0;

	*p++ = 0;		/* 32bit: unknown */
	*p++ = 0;
	*p++ = 0;
	*p++ = 0;

	t = time(nil);
	t += 11644473600LL;
	t *= 10000000LL;

	*p++ = t;		/* 64bit: time in NT format */
	*p++ = t >> 8;
	*p++ = t >> 16;
	*p++ = t >> 24;
	*p++ = t >> 32;
	*p++ = t >> 40;
	*p++ = t >> 48;
	*p++ = t >> 56;

	genrandom(p, 8);
	p += 8;			/* 64bit: client nonce */

	*p++ = 0;		/* 32bit: unknown data */
	*p++ = 0;
	*p++ = 0;
	*p++ = 0;

	*p++ = 2;		/* AvPair Domain */
	*p++ = 0;
	*p++ = 0;		/* length */
	*p++ = 0;
	nb = 0;
	while(*dom){
		dom += chartorune(&rr, dom);
		if(p+2+4+4 > e)
			return 0;
		*p++ = rr & 0xFF;
		*p++ = rr >> 8;
		nb += 2;
	}
	p[-nb - 2] = nb & 0xFF;
	p[-nb - 1] = nb >> 8;

	*p++ = 0;		/* AvPair EOF */
	*p++ = 0;
	*p++ = 0;
	*p++ = 0;
	
	*p++ = 0;		/* 32bit: unknown data */
	*p++ = 0;
	*p++ = 0;
	*p++ = 0;

	/*
	 * LmResponse = Cat(HMAC_MD5(LmHash, Cat(SC, CC)), CC)
	 */
	s = hmac_md5(chal, 8, hash, MShashlen, nil, nil);
	genrandom((uchar*)r->LMresp+16, 8);
	hmac_md5((uchar*)r->LMresp+16, 8, hash, MShashlen, (uchar*)r->LMresp, s);

	/*
	 * NtResponse = Cat(HMAC_MD5(NtHash, Cat(SC, NtBlob)), NtBlob)
	 */
	s = hmac_md5(chal, 8, hash, MShashlen, nil, nil);
	hmac_md5((uchar*)r->NTresp+16, p - ((uchar*)r->NTresp+16), hash, MShashlen, (uchar*)r->NTresp, s);

	return p - resp;
}

static int
dochap(char *passwd, int id, char chal[ChapChallen], uchar *resp, int resplen)
{
	char buf[1+ChapChallen+MAXNAMELEN+1];
	int n;

	if(resplen < ChapResplen)
		return 0;

	memset(buf, 0, sizeof buf);
	*buf = id;
	n = strlen(passwd);
	if(n > MAXNAMELEN)
		n = MAXNAMELEN-1;
	strncpy(buf+1, passwd, n);
	memmove(buf+1+n, chal, ChapChallen);
	md5((uchar*)buf, 1+n+ChapChallen, resp, nil);

	return ChapResplen;
}
