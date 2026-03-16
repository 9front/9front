/* client for the GDB remote serial protocol, documented here:
	https://sourceware.org/gdb/current/onlinedocs/gdb.html/Remote-Protocol.html
*/
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <mach.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>
#include "dat.h"

static char Ebadctl[] = "bad process or channel control request";
static char hex[] = "0123456789abcdef";

/* /proc/$pid/[k]regs holds a Ureg structure for the target platform.
   However, its definition will not always match the output of the
   gdbserver's "g" command, so we need to conversion tables for each
   machine type */

typedef struct Gdbreg Gdbreg;
struct Gdbreg
{
	char *name;	/* matches Reglist[].name */
	int size;	/* may not always agree with libmach */
};

/* registers listed in the order output by gdbserver, no gaps */
static Gdbreg armregs[] = {
	{ "R0", 4 },
	{ "R1", 4 },
	{ "R2", 4 },
	{ "R3", 4 },
	{ "R4", 4 },
	{ "R5", 4 },
	{ "R6", 4 },
	{ "R7", 4 },
	{ "R8", 4 },
	{ "R9", 4 },
	{ "R10", 4 },
	{ "R11", 4 },
	{ "R12", 4 },
	{ "R13", 4 },
	{ "R14", 4 },
	{ "R15", 4 },
	{0},
};

static Gdbreg amd64regs[] = {
	{ "AX", 8 },
	{ "BX", 8 },
	{ "CX", 8 },
	{ "DX", 8 },
	{ "SI", 8 },
	{ "DI", 8 },
	{ "BP", 8 },
	{ "SP", 8 },
	{ "R8", 8 },
	{ "R9", 8 },
	{ "R10", 8 },
	{ "R11", 8 },
	{ "R12", 8 },
	{ "R13", 8 },
	{ "R14", 8 },
	{ "R15", 8 },
	{ "PC", 8 },
	{ "EFLAGS", 4 },
	{ "CS", 4},
	{ "SS", 4},
	{ "DS", 4},
	{ "ES", 4},
	{ "FS", 4},
	{ "GS", 4},
	{0},
};

static Gdbreg *gdbregs[] =
{
	[MARM]		= armregs,
	[MAMD64]	= amd64regs,
};

static Reglist *
findreg(char *rname)
{
	Reglist *r;
	for(r = mach->reglist; r->rname != nil; r++){
		if(strcmp(r->rname, rname) == 0)
			return r;
	}
	return nil;
}

uchar *
bin2ureg(uchar *src)
{
	uchar *ureg;
	Gdbreg *greg;
	Reglist *reg;

	union {
		u16int u16;
		u32int u32;
		u64int u64;
	} v;

	if(mach->mtype > nelem(gdbregs) || gdbregs[mach->mtype] == nil){
		werrstr("no register map for %s", mach->name);
		return nil;
	}
	ureg = mallocz(mach->regsize, 1);
	if(ureg == nil)
		return nil;

	for(greg = gdbregs[mach->mtype]; greg->name != nil; greg++){
		if((reg = findreg(greg->name)) == nil)
			continue;

		memmove(&v, src, greg->size);
		src += greg->size;

		switch(reg->rformat){
		case 'x':
			memmove(&ureg[reg->roffs], &v.u16, sizeof v.u16);
			break;
		case 'X': case 'W': case 'f':
			memmove(&ureg[reg->roffs], &v.u32, sizeof v.u32);
			break;
		case 'Y': case 'F':
			memmove(&ureg[reg->roffs], &v.u64, sizeof v.u64);
			break;
		}
	}
	return ureg;
}

static uintptr
off2addr(vlong off)
{
	off <<= 1;
	off >>= 1;
	return off;
}

static int
badsum(char *p, uint sum)
{
	while(*p)
		sum -= *p++;
	return sum & 0xff;
}

static long
hex2bin(char *dst, char *src, long len)
{
	int i, n, v;

	for(i = n = 0; n < len && src[i]; i++){
		switch(src[i]){
		case '0' ... '9':
			v = src[i] - '0';
			break;
		case 'a' ... 'f':
			v = src[i] - 'a' + 10;
			break;
		case 'A' ... 'F':
			v = src[i] - 'A' + 10;
			break;
		default:
			/* todo: run-length encoding */
			werrstr("bad hex digit %c", src[i]);
			return -1;
		}
		dst[n] = (dst[n] << 4) + v;
		n += i & 1;
	}
	if(i & 1){
		werrstr("incomplete hex digit");
		return -1;
	}
	return n;
}

static Channel *
cmdstr(char *str)
{
	Channel *rc;
	char *err;
	
	rc = chancreate(sizeof(char*), 0);
	if(sendp(gdb.c, rc) < 0){
		werrstr("interrupted");
		free(str);
		chanfree(rc);
		return nil;
	}
	if(sendp(rc, str) < 0){
		werrstr("interrupted");
		free(str);
		chanclose(rc);
		return nil;
	}

	/* ack */
	if(recv(rc, &err) < 0){
		werrstr("interrupted");
		chanclose(rc);
		return nil;
	}
	if(err != nil){
		werrstr("%s", err);
		free(err);
		chanclose(rc);
		return nil;
	}
	return rc;
}

static Channel *
vcmd(char *fmt, va_list args)
{
	int sum;
	char buf[512], *s, *e, *p;

	e = buf + sizeof buf;
	s = p = seprint(buf, e, "$");
	s = vseprint(s, e, fmt, args);

	for(sum = 0; *p != 0; p++)
		sum += *p;
	seprint(s, e, "#%02x", sum & 0xff);
	return cmdstr(estrdup9p(buf));
}

static Channel *
cmd(char *fmt, ...)
{
	Channel *rc;
	va_list args;
	va_start(args, fmt);
	rc = vcmd(fmt, args);
	va_end(args);
	return rc;
}

static char *
reply(Channel *rc)
{
	char *rsp;
	if(recv(rc, &rsp) < 0){
		werrstr("interrupted");
		chanclose(rc);
		return nil;
	}
	chanclose(rc);

	if(*rsp == 'E'){
		werrstr("%s", rsp);
		free(rsp);
		return nil;
	}
	return rsp;
}

static char *
cmdreply(char *fmt, ...)
{
	Channel *rc;
	va_list args;

	va_start(args, fmt);
	rc = vcmd(fmt, args);
	va_end(args);

	if(rc != nil)
		return reply(rc);
	return nil;
}

/* Protocol between gdbproc (G) and request handler (R):

	R → G command (char*)
	R ← G ack (nil) or err (char*)
	if !err:
		R ← G reply (char*) or err ("E...")
	R → G close()

   R closes channel when it goes out of scope
   G frees channel after receiving close
*/
static void
gdbproc(void *)
{
	int c, tries;
	Channel *rc;
	char *req, *rsp, sum[4];

	memset(sum, 0, sizeof sum);
	while(rc = recvp(gdb.c)){
		if(recv(rc, &req) < 0)
			goto next;

		tries = 5;
		do{
			dbg("→ %s\n", req);
			if(fprint(gdb.wfd, "%s", req) < 0)
				sysfatal("gdbproc send req: %r");
			c = Bgetc(gdb.rb);
			dbg("← %c\n", c);
		} while(c == '-' && tries --> 0);
		free(req);

		if(c != '+'){
			chanprint(rc, "E.wanted '+', got '%c'", c);
			goto next;
		}

		/* command ACKed. "start" needs this to end 9P req.
		   we must consume the response, so continue on
		   unconditionally, even if caller was interrupted */
		sendp(rc, nil);
		
		/* Skip notification packets. These are unsolicited,
		   and must not contain the start-of-packet marker '$' */
		if(Brdline(gdb.rb, '$') == nil
		|| Blinelen(gdb.rb) > 1 && fprint(gdb.wfd, "+") < 0)
		{
			sysfatal("gdbproc scan '$': %r");
		}

		rsp = Brdstr(gdb.rb, '#', 1);
		if(rsp != nil) dbg("← %s\n", rsp);

		if(rsp == nil)
			rsp = smprint("E.%r");
		else if(Bread(gdb.rb, sum, 2) < 0){
			free(rsp);
			rsp = smprint("E.%r");
		}
		else if(badsum(rsp, strtoul(sum, nil, 16))){
			free(rsp);
			rsp = smprint("E.bad checksum %s", sum);
		}
		else if(fprint(gdb.wfd, "+") < 0){
			free(rsp);
			rsp = smprint("E.ack %r");
		}
		if(sendp(rc, rsp) < 0)
			free(rsp);
next:
		recv(rc, nil);
		chanfree(rc);
	}
	chanclose(gdb.c);
}

void
gdbinit(int rfd, int wfd)
{
	static char qSupported[] = "qSupported:error-message+";
	int i, n;
	char *rsp, *features[64], *kv[2];
	
	gdb.wfd = wfd;
	gdb.rb = Bfdopen(rfd, OREAD);
	if(gdb.rb == nil)
		sysfatal("gdbinit: %r");

	gdb.c = chancreate(sizeof(Channel*), 0);
	gdb.tid = proccreate(gdbproc, nil, mainstacksize);
	
	if((rsp = cmdreply("%s", qSupported)) == nil)
		sysfatal("gdbinit: %r");

	n = getfields(rsp, features, nelem(features), 1, ";");
	for(i = 0; i < n; i++){
		if(getfields(features[i], kv, nelem(kv), 1, "=") != 2)
			continue;
		if(strcmp(kv[0], "PacketSize") == 0)
			gdb.pktlen = strtol(kv[1], nil, 16);
	}
	free(rsp);
	qlock(&gdb);
	gdb.state = Stopped;
	qunlock(&gdb);
}

void
gdbshutdown(void)
{
	qlock(&gdb);
	gdb.state = Shutdown;
	qunlock(&gdb);

	threadint(gdb.tid);
	if(sendp(gdb.c, nil) < 0)
		sysfatal("gdbshutdown: %r");
	recv(gdb.c, nil);
	chanfree(gdb.c);

	if(fprint(gdb.wfd, "$D#44") < 0)
		sysfatal("gdbshutdown detach: %r");
	Bterm(gdb.rb);
	close(gdb.wfd);
}

void
gdbwritemem(Req *r)
{
	Channel *rc;
	int i, sum, lo, hi;
	ulong count;
	char *rsp, *req, *s, *e, *b;

	count = r->ifcall.count;
	if(gdb.pktlen > Minwrite && count > (gdb.pktlen - Minwrite)/2)
		count = (gdb.pktlen - Minwrite)/2;

	s = req = emalloc9p(count + Minwrite);
	e = req + count + Minwrite;
	s = seprint(s, e, "$M%llux,%lux:", off2addr(r->ifcall.offset), count);

	for(b = req + 1, sum = 0; b < s; b++)
		sum += *b;
	for(i = 0; i < count; i++){
		hi = hex[(r->ifcall.data[i] & 0xf0) >> 4];
		lo = hex[(r->ifcall.data[i] & 0x0f) >> 0];
		sum += hi + lo;
		*s++ = hi;
		*s++ = lo;
	}
	seprint(s, e, "#%02x", sum & 0xff);

	qlock(&gdb);
	if(gdb.state != Stopped){
		respond(r, Ebadctl);
		qunlock(&gdb);
		free(req);
		return;
	}
	if((rc = cmdstr(req)) == nil || (rsp = reply(rc)) == nil)
		responderror(r);
	else {
		r->ofcall.count = count;
		respond(r, nil);
		free(rsp);
	}
	qunlock(&gdb);
}

void
gdbreadreg(Req *r)
{
	char *rsp;
	uchar *ureg;

	qlock(&gdb);
	if(gdb.state != Stopped){
		respond(r, Ebadctl);
		qunlock(&gdb);
		return;
	}
	rsp = cmdreply("g");
	qunlock(&gdb);

	if(rsp == nil){
		responderror(r);
		return;
	}
	if(hex2bin(rsp, rsp, strlen(rsp)) < 0)
		responderror(r);
	else if((ureg = bin2ureg((uchar*)rsp)) == nil)
		responderror(r);
	else {
		readbuf(r, ureg, mach->regsize);
		respond(r, nil);
		free(ureg);
	}
	free(rsp);
}

void
gdbwritereg(Req *r)
{
	respond(r, "not implemented");
}

void
gdbreadmem(Req *r)
{
	long n;
	ulong len;
	char *rsp;

	len = r->ifcall.count;
	if(gdb.pktlen > 0 && len > (gdb.pktlen - Minwrite)/2)
		len = (gdb.pktlen - Minwrite)/2;

	qlock(&gdb);
	if(gdb.state != Stopped){
		respond(r, Ebadctl);
		qunlock(&gdb);
		return;
	}
	rsp = cmdreply("m%llux,%lux", off2addr(r->ifcall.offset), len);
	qunlock(&gdb);

	if(rsp == nil){
		responderror(r);
		return;
	}
	if((n = hex2bin(r->ofcall.data, rsp, len)) < 0)
		responderror(r);
	else {
		r->ofcall.count = n;
		respond(r, nil);
	}
	free(rsp);
}

void
gdbstart(Req *r)
{
	static char haltcodes[] = "STXWN";
	char *rsp;
	Srv *srv;
	Channel *rc;

	srv = r->srv;

	qlock(&gdb);
	if(gdb.state != Stopped){
		respond(r, Ebadctl);
		qunlock(&gdb);
		return;
	}
	if((rc = cmd("c")) == nil){
		responderror(r);
		qunlock(&gdb);
		return;
	}
	gdb.state = Running;
	respond(r, nil);
	qunlock(&gdb);

	/* we need to stick around to track the state of
	   the target, even though 9P request is done */
	srvrelease(srv);
	rsp = reply(rc);
	srvacquire(srv);

	if(rsp == nil)
		sysfatal("gdbstart reply: %r");
	if(strchr(haltcodes, *rsp) == nil)
		sysfatal("gdbstart: bad response %s", rsp);
	free(rsp);
	qlock(&gdb);
	gdb.state = Stopped;
	qunlock(&gdb);
}

static int
interrupt(void)
{
	dbg("→ \\x03\n");

	/* we should probably have a lock guarding writes,
	   but this program makes small writes; it's unlikely
	   this would wind up in the middle of another one */
	return write(gdb.wfd, "\x03", 1);
}

void
gdbstop(Req *r)
{
	char *rsp;

	qlock(&gdb);
	if(gdb.state == Stopped)
		respond(r, nil);

	else if(interrupt() < 0)
		responderror(r);

	else if((rsp = cmdreply("?")) == nil)
		responderror(r);
	else {
		gdb.state = Stopped;
		free(rsp);
		respond(r, nil);
	}
	qunlock(&gdb);
}

void
gdbstartstop(Req *r)
{
	Channel *rc;
	char *rsp, err[sizeof "interrupted"];

	qlock(&gdb);
	if(gdb.state != Stopped){
		respond(r, Ebadctl);
		qunlock(&gdb);
		return;
	}
	if((rc = cmd("c")) == nil){
		responderror(r);
		qunlock(&gdb);
		return;
	}
	gdb.state = Running;
	qunlock(&gdb);

	/* target could run indefinitely, or until another
	   request stops it, so we need to unblock srv */
	srvrelease(r->srv);
	rsp = reply(rc);
	rerrstr(err, sizeof err);
	srvacquire(r->srv);

	if(rsp == nil && strcmp(err, "interrupted") == 0)
		interrupt();
	
	qlock(&gdb);
	gdb.state = Stopped;
	qunlock(&gdb);

	if(rsp == nil){
		responderror(r);
	} else {
		respond(r, nil);
		free(rsp);
	}
}

void
gdbwaitstop(Req *r)
{
	char *rsp;

	srvrelease(r->srv);
	rsp = cmdreply("?");
	srvacquire(r->srv);

	if(rsp  == nil)
		responderror(r);
	else {
		respond(r, nil);
		free(rsp);
	}
}
