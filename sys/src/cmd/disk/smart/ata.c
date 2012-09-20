#include <u.h>
#include <libc.h>
#include <fis.h>
#include "smart.h"

enum{
	Nop,
	Idall,
	Idpkt,
	Smart,
	Id,
	Sig,

	Cmdsz	= 18,
	Replysz	= 18,

};

typedef struct Atatab Atatab;
struct Atatab {
	ushort	cc;
	uchar	protocol;
	char	*name;
};

Atatab atatab[] = {
[Nop]	0x00,	Pnd|P28,	"nop",
[Idall]	0xff,	Pin|Ppio|P28,	"identify * device",
[Idpkt]	0xa1,	Pin|Ppio|P28,	"identify packet device",
[Smart]	0xb0,	Pnd|P28,	"smart",
[Id]	0xec,	Pin|Ppio|P28,	"identify device",
[Sig]	0xf000,	Pnd|P28,	"signature",
};

typedef struct Rcmd Rcmd;
struct Rcmd{
	uchar	sdcmd;		/* sd command; 0xff means ata passthrough */
	uchar	ataproto;	/* ata protocol.  non-data, pio, reset, dd, etc. */
	uchar	fis[Fissize];
};

typedef struct Req Req;
struct Req {
	char	haverfis;
	Rcmd	cmd;
	Rcmd	reply;
	uchar	data[0x200];
	uint	count;
};

static int
issueata(Req *r, Sdisk *d, int errok)
{
	char buf[ERRMAX];
	int ok, rv;

	if((rv = write(d->fd, &r->cmd, Cmdsz)) != Cmdsz){
		/* handle non-atazz compatable kernels */
		rerrstr(buf, sizeof buf);
		if(rv != -1 || strstr(buf, "bad arg in system call") != 0)
			eprint(d, "fis write error: %r\n");
		return -1;
	}

	werrstr("");
	switch(r->cmd.ataproto & Pdatam){
	default:
		ok = read(d->fd, "", 0) == 0;
		break;
	case Pin:
		ok = read(d->fd, r->data, r->count) == r->count;
		break;
	case Pout:
		ok = write(d->fd, r->data, r->count) == r->count;
		break;
	}
	rv = 0;
	if(ok == 0){
		rerrstr(buf, sizeof buf);
		if(!errok && strstr(buf, "not sata") == 0)
			eprint(d, "xfer error: %.2ux%.2ux: %r\n", r->cmd.fis[0], r->cmd.fis[2]);
		rv = -1;
	}
	if(read(d->fd, &r->reply, Replysz) != Replysz){
		if(!errok)
			eprint(d, "status fis read error: %r\n");
		return -1;
	}
	r->haverfis = 1;
	return rv;
}

int
issueatat(Req *r, int i, Sdisk *d, int e)
{
	uchar *fis;
	Atatab *a;

	a = atatab + i;
	r->haverfis = 0;
	r->cmd.sdcmd = 0xff;
	r->cmd.ataproto = a->protocol;
	fis = r->cmd.fis;
	fis[0] = H2dev;
	if(a->cc & 0xff00)
		fis[0] = a->cc >> 8;
	fis[1] = Fiscmd;
	if(a->cc != 0xff)
		fis[2] = a->cc;
	return issueata(r, d, e);
}

int
ataprobe(Sdisk *d)
{
	int rv;
	Req r;

	memset(&r, 0, sizeof r);
	if(issueatat(&r, Sig, d, 1) == -1)
		return -1;
	setfissig(d, fistosig(r.reply.fis));
	memset(&r, 0, sizeof r);
	r.count = 0x200;
	identifyfis(d, r.cmd.fis);
	if((rv = issueatat(&r, Idall, d, 1)) != -1){
		idfeat(d, (ushort*)r.data);
		if((d->feat & Dsmart) == 0)
			rv = -1;
	}
	return rv;
}

int
smartfis(Sfis *f, uchar *c, int n)
{
	if((f->feat & Dsmart) == 0)
		return -1;
	skelfis(c);
	c[2] = 0xb0;
	c[3] = 0xd8 + n;	/* able smart */
	c[5] = 0x4f;
	c[6] = 0xc2;
	return 0;
}

int
ataenable(Sdisk *d)
{
	int rv;
	Req r;

	memset(&r, 0, sizeof r);
	smartfis(d, r.cmd.fis, 0);
	rv = issueatat(&r, Smart, d, 0);
	return rv;
}

void
smartrsfis(Sfis*, uchar *c)
{
	skelfis(c);
	c[2] = 0xb0;
	c[3] = 0xda;		/* return smart status */
	c[5] = 0x4f;
	c[6] = 0xc2;
}

int
atastatus(Sdisk *d, char *s, int l)
{
	uchar *fis;
	int rv;
	Req r;

	memset(&r, 0, sizeof r);
	smartrsfis(d, r.cmd.fis);
	rv = issueatat(&r, Smart, d, 0);
	*s = 0;
	if(rv != -1){
		fis = r.reply.fis;
		if(fis[5] == 0x4f &&
		   fis[6] == 0xc2)
			snprint(s, l, "normal");
		else{
			snprint(s, l, "threshold exceeded");
			rv = -1;
		}
	} else
		snprint(s, l, "smart error");
	return rv;
}
