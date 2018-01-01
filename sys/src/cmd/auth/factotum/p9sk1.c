/*
 * p9sk1, dp9ik - Plan 9 secret (private) key authentication.
 * dp9ik uses AuthPAK diffie hellman key exchange with the
 * auth server to protect the password derived key from offline
 * dictionary attacks.
 *
 * Client protocol:
 *	write challenge[challen]
 *	 read pakreq[ticketreqlen + pakylen]	(dp9ik only)
 *	  write paky[pakylen]
 *	 read tickreq[tickreqlen]		(p9sk1 only)
 *	write ticket + authenticator
 *	read authenticator
 *
 * Server protocol:
 * 	read challenge[challen]
 *	 write pakreq[ticketreqlen + pakylen]	(dp9ik only)
 *	  read paky[pakylen]
 *	 write tickreq[tickreqlen]		(p9sk1 only)
 *	read ticket + authenticator
 *	write authenticator
 *
 * Login protocol:
 *	write user
 *	write password
 */

#include "dat.h"

struct State
{
	Key *key;
	Ticket t;
	Ticketreq tr;
	Authkey k;
	PAKpriv p;
	char cchal[CHALLEN];
	uchar y[PAKYLEN], rand[2*NONCELEN];
	char tbuf[MAXTICKETLEN + MAXAUTHENTLEN];
	int tbuflen;
	uchar *secret;
	int speakfor;
};

enum
{
	/* client phases */
	CHaveChal,
	 CNeedPAKreq,	/* dp9ik only */
	 CHavePAKy,
	 CNeedTreq,	/* p9sk1 only */
	CHaveTicket,
	CNeedAuth,

	/* server phases */
	SNeedChal,
	 SHavePAKreq,	/* dp9ik only */
	 SNeedPAKy,
	 SHaveTreq,	/* p9sk1 only */
	SNeedTicket,
	SHaveAuth,

	/* login phases */
	SNeedUser,
	SNeedPass,
	
	Maxphase,
};

static char *phasenames[Maxphase] =
{
[CHaveChal]		"CHaveChal",
[CNeedPAKreq]		"CNeedPAKreq",
[CHavePAKy]		"CHavePAKy",
[CNeedTreq]		"CNeedTreq",
[CHaveTicket]		"CHaveTicket",
[CNeedAuth]		"CNeedAuth",

[SNeedChal]		"SNeedChal",
[SHavePAKreq]		"SHavePAKreq",
[SNeedPAKy]		"SNeedPAKy",
[SHaveTreq]		"SHaveTreq",
[SNeedTicket]		"SNeedTicket",
[SHaveAuth]		"SHaveAuth",

[SNeedUser]		"SNeedUser",
[SNeedPass]		"SNeedPass",
};

static int gettickets(State*, uchar*, char*, int);

static int
p9skinit(Proto *p, Fsstate *fss)
{
	State *s;
	int iscli, ret;
	Key *k;
	Keyinfo ki;
	Attr *attr;
	char *role;

	role = _strfindattr(fss->attr, "role");
	if(role != nil && strcmp(role, "login") == 0)
		iscli = 0;
	else if((iscli = isclient(role)) < 0)
		return failure(fss, nil);

	s = emalloc(sizeof *s);
	fss = fss;
	fss->phasename = phasenames;
	fss->maxphase = Maxphase;
	fss->proto = p;
	if(iscli){
		fss->phase = CHaveChal;
		genrandom((uchar*)s->cchal, CHALLEN);
	}else{
		fss->phase = SNeedChal;
		s->tr.type = AuthTreq;
		attr = setattr(_copyattr(fss->attr), "proto=%q", p->name);
		if(strcmp(role, "login") == 0){
			attr = setattr(attr, "role=server");
			fss->phase = SNeedUser;
		}
		mkkeyinfo(&ki, fss, attr);
		ki.user = nil;
		ret = findkey(&k, &ki, "user? dom?");
		_freeattr(attr);
		if(ret != RpcOk){
			free(s);
			return ret;
		}
		safecpy(s->tr.authid, _strfindattr(k->attr, "user"), sizeof(s->tr.authid));
		safecpy(s->tr.authdom, _strfindattr(k->attr, "dom"), sizeof(s->tr.authdom));
		s->key = k;
		memmove(&s->k, k->priv, sizeof(Authkey));
		genrandom((uchar*)s->tr.chal, sizeof s->tr.chal);
	}
	s->tbuflen = 0;
	s->secret = nil;
	fss->ps = s;
	return RpcOk;
}

static int
establish(Fsstate *fss)
{
	AuthInfo *ai;
	State *s;

	s = fss->ps;
	ai = &fss->ai;

	ai->cuid = s->t.cuid;
	ai->suid = s->t.suid;
	if(fss->proto == &dp9ik){
		static char info[] = "Plan 9 session secret";

		ai->nsecret = 256;
		ai->secret = emalloc(ai->nsecret);
		hkdf_x(	s->rand, 2*NONCELEN,
			(uchar*)info, sizeof(info)-1,
			s->t.key, NONCELEN,
			ai->secret, ai->nsecret,
			hmac_sha2_256, SHA2_256dlen);
	} else {
		ai->nsecret = 8;
		ai->secret = emalloc(ai->nsecret);
		des56to64((uchar*)s->t.key, ai->secret);
	}
	s->secret = ai->secret;

	fss->haveai = 1;
	fss->phase = Established;
	return RpcOk;
}

static int
p9skread(Fsstate *fss, void *a, uint *n)
{
	int m;
	State *s;

	s = fss->ps;
	switch(fss->phase){
	default:
		return phaseerror(fss, "read");

	case CHaveChal:
		m = CHALLEN;
		if(*n < m)
			return toosmall(fss, m);
		*n = m;
		memmove(a, s->cchal, m);
		if(fss->proto == &dp9ik)
			fss->phase = CNeedPAKreq;
		else
			fss->phase = CNeedTreq;
		return RpcOk;

	case SHavePAKreq:
		m = TICKREQLEN + PAKYLEN;
		if(*n < m)
			return toosmall(fss, m);
		s->tr.type = AuthPAK;
		*n = convTR2M(&s->tr, a, *n);
		authpak_new(&s->p, &s->k, (uchar*)a + *n, 1);
		*n += PAKYLEN;
		fss->phase = SNeedPAKy;
		return RpcOk;

	case CHavePAKy:
		m = PAKYLEN;
		if(*n < m)
			return toosmall(fss, m);
		*n = m;
		memmove(a, s->y, m);
		fss->phase = CHaveTicket;
		return RpcOk;

	case SHaveTreq:
		m = TICKREQLEN;
		if(*n < m)
			return toosmall(fss, m);
		s->tr.type = AuthTreq;
		*n = convTR2M(&s->tr, a, *n);
		fss->phase = SNeedTicket;
		return RpcOk;

	case CHaveTicket:
		m = s->tbuflen;
		if(*n < m)
			return toosmall(fss, m);
		*n = m;
		memmove(a, s->tbuf, m);
		fss->phase = CNeedAuth;
		return RpcOk;

	case SHaveAuth:
		m = s->tbuflen;
		if(*n < m)
			return toosmall(fss, m);
		*n = m;
		memmove(a, s->tbuf, m);
		return establish(fss);
	}
}

static int
p9skwrite(Fsstate *fss, void *a, uint n)
{
	int m, ret, sret;
	char tbuf[2*PAKYLEN+2*MAXTICKETLEN], *user;
	Attr *attr;
	State *s;
	Key *srvkey;
	Keyinfo ki;
	Authenticator auth;

	s = fss->ps;
	switch(fss->phase){
	default:
		return phaseerror(fss, "write");

	case SNeedChal:
		m = CHALLEN;
		if(n < m)
			return toosmall(fss, m);
		memmove(s->cchal, a, m);
		if(fss->proto == &dp9ik)
			fss->phase = SHavePAKreq;
		else
			fss->phase = SHaveTreq;
		return RpcOk;

	case CNeedPAKreq:
		m = TICKREQLEN+PAKYLEN;
		if(n < m)
			return toosmall(fss, m);
	case CNeedTreq:
		m = TICKREQLEN;
		if(n < m)
			return toosmall(fss, m);

		m = convM2TR(a, n, &s->tr);
		if(m <= 0 || s->tr.type != (fss->phase==CNeedPAKreq ? AuthPAK : AuthTreq))
			return failure(fss, Easproto);

		if(s->key != nil)
			closekey(s->key);

		attr = _delattr(_delattr(_copyattr(fss->attr), "role"), "user");
		attr = setattr(attr, "proto=%q", fss->proto->name);
		user = _strfindattr(fss->attr, "user");

		/*
		 * If our client is the user who started factotum (client==owner), then
		 * he can use whatever keys we have to speak as whoever he pleases.
		 * If, on the other hand, we're speaking on behalf of someone else,
		 * we will only vouch for their name on the local system.
		 *
		 * We do the sysuser findkey second so that if we return RpcNeedkey,
		 * the correct key information gets asked for.
		 */
		srvkey = nil;
		s->speakfor = 0;
		sret = RpcFailure;
		if(user==nil || strcmp(user, fss->sysuser) == 0){
			mkkeyinfo(&ki, fss, attr);
			ki.user = nil;
			sret = findkey(&srvkey, &ki,
				"role=speakfor dom=%q user?", s->tr.authdom);
		}
		if(user != nil)
			attr = setattr(attr, "user=%q", user);
		mkkeyinfo(&ki, fss, attr);
		ret = findkey(&s->key, &ki,
			"role=client dom=%q %s", s->tr.authdom, fss->proto->keyprompt);
		if(ret == RpcOk)
			closekey(srvkey);
		else if(sret == RpcOk){
			s->key = srvkey;
			s->speakfor = 1;
		}else if(ret == RpcConfirm || sret == RpcConfirm){
			_freeattr(attr);
			return RpcConfirm;
		}else{
			_freeattr(attr);
			return ret;
		}

		/* fill in the rest of the request */
		safecpy(s->tr.hostid, _strfindattr(s->key->attr, "user"), sizeof s->tr.hostid);
		if(s->speakfor)
			safecpy(s->tr.uid, fss->sysuser, sizeof s->tr.uid);
		else
			safecpy(s->tr.uid, s->tr.hostid, sizeof s->tr.uid);

		/* get tickets from authserver or invent if we can */
		memmove(&s->k, s->key->priv, sizeof(Authkey));
		if(fss->phase == CNeedPAKreq)
			ret = gettickets(s, (uchar*)a + m, tbuf, sizeof(tbuf));
		else
			ret = gettickets(s, nil, tbuf, sizeof(tbuf));
		if(ret <= 0){
			_freeattr(attr);
			return failure(fss, nil);
		}
		m = convM2T(tbuf, ret, &s->t, &s->k);
		if(m <= 0 || (fss->proto == &dp9ik && s->t.form == 0)){
			_freeattr(attr);
			return failure(fss, Easproto);
		}
		if(s->t.num != AuthTc){
			if(s->key->successes == 0 && !s->speakfor)
				disablekey(s->key);
			if(askforkeys && !s->speakfor){
				snprint(fss->keyinfo, sizeof fss->keyinfo,
					"%A %s", attr, fss->proto->keyprompt);
				_freeattr(attr);
				return RpcNeedkey;
			}else{
				_freeattr(attr);
				return failure(fss, Ebadkey);
			}
		}
		s->key->successes++;
		_freeattr(attr);
		ret -= m;
		memmove(s->tbuf, tbuf+m, ret);
		genrandom(s->rand, NONCELEN);
		auth.num = AuthAc;
		memmove(auth.chal, s->tr.chal, CHALLEN);
		memmove(auth.rand, s->rand, NONCELEN);
		ret += convA2M(&auth, s->tbuf+ret, sizeof(s->tbuf)-ret, &s->t);
		s->tbuflen = ret;
		if(fss->phase == CNeedPAKreq)
			fss->phase = CHavePAKy;
		else
			fss->phase = CHaveTicket;
		return RpcOk;

	case SNeedPAKy:
		m = PAKYLEN;
		if(n < m)
			return toosmall(fss, m);
		if(authpak_finish(&s->p, &s->k, (uchar*)a))
			return failure(fss, Easproto);
		fss->phase = SNeedTicket;
		return RpcOk;

	case SNeedTicket:
		m = convM2T(a, n, &s->t, &s->k);
		if(m <= 0 || convM2A((char*)a+m, n-m, &auth, &s->t) <= 0)
			return toosmall(fss, MAXTICKETLEN + MAXAUTHENTLEN);
		if(fss->proto == &dp9ik && s->t.form == 0)
			return failure(fss, Easproto);
		if(s->t.num != AuthTs
		|| tsmemcmp(s->t.chal, s->tr.chal, CHALLEN) != 0)
			return failure(fss, Easproto);
		if(auth.num != AuthAc
		|| tsmemcmp(auth.chal, s->tr.chal, CHALLEN) != 0)
			return failure(fss, Easproto);
		memmove(s->rand, auth.rand, NONCELEN);
		genrandom(s->rand + NONCELEN, NONCELEN);
		auth.num = AuthAs;
		memmove(auth.chal, s->cchal, CHALLEN);
		memmove(auth.rand, s->rand + NONCELEN, NONCELEN);
		s->tbuflen = convA2M(&auth, s->tbuf, sizeof(s->tbuf), &s->t);
		fss->phase = SHaveAuth;
		return RpcOk;

	case CNeedAuth:
		m = convM2A(a, n, &auth, &s->t);
		if(m <= 0)
			return toosmall(fss, -m);
		if(auth.num != AuthAs
		|| tsmemcmp(auth.chal, s->cchal, CHALLEN) != 0)
			return failure(fss, Easproto);
		memmove(s->rand+NONCELEN, auth.rand, NONCELEN);
		return establish(fss);

	case SNeedUser:
		if(n >= sizeof(s->tr.hostid))
			return failure(fss, "user id too long");
		strncpy(s->tr.hostid, a, n);
		s->tr.hostid[n] = 0;
		strncpy(s->tr.uid, a, n);
		s->tr.uid[n] = 0;
		fss->phase = SNeedPass;
		return RpcOk;

	case SNeedPass:
		{
			Authkey ks;
			uchar tk[NONCELEN];
			char pass[PASSWDLEN];

			if(n >= sizeof(pass))
				return failure(fss, "password too long");

			/* save server key */
			memmove(&ks, &s->k, sizeof(ks));

			/* derive client key */
			strncpy(pass, a, n);
			pass[n] = 0;
			passtokey(&s->k, pass);
			memset(pass, 0, sizeof(pass));

			if(fss->proto == &dp9ik){
				uchar ys[PAKYLEN];
				PAKpriv ps;

				authpak_hash(&s->k, s->tr.hostid);

				authpak_new(&ps, &ks, ys, 1);
				ret = gettickets(s, ys, tbuf, sizeof(tbuf));
				if(ret > 0 && authpak_finish(&ps, &ks, s->y))
					return failure(fss, Easproto);
			} else {
				ret = gettickets(s, nil, tbuf, sizeof(tbuf));
			}
			if(ret <= 0)
				return failure(fss, nil);

			/* verify client ticket */
			m = convM2T(tbuf, ret, &s->t, &s->k);
			if(m <= 0 || (fss->proto == &dp9ik && s->t.form == 0))
				return failure(fss, Easproto);
			if(s->t.num != AuthTc || tsmemcmp(s->t.chal, s->tr.chal, CHALLEN) != 0)
				return failure(fss, Ebadkey);
			memmove(tk, s->t.key, sizeof(tk));

			/* restore sever key */
			memmove(&s->k, &ks, sizeof(ks));

			/* verify server ticket */
			m = convM2T(tbuf+m, ret-m, &s->t, &s->k);
			if(m <= 0 || (fss->proto == &dp9ik && s->t.form == 0))
				return failure(fss, Easproto);
			if(s->t.num != AuthTs || tsmemcmp(s->t.chal, s->tr.chal, CHALLEN) != 0
			|| tsmemcmp(tk, s->t.key, sizeof(tk)) != 0)
				return failure(fss, Ebadkey);

		}
		genrandom(s->rand, 2*NONCELEN);
		return establish(fss);
	}
}

static void
p9skclose(Fsstate *fss)
{
	State *s;

	s = fss->ps;
	if(s->secret != nil){
		free(s->secret);
		s->secret = nil;
	}
	if(s->key != nil){
		closekey(s->key);
		s->key = nil;
	}
	memset(s, 0, sizeof(State));
	free(s);
}

static int
p9skaddkey(Key *k, int before)
{
	Authkey *akey;
	char *s, *u;

	u = _strfindattr(k->attr, "user");
	if(u == nil){
		werrstr("no user attribute");
		return -1;
	}
	akey = emalloc(sizeof(Authkey));
	if(s = _strfindattr(k->privattr, "!hex")){
		if(k->proto == &dp9ik){
			if(dec16(akey->aes, AESKEYLEN, s, strlen(s)) != AESKEYLEN){
				free(akey);
				werrstr("malformed key data");
				return -1;
			}
		} else {
			if(dec16((uchar*)akey->des, DESKEYLEN, s, strlen(s)) != DESKEYLEN){
				free(akey);
				werrstr("malformed key data");
				return -1;
			}
		}
	}else if(s = _strfindattr(k->privattr, "!password")){
		passtokey(akey, s);
	}else{
		werrstr("no key data");
		free(akey);
		return -1;
	}
	if(k->proto == &dp9ik)
		authpak_hash(akey, u);
	else
		memset(akey->aes, 0, AESKEYLEN);	/* don't attempt AuthPAK for p9sk1 key */
	k->priv = akey;
	return replacekey(k, before);
}

static void
p9skclosekey(Key *k)
{
	if(k->priv == nil)
		return;
	memset(k->priv, 0, sizeof(Authkey));
	free(k->priv);
}

static int
mkservertickets(State *s, uchar *y, char *tbuf, int tbuflen)
{
	Ticketreq *tr = &s->tr;
	Ticket t;
	int ret;

	if(strcmp(tr->authid, tr->hostid) != 0)
		return -1;
/* this keeps creating accounts on martha from working.  -- presotto
	if(strcmp(tr->uid, "none") == 0)
		return -1;
*/
	memset(&t, 0, sizeof(t));
	ret = 0;
	if(y != nil){
		t.form = 1;
		authpak_new(&s->p, &s->k, s->y, 0);
		authpak_finish(&s->p, &s->k, y);
	}
	memmove(t.chal, tr->chal, CHALLEN);
	strcpy(t.cuid, tr->uid);
	strcpy(t.suid, tr->uid);
	genrandom((uchar*)t.key, sizeof(t.key));
	t.num = AuthTc;
	ret += convT2M(&t, tbuf+ret, tbuflen-ret, &s->k);
	t.num = AuthTs;
	ret += convT2M(&t, tbuf+ret, tbuflen-ret, &s->k);
	memset(&t, 0, sizeof(t));

	return ret;
}

static int
getastickets(State *s, uchar *y, char *tbuf, int tbuflen)
{
	Ticketreq *tr = &s->tr;
 	int asfd, rv;
	char *dom;

	if((dom = _strfindattr(s->key->attr, "dom")) == nil){
		werrstr("auth key has no domain");
		return -1;
	}
	asfd = _authdial(dom);
	if(asfd < 0)
		return -1;
	alarm(30*1000);
	if(y != nil){
		rv = -1;
		s->tr.type = AuthPAK;
		if(_asrequest(asfd, tr) != 0 || write(asfd, y, PAKYLEN) != PAKYLEN)
			goto Out;

		authpak_new(&s->p, &s->k, (uchar*)tbuf, 1);
		if(write(asfd, tbuf, PAKYLEN) != PAKYLEN)
			goto Out;

		if(_asrdresp(asfd, tbuf, 2*PAKYLEN) != 2*PAKYLEN)
			goto Out;
	
		memmove(s->y, tbuf, PAKYLEN);
		if(authpak_finish(&s->p, &s->k, (uchar*)tbuf+PAKYLEN))
			goto Out;
	}
	s->tr.type = AuthTreq;
	rv = _asgetticket(asfd, tr, tbuf, tbuflen);
Out:
	alarm(0);
	close(asfd);
	return rv;
}

static int
gettickets(State *s, uchar *y, char *tbuf, int tbuflen)
{
	int ret;

	if(s->tr.authdom[0] == 0
	|| s->tr.authid[0] == 0
	|| s->tr.hostid[0] == 0
	|| s->tr.uid[0] == 0){
		werrstr("bad ticket request");
		return -1;
	}
	ret = getastickets(s, y, tbuf, tbuflen);
	if(ret > 0)
		return ret;
	return mkservertickets(s, y, tbuf, tbuflen);
}

Proto p9sk1 = {
.name=	"p9sk1",
.init=	p9skinit,
.write=	p9skwrite,
.read=	p9skread,
.close=	p9skclose,
.addkey=	p9skaddkey,
.closekey=	p9skclosekey,
.keyprompt=	"user? !password?"
};

Proto dp9ik = {
.name=	"dp9ik",
.init=	p9skinit,
.write=	p9skwrite,
.read=	p9skread,
.close=	p9skclose,
.addkey=	p9skaddkey,
.closekey=	p9skclosekey,
.keyprompt=	"user? !password?"
};
