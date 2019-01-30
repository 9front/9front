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

static struct {
	Rendez;

	Cursor;

	Proc		*proc;
	uintptr		addr;
} hwcursor;

void
cursoron(void)
{
	wakeup(&hwcursor);
}

void
cursoroff(void)
{
}

void
setcursor(Cursor *curs)
{
	hwcursor.Cursor = *curs;
}

void
flushmemscreen(Rectangle r)
{
	if(badrect(fbscreen.rect))
		fbscreen.rect = r;
	else
		combinerect(&fbscreen.rect, r);
	wakeup(&fbscreen);
}

void
screeninit(void)
{
	conf.monitor = 1;
}

Memdata*
attachscreen(Rectangle *r, ulong *chan, int* d, int *width, int *softscreen)
{
	if(gscreen == nil)
		return nil;

	*r = gscreen->r;
	*d = gscreen->depth;
	*chan = gscreen->chan;
	*width = gscreen->width;
	*softscreen = 1;

	gscreen->data->ref++;
	return gscreen->data;
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


static void
cursorproc(void *arg)
{
	uchar *set, *clr;
	u32int *reg;
	Point xy;
	int i;

	for(i = 0; i < NSEG; i++)
		if(up->seg[i] == nil && i != ESEG)
			break;
	if(i == NSEG)
		panic(up->text);

	up->seg[i] = arg;

	hwcursor.proc = up;
	if(waserror()){
		hwcursor.addr = 0;
		hwcursor.proc = nil;
		pexit("detached", 1);
	}

	reg = (u32int*)hwcursor.addr;
	for(;;){
		eqlock(&drawlock);
		xy = addpt(mousexy(), hwcursor.offset);
		qunlock(&drawlock);

		set = hwcursor.set;
		clr = hwcursor.clr;
		for(i=0; i<8; i++){
			reg[0x70/4 + i] = clr[i*4]<<24 | clr[i*4+1]<<16 | clr[i*4+2]<<8 | clr[i*4+3];
			reg[0x90/4 + i] = set[i*4]<<24 | set[i*4+1]<<16 | set[i*4+2]<<8 | set[i*4+3];
		}
		reg[0] = (xy.x<<16) | (xy.y&0xFFFF);

		sleep(&hwcursor, return0, nil);
	}
}

void
mousectl(Cmdbuf *cb)
{
	Segment *s;
	uintptr addr;

	if(strcmp(cb->f[0], "addr") == 0 && cb->nf == 2){
		s = nil;
		addr = strtoul(cb->f[1], 0, 0);
		if(addr != 0){
			if((s = seg(up, addr, 0)) == nil || (s->type&SG_RONLY) != 0
			|| (addr&3) != 0 || addr+0xB0 > s->top)
				error(Ebadarg);
			incref(s);
		}
		if(hwcursor.proc != nil){
			postnote(hwcursor.proc, 0, "die", NUser);
			while(hwcursor.proc != nil)
				sched();
		}
		if(addr != 0){
			hwcursor.addr = addr;
			kproc("cursor", cursorproc, s);
		}
		return;
	}

	if(strcmp(cb->f[0], "linear") == 0){
		mouseaccelerate(0);
		return;
	}

	if(strcmp(cb->f[0], "accelerated") == 0){
		mouseaccelerate(cb->nf == 1 ? 1 : atoi(cb->f[1]));
		return;
	}
}

static int
isflush(void *)
{
	return !badrect(fbscreen.rect);
}

static void
screenproc(void *arg)
{
	int sno, n, w;
	uchar *sp, *dp, *top;
	Rectangle r;

	for(sno = 0; sno < NSEG; sno++)
		if(up->seg[sno] == nil && sno != ESEG)
			break;
	if(sno == NSEG)
		panic(up->text);

	up->seg[sno] = arg;

	fbscreen.proc = up;
	if(waserror()){
		fbscreen.addr = 0;
		fbscreen.proc = nil;
		pexit("detached", 1);
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
		s = nil;
		addr = strtoul(cb->f[1], 0, 0);
		if(addr != 0){
			if((s = seg(up, addr, 0)) == nil || (s->type&SG_RONLY) != 0)
				error(Ebadarg);
			incref(s);
		}
		if(fbscreen.proc != nil){
			postnote(fbscreen.proc, 0, "die", NUser);
			while(fbscreen.proc != nil)
				sched();
		}
		if(addr != 0){
			fbscreen.addr = addr;
			kproc("screen", screenproc, s);
		}
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
