#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

void
logit(char *fmt, ...)
{
	char buf[8192];
	va_list arg;

	va_start(arg, fmt);
	vseprint(buf, buf + sizeof(buf), fmt, arg);
	va_end(arg);
	if(debug)
		fprint(2, "%s\n", buf);
	syslog(0, progname, "(%s\\%s) %s", remotesys, remoteuser, buf);
}

char*
getremote(char *dir)
{
	int fd, n;
	char remfile[256];
	static char buf[256];

	sprint(remfile, "%s/remote", dir);
	fd = open(remfile, OREAD);
	if(fd < 0)
		return nil;
	if((n = read(fd, buf, sizeof(buf)-1))>0)
		buf[n-1] = 0;
	else
		strcpy(buf, remfile);
	close(fd);
	return buf;
}

char*
conspath(char *base, char *name)
{
	return cleanname(smprint("%s/%s", base, name ? name : ""));
}

int
splitpath(char *path, char **base, char **name)
{
	char *p, *b;

	b = strdup(path);
	if((p = strrchr(b, '/')) == nil){
		free(b);
		if(name)
			*name = nil;
		if(base)
			*base = nil;
		return 0;
	}
	if(p == b){
		if(name) *name = strdup(p+1);
		p[1] = 0;
	} else {
		*p++ = 0;
		if(name) *name = strdup(p);
	}
	if(base)
		*base = b;
	else
		free(b);
	return 1;
}

void
dumphex(char *s, uchar *h, uchar *e)
{
	int i, n;

	n = e - h;
	for(i=0; i<n; i++){
		if((i % 16) == 0)
			fprint(2, "%s%s: [%.4x] ", i ? "\n" : "", s, i);
		fprint(2, "%.2x ", (int)h[i]);
	}
	fprint(2, "\n");
}

void
todatetime(long time, int *pdate, int *ptime)
{
	Tm *tm;

	tm = gmtime(time);
	if(pdate)
		*pdate = (tm->mday) | ((tm->mon + 1) << 5) | ((tm->year - 80) << 9);
	if(ptime)
		*ptime = (tm->sec >> 1) | (tm->min << 5) | (tm->hour << 11);
}

long
fromdatetime(int date, int time)
{
	Tm tm;

	memset(&tm, 0, sizeof(tm));
	strcpy(tm.zone, "GMT");
	tm.mday = date & 0x1f;
	tm.mon = ((date >> 5) & 0xf) - 1;
	tm.year = (date >> 9) + 80;
	tm.yday = 0;
	tm.sec = (time & 0x1f) << 1;
	tm.min = (time >> 5) & 0x3f;
	tm.hour = time >> 11;
	return tm2sec(&tm);
}

vlong
tofiletime(long time)
{
	return ((vlong)time + 11644473600LL) * 10000000;
}

long
fromfiletime(vlong filetime)
{
	return filetime / 10000000 - 11644473600LL;
}


int
filesize32(vlong size)
{
	if(size > 0xFFFFFFFFUL)
		return 0xFFFFFFFF;
	return size;
}

vlong
allocsize(vlong size, int blocksize)
{
	return ((size + blocksize-1)/blocksize)*blocksize;
}

int
extfileattr(Dir *d)
{
	int a;

	a = (d->qid.type & QTDIR) ? ATTR_DIRECTORY : ATTR_NORMAL;
	if((d->qid.type & QTTMP) == 0)
		a |= ATTR_ARCHIVE;
	if((d->mode & 0222) == 0)
		a |= ATTR_READONLY;
	if(d->name[0] == '.' && d->name[1] && d->name[1] != '.')
		a |= ATTR_HIDDEN;
	return a;
}

int
dosfileattr(Dir *d)
{
	return extfileattr(d) & DOSMASK;
}

ulong
namehash(char *s)
{
	ulong h, t;
	Rune r;

	h = 0;
	while(*s){
		s += chartorune(&r, s);
		r = toupperrune(r);
		t = h & 0xf8000000;
		h <<= 5;
		h ^= t>>27;
		h ^= (ulong)r;
	}
	return h;
}

char*
strtr(char *s, Rune (*tr)(Rune))
{
	char buf[UTFmax], *p, *w;
	Rune r;
	int n;

	p = s;
	w = s;
	while(*p){
		p += chartorune(&r, p);
		r = (*tr)(r);
		n = runetochar(buf, &r);
		if(w + n <= p){
			memmove(w, buf, n);
			w += n;
		}
	}
	*w = 0;
	return s;
}

char*
strchrs(char *s, char *c)
{
	Rune r;
	int n;

	while(*s){
		n = chartorune(&r, s);
		if(strchr(c, r))
			return s;
		s += n;
	}
	return nil;
}

static int
strpack8(uchar *, uchar *p, uchar *e, char *s, int term, Rune (*tr)(Rune))
{
	uchar *t;
	Rune r;

	t = p;
	while((p < e) && *s){
		s += chartorune(&r, s);
		r = tr(r);
		*p++ = r & 0x7F;
	}
	if(p >= e)
		return 0;
	if(term)
		*p++ = 0;
	return p - t;
}

static int
strpack16(uchar *b, uchar *p, uchar *e, char *s, int term, Rune (*tr)(Rune))
{
	unsigned int rr;
	uchar *t;
	Rune r;

	t = p;
	if((p - b) % 2){
		if(p >= e)
			return 0;
		*p++ = 0;
	}
	while((p+1 < e) && *s){
		s += chartorune(&r, s);
		rr = tr(r);
		if(rr > 0xFFFF){
			if(p+3 >= e)
				break;
			rr -= 0x10000;
			*p++ = (rr>>10) & 0xFF;
			*p++ = ((rr>>18)&3) + 0xD8;
			*p++ = rr & 0xFF;
			*p++ = ((rr>>8)&3) + 0xDC;
		} else {
			*p++ = rr & 0xFF;
			*p++ = rr>>8;
		}
	}
	if(p+1 >= e)
		return 0;
	if(term){
		*p++ = 0;
		*p++ = 0;
	}
	return p - t;
}

static int
strunpack8(uchar *, uchar *p, uchar *e, char **dp, int term, Rune (*tr)(Rune))
{
	uchar *t;
	char *d;
	Rune r;
	int n;

	t = p;
	n = 0;
	while((p < e) && (!term || *p)){
		p++;
		n++;
	}
	if(term && ((p >= e) || *p))
		return 0;
	p -= n;
	*dp = d = malloc(n*UTFmax+1);
	while(n--){
		r = *p & 0x7F;
		r = tr(r);
		d += runetochar(d, &r);
		p++;
	}
	*d = 0;
	if(term)
		p++;
	return p - t;
}

static int
strunpack16(uchar *b, uchar *p, uchar *e, char **dp, int term, Rune (*tr)(Rune))
{
	unsigned int rr;
	uchar *t;
	char *d;
	Rune r;
	int n;

	t = p;
	if((p - b) % 2)
		p++;
	n = 0;
	while((p+1 < e) && (!term || (p[0] || p[1]))){
		p += 2;
		n++;
	}
	if(term && ((p+1 >= e) || p[0] || p[1]))
		return 0;
	p -= 2*n;
	*dp = d = malloc(n*UTFmax+1);
	while(n--){
		if(p[1] >= 0xD8 && p[1] <= 0xDB){
			if(!n--)
				break;
			rr = ((p[0]<<10) | ((p[1]-0xD8)<<18) | p[2] | ((p[3]-0xDC)<<8))+0x10000;
			p += 2;
		} else
			rr = p[0] | (p[1]<<8);
		r = tr(rr);
		d += runetochar(d, &r);
		p += 2;
	}
	*d = 0;
	if(term)
		p += 2;
	return p - t;
}

static Rune
notr(Rune r)
{
	return r;
}

static Rune
fromnametr(Rune r)
{
	switch(r){
	case '\\':
		return '/';
	case ' ':
		if(trspaces)
			return 0xa0;
	}
	return r;
}

static Rune
tonametr(Rune r)
{
	switch(r){
	case '/':
		return '\\';
	case 0xa0:
		if(trspaces)
			return ' ';
	}
	return r;
}

int smbstrpack8(uchar *b, uchar *p, uchar *e, void *arg){
	return strpack8(b, p, e, (char*)arg, 1, notr);
}
int smbstrpack16(uchar *b, uchar *p, uchar *e, void *arg){
	return strpack16(b, p, e, (char*)arg, 1, notr);
}
int smbstrunpack8(uchar *b, uchar *p, uchar *e, void *arg){
	return strunpack8(b, p, e, (char**)arg, 1, notr);
}
int smbstrunpack16(uchar *b, uchar *p, uchar *e, void *arg){
	return strunpack16(b, p, e, (char**)arg, 1, notr);
}
int smbnamepack8(uchar *b, uchar *p, uchar *e, void *arg){
	return strpack8(b, p, e, (char*)arg, 1, tonametr);
}
int smbnamepack16(uchar *b, uchar *p, uchar *e, void *arg){
	return strpack16(b, p, e, (char*)arg, 1, tonametr);
}
int smbnameunpack8(uchar *b, uchar *p, uchar *e, void *arg){
	return strunpack8(b, p, e, (char**)arg, 1, fromnametr);
}
int smbnameunpack16(uchar *b, uchar *p, uchar *e, void *arg){
	return strunpack16(b, p, e, (char**)arg, 1, fromnametr);
}
int smbuntermstrpack8(uchar *b, uchar *p, uchar *e, void *arg){
	return strpack8(b, p, e, (char*)arg, 0, notr);
}
int smbuntermstrpack16(uchar *b, uchar *p, uchar *e, void *arg){
	return strpack16(b, p, e, (char*)arg, 0, notr);
}
int smbuntermnamepack8(uchar *b, uchar *p, uchar *e, void *arg){
	return strpack8(b, p, e, (char*)arg, 0, tonametr);
}
int smbuntermnamepack16(uchar *b, uchar *p, uchar *e, void *arg){
	return strpack16(b, p, e, (char*)arg, 0, tonametr);
}
