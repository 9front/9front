#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

#define	Image	IMAGE
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include "screen.h"

Memimage *gscreen;

static struct {
	Rendez;

	Rectangle	rect;
	Proc		*proc;

	uintptr		addr;
} fbscreen;

void
cursoron(void)
{
}

void
cursoroff(void)
{
}

void
setcursor(Cursor*)
{
}

void
flushmemscreen(Rectangle r)
{
	combinerect(&fbscreen.rect, r);
	wakeup(&fbscreen);
}

void
screeninit(void)
{
	conf.monitor = 1;
}

uchar*
attachscreen(Rectangle *r, ulong *chan, int* d, int *width, int *softscreen)
{
	if(gscreen == nil)
		return nil;

	*r = gscreen->r;
	*d = gscreen->depth;
	*chan = gscreen->chan;
	*width = gscreen->width;

	/* make devdraw use gscreen->data */
	*softscreen = 0xa110c;
	gscreen->data->ref++;

	return gscreen->data->bdata;
}

void
getcolor(ulong, ulong *, ulong *, ulong *)
{
}

int
setcolor(ulong, ulong, ulong, ulong)
{
	return 0;
}

void
blankscreen(int)
{
}

void
mousectl(Cmdbuf *)
{
}

static int
isflush(void *)
{
	return !badrect(fbscreen.rect);
}

static void
flushproc(void *arg)
{
	int sno, n, w;
	uchar *sp, *dp, *top;
	Rectangle r;

	for(sno = 0; sno < NSEG; sno++)
		if(up->seg[sno] == nil && sno != ESEG)
			break;
	if(sno == NSEG)
		panic("flushproc");

	up->seg[sno] = arg;

	cclose(up->dot);
	up->dot = up->slash;
	incref(up->dot);

	fbscreen.proc = up;
	if(waserror()){
		print("flushproc: %s\n", up->errstr);
		fbscreen.addr = 0;
		fbscreen.proc = nil;
		return;
	}

	for(;;){
		sleep(&fbscreen, isflush, nil);

		eqlock(&drawlock);
		r = fbscreen.rect;
		fbscreen.rect = ZR;
		if(badrect(r) || gscreen == nil || rectclip(&r, gscreen->r) == 0){
			qunlock(&drawlock);
			continue;
		}
		w = sizeof(ulong)*gscreen->width;
		n = bytesperline(r, gscreen->depth);
		sp = byteaddr(gscreen, r.min);
		dp = (uchar*)fbscreen.addr + (sp - &gscreen->data->bdata[gscreen->zero]);
		top = (uchar*)up->seg[sno]->top;
		if(dp+(Dy(r)-1)*w+n > top)
			r.max.y = (top-(uchar*)fbscreen.addr)/w;
		qunlock(&drawlock);

		while(r.min.y < r.max.y) {
			memmove(dp, sp, n);
			sp += w;
			dp += w;
			r.min.y++;
		}
	}
}

enum {
	CMaddr,
	CMsize,
	CMinit,
};

static Cmdtab fbctlmsg[] = {
	CMaddr,		"addr",		2,
	CMsize,		"size",		3,
	CMinit,		"init",		1,
};

long
fbctlwrite(Chan*, void *a, long n, vlong)
{
	Cmdbuf *cb;
	Cmdtab *ct;
	ulong x, y, z, chan;
	Segment *s;
	uintptr addr;
	char *p;

	cb = parsecmd(a, n);
	if(waserror()){
		free(cb);
		nexterror();
	}
	ct = lookupcmd(cb, fbctlmsg, nelem(fbctlmsg));
	switch(ct->index){
	case CMaddr:
		addr = strtoul(cb->f[1], 0, 0);

		eqlock(&up->seglock);
		if((s = seg(up, addr, 0)) == nil || (s->type&SG_RONLY) != 0){
			qunlock(&up->seglock);
			error(Ebadarg);
		}
		incref(s);
		qunlock(&up->seglock);

		if(fbscreen.proc != nil){
			postnote(fbscreen.proc, 0, "die", NUser);
			while(fbscreen.proc != nil)
				sched();
		}
		fbscreen.addr = addr;
		kproc("fbflush", flushproc, s);
		break;

	case CMsize:
		x = strtoul(cb->f[1], &p, 0);
		if(x == 0 || x > 10240)
			error(Ebadarg);
		if(*p)
			p++;
		y = strtoul(p, &p, 0);
		if(y == 0 || y > 10240)
			error(Ebadarg);
		if(*p)
			p++;
		z = strtoul(p, &p, 0);

		if((chan = strtochan(cb->f[2])) == 0)
			error("bad channel");
		if(chantodepth(chan) != z)
			error("depth, channel do not match");

		cursoroff();
		deletescreenimage();
		eqlock(&drawlock);
		if(memimageinit() < 0){
			qunlock(&drawlock);
			error("memimageinit failed");
		}
		if(gscreen != nil){
			freememimage(gscreen);
			gscreen = nil;
		}
		gscreen = allocmemimage(Rect(0,0,x,y), chan);
		qunlock(&drawlock);
		/* wet floor */

	case CMinit:
		if(gscreen == nil)
			error("no framebuffer");
		resetscreenimage();
		cursoron();
		break;
	}
	free(cb);
	poperror();
	return n;
}

long
fbctlread(Chan*, void *a, long n, vlong offset)
{
	char buf[256], chan[32], *p, *e;

	p = buf;
	e = p + sizeof(buf);
	qlock(&drawlock);
	if(gscreen != nil){
		p = seprint(p, e, "size %dx%dx%d %s\n",
			Dx(gscreen->r), Dy(gscreen->r), gscreen->depth,
			chantostr(chan, gscreen->chan));
	}
	qunlock(&drawlock);
	seprint(p, e, "addr %#p\n", fbscreen.addr);
	return readstr(offset, a, n, buf);
}

