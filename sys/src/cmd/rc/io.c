#include "rc.h"
#include "exec.h"
#include "io.h"
#include "fns.h"

enum {
	NBUF = IOUNIT,
	IOBUF_GROW_FACTOR = 2,
	MIN_ALLOC = 100,
};

void
vpfmt(io *f, char *fmt, va_list ap)
{
	if(f == nil || fmt == nil)
		return;
	
	for(;*fmt;fmt++) {
		if(*fmt != '%') {
			pchr(f, *fmt);
			continue;
		}
		if(*++fmt == '\0')		/* "blah%"? */
			break;
		switch(*fmt){
		case 'c':
			pchr(f, va_arg(ap, int));
			break;
		case 'd':
			pdec(f, va_arg(ap, int));
			break;
		case 'o':
			poct(f, va_arg(ap, unsigned));
			break;
		case 'p':
			pptr(f, va_arg(ap, void*));
			break;
		case 'Q':
			pquo(f, va_arg(ap, char *));
			break;
		case 'q':
			pwrd(f, va_arg(ap, char *));
			break;
		case 's':
			pstr(f, va_arg(ap, char *));
			break;
		case 't':
			pcmd(f, va_arg(ap, tree *));
			break;
		case 'v':
			pval(f, va_arg(ap, word *));
			break;
		case '%':
			pchr(f, '%');
			break;
		default:
			pchr(f, *fmt);
			break;
		}
	}
}

void
pfmt(io *f, char *fmt, ...)
{
	va_list ap;
	
	if(f == nil || fmt == nil)
		return;
	
	va_start(ap, fmt);
	vpfmt(f, fmt, ap);
	va_end(ap);
}

void
pchr(io *b, int c)
{
	if(b == nil)
		return;
	if(b->bufp >= b->ebuf)
		flushio(b);
	*b->bufp++ = c;
}

int
rchr(io *b)
{
	if(b == nil)
		return EOF;
	if(b->bufp >= b->ebuf)
		return emptyiobuf(b);
	return *b->bufp++;
}

char*
rstr(io *b, char *stop)
{
	char *s, *p;
	int l, m, n;

	if(b == nil || stop == nil)
		return nil;

	do {
		l = rchr(b);
		if(l == EOF)
			return nil;
	} while(l && strchr(stop, l));
	b->bufp--;

	s = nil;
	l = 0;
	for(;;){
		p = (char*)b->bufp;
		n = (char*)b->ebuf - p;
		if(n > 0){
			for(m = 0; m < n; m++){
				if(strchr(stop, p[m]) == nil)
					continue;

				b->bufp += m+1;
				if(m > 0 || s == nil){
					s = erealloc(s, l+m+1);
					memmove(s+l, p, m);
					l += m;
				}
				s[l] = '\0';
				return s;
			}
			s = erealloc(s, l+m+1);
			memmove(s+l, p, m);
			l += m;
			b->bufp += m;
		}
		if(emptyiobuf(b) == EOF){
			if(s) 
				s[l] = '\0';
			return s;
		}
		b->bufp--;
	}
}

void
pquo(io *f, char *s)
{
	if(f == nil)
		return;
	if(s == nil)
		s = "";
	
	pchr(f, '\'');
	for(;*s;s++){
		if(*s == '\'')
			pchr(f, *s);
		pchr(f, *s);
	}
	pchr(f, '\'');
}

void
pwrd(io *f, char *s)
{
	char *t;
	
	if(f == nil)
		return;
	if(s == nil)
		s = "";
	
	for(t = s; *t; t++)
		if(*t >= 0 && (*t <= ' ' || strchr("`^#*[]=|\\?${}()'<>&;", *t)))
			break;
	
	if(t == s || *t)
		pquo(f, s);
	else 
		pstr(f, s);
}

void
pptr(io *f, void *p)
{
	static char hex[] = "0123456789ABCDEF";
	unsigned long long v;
	int n;

	if(f == nil)
		return;

	v = (unsigned long long)p;
	if(sizeof(v) == sizeof(p) && v >> 32)
		for(n = 60; n >= 32; n -= 4)
			pchr(f, hex[(v >> n) & 0xF]);
	for(n = 28; n >= 0; n -= 4)
		pchr(f, hex[(v >> n) & 0xF]);
}

void
pstr(io *f, char *s)
{
	if(f == nil)
		return;
	if(s == nil)
		s = "(null)";
	while(*s) 
		pchr(f, *s++);
}

void
pdec(io *f, int n)
{
	if(f == nil)
		return;
	
	if(n < 0){
		n = -n;
		if(n >= 0){
			pchr(f, '-');
			pdec(f, n);
			return;
		}
		n = 1 - n;
		pchr(f, '-');
		pdec(f, n / 10);
		pchr(f, n % 10 + '1');
		return;
	}
	if(n > 9)
		pdec(f, n / 10);
	pchr(f, n % 10 + '0');
}

void
poct(io *f, unsigned n)
{
	if(f == nil)
		return;
	if(n > 7)
		poct(f, n >> 3);
	pchr(f, (n & 7) + '0');
}

void
pval(io *f, word *a)
{
	if(f == nil || a == nil)
		return;
	
	while(a->next && a->next->word){
		pwrd(f, (char *)a->word);
		pchr(f, ' ');
		a = a->next;
	}
	pwrd(f, (char *)a->word);
}

io*
newio(unsigned char *buf, int len, int fd)
{
	io *f;
	
	if(buf == nil && len > 0)
		return nil;
	
	f = new(io);
	f->buf = buf;
	f->bufp = buf;
	f->ebuf = buf + len;
	f->fd = fd;
	return f;
}

io*
openiostr(void)
{
	unsigned char *buf = emalloc(MIN_ALLOC + 1);
	memset(buf, '\0', MIN_ALLOC + 1);
	return newio(buf, MIN_ALLOC, -1);
}

char*
closeiostr(io *f)
{
	void *buf;
	
	if(f == nil)
		return nil;
	
	buf = f->buf;
	free(f);
	return buf;
}

io*
openiofd(int fd)
{
	if(fd < 0)
		return nil;
	return newio(emalloc(NBUF), 0, fd);
}

io*
openiocore(void *buf, int len)
{
	if(buf == nil || len < 0)
		return nil;
	return newio(buf, len, -1);
}

void
flushio(io *f)
{
	int n, newsize;

	if(f == nil)
		return;

	if(f->fd < 0){
		n = f->ebuf - f->buf;
		newsize = n * IOBUF_GROW_FACTOR;
		f->buf = erealloc(f->buf, newsize + 1);
		f->bufp = f->buf + n;
		f->ebuf = f->buf + newsize;
		memset(f->bufp, '\0', (newsize - n) + 1);
	}
	else{
		n = f->bufp - f->buf;
		if(n && Write(f->fd, f->buf, n) != n){
			if(ntrap)
				dotrap();
		}
		f->bufp = f->buf;
		f->ebuf = f->buf + NBUF;
	}
}

void
closeio(io *f)
{
	if(f == nil)
		return;
	if(f->fd >= 0) 
		Close(f->fd);
	free(closeiostr(f));
}

int
emptyiobuf(io *f)
{
	int n;
	
	if(f == nil || f->fd < 0)
		return EOF;
	
	n = Read(f->fd, f->buf, NBUF);
	if(n <= 0)
		return EOF;
	
	f->bufp = f->buf;
	f->ebuf = f->buf + n;
	return *f->bufp++;
}
