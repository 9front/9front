#include <u.h>
#include <libc.h>

typedef struct Tap Tap;
typedef struct Dap Dap;

struct Tap
{
	int	off;
	int	len;
	int	delay;

	u32int	id;
	u32int	dapsel;
};

struct Dap
{
	Tap	*tap;

	uint	port;
	u32int	id;
};

int	dfd = -1;
int	lastbit = -1;

int	irlen;

int	ntaps;
Tap*	taps;

int	ndaps;
Dap*	daps;

Dap*	ahbap;
Dap*	apbap;

/* MPSSE command bits */
enum {
	FEW		=	1<<0,	/* -ve CLK on write */
	BITS		=	1<<1,	/* bits or bytes */
	FER		=	1<<2,	/* -ve CLK on read */
	LSB		=	1<<3,	/* LSB first = 1 else MSB first */
	TDI		=	1<<4,	/* do write TDI */
	TDO		=	1<<5,	/* do read TDO */
	TMS		=	1<<6,	/* do write TMS */
};

void
ioinit(char *dev)
{
	uchar b[3];

	dfd = open(dev, ORDWR);
	if(dfd < 0)
		sysfatal("open: %r");

	b[0] = 0x80;
	b[1] = 0x08;
	b[2] = 0x0B;
	write(dfd, b, 3);
}

void
io(int cmd, int len, uchar *dat)
{
	uchar buf[64];
	uchar *p = buf;

	*p++ = cmd;
	*p++ = len-1;
	if((cmd & BITS) != 0)
		len = 1;
	else
		*p++ = (len-1)>>8;
	if((cmd & (TDI|TMS)) != 0){
		memmove(p, dat, len);
		p += len;
	}
	if(write(dfd, buf, p - buf) != (p - buf))
		sysfatal("io write: %r");
	if((cmd & TDO) != 0)
		if(readn(dfd, dat, len) != len)
			sysfatal("io read: %r");
}

void
dstate(u32int s, int len)
{
	uchar b[1];

	assert(len < 8);
	b[0] = s;
	if(lastbit != -1){
		b[0] |= lastbit << 7;
		lastbit = -1;
	}
	io(TMS|LSB|BITS|FEW, len, b);
}
uvlong
dshift(uvlong w, int len)
{
	uchar b[8];
	int c, s, n;

	c = TDI|LSB|FEW;
	if(len < 0){
		len = -len;
		c |= TDO;
	}
	s = 0;
	n = len/8;
	if(n > 0) {
		switch(n){
		case 8:	b[7] = w >> 56;
		case 7:	b[6] = w >> 48;
		case 6:	b[5] = w >> 40;
		case 5:	b[4] = w >> 32;
		case 4:	b[3] = w >> 24;
		case 3:	b[2] = w >> 16;
		case 2:	b[1] = w >> 8;
		case 1:	b[0] = w >> 0;
		}
		io(c, n, b);
		s = n*8;
		if((c & TDO) != 0){
			w &= ~((1ULL<<s)-1);
			switch(n){
			case 8:	w |= (uvlong)b[7] << 56;
			case 7:	w |= (uvlong)b[6] << 48;
			case 6:	w |= (uvlong)b[5] << 40;
			case 5:	w |= (uvlong)b[4] << 32;
			case 4:	w |= (uvlong)b[3] << 24;
			case 3:	w |= (uvlong)b[2] << 16;
			case 2:	w |= (uvlong)b[1] << 8;
			case 1:	w |= (uvlong)b[0] << 0;
			}
		}
		len -= s;
	}
	if(len > 0){
		b[0] = w >> s;
		c |= BITS;
		io(c, len, b);
		if((c & TDO) != 0){
			w &= ~((uvlong)((1<<len)-1) << s);
			w |= (uvlong)(b[0] >> 8-len) << s;
		}
		s += len;
	}
	return w & (1ULL<<s)-1;
}
void
dshiftones(int len)
{
	while(len >= 64){
		dshift(~0ULL, 64);
		len -= 64;
	}
	dshift(~0ULL, len);
}
int
dshiftdelay(void)
{
	int i;

	/* send ones */
	dshiftones(512);
	for(i=0; i<512; i++){
		if(dshift(i != 0, -1) == 0)
			return i;
	}
	return 0;
}

void
irw(Tap *tap, uvlong w)
{
	/* 0011 -> Shift-IR */
	dstate(0x3, 4);

	dshiftones(tap->off);
	if((tap->off + tap->len) == irlen){
		dshift(w, tap->len-1);
		lastbit = w >> (tap->len-1);
	} else {
		dshift(w, tap->len);
		dshiftones(irlen - (tap->off + tap->len-1));
		lastbit = 1;
	}

	/* 011 -> Idle */
	dstate(0x3, 3);
}
uvlong
drr(Tap *tap, int len)
{
	uvlong w, d;

	/* 001 -> Shift-DR */
	dstate(0x1, 3);

	d = dshift(0, -tap->delay);
	w = dshift(0, -len);
	dshift(d, tap->delay);
	dshift(w, len-1);
	lastbit = (w >> len-1) & 1;

	/* 011 -> Idle */
	dstate(0x3, 3);

	return w;
}
void
drw(Tap *tap, uvlong w, int len)
{
	/* 001 -> Shift-DR */
	dstate(0x1, 3);

	dshift(0, tap->delay);
	dshift(w, len-1);
	lastbit = (w >> len-1) & 1;

	/* 011 -> Idle */
	dstate(0x3, 3);
}

enum {
	ABORT	= 0x8,
	DPACC	= 0xA,
	APACC	= 0xB,
		CTRLSTAT	= 0x4,
		SELECT		= 0x8,
		RDBUF		= 0xC,
};

u32int
dapr(Dap *dap, uchar r, uchar a)
{
	uvlong w;

	irw(dap->tap, r);
	w = 1 | (a >> 1) & 0x6;
	drw(dap->tap, w, 35);
	do {
		w = drr(dap->tap, 35);
	} while((w & 7) == 1);
	return w >> 3;
}
void
dapw(Dap *dap, uchar r, uchar a, u32int v)
{
	uvlong w;

	irw(dap->tap, r);
	w = (a >> 1) & 0x6;
	w |= (uvlong)v << 3;
	drw(dap->tap, w, 35);
}

void
app(Dap *dap)
{
	enum {
	CSYSPWRUPACK	= 1<<31,
	CSYSPWRUPREQ	= 1<<30,
	CDBGPWRUPACK	= 1<<29,
	CDBGPWRUPREQ	= 1<<28,
	CDBGRSTACK	= 1<<27,
	CDBGRSTREQ	= 1<<26,
	};
	u32int s;

	for(;;){
		s = dapr(dap, DPACC, CTRLSTAT);
		if((s & (CDBGPWRUPACK|CSYSPWRUPACK)) == (CDBGPWRUPACK|CSYSPWRUPACK))
			break;
		s |= CSYSPWRUPREQ|CDBGPWRUPREQ;
		dapw(dap, DPACC, CTRLSTAT, s);
	}
}
void
apa(Dap *dap, uchar a)
{
	u32int s;

	s = dap->port<<24 | a&0xf0;
	if(s != dap->tap->dapsel){
		dap->tap->dapsel = s;
		dapw(dap, DPACC, SELECT, s);
		app(dap);
	}
}
u32int
apr(Dap *dap, uchar a)
{
	apa(dap, a);
	return dapr(dap, APACC, a&0xC);
}
void
apw(Dap *dap, uchar a, u32int v)
{
	apa(dap, a);
	dapw(dap, APACC, a&0xC, v);
}
u32int
mmr(Dap *ap, u32int addr)
{
	apw(ap, 0x4, addr);
	return apr(ap, 0xC);
}
void
mmw(Dap *ap, u32int addr, u32int val)
{
	apw(ap, 0x4, addr);
	apw(ap, 0xC, val);
}

void
tapreset(void)
{
	int i, j, o;

	dstate(0x1F, 6);	/* 011111 -> Reset->Idle */
	dstate(0x3, 4);		/*   0011 -> Shift-IR */

	irlen = dshiftdelay();
	lastbit = 1;
	
	dstate(0x7, 5);		/*  00111 -> Shift-IR->Shift-DR */

	ntaps = dshiftdelay();

	dstate(0x1F, 6);	/* 011111 -> Reset->Idle */
	dstate(0x1, 3);		/*    001 -> Shift-DR */

	taps = realloc(taps, sizeof(taps[0]) * ntaps);

	o = 0;
	for(i=ntaps-1; i>=0; i--){
		taps[i].delay = ntaps - i - 1;
		taps[i].off = o;
		taps[i].id = dshift(0, -32);
		switch(taps[i].id){
		default:
			sysfatal("unknown tapid %.8ux\n", taps[i].id);
		case 0x03727093:
		case 0x0373b093:
		case 0x23727093:
			taps[i].len = 6;
			break;
		case 0x4ba00477:
			taps[i].len = 4;
			break;
		}
		o += taps[i].len;
	}

	dstate(0x1F, 6);	/* 011111 -> Reset->Idle */

	if(o != irlen)
		sysfatal("wrong tapchain irlen %d %d\n", o, irlen);

	ndaps = 0;
	for(i=0; i<ntaps; i++){
		fprint(2, "tap%d: id=%.8ux off=%d len=%d delay=%d\n",
			i, taps[i].id, taps[i].off, taps[i].len, taps[i].delay);

		switch(taps[i].id){
		case 0x4ba00477:
			o = 3;
			daps = realloc(daps, sizeof(daps[0]) * (ndaps+o));
			for(j=0; j<o; j++){
				daps[ndaps].tap = taps+i;
				daps[ndaps].port = j;
				daps[ndaps].id = apr(daps+ndaps, 0xFC);
				fprint(2, "\tdap%d: id=%.8ux\n", j, daps[ndaps].id);

				ndaps++;
			}
			break;
		}
	}

	for(i=0; i<ndaps; i++){
		switch(daps[i].id){
		case 0x44770001:
			ahbap = daps+i;
			break;
		case 0x24770002:
			apbap = daps+i;
			break;
		}
	}
}

enum {
	DBGDIDR		= 0x000,
	DBGDEVID	= 0xFC8,
	DBGDSCR		= 0x088,
		RXfull		= 1<<30,
		TXfull		= 1<<29,
		RXfull_1	= 1<<27,
		TXfull_1	= 1<<26,
		PipeAdv		= 1<<25,
		InstrCompl_1	= 1<<24,
		ExtDCCmodeShift	= 20,
		ExtDCCmodeMask	= 3<<ExtDCCmodeShift,
		ADAdiscard	= 1<<19,
		NS		= 1<<18,
		SPNIDdis	= 1<<17,
		SPIDdis		= 1<<16,
		MDBGen		= 1<<15,
		HDBGen		= 1<<14,
		ITRen		= 1<<13,
		UDCCdis		= 1<<12,
		INTdis		= 1<<11,
		DBGack		= 1<<10,
		UND_1		= 1<<8,
		ADABORT_1	= 1<<7,
		SDABORT_1	= 1<<6,
		MOEShift	= 2,
		MOEMask		= 15<<MOEShift,
		RESTARTED	= 1<<1,
		HALTED		= 1<<0,

	DBGDRCR	= 0x90,
		RestartReq	= 1<<1,
		HaltReq		= 1<<0,

	DBGPRCR	= 0x310,

	DBGITR		= 0x084,	/* Instruction Transfer Register */
	DBGDTRRX	= 0x080,	/* Host to Target Data Transfer Register */
	DBGDTRTX	= 0x08C,	/* Target to Host Data Transfer Register */
};

typedef struct Arm Arm;
struct Arm
{
	u32int	dbgbase;

	Dap	*dbgap;
	Dap	*memap;

	char	*id;
};
Arm arm[2];
u32int
dbgr(Arm *arm, u32int reg)
{
	return mmr(arm->dbgap, arm->dbgbase+reg);
}
void
dbgw(Arm *arm, u32int reg, u32int val)
{
	mmw(arm->dbgap, arm->dbgbase+reg, val);
}
u32int
dbgrpoll(Arm *arm, u32int reg, u32int mask, u32int val)
{
	u32int w;

	for(;;){
		w = dbgr(arm, reg);
		if((w & mask) == val)
			break;
	}
	return w;
}

void
startstop(Arm *arm, int stop)
{
	u32int s;

	s = dbgr(arm, DBGDSCR);
	if((s & HALTED) != stop){
		if(!stop){
			s &= ~ITRen;
			dbgw(arm, DBGDSCR, s);
		}
		dbgw(arm, DBGDRCR, stop ? HaltReq : RestartReq);
		s = dbgrpoll(arm, DBGDSCR, HALTED, stop);
		if(stop){
			s |= ITRen;
			dbgw(arm, DBGDSCR, s);
		}
		fprint(2, "%s: startstop: %.8ux\n", arm->id, s);
	}
}

void
armxec(Arm *arm, u32int instr)
{
	dbgw(arm, DBGITR, instr);
	dbgrpoll(arm, DBGDSCR, InstrCompl_1, InstrCompl_1);
}

#define ARMV4_5_MRC(CP, op1, Rd, CRn, CRm, op2) \
	(0xee100010 | (CRm) | ((op2) << 5) | ((CP) << 8) \
	| ((Rd) << 12) | ((CRn) << 16) | ((op1) << 21))
#define ARMV4_5_MCR(CP, op1, Rd, CRn, CRm, op2) \
	(0xee000010 | (CRm) | ((op2) << 5) | ((CP) << 8) \
	| ((Rd) << 12) | ((CRn) << 16) | ((op1) << 21))

void
trrxw(Arm *arm, u32int val)
{
	dbgrpoll(arm, DBGDSCR, RXfull_1, 0);
	dbgw(arm, DBGDTRRX, val);
}
u32int
trtxr(Arm *arm)
{
	dbgrpoll(arm, DBGDSCR, TXfull_1, TXfull_1);
	return dbgr(arm, DBGDTRTX);
}

void
armrw(Arm *arm, int reg, u32int val);

u32int
armrr(Arm *arm, int rn)
{
	if(rn == 15){
		u32int r0;

		r0 = armrr(arm, 0);
		armxec(arm, 0xE1A0000F);
		armxec(arm, ARMV4_5_MCR(14, 0, 0, 0, 5, 0));
		armrw(arm, 0, r0);
	} else {
		armxec(arm, ARMV4_5_MCR(14, 0, rn, 0, 5, 0));
	}
	return trtxr(arm);
}
void
armrw(Arm *arm, int rn, u32int val)
{
	if(rn == 15){
		u32int r0;

		r0 = armrr(arm, 0);
		armrw(arm, 0, val);
		armxec(arm, 0xE1A0F000);
		armrw(arm, 0, r0);
	} else {
		trrxw(arm, val);
		armxec(arm, ARMV4_5_MRC(14, 0, rn, 0, 5, 0));
	}
}

/*
 * mww phys 0xf8000008 0xdf0d
 * mww phys 0xf8000910 0xf
 * load_image "/sys/src/boot/zynq/fsbl" 0xfffc0000 bin
 * reg pc 0xfffc0000
 */
void
boot(char *file, u32int entry)
{
	u32int *buf, *src;
	int fd, size;
	u32int dst;

	fprint(2, "load %s", file);
	if((fd = open(file, OREAD)) < 0)
		sysfatal("open: %r");

	size = seek(fd, 0, 2);
	fprint(2, " [%ud]", size);
	seek(fd, 0, 0);
	buf = malloc((size+3) & ~3);
	if(readn(fd, buf, size) != size)
		sysfatal("read: %r");
	close(fd);

	/* map ocm */
	mmw(arm->memap, 0xf8000008, 0xdf0d);
	mmw(arm->memap, 0xf8000910, 0xf);

	src = buf;
	for(dst = entry; size > 0; dst += 4, size -= 4){
		if((dst & 0xF) == 0)
			fprint(2, ".");
		mmw(arm->memap, dst, *src++);
	}
	free(buf);
	fprint(2, ".\nentry %.8ux\n", entry);

	armrw(arm, 15, entry);
}

void
usage(void)
{
	fprint(2, "%s [ -j jtagdev ] entry image\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	char *jtag = "/dev/jtagddd94.0";
	char *image;
	u32int entry;

	fmtinstall('H', encodefmt);

	ARGBEGIN {
	case 'j':
		jtag = EARGF(usage());
		break;
	default:
		usage();
	} ARGEND;

	if(argc != 2)
		usage();
	entry = strtoul(argv[0], nil, 0);
	image = argv[1];

	ioinit(jtag);
	tapreset();

	arm[0].dbgbase = 0x80090000;
	arm[0].dbgap = apbap;
	arm[0].memap = ahbap;
	arm[0].id = "arm0";

	arm[1].dbgbase = 0x80092000;
	arm[1].dbgap = apbap;
	arm[1].memap = ahbap;
	arm[1].id = "arm1";

	startstop(arm+0, 1);
	startstop(arm+1, 1);

	boot(image, entry);

	startstop(arm+0, 0);
	startstop(arm+1, 0);

	exits(nil);
}
