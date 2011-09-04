#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <panel.h>

#include <libsec.h>		/* tlsClient */

#include "mothra.h"
typedef struct Cache Cache;
struct Cache{
	int fd;			/* file descriptor on which to write cached data */
	ulong hash;		/* hash of url, used to compute cache file name */
	int modtime;		/* time at which cache entry was created */
	int type;		/* url->type of cached entry */
};
void httpheader(Url *, char *);
int httpresponse(char *);
static char *proxyserver;	/* name of proxy server */
void exitnow(void*, char*){
	noted(NDFLT);
}
void hashname(char *name, int n, char *stem, Cache *c){
	snprint(name, n, "/sys/lib/mothra/cache/%s.%.8lux", stem, c->hash);
}
// #define	CacheEnabled
/*
 * Returns fd of cached file, if found (else -1)
 * Fills in Cache data structure for caller
 * If stale is set, caller has determined that the existing
 * cache entry for this url is stale, so we shouldn't bother re-examining it.
 */
int cacheopen(Url *url, Cache *c, int stale){
#ifdef CacheEnabled
	int fd, n;
	char name[NNAME+1], *s, *l;
	/*
	 * If we're using a proxy server or the url contains a ? or =,
	 * don't even bother.
	 */
	if(proxyserver || strchr(url->reltext, '?')!=0 || strchr(url->reltext, '=')!=0){
		c->fd=-1;
		return -1;
	}
	c->hash=0;
	for(s=url->fullname,n=0;*s;s++,n++) c->hash=c->hash*n+(*s&255);
	if(stale)
		fd=-1;
	else{
		hashname(name, sizeof(name), "cache", c);
		fd=open(name, OREAD);
	}
	if(fd==-1){
		hashname(name, sizeof(name), "write", c);
		c->fd=create(name, OWRITE, 0444);
		if(c->fd!=-1)
			fprint(c->fd, "%s %10ld\n", url->fullname, time(0));
		return -1;
	}
	c->fd=-1;
	for(l=name;l!=&name[NNAME];l+=n){
		n=&name[NNAME]-l;
		n=read(fd, l, n);
		if(n<=0) break;
	}
	*l='\0';
	s=strchr(name, ' ');
	if(s==0){
		close(fd);
		return -1;
	}
	*s='\0';
	if(strcmp(url->fullname, name)!=0){
		close(fd);
		return -1;
	}
	c->modtime=atol(++s);
	s=strchr(s, '\n');
	if(s==0){
		close(fd);
		return -1;
	}
	s++;
	if(strncmp(s, "type ", 5)!=0){
		close(fd);
		return -1;
	}
	c->type=atoi(s+5);
	s=strchr(s+5, '\n');
	if(s==0){
		close(fd);
		return -1;
	}
	
	seek(fd, s-name+1, 0);
	return fd;
#else
	c->fd=-1;
	return -1;
#endif
}
/*
 * Close url->fd and either rename the cache file or
 * remove it, depending on success
 */
void cacheclose(Cache *c, int success){
	char wname[NNAME+1], cname[NNAME+1], *celem;
	Dir *wdir;
	if(c->fd==-1) return;
	close(c->fd);
	hashname(wname, sizeof(wname), "write", c);
	if(!success){
		remove(wname);
		return;
	}
	if((wdir = dirstat(wname)) == 0)
		return;
	hashname(cname, sizeof(cname), "cache", c);
	if(access(cname, 0) == 0){
		if(remove(cname)==-1){
			remove(wname);
			free(wdir);
			return;
		}
		/*
		 * This looks implausible, but it's what the mv command does
		 */
		do; while(remove(cname)!=-1);
	}
	celem=strrchr(cname, '/');
	if(celem==0) celem=cname;
	else celem++;
	strcpy(wdir->name, celem);
	if(dirwstat(wname, wdir)==-1)
		remove(wname);
	free(wdir);
}
static char *wkday[]={
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};
static char *month[]={
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};
/*
 * Sun, 06 Nov 1994 08:49:38 GMT
 * 123456789 123456789 123456789
 */
char *rfc1123date(long time){
	static char buf[50];
	Tm *t;
	t=gmtime(time);
	snprint(buf, sizeof(buf), "%s, %2.2d %s %4.4d %2.2d:%2.2d:%2.2d GMT",
		wkday[t->wday], t->mday, month[t->mon], t->year+1900,
		t->hour, t->min, t->sec);
	return buf;
}
/*
 * Given a url, return a file descriptor on which caller can
 * read an http document.  As a side effect, we parse the
 * http header and fill in some fields in the url.
 * The caller is responsible for processing redirection loops.
 * Method can be either GET or POST.  If method==post, body
 * is the text to be posted.
 */
int http(Url *url, int method, char *body){
	char *addr, *com;
	int fd, n, nnl, len;
	int ncom, m;
	int pfd[2];
	char buf[1024], *bp, *ebp;
	char line[1024+1], *lp, *elp;
	char authstr[NAUTH], *urlname;
	int gotresponse;
	int response;
	Cache cache;
	int cfd, cookiefd;
	static int firsttime=1;
	static int gotcookies;

	if(firsttime){
		proxyserver=getenv("httpproxy");
		gotcookies=(access("/mnt/webcookies/http", AREAD|AWRITE)==0);
		firsttime=0;
	}
	*authstr = 0;
Authorize:
	cfd=-1;
	cookiefd=-1;
	if(proxyserver && proxyserver[0]!='\0'){
		addr=strdup(proxyserver);
		urlname=url->fullname;
	}
	else{
		addr=emalloc(strlen(url->ipaddr)+100);
		sprint(addr, "tcp!%s!%d", url->ipaddr, url->port);
		urlname=url->reltext;
	}
	fd=dial(addr, 0, 0, 0);
	free(addr);
	if(fd==-1) goto ErrReturn;
	if(url->ssl){
		int tfd;
		TLSconn conn;

		memset(&conn, 0, sizeof conn);
		tfd = tlsClient(fd, &conn);
		if(tfd < 0){
			close(fd);
			goto ErrReturn;
		}
		/* BUG: check cert here? */
		if(conn.cert)
			free(conn.cert);
		close(fd);
		fd = tfd;
	}
	ncom=strlen(urlname)+sizeof(buf);
	com=emalloc(ncom+2);
	cache.fd=-1;
	switch(method){
	case GET:
		cfd=cacheopen(url, &cache, 0);
		if(cfd==-1)
			n=sprint(com,
				"GET %s HTTP/1.0\r\n%s"
				"Accept: */*\r\n"
				"User-agent: mothra/%s\r\n"
				"Host: %s\r\n",
				urlname, authstr, version, url->ipaddr);
		else
			n=sprint(com,
				"GET %s HTTP/1.0\r\n%s"
				"If-Modified-since: %s\r\n"
				"Accept: */*\r\n"
				"User-agent: mothra/%s\r\n"
				"Host: %s\r\n",
				urlname, authstr, rfc1123date(cache.modtime), version, url->ipaddr);
		break;
	case POST:
		len=strlen(body);
		n=sprint(com,
			"POST %s HTTP/1.0\r\n%s"
			"Content-type: application/x-www-form-urlencoded\r\n"
			"Content-length: %d\r\n"
			"User-agent: mothra/%s\r\n",
			urlname, authstr, len, version);
		break;
	}
	if(gotcookies && (cookiefd=open("/mnt/webcookies/http", ORDWR)) >= 0){
		if(fprint(cookiefd, "%s", url->fullname) > 0){
			while((m=read(cookiefd, buf, sizeof buf)) > 0){
				if(m+n>ncom){
					if(write(fd, com, n)!= n){
						free(com);
						goto fdErrReturn;
					}
					n=0;
					com[0] = '\0';
				}
				strncat(com, buf, m);
				n += m;
			}
		}else{
			close(cookiefd);
			cookiefd=-1;
		}
	}
	strcat(com, "\r\n");
	n += 2;
	switch(method){
	case GET:
		if(write(fd, com, n)!=n){
			free(com);
			goto fdErrReturn;
		}
		break;
	case POST:
		if(write(fd, com, n)!=n
		|| write(fd, body, len)!=len){
			free(com);
			goto fdErrReturn;
		}
		break;
	}
	free(com);
	if(pipe(pfd)==-1) goto fdErrReturn;
	n=read(fd, buf, 1024);
	if(n<=0){
	EarlyEof:
		if(n==0){
			fprint(2, "%s: EOF in header\n", url->fullname);
			werrstr("EOF in header");
		}
	pfdErrReturn:
		close(pfd[0]);
		close(pfd[1]);
	fdErrReturn:
		close(fd);
	ErrReturn:
		if(cookiefd>=0)
			close(cookiefd);
		cacheclose(&cache, 0);
		return -1;
	}
	bp=buf;
	ebp=buf+n;
	url->type=0;
	if(strncmp(buf, "HTTP/", 5)==0){	/* hack test for presence of header */
		SET(response);
		gotresponse=0;
		url->redirname[0]='\0';
		nnl=0;
		lp=line;
		elp=line+1024;
		while(nnl!=2){
			if(bp==ebp){
				n=read(fd, buf, 1024);
				if(n<=0) goto EarlyEof;
				ebp=buf+n;
				bp=buf;
			}
			if(*bp!='\r'){
				if(nnl==1 && (!gotresponse || (*bp!=' ' && *bp!='\t'))){
					*lp='\0';
					if(gotresponse){
						if(cookiefd>=0 && cistrncmp(line, "Set-Cookie:", 11) == 0)
							fprint(cookiefd, "%s\n", line);
						httpheader(url, line);
					}else{
						response=httpresponse(line);
						gotresponse=1;
					}
					lp=line;
				}
				if(*bp=='\n') nnl++;
				else{
					nnl=0;
					if(lp!=elp) *lp++=*bp;
				}
			}
			bp++;
		}
		if(gotresponse) switch(response){
		case 200:	/* OK */
		case 201:	/* Created */
		case 202:	/* Accepted */
			break;
		case 204:	/* No Content */
			werrstr("URL has no content");
			goto pfdErrReturn;
		case 301:	/* Moved Permanently */
		case 302:	/* Moved Temporarily */
			if(url->redirname[0]){
				url->type=FORWARD;
				werrstr("URL forwarded");
				goto pfdErrReturn;
			}
			break;
		case 304:	/* Not Modified */
			if(cfd!=-1){
				url->type=cache.type;
				close(pfd[0]);
				close(pfd[1]);
				close(fd);
				if(cookiefd>=0)
					close(cookiefd);
				return cfd;
			}
			werrstr("Not modified!");
			goto pfdErrReturn;
		case 400:	/* Bad Request */
			werrstr("Bad Request to server");
			goto pfdErrReturn;
		case 401:	/* Unauthorized */
		case 402:	/* ??? */
			if(*authstr == 0){
				close(pfd[0]);
				close(pfd[1]);
				close(fd);
				if(auth(url, authstr, sizeof(authstr)) == 0){
					if(cfd!=-1)
						close(cfd);
					goto Authorize;
				}
				goto ErrReturn;
			}
			break;
		case 403:	/* Forbidden */
			werrstr("Forbidden by server");
			goto pfdErrReturn;
		case 404:	/* Not Found */
			werrstr("Not found on server");
			goto pfdErrReturn;
		case 500:	/* Internal server error */
			werrstr("Server choked");
			goto pfdErrReturn;
		case 501:	/* Not implemented */
			werrstr("Server can't do it!");
			goto pfdErrReturn;
		case 502:	/* Bad gateway */
			werrstr("Bad gateway");
			goto pfdErrReturn;
		case 503:	/* Service unavailable */
			werrstr("Service unavailable");
			goto pfdErrReturn;
		}
	}
	if(cfd!=-1){
		close(cfd);
		cfd=cacheopen(url, &cache, 1);
	}
	if(cookiefd>=0){
		close(cookiefd);
		cookiefd=-1;
	}
	if(url->type==0)
		url->type=suffix2type(url->fullname);
	if(cache.fd!=-1) fprint(cache.fd, "type %d\n", url->type);
	switch(rfork(RFFDG|RFPROC|RFNOWAIT)){
	case -1:
		werrstr("Can't fork");
		goto pfdErrReturn;
	case 0:
		notify(exitnow); /* otherwise write on closed pipe below may cause havoc */
		close(pfd[0]);
		if(bp!=ebp){
			write(pfd[1], bp, ebp-bp);
			if(cache.fd!=-1) write(cache.fd, bp, ebp-bp);
		}
		while((n=read(fd, buf, 1024))>0){
			write(pfd[1], buf, n);
			if(cache.fd!=-1) write(cache.fd, buf, n);
		}
		cacheclose(&cache, 1);
		_exits(0);
	default:
		if(cache.fd!=-1) close(cache.fd);
		close(pfd[1]);
		close(fd);
		return pfd[0];
	}
}
/*
 * Process a header line for this url
 */
void httpheader(Url *url, char *line){
	char *name, *arg, *s, *arg2;
	name=line;
	while(*name==' ' || *name=='\t') name++;
	for(s=name;*s!=':';s++) if(*s=='\0') return;
	*s++='\0';
	while(*s==' ' || *s=='\t') s++;
	arg=s;
	while(*s!=' ' && *s!='\t' && *s!=';' && *s!='\0') s++;
	while(*s == ' ' || *s == '\t' || *s == ';')
		*s++ = '\0';
	arg2 = s;
	if(cistrcmp(name, "Content-Type")==0){
		url->type|=content2type(arg, url->reltext);
		if(cistrncmp(arg2, "charset=", 8) == 0){
			strncpy(url->charset, arg2+8, sizeof(url->charset));
		} else {
			url->charset[0] = '\0';
		}
	}
	else if(cistrcmp(name, "Content-Encoding")==0)
		url->type|=encoding2type(arg);
	else if(cistrcmp(name, "WWW-authenticate")==0){
		strncpy(url->authtype, arg, sizeof(url->authtype));
		strncpy(url->autharg, arg2, sizeof(url->autharg));
	}
	else if(cistrcmp(name, "URI")==0){
		if(*arg!='<') return;
		++arg;
		for(s=arg;*s!='>';s++) if(*s=='\0') return;
		*s='\0';
		strncpy(url->redirname, arg, sizeof(url->redirname));
	}
	else if(cistrcmp(name, "Location")==0)
		strncpy(url->redirname, arg, sizeof(url->redirname));
}
int httpresponse(char *line){
	while(*line!=' ' && *line!='\t' && *line!='\0') line++;
	return atoi(line);
}
