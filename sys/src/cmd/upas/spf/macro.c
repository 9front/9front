#include "spf.h"

#define mrprint(...)	snprint(m->mreg, sizeof m->mreg, __VA_ARGS__)

typedef struct Mfmt Mfmt;
typedef struct Macro Macro;

struct Mfmt{
	char	buf[0xff];
	char	*p;
	char	*e;

	char	mreg[0xff];
	int	f1;
	int	f2;
	int	f3;

	char	*sender;
	char	*domain;
	char	*ip;
	char	*helo;
	uchar	ipa[IPaddrlen];
};

struct Macro{
	char	c;
	void	(*f)(Mfmt*);
};

static void
ms(Mfmt *m)
{
	mrprint("%s", m->sender);
}

static void
ml(Mfmt *m)
{
	char *p;

	mrprint("%s", m->sender);
	if(p = strchr(m->mreg, '@'))
		*p = 0;
}

static void
mo(Mfmt *m)
{
	mrprint("%s", m->domain);
}

static void
md(Mfmt *m)
{
	mrprint("%s", m->domain);
}

static void
mi(Mfmt *m)
{
	uint i, c;

	if(isv4(m->ipa))
		mrprint("%s", m->ip);
	else{
		for(i = 0; i < 32; i++){
			c = m->ipa[i / 2];
			if((i & 1) == 0)
				c >>= 4;
			sprint(m->mreg+2*i, "%ux.", c & 0xf);
		}
		m->mreg[2*32 - 1] = 0;
	}
}

static int
maquery(Mfmt *m, char *d, char *match, int recur)
{
	int r;
	Ndbtuple *t, *n;

	r = 0;
	t = vdnsquery(d, "any", recur);
	for(n = t; n; n = n->entry)
		if(!strcmp(n->attr, "ip") || !strcmp(n->attr, "ipv6")){
			if(!strcmp(n->val, match)){
				r = 1;
				break;
			}
		}else if(!strcmp(n->attr, "cname"))
			maquery(m, d, match, recur+1);
	ndbfree(t);
	return r;
}

static int
lrcmp(char *a, char *b)
{
	return strlen(b) - strlen(a);
}

static void
mptrquery(Mfmt *m, char *d, int recur)
{
	char *s, buf[64], *a, *list[11];
	int nlist, i;
	Ndbtuple *t, *n;

	nlist = 0;
	dnreverse(buf, sizeof buf, s = strdup(m->ip));
	t = vdnsquery(buf, "ptr", recur);
	for(n = t; n; n = n->entry){
		if(!strcmp(n->attr, "dom") || !strcmp(n->attr, "cname"))
		if(dncontains(n->val, d) && maquery(m, n->val, m->ip, recur+1))
			list[nlist++] = strdup(n->val);
	}
	ndbfree(t);
	free(s);
	qsort(list, nlist, sizeof *list, (int(*)(void*,void*))lrcmp);
	a = "unknown";
	for(i = 0; i < nlist; i++)
		if(!strcmp(list[i], d)){
			a = list[i];
			break;
		}else if(dncontains(list[i], d))
			a = list[i];
	mrprint("%s", a);
	for(i = 0; i < nlist; i++)
		free(list[i]);
}

static void
mp(Mfmt *m)
{
	/*
	 * we're supposed to do a reverse lookup on the ip & compare.
	 * this is a very bad idea.
	 */
//	mrprint("unknown);	/* simulate dns failure */
	mptrquery(m, m->domain, 0);
}

static void
mv(Mfmt *m)
{
	if(isv4(m->ipa))
		mrprint("in-addr");
	else
		mrprint("ip6");
}

static void
mh(Mfmt *m)
{
	mrprint("%s", m->helo);
}

static Macro tab[] = {
's',	ms,	/* sender */
'l',	ml,	/* local part of sender */
'o',	mo,	/* domain of sender */
'd',	md,	/* domain */
'i',	mi,	/* ip */
'p',	mp,	/* validated domain name of ip */
'v',	mv,	/* "in-addr" if ipv4, or "ip6" if ipv6 */
'h',	mh,	/* helo/ehol domain */
};

static void
reverse(Mfmt *m)
{
	char *p, *e, buf[100], *f[32], sep[2];
	int i, n;

	sep[0] = m->f2;
	sep[1] = 0;
	n = getfields(m->mreg, f, nelem(f), 0, sep);
	p = e = buf;
	e += sizeof buf-1;
	for(i = 0; i < n; i++)
		p = seprint(p, e, "%s.", f[n-i-1]);
	if(p > buf)
		p--;
	*p = 0;
	memmove(m->mreg, buf, p-buf+1);
	m->f2 = '.';
}

static void
chop(Mfmt *m)
{
	char *p, *e, buf[100], *f[32], sep[2];
	int i, n;

	sep[0] = m->f2;
	sep[1] = 0;
	n = getfields(m->mreg, f, nelem(f), 0, sep);
	p = e = buf;
	e += sizeof buf-1;
	if(m->f1 == 0)
		i = 0;
	else
		i = n-m->f1;
	if(i < 0)
		i = 0;
	for(; i < n; i++)
		p = seprint(p, e, "%s.", f[i]);
	if(p > buf)
		p--;
	*p = 0;
	memmove(m->mreg, buf, p-buf+1);
	m->f2 = '.';
}

static void
mfmtinit(Mfmt *m, char *s, char *d, char *h, char *i)
{
	memset(m, 0, sizeof *m);
	m->p = m->buf;
	m->e = m->p + sizeof m->buf-1;
	m->sender = s? s: "Unsets";
	m->domain = d? d: "Unsetd";
	m->helo = h? h: "Unseth";
	m->ip = i? i: "127.0.0.2";
	parseip(m->ipa, m->ip);
}

/* url escaping? rfc3986 */
static void
mputc(Mfmt *m, int c)
{
	if(m->p < m->e)
		*m->p++ = c;
}

static void
mputs(Mfmt *m, char *s)
{
	int c;

	while(c = *s++)
		mputc(m, c);
}

char*
macro(char *f, char *sender, char *dom, char *hdom, char *ip)
{
	char *p;
	int i, c;
	Mfmt m;

	mfmtinit(&m, sender, dom, hdom, ip);
	while(*f){
		while((c = *f++) && c != '%')
			mputc(&m, c);
		if(c == 0)
			break;
		switch(*f++){
		case '%':
			mputc(&m, '%');
			break;
		case '-':
			mputs(&m, "%20");
			break;
		case '_':
			mputc(&m, ' ');
			break;
		case '{':
			m.f1 = 0;
			m.f2 = '.';
			m.f3 = 0;
			c = *f++;
			if(c >= 'A' && c <= 'Z')
				c += 0x20;
			for(i = 0; i < nelem(tab); i++)
				if(tab[i].c == c)
					break;
			if(i == nelem(tab))
				return 0;
			for(c = *f++; c >= '0' && c <= '9'; c = *f++)
				m.f1 = m.f1*10 + c-'0';
			if(c == 'R' || c == 'r'){
				m.f3 = 'r';
				c = *f++;
			}
			for(; p = strchr(".-+,_=", c); c = *f++)
				m.f2 = *p;
			if(c == '}'){
				tab[i].f(&m);
				if(m.f1 || m.f2 != '.')
					chop(&m);
				if(m.f3 == 'r')
					reverse(&m);
				mputs(&m, m.mreg);
				m.mreg[0] = 0;
				break;
			}
		default:
			return 0;
		}
	}
	mputc(&m, 0);
	return strdup(m.buf);
}
