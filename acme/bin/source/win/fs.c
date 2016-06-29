#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include "dat.h"

Channel *fschan;
Channel *writechan;

static File *devcons, *devnew, *devwdir;

static void
fsread(Req *r)
{
	Fsevent e;

	if(r->fid->file == devnew){
		if(r->fid->aux==nil){
			respond(r, "phase error");
			return;
		}
		readstr(r, r->fid->aux);
		respond(r, nil);
		return;
	}

	if(r->fid->file == devwdir){
		readstr(r, wdir);
		respond(r, nil);
		return;
	}

	e.type = 'r';
	e.r = r;
	send(fschan, &e);
}

static void
fsflush(Req *r)
{
	Fsevent e;

	e.type = 'f';
	e.r = r;
	send(fschan, &e);
}

static void
fswrite(Req *r)
{
	static Event *e[4];
	Event *ep;
	int nb, wid, pid;
	Rune rune;
	char *s, *se, *d, *p;
	static int n, partial;

	if(r->fid->file == devnew){
		if(r->fid->aux){
			respond(r, "already created a window");
			return;
		}
		s = emalloc(r->ifcall.count+1);
		memmove(s, r->ifcall.data, r->ifcall.count);
		s[r->ifcall.count] = 0;
		pid = strtol(s, &p, 0);
		if(*p==' ')
			p++;
		nb = newpipewin(pid, p);
		free(s);
		s = emalloc(32);
		sprint(s, "%lud", (ulong)nb);
		r->fid->aux = s;
		r->ofcall.count = r->ifcall.count;
		respond(r, nil);
		return;
	}

	if(r->fid->file == devwdir){
		s = emalloc(r->ifcall.count+1);
		memmove(s, r->ifcall.data, r->ifcall.count);
		s[r->ifcall.count] = 0;
		if(s[0] == '#' || s[0] == '/'){
			free(wdir);
			wdir = s;
		} else {
			wdir = eappend(wdir, "/", s);
			free(s);
		}
		cleanname(wdir);
		winsetdir(win, wdir, wname);
		respond(r, nil);
		return;
	}

	if(r->fid->file != devcons){
		respond(r, "bug in fswrite");
		return;
	}

	/* init buffer rings */
	if(e[0] == nil){
		for(n=0; n<nelem(e); n++){
			e[n] = emalloc(sizeof(Event));
			e[n]->c1 = 'S';
		}
	}

	s = r->ifcall.data;
	se = s + r->ifcall.count;

	while((nb = (se - s)) > 0){
		assert(partial >= 0);
		if((partial+nb) > EVENTSIZE)
			nb = EVENTSIZE - partial;

		/* fill buffer */
		ep = e[n++ % nelem(e)];
		memmove(ep->b+partial, s, nb);
		partial += nb;
		s += nb;

		/* check full runes, remove null bytes */
		ep->nr = ep->nb = 0;
		for(d = p = ep->b; partial > 0; partial -= wid, p += wid){
			if(*p == '\0'){
				wid = 1;
				continue;
			}

			if(!fullrune(p, partial))
				break;

			wid = chartorune(&rune, p);
			runetochar(d, &rune);
			d += wid;

			ep->nr++;
			ep->nb += wid;
		}

		/* put partial reminder onto next buffer */
		if(partial > 0)
			memmove(e[n % nelem(e)]->b, p, partial);

		/* send buffer when not empty */
		if(ep->nb > 0){
			ep->b[ep->nb] = '\0';
			sendp(win->cevent, ep);
			recvp(writechan);
		}
	}

	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
}

void
fsdestroyfid(Fid *fid)
{
	if(fid->aux)
		free(fid->aux);
}

Srv fs = {
.read=	fsread,
.write=	fswrite,
.flush=	fsflush,
.destroyfid=	fsdestroyfid,
.leavefdsopen=	1,
};

void
mountcons(void)
{
	fschan = chancreate(sizeof(Fsevent), 0);
	writechan = chancreate(sizeof(void*), 0);
	fs.tree = alloctree("win", "win", DMDIR|0555, nil);
	devcons = createfile(fs.tree->root, "cons", "win", 0666, nil);
	if(devcons == nil)
		sysfatal("creating /dev/cons: %r");
	devnew = createfile(fs.tree->root, "wnew", "win", 0666, nil);
	if(devnew == nil)
		sysfatal("creating /dev/wnew: %r");
	devwdir = createfile(fs.tree->root, "wdir", "win", 0666, nil);
	if(devwdir == nil)
		sysfatal("creating /dev/wdir: %r");
	threadpostmountsrv(&fs, nil, "/dev", MBEFORE);
}
