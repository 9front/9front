/* i_video.c */

#include "doomdef.h"	// printf
#include "i_system.h"
#include "v_video.h"	// screens[]
#include "d_main.h"	// D_PostEvent

#include <draw.h>
#include <mouse.h>
#include <keyboard.h>

static int resized;
static int mouseactive;

static Rectangle grabout;
static Point center;

static void kbdproc(void);
static void mouseproc(void);

static uchar cmap[3*256];

static int kbdpid = -1;
static int mousepid = -1;

static void
catch(void *, char *msg)
{
	/* in case we crash, disable mouse grab */
	if(strncmp(msg, "sys:", 4) == 0)
		mouseactive = 0;
	noted(NDFLT);
}

void I_InitGraphics(void)
{
	int pid;

	notify(catch);

	if(initdraw(nil, nil, "doom") < 0)
		I_Error("I_InitGraphics failed");

	draw(screen, screen->r, display->black, nil, ZP);

	center = addpt(screen->r.min, Pt(Dx(screen->r)/2, Dy(screen->r)/2));
	grabout = insetrect(screen->r, Dx(screen->r)/8);

	if((pid = rfork(RFPROC|RFMEM)) == 0){
		kbdproc();
		exits(nil);
	}
	kbdpid = pid;

	if((pid = rfork(RFPROC|RFMEM)) == 0){
		mouseproc();
		exits(nil);
	}
	mousepid = pid;
}

void I_ShutdownGraphics(void)
{
	if(kbdpid != -1){
		postnote(PNPROC, kbdpid, "shutdown");
		kbdpid = -1;
	}
	if(mousepid != -1){
		postnote(PNPROC, mousepid, "shutdown");
		mousepid = -1;
	}
}

void I_SetPalette(byte *palette)
{
	uchar *c;

	c = cmap;
	while(c < cmap+3*256)
		*c++ = gammatable[usegamma][*palette++];
}

void I_UpdateNoBlit(void)
{
	// DELETEME?
}

void I_FinishUpdate(void)
{
	Image *rowimg;
	Rectangle r;
	int y, scale;
	uchar *s, *e, *d, *m;
	uchar buf[SCREENWIDTH*3*4];

	if(resized){
		resized = 0;
		if(getwindow(display, Refnone) < 0)
			sysfatal("getwindow: %r");

		/* make black background */
		draw(screen, screen->r, display->black, nil, ZP);

		center = addpt(screen->r.min, Pt(Dx(screen->r)/2, Dy(screen->r)/2));
		grabout = insetrect(screen->r, Dx(screen->r)/8);
	}

	scale = Dx(screen->r)/SCREENWIDTH;
	if(scale <= 0)
		scale = 1;
	else if(scale > 4)
		scale = 4;

	/* where to draw the scaled row */
	r = rectsubpt(rectaddpt(Rect(0, 0, scale*SCREENWIDTH, scale), center),
		Pt(scale*SCREENWIDTH/2, scale*SCREENHEIGHT/2));

	/* the row image, y-axis gets scaled with repl flag */
	rowimg = allocimage(display, Rect(0, 0, scale*SCREENWIDTH, 1), RGB24, scale > 1, DNofill);
	if(rowimg == nil)
		sysfatal("allocimage: %r");

	s = screens[0];
	for(y = 0; y < SCREENHEIGHT; y++){
		d = buf;
		e = s + SCREENWIDTH;
		for(; s < e; s++){
			m = &cmap[*s * 3];
			switch(scale){
			case 4:
				*d++ = m[2];
				*d++ = m[1];
				*d++ = m[0];
			case 3:
				*d++ = m[2];
				*d++ = m[1];
				*d++ = m[0];
			case 2:
				*d++ = m[2];
				*d++ = m[1];
				*d++ = m[0];
			case 1:
				*d++ = m[2];
				*d++ = m[1];
				*d++ = m[0];
			}
		}
		loadimage(rowimg, rowimg->r, buf, d - buf);
		draw(screen, r, rowimg, nil, ZP);
		r.min.y += scale;
		r.max.y += scale;
	}
	freeimage(rowimg);

	flushimage(display, 1);
}

void I_MouseEnable(int on)
{
	static char nocurs[2*4+2*2*16];
	static int fd = -1;

	if(mouseactive == on)
		return;
	if(mouseactive = on){
		if((fd = open("/dev/cursor", ORDWR|OCEXEC)) < 0)
			return;
		write(fd, nocurs, sizeof(nocurs));
	}else if(fd >= 0) {
		close(fd);
		fd = -1;
	}
}

void I_ReadScreen(byte *scr)
{
	memcpy (scr, screens[0], SCREENWIDTH*SCREENHEIGHT);
}

void I_BeginRead(void)
{
	I_Error("PORTME i_video.c I_BeginRead");
}

void I_EndRead(void)
{
	I_Error("PORTME i_video.c I_EndRead");
}

void I_StartTic(void)
{
}

void I_WaitVBL(int)
{
}


static int
runetokey(Rune r)
{
	switch(r){
	case Kleft:
		return KEY_LEFTARROW;
	case Kright:
		return KEY_RIGHTARROW;
	case Kup:
		return KEY_UPARROW;
	case Kdown:
		return KEY_DOWNARROW;

	case Kshift:
		return KEY_RSHIFT;
	case Kctl:
		return KEY_RCTRL;
	case Kalt:
		return KEY_LALT;
	case Kaltgr:
		return KEY_RALT;

	case Kbs:
		return KEY_BACKSPACE;
	case '\n':
		return KEY_ENTER;
	case Kprint:
		return KEY_PAUSE;

	case KF|1:
	case KF|2:
	case KF|3:
	case KF|4:
	case KF|5:
	case KF|6:
	case KF|7:
	case KF|8:
	case KF|9:
	case KF|10:
	case KF|11:
	case KF|12:
		return KEY_F1+(r-(KF|1));

	default:
		if(r < 0x80)
			return r;
	}
	return 0;
}

static void
kbdproc(void)
{
	char buf[128], buf2[128], *s;
	int kfd, n;
	Rune r;
	event_t e;

	if((kfd = open("/dev/kbd", OREAD)) < 0)
		sysfatal("can't open kbd: %r");

	buf2[0] = 0;
	buf2[1] = 0;
	buf[0] = 0;
	for(;;){
		if(buf[0] != 0){
			n = strlen(buf)+1;
			memmove(buf, buf+n, sizeof(buf)-n);
		}
		if(buf[0] == 0){
			n = read(kfd, buf, sizeof(buf)-1);
			if(n <= 0)
				break;
			buf[n-1] = 0;
			buf[n] = 0;
		}

		e.data1 = -1;
		e.data2 = -1;
		e.data3 = -1;

		switch(buf[0]){
		case 'c':
			chartorune(&r, buf+1);
			if(r){
				e.data1 = r;
				e.type = ev_char;
				D_PostEvent(&e);
			}
			/* no break */
		default:
			continue;
		case 'k':
			s = buf+1;
			while(*s){
				s += chartorune(&r, s);
				if(utfrune(buf2+1, r) == nil){
					if(e.data1 = runetokey(r)){
						e.type = ev_keydown;
						D_PostEvent(&e);
					}
				}
			}
			break;
		case 'K':
			s = buf2+1;
			while(*s){
				s += chartorune(&r, s);
				if(utfrune(buf+1, r) == nil){
					if(e.data1 = runetokey(r)){
						e.type = ev_keyup;
						D_PostEvent(&e);
					}
				}
			}
			break;
		}
		strcpy(buf2, buf);
	}
}

static void
mouseproc(void)
{
	int fd, n, nerr;
	Mouse m, om;
	char buf[1+5*12];
	event_t e;

	if((fd = open("/dev/mouse", ORDWR)) < 0)
		sysfatal("can't open mouse: %r");

	memset(&m, 0, sizeof m);
	memset(&om, 0, sizeof om);
	nerr = 0;
	for(;;){
		n = read(fd, buf, sizeof buf);
		if(n != 1+4*12){
			fprint(2, "mouse: bad count %d not 49: %r\n", n);
			if(n<0 || ++nerr>10)
				break;
			continue;
		}
		nerr = 0;
		switch(buf[0]){
		case 'r':
			resized = 1;
			/* fall through */
		case 'm':
			if(!mouseactive)
				break;

			m.xy.x = atoi(buf+1+0*12);
			m.xy.y = atoi(buf+1+1*12);
			m.buttons = atoi(buf+1+2*12);
			m.msec = atoi(buf+1+3*12);

			if(!ptinrect(m.xy, grabout)){
				fprint(fd, "m%d %d", center.x, center.y);

				m.xy = center;
				om.xy = center;
			}
			
			e.type = ev_mouse;
			e.data1 = m.buttons;
			e.data2 = 10*(m.xy.x - om.xy.x);
			e.data3 = 10*(om.xy.y - m.xy.y);
			D_PostEvent(&e);
			om = m;

			break;
		}
	}
}

