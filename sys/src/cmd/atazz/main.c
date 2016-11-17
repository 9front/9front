#include <u.h>
#include <libc.h>
#include <fis.h>
#include "atazz.h"
#include "tabs.h"

#pragma	varargck	argpos	eprint	1
#pragma	varargck	type	"π"	char**

enum {
	Dontread	= -2,
};

int	interrupted;
int	rflag;
int	squelch;
int	scttrace;
uchar	issuetr[0x100];

Atatab	*idcmd;
Atatab	*idpktcmd;
Atatab	*sigcmd;
Atatab	*sctread;
Atatab	*sctissue;

int
πfmt(Fmt *f)
{
	char **p;

	p = va_arg(f->args, char**);
	if(p == nil)
		return fmtstrcpy(f, "<nil**>");
	for(; *p; p++){
		fmtstrcpy(f, *p);
		if(p[1] != nil)
			fmtstrcpy(f, " ");
	}
	return 0;
}

int
eprint(char *fmt, ...)
{
	int n;
	va_list args;

	if(squelch)
		return 0;
//	Bflush(&out);

	va_start(args, fmt);
	n = vfprint(2, fmt, args);
	va_end(args);
	return n;
}

void
fisset(Req *r, uint i, uint v)
{
	if(r->fisbits & 1<<i)
		return;
	r->fisbits |= 1<<i;
	r->cmd.fis[i] = v;
}

void
prreq(Req *r)
{
	uchar *u;

	print("%.2ux %.2ux\n", r->cmd.sdcmd, r->cmd.ataproto);
	u = r->cmd.fis;
	print("%.2ux%.2ux%.2ux%.2ux%.2ux%.2ux%.2ux%.2ux ",
		u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7]);
	u += 8;
	print("%.2ux%.2ux%.2ux%.2ux%.2ux%.2ux%.2ux%.2ux\n",
		u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7]);
}

char*
protostr(char *p, char *e, int pr)
{
	char *s;

	*p = 0;
	if(pr & P28)
		p = seprint(p, e, "28:");
	else
		p = seprint(p, e, "28:");
	switch(pr & Pprotom){
	default:
		s = "unk";
		break;
	case Ppkt:
		s = "pkt";
		break;
	case Pdiag:
		s = "dig";
		break;
	case Preset:
		s = "rst";
		break;
	case Pdmq:
		s = "dmq";
		break;
	case Pdma:
		s = "dma";
		break;
	case Ppio:
		s = "pio";
		break;
	}
	p = seprint(p, e, "%s:", s);
	switch(pr & Pdatam){
	default:
		s = "nd";
		break;
	case Pin:
		s = "in";
		break;
	case Pout:
		s = "out";
		break;
	}
	p = seprint(p, e, "%s", s);
	return p;
}

void
displaycmd(Req *r, Atatab *a)
{
	char buf[32];
	int i;

	if(a->cc > nelem(issuetr) || !issuetr[a->cc])
		return;
	protostr(buf, buf + sizeof buf, a->protocol);
	fprint(2, "cmd %s:%2ux ", buf, a->cc);
	for(i = 0; i < 16; i++)
		fprint(2, "%.2ux", r->cmd.fis[i]);
	fprint(2, "\n");
}

int
issueata(Req *r, Atatab *a, Dev *d)
{
	uchar u;
	int n, ok, pr, rv;

	r->haverfis = 0;
	pr = a->protocol & Pdatam;
	r->data = realloc(r->data, r->count);
	if(r->data == nil && r->count > 0)
		sysfatal("realloc: %r");
	if(r->data == nil && pr != Pnd)
		sysfatal("no data for cmd %.2ux", a->cc);
	if(0 && r->fisbits)
		print("fisbits %.16b\n", r->fisbits);
	r->cmd.sdcmd = 0xff;
	r->cmd.ataproto = a->protocol;
	if(a->cc & 0xff00)
		fisset(r, 0, a->cc >> 8);
	else
		fisset(r, 0, H2dev);
	fisset(r, 1, Fiscmd);
	fisset(r, 2, a->cc);
	switch(pr){
	case Pout:
		if(r->rfd != Dontread){
			n = readn(r->rfd, r->data, r->count);
			if(n != r->count){
				if(n == -1)
					eprint("!short src read %r\n");
				else
					eprint("!short src read %d wanted %lld\n", n, r->count);
				return -1;
			}
		}
	case Pnd:
	case Pin:
		if(0 && (d->feat & Dlba) == 0 && (d->c | d->h | d->s)){
			int c, h, s;
			c = r->lba / (d->s * d->h);
			h = (r->lba / d->s) % d->h;
			s = (r->lba % d->s) + 1;
print("%d %d %d\n", c, h, s);
			fisset(r, 4, s);
			fisset(r, 5, c);
			fisset(r, 6, c >> 8);
			fisset(r, 7, Ataobs | h);
		}else{
			fisset(r, 4, r->lba);
			fisset(r, 5, r->lba >> 8);
			fisset(r, 6, r->lba >> 16);
			u = Ataobs;
			if(pr == Pin || pr == Pout)
				u |= Atalba;
			if((d->feat & Dllba) == 0)
				u |= (r->lba >> 24) & 7;
			fisset(r, 7, u);
			fisset(r, 8, r->lba >> 24);
			fisset(r, 9, r->lba >> 32);
			fisset(r, 10, r->lba >> 48);
		}
		fisset(r, 12, r->nsect);
		fisset(r, 13, r->nsect >> 8);
		break;
	}
	fisset(r, 7, Ataobs);
	displaycmd(r, a);
	if(write(d->fd, &r->cmd, Cmdsz) != Cmdsz){
		eprint("fis write error: %r\n");
		return -1;
	}

	werrstr("");
	switch(pr){
	default:
		ok = read(d->fd, "", 0) == 0;
		break;
	case Pin:
		ok = read(d->fd, r->data, r->count) == r->count;
		r->lba += r->nsect;
		break;
	case Pout:
		ok = write(d->fd, r->data, r->count) == r->count;
		r->lba += r->nsect;
		break;
	}
	rv = 0;
	if(ok == 0){
		eprint("xfer error: %.2ux %r\n", a->cc);
		rv = -1;
	}
	switch(n = read(d->fd, &r->reply, Replysz)){
	case Replysz:
		r->haverfis = 1;
		return rv;
	case -1:
		eprint("status fis read error: %r\n");
		return -1;
	default:
		eprint("status fis read error: short read: %d of %d\n", n, Replysz);
		return -1;
	}
}

/*
 * cheezy code; just issue a inquiry.  use scuzz
 * for real work with atapi devices
 */
int
issuepkt(Req *r, Atatab *a, Dev *d)
{
	char *p;
	uchar *u;
	int n, rv;

	r->haverfis = 0;
	r->count = 128;
	r->data = realloc(r->data, r->count);
	if(r->data == nil && r->count > 0)
		sysfatal("realloc: %r");
	r->cmd.sdcmd = 0xff;
	r->cmd.ataproto = a->protocol;
	memset(r->cmd.fis, 0, Fissize);

	u = r->cmd.fis;
	u[0] = 0x12;
	u[4] = 128-1;
	displaycmd(r, a);

	if(write(d->fd, &r->cmd, 6 + 2) != 6 + 2){
		eprint("fis write error: %r\n");
		return -1;
	}
	n = read(d->fd, r->data, r->count);
	rv = 0;
	if(n == -1){
		eprint("xfer error: %.2ux %r\n", a->cc);
		rv = -1;
	}

	print("n is %d (%lld)\n", n, r->count);
	if(n > 32){
		p = (char*)r->data;
		print("%.8s %.16s\n", p + 8, p + 16);
	}

	u = (uchar*)&r->reply;
	n = read(d->fd, u, Replysz);
	if(n < 0){
		eprint("status fis read error (%d): %r\n", n);
		return -1;
	}
	
	if(n < Replysz)
		memset(u + n, 0, Replysz - n);
	r->haverfis = 1;
	return rv;
}

/*
 * silly protocol:
 * 1.  use write log ext 0xe0 to fill out the command
 * 2.  use write log ext 0xe1 to write or data (if any)
 * 3.  use read log ext 0xe0 to nab status.  polled
 */
void
sctreq(Req *r)
{
	memset(r, 0, sizeof *r);
	r->rfd = Dontread;
}

char*
sctrsp(Req *r)
{
	uint i;
	static char buf[32];

	if(!r->haverfis)
		return "no rfis";
	if((r->reply.fis[Frerror] & (Eidnf | Eabrt)) == 0)
		return nil;
	i = r->reply.fis[Fsc] | r->reply.fis[Flba0]<<8;
	if(i == 0xffff)
		return "in progress";
	else if(i == 0){
		snprint(buf, sizeof buf, "unknown %.2ux", r->reply.fis[Frerror]);
		return buf;
	}else if(i < nelem(sctetab))
		return sctetab[i];
	else
		return "<bad>";
}

char*
sctready(Dev *d, int sec)
{
	char *s;
	int i;
	Req r;
	static char e[ERRMAX];

	for(;;){
		if(interrupted){
			s = "interrupted";
			break;
		}
		sctreq(&r);
		fisset(&r, Fsc, 1);
		fisset(&r, Flba0, 0xe0);
		r.count = 512;
		i = issueata(&r, sctread, d);
		free(r.data);
		if(i == -1){
			rerrstr(e, ERRMAX);
			s = e;
			break;
		}
		if((r.cmd.fis[Fsc] | r.cmd.fis[Fsc8]<<8) != 0xffff){
			s = sctrsp(&r);
			break;
		}
		if(sec == 0){
			s = "timeout";
			break;
		}
		sleep(1000);
		sec--;
	}
	return s;
}

typedef struct Sttab Sttab;
struct Sttab {
	int	o;
	int	sz;
	char	*name;
};

Sttab sctt[] = {
	0,	2,	"version",
	2,	2,	"period",
	4,	2,	"intval",
	6,	1,	"max op",
	7,	1,	"max",
	8,	1,	"min op",
	9,	1,	"min",
};

void
sctttab(Req *r)
{
	char c, buf[10];
	int i, n, l, d;
	uchar *u;

	u = r->data;
	for(i = 0; i < nelem(sctt); i++){
		switch(sctt[i].sz){
		case 1:
			c = u[sctt[i].o];
			print("%s\t%d\n", sctt[i].name, c);
			break;
		case 2:
			d = w(u + sctt[i].o);
			print("%s\t%ud\n", sctt[i].name, d);
			break;
		}
	}
	n = w(u + 30);
	l = w(u + 32);
	for(i = 0; i < n; i++){
		c = u[34 + (l + i) % n];
		if((uchar)c == 0x80)
			snprint(buf, sizeof buf, "xx");
		else
			snprint(buf, sizeof buf, "%d", c);
		d = i%10;
		if(d == 0)
			print("\nt%d\t%d", i, c);
		else
			print("% .2d", c);
	}
	if(i%10)
		print("\n");
}

static struct {
	uint	code;
	char	*s;
	char	*ms;
} fxtab[] = {
	0x00010001,	"set features",	0,
	0x00010002,	"enabled",	0,
	0x00010003,	"disabled",	0,

	0x00020001,	"enabled",	0,
	0x00020002,	"disabled",	0,

	0x0003ffff,	"minute",	"minutes",
};

void
sctfcout(ushort *u, Req *r)
{
	uchar *f;
	ushort v;
	uint c, m, i;

	f = r->reply.fis;
	switch(u[1]){
	case 1:
	case 2:
		v = f[Fsc] | f[Flba0]<<8;
		c = u[2]<<16 | v;
		m = u[2]<<16 | 0xffff;
		for(i = 0; i < nelem(fxtab); i++)
			if(fxtab[i].code == c)
				print("%s\n", fxtab[i].s);
			else if(fxtab[i].code == m)
				print("%d %s\n", v, v>1? fxtab[i].ms: fxtab[i].s);
		break;
	case 3:
		v = f[Fsc] | f[Flba0]<<8;
		if(v & 1)
			print("preserve\n");
		else
			print("volatile\n");
		break;
	}
}

void
scterout(ushort *u, Req *r)
{
	uchar *f;
	uint v;

	f = r->reply.fis;
	switch(u[1]){
	case 2:
		v = f[Fsc] | f[Flba0]<<8;
		v *= 100;
		print("%dms\n", v);
	}
}

void
sctout(ushort *u, Req *r)
{
	switch(u[0]){
	case 5:
		sctttab(r);
		break;
	case 4:
		sctfcout(u, r);
		break;
	case 3:
		scterout(u, r);
		break;
	}
}

int
issuesct0(Req *r0, Atatab *a, Dev *d)
{
	char *s;
	uchar proto;
	Atatab *txa;
	Req r;

	if((d->feat & Dsct) == 0){
		eprint("sct not supported\n");
		return -1;
	}

	/* 1. issue command */
	sctreq(&r);
	r.data = malloc(r0->count);
	memcpy(r.data, r0->data, r0->count);
	r.count = r0->count;
	fisset(&r, Fsc, 1);
	fisset(&r, Flba0, 0xe0);
	if(issueata(&r, sctissue, d) == -1)
		return -1;
	if(s = sctrsp(&r)){
		eprint("sct error: %s\n", s);
		return -1;
	}

	/* 1a. check response */
	if((s = sctready(d, 1)) != nil){
		eprint("sct cmd: %s\n", s);
		return -1;
	}
	/* 2. transfer data */

	proto = a->protocol;
	if(r0->fisbits & 1 << 16){
		proto &= ~Pdatam;
		proto |= r0->cmd.ataproto;
	}
	switch(proto & Pdatam){
	default:
		txa = nil;
		break;
	case Pin:
		txa = sctread;
		break;
/*	case Pout:
		txa = sctout;
		break;
*/
	}

	if(txa != nil){
		sctreq(&r);
		r.count = 512;
		fisset(&r, Fsc, 1);
		fisset(&r, Flba0, 0xe1);
		if(issueata(&r, txa, d) == -1)
			return -1;

		/* 2a. check response */
		if((s = sctready(d, 1)) != nil){
			eprint("sct cmd: %s\n", s);
			return -1;
		}
	}

	sctout((ushort*)r0->data, &r);
	free(r.data);
	return 0;
}

static void*
pushtrace(int i)
{
	void *tr0;

	tr0 = malloc(sizeof issuetr);
	if(tr0 == 0)
		return 0;
	memcpy(tr0, issuetr, sizeof issuetr);
	memset(issuetr, i, sizeof issuetr);
	return tr0;
}

static void
poptrace(void *tr0)
{
	if(tr0 == nil)
		return;
	memcpy(issuetr, tr0, sizeof issuetr);
	free(tr0);
}

int
issuesct(Req *r0, Atatab *a, Dev *d)
{
	int r;
	void *t;

	t = nil;
	if(scttrace)
		t = pushtrace(1);
	r = issuesct0(r0, a, d);
	if(scttrace)
		poptrace(t);
	return r;
}

int
issue(Req *r, Atatab *a, Dev *d)
{
	int rv;
	int (*f)(Req*, Atatab*, Dev*);

	if(a->protocol & Psct)
		f = issuesct;
	else if((a->protocol & Pprotom) == Ppkt)
		f = issuepkt;
	else
		f = issueata;
	rv = f(r, a, d);
	if(r->haverfis)
	if(r->reply.fis[Fstatus] & ASerr){
		werrstr("ata error");
		rv = -1;
	}
	return rv;
}

void
sigfmt(Req *r)
{
	print("%.8ux\n", fistosig(r->reply.fis));
}

int
opendev(char *dev, Dev *d)
{
	char buf[128];
	int rv;
	ushort *u;
	Req r;

	if(d->fd != -1)
		close(d->fd);
	memset(d, 0, sizeof *d);
	snprint(buf, sizeof buf, "%s/raw", dev);
	d->fd = open(buf, ORDWR);
	if(d->fd == -1)
		return -1;
	memset(&r, 0, sizeof r);
	if(issue(&r, sigcmd, d) == -1){
lose:
		close(d->fd);
		return -1;
	}
	setfissig(d, fistosig(r.reply.fis));
	memset(&r, 0, sizeof r);
	r.count = 512;
	r.nsect = 1;
	if(d->sig>>16 == 0xeb14)
		rv = issue(&r, idpktcmd, d);
	else
		rv = issue(&r, idcmd, d);
	if(rv == -1)
		goto lose;
	u = (ushort*)r.data;
	d->nsect = idfeat(d, u);
	d->secsize = idss(d, u);
	d->wwn = idwwn(d, u);
	return 0;
}

void
rawout(Req *r)
{
	int n;

	n = write(r->wfd, r->data, r->count);
	if(n != r->count)
		eprint("!short write %ud wanted %lld\n", n, r->count);
}

static ushort
gbit16(void *a)
{
	ushort j;
	uchar *i;

	i = a;
	j  = i[1] << 8;
	j |= i[0];
	return j;
}

static Btab extra[] = {
	12,	"ncqpri",
	11,	"ncqunload",
	10,	"phyevent",
	9,	"hpwrctl",
	3,	"6.0gbit",
	2,	"3.0gbit",
	1,	"1.5gbit",
};

static Btab suptab[] = {
	8,	"wwn",
	5,	"mediaserial",
	1,	"smartst",
	0,	"smartlog"
};

char*
pextraid(char *p, char *e, ushort *id, uint *medserial)
{
	char *p0;
	ushort u;

	*p = 0;
	*medserial = 0;
	p0 = p;
	p = sebtab(p, e, extra, nelem(extra), gbit16(id + 76));
	if(p != p0)
		p = seprint(p, e, " ");
	u = gbit16(id + 83);
	if(u & 1<<5)
		p = seprint(p, e, "gpl ");
	p0 = p;
	p = sebtab(p, e, suptab, nelem(suptab), gbit16(id + 84));
	if(p != p0)
		p = seprint(p, e, " ");
	u = gbit16(id + 120);
	if(u & 1<<2)
		p = seprint(p, e, "wunc ");
	return p;
}

static char *patatab[] = {
	"ata8-apt",
	"ata/atapi-7",
};

static char *satatab[] = {
	"ata8-ast",
	"sata1.0a",
	"sataiiext",
	"sata2.5",
	"sata2.6",
	"sata3.0",
};

char*
ptransport(char *p, char *e, ushort *id)
{
	char *s;
	ushort u, i;

	u = gbit16(id + 222);
	if(u == 0 || u == 0xffff)
		return seprint(p, e, "unreported ");
	i = (u>>5) & 0x7f;
	switch(u & 7<<12){
	default:
		s = "unktransport";
		break;
	case 0:
		s = "unkparallel";
		if(i < nelem(patatab))
			s = patatab[i];
		break;
	case 1<<12:
		s = "unkserial";
		if(i < nelem(satatab))
			s = satatab[i];
		break;
	}
	return seprint(p, e, "%s ", s);
}

Btab entab[] = {
	10,	"hpa",
	9,	"reset",
	8,	"service",
	7,	"release",
	6,	"rdlookahd",
	5,	"vwc",
	4,	"packet",
	3,	"pm",
	2,	"security",
	1,	"smart",
};

Btab addlen[] = {
	15,	"cfast",
//	14,	"trim",	/* check 169 */
	13,	"lpsalignerr",
	12,	"iddma",
	11,	"rbufdma",
	10,	"wbufdma",
	9,	"pwddma",
	8,	"dlmcdma",
};

char*
penabled(char *p, char *e, ushort *id)
{
	char *p0;
	ushort u;

	p0 = p;
	p = sebtab(p, e, addlen, nelem(addlen), gbit16(id + 69));
	u = gbit16(id + 87);
	if(u>>14 == 1){
		if(p != p0)
			p = seprint(p, e, " ");
		p = sebtab(p, e, entab, nelem(entab), gbit16(id + 85));
	}
	return p;
}

static char *fftab[] = {
	nil,
	"5¼",
	"3½",
	"2½",
	"1.8",
	"<1.8",
};

char*
pff(char *p, char *e, ushort *id)
{
	char *p0;
	ushort u;

	p0 = p;
	u = gbit16(id + 168);
	if(u < nelem(fftab) && fftab[u] != nil)
		p = seprint(p, e, "%s ", fftab[u]);
	u = gbit16(id + 217);
	if(u == 1)
		p = seprint(p, e, "solid-state ");
	else if(u != 0 && u != 0xfffe)
		p = seprint(p, e, "%udrpm ", u);
	if(p != p0)
		p--;
	*p = 0;
	return p;
}

Btab scttab[] = {
	5,	"tables",
	4,	"feactl",
	3,	"errctl",
	2,	"wsame",
	1,	"rwlong",
	0,	"sct",
};

char*
psct(char *p, char *e, ushort *id)
{
	return sebtab(p, e, scttab, nelem(scttab), gbit16(id + 206));
}

void
idfmt(Req *r)
{
	char buf[100];
	uint ss, i;
	ushort *id;
	uvlong nsect;
	Sfis f;

	if(r->fmtrw == 0){
		rawout(r);
		return;
	}
	id = (ushort*)r->data;
	nsect = idfeat(&f, id);
	ss = idss(&f, id);

	idmove(buf, id+10, 20);
	print("serial\t%s\n", buf);
	idmove(buf, id+23, 8);
	print("firm\t%s\n", buf);
	idmove(buf, id+27, 40);
	print("model\t%s\n", buf);
	print("wwn\t%ullx\n", idwwn(&f, id));
	pflag(buf, buf + sizeof buf, &f);
	print("flags\t%s", buf);
	print("geometry %llud %ud", nsect, ss);
	if(f.c | f.h | f.s)
		print(" %ud %ud %ud", f.c, f.h, f.s);
	print("\n");
	penabled(buf, buf + sizeof buf, id);
	print("enabled\t%s\n", buf);
	pextraid(buf, buf + sizeof buf, id, &i);
	print("extra\t%s\n", buf);
	if(i){
		idmove(buf, id + 176, 60);
		if(buf[0] != 0)
			print("medias\t%s\n", buf);
	}
	psct(buf, buf + sizeof buf, id);
	if(buf[0])
		print("sct\t%s\n", buf);
	ptransport(buf, buf + sizeof buf, id);
	print("trans\t%s\n", buf);
	pff(buf, buf + sizeof buf, id);
	if(buf[0])
		print("ff\t%s\n", buf);
}

void
smfmt(Req *r)
{
	uchar *fis;

	if(r->cmd.fis[Ffeat] == 0xda){
		fis = r->reply.fis;
		if(fis[5] == 0x4f &&
		   fis[6] == 0xc2)
			eprint("normal\n");
		else
			eprint("threshold exceeded\n");
		return;
	}
}

void
iofmt(Req *r)
{
	uchar *u;
	int i;

	if(r->fmtrw == 0){
		rawout(r);
		return;
	}
	u = r->data;
	for(i = 0; i < r->count; i += 16)
		fprint(2, "%.2ux%.2ux%.2ux%.2ux%.2ux%.2ux%.2ux%.2ux"
		"%.2ux%.2ux%.2ux%.2ux%.2ux%.2ux%.2ux%.2ux\n",
		u[i + 0], u[i + 1], u[i + 2], u[i + 3], u[i + 4], u[i + 5], u[i + 6], u[i + 7],
		u[i + 8], u[i + 9], u[i +10], u[i +11], u[i +12], u[i +13], u[i +14], u[i +15]);
}

static char *csbyte[] = {
	"never started",
	nil,
	"competed without error",
	"in progress",
	"suspended by cmd from host",
	"aborted by cmd from host",
	"aborted by device with fatal error",
};

static char *exe[] = {
	"no error or never run",
	"aborted by host",
	"interrupted by host",
	"fatal error; unable to complete",
	"failed",
	"failed: electricial",
	"failed: servo",
	"failed: read",
	"failed: shipping damage",
[0xf]	"in progress",
};

char*
tabtr(uint u, char **tab, int ntab)
{
	char *s;

	if(u >= ntab || (s = tab[u]) == nil)
		s = "reserved";
	return s;
}

void
sdfmt(Req *r)
{
	char *s;
	uchar *b;
	ushort u;

	if(r->fmtrw == 0){
		rawout(r);
		return;
	}
	b = r->data;
	u = b[362];
	if((u & 0xf0) == 0x80 && u != 0x81 && u != 0x83)
		u &= 0xf;
	s = tabtr(u, csbyte, nelem(csbyte));
	print("col status: %.2ux %s\n", b[362], s);
	u = b[363];
	s = tabtr(u>>4, exe, nelem(exe));
	if(u & 0xf)
		print("exe status: %.2ux %s, %d0%% left\n", u, s, u & 0xf);
	else
		print("exe status: %.2ux %s\n", u, s);
	u = b[364] | b[365]<<8;
	print("time left: %uds\n", u);
	print("shrt poll: %udm\n", b[373]);
	u = b[374];
	if(u == 0xff)
		u = b[375] | b[376]<<8;
	print("ext poll: %udm\n", u);
}

void
pagemapfmt(Req *r)
{
	int i;
	ushort *u;

	u = (ushort*)r->data;
	if(u[0] != 1){
		print("unsupported\n");
		return;
	}
	for(i = 1; i < 128; i++)
		if(u[i] > 0)
			print("page %d: %d\n", i, u[i]);
}

void
slfmt(Req *r)
{
	switch(r->cmd.fis[Flba0]){
	default:
		iofmt(r);
		break;
	case 0:
		pagemapfmt(r);
		break;
	}
}

enum{
	Physz	= 7<<12,
};

static char *phyec[] = {
	"no event",
	"icrc",
	"err data",
	"err d2h data",
	"err h2d data",
[0x05]	"err nd",
	"err d2h nd",
	"err h2d nd",
	"retry d2h nd",
	"nready",
[0x0a]	"comreset",
	"h2d crc",
	nil,
	"bad h2d",
	nil,
	"err h2d data crc",
[0x10]	"err h2d data",
	nil,
	"err h2d nd crc",
	"err h2d nd",
};

void
phyfmt(Req *r)
{
	char *ec;
	uchar *p;
	ushort *u, *e, id, sz;

	u = (ushort*)r->data;
	e = u + 510/sizeof *u;
	for(u += 2; u < e; u += sz){
		id = w((uchar*)u);
		sz = (id & Physz) >> 12;
		id &= ~Physz;
		if(sz == 0)
			break;
		ec = "unk";
		if(id < nelem(phyec) && phyec[id] != nil)
			ec = phyec[id];
		print("%.4ux\t%-15s\t", id, ec);
		p = (uchar*)u + 2;
		switch(sz<<1){
		default:
			print("\n");
			break;
		case 2:
			print("%.4ux\n", w(p));
			break;
		case 4:
			print("%.8ux\n", dw(p));
			break;
		case 8:
			print("%.16llux\n", qw(p));
			break;
		}
		sz += 1;
	}
}

typedef struct Gltab Gltab;
struct Gltab{
	int	offset;
	char	*name;
};

Gltab page3[] = {
	8,	"power-on hrs",
	16,	"head flying hrs",
	24,	"head loads",
	32,	"realloc'd sec",
	40,	"read recovery att",
	48,	"start failures"
};
	
void
qpfmt(Req *r, Gltab *t, int ntab)
{
	uchar *u;
	int i;
	uvlong v;

	u = r->data;
	for(i = 0; i < ntab; i++){
		v = qw(u + t[i].offset);
		if((v & 3ll<<63) != 3ll<<63)
			continue;
		print("%lud\t%s\n", (ulong)v, t[i].name);
	}
}

static char *sctsttab[] = {
	"active waiting",
	"standby",
	"sleep",
	"dst bgnd",
	"smart bgnd",
	"sct bgnd",
};

void
sctstatfmt(Req *r)
{
	char *s;
	uchar *id, c;

	id = r->data;
	print("version\t%d\n", gbit16(id + 0));
	print("vnd ver\t%2ux\n", gbit16(id + 2));
	print("flags\t%.8ux\n", dw(id + 6));
	c = id[10];
	s = "unk";
	if(c < nelem(sctsttab))
		s = sctsttab[c];
	print("state\t%s\n", s);
	print("ext stat\t%.4ux\n", gbit16(id + 14));
	print("act code\t%.4ux\n", gbit16(id + 16));
	print("fn code\t%.4ux\n", gbit16(id + 18));
	print("lba\t%llud\n", qw(id + 40));
	print("temp\t%d\n", id[200]);
	print("min t\t%d %d\n", id[201], id[203]);
	print("max t\t%d %d\n", id[202], id[204]);
	print("ot\t%d\n", dw(id + 206));
	print("ut\t%d\n", dw(id + 210));
}


void
glfmt(Req *r)
{
	switch(r->cmd.fis[Flba0]){
	case 0:
		pagemapfmt(r);
		break;
	case 3:
		qpfmt(r, page3, nelem(page3));
		break;
	case 17:
		phyfmt(r);
		break;
	case 0xe0:
		sctstatfmt(r);
		break;
	default:
		iofmt(r);
		break;
	}
}

char*
readline(char *prompt, char *line, int len)
{
	char *p, *e, *q;
	int n, dump;

	e = line + len;
retry:
	dump = 0;
	if(interrupted)
		eprint("\n%s", prompt);
	else
		eprint("%s", prompt);
	interrupted = 0;
	for(p = line;; p += n){
		if(p == e){
			dump = 1;
			p = line;
		}
		n = read(0, p, e - p);
		if(n < 0){
			if(interrupted)
				goto retry;
			return nil;
		}
		if(n == 0)
			return nil;
		if(q = memchr(p, '\n', n)){
			if(dump){
				eprint("!line too long\n");
				goto retry;
			}
			p = q;
			break;
		}
	}
	*p = 0;
	return line;
}

void
suggesttab(char *cmd, Atatab *a, int n)
{
	int i, l;

	l = strlen(cmd);
	for(i = 0; i < n; i++)
		if(cistrncmp(cmd, a[i].name, l) == 0)
			eprint("%s\n", a[i].name);
}

Atatab*
findtab(char **cmd, Atatab *a, int n)
{
	char *p, *c;
	int i, cc, max, l;

	cc = strtoul(*cmd, &p, 0);
	if(p != *cmd && (*p == 0 || *p == ' ')){
		for(i = 0; i < n; i++)
			if(a[i].cc == cc){
				*cmd = p + 1;
				return a + cc;
			}
		return 0;
	}
	max = 0;
	cc = 0;
	c = *cmd;
	for(i = 0; i < n; i++){
		l = strlen(a[i].name);
		if(l > max && cistrncmp(*cmd, a[i].name, l) == 0)
		if(c[l] == ' ' || c[l] == 0){
			max = l + (c[l] == ' ');
			cc = i;
		}
	}
	if(max > 0){
		*cmd = *cmd + max;
		return a + cc;
	}
	return 0;
}
		
int
catch(void*, char *note)
{
	if(strstr(note, "interrupt") != nil)
		return interrupted = 1;
	return 0;
}

char**
ndargs(Atatab*, Req *, char **p)
{
	return p;
}

char**
ioargs(Atatab *, Req *r, char **p)
{
	if(r->nsect == 0)
		r->nsect = 1;
	if(p[0] == 0)
		return p;
	r->lba = strtoull(p[0], 0, 0);
	p++;
	if(p[0] == 0)
		return p;
	r->nsect = strtoul(p[0], 0, 0);
	return p + 1;
}

char**
stdargs(Atatab *, Req *r, char **p)
{
	char *s;
	Rune x;

	for(; p[0] && p[0][0] == '-' && p[0][1]; p++){
		s = p[0] + 1;
		if(*s == '-'){
			p++;
			break;
		}
		while(*s && (s += chartorune(&x, s)))
		switch(x){
		case 'r':
			r->raw = 1;
			break;
		default:
			return p;
		}
	}
	return p;
}

static void
chopoff(char *s, char *extra)
{
	char *p;
	int l, ls;

	l = strlen(extra);
	ls = strlen(s);
	if(l >= ls)
		return;
	p = s + ls - l;
	if(strcmp(p, extra) == 0)
		*p = 0;
}

char*
trim(char *s)
{
	char *p;

	while(*s && (*s == ' ' || *s == '\t'))
		s++;
	if(*s == 0)
		return nil;
	p = s + strlen(s) - 1;
	while(*p == ' ' || *p == '\t')
		p--;
	p[1] = 0;
	return s;
}

int
doredir(Req *r, char **f, int nf, int mode, int *fd1, int *fd2)
{
	int fd;

	if(nf != 1 && nf != 2){
		eprint("!args\n");
		return -1;
	}
	fd = -1;
	if(nf == 2){
		fd = open(f[1], mode);
		if(mode != OREAD){
			if(fd == -1)
				fd = create(f[1], mode, 0660);
			else
				seek(fd, 0, 2);
		}
	}
	if(fd1){
		close(*fd1);
		*fd1 = fd;
	}
	if(fd2){
		r->fmtrw = fd == -1;
		close(*fd2);
		*fd2 = fd;
	}
	return fd;
}

int
special(char *s, Dev *d, Req *r)
{
	char buf[512], path[128], *f[20], sbuf[512], s2[512], *p, *e, *t;
	uchar *u;
	int i, j, nf;
	Atatab *a;

	p = buf;
	e = buf + sizeof buf;
	if(!strcmp(s, "close")){
		r->haverfis = 0;
		close(d->fd);
		d->fd = -1;
		return 0;
	}
	if(!strcmp(s, "scttrace")){
		scttrace = 1;
		return 0;
	}
	if(!strcmp(s, "dev")){
		if(d->fd == -1){
			eprint("!bad cmd (device closed)\n");
			return 0;
		}
		if(fd2path(d->fd, path, sizeof path) == -1)
			sysfatal("fd2path: %r");
		chopoff(path, "/raw");
		p = seprint(p, e, "dev\t%s\n", path);
		p = seprint(p, e, "flags\t");
		p = pflag(p, e, d);
		p = seprint(p, e, "lsectsz\t" "%ud ptol %ud\n", d->lsectsz, 1<<d->physshift);
		p = seprint(p, e, "geometry %llud %ud\n", d->nsect, d->secsize);
		if(d->c | d->h | d->s)
			seprint(p, e, "chs\t%d %d %d\n", d->c, d->h, d->s);
		print("%s", buf);
		return 0;
	}
	if(!strcmp(s, "help")){
		suggesttab(buf, atatab, nelem(atatab));
		return 0;
	}
	if(!strcmp(s, "probe")){
		probe();
		return 0;
	}
	if(!strcmp(s, "rfis")){
		if(r->haverfis == 0){
			eprint("!no rfis\n");
			return 0;
		}
		p = seprint(p, e, "%.2x\n", r->reply.sdcmd);
		u = r->reply.fis;
		for(i = 0; i < 16; i++)
			p = seprint(p, e, "%.2ux", u[i]);
		seprint(p, e, "\n");
		print("%s", buf);
		return 0;
	}
	for(t = s; *t == '<' || *t == '>'; t++)
		;
	if(t != s)
		snprint(sbuf, sizeof buf, "%.*s %s", (int)(t - s), s, t);
	else
		snprint(sbuf, sizeof sbuf, "%s", s);
	nf = tokenize(sbuf, f, nelem(f));
	if(!strcmp(f[0], "issuetr")){
		if(nf == 1)
			for(i = 0; i < nelem(issuetr); i++)
				issuetr[i] ^= 1;
		else{
			p = s2;
			e = s2 + sizeof s2;
			for(i = 1; i < nf - 1; i++)
				p = seprint(p, e, "%s ", f[i]);
			p = seprint(p, e, "%s", f[i]);
			e = s2;
			for(i = 1; i < nf; i++){
				j = strtoul(f[i], &p, 0);
				if(*p == 0 && j < nelem(issuetr))
					issuetr[i] ^= 1;
				else if(a = findtab(&e, atatab, nelem(atatab)))
					issuetr[a->cc & 0xff] ^= 1;
			}
		}
		return 0;
	}
	if(!strcmp(f[0], "open")){
		r->lba = 0;
		if(nf == 2)
			opendev(f[1], d);
		else
			eprint("!bad args to open\n");
		return 0;
	}
	if(!strcmp(f[0], ">")){
		doredir(r, f, nf, OWRITE, 0, &r->wfd);
		return 0;
	}
	if(!strcmp(f[0], "<")){
		doredir(r, f, nf, OREAD, &r->rfd, 0);
		return 0;
	}
	if(!strcmp(f[0], "<>")){
		doredir(r, f, nf, OWRITE, &r->rfd, &r->wfd);
		return 0;
	}
	return -1;
}

void
setreg(Req *r, uint reg, uvlong v)
{
	uchar *o;
	int x;

	switch(reg & (Sbase | Pbase)){
	case 0:
		r->fisbits |= 1 << reg;
		r->cmd.fis[reg] = v;
		break;
	case Sbase:
	case Sbase | Pbase:
		x = reg & ~(Sbase | Ssz);
		o = r->data + x*2;
		assert(x < r->count);
		switch(reg & Ssz){
		default:
			print("reg & Ssz %ux\n", reg & Ssz);
			_assert("bad table");
		case Sw:
			pw(o, v);
			break;
		case Sdw:
			pdw(o, v);
			break;
		case Sqw:
			pqw(o, v);
			break;
		}
		break;
	case Pbase:
		/* fix me please: this is teh suck */
		r->fisbits |= 1 << 16;
		r->cmd.ataproto = v;
		break;
	}
}

int
setfis0(Req *r, Txtab *t, char *p)
{
	char *e;
	uvlong v;

	v = strtoull(p, &e, 0);
	setreg(r, t->val, v);
	return *e != 0;
}

char**
setfis(Atatab*, Req *r, char **p)
{
	char *s;
	int i;

loop:
	if((s = p[0]) == 0)
		return p;
	for(i = 0; i < nelem(regtx); i++)
		if(strcmp(s, regtx[i].name) == 0 && p[1] != nil){
//			print("setfis0 %s %s\n", p[0], p[1]);
			setfis0(r, regtx + i, p[1]);
			p += 2;
			goto loop;
		}
	return p;
}

char*
rname(char *buf, int n, int r)
{
	int i;

	for(i = 0; i < nelem(regtx); i++)
		if(regtx[i].val == r){
			snprint(buf, n, "%s", regtx[i].name);
			return buf;
		}
	snprint(buf, n, "%.2ux", r);
	return buf;
}

int
mwcmp(char *a, char ***l)
{
	char buf[128], *f[20], **p;
	int nf, i;

	if(*a == 0)
		return 0;
	p = *l;
	if(p[0] == 0)
		return -1;
	snprint(buf, sizeof buf, "%s", a);
	nf = tokenize(buf, f, nelem(f));
	for(i = 0; i < nf; i++)
		if(p[i] == nil || cistrcmp(p[i], f[i]) != 0)
			return -1;
	*l = p + i - 1;
	return 0;
}

char **dofetab(Fetab*, Req*, char**);

static char hexdig[] = "ABCDEFabcdef0123456789";
static char hexonly[] = "ABCDEFabcdef";
static char Enum[] = "expecting number";

int
fenum(Fetab *, int v, char ***p)
{
	char *e, *s, *r;
	int base;

	if(v >= 0)
		return v;
	s = *(*p + 1);
	e = nil;
	if(s == nil || *s == 0)
		e = Enum;
	else{
		base = 0;
		if(strspn(s, hexdig) == strlen(s) &&
		strpbrk(s, hexonly) != nil)
			base = 0x10;
		v = strtoul(s, &r, base);
		if(*r)
			e = Enum;
	}
	if(e == nil)
		(*p)++;
	else
		print("error: %s [%s]\n", e, s);
	return v;
}

char**
dofetab0(Fetab *t, Req *r, char **p)
{
	int i, v;
	Txtab *tab;

	if(t == nil)
		return p;
	tab = t->tab;
loop:
	for(i = 0; i < t->ntab; i++)
		if(mwcmp(tab[i].name, &p) == 0){
			v = fenum(t, tab[i].val, &p);
			setreg(r, t->reg, v);
			if(tab[i].name[0] != 0){
				p = dofetab(tab[i].fe, r, p + 1);
				goto loop;
			}
		}
	return p;

}

char**
dofetab(Fetab *t, Req *r, char **p)
{
	for(; t != nil && t->ntab > 0; t++)
		p = dofetab0(t, r, p);
	return p;
}

char**
dotab(Atatab *a, Req *r, char **p)
{
	if(a->tab == nil)
		return p;
	return dofetab(a->tab, r, p);
}

void
initreq(Req *r)
{
	memset(r, 0, sizeof *r);
//	r->wfd = open("/dev/null", OWRITE);
	r->wfd = dup(1, -1);
	if(rflag == 0)
		r->fmtrw = 1;
	r->rfd = open("/dev/zero", OREAD);
}

void
setup(void)
{
	int i;

	for(i = 0; i < nelem(atatab); i++)
		if(atatab[i].cc == 0x2f){
			sctread = atatab + i;
			break;
		}
	for(; i < nelem(atatab); i++)
		if(atatab[i].cc == 0x3f){
			sctissue = atatab + i;
			break;
		}
	for(; i < nelem(atatab); i++)
		if(atatab[i].cc == 0xa1){
			idpktcmd = atatab + i;
			break;
		}
	for(; i < nelem(atatab); i++)
		if(atatab[i].cc == 0xec){
			idcmd = atatab + i;
			break;
		}
	for(; i < nelem(atatab); i++)
		if(atatab[i].cc == 0xf000){
			sigcmd = atatab + i;
			break;
		}
}

typedef struct Htab Htab;
struct Htab {
	ulong	bit;
	char	*name;
};

Htab ertab[] = {
	Eicrc,	"icrc",
	Ewp,	"wp",
	Emc,	"mc",
	Eidnf,	"idnf",
	Emcr,	"mcr",
	Eabrt,	"abrt",
	Enm,	"nm",
	Emed,	"med",
	Eunc,	"unc",
};

Htab sttab[] = {
	ASbsy,	"bsy",
	ASdrdy,	"drdy",
	ASdf,	"df",
	ASdrq,	"drq",
	ASerr,	"err",
};

static char*
htabfmt(char *p, char *e, Htab *t, int n, ulong u)
{
	char *p0;
	uint i;

	p0 = p;
	for(i = 0; i < n; i++)
		if(u & t[i].bit)
			p = seprint(p, e, "%s | ", t[i].name);
	if(p - 3 >= p0)
		p -= 3;
	if(p < e)
		p[0] = 0;
	return p;
}

void
prerror(Req *r)
{
	char st[64], er[64];
	uchar *u;

	u = r->reply.fis;
	if(r->haverfis == 0 ||  (u[Fstatus] & ASerr) == 0)
		return;
	htabfmt(er, er + sizeof er, ertab, nelem(ertab), u[Frerror]);
	htabfmt(st, st + sizeof st, sttab, nelem(sttab), u[Fstatus] & ~ASobs);
	fprint(2, "err %.2ux %.2ux (%s, %s)\n", u[Frerror], u[Fstatus], er, st);
}

void
usage(void)
{
	eprint("usage: atazz dev\n");
	eprint(" or -c cmd\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char buf[1024], *p, *f[20], **fp;
	int nf, cflag, i;
	Atatab *a;
	Req r;
	Dev d;

	cflag = 0;
	ARGBEGIN{
	case 'c':
		cflag = atoi(EARGF(usage()));
		break;
	case 'r':
		rflag = 1;
		break;
	default:
		usage();
	}ARGEND

	if(cflag){
		for(i = 0; i < nelem(atatab); i++)
			if(atatab[i].cc == cflag)
				print("%s\n", atatab[i].name);
		exits("");
	}

	setup();
	fmtinstall(L'π', πfmt);
	if(argc > 1)
		usage();
	initreq(&r);
	d.fd = -1;
	if(argc == 1 && opendev(*argv, &d) == -1)
		sysfatal("opendev: %r");
	atnotify(catch, 1);
	for(;;){
		memset(&r.cmd, 0, sizeof r.cmd);
		r.fisbits = 0;
		if(readline("az> ", buf, sizeof buf-1) == nil)
			break;
		if((p = trim(buf)) == nil)
			continue;
		if(special(buf, &d, &r) == 0)
			continue;
		if(d.fd == -1){
			eprint("!bad cmd (device closed)\n");
			continue;
		}
		a = findtab(&p, atatab, nelem(atatab));
		if(!a){
			suggesttab(buf, atatab, nelem(atatab));
			eprint("!unknown cmd\n");
			continue;
		}
		nf = tokenize(p, f, nelem(f) - 1);
		f[nf] = 0;
		fp = stdargs(a, &r, f);
		fp = setfis(a, &r, fp);
		if(a->protocol & Psct){
			r.count = 1 * 512;
			r.data = realloc(r.data, r.count);
			memset(r.data, 0, r.count);
		}
		fp = dotab(a, &r, fp);
		switch(a->protocol & Pprotom){
		default:
			eprint("!bad proto1 %.2ux\n", a->protocol & Pprotom);
			continue;
		case Pnd:
			fp = ndargs(a, &r, fp);
		case Preset:
		case Pdiag:
			r.count = 0;
			r.lba = 0;
			r.nsect = 0;
			break;
		case Ppio:
		case Pdma:
		case Pdmq:
		case Ppkt:
			if(a->flags & Cmd5sc){
				r.nsect = r.cmd.fis[Fsc];
				if(r.nsect == 0)
					r.nsect = 1;
				r.cmd.fis[Fsc] = r.nsect;
				r.count = r.nsect * 0x200;
			}else if((a->protocol & Pssm) == P512){
				r.lba = 0;
				r.nsect = 0;
				r.count = 512;
			}else{
				fp = ioargs(a, &r, fp);
				r.count = d.secsize * r.nsect;
			}
			break;
		}
		if(fp[0]){
			eprint("!extra args %π\n", fp);
			continue;
		}
		if(issue(&r, a, &d) == -1){
			prerror(&r);
			continue;
		}
		if(a->fmt)
			a->fmt(&r);
	}
	exits("");
}
