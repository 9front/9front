#include <u.h>
#include <libc.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>
#include "dat.h"
#include "fns.h"

extern int regsfd;
char Egreg[] = "the front fell off";

enum {
	Qregs,
	Qmem,
	Qmax
};

static Dir files[] = {
	[Qregs] {.name "regs", .mode 0440},
	[Qmem] {.name "mem", .mode 0440},
};

void
srvread(Req *r)
{
	int rc;

	switch((int)r->fid->qid.path){
	case Qregs:
		rc = pread(regsfd, r->ofcall.data, r->ifcall.count, r->ifcall.offset);
		if(rc < 0)
			responderror(r);
		else{
			r->ofcall.count = rc;
			respond(r, nil);
		}
		break;
	case Qmem:
		r->ofcall.count = vmemread(r->ofcall.data, r->ifcall.count, r->ifcall.offset);
		if(r->ofcall.count == 0)
			respond(r, "fault");
		else
			respond(r, nil);
		break;
	default:
		respond(r, Egreg);
	}
}

Srv vmxsrv = {
	.read srvread,
};

void
init9p(char *srvname)
{
	char *uid;
	int i;
	
	uid = getuser();
	vmxsrv.tree = alloctree(uid, uid, 0770, nil);
	for(i = 0; i < Qmax; i++)
		createfile(vmxsrv.tree->root, files[i].name, uid, files[i].mode, nil);
	threadpostmountsrv(&vmxsrv, srvname, nil, 0);
}
