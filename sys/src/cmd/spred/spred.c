#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include <frame.h>
#include <cursor.h>
#include "dat.h"
#include "fns.h"

Mousectl *mc;
Keyboardctl *kc;
int quitok;

enum {
	ZEROX,
	RESIZE,
	CLOSE,
	WRITE,
	QUIT,
	WIN
};

int 
quit(void)
{
	File *f;
	
	if(!quitok)
		for(f = flist.next; f != &flist; f = f->next)
			if(f->change > 0){
				cmdprint("?\n");
				quitok = 1;
				return 0;
			}
	return -1;
}

static char *
menugen(int n)
{
	File *f;
	static int mw;
	static char buf[512];
	int rc;
	char *p;

	switch(n){
	case ZEROX: return "zerox";
	case CLOSE: return "close";
	case RESIZE: return "resize";
	case WRITE: return "write";
	case QUIT: return "quit";
	}
	if(n < WIN)
		sysfatal("menugen: no string for n=%d", n);
	n -= WIN;
	if(n == 0){
		mw = 0;
		for(f = flist.next; f != &flist; f = f->next){
			rc = filtitlelen(f);
			if(rc > mw)
				mw = rc;
		}
		return "~~spred~~";
	}
	for(f = flist.next; f != &flist; f = f->next)
		if(--n == 0){
			p = filtitle(f, buf, buf + sizeof(buf));
			rc = mw - utflen(buf);
			if(p + rc >= buf + sizeof(buf))
				rc = buf + sizeof(buf) - p - 1;
			memset(p, ' ', rc);
			p[rc] = 0;
			return buf;
		}
	return nil;
	
}

static int
rmb(void)
{
	static Menu menu = {nil, menugen};
	int n;
	Win *w;
	File *f;

	if(actw != nil && actw->tab->rmb != nil && actw->tab->rmb(actw, mc) >= 0)
		return 0;
	n = menuhit(3, mc, &menu, nil);
	if(n < 0)
		return 0;
	switch(n){
	case ZEROX:
		w = winsel(mc, 3);
		if(w != nil)
			winzerox(w, mc);
		return 0;
	case CLOSE:
		w = winsel(mc, 3);
		if(w != nil)
			winclose(w);
		return 0;
	case RESIZE:
		winresize(winsel(mc, 3), mc);
		return 0;
	case WRITE:
		w = winsel(mc, 3);
		if(w != nil)
			winwrite(w, nil);
		return 0;
	case QUIT:
		return quit();
	}
	if(n < WIN)
		sysfatal("rmb: no action for n=%d", n);
	if(n == 0){
		setfocus(cmdw);
		return 0;
	}
	n -= WIN;
	for(f = flist.next; f != &flist; f = f->next)
		if(--n == 0){
			if(f->wins.wnext == &f->wins){
				newwinsel(f->type, mc, f);
				return 0;
			}
			for(w = f->wins.wnext; w != &f->wins && w != actw; w = w->wnext)
				;
			if(w->wnext == &f->wins)
				w = w->wnext;
			setfocus(w->wnext);
			return 0;
		}
	return 0;
}

static void
loop(void)
{
	Rune r;
	int n;

	Alt a[] = {
		{mc->c, &mc->Mouse, CHANRCV},
		{kc->c, &r, CHANRCV},
		{mc->resizec, &n, CHANRCV},
		{nil, nil, CHANEND}
	};
	
	for(;;){
		flushimage(display, 1);
		switch(alt(a)){
		case 0:
			if((mc->buttons & 1) != 0)
				winclick(mc);
			if((mc->buttons & 2) != 0)
				if(actw != nil && actw->tab->menu != nil)
					actw->tab->menu(actw, mc);
			if((mc->buttons & 4) != 0)
				if(rmb() < 0)
					return;
			break;
		case 1:
			if(actw != nil && actw->tab->key != nil)
				actw->tab->key(actw, r);
			break;
		case 2:
			resize();
			break;
		}
	}
}

void
threadmain(int argc, char **argv)
{
	ARGBEGIN {
	default:
		;
	} ARGEND;
	
	quotefmtinstall();
	if(initdraw(nil, nil, nil) < 0)
		sysfatal("initdraw: %r");
	initwin();
	mc = initmouse(nil, screen);
	if(mc == nil)
		sysfatal("initmouse: %r");
	kc = initkeyboard(nil);
	if(kc == nil)
		sysfatal("initkeyboard: %r");
	loop();
	threadexitsall(nil);
}

Cursor crosscursor = {
	{-7, -7},
	{0x03, 0xC0, 0x03, 0xC0, 0x03, 0xC0, 0x03, 0xC0,
	 0x03, 0xC0, 0x03, 0xC0, 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0xC0, 0x03, 0xC0,
	 0x03, 0xC0, 0x03, 0xC0, 0x03, 0xC0, 0x03, 0xC0, },
	{0x00, 0x00, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80,
	 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x7F, 0xFE,
	 0x7F, 0xFE, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80,
	 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x00, 0x00, }
};
