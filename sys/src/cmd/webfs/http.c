#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

#include "dat.h"
#include "fns.h"

#include <auth.h>
#include <mp.h>
#include <libsec.h>

typedef struct Hconn Hconn;
typedef struct Hpool Hpool;
typedef struct Hauth Hauth;

struct Hconn
{
	Hconn	*next;
	long	time;

	int	fd;
	int	ctl;
	int	keep;
	int	cancel;
	int	tunnel;
	int	len;
	char	addr[128];
	char	buf[8192+2];
};

struct Hpool
{
	QLock;

	Hconn	*head;
	int	active;

	int	limit;
	int	peer;
	int	idle;
};

struct Hauth
{
	Hauth	*next;
	Url	*url;
	char	*auth;
};

static Hpool hpool = {
	.limit	= 16,
	.peer	= 4,
	.idle	= 5,	/* seconds */
};

static QLock authlk;
static Hauth *hauth;

static void hclose(Hconn *h);

static int
tlstrace(char *fmt, ...)
{
	int r;
	va_list a;
	va_start(a, fmt);
	r = vfprint(2, fmt, a);
	va_end(a);
	return r;
}

static int
tlswrap(int fd, char *servername)
{
	TLSconn conn;

	memset(&conn, 0, sizeof(conn));
	if(debug)
		conn.trace = tlstrace;
	if(servername != nil)
		conn.serverName = smprint("%N", servername);
	if((fd = tlsClient(fd, &conn)) < 0){
		if(debug) fprint(2, "tlsClient: %r\n");
	}
	free(conn.cert);
	free(conn.sessionID);
	free(conn.serverName);
	return fd;
}

static Hconn*
hdial(Url *u, int cached)
{
	char addr[128];
	Hconn *h, *p;
	int fd, ctl;

	snprint(addr, sizeof(addr), "tcp!%s!%s", u->host, u->port ? u->port : u->scheme);

	qlock(&hpool);
	if(cached){
		for(p = nil, h = hpool.head; h; p = h, h = h->next){
			if(strcmp(h->addr, addr) == 0){
				if(p)
					p->next = h->next;
				else
					hpool.head = h->next;
				h->next = nil;
				qunlock(&hpool);
				return h;
			}
		}
	}
	hpool.active++;
	qunlock(&hpool);

	if(debug)
		fprint(2, "hdial [%d] %s\n", hpool.active, addr);

	if(proxy)
		snprint(addr, sizeof(addr), "tcp!%s!%s",
			proxy->host, proxy->port ? proxy->port : proxy->scheme);

	if((fd = dial(addr, 0, 0, &ctl)) < 0)
		return nil;

	if(proxy){
		if(strcmp(proxy->scheme, "https") == 0)
			fd = tlswrap(fd, proxy->host);
	} else {
		if(strcmp(u->scheme, "https") == 0)
			fd = tlswrap(fd, u->host);
	}
	if(fd < 0){
		close(ctl);
		return nil;
	}

	h = emalloc(sizeof(*h));
	h->next = nil;
	h->time = 0;
	h->cancel = 0;
	h->tunnel = 0;
	h->keep = cached;
	h->len = 0;
	h->fd = fd;
	h->ctl = ctl;

	if(proxy){
		h->tunnel = strcmp(u->scheme, "https") == 0;
		snprint(addr, sizeof(addr), "tcp!%s!%s", u->host, u->port ? u->port : u->scheme);
	}
	nstrcpy(h->addr, addr, sizeof(h->addr));

	return h;
}

static void
hcloseall(Hconn *x)
{
	Hconn *h;

	while(h = x){
		x = h->next;
		h->next = nil;
		h->keep = 0;
		hclose(h);
	}
}

static void
hclose(Hconn *h)
{
	Hconn *x, *t;
	int i, n;

	if(h == nil)
		return;

	qlock(&hpool);
	if(!h->tunnel && h->keep && h->fd >= 0){
		for(n = 0, i = 0, t = nil, x = hpool.head; x; x = x->next){
			if(strcmp(x->addr, h->addr) == 0)
				if(++n > hpool.peer)
					break;
			if(++i < hpool.limit)
				t = x;
		}
		if(x == nil){
			/* return connection to pool */
			h->time = time(0);
			h->next = hpool.head;
			hpool.head = h;

			/* cut off tail */
			if(t){
				x = t->next;
				t->next = nil;
			}

			i = h->next != nil;
			qunlock(&hpool);

			/* free the tail */
			hcloseall(x);

			/*
			 * if h is first one in pool, spawn proc to close
			 * idle connections.
			 */
			if(i == 0)
			if(rfork(RFMEM|RFPROC|RFNOWAIT) == 0){
				do {
					Hconn **xx;
					long now;

					sleep(1000);

					qlock(&hpool);
					now = time(0);

					x = nil;
					xx = &hpool.head;
					while(h = *xx){
						if((now - h->time) > hpool.idle){
							*xx = h->next;

							/* link to tail */
							h->next = x;
							x = h;
							continue;
						}
						xx = &h->next;
					}

					i = hpool.head != nil;
					qunlock(&hpool);

					/* free the tail */
					hcloseall(x);
				} while(i);
				exits(nil);
			}
			return;
		}
	}
	hpool.active--;
	qunlock(&hpool);

	if(debug)
		fprint(2, "hclose [%d] %s\n", hpool.active, h->addr);

	if(h->ctl >= 0)
		close(h->ctl);
	if(h->fd >= 0)
		close(h->fd);
	free(h);
}

static void
hhangup(Hconn *h)
{
	if(debug)
		fprint(2, "hangup pc=%p: %r\n", getcallerpc(&h));
	h->keep = 0;
	if(h->ctl >= 0)
		hangup(h->ctl);
}

static int
hread(Hconn *h, void *data, int len)
{
	if(h->len > 0){
		if(len > h->len)
			len = h->len;
		memmove(data, h->buf, len);
		h->len -= len;
		if(h->len > 0)
			memmove(h->buf, h->buf + len, h->len);
		return len;
	}
	if((len = read(h->fd, data, len)) == 0)
		h->keep = 0;
	if(len < 0)
		hhangup(h);
	return len;
}

static int
hwrite(Hconn *h, void *data, int len)
{
	if(write(h->fd, data, len) != len){
		hhangup(h);
		return -1;
	}
	return len;
}

static int
hline(Hconn *h, char *data, int len, int cont)
{
	char *x, *y, *e;
	int n;

	data[0] = 0;
	for(;;){
		if(h->len > 0){
			while(x = memchr(h->buf, '\n', h->len)){
				n = x - h->buf;
				if(n > 0 && x[-1] == '\r')
					n--;
				if(n > 0 && cont){
					e = h->buf + h->len;
					for(y = x+1; y < e; y++)
						if(*y != ' ' && *y != '\t')
							break;
					if(y >= e || *y == 0)
						break;
					if(y > x+1){
						if(x > h->buf && x[-1] == '\r')
							x--;
						memmove(x, y, e - y);
						h->len -= y - x;
						continue;
					}
				}			
				if(n < len)
					len = n;
				memmove(data, h->buf, len);
				data[len] = 0;
				h->len -= (++x - h->buf);
				if(h->len > 0)
					memmove(h->buf, x, h->len);
				return len;
			}
		}
		n = sizeof(h->buf) - h->len;
		if(n <= 0)
			return 0;
		if(h->tunnel)
			n = 1;	/* do not read beyond header */
		if((n = read(h->fd, h->buf + h->len, n)) <= 0){
			hhangup(h);
			return -1;
		}
		h->len += n;
	}
}

static int
hauthgetkey(char *params)
{
	if(debug)
		fprint(2, "hauthgetkey %s\n", params);
	werrstr("needkey %s", params);
	return -1;
}

int
authenticate(Url *u, Url *ru, char *method, char *s)
{
	char oerr[ERRMAX], *user, *pass, *realm, *nonce, *opaque, *x;
	Hauth *a;
	Fmt fmt;
	int n;

	snprint(oerr, sizeof(oerr), "authentification failed");
	errstr(oerr, sizeof(oerr));

	user = u->user;
	pass = u->pass;
	realm = nonce = opaque = nil;
	if(!cistrncmp(s, "Basic ", 6)){
		UserPasswd *up;

		s += 6;
		if(x = cistrstr(s, "realm="))
			realm = unquote(x+6, &s);
		if(realm == nil)
			return -1;
		up = nil;
		if(user == nil || pass == nil){
			fmtstrinit(&fmt);
			fmtprint(&fmt, " realm=%q", realm);
			if(user)
				fmtprint(&fmt, " user=%q", user);
			if((s = fmtstrflush(&fmt)) == nil)
				return -1;
			up = auth_getuserpasswd(hauthgetkey,
				"proto=pass service=http server=%q%s", u->host, s);
			free(s);
			if(up == nil)
				return -1;
			user = up->user;
			pass = up->passwd;
		}
		fmtstrinit(&fmt);
		fmtprint(&fmt, "%s:%s", user ? user : "", pass ? pass : "");
		if(up){
			memset(up->user, 0, strlen(up->user));
			memset(up->passwd, 0, strlen(up->passwd));
			free(up);
		}
		if((s = fmtstrflush(&fmt)) == nil)
			return -1;
		n = strlen(s);
		fmtstrinit(&fmt);
		fmtprint(&fmt, "Basic %.*[", n, s);
		memset(s, 0, n);
		free(s);
		u = saneurl(url(".", u));	/* all uris below the requested one */
	}else
	if(!cistrncmp(s, "Digest ", 7)){
		char chal[1024], ouser[128], resp[2*MD5LEN+1];
		int nchal;

		s += 7;
		if(x = cistrstr(s, "realm="))
			realm = unquote(x+6, &s);
		if(x = cistrstr(s, "nonce="))
			nonce = unquote(x+6, &s);
		if(x = cistrstr(s, "opaque="))
			opaque = unquote(x+7, &s);
		if(realm == nil || nonce == nil)
			return -1;
		fmtstrinit(&fmt);
		fmtprint(&fmt, " realm=%q", realm);
		if(user)
			fmtprint(&fmt, " user=%q", user);
		if((s = fmtstrflush(&fmt)) == nil)
			return -1;
		nchal = snprint(chal, sizeof(chal), "%s %s %U", nonce, method, ru);
		n = auth_respond(chal, nchal, ouser, sizeof ouser, resp, sizeof resp, hauthgetkey,
			"proto=httpdigest role=client server=%q%s", u->host, s);
		memset(chal, 0, sizeof(chal));
		free(s);
		if(n < 0)
			return -1;
		fmtstrinit(&fmt);
		fmtprint(&fmt, "Digest ");
		fmtprint(&fmt, "username=\"%s\", ", ouser);
		fmtprint(&fmt, "realm=\"%s\", ", realm);
		fmtprint(&fmt, "host=\"%N\", ", u->host);
		fmtprint(&fmt, "uri=\"%U\", ", ru);
		fmtprint(&fmt, "nonce=\"%s\", ", nonce);
		fmtprint(&fmt, "response=\"%s\"", resp);
		if(opaque)
			fmtprint(&fmt, ", opaque=\"%s\"", opaque);
		u = saneurl(url("/", u));	/* BUG: should be the ones in domain= only */
	} else
		return -1;
	if((s = fmtstrflush(&fmt)) == nil){
		freeurl(u);
		return -1;
	}
	if(u == nil){
		free(s);
		return -1;
	}

	a = emalloc(sizeof(*a));
	a->url = u;
	a->auth = s;
	qlock(&authlk);
	a->next = hauth;
	hauth = a;
	qunlock(&authlk);

	errstr(oerr, sizeof(oerr));
	return 0;
}

int
hauthenticate(Url *u, Url *ru, char *method, char *key, Key *hdr)
{
	for(hdr = getkey(hdr, key); hdr != nil; hdr = getkey(hdr->next, key))
		if(authenticate(u, ru, method, hdr->val) == 0)
			return 0;
	return -1;
}

void
flushauth(Url *u, char *t)
{
	Hauth *a, *p;

	qlock(&authlk);
Again:
	for(p = nil, a = hauth; a; p = a, a = a->next)
		if(matchurl(u, a->url) && (t == nil || !strcmp(t, a->auth))){
			if(p)
				p->next = a->next;
			else
				hauth = a->next;
			if(debug)
				fprint(2, "flushauth for %U\n", a->url);
			freeurl(a->url);
			memset(a->auth, 0, strlen(a->auth));
			free(a->auth);
			free(a);
			goto Again;
		}
	qunlock(&authlk);
}

static void
catch(void *, char *msg)
{
	if(strstr("alarm", msg) != nil)
		noted(NCONT);
	else
		noted(NDFLT);
}

#define NOLENGTH 0x7fffffffffffffffLL

void
http(char *m, Url *u, Key *shdr, Buq *qbody, Buq *qpost)
{
	int i, l, n, try, pid, fd, cfd, needlength, chunked, retry, nobody, badauth;
	char *s, *x, buf[8192+2], status[256], method[16], *host;
	vlong length, offset;
	Url ru, tu, *nu;
	Key *k, *rhdr;
	Hconn *h;
	Hauth *a;

	incref(qbody);
	if(qpost) incref(qpost);
	nstrcpy(method, m, sizeof(method));
	switch(rfork(RFPROC|RFMEM|RFNOWAIT)){
	default:
		return;
	case -1:
		buclose(qbody, "can't fork");
		bufree(qbody);
		buclose(qpost, "can't fork");
		bufree(qpost);
		while(k = shdr){
			shdr = k->next;
			free(k);
		}
		freeurl(u);
		return;
	case 0:
		break;
	}

	notify(catch);
	if(qpost){
		/* file for spooling the postbody if we need to restart the request */
		snprint(buf, sizeof(buf), "/tmp/http.%d.%d.post", getppid(), getpid());
		fd = create(buf, OEXCL|ORDWR|ORCLOSE, 0600);
	} else
		fd = -1;

	h = nil;
	cfd = -1;
	pid = 0;
	host = nil;
	needlength = 0;
	badauth = 0;
	for(try = 0; try < 12; try++){
		strcpy(status, "0 No status");
		if(u == nil || (strcmp(u->scheme, "http") && strcmp(u->scheme, "https"))){
			werrstr("bad url scheme");
			break;
		}

		if(debug)
			fprint(2, "http(%d): %s %U\n", try, method, u);

		/* preemptive authentication from hauth cache */
		qlock(&authlk);
		if(proxy && !lookkey(shdr, "Proxy-Authorization"))
			for(a = hauth; a; a = a->next)
				if(matchurl(a->url, proxy)){
					shdr = addkey(shdr, "Proxy-Authorization", a->auth);
					break;
				}
		if(!lookkey(shdr, "Authorization"))
			for(a = hauth; a; a = a->next)
				if(matchurl(a->url, u)){
					shdr = addkey(shdr, "Authorization", a->auth);
					break;
				}
		qunlock(&authlk);

		length = 0;
		chunked = 0;
		if(qpost){
			/* have to read it to temp file to figure out the length */
			if(fd >= 0 && needlength && lookkey(shdr, "Content-Length") == nil){
				seek(fd, 0, 2);
				while((n = buread(qpost, buf, sizeof(buf))) > 0)
					write(fd, buf, n);
				shdr = delkey(shdr, "Transfer-Encoding");
			}

			qlock(qpost);
			/* wait until buffer is full, most posts are small */
			while(!qpost->closed && qpost->size < qpost->limit && qpost->nwq == 0)
				rsleep(&qpost->rz);

			if(lookkey(shdr, "Content-Length"))
				chunked = 0;
			else if(x = lookkey(shdr, "Transfer-Encoding"))
				chunked = cistrstr(x, "chunked") != nil;
			else if(chunked = !qpost->closed)
				shdr = addkey(shdr, "Transfer-Encoding", "chunked");
			else if(qpost->closed){
				if(fd >= 0){
					length = seek(fd, 0, 2);
					if(length < 0)
						length = 0;
				}
				length += qpost->size;
				snprint(buf, sizeof(buf), "%lld", length);
				shdr = addkey(shdr, "Content-Length", buf);
			}
			qunlock(qpost);
		}

		/* http requires ascii encoding of host */
		free(host);
		host = smprint("%N", u->host);

		if(proxy && strcmp(u->scheme, "https") != 0){
			ru = *u;
			ru.host = host;
			ru.fragment = nil;
		} else {
			memset(&ru, 0, sizeof(ru));
			ru.path = Upath(u);
			ru.query = u->query;
		}
		n = snprint(buf, sizeof(buf), "%s %U HTTP/1.1\r\nHost: %s%s%s\r\n",
			method, &ru, host, u->port ? ":" : "", u->port ? u->port : "");
		if(n >= sizeof(buf)-64){
			werrstr("request too large");
			break;
		}
		if(h == nil){
			alarm(timeout);
			if((h = hdial(u, qpost==nil)) == nil)
				break;
		}
		if(h->tunnel){
			n = snprint(buf, sizeof(buf), "CONNECT %s:%s HTTP/1.1\r\nHost: %s:%s\r\n",
				host, u->port ? u->port : "443",
				host, u->port ? u->port : "443");
		}
		else if((cfd = open("/mnt/webcookies/http", ORDWR)) >= 0){
			/* only scheme, host and path are relevant for cookies */
			memset(&tu, 0, sizeof(tu));
			tu.scheme = u->scheme;
			tu.host = host;
			tu.path = Upath(u);
			fprint(cfd, "%U", &tu);
			for(;;){
				if(n >= sizeof(buf)-2){
					if(debug)
						fprint(2, "-> %.*s", utfnlen(buf, n), buf);
					if(hwrite(h, buf, n) != n)
						goto Badflush;
					n = 0;
				}
				if((l = read(cfd, buf+n, sizeof(buf)-2 - n)) == 0)
					break;
				if(l < 0){
					close(cfd);
					cfd = -1;
					break;
				}
				n += l;
			}
		}

		for(k = shdr; k; k = k->next){
			/* only send proxy headers when establishing tunnel */
			if(h->tunnel && cistrncmp(k->key, "Proxy-", 6) != 0)
				continue;
			if(n > 0){
				if(debug)
					fprint(2, "-> %.*s", utfnlen(buf, n), buf);
				if(hwrite(h, buf, n) != n)
					goto Badflush;
			}
			n = snprint(buf, sizeof(buf)-2, "%s: %s\r\n", k->key, k->val);
		}
		n += snprint(buf+n, sizeof(buf)-n, "\r\n");
		if(debug)
			fprint(2, "-> %.*s", utfnlen(buf, n), buf);
		if(hwrite(h, buf, n) != n){
		Badflush:
			alarm(0);
			goto Retry;
		}

		if(qpost && !h->tunnel){
			h->cancel = 0;
			if((pid = rfork(RFMEM|RFPROC)) <= 0){
				int ifd;

				if((ifd = fd) >= 0)
					seek(ifd, 0, 0);
				while(!h->cancel){
					alarm(0);
					if((ifd < 0) || ((n = read(ifd, buf, sizeof(buf)-2)) <= 0)){
						ifd = -1;
						if((n = buread(qpost, buf, sizeof(buf)-2)) <= 0)
							break;
						if(fd >= 0)
							if(write(fd, buf, n) != n)
								break;
					}
					alarm(timeout);
					if(chunked){
						char tmp[32];
						if(hwrite(h, tmp, snprint(tmp, sizeof(tmp), "%x\r\n", n)) < 0)
							break;
						buf[n++] = '\r';
						buf[n++] = '\n';
					}
					if(hwrite(h, buf, n) != n)
						break;
				}
				if(chunked){
					alarm(timeout);
					hwrite(h, "0\r\n\r\n", 5);
				}else
					h->keep = 0;
				if(pid == 0)
					exits(nil);
			}
			/* no timeout when posting */
			alarm(0);
		} else
			alarm(timeout);

		Cont:
		rhdr = 0;
		retry = 0;
		chunked = 0;
		offset = 0;
		length = NOLENGTH;
		for(l = 0; hline(h, s = buf, sizeof(buf)-1, 1) > 0; l++){
			if(debug)
				fprint(2, "<- %s\n", s);
			if(l == 0){
				if(x = strchr(s, ' '))
					while(*x == ' ')
						*x++ = 0;
				if(cistrncmp(s, "HTTP", 4)){
					h->keep = 0;
					if(cistrcmp(s, "ICY"))
						break;
				}
				if(x[0])
					nstrcpy(status, x, sizeof(status));
				continue;
			}
			if((k = parsehdr(s)) == nil)
				continue;
			if(!cistrcmp(k->key, "Connection")){
				if(cistrstr(k->val, "close"))
					h->keep = 0;
			}
			else if(!cistrcmp(k->key, "Content-Length"))
				length = atoll(k->val);
			else if(!cistrcmp(k->key, "Transfer-Encoding")){
				if(cistrstr(k->val, "chunked"))
					chunked = 1;
			}
			else if(!cistrcmp(k->key, "Set-Cookie") || 
				!cistrcmp(k->key, "Set-Cookie2")){
				if(cfd >= 0)
					fprint(cfd, "Set-Cookie: %s\n", k->val);
				free(k);
				continue;
			}
			k->next = rhdr;
			rhdr = k;
		}
		alarm(0);
		if(cfd >= 0){
			close(cfd);
			cfd = -1;
		}

		nobody = !cistrcmp(method, "HEAD");
		if((i = atoi(status)) < 0)
			i = 0;
		Status:
		switch(i){
		default:
			if(i % 100){
				i -= (i % 100);
				goto Status;
			}
			goto Error;
		case 100:	/* Continue */
		case 101:	/* Switching Protocols */
		case 102:	/* Processing */
		case 103:	/* Early Hints */
			while(k = rhdr){
				rhdr = k->next;
				free(k);
			}
			strcpy(status, "0 No status");
			goto Cont;
		case 304:	/* Not Modified */
			nobody = 1;
		case 305:	/* Use Proxy */
		case 400:	/* Bad Request */
		case 402:	/* Payment Required */
		case 403:	/* Forbidden */
		case 404:	/* Not Found */
		case 405:	/* Method Not Allowed */
		case 406:	/* Not Acceptable */
		case 408:	/* Request Timeout */
		case 409:	/* Conflict */
		case 410:	/* Gone */
			goto Error;
		case 411:	/* Length Required */
			if(qpost){
				needlength = 1;
				h->cancel = 1;
				retry = 1;
				break;
			}
		case 412:	/* Precondition Failed */
		case 413:	/* Request Entity Too Large */
		case 414:	/* Request URI Too Large */
		case 415:	/* Unsupported Media Type */
		case 416:	/* Requested Range Not Satisfiable */
		case 417:	/* Expectation Failed */
		case 500:	/* Internal server error */
		case 501:	/* Not implemented */
		case 502:	/* Bad gateway */
		case 503:	/* Service unavailable */
		case 504:	/* Gateway Timeout */
		case 505:	/* HTTP Version not Supported */
		Error:
			h->cancel = 1;
			buclose(qbody, status);
			buclose(qpost, status);
			break;
		case 300:	/* Multiple choices */
		case 302:	/* Found */
		case 303:	/* See Other */
			if(qpost){
				if(pid > 0){
					waitpid();
					pid = 0;
				}
				buclose(qpost, 0);
				bufree(qpost);
				qpost = nil;
			}
			shdr = delkey(shdr, "Content-Length");
			shdr = delkey(shdr, "Content-Type");
			shdr = delkey(shdr, "Transfer-Encoding");
			if(cistrcmp(method, "HEAD"))
				nstrcpy(method, "GET", sizeof(method));
		case 301:	/* Moved Permanently */
		case 307:	/* Temporary Redirect */
		case 308:	/* Resume Incomplete */
			if((x = lookkey(rhdr, "Location")) == nil)
				goto Error;
			if((nu = saneurl(url(x, u))) == nil)
				goto Error;
			freeurl(u);
			u = nu;
		if(0){
		case 401:	/* Unauthorized */
			if(x = lookkey(shdr, "Authorization")){
				flushauth(nil, x);
				if(badauth++)
					goto Error;
			}
			if(hauthenticate(u, &ru, method, "WWW-Authenticate", rhdr) < 0){
			Autherror:
				h->cancel = 1;
				rerrstr(buf, sizeof(buf));
				buclose(qbody, buf);
				buclose(qpost, buf);
				break;
			}
		}
		if(0){
		case 407:	/* Proxy Auth */
			if(proxy == nil)
				goto Error;
			if(x = lookkey(shdr, "Proxy-Authorization")){
				flushauth(proxy, x);
				if(badauth++)
					goto Error;
			}
			if(hauthenticate(proxy, proxy, method, "Proxy-Authenticate", rhdr) < 0)
				goto Autherror;
		}
		case 0:		/* No status */
			if(qpost && fd < 0){
				if(i > 0)
					goto Error;
				break;
			}
			h->cancel = 1;
			retry = 1;
			break;
		case 204:	/* No Content */
		case 205:	/* Reset Content */
			nobody = 1;
		case 200:	/* OK */
		case 201:	/* Created */
		case 202:	/* Accepted */
		case 203:	/* Non-Authoritative Information */
		case 206:	/* Partial Content */
			if(h->tunnel)
				break;
			qbody->url = u; u = nil;
			qbody->hdr = rhdr; rhdr = nil;
			if(nobody)
				buclose(qbody, 0);
			break;
		}

		while(k = rhdr){
			rhdr = k->next;
			free(k);
		}

		/*
		 * remove authorization headers so on the next round, we use
		 * the hauth cache (wich checks the scope url). this makes
		 * sure we wont send credentials to the wrong url after
		 * a redirect.
		 */
		shdr = delkey(shdr, "Proxy-Authorization");
		shdr = delkey(shdr, "Authorization");

		/*
		 * when 2xx response is given for the CONNECT request
		 * then the proxy server has established the connection.
		 */
		if(h->tunnel && !retry && (i/100) == 2){
			if((h->fd = tlswrap(h->fd, host)) < 0)
				break;

			/* proceed to the original request */
			h->tunnel = 0;
			continue;
		}

		if(!chunked && length == NOLENGTH)
			h->keep = 0;

		/*
		 * read the response body (if any). retry means we'r just
		 * skipping the error page so we wont touch qbody.
		 */
		while(!nobody){
			if((qbody->closed || retry) && !h->keep)
				break;
			if(chunked){
				if(hline(h, buf, sizeof(buf)-1, 0) <= 0)
					break;
				length = strtoll(buf, nil, 16);
				offset = 0;
			}
			while(offset < length){
				l = sizeof(buf);
				if(l > (length - offset))
					l = (length - offset);
				if((n = hread(h, buf, l)) <= 0)
					break;
				offset += n;
				if(!retry)
					if(buwrite(qbody, buf, n) != n)
						break;
			}
			if(offset != length){
				h->keep = 0;
				if(length != NOLENGTH)
					break;
			}
			if(chunked){
				while(hline(h, buf, sizeof(buf)-1, 1) > 0){
					if(debug)
						fprint(2, "<= %s\n", buf);
					if(!retry)
						if(k = parsehdr(buf)){
							k->next = qbody->hdr;
							qbody->hdr = k;
						}
				}
				if(length > 0)
					continue;
			}
			if(!retry)
				buclose(qbody, 0);
			break;
		}

		if(!retry)
			break;
		Retry:
		if(cfd >= 0)
			close(cfd);
		if(pid > 0){
			waitpid();
			pid = 0;
		}
		hclose(h);
		h = nil;
	}
	alarm(0);
	snprint(buf, sizeof(buf), "%s %r", status);
	buclose(qbody, buf);
	bufree(qbody);

	if(qpost){
		if(pid > 0)
			waitpid();
		buclose(qpost, buf);
		bufree(qpost);
	}
	if(fd >= 0)
		close(fd);

	hclose(h);
	freeurl(u);
	free(host);

	while(k = shdr){
		shdr = k->next;
		free(k);
	}
	exits(nil);
}
