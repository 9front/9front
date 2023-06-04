#include <u.h>
#include <libc.h>
#include <auth.h>
#include <mp.h>
#include <libsec.h>

#define GET4(p)	(u32int)(p)[3] | (u32int)(p)[2]<<8 | (u32int)(p)[1]<<16 | (u32int)(p)[0]<<24

static char *magic = "openssh-key-v1";

static uchar *pubkey;
static uchar *pubend;

static uchar *privkey;
static uchar *privend;

static void*
emalloc(ulong size, int clr)
{
	void *p;
	if((p = mallocz(size, clr)) == nil)
		sysfatal("emalloc: %r");
	return p;
}

static long
ereadn(int fd, void *buf, long nbytes)
{
	long n;
	if((n = readn(fd, buf, nbytes)) != nbytes)
		sysfatal("ereadn: %r");
	return n;
}

static uchar*
slurp(int fd, int alloc, u32int *np)
{
	u32int n;
	uchar buf[4], trash[1024];
	uchar *p;

	ereadn(fd, buf, sizeof buf);
	n = GET4(buf);
	if(alloc){
		*np = n;
		p = emalloc(n, 0);
		ereadn(fd, p, n);
		return p;
	}
	if(n >= sizeof trash)
		sysfatal("key component too large");
	ereadn(fd, trash, n);
	return nil;
}

static u32int
decode(int fd)
{
	uchar buf[4];
	u32int ret, n;

	/* ciphername */
	slurp(fd, 0, nil);
	/* kdfname */
	slurp(fd, 0, nil);
	/* kdfoptions */
	slurp(fd, 0, nil);

	ereadn(fd, buf, 4);
	ret = GET4(buf);

	pubkey = slurp(fd, 1, &n);
	pubend = pubkey + n;

	privkey = slurp(fd, 1, &n);
	privend = privkey + n;

	return ret;
}

static u32int
scan(uchar **p, uchar *e)
{
	u32int n;

	if(*p + 4 > e)
		sysfatal("unexpected end of key");
	n = GET4(*p); *p += 4;
	if(*p + n > e)
		sysfatal("unexpected end of key");
	return n;
}

static mpint*
scanmp(uchar **p, uchar *e)
{
	mpint *m;
	u32int n;

	n = scan(p, e);
	if(n == 0)
		sysfatal("required key component has zero length");
	m = betomp(*p, n, nil);
	if(m == nil)
		sysfatal("betomp: %r");
	*p += n;
	return m;
}

static RSApriv*
fill(void)
{
	char *rsa = "ssh-rsa";
	u32int n, a, b;
	uchar *p, *e;
	mpint *ek, *mod, *dk, *p0, *q0;

	p = pubkey;
	e = pubend;
	if(scan(&p, e) != 7 || memcmp(rsa, p, 7) != 0)
		sysfatal("not a RSA key");
	p += 7;

	ek = scanmp(&p, e);
	mod = scanmp(&p, e);

	p = privkey;
	e = privend;
	if(p + 8 >= e)
		sysfatal("unexpected end of key");
	a = GET4(p); p += 4;
	b = GET4(p); p += 4;
	if(a != b)
		sysfatal("private key seems encrypted");

	if(scan(&p, e) != 7 || memcmp(rsa, p, 7) != 0)
		sysfatal("not a RSA key");
	p += 7;

	/* public components are repeated */
	n = scan(&p, e); p += n;
	n = scan(&p, e); p += n;
	dk = scanmp(&p, e);
	/* iq */
	n = scan(&p, e); p += n;
	p0 = scanmp(&p, e);
	q0 = scanmp(&p, e);

	return rsafill(mod, ek, dk, p0, q0);
}

void
usage(void)
{
	fprint(2, "usage: auth/ssh2rsa [file]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int fd;
	RSApriv *p;
	char *s;
	uchar buf[64];

	fd = 0;
	fmtinstall('B', mpfmt);
	ARGBEGIN{
	default:
		usage();
	}ARGEND
	switch(argc){
	default:
		usage();
	case 1:
		fd = open(argv[0], OREAD);
		if(fd < 0)
			sysfatal("open: %r");
	case 0:
		break;
	}

	ereadn(fd, buf, strlen(magic)+1);
	if(memcmp(buf, magic, strlen(magic)+1) != 0)
		sysfatal("bad magic");

	if(decode(fd) != 1)
		sysfatal("invalid key");
	if((p = fill()) == nil)
		sysfatal("fill: %r");

	s = smprint("%s size=%d ek=%B !dk=%B n=%B !p=%B !q=%B !kp=%B !kq=%B !c2=%B\n",
		"key service=ssh proto=rsa",
		mpsignif(p->pub.n), p->pub.ek,
		p->dk, p->pub.n, p->p, p->q,
		p->kp, p->kq, p->c2);
	if(s == nil)
		sysfatal("smprint: %r");
	if(write(1, s, strlen(s)) != strlen(s))
		sysfatal("write: %r");

	exits(nil);
}
