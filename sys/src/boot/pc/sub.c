#include <u.h>
#include <a.out.h>
#include "fns.h"
#include "mem.h"

void
memset(void *dst, int v, int n)
{
	uchar *d = dst;

	while(n > 0){
		*d++ = v;
		n--;
	}
}

void
memmove(void *dst, void *src, int n)
{
	uchar *d = dst;
	uchar *s = src;

	if(d < s){
		while(n-- > 0)
			*d++ = *s++;
	} else if(d > s){
		s += n;
		d += n;
		while(n-- > 0)
			*--d = *--s;
	}
}

int
memcmp(void *src, void *dst, int n)
{
	uchar *d = dst;
	uchar *s = src;
	int r = 0;

	while(n-- > 0)
		if(r = (*d++ - *s++))
			break;

	return r;
}

int
strlen(char *s)
{
	char *p = s;

	while(*p)
		p++;

	return p - s;
}

char*
strchr(char *s, int c)
{
	for(; *s; s++)
		if(*s == c)
			return s;

	return 0;
}

char*
strrchr(char *s, int c)
{
	char *r = 0;

	while(s = strchr(s, c))
		r = s++;

	return r;
}

void
print(char *s)
{
	while(*s)
		putc(*s++);
}

int
readn(void *f, void *data, int len)
{
	uchar *p, *e;

	putc(' ');
	p = data;
	e = p + len;
	while(p < e){
		if(((ulong)p & 0xF000) == 0){
			putc('\b');
			putc(hex[((ulong)p>>16)&0xF]);
		}
		if((len = read(f, p, e - p)) <= 0)
			break;
		p += len;
	}
	putc('\b');

	return p - (uchar*)data;
}

static int
readline(void *f, char buf[64])
{
	static char white[] = "\t ";
	char *p;

	p = buf;
	do{
		if(!f)
			putc('>');
		while(p < buf + 64-1){
			if(!f){
				putc(*p = getc());
				if(*p == '\r')
					putc('\n');
				else if(*p == '\b' && p > buf){
					p--;
					continue;
				}
			}else if(read(f, p, 1) <= 0)
				return 0;
			if(p == buf && strchr(white, *p))
				continue;
			if(strchr(crnl, *p))
				break;
			p++;
		}
		while(p > buf && strchr(white, p[-1]))
			p--;
	}while(p == buf);
	*p = 0;

	return p - buf;
}

static int
timeout(int ms)
{
	while(ms > 0){
		if(gotc())
			return 1;
		usleep(100000);
		ms -= 100;
	}
	return 0;
}

#define BOOTLINE	((char*)CONFADDR)
#define BOOTLINELEN	64
#define BOOTARGS	((char*)(CONFADDR+BOOTLINELEN))
#define	BOOTARGSLEN	(4096-0x200-BOOTLINELEN)

char *confend;

static void apmconf(int);
static void e820conf(void);

static int
delconf(char *s)
{
	char *p, *e;

	for(p = BOOTARGS; p < confend; p = e){
		for(e = p+1; e < confend; e++){
			if(*e == '\n'){
				e++;
				break;
			}
		}
		if(!memcmp(p, s, strlen(s))){
			memmove(p, e, confend - e);
			confend -= e - p;
			*confend = 0;
			return 1;
		}
	}
	return 0;
}

char*
configure(void *f, char *path)
{
	char line[64], *kern, *s, *p;
	int inblock, nowait, n;

Clear:
	kern = 0;
	nowait = 1;
	inblock = 0;

	memset(BOOTLINE, 0, BOOTLINELEN);

	confend = BOOTARGS;
	memset(confend, 0, BOOTARGSLEN);

	e820conf();
Loop:
	while(readline(f, line) > 0){
		if(*line == 0 || strchr("#;=", *line))
			continue;
		if(*line == '['){
			inblock = memcmp("[common]", line, 8);
			continue;
		}
		if(!memcmp("boot", line, 5)){
			nowait=1;
			break;
		}
		if(!memcmp("wait", line, 5)){
			nowait=0;
			continue;
		}
		if(!memcmp("clear", line, 5)){
			if(line[5] == 0){
				print("ok");
				print(crnl);
				goto Clear;
			} else if(line[5] == ' ' && delconf(line+6)){
				print("ok");
				print(crnl);
			}
			continue;
		}
		if(inblock || (p = strchr(line, '=')) == nil)
			continue;
		*p++ = 0;
		delconf(line);
		if(!memcmp("apm", line, 3)){
			apmconf('0' - line[3]);
			continue;
		}
		if(!memcmp("bootfile", line, 8))
			memmove(kern = path, p, strlen(p)+1);

		s = confend;
		memmove(confend, line, n = strlen(line)); confend += n;
		*confend++ = '=';
		memmove(confend, p, n = strlen(p)); confend += n;
		*confend = 0;

		print(s); print(crnl);

		*confend++ = '\n';
		*confend = 0;
	}

	if(f){
		close(f);
		f = 0;

		if(kern && (nowait==0 || timeout(1000)))
			goto Loop;
	}

	if(!kern){
		print("no bootfile\r\n");
		goto Loop;
	}
	if(p = strrchr(kern, '!'))
		kern = p+1;

	return kern;
}


static void
hexfmt(char *s, int i, ulong a)
{
	s += i;
	while(i > 0){
		*--s = hex[a&15];
		a >>= 4;
		i--;
	}
}

static void
addconfx(char *s, int w, ulong v)
{
	int n;

	n = strlen(s);
	memmove(confend, s, n);
	hexfmt(confend+n, w, v);
	confend += n+w;
	*confend = 0;
}

void apm(int id);

static void
apmconf(int id)
{
	uchar *a;
	char *s;

	a = (uchar*)CONFADDR;
	memset(a, 0, 20);

	apm(id);
	if(memcmp(a, "APM", 4))
		return;

	s = confend;

	addconfx("apm", 1, id);
	addconfx("=ax=", 4, *((ushort*)(a+4)));
	addconfx(" ebx=", 8, *((ulong*)(a+12)));
	addconfx(" cx=", 4, *((ushort*)(a+6)));
	addconfx(" dx=", 4, *((ushort*)(a+8)));
	addconfx(" di=", 4, *((ushort*)(a+10)));
	addconfx(" esi=", 8, *((ulong*)(a+16)));

	print(s); print(crnl);

	*confend++ = '\n';
	*confend = 0;
}

ulong e820(ulong bx, void *p);

static void
e820conf(void)
{
	struct {
		uvlong	base;
		uvlong	len;
		ulong	typ;
		ulong	ext;
	} e;
	uvlong v;
	ulong bx;
	char *s;

	bx=0;
	s = confend;

	do{
		e.base = 0;
		e.len = 0;
		e.typ = 0;
		e.ext = 1;
		bx = e820(bx, &e);
		if(e.typ == 1 && e.len != 0 && (e.ext & 3) == 1){
			if(confend == s){
				/* single entry <= 1MB is useless */
				if(bx == 0 && e.len <= 0x100000)
					break;
				memmove(confend, "e820=", 5);
				confend += 5;
			}
			v = e.base;
			addconfx("", 8, v>>32);
			addconfx("", 8, v&0xffffffff);
			v += e.len;
			addconfx(" ", 8, v>>32);
			addconfx("", 8, v&0xffffffff);
			*confend++ = ' ';
		}
	} while(bx);

	if(confend == s)
		return;

	*confend = 0;
	print(s); print(crnl);

	*confend++ = '\n';
	*confend = 0;
}

static ushort
beswab(ushort s)
{
	uchar *p;

	p = (uchar*)&s;
	return (p[0]<<8) | p[1];
}

static ulong
beswal(ulong l)
{
	uchar *p;

	p = (uchar*)&l;
	return (p[0]<<24) | (p[1]<<16) | (p[2]<<8) | p[3];
}

void a20(void);

char*
bootkern(void *f)
{
	uchar *e, *d, *t;
	ulong n;
	Exec ex;

	a20();

	if(readn(f, &ex, sizeof(ex)) != sizeof(ex))
		return "bad header";
	if(beswal(ex.magic) != I_MAGIC)
		return "bad magic";

	e = (uchar*)(beswal(ex.entry) & ~0xF0000000UL);
	t = e;
	n = beswal(ex.text);

	if(readn(f, t, n) != n)
		goto Error;
	d = (uchar*)PGROUND((ulong)t + n);
	n = beswal(ex.data);

	if(readn(f, d, n) != n)
		goto Error;
	close(f);
	unload();

	print("boot");
	print(crnl);

	jump(e);

Error:		
	return "i/o error";
}
