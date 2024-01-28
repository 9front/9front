#include <u.h>
#include <libc.h>
#include <json.h>
#include <mp.h>
#include <libsec.h>
#include <auth.h>
#include <authsrv.h>

typedef struct Hdr Hdr;

#pragma varargck	type	"E"	char*

struct Hdr {
	char	*name;
	char	*val;
	int	nval;
};

#define Keyspec		"proto=rsa service=acme role=sign hash=sha256 acct=%s"
#define Contenttype	"contenttype application/jose+json"
#define between(x,min,max)	(((min-1-x) & (x-max-1))>>8)
int	debug;
int	(*challengefn)(char*, char*, char*, int*);
char	*keyspec;
char	*provider = "https://acme-v02.api.letsencrypt.org/directory"; /* default endpoint */
char	*challengecmd;
char	*challengeout;
char	*keyid;
char	*epnewnonce;
char	*epnewacct;
char	*epneworder;
char	*eprevokecert;
char	*epkeychange;
char	*jwsthumb;
JSON	*jwskey;

#define dprint(...) if(debug)fprint(2, __VA_ARGS__);

char*
evsmprint(char *fmt, va_list ap)
{
	char *r;

	if((r = vsmprint(fmt, ap)) == nil)
		abort();
	return r;
}

char*
esmprint(char *fmt, ...)
{
	va_list ap;
	char *r;

	va_start(ap, fmt);
	r = evsmprint(fmt, ap);
	va_end(ap);
	return r;
}

int
encurl64chr(int o)
{
	int c;

	c  = between(o,  0, 25) & ('A'+o);
	c |= between(o, 26, 51) & ('a'+(o-26));
	c |= between(o, 52, 61) & ('0'+(o-52));
	c |= between(o, 62, 62) & ('-');
	c |= between(o, 63, 63) & ('_');
	return c;
}
char*
encurl64(void *in, int n)
{
	int lim;
	char *out, *p;

	lim = 4*n/3 + 5;
	if((out = malloc(lim)) == nil)
		abort();
	enc64x(out, lim, in, n, encurl64chr);
	if((p = strchr(out, '=')) != nil)
		*p = 0;
	return out;
}

char*
signRS256(char *hdr, char *prot)
{
	uchar hash[SHA2_256dlen];
	DigestState *s;
	AuthRpc *rpc;
	int afd;
	char *r;

	if((afd = open("/mnt/factotum/rpc", ORDWR|OCEXEC)) < 0)
		return nil;
	if((rpc = auth_allocrpc(afd)) == nil){
		close(afd);
		return nil;
	}
	if(auth_rpc(rpc, "start", keyspec, strlen(keyspec)) != ARok){
		auth_freerpc(rpc);
		close(afd);
		return nil;
	}

	s = sha2_256((uchar*)hdr, strlen(hdr), nil, nil);
	s = sha2_256((uchar*)".", strlen("."), nil, s);
	sha2_256((uchar*)prot, strlen(prot), hash, s);

	if(auth_rpc(rpc, "write", hash, sizeof(hash)) != ARok)
		sysfatal("sign: write hash: %r");
	if(auth_rpc(rpc, "read", nil, 0) != ARok)
		sysfatal("sign: read sig: %r");
	r = encurl64(rpc->arg, rpc->narg);
	auth_freerpc(rpc);
	close(afd);
	return r;	
}

/*
 * Reads all available data from an fd.
 * guarantees returned value is terminated.
 */
static void*
slurp(int fd, int *n)
{
	char *b;
	int r, sz;

	*n = 0;
	sz = 32;
	if((b = malloc(sz)) == nil)
		abort();
	while(1){
		if(*n + 1 == sz){
			sz *= 2;
			if((b = realloc(b, sz)) == nil)
				abort();
		}
		r = read(fd, b + *n, sz - *n - 1);
		if(r == 0)
			break;
		if(r == -1){
			free(b);
			return nil;
		}
		*n += r;
	}
	b[*n] = 0;
	return b;
}
		
static int
webopen(char *url, char *dir, int ndir)
{
	char buf[16];
	int n, cfd, conn;

	if((cfd = open("/mnt/web/clone", ORDWR|OCEXEC)) == -1)
		return -1;
	if((n = read(cfd, buf, sizeof(buf)-1)) == -1)
		goto Error;
	buf[n] = 0;
	conn = atoi(buf);

	if(fprint(cfd, "url %s", url) == -1)
		goto Error;
	snprint(dir, ndir, "/mnt/web/%d", conn);
	return cfd;
Error:
	close(cfd);
	return -1;
}

static char*
get(char *url, int *n)
{
	char *r, dir[64], path[80];
	int cfd, dfd;

	r = nil;
	dfd = -1;
	if((cfd = webopen(url, dir, sizeof(dir))) == -1)
		goto Error;
	snprint(path, sizeof(path), "%s/%s", dir, "body");
	if((dfd = open(path, OREAD|OCEXEC)) == -1)
		goto Error;
	r = slurp(dfd, n);
Error:
	if(dfd != -1) close(dfd);
	if(cfd != -1) close(cfd);
	return r;
}

static char*
post(char *url, char *buf, int nbuf, int *nret, Hdr *h)
{
	char *r, dir[64], path[80];
	int cfd, dfd, hfd, ok;

	r = nil;
	ok = 0;
	dfd = -1;
	hfd = -1;
	if((cfd = webopen(url, dir, sizeof(dir))) == -1)
		goto Error;
	if(write(cfd, Contenttype, strlen(Contenttype)) == -1)
		goto Error;
	snprint(path, sizeof(path), "%s/%s", dir, "postbody");
	if((dfd = open(path, OWRITE|OCEXEC)) == -1)
		goto Error;
	if(write(dfd, buf, nbuf) != nbuf)
		goto Error;
	close(dfd);
	snprint(path, sizeof(path), "%s/%s", dir, "body");
	if((dfd = open(path, OREAD|OCEXEC)) == -1)
		goto Error;
	if(h != nil){
		snprint(path, sizeof(path), "%s/%s", dir, h->name);
		if((hfd = open(path, OREAD|OCEXEC)) == -1)
			goto Error;
		if((h->val = slurp(hfd, &h->nval)) == nil)
			goto Error;
	}
	if((r = slurp(dfd, nret)) == nil)
		goto Error;
	ok = 1;
Error:
	if(hfd != -1) close(hfd);
	if(dfd != -1) close(dfd);
	if(cfd != -1) close(cfd);
	if(!ok && h != nil){
		free(h->val);
		h->val = nil;
		h->nval = 0;
	}
	return r;
}

static int
endpoints(void)
{
	JSON *j;
	JSONEl *e;
	char *s;
	int n;

	if((s = get(provider, &n)) == nil)
		sysfatal("get %s: %r", provider);
	if((j = jsonparse(s)) == nil)
		sysfatal("parse endpoints: %r");
	if(j->t != JSONObject)
		sysfatal("expected object");
	for(e = j->first; e != nil; e = e->next){
		if(e->val->t != JSONString)
			continue;
		if(strcmp(e->name, "keyChange") == 0)
			epkeychange = strdup(e->val->s);
		else if(strcmp(e->name, "newAccount") == 0)
			epnewacct = strdup(e->val->s);
		else if(strcmp(e->name, "newNonce") == 0)
			epnewnonce = strdup(e->val->s);
		else if(strcmp(e->name, "newOrder") == 0)
			epneworder = strdup(e->val->s);
		else if(strcmp(e->name, "revokeCert") == 0)
			eprevokecert = strdup(e->val->s);
	}
	jsonfree(j);
	free(s);
	if(epnewnonce==nil|| epnewacct==nil || epneworder==nil
	|| eprevokecert==nil || epkeychange==nil)
		sysfatal("missing directory entries");
	return 0;
}

static char*
getnonce(void)
{
	char *r, dir[64], path[80];
	int n, cfd, dfd, hfd;

	r = nil;
	dfd = -1;
	hfd = -1;
	if((cfd = webopen(epnewnonce, dir, sizeof(dir))) == -1)
		goto Error;
	fprint(cfd, "request HEAD");

	snprint(path, sizeof(path), "%s/%s", dir, "body");
	if((dfd = open(path, OREAD|OCEXEC)) == -1)
		goto Error;
	snprint(path, sizeof(path), "%s/%s", dir, "replaynonce");
	if((hfd = open(path, OREAD|OCEXEC)) == -1)
		goto Error;
	r = slurp(hfd, &n);
Error:
	if(hfd != -1)
		close(hfd);
	if(dfd != -1)
		close(dfd);
	close(cfd);
	return r;
}

char*
jwsenc(char *hdr, char *msg, int *nbuf)
{
	char *h, *m, *s, *r;

	h = encurl64(hdr, strlen(hdr));
	m = encurl64(msg, strlen(msg));
	s = signRS256(h, m);
	if(s == nil)
		return nil;

	r = esmprint(
		"{\n"
		"\"protected\": \"%s\",\n"
		"\"payload\": \"%s\",\n"
		"\"signature\": \"%s\"\n"
		"}\n",
		h, m, s);
	*nbuf = strlen(r);
	free(h);
	free(m);
	free(s);

	return r;
}

char*
jwsheader(char *url)
{
	char *nonce;

	if((nonce = getnonce()) == nil)
		sysfatal("get nonce: %r");
	return esmprint(
		"{"
		"\"alg\": \"RS256\","
		"\"nonce\": \"%E\","
		"\"kid\": \"%E\","
		"\"url\": \"%E\""
		"}",
		nonce, keyid, url);
}

char*
jwsrequest(char *url, int *nresp, Hdr *h, char *fmt, ...)
{
	char *hdr, *msg, *req, *resp;
	int nreq;
	va_list ap;

	va_start(ap, fmt);
	hdr = jwsheader(url);
	msg = evsmprint(fmt, ap);
	req = jwsenc(hdr, msg, &nreq);
	dprint("req=\"%s\"\n", req);
	resp = post(url, req, nreq, nresp, h);
	free(hdr);
	free(req);
	free(msg);
	va_end(ap);
	dprint("resp=%s\n", resp);
	return resp;
}

static void
mkaccount(char *addr)
{
	char *nonce, *hdr, *msg, *req, *resp;
	int nreq, nresp;
	Hdr loc = { "location" };

	if((nonce = getnonce()) == nil)
		sysfatal("get nonce: %r");
	hdr = esmprint(
		"{"
		"\"alg\": \"RS256\","
		"\"jwk\": %J,"
		"\"nonce\": \"%E\","
		"\"url\": \"%E\""
		"}",
		jwskey, nonce, epnewacct);
	msg = esmprint(
		"{"
		"\"termsOfServiceAgreed\": true,"
		"\"contact\": [\"mailto:%E\"]"
		"}",
		addr);
	free(nonce);
	if((req = jwsenc(hdr, msg, &nreq)) == nil)
		sysfatal("failed to sign: %r");
	dprint("req=\"%s\"\n", req);

	if((resp = post(epnewacct, req, nreq, &nresp, &loc)) == nil)
		sysfatal("failed req: %r");
	dprint("resp=%s, loc=%s\n", resp, loc.val);
	keyid = loc.val;
}

static char*
idn(char *dom)
{
	static char buf[256];
	if(utf2idn(dom, buf, sizeof(buf)) >= 0)
		return buf;
	return dom;
}

static JSON*
submitorder(char **dom, int ndom, Hdr *hdr)
{
	char *req, *resp, *sep, rbuf[8192];
	int nresp, i;
	JSON *r;

	sep = "";
	req = seprint(rbuf, rbuf+sizeof(rbuf),
		"{"
		"  \"identifiers\": [");
	for(i = 0; i < ndom; i++){
		req = seprint(req, rbuf+sizeof(rbuf),
			"%s{"
			"  \"type\": \"dns\","
			"  \"value\": \"%E\""
			"}",
			sep, idn(dom[i]));
		sep = ",";
	}
	req = seprint(req, rbuf+sizeof(rbuf),
		"  ]"
		"}");
	if(req - rbuf < 2)
		sysfatal("truncated order");
	resp = jwsrequest(epneworder, &nresp, hdr, "%s", rbuf);
	if(resp == nil)
		sysfatal("submit order: %r");
	if((r = jsonparse(resp)) == nil)
		sysfatal("parse order: %r");
	free(resp);
	return r;
}

static void
hashauthbuf(char *buf, int nbuf)
{
	uchar hash[SHA2_256dlen];
	char *enc;

	sha2_256((uchar*)buf, strlen(buf), hash, nil);
	if((enc = encurl64(hash, sizeof(hash))) == nil)
		sysfatal("hashbuf: %r");
	if(snprint(buf, nbuf, "%s", enc) != strlen(enc))
		sysfatal("hashbuf: buffer too small, truncated");
	free(enc);
}

static int
runchallenge(char *ty, char *dom, char *tok, int *matched)
{
	char auth[1024];
	Waitmsg *w;
	int pid;

	snprint(auth, sizeof(auth), "%s.%s", tok, jwsthumb);
	if(strcmp(ty, "dns-01") == 0)
		hashauthbuf(auth, sizeof(auth));

	pid = fork();
	switch(pid){
	case -1:
		return -1;
	case 0:
		dup(1, 2);
		execl(challengecmd, challengecmd, ty, dom, tok, auth, nil);
		sysfatal("%s: %r", challengecmd);
	}

	while((w = wait()) != nil){
		if(w->pid != pid){
			free(w);
			continue;
		}
		if(w->msg[0] == '\0'){
			free(w);
			*matched = 1;
			return 0;
		}
		werrstr("%s", w->msg);
		free(w);
		return -1;
	}
	return -1;
}

static int
httpchallenge(char *ty, char *, char *tok, int *matched)
{
	char path[1024];
	int fd, r;

	if(strcmp(ty, "http-01") != 0)
		return -1;
	*matched = 1;

	snprint(path, sizeof(path), "%s/%s", challengeout, tok);
	if((fd = create(path, OWRITE|OCEXEC, 0666)) == -1)
		return -1;
	r = fprint(fd, "%s.%s\n", tok, jwsthumb);
	close(fd);
	return r;
}

static int
dnschallenge(char *ty, char *dom, char *tok, int *matched)
{
	char auth[1024];
	int fd;

	if(strcmp(ty, "dns-01") != 0)
		return -1;
	*matched = 1;

	snprint(auth, sizeof(auth), "%s.%s", tok, jwsthumb);
	hashauthbuf(auth, sizeof(auth));

	if((fd = create(challengeout, OWRITE|OCEXEC, 0666)) == -1){
		werrstr("could not create challenge: %r");
		return -1;
	}
	if(fprint(fd,"dom=_acme-challenge.%s soa=\n\ttxt=\"%s\"\n", dom, auth) == -1){
		werrstr("could not write challenge: %r");
		close(fd);
		return -1;
	}
	close(fd);

	if((fd = open("/net/dns", OWRITE|OCEXEC)) == -1){
		werrstr("could not open dns ctl: %r");
		return -1;
	}
	if(fprint(fd, "refresh") == -1){
		werrstr("could not write dns refresh: %r");
		close(fd);
		return -1;
	}
	close(fd);

	return 0;
}

static int
challenge(JSON *j, char *authurl, JSON *id, char *dom[], int ndom, int *matched, int wild)
{
	JSON *dn, *ty, *url, *tok, *poll, *state;
	char *resp;
	int i, nresp;

	if((ty = jsonbyname(j, "type")) == nil)
		return -1;
	if((url = jsonbyname(j, "url")) == nil)
		return -1;
	if((tok = jsonbyname(j, "token")) == nil)
		return -1;

	if(ty->t != JSONString || url->t != JSONString || tok->t != JSONString) {
		werrstr("invalid challenge");
		return -1;
	}

	if((dn = jsonbyname(id, "value")) == nil)
		return -1;
	if(dn->t != JSONString) {
		werrstr("invalid identifier");
		return -1;
	}

	/* make sure the identifier matches the csr */
	for(i = 0; i < ndom; i++){
		/* if we got a wildcard certificate, the returned identifier has the *. removed */
		if(wild && strncmp(dom[i], "*.", 2) == 0)
			dom[i] += 2;

		if(cistrcmp(idn(dom[i]), dn->s) == 0)
			break;
	}
	if(i >= ndom){
		werrstr("unknown challenge identifier '%s'", dn->s);
		return -1;
	}

	dprint("trying challenge %s for %s (%s)\n", ty->s, dom[i], dn->s);
	if(challengefn(ty->s, dom[i], tok->s, matched) == -1){
		dprint("challengefn failed: %r\n");
		return -1;
	}

	if((resp = jwsrequest(url->s, &nresp, nil, "{}")) == nil)
		sysfatal("challenge: post %s: %r", url->s);
	free(resp);

	for(i = 0; i < 60; i++){
		sleep(1000);
		if((resp = jwsrequest(authurl, &nresp, nil, "")) == nil)
			sysfatal("challenge: post %s: %r", url->s);
		if((poll = jsonparse(resp)) == nil){
			free(resp);
			return -1;
		}
		if((state = jsonbyname(poll, "status")) != nil && state->t == JSONString){
			if(strcmp(state->s, "valid") == 0){
				jsonfree(poll);
				return 0;
			}
			else if(strcmp(state->s, "pending") != 0){
				fprint(2, "error: %J", poll);
				werrstr("status '%s'", state->s);
				jsonfree(poll);
				return -1;
			}
		}
		jsonfree(poll);	
	}
	werrstr("timeout");
	return -1;
}

static int
dochallenges(char *dom[], int ndom, JSON *order)
{
	JSON *chals, *j, *cl, *id, *wc;
	JSONEl *ae, *ce;
	int nresp, matched, wild;
	char *resp;

	if((j = jsonbyname(order, "authorizations")) == nil){
		werrstr("parse response: missing authorizations");
		return -1;
	}
	if(j->t != JSONArray){
		werrstr("parse response: authorizations must be array");
		return -1;
	}
	for(ae = j->first; ae != nil; ae = ae->next){
		if(ae->val->t != JSONString){
			werrstr("challenge: auth must be url");
			return -1;
		}
		if((resp = jwsrequest(ae->val->s, &nresp, nil, "")) == nil){
			werrstr("challenge: request %s: %r", ae->val->s);
			return -1;
		}
		if((chals = jsonparse(resp)) == nil){
			werrstr("invalid challenge: %r");
			return -1;
		}
		if((id = jsonbyname(chals, "identifier")) == nil){
			werrstr("missing identifier");
			jsonfree(chals);
			return -1;
		}
		if((cl = jsonbyname(chals, "challenges")) == nil){
			werrstr("missing challenge");
			jsonfree(chals);
			return -1;
		}

		wild = 0;
		if((wc = jsonbyname(chals, "wildcard")) != nil){
			if(wc->t == JSONBool && wc->n == 1)
				wild = 1;
		}

		matched = 0;
		for(ce = cl->first; ce != nil; ce = ce->next){
			if(challenge(ce->val, ae->val->s, id, dom, ndom, &matched, wild) == 0)
				break;
			if(matched)
				werrstr("could not complete challenge: %r");
		}
		if(!matched)
			sysfatal("no matching auth type: %r");
		jsonfree(chals);
		free(resp);
	}
	return 0;
}

static int
submitcsr(JSON *order, char *b64csr)
{
	char *resp;
	int nresp;
	JSON *j;

	if((j = jsonbyname(order, "finalize")) == nil)
		sysfatal("parse response: missing authorizations");
	if(j->t != JSONString)
		werrstr("parse response: finalizer must be string");
	if((resp = jwsrequest(j->s, &nresp, nil, "{\"csr\":\"%E\"}", b64csr)) == nil)
		sysfatal("submit csr: %r");
	free(resp);
	return 0;
}

static int
fetchcert(char *url)
{
	JSON *cert, *poll, *state;
	int i, r, nresp;
	char *resp;

	poll = nil;
	for(i = 0; i < 60; i++){
		sleep(1000);
		if((resp = jwsrequest(url, &nresp, nil, "")) == nil)
			return -1;
		if((poll = jsonparse(resp)) == nil){
			free(resp);
			return -1;
		}
		free(resp);
		if((state = jsonbyname(poll, "status")) != nil && state->t == JSONString){
			if(strcmp(state->s, "valid") == 0)
				break;
			else if(strcmp(state->s, "pending") != 0 && strcmp(state->s, "processing") != 0){
				fprint(2, "error: %J", poll);
				werrstr("invalid request: %s", state->s);
				jsonfree(poll);
				return -1;
				
			}
		}
		jsonfree(poll);
	}
	if(poll == nil){
		werrstr("timed out");
		return -1;
	}
	if((cert = jsonbyname(poll, "certificate")) == nil || cert->t != JSONString){
		werrstr("missing cert url in response");
		jsonfree(poll);
		return -1;
	}
	if((resp = jwsrequest(cert->s, &nresp, nil, "")) == nil){
		jsonfree(poll);
		return -1;
	}
	jsonfree(poll);
	r = write(1, resp, nresp);
	free(resp);
	if(r != nresp)
		return -1;
	return 0;
}

static void
getcert(char *csrpath)
{
	char *csr, *dom[64], subj[2048];
	uchar *der;
	int nder, ndom, fd;
	RSApub *rsa;
	Hdr loc = { "location" };
	JSON *o;

	if((fd = open(csrpath, OREAD|OCEXEC)) == -1)
		sysfatal("open %s: %r", csrpath);
	if((der = slurp(fd, &nder)) == nil)
		sysfatal("read %s: %r", csrpath);
	close(fd);

	if((rsa = X509reqtoRSApub(der, nder, subj, sizeof(subj))) == nil)
		sysfatal("decode csr: %r");
	rsapubfree(rsa);
	if((csr = encurl64(der, nder)) == nil)
		sysfatal("encode %s: %r", csrpath);
	free(der);

	dprint("subject: %s\n", subj);
	if((ndom = getfields(subj, dom, nelem(dom), 1, ", ")) == nelem(dom))
		sysfatal("too man domains");

	if((o = submitorder(dom, ndom, &loc)) == nil)
		sysfatal("order: %r");
	if(dochallenges(dom, ndom, o) == -1)
		sysfatal("challenge: %r");

	if(submitcsr(o, csr) == -1)
		sysfatal("signing cert: %r");
	if(fetchcert(loc.val) == -1)
		sysfatal("saving cert: %r");

	free(csr);
}

static int
Econv(Fmt *f)
{
	char *s;
	Rune r;
	int w;

	w = 0;
	s = va_arg(f->args, char*);
	while(*s){
		s += chartorune(&r, s);
		if(r == '\\' || r == '\"')
			w += fmtrune(f, '\\');
		w += fmtrune(f, r);
	}
	return w;
}

static int
loadkey(char *path)
{
	uchar h[SHA2_256dlen];
	char key[8192];
	JSON *j, *e, *kty, *n;
	DigestState *ds;
	int fd, nr;

	if((fd = open(path, OREAD|OCEXEC)) == -1)
		return -1;
	nr = readn(fd, key, sizeof(key));
	close(fd);
	if(nr == -1)
		return -1;
	key[nr] = 0;

	if((j = jsonparse(key)) == nil)
		return -1;
	if((e = jsonbyname(j, "e")) == nil || e->t != JSONString)
		return -1;
	if((kty = jsonbyname(j, "kty")) == nil || kty->t != JSONString)
		return -1;
	if((n = jsonbyname(j, "n")) == nil || n->t != JSONString)
		return -1;

	ds = sha2_256((uchar*)"{\"e\":\"", 6, nil, nil);
	ds = sha2_256((uchar*)e->s, strlen(e->s), nil, ds);
	ds = sha2_256((uchar*)"\",\"kty\":\"", 9, nil, ds);
	ds = sha2_256((uchar*)kty->s, strlen(kty->s), nil, ds);
	ds = sha2_256((uchar*)"\",\"n\":\"", 7, nil, ds);
	ds = sha2_256((uchar*)n->s, strlen(n->s), nil, ds);
	sha2_256((uchar*)"\"}", 2, h, ds);
	jwskey = j;
	jwsthumb = encurl64(h, sizeof(h));
	return 0;
}

static void
usage(void)
{
	fprint(2, "usage: %s [-a acctkey] [-e cmd | -o chalout -t type] [-p provider] acct csr\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *acctkey, *ct, *co;

	JSONfmtinstall();
	fmtinstall('E', Econv);

	ct = nil;
	co = nil;
	acctkey = nil;
	ARGBEGIN{
	case 'd':
		debug++;
		break;
	case 'a':
		acctkey = EARGF(usage());
		break;
	case 'e':
		challengecmd = EARGF(usage());
		break;
	case 'o':
		co = EARGF(usage());
		break;
	case 't':
		ct = EARGF(usage());
		break;
	case 'p':
		provider = EARGF(usage());
		break;
	default:
		usage();
		break;
	}ARGEND;

	if(challengecmd != nil){
		if(ct != nil || co != nil)
			usage();
		challengeout = "/dev/null";
		challengefn = runchallenge;
	}else if(ct == nil || strcmp(ct, "http") == 0){
		challengeout = (co != nil) ? co : "/usr/web/.well-known/acme-challenge";
		challengefn = httpchallenge;
	}else if(strcmp(ct, "dns") == 0){
		challengeout = (co != nil) ? co : "/lib/ndb/dnschallenge";
		challengefn = dnschallenge;
	}else {
		sysfatal("unknown challenge type '%s'", ct);
	}

	if(argc != 2)
		usage();

	if(acctkey == nil)
		acctkey = esmprint("/sys/lib/tls/acmed/%s.pub", argv[0]);
	if((keyspec = smprint(Keyspec, argv[0])) == nil)
		sysfatal("smprint: %r");
	if(loadkey(acctkey) == -1)
		sysfatal("load key: %r");

	if(endpoints() == -1)
		sysfatal("endpoints: %r");
	mkaccount(argv[0]);
	getcert(argv[1]);
	exits(nil);
}
