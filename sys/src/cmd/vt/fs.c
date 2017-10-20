#include <u.h>
#include <libc.h>
#include <draw.h>

#include "cons.h"

#include <thread.h>
#include <fcall.h>
#include <9p.h>

extern Channel *hc[2];

static File *devcons, *devconsctl;

static Channel *readreq;
static Channel *flushreq;

static void
fsreader(void*)
{
	Req *r, *fr;
	char *s, *p;

	r = nil;
	s = p = nil;
	for(;;){
		Alt a[] = {
			{ flushreq, &fr, CHANRCV },
			{ readreq, &r, r == nil ? CHANRCV : CHANNOP },
			{ hc[0], &s, s == nil ? CHANRCV : CHANNOP },
			{ nil, nil, s == nil || r == nil ? CHANEND : CHANNOBLK },
		};
		if(alt(a) == 0){
			if(fr->oldreq == r){
				respond(r, "interrupted");
				r = nil;
			}
			respond(fr, nil);
		}
		if(s == nil || r == nil)
			continue;
		if(p == nil)
			p = s;
		r->ofcall.count = 0;
		while(r->ifcall.count > r->ofcall.count){
			if(*p == 0)
				break;
			r->ofcall.data[r->ofcall.count++] = *p++;
			if(*p == 0){
				free(s);
				if((p = s = nbrecvp(hc[0])) == nil)
					break;
			}
		}
		respond(r, nil);
		r = nil;
	}
}

static void
fsread(Req *r)
{
	if(r->fid->file == devcons){
		sendp(readreq, r);
		return;
	}
	respond(r, "not implemented");
}

typedef struct Partutf Partutf;
struct Partutf
{
	int	n;
	char	s[UTFmax];
};

static Rune*
cvtc2r(char *b, int n, Partutf *u)
{
	char *cp, *ep;
	Rune *rp, *rb;

	cp = b, ep = b + n;
	rp = rb = emalloc9p(sizeof(Rune)*(n+2));

	while(u->n > 0 && cp < ep){
		u->s[u->n++] = *cp++;
		if(fullrune(u->s, u->n)){
			chartorune(rp, u->s);
			if(*rp != 0)
				rp++;
			u->n = 0;
			break;
		}
	}
	if(u->n == 0){
		while(cp < ep && fullrune(cp, ep - cp)){
			cp += chartorune(rp, cp);
			if(*rp != 0)
				rp++;
		}
		n = ep - cp;
		if(n > 0){
			memmove(u->s, cp, n);
			u->n = n;
		}
	}
	if(rb == rp){
		free(rb);
		return nil;
	}
	*rp = 0;

	return rb;
}

static void
fswrite(Req *r)
{
	if(r->fid->file == devcons){
		Partutf *u;
		Rune *rp;

		if((u = r->fid->aux) == nil)
			u = r->fid->aux = emalloc9p(sizeof(*u));
		if((rp = cvtc2r((char*)r->ifcall.data, r->ifcall.count, u)) != nil)
			sendp(hc[1], rp);

		r->ofcall.count = r->ifcall.count;
		respond(r, nil);
		return;
	}
	if(r->fid->file == devconsctl){
		char *s = r->ifcall.data;
		int n = r->ifcall.count;

		if(n >= 5 && strncmp(s, "rawon", 5) == 0)
			cs->raw = 1;
		else if(n >= 6 && strncmp(s, "rawoff", 6) == 0)
			cs->raw = 0;
		else if(n >= 6 && strncmp(s, "holdon", 6) == 0)
			cs->hold = 1;
		else if(n >= 7 && strncmp(s, "holdoff", 7) == 0)
			cs->hold = 0;
		else if(n >= 7 && strncmp(s, "winchon", 7) == 0)
			cs->winch = 1;
		else if(n >= 8 && strncmp(s, "winchoff", 8) == 0)
			cs->winch = 0;

		r->ofcall.count = r->ifcall.count;
		respond(r, nil);
		return;
	}

	respond(r, "not implemented");
}

static void
fsflush(Req *r)
{
	sendp(flushreq, r);
}

static void
fsdestroyfid(Fid *f)
{
	if(f->file == devconsctl && f->omode >= 0){
		cs->raw = 0;
		cs->hold = 0;
		cs->winch = 0;
	}
	if(f->aux != nil){
		free(f->aux);
		f->aux = nil;
	}
}

static void
fsstart(Srv*)
{
	flushreq = chancreate(sizeof(Req*), 4);
	readreq = chancreate(sizeof(Req*), 4);
	proccreate(fsreader, nil, 16*1024);
}

static void
fsend(Srv*)
{
	sendp(hc[1], nil);
}

Srv fs = {
.read=fsread,
.write=fswrite,
.flush=fsflush,
.destroyfid=fsdestroyfid,
.start=fsstart,
.end=fsend,
};

void
mountcons(void)
{
	fs.tree = alloctree("vt", "vt", DMDIR|0555, nil);
	devcons = createfile(fs.tree->root, "cons", "vt", 0666, nil);
	if(devcons == nil)
		sysfatal("creating /dev/cons: %r");
	devconsctl = createfile(fs.tree->root, "consctl", "vt", 0666, nil);
	if(devconsctl == nil)
		sysfatal("creating /dev/consctl: %r");
	threadpostmountsrv(&fs, nil, "/dev", MBEFORE);
}
