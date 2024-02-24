#include <u.h>
#include <libc.h>
#include <thread.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

int gpiogame;
static int gpioen;
static int rtcclk;
static uchar rtcdata, rtcout;
static int rtccount;
static uchar pinstate[3] = { 1, 1, 1 };

enum {
	RCLK=1<<0,
	RSIO=1<<1,
	RCS=1<<2,

	PCLK=0,
	PSIO=1,
	PCS=2,
};

void
gpioident(void)
{
	char code[4];

	memcpy(code, rom+0xAC, 4);

	/* Pokemon E/S/R. RTC only */
	if(memcmp(code, "BPEE", 4) == 0
	|| memcmp(code, "AXPE", 4) == 0
	|| memcmp(code, "AXVE", 4) == 0)
		gpiogame = 1;
	
}

#define BCD(x) (x/10 * 16 + x%10)

static void
getdate(uchar out[7])
{
	static Tzone *zone;
	static Tm date;

	if(zone == nil)
		zone = tzload("local");
	tmnow(&date, zone);
	date.year -= 100;
	date.mon++;
	out[0] = BCD(date.year);
	out[1] = BCD(date.mon);
	out[2] = BCD(date.mday);
	out[3] = BCD(date.wday);
	out[4] = BCD(date.hour);
	out[5] = BCD(date.min);
	out[6] = BCD(date.sec);
}

// https://html.alldatasheet.com/html-pdf/80559/SII/S-3511/45/1/S-3511.html
static void
rtcstate(void)
{
	enum { CMD, STATUS, DATETIME };
	static int state = CMD;
	enum { WR=0, RD=1 };
	static int dir = WR;
	static int datetimepos = 0;
	static uchar date[7];

	uchar cmd;

	switch(state){
	case CMD:
		dir = rtcdata&1;
		if((rtcdata&0xF0) != 0x60){
			print("wrong rtc state: %X\n", rtcdata);
			return;
		}
		cmd = (rtcdata&0xF)>>1;
		switch(cmd){
		case 0: /* reset */
			break;
		case 1: /* status */
			if(dir == RD)
				rtcdata = 0b01000000;
			state = STATUS;
			break;
		case 2:	/* year → seconds */
		case 3: /* hour → seconds */
			getdate(date);
			datetimepos = (cmd-2)<<2;
			if(dir == RD)
				rtcdata = date[datetimepos];
			datetimepos++;
			state = DATETIME;
			break;
		default:
			print("unsupported rtc cmd: %d %d\n", dir, cmd);
			rtcdata = 0;
			break;
		}
		break;
	case STATUS:
		state = CMD;
		if(dir == WR)
			rtcdata = 0;
		break;
	case DATETIME:
		if(dir == RD)
			rtcdata = date[datetimepos];
		else
			rtcdata = 0;
		if(++datetimepos == 8){
			datetimepos = 0;
			state = CMD;
		}
		break;
	}
}

void
gpiowdata(u32int v)
{
	if((v&RCLK) == 0 && rtcclk == 1){
		if(pinstate[PSIO] == 1){
			rtcdata |= ((v&RSIO)>>1)<<(7-rtccount);
		} else {
			rtcout = rtcdata&1;
			rtcdata >>= 1;
		}
		rtccount++;
	}
	if(rtccount == 8){
		rtcstate();
		rtccount = 0;
	}
	rtcclk = v&RCLK;
}

u32int
gpiordata(void)
{
	return rtcout<<1;
}

void
gpiowdir(u32int v)
{
	pinstate[PCLK] = v&1;
	pinstate[PSIO] = (v&2)>>1;
	pinstate[PCS] = (v&4)>>2;
}

u32int
gpiordir(void)
{
	return (pinstate[PCS]<<2) | (pinstate[PSIO]<<1) | (u32int)pinstate[PCLK];
}

void
gpiowcontrol(u32int v)
{
	gpioen = v;
}

u32int
gpiorcontrol(void)
{
	return gpioen;
}
