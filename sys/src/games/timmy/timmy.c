#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include <cursor.h>
#include "dat.h"
#include "fns.h"

Screen *scr;
Image *work, *tray;
Image *grey;
Obj trayo;
Obj worko;
Obj runo;
Mousectl *mc;
Keyboardctl *kc;
Obj *carry;
int showcarry;
extern int Steps;

void *
emalloc(ulong sz)
{
	void *v;
	
	v = malloc(sz);
	if(v == nil) sysfatal("malloc: %r");
	memset(v, 0, sz);
	setmalloctag(v, getcallerpc(&sz));
	return v;
}

Image *
rgb(u32int c)
{
	return allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, c);
}

void
addtray(ObjT *t, ...)
{
	Obj *o;
	va_list va;
	static double trayw;
	
	va_start(va, t);
	for(; t != nil; t = va_arg(va, ObjT *)){
		o = mkobj(t);
		o->tab->move(o, 0, 0, 0);
		trayw += TraySpc;
		o->tab->move(o, trayw + Dx(o->bbox)/2, TrayH/2, 0);
		trayw += Dx(o->bbox);
		objcat(&trayo, o);
	}
	va_end(va);
}

static void
drawtray(void)
{
	Obj *o;
	
	for(o = trayo.next; o != &trayo; o = o->next)
		o->tab->draw(o, tray);
}

static void
screeninit(void)
{
	grey = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0xCCCCCCFF);
	scr = allocscreen(screen, display->white, 0);
	work = allocwindow(scr, Rect(screen->r.min.x, screen->r.min.y, screen->r.max.x, screen->r.max.y - TrayH), 0, 0xFFFFFFFF);
	tray = allocwindow(scr, Rect(screen->r.min.x, screen->r.max.y - TrayH, screen->r.max.x, screen->r.max.y), 0, 0xCCCCCCFF);
}

static Obj *
objclick(Point p, Obj *l)
{
	Obj *o;

	for(o = l->next; o != l; o = o->next)
		if(ptinrect(p, o->bbox))
			return o;
	return nil;
}

static void
workdraw(void)
{
	Obj *o;

	draw(work, work->r, display->white, nil, ZP);
	for(o = worko.next; o != &worko; o = o->next)
		o->tab->draw(o, work);
	if(carry != nil && showcarry)
		carry->tab->draw(carry, work);
	flushimage(display, 1);
}

static void
rundraw(void)
{
	Obj *o;

	draw(work, work->r, display->white, nil, ZP);
	for(o = runo.next; o != &runo; o = o->next)
		o->tab->draw(o, work);
	flushimage(display, 1);
}

static int
canhinge(Obj *a, Obj *b)
{
	Hinge *h, *k;
	
	if(a->hinge == nil || b->hinge == nil) return 0;
	for(h = a->hinge; h != nil; h = h->onext)
		for(k = b->hinge; k != nil; k = k->onext)
			if(vecdist(h->p, k->p) <= HingeSep)
				return 1;
	return 0;
}

static int
hinge(Obj *a, Obj *b)
{
	Hinge *h, *k, *l;
	
	if(a->hinge == nil || b->hinge == nil) return 0;
	for(h = a->hinge; h != nil; h = h->onext)
		for(k = b->hinge; k != nil; k = k->onext)
			if(vecdist(h->p, k->p) <= HingeSep){
				h->cprev->cnext = k;
				k->cprev->cnext = h;
				l = h->cprev;
				h->cprev = k->cprev;
				k->cprev = l;
				b->tab->move(b, b->p.x + h->p.x - k->p.x, b->p.y + h->p.y - k->p.y, b->θ);
				return 1;
			}
	return 0;	
}

static void
place(void)
{
	Obj *o;
	int hinges;
	
	hinges = 0;
	for(o = worko.next; o != &worko; o = o->next)
		if(objcoll(o, carry))
			if(canhinge(o, carry))
				hinges++;
			else
				return;
	for(o = worko.next; hinges > 0 && o != &worko; o = o->next)
		if(objcoll(o, carry))
			hinges -= hinge(o, carry);
	if(hinges != 0) print("hinge error\n");
	objcat(&worko, carry);
	carry = nil;
	workdraw();
}

static void
mouse(void)
{
	static int lbut = -1;
	Point p;
	
	if(lbut < 0)
		lbut = mc->buttons;
	if(ptinrect(mc->xy, work->r)){
		p = subpt(mc->xy, work->r.min);
		if(carry != nil && (carry->p.x != p.x || carry->p.y != p.y || !showcarry)){
			carry->tab->move(carry, p.x, p.y, carry->θ);
			showcarry = 1;
			workdraw();
		}
	}else if(showcarry){
		showcarry = 0;
		if(carry != nil)
			workdraw();
	}
	if((~mc->buttons & lbut & 1) != 0){
		if(ptinrect(mc->xy, tray->r)){
			carry = objclick(subpt(mc->xy, tray->r.min), &trayo);
			if(carry != nil)
				carry = objdup(carry);
		}else if(ptinrect(mc->xy, work->r)){
			if(carry != nil)
				place();
			else{
				carry = objclick(subpt(mc->xy, work->r.min), &worko);
				if(carry != nil)
					objexcise(carry);
			}
		}
	}
	if((~mc->buttons & lbut & 4) != 0){
		if(carry != nil){
			freeobj(carry);
			carry = nil;
			showcarry = 0;
			workdraw();
		}else if(ptinrect(mc->xy, work->r)){
			carry = objclick(subpt(mc->xy, work->r.min), &worko);
			if(carry != nil)
				carry = objdup(carry);
		}
	}
	lbut = mc->buttons;
}

static void
run(void)
{
	Obj *o, *oo;
	Rune r;
	static Cursor cursor;

	for(o = runo.next; o != &runo; o = oo){
		oo = o->next;
		freeobj(o);
	}
	for(o = worko.next; o != &worko; o = o->next)
		objcat(&runo, objdup(o));
	copyhinges(&worko, &runo);
	setcursor(mc, &cursor);
	for(;;){
		Alt a[] = {
			{mc->c, &mc->Mouse, CHANRCV},
			{kc->c, &r, CHANRCV},
			{nil, nil, CHANNOBLK}
		};
		
		switch(alt(a)){
		case 0: mouse(); break;
		case 1:
			switch(r){
			case ' ': goto out;
			case Kdel: threadexitsall(nil);
			}
		}
		
		physstep();
		rundraw();
	}
out:
	workdraw();
	setcursor(mc, nil);	
}

static void
key(Rune r)
{
	switch(r){
	case Kdel:
		threadexitsall(nil);
	case 'w':
		if(carry != nil){
			carry->tab->move(carry, carry->p.x, carry->p.y, carry->θ - 15);
			workdraw();
		}
		break;
	case 'e':
		if(carry != nil){
			carry->tab->move(carry, carry->p.x, carry->p.y, carry->θ + 15);
			workdraw();
		}
		break;
	case ' ':
		run();
		break;
	}
}

static void
usage(void)
{
	fprint(2, "usage: %s [-s steps]\n", argv0);
	threadexitsall("usage");
}

void
threadmain(int argc, char **argv)
{
	void simpleinit(void);
	Rune r;
	char *s;
	
	ARGBEGIN{
	case 's':
		Steps = strtol(EARGF(usage()), &s, 0);
		if(*s != 0) usage();
		break;
	default: usage();
	}ARGEND;

	if(initdraw(nil, nil, nil) < 0) sysfatal("initdraw: %r");
	mc = initmouse(nil, screen);
	if(mc == nil) sysfatal("initmouse: %r");
	kc = initkeyboard(nil);
	if(kc == nil) sysfatal("initkeyboard: %r");
	screeninit();
	trayo.prev = trayo.next = &trayo;
	worko.prev = worko.next = &worko;
	runo.prev = runo.next = &runo;
	simpleinit();
	drawtray();
	flushimage(display, 1);

	for(;;){
		Alt a[] = {
			{mc->c, &mc->Mouse, CHANRCV},
			{kc->c, &r, CHANRCV},
			{nil, nil, CHANEND}
		};
		
		switch(alt(a)){
		case 0: mouse(); break;
		case 1: key(r); break;
		}
	}
}
