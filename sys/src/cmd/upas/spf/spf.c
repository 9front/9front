#include "spf.h"

#define	vprint(...) if(vflag) fprint(2, __VA_ARGS__)

enum{
	Traw,
	Tip4,
	Tip6,
	Texists,
	Tall,
	Tbegin,
	Tend,
};

char *typetab[] = {
	"raw",
	"ip4",
	"ip6",
	"exists",
	"all",
	"begin",
	"end",
};

typedef struct Squery Squery;
struct Squery{
	char	ver;
	char	sabort;
	char	mod;
	char	*cidrtail;
	char	*ptrmatch;
	char	*ip;
	char	*domain;
	char	*sender;
	char	*hello;
};

typedef struct Spf Spf;
struct Spf{
	char	mod;
	char	type;
	char	s[100];
};
#pragma	varargck type	"§"	Spf*

char	*txt;
char	*netroot = "/net";
char	dflag;
char	eflag;
char	mflag;
char	pflag;
char	rflag;
char	vflag;

char *vtab[] = {0, "v=spf1", "spf2.0/"};

char*
isvn(Squery *q, char *s, int i)
{
	char *p, *t;

	t = vtab[i];
	if(cistrncmp(s, t, strlen(t)))
		return 0;
	p = s + strlen(t);
	if(i == 2){
		p = strchr(p, ' ');
		if(p == nil)
			return 0;
	}
	if(*p && *p++ != ' ')
		return 0;
	q->ver = i;
	return p;
}

char*
pickspf(Squery *s, char *v1, char *v2)
{
	switch(s->ver){
	default:
	case 0:
		if(v1)
			return v1;
		return v2;
	case 1:
		if(v1)
			return v1;
		return 0;
	case 2:
		if(v2)
			return v2;
		return v1;	/* spf2.0/pra,mfrom */
	}
}

char *ftab[] = {"txt", "spf"};	/* p. 9 */

char*
spffetch(Squery *s, char *d)
{
	char *p, *v1, *v2;
	int i;
	Ndbtuple *t, *n;

	if(txt){
		p = strdup(txt);
		txt = 0;
		return p;
	}
	v1 = v2 = 0;
	for(i = 0; i < nelem(ftab); i++){
		t = vdnsquery(d, ftab[i], 0);
		for(n = t; n; n = n->entry){
			if(strcmp(n->attr, ftab[i]))
				continue;
			v1 = isvn(s, n->val, 1);
			v2 = isvn(s, n->val, 2);
		}
		if(p = pickspf(s, v1, v2))
			p = strdup(p);
		ndbfree(t);
		if(p)
			return p;
	}
	return 0;
}

Spf	spftab[200];
int	nspf;
int	mod;

Spf*
spfadd(int type, char *s)
{
	Spf *p;

	if(nspf >= nelem(spftab))
		return 0;
	p = spftab+nspf;
	p->s[0] = 0;
	if(s)
		snprint(p->s, sizeof p->s, "%s", s);
	p->type = type;
	p->mod = mod;
	nspf++;
	return p;
}

int
parsecidr(uchar *addr, uchar *mask, char *from)
{
	char *p, buf[50];
	int i, bits, z;
	vlong v;
	uchar *a;

	strecpy(buf, buf+sizeof buf, from);
	if(p = strchr(buf, '/'))
		*p = 0;
	v = parseip(addr, buf);
	if(v == -1)
		return -1;
	switch((ulong)v){
	default:
		bits = 32;
		z = 96;
		break;
	case 6:
		bits = 128;
		z = 0;
		break;
	}

	if(p){
		i = strtoul(p+1, &p, 0);
		if(i > bits)
			i = bits;
		i += z;
		memset(mask, 0, 128/8);
		for(a = mask; i >= 8; i -= 8)
			*a++ = 0xff;
		if(i > 0)
			*a = ~((1<<(8-i))-1);
	}else
		memset(mask, 0xff, IPaddrlen);
	return 0;
}

/*
 * match x.y.z.w to x1.y1.z1.w1/m
 */
int
cidrmatch(char *x, char *y)
{
	uchar a[IPaddrlen], b[IPaddrlen], m[IPaddrlen];

	if(parseip(a, x) == -1)
		return 0;
	parsecidr(b, m, y);
	maskip(a, m, a);
	maskip(b, m, b);
	if(!memcmp(a, b, IPaddrlen))
		return 1;
	return 0;
}

int
ptrmatch(Squery *q, char *s)
{
	if(!q->ptrmatch || !strcmp(q->ptrmatch, s))
		return 1;
	return 0;
}

Spf*
spfaddcidr(Squery *q, int type, char *s)
{
	char buf[64];

	if(q->cidrtail){
		snprint(buf, sizeof buf, "%s/%s", s, q->cidrtail);
		s = buf;
	}
	if(ptrmatch(q, s))
		return spfadd(type, s);
	return 0;
}

char*
qpluscidr(Squery *q, char *d, int recur, int *y)
{
	char *p;

	*y = 0;
	if(!recur && (p = strchr(d, '/'))){
		q->cidrtail = p + 1;
		*p = 0;
		*y = 1;
	}
	return d;
}

void
cidrtail(Squery *q, char *, int y)
{
	if(!y)
		return;
	q->cidrtail[-1] = '/';
	q->cidrtail = 0;
}

void
aquery(Squery *q, char *d, int recur)
{
	int y;
	Ndbtuple *t, *n;

	d = qpluscidr(q, d, recur, &y);
	t = vdnsquery(d, "any", recur);
	for(n = t; n; n = n->entry){
		if(!strcmp(n->attr, "ip"))
			spfaddcidr(q, Tip4, n->val);
		else if(!strcmp(n->attr, "ipv6"))
			spfaddcidr(q, Tip6, n->val);
		else if(!strcmp(n->attr, "cname"))
			aquery(q, d, recur+1);
	}
	cidrtail(q, d, y);
	ndbfree(t);
}

void
mxquery(Squery *q, char *d, int recur)
{
	int i, y;
	Ndbtuple *t, *n;

	d = qpluscidr(q, d, recur, &y);
	i = 0;
	t = vdnsquery(d, "mx", recur);
	for(n = t; n; n = n->entry)
		if(i++ < 10 && !strcmp(n->attr, "mx"))
			aquery(q, n->val, recur+1);
	ndbfree(t);
	cidrtail(q, d, y);
}

void
ptrquery(Squery *q, char *d, int recur)
{
	char *s, buf[64];
	int i, y;
	Ndbtuple *t, *n;

	if(!q->ip){
		fprint(2, "spf: ptr query; no ip\n");
		return;
	}
	d = qpluscidr(q, d, recur, &y);
	i = 0;
	dnreverse(buf, sizeof buf, s = strdup(q->ip));
	t = vdnsquery(buf, "ptr", recur);
	for(n = t; n; n = n->entry){
		if(!strcmp(n->attr, "dom") || !strcmp(n->attr, "cname"))
		if(i++ < 10 && dncontains(d, n->val)){
			q->ptrmatch = q->ip;
			aquery(q, n->val, recur+1);
			q->ptrmatch = 0;
		}
	}
	ndbfree(t);
	free(s);
	cidrtail(q, d, y);
}

/*
 * this looks very wrong; see §5.7 which says only a records match.
 */
void
exists(Squery*, char *d, int recur)
{
	Ndbtuple *t;

	if(t = vdnsquery(d, "ip", recur))
		spfadd(Texists, "1");
	else
		spfadd(Texists, 0);
	ndbfree(t);
}

void
addfail(void)
{
	mod = '-';
	spfadd(Tall, 0);
}

void
addend(char *s)
{
	spfadd(Tend, s);
	spftab[nspf-1].mod = 0;
}

Spf*
includeloop(char *s1, int n)
{
	char *s, *p;
	int i;

	for(i = 0; i < n; i++){
		s = spftab[i].s;
		if(s)
		if(p = strstr(s, " -> "))
		if(!strcmp(p+4, s1))
			return spftab+i;
	}
	return nil;
}

void
addbegin(int c, char *s0, char *s1)
{
	char buf[0xff];

	snprint(buf, sizeof buf, "%s -> %s", s0, s1);
	spfadd(Tbegin, buf);
	spftab[nspf-1].mod = c;
}

void
ditch(void)
{
	if(nspf > 0)
		nspf--;
}

static void
lower(char *s)
{
	int c;

	for(; c = *s; s++)
		if(c >= 'A' && c <= 'Z')
			*s = c + 0x20;
}

int
spfquery(Squery *x, char *d, int include)
{
	char *s, **t, *r, *p, *q, buf[10];
	int i, n, c;
	Spf *inc;

	if(include)
	if(inc = includeloop(d, nspf-1)){
		fprint(2, "spf: include loop: %s (%s)\n", d, inc->s);
		return -1;
	}
	s = spffetch(x, d);
	if(!s)
		return -1;
	t = malloc(500*sizeof *t);
	n = getfields(s, t, 500, 1, " ");
	x->sabort = 0;
	for(i = 0; i < n && !x->sabort; i++){
		if(!strncmp(t[i], "v=", 2))
			continue;
		c = *t[i];
		r = t[i]+1;
		switch(c){
		default:
			mod = '+';
			r--;
			break;
		case '-':
		case '~':
		case '+':
		case '?':
			mod = c;
			break;
		}
		if(!strcmp(r, "all")){
			spfadd(Tall, 0);
			continue;
		}
		strecpy(buf, buf+sizeof buf, r);
		p = strchr(buf, ':');
		if(p == 0)
			p = strchr(buf, '=');
		q = d;
		if(p){
			*p = 0;
			q = p+1;
			q = r+(q-buf);
		}
		if(!mflag)
			q = macro(q, x->sender, x->domain, x->hello, x->ip);
		else
			q = strdup(q);
		lower(buf);
		if(!strcmp(buf, "ip4"))
			spfaddcidr(x, Tip4, q);
		else if(!strcmp(buf, "ip6"))
			spfaddcidr(x, Tip6, q);
		else if(!strcmp(buf, "a"))
			aquery(x, q, 0);
		else if(!strcmp(buf, "mx"))
			mxquery(x, d, 0);
		else if(!strcmp(buf, "ptr"))
			ptrquery(x, d, 0);
		else if(!strcmp(buf, "exists"))
			exists(x, q, 0);
		else if(!strcmp(buf, "include") || !strcmp(buf, "redirect")){
			if(q && *q){
				if(rflag)
					fprint(2, "I> %s\n", q);
				addbegin(mod, r, q);
				if(spfquery(x, q, 1) == -1){
					ditch();
					addfail();
				}else
					addend(r);
			}
		}
		free(q);
	}
	free(t);
	free(s);
	return 0;
}

char*
url(char *s)
{
	char buf[64], *p, *e;
	int c;

	p = buf;
	e = p + sizeof buf;
	*p = 0;
	while(c = *s++){
		if(c >= 'A' && c <= 'Z')
			c += 0x20;
		if(c <= ' ' || c == '%' || c & 0x80)
			p = seprint(p, e, "%%%2.2X", c);
		else
			p = seprint(p, e, "%c", c);
	}
	return strdup(buf);
}

void
spfinit(Squery *q, char *dom, int argc, char **argv)
{
	uchar a[IPaddrlen];

	memset(q, 0, sizeof q);
	q->ip = argc>0? argv[1]: 0;
	if(q->ip && parseip(a, q->ip) == -1)
		sysfatal("bogus ip");
	q->domain = url(dom);
	q->sender = argc>2? url(argv[2]): 0;
	q->hello = argc>3? url(argv[3]): 0;
	mod = 0;				/* BOTCH */
}

int
§fmt(Fmt *f)
{
	char *p, *e, buf[115];
	Spf *spf;

	spf = va_arg(f->args, Spf*);
	if(!spf)
		return fmtstrcpy(f, "<nil>");
	e = buf+sizeof buf;
	p = buf;
	if(spf->mod && spf->mod != '+')
		*p++ = spf->mod;
	p = seprint(p, e, "%s", typetab[spf->type]);
	if(spf->s[0])
		seprint(p, e, " : %s", spf->s);
	return fmtstrcpy(f, buf);
}

static Spf head;

struct{
	int	i;
}walk;

int
invertmod(int c)
{
	switch(c){
	case '?':
		return '?';
	case '+':
		return '-';
	case '-':
		return '+';
	case '~':
		return '?';
	}
	return 0;
}

#define reprint(...) if(vflag && recur == 0) fprint(2, __VA_ARGS__)

int
spfwalk(int all, int recur, char *ip)
{
	int match, bias, mod, r;
	Spf *s;

	r = 0;
	bias = 0;
	if(recur == 0)
		walk.i = 0;
	for(; walk.i < nspf; walk.i++){
		s = spftab+walk.i;
		mod = s->mod;
		switch(s->type){
		default:
			abort();
		case Tbegin:
			walk.i++;
			match = spfwalk(s->s[0] == 'r', recur+1, ip);
			if(match < 0)
				mod = invertmod(mod);
			break;
		case Tend:
			return r;
		case Tall:
			match = 1;
			break;
		case Texists:
			match = s->s[0];
			break;
		case Tip4:
		case Tip6:
			match = cidrmatch(ip, s->s);
			break;
		}
		if(!r && match)
			switch(mod){
			case '~':
				reprint("bias %§\n", s);
				bias = '~';
			case '?':
				break;
			case '-':
				if(all || s->type !=Tall){
					vprint("fail %§\n", s);
					r = -1;
				}
				break;
			case '+':
			default:
				vprint("match %§\n", s);
				r = 1;
			}
	}
	/* recur == 0 */
	if(r == 0 && bias == '~')
		r = -1;
	return r;
}

/* ad hoc and noncomprehensive */
char *tccld[] = {"au", "ca", "gt", "id", "pk",  "uk", "ve", };
int
is3cctld(char *s)
{
	int i;

	if(strlen(s) != 2)
		return 0;
	for(i = 0; i < nelem(tccld); i++)
		if(!strcmp(tccld[i], s))
			return 1;
	return 0;
}

char*
rootify(char *d)
{
	char *p, *q;

	if(!(p = strchr(d, '.')))
		return 0;
	p++;
	if(!(q = strchr(p, '.')))
		return 0;
	q++;
	if(!strchr(q, '.') && is3cctld(q))
		return 0;
	return p;
}

void
usage(void)
{
	fprint(2, "spf [-demrpv] [-n netroot] dom [ip sender helo]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *s, *d, *e;
	int i, j, t[] = {0, 3};
	Squery q;

	ARGBEGIN{
	case 'd':
		dflag = 1;
		break;
	case 'e':
		eflag = 1;
		break;
	case 'm':
		mflag = 1;
		break;
	case 'n':
		netroot = EARGF(usage());
		break;
	case 'p':
		pflag = 1;
		break;
	case 'r':
		rflag = 1;
		break;
	case 't':
		txt = EARGF(usage());
		break;
	case 'v':
		vflag = 1;
		break;
	default:
		usage();
	}ARGEND

	if(argc < 1 || argc > 4)
		usage();
	if(argc == 1)
		pflag = 1;
	fmtinstall(L'§', §fmt);
	fmtinstall('I', eipfmt);
	fmtinstall('M', eipfmt);

	e = "none";
	for(i = 0; i < nelem(t); i++){
		if(argc <= t[i])
			break;
		d = argv[t[i]];
		for(j = 0; j < i; j++)
			if(!strcmp(argv[t[j]], d))
				goto loop;
		for(s = d; ; s = rootify(s)){
			if(!s)
				goto loop;
			spfinit(&q, d, argc, argv);	/* or s? */
			addbegin('+', ".", s);
			if(spfquery(&q, s, 0) != -1)
				break;
		}
		if(eflag && nspf)
			addfail();
		e = "";
		if(pflag)
		for(j = 0; j < nspf; j++)
			print("%§\n", spftab+j);
		if(argc >= t[i] && argc > 1)
		if(spfwalk(1, 0, argv[1]) == -1)
			exits("fail");
loop:;
	}
	exits(e);
}
