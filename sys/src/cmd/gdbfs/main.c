#include <u.h>
#include <libc.h>
#include <bio.h>
#include <mach.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>
#include "dat.h"

int debug;
void
dbg(char *fmt, ...)
{
	va_list args;
	if(debug){
		va_start(args, fmt);
		vfprint(2, fmt, args);
		va_end(args);
	}
}

int textfd = -1;
char *srvname;
char *procname = "1";

void
usage(void)
{
	fprint(2, "usage: gdbfs [-s srvname] [-p pid] [-t text] [addr]\n");
	threadexitsall("usage");
}

enum
{
	Xctl	= 1,
	Xfpregs,
	Xkregs,
	Xmem,
	Xproc,
	Xregs,
	Xtext,
	Xstatus,
};

struct {
	char *s;
	int id;
	int mode;
} tab[] = {
	"ctl",		Xctl,		0666,
	"fpregs",	Xfpregs,	0666,
	"kregs",	Xkregs,		0666,
	"mem",		Xmem,		0666,
	"regs",		Xregs,		0666,
	"text",		Xtext,		0444,
	"status",	Xstatus,	0444,
};

void
die(Srv*)
{
	threadint(gdb.tid);
	gdbshutdown();
}

void
fsopen(Req *r)
{
	switch((uintptr)r->fid->file->aux){
	case Xfpregs:
	case Xkregs:
	case Xregs:
	case Xmem:
		/* data is hex encoded, so packet size is ×2 iounit */
		if(gdb.pktlen > Minwrite)
			r->ofcall.iounit = (gdb.pktlen - Minwrite)/2;
		break;
	}
	respond(r, nil);
}

void
fsread(Req *r)
{
	int n, i;
	char buf[512], *status;
	
	switch((uintptr)r->fid->file->aux){
	case Xctl:
		readstr(r, procname);
		respond(r, nil);
		break;
	case Xfpregs:
		respond(r, "not implemented");
		break;
	case Xkregs:
	case Xregs:
		gdbreadreg(r);
		break;
	case Xmem:
		gdbreadmem(r);
		break;
	case Xtext:
		if(textfd != -1)
			n = pread(textfd, r->ofcall.data, r->ifcall.count, r->ifcall.offset);
		else
			n = 0;
		if(n < 0)
			responderror(r);
		else {
			r->ofcall.count = n;
			respond(r, nil);
		}
		break;
	case Xstatus:
		qlock(&gdb);
		switch(gdb.state){
		case Stopped:
			status = "Stopped";
			break;
		case Running:
			status = "Running";
			break;
		case Shutdown:
			status = "Moribund";
			break;
		default:
			status = "New";
			break;
		}
		qunlock(&gdb);
		n = sprint(buf, "%-28s%-28s%-28s", "remote", "system", status);
		for(i = 0; i < 9; i++)
			n += sprint(buf+n, "%-12d", 0);
		readstr(r, buf);
		respond(r, nil);
		break;
	default:
		respond(r, "Egreg");
	}
}

void
doctl(Req *r)
{
	enum {
		/* see proc(3) */
		Stop,
		Start,
		Waitstop,
		Startstop,
	};
	Cmdbuf *cb;
	Cmdtab *ct;
	static Cmdtab cmds[] = {
		{ Stop, "stop", 1 },
		{ Start, "start", 1 },
		{ Waitstop, "waitstop", 1 },
		{ Startstop, "startstop", 1 },
	};
	
	cb = parsecmd(r->ifcall.data, r->ifcall.count);
	ct = lookupcmd(cb, cmds, nelem(cmds));
	free(cb);
	if(ct == nil){
		responderror(r);
		free(cb);
		return;
	}
	r->ofcall.count = r->ifcall.count;

	switch(ct->index){
	default:
		respond(r, "not implemented");
		break;
	case Stop:
		gdbstop(r);
		break;
	case Start:
		gdbstart(r);
		break;
	/* remaining commands block, so we may interrupt them */
	case Waitstop:
		r->aux = (void*)threadid();
		gdbwaitstop(r);
		break;
	case Startstop:
		r->aux = (void*)threadid();
		gdbstartstop(r);
		break;
	}
}

void
fswrite(Req *r)
{
	switch((uintptr)r->fid->file->aux){
	case Xctl:
		doctl(r);
		break;
	case Xfpregs:
	case Xkregs:
	case Xregs:
		gdbwritereg(r);
		break;
	case Xmem:
		gdbwritemem(r);
		break;
	case Xtext:
	case Xstatus:
	default:
		respond(r, "Egreg");
		break;
	}
}

void
fsflush(Req *r)
{
	if(r->oldreq->aux != 0)
		threadint((uintptr)r->oldreq->aux);
	respond(r, nil);
}

void
fsstat(Req *r)
{
	Dir *d;
	
	switch((uintptr)r->fid->file->aux) {
	default:
		respond(r, nil);
		break;
	case Xregs:
	case Xkregs:
		r->d.length = mach->regsize;
		break;
	case Xtext:
		/* setting the correct size here allows libmach to seek
		   to the end of the file in thumbpctab(), and probably
		   elsewhere */
		if((d = dirfstat(textfd)) == nil){
			responderror(r);
			break;
		}
		r->d.length = d->length;
		respond(r, nil);
		free(d);
		break;
	}
}

static int rfd = 0, wfd = 1;

static void
start(Srv*)
{
	gdbinit(rfd, wfd);
}

Srv fs = {
	.start	= start,
	.open	= fsopen,
	.read	= fsread,
	.write	= fswrite,
	.flush	= fsflush,
	.stat	= fsstat,
	.end	= die,
};

void
threadmain(int argc, char *argv[])
{
	int i;
	File *dir;
	Fhdr hdr;
	char *textfile, *arch;

	fmtinstall('F', fcallfmt);
	textfile = arch = nil;
	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	case 'd':
		debug = 1;
		break;
	case 'p':
		procname = EARGF(usage());
		break;
	case 'm':
		arch = EARGF(usage());
		break;
	case 't':
		textfile = EARGF(usage());
		break;
	case 's':
		srvname = EARGF(usage());
		break;
	default:
		usage();
	} ARGEND;

	if(textfile != nil){
		textfd = open(textfile, OREAD);
		if(textfd == -1)
			sysfatal("open %s: %r", textfile);
		if(crackhdr(textfd, &hdr) < 0)
			sysfatal("crackhdr: %r");
	} else if(arch != nil){
		if(machbyname(arch) < 0)
			sysfatal("machbyname: %r");
	} else
		sysfatal("-t or -m required");
	
	switch(argc){
	case 1:
		rfd = dial(netmkaddr(argv[0], "tcp", "1234"), nil, nil, nil);
		if(rfd == -1)
			sysfatal("dial %s: %r", argv[0]);
		wfd = rfd;
		break;
	case 0:
		break;
	default:
		usage();
	}
	
	fs.tree = alloctree("gdbfs", "gdbfs", DMDIR|0555, nil);
	dir = createfile(fs.tree->root, procname, "gdbfs", DMDIR|0555, 0);
	for(i = 0; i < nelem(tab); i++)
		closefile(createfile(dir, tab[i].s, "gdbfs", tab[i].mode, (void*)tab[i].id));
	closefile(dir);
	threadpostmountsrv(&fs, srvname, "/proc", MBEFORE);
	exits(0);
}
