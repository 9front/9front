#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <keyboard.h>
#include <mouse.h>
#include "eui.h"

typedef struct Kfn Kfn;

u64int keys, keys2;
int trace, paused;
int savereq, loadreq;
QLock pauselock;
int scale, fixscale, warp10;
uchar *pic;
Rectangle picr;
Mousectl *mc;
Image *bg;

static int profile, framestep;
static int vwdx, vwdy, vwbpp;
static ulong vwchan;
static Image *fb;

struct Kfn{
	Rune r;
	int k;
	char joyk[16];
	void(*fn)(void);
	Kfn *n;
};
static Kfn kfn, kkn;
static int ax0, ax1;

void *
emalloc(ulong sz)
{
	void *v;

	v = mallocz(sz, 1);
	if(v == nil)
		sysfatal("malloc: %r");
	setmalloctag(v, getcallerpc(&sz));
	return v;
}

static void
joyproc(void *)
{
	char *s, *down[9];
	static char buf[64];
	int n, k, j;
	Kfn *kp;

	j = 1;

	for(;;){
		n = read(0, buf, sizeof(buf) - 1);
		if(n <= 0)
			sysfatal("read: %r");
		buf[n] = 0;
		n = getfields(buf, down, nelem(down), 1, " ");
		k = 0;
		for(n--; n >= 0; n--){
			s = down[n];
			if(strcmp(s, "joy1") == 0)
				j = 1;
			else if(strcmp(s, "joy2") == 0)
				j = 2;
			for(kp=kkn.n; kp!=nil; kp=kp->n){
				if(strcmp(kp->joyk, s) == 0)
					k |= kp->k;
			}
		}
		if(j == 2)
			keys2 = k;
		else
			keys = k;
	}
}

static void
keyproc(void *)
{
	int fd, n, k;
	static char buf[256];
	char *s;
	Rune r;
	Kfn *kp;

	fd = open("/dev/kbd", OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	for(;;){
		if(buf[0] != 0){
			n = strlen(buf)+1;
			memmove(buf, buf+n, sizeof(buf)-n);
		}
		if(buf[0] == 0){
			n = read(fd, buf, sizeof(buf)-1);
			if(n <= 0)
				sysfatal("read /dev/kbd: %r");
			buf[n-1] = 0;
			buf[n] = 0;
		}
		if(buf[0] == 'c'){
			if(utfrune(buf, Kdel)){
				close(fd);
				threadexitsall(nil);
			}
			if(utfrune(buf, KF|5))
				savereq = 1;
			if(utfrune(buf, KF|6))
				loadreq = 1;
			if(utfrune(buf, KF|12))
				profile ^= 1;
			if(utfrune(buf, 't'))
				trace = !trace;
			for(kp=kfn.n; kp!=nil; kp=kp->n){
				if(utfrune(buf, kp->r))
					kp->fn();
			}
		}
		if(buf[0] != 'k' && buf[0] != 'K')
			continue;
		s = buf + 1;
		k = 0;
		while(*s != 0){
			s += chartorune(&r, s);
			switch(r){
			case Kdel: close(fd); threadexitsall(nil);
			case Kesc:
				if(paused)
					qunlock(&pauselock);
				else
					qlock(&pauselock);
				paused = !paused;
				break;
			case KF|1:	
				if(paused){
					qunlock(&pauselock);
					paused=0;
				}
				framestep = !framestep;
				break;
			case '`':
				warp10 = !warp10;
				break;
			}
			for(kp=kkn.n; kp!=nil; kp=kp->n){
				if(utfrune(buf, kp->r))
					k |= kp->k;
			}
		}
		if((k & ax0) == ax0)
			k &= ~ax0;
		if((k & ax1) == ax1)
			k &= ~ax1;
		keys = k;
	}
}

static void
timing(void)
{
	static int fcount;
	static vlong old;
	static char buf[32];
	vlong new;

	if(++fcount == 60)
		fcount = 0;
	else
		return;
	new = nsec();
	if(new != old)
		sprint(buf, "%6.2f%%", 1e11 / (new - old));
	else
		buf[0] = 0;
	draw(screen, rectaddpt(Rect(10, 10, vwdx-40, 30), screen->r.min), bg, nil, ZP);
	string(screen, addpt(screen->r.min, Pt(10, 10)), display->black, ZP, display->defaultfont, buf);
	old = nsec();
}

static void
screeninit(void)
{
	Point p;

	if(!fixscale){
		scale = Dx(screen->r) / vwdx;
		if(Dy(screen->r) / vwdy < scale)
			scale = Dy(screen->r) / vwdy;
	}
	if(scale <= 0)
		scale = 1;
	else if(scale > 16)
		scale = 16;
	p = divpt(addpt(screen->r.min, screen->r.max), 2);
	picr = Rpt(subpt(p, Pt(scale * vwdx/2, scale * vwdy/2)),
		addpt(p, Pt(scale * vwdx/2, scale * vwdy/2)));
	freeimage(fb);
	fb = allocimage(display, Rect(0, 0, scale * vwdx, scale > 1 ? 1 : scale * vwdy),
		vwchan, scale > 1, 0);
	free(pic);
	pic = emalloc(vwdx * vwdy * vwbpp * scale);
	draw(screen, screen->r, bg, nil, ZP);	
}

void
flushmouse(int discard)
{
	Mouse m;

	if(nbrecvul(mc->resizec) > 0){
		if(getwindow(display, Refnone) < 0)
			sysfatal("resize failed: %r");
		screeninit();
	}
	if(discard)
		while(nbrecv(mc->c, &m) > 0)
			;
}

void
flushscreen(void)
{
	if(scale == 1){
		loadimage(fb, fb->r, pic, vwdx * vwdy * vwbpp);
		draw(screen, picr, fb, nil, ZP);
	} else {
		Rectangle r;
		uchar *s;
		int w;

		s = pic;
		r = picr;
		w = vwdx * vwbpp * scale;
		while(r.min.y < picr.max.y){
			loadimage(fb, fb->r, s, w);
			s += w;
			r.max.y = r.min.y+scale;
			draw(screen, r, fb, nil, ZP);
			r.min.y = r.max.y;
		}
	}
	flushimage(display, 1);
	if(profile)
		timing();
}

void
flushaudio(int (*audioout)(void))
{
	static vlong old, delta;
	vlong new, diff;

	if(audioout == nil || audioout() < 0 && !warp10){
		new = nsec();
		diff = 0;
		if(old != 0){
			diff = BILLION/60 - (new - old) - delta;
			if(diff >= MILLION)
				sleep(diff/MILLION);
		}
		old = nsec();
		if(diff > 0){
			diff = (old - new) - (diff / MILLION) * MILLION;
			delta += (diff - delta) / 100;
		}
	}
	if(framestep){
		paused = 1;
		qlock(&pauselock);
		framestep = 0;
	}
}

void
regkeyfn(Rune r, void (*fn)(void))
{
	Kfn *kp;

	for(kp=&kfn; kp->n!=nil; kp=kp->n)
		;
	kp->n = emalloc(sizeof *kp);
	kp->n->r = r;
	kp->n->fn = fn;
}

void
regkey(char *joyk, Rune r, int k)
{
	Kfn *kp;

	for(kp=&kkn; kp->n!=nil; kp=kp->n)
		;
	kp->n = emalloc(sizeof *kp);
	strncpy(kp->n->joyk, joyk, sizeof(kp->n->joyk)-1);
	if(strcmp(joyk, "up") == 0 || strcmp(joyk, "down") == 0)
		ax0 |= k;
	if(strcmp(joyk, "left") == 0 || strcmp(joyk, "right") == 0)
		ax1 |= k;
	kp->n->r = r;
	kp->n->k = k;
}

void
initemu(int dx, int dy, int bpp, ulong chan, int dokey, void(*kproc)(void*))
{
	vwdx = dx;
	vwdy = dy;
	vwchan = chan;
	vwbpp = bpp;
	if(initdraw(nil, nil, nil) < 0)
		sysfatal("initdraw: %r");
	mc = initmouse(nil, screen);
	if(mc == nil)
		sysfatal("initmouse: %r");
	if(dokey)
		proccreate(kproc != nil ? kproc : keyproc, nil, mainstacksize);
	if(kproc == nil)
		proccreate(joyproc, nil, mainstacksize);
	bg = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0xCCCCCCFF);
	scale = fixscale;
	screeninit();
}
