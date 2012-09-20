#include <u.h>
#include <libc.h>
#include <disk.h>
#include <fis.h>
#include </sys/src/cmd/scuzz/scsireq.h>
#include "smart.h"

enum{
	Replysz	= 16,
};

typedef struct Rcmd Rcmd;
struct Rcmd{
	uchar	proto;
	uchar	cdbsz;
	uchar	cdb[16];
};

typedef struct Req Req;
struct Req {
	char	haverfis;
	Rcmd	cmd;
	char	sdstat[16];
	uchar	sense[0x100];
	uchar	data[0x200];
	uint	count;
};

void
turcdb(Req *r)
{
	uchar *cmd;

	cmd = r->cmd.cdb;
	r->cmd.cdbsz = 6;
	r->cmd.proto = Pin;
	memset(cmd, 0, 6);
	r->count = 0;
}

void
reqsensecdb(Req *r)
{
	uchar *cmd;

	cmd = r->cmd.cdb;
	r->cmd.cdbsz = 6;
	r->cmd.proto = Pin;
	memset(cmd, 0, 6);
	cmd[0] = ScmdRsense;
	cmd[4] = 128;
	r->count = 128;
}

static void
sensetrace(uchar *cdb, uchar *u)
{
	char *e;

	if(1)
		return;
	e = scsierror(u[12], u[13]);
	fprint(2, "sense %.2ux: %.2ux%.2ux%.2ux %s\n", cdb[0], u[2], u[12], u[13], e);
}

static int
issuescsi(Req *r, Sdisk *d)
{
	uchar *u;
	int ok, rv, n;
	Req sense;

	if(write(d->fd, r->cmd.cdb, r->cmd.cdbsz) != r->cmd.cdbsz){
		eprint(d, "cdb write error: %r\n");
		return -1;
	}
	werrstr("");
	switch(r->cmd.proto){
	default:
	case Pin:
		n = read(d->fd, r->data, r->count);
		ok = n >= 0;
		r->count = 0;
		if(ok)
			r->count = n;
		break;
	case Pout:
		n = write(d->fd, r->data, r->count);
		ok = n == r->count;
		break;
	}
	rv = 0;
	memset(r->sdstat, 0, sizeof r->sdstat);
	if(read(d->fd, r->sdstat, Replysz) < 1){
		eprint(d, "status reply read error: %r\n");
		return -1;
	}
	if(n == -1)
		rv = -1;		/* scsi not supported; don't whine */
	else if(rv == 0 && (rv = atoi(r->sdstat)) != 0){
		memset(&sense, 0, sizeof sense);
		reqsensecdb(&sense);
		if(issuescsi(&sense, d) == 0){
			memmove(r->sense, sense.data, sense.count);
			u = r->sense;
			rv = u[2];
			sensetrace(r->cmd.cdb, u);
		}else
			rv = -1;
	}
	return ok? rv: -1;
}

void
modesensecdb(Req *r, uchar page, uint n)
{
	uchar *cmd;

	cmd = r->cmd.cdb;
	r->cmd.cdbsz = 10;
	r->cmd.proto = Pin;
	memset(cmd, 0, 10);
	cmd[0] = ScmdMsense10;
	cmd[2] = page;
	cmd[7] = n>>8;
	cmd[8] = n;
	r->count = n;
}

void
modeselectcdb(Req *r, uint n)
{
	uchar *cmd;

	cmd = r->cmd.cdb;
	r->cmd.proto = Pout;
	r->cmd.cdbsz = 10;
	memset(cmd, 0, 10);
	cmd[0] = ScmdMselect10;
	cmd[1] = 0x10;			/* assume scsi2 ! */
	cmd[7] = n>>8;
	cmd[8] = n;
	r->count = n;
}

int
scsiprobe(Sdisk *d)
{
	Req r;

	memset(&r, 0, sizeof r);
	turcdb(&r);
	if(issuescsi(&r, d) == -1)
		return -1;
	memset(&r, 0, sizeof r);
	modesensecdb(&r, 0x1c, sizeof r.data);
	if(issuescsi(&r, d) != 0 || r.count < 8)
		return -1;
	return 0;
}

enum{
	/* mrie bits */
	Mnone		= 0,
	Masync		= 1,		/* obs */
	Mattn		= 2,		/* generate unit attention */
	Mcrerror		= 3,		/* conditionally generate recovered error */
	Mrerror		= 4,		/* unconditionally " */
	Mnosense	= 5,		/* generate no sense */
	Mreqonly	= 6,		/* report only in response to req sense */

	/* byte 2 bits */
	Perf		= 1<<7,	/* smart may not cause delays */
	Ebf		= 1<<5,	/* enable bacground functions */
	Ewasc		= 1<<4,	/* enable warnings */
	Dexcpt		= 1<<3,	/* disable smart */
	Smarttst		= 1<<4,	/* generate spurious smart error 5dff */
	Logerr		= 1<<0,	/* enable reporting */
};

int
scsienable(Sdisk *d)
{
	Req r;

	memset(&r, 0, sizeof r);
	r.data[8 + 0] = 0x1c;
	r.data[8 + 1] = 0xa;
	r.data[8 + 2] = Ebf | Ewasc | Logerr;
	r.data[8 + 3] = Mreqonly;
	r.data[8 +11] = 1;
	modeselectcdb(&r, 12 + 8);
	if(issuescsi(&r, d) != 0)
		return -1;
	return 0;
}

int
scsistatus(Sdisk *d, char *s, int l)
{
	char *err;
	uchar *u;
	int rv;
	Req r;

	memset(&r, 0, sizeof r);
	reqsensecdb(&r);
	rv = issuescsi(&r, d);
	if(rv == 0 && r.count > 12){
		u = r.data;
		if(u[12] + u[13] == 0)
			err = "normal";
		else{
			err = scsierror(u[12], u[13]);
			rv = -1;
		}
		if(err == nil)
			err = "unknown";
		snprint(s, l, "%s", err);
	}else
		snprint(s, l, "smart error");
	return rv;
}
