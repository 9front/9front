#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

enum
{
	Mhz = 1000*1000,
	Pwmsrcclk = 25*Mhz,

	Scharge = 0,
	Sovervolted,
	Scooldown,
	Sundervolted,
	Smissing,
	Sfullycharged,
	Spowersave,

	Psomoff = 1,

	Light = 1,
	Temp,
	Battery,
	Pmctl,

	PWMSAR = 0x0c/4,
	PWMPR = 0x10/4,

	TMUTMR = 0x00/4,
		TMR_ME = 1<<31,
		TMR_ALPF_SHIFT = 26,
		TMR_MSITE_SHIFT = 13,
	TMUTSR = 0x04/4,
		TSR_MIE = 1<<30,
		TSR_ORL = 1<<29,
		TSR_ORH = 1<<28,
	TMUTMTMIR = 0x08/4,
	TMUTIER = 0x20/4,
	TMUTIDR = 0x24/4,
		TIDR_MASK = 0xe0000000,
	TMUTISCR = 0x28/4,
	TMUTICSCR = 0x2c/4,
	TMUTTCFGR = 0x80/4,
	TMUTSCFGR = 0x84/4,
	TMUTRITSR0 = 0x100/4,
	TMUTRITSR1 = 0x110/4,
	TMUTRITSR2 = 0x120/4,
	TMUTTR0CR = 0xf10/4,
	TMUTTR1CR = 0xf14/4,
	TMUTTR2CR = 0xf18/4,
	TMUTTR3CR = 0xf1c/4,
		CR_CAL_PTR_SHIFT = 16,

	SPIx_RXDATA = 0x00/4,
	SPIx_TXDATA = 0x04/4,
	SPIx_CONREG = 0x08/4,
		CON_BURST_LENGTH = 1<<20,
		CON_PRE_DIVIDER = 1<<12,
		CON_POST_DIVIDER = 1<<8,
		CON_CHAN_MASTER = 1<<4,
		CON_XCH = 1<<2,
		CON_EN = 1<<0,
	SPIx_CONFIGREG = 0x0c/4,
		CONFIG_SCLK_CTL_LOW = 0<<20,
		CONFIG_DATA_CTL_HIGH = 0<<16,
		CONFIG_SS_POL_LOW = 0<<12,
		CONFIG_SS_CTL_NCSS = 1<<8,
		CONFIG_SCLK_POL_HIGH = 0<<4,
		CONFIG_SCLK_PHA_1 = 1<<0,
	SPIx_STATREG = 0x18/4,
		STAT_RR = 1<<3,
};

static char lpcfw[9*3+1];
static u32int *pwm2, *tmu, *spi2;
static char *uid = "pm";

static void
wr(u32int *base, int reg, u32int v)
{
	//fprint(2, "[0%x] ← 0x%ux\n", reg*4, v);
	if(base != nil)
		base[reg] = v;
}

static u32int
rd(u32int *base, int reg)
{
	return base != nil ? base[reg] : -1;
}

static void
setlight(int p)
{
	u32int v;

	if(p < 0)
		p = 0;
	if(p > 100)
		p = 100;

	v = Pwmsrcclk / rd(pwm2, PWMSAR);
	wr(pwm2, PWMPR, (Pwmsrcclk/(v*p/100))-2);
}

static int
getlight(void)
{
	u32int m, v;

	m = Pwmsrcclk / rd(pwm2, PWMSAR);
	v = Pwmsrcclk / (rd(pwm2, PWMPR)+2);
	return v*100/m;
}

static int
gettemp(int c[3])
{
	int i, r[] = {TMUTRITSR0, TMUTRITSR1, TMUTRITSR2};
	u32int s;

	s = rd(tmu, TMUTSR);
	if(s & TSR_MIE){
		werrstr("monitoring interval exceeded");
		return -1;
	}
	if(s & (TSR_ORL|TSR_ORH)){
		werrstr("out of range");
		return -1;
	}

	c[0] = c[1] = c[2] = 0;
	for(;;){
		for(i = 0; i < 3; i++)
			if(c[i] >= 0)
				c[i] = rd(tmu, r[i]);
		if(c[0] < 0 && c[1] < 0 && c[2] < 0)
			break;
		sleep(10);
	}
	c[0] &= 0xff;
	c[1] &= 0xff;
	c[2] &= 0xff;
	return 0;
}

static void
tmuinit(void)
{
	/* without proper calibration data sensing is useless */
	static u8int cfg[4][12] = {
		{0x23, 0x29, 0x2f, 0x35, 0x3d, 0x43, 0x4b, 0x51, 0x57, 0x5f, 0x67, 0x6f},
		{0x1b, 0x23, 0x2b, 0x33, 0x3b, 0x43, 0x4b, 0x55, 0x5d, 0x67, 0x70, 0},
		{0x17, 0x23, 0x2d, 0x37, 0x41, 0x4b, 0x57, 0x63, 0x6f, 0},
		{0x15, 0x21, 0x2d, 0x39, 0x45, 0x53, 0x5f, 0x71, 0},
	};
	int i, j;

	wr(tmu, TMUTMR, 0); /* disable */
	wr(tmu, TMUTIER, 0); /* disable all interrupts */
	wr(tmu, TMUTMTMIR, 0xf); /* no monitoring interval */

	/* configure default ranges */
	wr(tmu, TMUTTR0CR, 11<<CR_CAL_PTR_SHIFT | 0);
	wr(tmu, TMUTTR1CR, 10<<CR_CAL_PTR_SHIFT | 38);
	wr(tmu, TMUTTR2CR, 8<<CR_CAL_PTR_SHIFT | 72);
	wr(tmu, TMUTTR3CR, 7<<CR_CAL_PTR_SHIFT | 97);

	/* calibration data */
	for(i = 0; i < 4; i++){
		for(j = 0; j < 12 && cfg[i][j] != 0; j++){
			wr(tmu, TMUTTCFGR, i<<16|j);
			wr(tmu, TMUTSCFGR, cfg[i][j]);
		}
	}

	/* enable: all sites, ALPF 11=0.125 */
	wr(tmu, TMUTMR, TMR_ME | 3<<TMR_ALPF_SHIFT | 7<<TMR_MSITE_SHIFT);
}

static void
lpccall(char cmd, u8int arg, void *ret)
{
	u32int con;
	int i, try;

	/* in case someone messed up and ran pm more than once */
	try = 0;
	while((rd(spi2, SPIx_CONREG) & CON_EN) != 0 && try++ < 50)
		sleep(10);

	con =
		/* 8 bits burst */
		CON_BURST_LENGTH*(8-1) |
		/* clk=25Mhz, pre=1, post=2⁶ → 25Mhz/1/2⁶ ≲ 400kHz */
		CON_PRE_DIVIDER*(1-1) | CON_POST_DIVIDER*6 |
		/* master mode on channel 0 */
		CON_CHAN_MASTER<<0 |
		/* enable */
		CON_EN;
	wr(spi2, SPIx_CONREG, con);

	wr(spi2, SPIx_CONFIGREG,
		/* defaults */
		CONFIG_SCLK_CTL_LOW |
		CONFIG_DATA_CTL_HIGH |
		CONFIG_SS_POL_LOW |
		CONFIG_SCLK_POL_HIGH |
		/* tx shift - rising edge SCLK; tx latch - falling edge */
		CONFIG_SCLK_PHA_1 |
		CONFIG_SS_CTL_NCSS);

	wr(spi2, SPIx_TXDATA, 0xb5);
	wr(spi2, SPIx_TXDATA, cmd);
	wr(spi2, SPIx_TXDATA, arg);
	wr(spi2, SPIx_CONREG, con | CON_XCH);

	/*
	 * give enough time to send and for LPC to process
	 * 50ms seems safe but add more just in case
	 */
	sleep(75);
	/* LPC buffers 3 bytes without responding, ignore */
	for(i = 0; i < 3; i++)
		rd(spi2, SPIx_RXDATA);

	/*
	 * at this point LPC hopefully is blocked waiting for
	 * chip select to go active
	 */

	/* expecting 8 bytes, start the exchange */
	for(i = 0; i < 8; i++)
		wr(spi2, SPIx_TXDATA, 0);
	wr(spi2, SPIx_CONREG, con | CON_XCH);

	for(i = 0; i < 8; i++){
		try = 0;
		do{
			sleep(10);
		}while((rd(spi2, SPIx_STATREG) & STAT_RR) == 0 && try++ < 50);
		if(try >= 50) /* give up */
			break;
		((u8int*)ret)[i] = rd(spi2, SPIx_RXDATA);
	}

	wr(spi2, SPIx_CONREG, con & ~CON_EN);
}

static void
lpcinit(void)
{
	char s[3][8];

	lpccall(0, 0, s[0]); /* a dummy one to make sure there is no garbage */
	lpccall('f', 0, s[0]);
	lpccall('f', 1, s[1]);
	lpccall('f', 2, s[2]);
	snprint(lpcfw, sizeof(lpcfw), "%.*s %.*s %.*s", 8, s[0], 8, s[1], 8, s[2]);
}

static char *
readbattery(char *s, char *e)
{
	int hh, mm, ss, full, remain, warn, safe;
	u8int st[8], ch[8];
	s16int current;
	char *state;

	lpccall('c', 0, ch);
	lpccall('q', 0, st);
	current = (s16int)(st[2] | st[3]<<8);
	remain = ch[0]|ch[1]<<8;
	warn = ch[2]|ch[3]<<8;
	full = ch[4]|ch[5]<<8;
	safe = remain - warn;

	hh = mm = ss = 0;
	state = "unknown";
	switch(st[5]){
	case Sfullycharged: state = "full"; break;
	case Sovervolted: state = "balancing"; break;
	case Scooldown: state = "cooldown"; break;
	case Spowersave: state = "powersave"; break;
	case Scharge:
		if(current < 0)
			state = "charging";
		if(current > 0)
			state = "discharging";
		if(current != 0){
			ss = (current < 0 ? full - safe : safe) * 3600 / abs(current);
			hh = ss/3600;
			ss -= 3600*(ss/3600);
			mm = ss/60;
			ss -= 60*(ss/60);
		}
		break;
	case Smissing:
		werrstr("battery is missing");
		return nil;
	}

	return seprint(s, e, "%d mA %d %d %d %d ? mV %d ? %02d:%02d:%02d %s\n",
		st[4],
		remain, full, full, warn,
		st[0]|st[1]<<8,
		hh, mm, ss,
		state
	);
}

static char *
readcells(char *s, char *e)
{
	u8int v[16];
	int i;

	lpccall('v', 0, v+0);
	lpccall('v', 1, v+8);
	s = seprint(s, e, "cells(mV)");
	for(i = 0; i < 16; i += 2)
		s = seprint(s, e, " %d", v[i] | v[i+1]<<8);
	return seprint(s, e, "\n");
}

static char *
readcurrent(char *s, char *e)
{
	u8int q[8];

	lpccall('q', 0, q);
	return seprint(s, e, "current(mA) %d\n", (s16int)(q[2] | q[3]<<8));
}

static void
fsread(Req *r)
{
	char msg[256], *s, *e;
	void *aux;
	int c[3];

	msg[0] = 0;
	if(r->ifcall.offset == 0){
		aux = r->fid->file->aux;
		s = msg;
		e = s+sizeof(msg);
		if(aux == (void*)Light){
			snprint(msg, sizeof(msg), "lcd %d\n", getlight());
		}else if(aux == (void*)Temp){
			if(gettemp(c) != 0){
				responderror(r);
				return;
			}
			/* only the first one is CPU temperature */
			snprint(msg, sizeof(msg), "%d.0\n", c[0]);
		}else if(aux == (void*)Battery){
			if(readbattery(s, e) == nil){
				responderror(r);
				return;
			}
		}else if(aux == (void*)Pmctl){
			s = seprint(s, e, "version %s\n", lpcfw);
			s = readcells(s, e);
			s = readcurrent(s, e);
			USED(s);
		}
	}

	readstr(r, msg);
	respond(r, nil);
}

static void
fswrite(Req *r)
{
	char msg[256], *f[4];
	int nf, v, p;
	void *aux;

	snprint(msg, sizeof(msg), "%.*s",
		utfnlen((char*)r->ifcall.data, r->ifcall.count), (char*)r->ifcall.data);
	nf = tokenize(msg, f, nelem(f));
	aux = r->fid->file->aux;
	if(aux == (void*)Light){
		if(nf < 2){
Bad:
			respond(r, "invalid ctl message");
			return;
		}
		if(strcmp(f[0], "lcd") == 0){
			v = atoi(f[1]);
			if(*f[1] == '+' || *f[1] == '-')
				v += getlight();
			setlight(v);
		}
	}else if(aux == (void*)Pmctl){
		p = -1;
		if(nf == 2 && strcmp(f[0], "power") == 0 && strcmp(f[1], "off") == 0)
			p = Psomoff;
		if(p < 0)
			goto Bad;
		lpccall('p', p, msg);
	}

	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
}

static Srv fs = {
	.read = fsread,
	.write = fswrite,
};

static void
usage(void)
{
	fprint(2, "usage: %s [-D] [-m mountpoint] [-s service]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *mtpt, *srv;

	mtpt = "/dev";
	srv = nil;
	ARGBEGIN{
	case 'D':
		chatty9p = 1;
		break;
	case 'm':
		mtpt = EARGF(usage());
		break;
	case 's':
		srv = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	if((tmu = segattach(0, "tmu", 0, 0xf20)) == (void*)-1)
		sysfatal("no tmu");
	if((pwm2 = segattach(0, "pwm2", 0, 0x18)) == (void*)-1)
		sysfatal("no pwm2");
	if((spi2 = segattach(0, "ecspi2", 0, 0x20)) == (void*)-1)
		sysfatal("no spi2");
	tmuinit();
	lpcinit();
	fs.tree = alloctree(uid, uid, DMDIR|0555, nil);
	createfile(fs.tree->root, "battery", uid, 0444,(void*)Battery);
	createfile(fs.tree->root, "cputemp", uid, 0444, (void*)Temp);
	createfile(fs.tree->root, "light", uid, 0666, (void*)Light);
	createfile(fs.tree->root, "pmctl", uid, 0666, (void*)Pmctl);
	postmountsrv(&fs, srv, mtpt, MAFTER);

	exits(nil);
}
