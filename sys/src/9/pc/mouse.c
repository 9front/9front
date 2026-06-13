#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "io.h"

#define	Image	IMAGE
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include "screen.h"

extern Memimage* gscreen;

/*
 *  mouse types
 */
enum
{
	Mouseother=	0,
	Mouseserial=	1,
	MousePS2=	2,
};

/*
 * bytes which correspond
 * to mouse buttons
 */
enum
{
	Mousereleas= 0x8, 
	Mouseleft= 0x9, 
	Mousemid= 0xc, 
	Mouseright= 0xa
};

static QLock mousectlqlock;
static int mousetype;
static int intellimouse;
static int packetsize;
static int resolution;
static int accelerated;
static int mousehwaccel;
static int synaptic;
static volatile int disabletouch;
static char mouseport[5];

enum
{
	CMaccelerated,
	CMhwaccel,
	CMintellimouse,
	CMlinear,
	CMps2,
	CMps2intellimouse,
	CMres,
	CMreset,
	CMserial,
	CMtouchpad,
	CMsynaptic,
};

static Cmdtab mousectlmsg[] =
{
	CMaccelerated,		"accelerated",		0,
	CMhwaccel,		"hwaccel",		2,
	CMintellimouse,		"intellimouse",		1,
	CMlinear,		"linear",		1,
	CMps2,			"ps2",			1,
	CMps2intellimouse,	"ps2intellimouse",	1,
	CMres,			"res",			0,
	CMreset,		"reset",		1,
	CMserial,		"serial",		0,
	CMtouchpad,		"touchpad",		2,
	CMsynaptic,		"synaptic",		1,
};

/*
 *  ps/2 mouse message is three bytes
 *
 *	byte 0 -	0 0 SDY SDX 1 M R L
 *	byte 1 -	DX
 *	byte 2 -	DY
 *
 *  shift & right button is the same as middle button
 *
 * Intellimouse and AccuPoint with extra buttons deliver
 *	byte 3 -	00 or 01 or FF according to extra button state.
 * extra buttons are mapped in this code to buttons 4 and 5.
 * AccuPoint generates repeated events for these buttons;
*  it and Intellimouse generate 'down' events only, so
 * user-level code is required to generate button 'up' events
 * if they are needed by the application.
 * Also on laptops with AccuPoint AND external mouse, the
 * controller may deliver 3 or 4 bytes according to the type
 * of the external mouse; code must adapt.
 *
 * On the NEC Versa series (and perhaps others?) we seem to
 * lose a byte from the packet every once in a while, which
 * means we lose where we are in the instruction stream.
 * To resynchronize, if we get a byte more than two seconds
 * after the previous byte, we assume it's the first in a packet.
 */
static void
ps2mouseputc(int c, int shift)
{
	static short msg[4];
	static int nb;
	static uchar b[] = {0, 1, 4, 5, 2, 3, 6, 7, 0, 1, 2, 3, 2, 3, 6, 7 };
	int buttons, dx, dy;

	/*
	 * Resynchronize in stream with timing; see comment above.
	 */
	if(lastmousems() > 2000)
		nb = 0;

	/*
	 *  check byte 0 for consistency
	 */
	if(nb==0 && (c&0xc8)!=0x08){
		if(intellimouse && (c==0x00 || c==0x01 || c==0xFF)){
			/* last byte of 4-byte packet */
			packetsize = 4;
		}
		return;
	}

	msg[nb] = c;
	if(++nb >= packetsize){
		nb = 0;
		if(msg[0] & 0x10)
			msg[1] |= 0xFF00;
		if(msg[0] & 0x20)
			msg[2] |= 0xFF00;

		buttons = b[(msg[0]&7) | (shift ? 8 : 0)];
		if(intellimouse && packetsize==4){
			if((msg[3]&0xc8) == 0x08){
				/* first byte of 3-byte packet */
				packetsize = 3;
				msg[0] = msg[3];
				nb = 1;
				/* fall through to emit previous packet */
			}else{
				/* The AccuPoint on the Toshiba 34[48]0CT
				 * encodes extra buttons as 4 and 5. They repeat
				 * and don't release, however, so user-level
				 * timing code is required. Furthermore,
				 * intellimice with 3buttons + scroll give a
				 * two's complement number in the lower 4 bits
				 * (bit 4 is sign extension) that describes
				 * the amount the scroll wheel has moved during
				 * the last sample. Here we use only the sign to
				 * decide whether the wheel is moving up or down
				 * and generate a single button 4 or 5 click
				 * accordingly.
				 */
				if((msg[3] >> 3) & 1)
					buttons |= 1<<3;
				else if(msg[3] & 0x7)
					buttons |= 1<<4;
			}
		}
		dx = msg[1];
		dy = -msg[2];
		mousetrack(dx, dy, buttons, TK2MS(MACHP(0)->ticks));
	}
}

static unsigned int
diffpos(int x1, int y1, int x2, int y2)
{
	unsigned int dx, dy, r;
	
	dx = abs(x1-x2);
	dy = abs(y1-y2);
	r = dx*dx + dy*dy;
	return r;
}

static void
synmouseputc(int c, int shift)
{
	static short msg[6];
	static int nb;
	static uchar b[] = {0, 1, 4, 5, 2, 3, 6, 7, 0, 1, 2, 3, 2, 3, 6, 7};
	static int prevx, prevy;
	static int maxx = 0;
	static int minx = 6143;
	static int maxy = 0;
	static int miny = 6143;
	static int edgminx = 1632;
	static int edgmaxx = 5312;
	static int edgminy = 1568;
	static int edgmaxy = 4288;
	static int ignore = 0;
	static int press;
	Memimage *i;
	int click[4];		
	int buttons, x, y, t, w, z, dx, dy;
	int deltah, deltal;
	int dp;

	deltah = 58;
	deltal = 3;
	click[0] = Mousereleas;
	click[1] = Mouseleft;
	click[2] = Mousemid;
	click[3] = Mouseright;
	
	/* re-synchronize with stream in case
	 * of packet loss
	 */
	if(lastmousems() > 500
	&& nb != 0)
		nb = 0;

	/* ignore 3-6 bytes
	 * of trailing idle packet
	 */
	if(nb == 2
	&& (c&0xFF) == 0x0
	&& msg[0] == 0x80)
		ignore = 4;

	if(ignore){
		--ignore;
		if(++nb == packetsize)
			nb = 0;
		return;
	}
		
	msg[nb] = c & 0xff;

	if(++nb < packetsize)
		return;

	nb = 0;

	w = (msg[3]&0x4) >> 2;
	w |= (msg[0]&0x4) >> 1;
	w |= (msg[0]&0x30) >> 2;
	
	z = msg[2];
	
	/* handle trackpoint */
	if(w == 3){
		/* handle buttons */
		if(msg[0] == 0x84
		&& msg[3] == 0xc4){
			switch(msg[1]){
			case Mousereleas:
				press = 0;
				break;
			case Mouseleft:
				press = 1;
				break;
			case Mousemid:
				press = 2;
				break;
			case Mouseright:
				press = 3;
			}
		}

		buttons = b[(msg[1]&7) | (shift ? 8 : 0)];
		if(msg[1] & 0x10)
			msg[4] |= 0xFF00;
		if(msg[1] & 0x20)
			msg[5] |= 0xFF00;
		dx = msg[4];
		dy = -msg[5];
		mousetrack(dx, dy, buttons, TK2MS(MACHP(0)->ticks));
		return;
	}
	
	/* palm-detect */
	if (w >= 6 || z > 110 || z < 40)
		return;

	if(w < 4
	&& w != 2)
		return;
	
	/* handle touchpad */
	if(disabletouch == 1)
		return;

	x = msg[4];
	x |= (msg[1]&0xF) << 8;
	if(msg[3]&0x10)
		x |= (1 << 12);

	y = msg[5];
	y |= (msg[1]&0xF0) << 4;
	if(msg[3]&0x20)
		y |= (1 << 12);
	
	/* compute edges */
	if(x > maxx)
		maxx = x;	
	if(y > maxy)
		maxy = y;
	if(x < minx)
		minx = x;	
	if(y < miny)
		miny = y;
	
	/* compute margins */
	edgminx = (minx*57)/50;
	edgmaxx = (maxx*121)/125;
	edgminy = (miny*57)/50;
	edgmaxy = (maxy*121)/125;
	
	if(x < edgminx
	|| x > edgmaxx)
		return;
	
	if(y < edgminy
	|| y > edgmaxy)
		return;
	
	if((i = gscreen) == nil)
		return;
	
	x = ((x-edgminx)*Dx(i->clipr))/(edgmaxx-edgminx);
	if(x < 0)
		x = 0;
	t = Dy(i->clipr)-(edgminy*-Dy(i->clipr))/(edgmaxy-edgminy);
	y = t + ((y*-Dy(i->clipr))/(edgmaxy-edgminy));
	if(y < 0)
		y = 0;

	/* ignore accidental taps */		
	dp = diffpos(x, y, prevx, prevy);
	if(dp > deltah*deltah
	|| dp < deltal*deltal){
		prevx = x;
		prevy = y;
		return;
	}
	
	/* imitate trackpoint buttons */
	buttons = b[(click[press]&7) | (shift ? 8 : 0)];
	mousetrack(x-prevx, y-prevy, buttons, TK2MS(MACHP(0)->ticks));
	prevx = x;
	prevy = y;
}

/*
 *  set up a ps2 mouse
 */
static void
ps2mouse(void)
{
	if(mousetype == MousePS2)
		return;

	mousetype = MousePS2;
	packetsize = 3;
	mousehwaccel = 0;

	i8042auxenable(ps2mouseputc);
	i8042auxcmd(0xEA);	/* set stream mode */
}

/*
 * The PS/2 Trackpoint multiplexor on the IBM Thinkpad T23 ignores
 * acceleration commands.  It is supposed to pass them on
 * to the attached device, but my Logitech mouse is simply
 * not behaving any differently.  For such devices, we allow
 * the user to use "hwaccel off" to tell us to back off to
 * software acceleration even if we're using the PS/2 port.
 * (Serial mice are always software accelerated.)
 * For more information on the Thinkpad multiplexor, see
 * http://wwwcssrv.almaden.ibm.com/trackpoint/
 */
static void
setaccelerated(int x)
{
	accelerated = x;
	if(mousehwaccel){
		switch(mousetype){
		case MousePS2:
			i8042auxcmd(0xE7);
			return;
		}
	}
	mouseaccelerate(x);
}

static void
setlinear(void)
{
	accelerated = 0;
	if(mousehwaccel){
		switch(mousetype){
		case MousePS2:
			i8042auxcmd(0xE6);
			return;
		}
	}
	mouseaccelerate(0);
}

static void
setres(int n)
{
	resolution = n;
	switch(mousetype){
	case MousePS2:
		i8042auxcmd(0xE8);
		i8042auxcmd(n);
		break;
	}
}

static void
setintellimouse(void)
{
	intellimouse = 1;
	packetsize = 4;
	switch(mousetype){
	case MousePS2:
		i8042auxcmd(0xF3);	/* set sample */
		i8042auxcmd(0xC8);
		i8042auxcmd(0xF3);	/* set sample */
		i8042auxcmd(0x64);
		i8042auxcmd(0xF3);	/* set sample */
		i8042auxcmd(0x50);
		break;
	case Mouseserial:
		uartsetmouseputc(mouseport, m5mouseputc);
		break;
	}
}

static void
resetmouse(void)
{
	packetsize = 3;
	switch(mousetype){
	case MousePS2:
		i8042auxcmd(0xF6);
		i8042auxcmd(0xEA);	/* streaming */
		i8042auxcmd(0xE8);	/* set resolution */
		i8042auxcmd(3);
		break;
	}
}

static void
setstream(int on)
{
	int i;

	switch(mousetype){
	case MousePS2:
		/*
		 * disabling streaming can fail when
		 * a packet is currently transmitted.
		 */
		for(i=0; i<4; i++){
			if(i8042auxcmd(on ? 0xF4 : 0xF5) != -1)
				break;
			tsleep(&up->sleep, return0, 0, 50);
		}
		break;
	}
}

static void
disstream(void)
{
	for(int i=0; i<4; i++){
		if(i8042auxcmd(0xF5) != -1)
			break;
		tsleep(&up->sleep, return0, 0, 50);
	}
}

static void
setabs(void)
{
	disstream();

	i8042auxcmd(0xE8);
	i8042auxcmd(2);
	i8042auxcmd(0xE8);
	i8042auxcmd(0);
	i8042auxcmd(0xE8);
	i8042auxcmd(0);
	i8042auxcmd(0xE8);
	i8042auxcmd(1);
	i8042auxcmd(0xF3);
	i8042auxcmd(0x14);
}

void
mousectl(Cmdbuf *cb)
{
	Cmdtab *ct;

	qlock(&mousectlqlock);
	if(waserror()){
		qunlock(&mousectlqlock);
		nexterror();
	}

	ct = lookupcmd(cb, mousectlmsg, nelem(mousectlmsg));
	switch(ct->index){
	case CMaccelerated:
		setstream(0);
		setaccelerated(cb->nf == 1 ? 1 : atoi(cb->f[1]));
		setstream(1);
		break;
	case CMintellimouse:
		setstream(0);
		setintellimouse();
		setstream(1);
		break;
	case CMlinear:
		setstream(0);
		setlinear();
		setstream(1);
		break;
	case CMps2:
		intellimouse = 0;
		ps2mouse();
		setstream(1);
		break;
	case CMsynaptic:
		intellimouse = 0;
		setabs();
		synaptic = 1;
		packetsize = 6;
		mousehwaccel = 0;
		i8042auxenable(synmouseputc);
		i8042auxcmd(0xF3);
		i8042auxcmd(0xC8);
		i8042auxcmd(0xF3);
		i8042auxcmd(0x64);
		i8042auxcmd(0xF3);
		i8042auxcmd(0x50);
		i8042auxcmd(0xF4);
		break;
	case CMps2intellimouse:
		ps2mouse();
		setintellimouse();
		setstream(1);
		break;
	case CMres:
		setstream(0);
		if(cb->nf >= 2)
			setres(atoi(cb->f[1]));
		else
			setres(1);
		setstream(1);
		break;
	case CMreset:
		resetmouse();
		if(accelerated)
			setaccelerated(accelerated);
		if(resolution)
			setres(resolution);
		if(intellimouse)
			setintellimouse();
		setstream(1);
		break;
	case CMserial:
		if(mousetype == Mouseserial)
			error(Emouseset);

		if(cb->nf > 2){
			if(strcmp(cb->f[2], "M") == 0)
				uartmouse(cb->f[1], m3mouseputc, 0);
			else if(strcmp(cb->f[2], "MI") == 0)
				uartmouse(cb->f[1], m5mouseputc, 0);
			else
				uartmouse(cb->f[1], mouseputc, cb->nf == 1);
		} else
			uartmouse(cb->f[1], mouseputc, cb->nf == 1);

		mousetype = Mouseserial;
		strncpy(mouseport, cb->f[1], sizeof(mouseport)-1);
		mouseport[sizeof(mouseport)-1] = 0;
		packetsize = 3;
		break;
	case CMhwaccel:
		if(strcmp(cb->f[1], "on")==0)
			mousehwaccel = 1;
		else if(strcmp(cb->f[1], "off")==0)
			mousehwaccel = 0;
		else
			cmderror(cb, "bad mouse control message");
		break;
	case CMtouchpad:
		if(strcmp(cb->f[1], "off")==0){
			if(synaptic==1)
				disabletouch = 1;
		}else if(strcmp(cb->f[1], "on")==0){
			if(synaptic==1)
				disabletouch = 0;
		}else
			cmderror(cb, "bad mouse control message");
		break;
	}

	qunlock(&mousectlqlock);
	poperror();
}
