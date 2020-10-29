#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

int
unpack(uchar *b, uchar *p, uchar *e, char *f, ...)
{
	va_list a;
	int r;

	va_start(a, f);
	r = vunpack(b, p, e, f, a);
	va_end(a);
	return r;
}

int
pack(uchar *b, uchar *p, uchar *e, char *f, ...)
{
	va_list a;
	int r;

	va_start(a, f);
	r = vpack(b, p, e, f, a);
	va_end(a);
	return r;
}

int
vunpack(uchar *b, uchar *p, uchar *e, char *f, va_list a)
{
	struct {
		void *prev;
		int o, c, i;
		uchar *e;
		uchar **ap, **ae;
	} sub[8], *sp, *sa;
	int (*funpack)(uchar *, uchar *, uchar *, void *);
	char c, ff[2];
	int i, x, n;
	uchar *t;

	memset(sub, 0, sizeof(sub));
	for(sp = sub; sp < sub+nelem(sub); sp++){
		sp->o = -1;
		sp->c = -1;
		sp->i = 1;
	}

	t = p;
	sp = nil;
	sa = sub;
	while(c = *f++){
		switch(c){
		case '_':
		case 'b':
			if(p >= e)
				return 0;
			if(c == 'b')
				*va_arg(a, int*) = *p;
			p++;
			break;
		case 'w':
			if(p+1 >= e)
				return 0;
			*va_arg(a, int*) = (int)p[1]<<8 | (int)p[0];
			p+=2;
			break;
		case 'l':
			if(p+3 >= e)
				return 0;
			*va_arg(a, int*) = (int)p[3]<<24 | (int)p[2]<<16 | (int)p[1]<<8 | (int)p[0];
			p+=4;
			break;
		case 'v':
			if(p+7 >= e)
				return 0;
			*va_arg(a, vlong*) =
					(vlong)p[7]<<56 |
					(vlong)p[6]<<48 |
					(vlong)p[5]<<40 |
					(vlong)p[4]<<32 |
					(vlong)p[3]<<24 |
					(vlong)p[2]<<16 |
					(vlong)p[1]<<8 |
					(vlong)p[0];
			p += 8;
			break;
		case '%':
			x = *f++ - '0';
			while((p - b) % x)
				p++;
			break;
		case 'f':
			funpack = va_arg(a, void*);
			if((n = funpack(b, p, e, va_arg(a, void*))) == 0)
				return 0;
			p += n;
			break;
		case '#':
		case '@':
			x = *f++ - '0';
			ff[0] = *f++;
			ff[1] = 0;
			if((n = unpack(b, p, e, ff, &i)) == 0)
				return 0;
			p += n;
			if(c == '#'){
				sub[x].c = i;
			} else {
				sub[x].o = i;
			}
			break;
		case '{':
		case '[':
			sa->prev = sp;
			sp = sa++;
			if(*f == '*'){
				sp->i = f[1]-'0';
				f += 2;
			}
			if(sp->o >= 0 && b + sp->o > p)
				if(b + sp->o <= e || *f != '?')
					p = b + sp->o;
			if(*f == '?')
				f++;
			sp->o = p - b;
			sp->e = e;
			if(sp->c >= 0){
				e = p + sp->c * sp->i;
				if(e > sp->e)
					return 0;
			}
			if(c == '['){
				sp->ap = va_arg(a, uchar**);
				sp->ae = va_arg(a, uchar**);
			}
			break;
		case '}':
		case ']':
			e = sp->e;
			if(sp->c < 0)
				sp->c = ((p - (b + sp->o))+sp->i-1)/sp->i;
			p = b + sp->o + sp->c * sp->i;
			if(p > e)
				return 0;
			if(sp->ap)
				*sp->ap = b + sp->o;
			if(sp->ae)
				*sp->ae = p;
			sp = sp->prev;
			break;
		case '.':
			*va_arg(a, uchar**) = p;
			break;
		}
		if(p > e)
			return 0;
	}
	return p - t;
}

int
vpack(uchar *b, uchar *p, uchar *e, char *f, va_list a)
{
	struct {
		void *prev;
		int o, i;
		uchar *wc, *wo, wcf, wof;
	} sub[8], *sp, *sa;
	int (*fpack)(uchar *, uchar *, uchar *, void *);
	char c, ff[2];
	int i, x, n;
	vlong v;
	uchar *t;

	memset(sub, 0, sizeof(sub));
	for(sp = sub; sp < sub+nelem(sub); sp++){
		sp->o = -1;
		sp->i = 1;
	}

	t = p;
	sp = nil;
	sa = sub;
	while(c = *f++){
		switch(c){	
		case '_':
		case 'b':
			if(p >= e)
				return 0;
			if(c == 'b')
				*p++ = va_arg(a, int);
			else
				*p++ = 0;
			break;
		case 'w':
			if(p+1 >= e)
				return 0;
			i = va_arg(a, int);
			*p++ = i & 0xFF;
			*p++ = i>>8 & 0xFF;
			break;
		case 'l':
			if(p+3 >= e)
				return 0;
			i = va_arg(a, int);
			*p++ = i & 0xFF;
			*p++ = i>>8 & 0xFF;
			*p++ = i>>16 & 0xFF;
			*p++ = i>>24 & 0xFF;
			break;
		case 'v':
			if(p+7 >= e)
				return 0;
			v = va_arg(a, vlong);
			*p++ = v & 0xFF;
			*p++ = v>>8 & 0xFF;
			*p++ = v>>16 & 0xFF;
			*p++ = v>>24 & 0xFF;
			*p++ = v>>32 & 0xFF;
			*p++ = v>>40 & 0xFF;
			*p++ = v>>48 & 0xFF;
			*p++ = v>>56 & 0xFF;
			break;
		case '%':
			x = *f++ - '0';
			while((p - b) % x){
				if(p >= e)
					return 0;
				*p++ = 0;
			}
			break;
		case 'f':
			fpack = va_arg(a, void*);
			if((n = fpack(b, p, e, va_arg(a, void*))) == 0)
				return 0;
			p += n;
			break;
		case '#':
		case '@':
			x = *f++ - '0';
			ff[0] = *f++;
			ff[1] = 0;
			if((n = pack(b, p, e, ff, 0)) == 0)
				return 0;
			if(c == '#'){
				sub[x].wc = p;
				sub[x].wcf = ff[0];
			} else {
				sub[x].wo = p;
				sub[x].wof = ff[0];
			}
			p += n;
			break;
		case '{':
		case '[':
			sa->prev = sp;
			sp = sa++;
			if(*f == '*'){
				sp->i = f[1]-'0';
				f += 2;
			}
			if(*f == '?')
				f++;
			sp->o = p - b;
			if(c == '['){
				uchar *s, *se;

				s = va_arg(a, uchar*);
				se = va_arg(a, uchar*);
				n = se - s;
				if(n < 0 || p + n > e)
					return 0;
				if(p != s)
					memmove(p, s, n);
				p += n;
			}
			break;
		case '}':
		case ']':
			n = ((p - (b + sp->o))+sp->i-1)/sp->i;
			p = b + sp->o + n * sp->i;
			if(sp->wc){
				ff[0] = sp->wcf;
				ff[1] = 0;
				pack(b, sp->wc, e, ff, n);
			}
			if(sp->wo){
				ff[0] = sp->wof;
				ff[1] = 0;
				pack(b, sp->wo, e, ff, sp->o);
			}
			sp = sp->prev;
			break;
		case '.':
			*va_arg(a, uchar**) = p;
			break;
		}
		if(p > e)
			return 0;
	}
	return p - t;
}

