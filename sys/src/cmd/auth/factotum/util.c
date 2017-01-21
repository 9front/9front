#include "dat.h"

static char secstore[100];   /* server name */
static uchar zeros[16];

/* bind in the default network and cs */
static int
bindnetcs(void)
{
	int srvfd;

	if(access("/net/tcp", AEXIST) < 0)
		bind("#I", "/net", MBEFORE);

	if(access("/net/cs", AEXIST) < 0){
		if((srvfd = open("#s/cs", ORDWR)) >= 0){
			if(mount(srvfd, -1, "/net", MBEFORE, "") >= 0)
				return 0;
			close(srvfd);
		}
		return -1;
	}
	return 0;
}

/* get all auth= attribute values from /net/ndb */
static void
netndbauthaddr(void)
{
	enum { CHUNK = 1024 };
	char *b, *p, *e;
	int fd, n, m, i;

	if((fd = open("/net/ndb", OREAD)) < 0)
		return;
	m = 0;
	b = nil;
	for(;;){
		if((p = realloc(b, m+CHUNK+1)) == nil)
			break;
		b = p;
		if((n = read(fd, b+m, CHUNK)) <= 0)
			break;
		m += n;
	}
	close(fd);
	if(b == nil)
		return;
	b[m] = '\0';

	i = 0;
	e = b;
	while((p = strstr(e, "auth=")) != nil){
		if(p > e && strchr("\n\t ", p[-1]) == nil){
			e = p + strlen("auth=");
			continue;
		}
		p += strlen("auth=");
		for(e = p; *e != '\0'; e++)
			if(strchr("\n\t ", *e) != nil)
				break;
		if(*e == '\0')
			break;
		*e++ = '\0';
		if(*p == '\0')
			continue;
		authaddr[i++] = estrdup(p);
		if(i >= nelem(authaddr)-1)
			break;
	}
	authaddr[i] = nil;
	free(b);
}

int
_authdial(char *authdom)
{
	int i, fd;

	alarm(30*1000);
	if(bindnetcs()>=0)
		fd = authdial(nil, authdom);
	else {
		/*
		 * If we failed to mount /srv/cs, assume that
		 * we're still bootstrapping the system and dial
		 * the one auth server passed to us on the command line or
		 * look for auth= attributes in /net/ndb.
		 * In normal operation, it is important *not* to do this,
		 * because the bootstrap auth server is only good for
		 * a single auth domain.
		 *
		 * The ticket request code should really check the
		 * remote authentication domain too.
		 */
		fd = -1;
		if(authaddr[0] == nil)
			netndbauthaddr();
		for(i = 0; fd < 0 && authaddr[i] != nil; i++){
			fd = dial(netmkaddr(authaddr[i], "tcp", "567"), 0, 0, 0);
			if(fd < 0)
				fd = dial(netmkaddr(authaddr[i], "il", "566"), 0, 0, 0);
		}
	}
	alarm(0);
	return fd;
}

int
_authreq(Ticketreq *tr, Authkey *k)
{
	int fd;

	fd = _authdial(tr->authdom);
	if(fd < 0)
		return -1;
	alarm(30*1000);
	if(tsmemcmp(k->aes, zeros, AESKEYLEN) != 0){
		if(_asgetpakkey(fd, tr, k) < 0){
			alarm(0);
			close(fd);
			return -1;
		}
	}
	if(_asrequest(fd, tr) < 0){
		alarm(0);
		close(fd);
		return -1;
	}
	alarm(0);
	return fd;
}

/*
 *  prompt user for a key.  don't care about memory leaks, runs standalone
 */
static Attr*
promptforkey(char *params)
{
	char *v;
	int fd;
	Attr *a, *attr;
	char *def;

	fd = open("/dev/cons", ORDWR);
	if(fd < 0)
		sysfatal("opening /dev/cons: %r");

	attr = _parseattr(params);
	fprint(fd, "\n!Adding key:");
	for(a=attr; a; a=a->next)
		if(a->type != AttrQuery && a->name[0] != '!')
			fprint(fd, " %q=%q", a->name, a->val);
	fprint(fd, "\n");

	for(a=attr; a; a=a->next){
		v = a->name;
		if(a->type != AttrQuery || v[0]=='!')
			continue;
		def = nil;
		if(strcmp(v, "user") == 0)
			def = getuser();
		a->val = readcons(v, def, 0);
		if(a->val == nil)
			sysfatal("user terminated key input");
		a->type = AttrNameval;
	}
	for(a=attr; a; a=a->next){
		v = a->name;
		if(a->type != AttrQuery || v[0]!='!')
			continue;
		def = nil;
		if(strcmp(v+1, "user") == 0)
			def = getuser();
		a->val = readcons(v+1, def, 1);
		if(a->val == nil)
			sysfatal("user terminated key input");
		a->type = AttrNameval;
	}
	fprint(fd, "!\n");
	close(fd);
	return attr;
}

/*
 *  send a key to the mounted factotum
 */
static int
sendkey(Attr *attr)
{
	int fd, rv;
	char buf[1024];

	fd = open("/mnt/factotum/ctl", ORDWR);
	if(fd < 0)
		sysfatal("opening /mnt/factotum/ctl: %r");
	rv = fprint(fd, "key %A\n", attr);
	read(fd, buf, sizeof buf);
	close(fd);
	return rv;
}

/* askuser */
void
askuser(char *params)
{
	Attr *attr;

	attr = promptforkey(params);
	if(attr == nil)
		sysfatal("no key supplied");
	if(sendkey(attr) < 0)
		sysfatal("sending key to factotum: %r");
}

ulong conftaggen;
int
canusekey(Fsstate *fss, Key *k)
{
	int i;

	if(_strfindattr(k->attr, "confirm")){
		for(i=0; i<fss->nconf; i++)
			if(fss->conf[i].key == k)
				return fss->conf[i].canuse;
		if(fss->nconf%16 == 0)
			fss->conf = erealloc(fss->conf, (fss->nconf+16)*(sizeof(fss->conf[0])));
		incref(k);
		fss->conf[fss->nconf].key = k;
		fss->conf[fss->nconf].canuse = -1;
		fss->conf[fss->nconf].tag = conftaggen++;
		fss->nconf++;
		return -1;
	}
	return 1;
}

/* closekey */
void
closekey(Key *k)
{
	if(k == nil)
		return;
	if(decref(k))
		return;
	if(k->proto && k->proto->closekey)
		(*k->proto->closekey)(k);
	_freeattr(k->attr);
	_freeattr(k->privattr);
	k->attr = (void*)~1;
	k->privattr = (void*)~1;
	k->proto = nil;
	free(k);
}

static uchar*
pstring(uchar *p, uchar *e, char *s)
{
	uint n;

	if(p == nil)
		return nil;
	if(s == nil)
		s = "";
	n = strlen(s);
	if(p+n+BIT16SZ >= e)
		return nil;
	PBIT16(p, n);
	p += BIT16SZ;
	memmove(p, s, n);
	p += n;
	return p;
}

static uchar*
pcarray(uchar *p, uchar *e, uchar *s, uint n)
{
	if(p == nil)
		return nil;
	if(s == nil){
		if(n > 0)
			sysfatal("pcarray");
		s = (uchar*)"";
	}
	if(p+n+BIT16SZ >= e)
		return nil;
	PBIT16(p, n);
	p += BIT16SZ;
	memmove(p, s, n);
	p += n;
	return p;
}

uchar*
convAI2M(AuthInfo *ai, uchar *p, int n)
{
	uchar *e = p+n;

	p = pstring(p, e, ai->cuid);
	p = pstring(p, e, ai->suid);
	p = pstring(p, e, ai->cap);
	p = pcarray(p, e, ai->secret, ai->nsecret);
	return p;
}

int
failure(Fsstate *s, char *fmt, ...)
{
	char e[ERRMAX];
	va_list arg;

	if(fmt == nil)
		rerrstr(s->err, sizeof(s->err));
	else {
		va_start(arg, fmt);
		vsnprint(e, sizeof e, fmt, arg);
		va_end(arg);
		strecpy(s->err, s->err+sizeof(s->err), e);
		errstr(e, sizeof e);
	}
	flog("%d: failure %s", s->seqnum, s->err);
	return RpcFailure;
}

static int
hasqueries(Attr *a)
{
	for(; a; a=a->next)
		if(a->type == AttrQuery)
			return 1;
	return 0;
}

char *ignored[] = {
	"role",
	"disabled",
};

static int
ignoreattr(char *s)
{
	int i;

	for(i=0; i<nelem(ignored); i++)
		if(strcmp(ignored[i], s)==0)
			return 1;
	return 0;
}

Keyinfo*
mkkeyinfo(Keyinfo *k, Fsstate *fss, Attr *attr)
{
	memset(k, 0, sizeof *k);
	k->fss = fss;
	k->user = fss->sysuser;
	if(attr)
		k->attr = attr;
	else
		k->attr = fss->attr;
	return k;
}

int
findkey(Key **ret, Keyinfo *ki, char *fmt, ...)
{
	int i, s, nmatch;
	char buf[1024], *p, *who;
	va_list arg;
	Attr *a, *attr0, *attr1, *attr2, *attr3, **l;
	Key *k;

	*ret = nil;

	who = ki->user;
	attr0 = ki->attr;
	if(fmt){
		va_start(arg, fmt);
		vseprint(buf, buf+sizeof buf, fmt, arg);
		va_end(arg);
		attr1 = _parseattr(buf);
	}else
		attr1 = nil;

	if(who && strcmp(who, owner) == 0)
		who = nil;

	if(who){
		snprint(buf, sizeof buf, "owner=%q", who);
		attr2 = _parseattr(buf);
		attr3 = _parseattr("owner=*");
	}else
		attr2 = attr3 = nil;

	p = _strfindattr(attr0, "proto");
	if(p == nil)
		p = _strfindattr(attr1, "proto");
	if(p && findproto(p) == nil){
		werrstr("unknown protocol %s", p);
		_freeattr(attr1);
		_freeattr(attr2);
		_freeattr(attr3);
		return failure(ki->fss, nil);
	}

	qlock(ring);
	nmatch = 0; 
	for(i=0; i<ring->nkey; i++){
		k = ring->key[i];
		if(_strfindattr(k->attr, "disabled") && !ki->usedisabled)
			continue;
		if(matchattr(attr0, k->attr, k->privattr) && matchattr(attr1, k->attr, k->privattr)){
			/* check ownership */
			if(!matchattr(attr2, k->attr, nil) && !matchattr(attr3, k->attr, nil))
				continue;
			if(nmatch++ < ki->skip)
				continue;
			if(!ki->noconf){
				switch(canusekey(ki->fss, k)){
				case -1:
					qunlock(ring);
					_freeattr(attr1);
					_freeattr(attr2);
					_freeattr(attr3);
					return RpcConfirm;
				case 0:
					continue;
				case 1:
					break;
				}
			}
			_freeattr(attr1);
			_freeattr(attr2);
			_freeattr(attr3);
			incref(k);
			*ret = k;
			qunlock(ring);
			return RpcOk;
		}
	}
	qunlock(ring);
	flog("%d: no key matches %A %A %A %A", ki->fss->seqnum, attr0, attr1, attr2, attr3);
	werrstr("no key matches %A %A", attr0, attr1);
	_freeattr(attr2);
	_freeattr(attr3);
	s = RpcFailure;
	if(askforkeys && who==nil && (hasqueries(attr0) || hasqueries(attr1))){
		if(nmatch == 0){
			attr0 = _copyattr(attr0);
			for(l=&attr0; *l; l=&(*l)->next)
				;
			*l = attr1;
			for(l=&attr0; *l; ){
				if(ignoreattr((*l)->name)){
					a = *l;
					*l = (*l)->next;
					a->next = nil;
					_freeattr(a);
				}else
					l = &(*l)->next;
			}
			attr0 = sortattr(attr0);
			snprint(ki->fss->keyinfo, sizeof ki->fss->keyinfo, "%A", attr0);
			_freeattr(attr0);
			attr1 = nil;	/* attr1 was linked to attr0 */
		}else
			ki->fss->keyinfo[0] = '\0';
		s = RpcNeedkey;
	}
	_freeattr(attr1);
	if(s == RpcFailure)
		return failure(ki->fss, nil);	/* loads error string */
	return s;
}

int
findp9authkey(Key **k, Fsstate *fss)
{
	char *dom;
	Keyinfo ki;
	int rv;

	/*
	 * We don't use fss->attr here because we don't
	 * care about what the user name is set to, for instance.
	 */
	mkkeyinfo(&ki, fss, nil);
	ki.attr = nil;
	ki.user = nil;
	dom = _strfindattr(fss->attr, "dom");
	if(dom != nil)
		rv = findkey(k, &ki, "proto=dp9ik dom=%q role=server user?", dom);
	else
		rv = findkey(k, &ki, "proto=dp9ik role=server dom? user?");
	if(rv != RpcOk){
		if(dom != nil)
			rv = findkey(k, &ki, "proto=p9sk1 dom=%q role=server user?", dom);
		else
			rv = findkey(k, &ki, "proto=p9sk1 role=server dom? user?");
	}
	return rv;
}

Proto*
findproto(char *name)
{
	int i;

	for(i=0; prototab[i]; i++)
		if(strcmp(name, prototab[i]->name) == 0)
			return prototab[i];
	return nil;
}

int
getnvramkey(int flag)
{
	Nvrsafe safe;
	char *s;

	memset(&safe, 0, sizeof safe);
	/*
	 * readnvram can return -1 meaning nvram wasn't written,
	 * but safe still holds good data.
	 */
	if(readnvram(&safe, flag)<0 && safe.authid[0]==0) 
		return -1;

	if(tsmemcmp(safe.machkey, zeros, DESKEYLEN) != 0){
		s = smprint("key proto=p9sk1 user=%q dom=%q !hex=%.*H !password=______", 
			safe.authid, safe.authdom, DESKEYLEN, safe.machkey);
		if(s != nil){
			ctlwrite(s, 0);
			memset(s, 0, strlen(s));
			free(s);
		}
	}
	if(tsmemcmp(safe.aesmachkey, zeros, AESKEYLEN) != 0){
		s = smprint("key proto=dp9ik user=%q dom=%q !hex=%.*H !password=______", 
			safe.authid, safe.authdom, AESKEYLEN, safe.aesmachkey);
		if(s != nil){
			ctlwrite(s, 0);
			memset(s, 0, strlen(s));
			free(s);
		}
	}
	writehostowner(safe.authid);
	memset(&safe, 0, sizeof safe);

	return 0;
}

int
isclient(char *role)
{
	if(role == nil){
		werrstr("role not specified");
		return -1;
	}
	if(strcmp(role, "server") == 0)
		return 0;
	if(strcmp(role, "client") == 0)
		return 1;
	werrstr("unknown role %q", role);
	return -1;
}

static int
hasname(Attr *a0, Attr *a1, char *name)
{
	return _findattr(a0, name) || _findattr(a1, name);
}

static int
hasnameval(Attr *a0, Attr *a1, char *name, char *val)
{
	Attr *a;

	for(a=_findattr(a0, name); a; a=_findattr(a->next, name))
		if(strcmp(a->val, val) == 0)
			return 1;
	for(a=_findattr(a1, name); a; a=_findattr(a->next, name))
		if(strcmp(a->val, val) == 0)
			return 1;
	return 0;
}

int
matchattr(Attr *pat, Attr *a0, Attr *a1)
{
	int type;

	for(; pat; pat=pat->next){
		type = pat->type;
		if(ignoreattr(pat->name))
			type = AttrDefault;
		switch(type){
		case AttrQuery:		/* name=something be present */
			if(!hasname(a0, a1, pat->name))
				return 0;
			break;
		case AttrNameval:	/* name=val must be present */
			if(!hasnameval(a0, a1, pat->name, pat->val))
				return 0;
			break;
		case AttrDefault:	/* name=val must be present if name=anything is present */
			if(hasname(a0, a1, pat->name) && !hasnameval(a0, a1, pat->name, pat->val))
				return 0;
			break;
		}
	}
	return 1;		
}

/*
 *  keep caphash fd open since opens of it could be disabled
 */
static int caphashfd;

void
initcap(void)
{
	caphashfd = open("#¤/caphash", OWRITE);
//	if(caphashfd < 0)
//		fprint(2, "%s: opening #¤/caphash: %r\n", argv0);
}

/*
 *  create a change uid capability 
 */
char*
mkcap(char *from, char *to)
{
	uchar rand[20];
	char *cap;
	char *key;
	int nfrom, nto;
	uchar hash[SHA1dlen];

	if(caphashfd < 0)
		return nil;

	/* create the capability */
	nto = strlen(to);
	nfrom = strlen(from);
	cap = emalloc(nfrom+1+nto+1+sizeof(rand)*3+1);
	sprint(cap, "%s@%s", from, to);
	genrandom(rand, sizeof(rand));
	key = cap+nfrom+1+nto+1;
	enc64(key, sizeof(rand)*3, rand, sizeof(rand));

	/* hash the capability */
	hmac_sha1((uchar*)cap, strlen(cap), (uchar*)key, strlen(key), hash, nil);

	/* give the kernel the hash */
	key[-1] = '@';
	if(write(caphashfd, hash, SHA1dlen) < 0){
		free(cap);
		return nil;
	}

	return cap;
}

int
phaseerror(Fsstate *s, char *op)
{
	char tmp[32];

	werrstr("protocol phase error: %s in state %s", op, phasename(s, s->phase, tmp));	
	return RpcPhase;
}

char*
phasename(Fsstate *fss, int phase, char *tmp)
{
	char *name;

	if(fss->phase == Broken)
		name = "Broken";
	else if(phase == Established)
		name = "Established";
	else if(phase == Notstarted)
		name = "Notstarted";
	else if(phase < 0 || phase >= fss->maxphase
	|| (name = fss->phasename[phase]) == nil){
		sprint(tmp, "%d", phase);
		name = tmp;
	}
	return name;
}

/*
 *  get host owner and set it
 */
void
promptforhostowner(void)
{
	char *p;

	/* hack for bitsy; can't prompt during boot */
	p = getenv("user");
	while(p == nil || *p == 0){
		free(p);
		p = readcons("user", "glenda", 0);
		if(p == nil)
			return;
	}
	writehostowner(p);
	free(p);
}

/*
 * Insert a key into the keyring.
 * If the public attributes are identical to some other key, replace that one.
 */
int
replacekey(Key *kn, int before)
{
	int i;
	Key *k;

	qlock(ring);
	incref(kn);
	for(i=0; i<ring->nkey; i++){
		k = ring->key[i];
		if(matchattr(kn->attr, k->attr, nil) && matchattr(k->attr, kn->attr, nil)){
			ring->key[i] = kn;
			qunlock(ring);
			closekey(k);
			return 0;
		}
	}
	if(ring->nkey%16 == 0)
		ring->key = erealloc(ring->key, (ring->nkey+16)*sizeof(ring->key[0]));
	if(before){
		memmove(ring->key+1, ring->key, ring->nkey*sizeof ring->key[0]);
		ring->key[0] = kn;
		ring->nkey++;
	}else
		ring->key[ring->nkey++] = kn;
	qunlock(ring);
	return 0;
}

char*
safecpy(char *to, char *from, int n)
{
	memset(to, 0, n);
	if(n == 1)
		return to;
	if(from==nil)
		sysfatal("safecpy called with from==nil, pc=%#p",
			getcallerpc(&to));
	strncpy(to, from, n-1);
	return to;
}

Attr*
setattr(Attr *a, char *fmt, ...)
{
	char buf[1024];
	va_list arg;
	Attr *b;

	va_start(arg, fmt);
	vseprint(buf, buf+sizeof buf, fmt, arg);
	va_end(arg);
	b = _parseattr(buf);
	a = setattrs(a, b);
	setmalloctag(a, getcallerpc(&a));
	_freeattr(b);
	return a;
}

/*
 *  add attributes in list b to list a.  If any attributes are in
 *  both lists, replace those in a by those in b.
 */
Attr*
setattrs(Attr *a, Attr *b)
{
	int found;
	Attr **l, *freea;

	for(; b; b=b->next){
		found = 0;
		for(l=&a; *l; ){
			if(strcmp(b->name, (*l)->name) == 0){
				switch(b->type){
				case AttrNameval:
					if(!found){
						found = 1;
						free((*l)->val);
						(*l)->val = estrdup(b->val);
						(*l)->type = AttrNameval;
						l = &(*l)->next;
					}else{
						freea = *l;
						*l = (*l)->next;
						freea->next = nil;
						_freeattr(freea);
					}
					break;
				case AttrQuery:
					goto continue2;
				}
			}else
				l = &(*l)->next;
		}
		if(found == 0){
			*l = _mkattr(b->type, b->name, b->val, nil);
			setmalloctag(*l, getcallerpc(&a));
		}
continue2:;
	}
	return a;		
}

void
setmalloctaghere(void *v)
{
	setmalloctag(v, getcallerpc(&v));
}

Attr*
sortattr(Attr *a)
{
	int i;
	Attr *anext, *a0, *a1, **l;

	if(a == nil || a->next == nil)
		return a;

	/* cut list in halves */
	a0 = nil;
	a1 = nil;
	i = 0;
	for(; a; a=anext){
		anext = a->next;
		if(i++%2){
			a->next = a0;
			a0 = a;
		}else{
			a->next = a1;
			a1 = a;
		}
	}

	/* sort */
	a0 = sortattr(a0);
	a1 = sortattr(a1);

	/* merge */
	l = &a;
	while(a0 || a1){
		if(a1==nil){
			anext = a0;
			a0 = a0->next;
		}else if(a0==nil){
			anext = a1;
			a1 = a1->next;
		}else if(strcmp(a0->name, a1->name) < 0){
			anext = a0;
			a0 = a0->next;
		}else{
			anext = a1;
			a1 = a1->next;
		}
		*l = anext;
		l = &(*l)->next;
	}
	*l = nil;
	return a;
}

int
toosmall(Fsstate *fss, uint n)
{
	fss->rpc.nwant = n;
	return RpcToosmall;
}

void
writehostowner(char *owner)
{
	int fd;
	char *s;

	if((s = strchr(owner,'@')) != nil)
		*s = 0;
	fd = open("#c/hostowner", OWRITE);
	if(fd >= 0){
		if(fprint(fd, "%s", owner) < 0)
			fprint(2, "factotum: setting #c/hostowner to %q: %r\n",
				owner);
		close(fd);
	}
}

int
attrnamefmt(Fmt *fmt)
{
	char *b, buf[1024], *ebuf;
	Attr *a;

	ebuf = buf+sizeof buf;
	b = buf;
	strcpy(buf, " ");
	for(a=va_arg(fmt->args, Attr*); a; a=a->next){
		if(a->name == nil)
			continue;
		b = seprint(b, ebuf, " %q?", a->name);
	}
	return fmtstrcpy(fmt, buf+1);
}

void
disablekey(Key *k)
{
	Attr *a;

	if(sflag)	/* not on servers */
		return;
	for(a=k->attr; a; a=a->next){
		if(a->type==AttrNameval && strcmp(a->name, "disabled") == 0)
			return;
		if(a->next == nil)
			break;
	}
	if(a)
		a->next = _mkattr(AttrNameval, "disabled", "by.factotum", nil);
	else
		k->attr = _mkattr(AttrNameval, "disabled", "by.factotum", nil);	/* not reached: always a proto attribute */
}
