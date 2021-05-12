#include <u.h>
#include <libc.h>
#include <thread.h>
#include <bio.h>
#include <mach.h>
#include <fcall.h>
#include <9p.h>
#include "dat.h"
#include "fns.h"

extern int regsfd;

static char Egreg[] = "the front fell off";
static File *memfile, *regsfile, *kregsfile, *xregsfile;
static uchar ureg[1024];

static int
makeureg(void)
{
	extern Mach mi386, mamd64;
	char rbuf[4096], *p, *q, *f[2];
	Reglist *r;
	uvlong v;
	int rc;

	mach = sizeof(uintptr) == 8 ? &mamd64 : &mi386;
	memset(ureg, 0, mach->regsize);

	rc = pread(regsfd, rbuf, sizeof(rbuf)-1, 0);
	if(rc < 0)
		return -1;
	rbuf[rc] = 0;

	for(p = rbuf; (q = strchr(p, '\n')) != nil; p = q + 1){
		*q = 0;
		if(tokenize(p, f, nelem(f)) < 2) continue;
		for(r = mach->reglist; r->rname != nil; r++){
			if(r->rflags == RINT && cistrcmp(f[0], r->rname) == 0){
				v = strtoull(f[1], nil, 0);
				switch(r->rformat){
				case 'Y':
					PUT64(ureg, r->roffs, v);
					break;
				case 'X':
					PUT32(ureg, r->roffs, v);
					break;
				case 'x':
					PUT16(ureg, r->roffs, v);
					break;
				case 'b':
					PUT8(ureg, r->roffs, v);
					break;
				}
				break;
			}
		}
	}
	return mach->regsize;
}

static uintptr
off2addr(vlong off)
{
	off <<= 1;
	off >>= 1;
	return (uintptr)off;
}

static void
srvread(Req *r)
{
	int rc;

	if(r->fid->file == memfile){
		r->ofcall.count = vmemread(r->ofcall.data, r->ifcall.count, off2addr(r->ifcall.offset));
		if(r->ofcall.count == 0)
			respond(r, "fault");
		else
			respond(r, nil);
		return;
	}
	if(r->fid->file == regsfile || r->fid->file == kregsfile){
		rc = makeureg();
		if(rc < 0){
			responderror(r);
			return;
		}
		readbuf(r, ureg, rc);
		respond(r, nil);
		return;
	}
	if(r->fid->file == xregsfile){
		rc = pread(regsfd, r->ofcall.data, r->ifcall.count, r->ifcall.offset);
		if(rc < 0)
			responderror(r);
		else{
			r->ofcall.count = rc;
			respond(r, nil);
		}
		return;
	}
	respond(r, Egreg);
}

static void
srvwrite(Req *r)
{
	if(r->fid->file == memfile){
		r->ofcall.count = vmemwrite(r->ifcall.data, r->ifcall.count, off2addr(r->ifcall.offset));
		if(r->ofcall.count == 0)
			respond(r, "fault");
		else
			respond(r, nil);
		return;
	}
	respond(r, Egreg);
}

static Srv vmxsrv = {
	.read srvread,
	.write srvwrite,
};

void
init9p(char *srvname)
{
	char *uid;
	
	uid = getuser();
	vmxsrv.tree = alloctree(uid, uid, 0770, nil);
	memfile = createfile(vmxsrv.tree->root, "mem", uid, 0660, nil);
	regsfile = createfile(vmxsrv.tree->root, "regs", uid, 0440, nil);
	kregsfile = createfile(vmxsrv.tree->root, "kregs", uid, 0440, nil);
	xregsfile = createfile(vmxsrv.tree->root, "xregs", uid, 0440, nil);
	threadpostmountsrv(&vmxsrv, srvname, nil, 0);
}
