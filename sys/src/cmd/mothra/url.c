#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <panel.h>
#include "mothra.h"

static int
hexdigit(int c)
{
	if(c >= '0' && c <= '9')
		return c-'0';
	if(c >= 'a' && c <= 'f')
		return c-'a'+10;
	if(c >= 'A' && c <= 'F')
		return c-'A'+10;
	return -1;
}

static int
dechex(uchar *out, int lim, char *in, int n)
{
	uchar *start, *end;
	int c;

	start = out;
	end = start + lim;
	while(n-- > 0 && out < end){
		c = *in++;
		if(c == 0)
			break;
		if(c & 0x80)
			return -1;
		if(c == '%'){
			n -= 2;
			if(n < 0 || (c = hexdigit(*in++)) == -1)
				return -1;
			if((c = (c << 4) | hexdigit(*in++)) == -1)
				return -1;
		}
		*out++ = c;
	}
	return out - start;
}

static int
dataget(Url *url)
{
	int (*decfun)(uchar *, int, char *, int) = dechex;
	char *s, *p;
	int fd, n, m;

	s = url->reltext;
	if(cistrncmp(s, "data:", 5) != 0)
		return -1;
	s += 5;
	if((p = strchr(s, ',')) != nil){
		*p = 0;
		if(strstr(s, "base64") != nil)
			decfun = dec64;
		*p = ',';
		s = p+1;
	} else
		s = strchr(s, 0);
	n = strlen(s);
	m = n+64;
	p = malloc(m);
	strcpy(p, "/tmp/duXXXXXXXXXXX");
	if((fd = create(mktemp(p), ORDWR|ORCLOSE, 0600)) < 0){
		free(p);
		return -1;
	}
	if((m = (*decfun)((uchar*)p, m, s, n)) < 0 || write(fd, p, m) != m){
		free(p);
		close(fd);
		return -1;
	}
	free(p);
	seek(fd, 0, 0);
	return fd;
}

static int
fileget(Url *url)
{
	char *rel, *base, *x;

	rel = base = nil;
	if(cistrncmp(url->basename, "file:", 5) == 0)
		base = url->basename+5;
	if(cistrncmp(url->reltext, "file:", 5) == 0)
		rel = url->reltext+5;
	if(rel == nil && base == nil)
		return -1;
	if(rel == nil)
		rel = url->reltext;
	if(base && base[0] == '/' && rel[0] != '/'){
		if(x = strrchr(base, '/'))
			*x = 0;
		snprint(url->fullname, sizeof(url->fullname), "%s/%s", base, rel);
		if(x)	*x = '/';
	}else
		snprint(url->fullname, sizeof(url->fullname), "%s", rel);
	url->tag[0] = 0;
	if(x = strrchr(url->fullname, '#')){
		*x++ = 0;
		nstrcpy(url->tag, x, sizeof(url->tag));
	}
	base = cleanname(url->fullname);
	x = base + strlen(base)+1;
	if((x - base) > sizeof(url->fullname)-5)
		return -1;
	memmove(url->fullname+5, base, x - base);
	memmove(url->fullname, "file:", 5);
	return open(url->fullname+5, OREAD);
}

char *mtpt="/mnt/web";

static int
webclone(Url *url, char *buf, int nbuf)
{
	int n, conn, fd;

	snprint(buf, nbuf, "%s/clone", mtpt);
	if((fd = open(buf, ORDWR)) < 0)
		return -1;
	if((n = read(fd, buf, nbuf-1)) <= 0){
		close(fd);
		return -1;
	}
	buf[n] = 0;
	conn = atoi(buf);
	if(url && url->reltext[0]){
		if(url->basename[0]){
			n = snprint(buf, nbuf, "baseurl %s", url->basename);
			write(fd, buf, n);
		}
		n = snprint(buf, nbuf, "url %s", url->reltext);
		if(write(fd, buf, n) < 0){
			close(fd);
			return -1;
		}
	}
	snprint(buf, nbuf, "%s/%d", mtpt, conn);
	return fd;
}

static int
readstr(char *path, char *buf, int nbuf){
	int n, fd;

	n = 0;
	if((fd = open(path, OREAD)) >= 0){
		if((n = read(fd, buf, nbuf-1)) < 0)
			n = 0;
		close(fd);
	}
	buf[n] = 0;
	return n;
}

int
urlpost(Url *url, char *ctype)
{
	char buf[1024];
	int n, fd;

	if((fd = webclone(url, buf, sizeof(buf))) < 0)
		return -1;
	if(ctype && *ctype)
		fprint(fd, "contenttype %s", ctype);
	n = strlen(buf);
	snprint(buf+n, sizeof(buf)-n, "/postbody");
	n = open(buf, OWRITE);
	close(fd);
	return n;
}

int
urlget(Url *url, int body)
{
	char buf[1024];
	int n, fd;

	if(body < 0){
		if((fd = dataget(url)) >= 0)
			return fd;
		if((fd = fileget(url)) >= 0)
			return fd;
		if((fd = webclone(url, buf, sizeof(buf))) < 0)
			return -1;
	}else{
		char *x;

		if(fd2path(body, buf, sizeof(buf))){
			close(body);
			return -1;
		}
		if(x = strrchr(buf, '/'))
			*x = 0;
		fd = open(buf, OREAD);
		close(body);
	}
	n = strlen(buf);
	snprint(buf+n, sizeof(buf)-n, "/body");
	body = open(buf, OREAD);
	close(fd);
	fd = body;
	if(fd < 0)
		return -1;

	snprint(buf+n, sizeof(buf)-n, "/parsed/url");
	readstr(buf, url->fullname, sizeof(url->fullname));

	snprint(buf+n, sizeof(buf)-n, "/parsed/fragment");
	readstr(buf, url->tag, sizeof(url->tag));

	snprint(buf+n, sizeof(buf)-n, "/contentencoding");
	readstr(buf, buf, sizeof(buf));

	if(!cistrcmp(buf, "compress"))
		fd = pipeline(fd, "exec uncompress");
	else if(!cistrcmp(buf, "gzip"))
		fd = pipeline(fd, "exec gunzip");
	else if(!cistrcmp(buf, "bzip2"))
		fd = pipeline(fd, "exec bunzip2");

	return fd;
}

int
urlresolve(Url *url)
{
	char buf[1024];
	int n, fd;

	if((fd = webclone(url, buf, sizeof(buf))) < 0)
		return -1;
	n = strlen(buf);
	snprint(buf+n, sizeof(buf)-n, "/parsed/url");
	readstr(buf, url->fullname, sizeof(url->fullname));
	snprint(buf+n, sizeof(buf)-n, "/parsed/fragment");
	readstr(buf, url->tag, sizeof(url->tag));
	close(fd);
	return 0;
}
