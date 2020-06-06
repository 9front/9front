#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/audioif.h"

typedef struct Codec Codec;
typedef struct Ctlr Ctlr;
typedef struct Bld Bld;
typedef struct Ring Ring;
typedef struct Stream Stream;

typedef struct Id Id;
typedef struct Widget Widget;
typedef struct Codec Codec;
typedef struct Fungroup Fungroup;
typedef struct Pinprop Pinprop;

enum {
	Gcap = 0x00,
	Gctl = 0x08,
		Rst = 1,
		Flush = 2,
		Acc = 1<<8,
	Wakeen = 0x0c,
	Statests = 0x0e,
		Sdiwake = 1 | 2 | 4,
	Intctl = 0x20,
		Gie = 1<<31,
		Cie = 1<<30,
	Intsts = 0x24,
		Gis = 1<<31,
		Cis = 1<<30,
	Walclk = 0x30,
	Corblbase = 0x40,
	Corbubase = 0x44,
	Corbwp = 0x48,
	Corbrp = 0x4a,
		Corbptrrst = 1<<15,
	Corbctl = 0x4c,
		Corbdma = 2,
		Corbint = 1,
	Corbsts = 0x4d,
		Cmei = 1,
	Corbsz = 0x4e,
	Rirblbase = 0x50,
	Rirbubase = 0x54,
	Rirbwp = 0x58,
		Rirbptrrst = 1<<15,
	Rintcnt = 0x5a,
	Rirbctl = 0x5c,
		Rirbover = 4,
		Rirbdma = 2,
		Rirbint = 1,
	Rirbsts = 0x5d,
		Rirbrover = 4,
		Rirbrint = 1,
	Rirbsz = 0x5e,
	Immcmd = 0x60,
	Immresp = 0x64,
	Immstat = 0x68,
	Dplbase = 0x70,
	Dpubase = 0x74,
	/* Warning: Sdctl is 24bit register */
	Sdctl0 = 0x80,
		Srst = 1<<0,
		Srun = 1<<1,
		Scie = 1<<2,
		Seie = 1<<3,
		Sdie = 1<<4,
		Stagbit = 20,
	Sdsts = 0x03,
		Scompl = 1<<2,
		Sfifoerr = 1<<3,
		Sdescerr = 1<<4,
		Sfifordy = 1<<5,
	Sdlpib = 0x04,
	Sdcbl =  0x08,
	Sdlvi =  0x0c,
	Sdfifow = 0x0e,
	Sdfifos = 0x10,
	Sdfmt = 0x12,
		Fmtmono = 0,
		Fmtstereo = 1,
		Fmtsampw = 1<<4,
		Fmtsampb = 0<<4,
		Fmtdiv1 = 0<<8,
		Fmtmul1 = 0<<11,
		Fmtbase441 = 1<<14,
		Fmtbase48 = 0<<14,
	Sdbdplo = 0x18,
	Sdbdphi = 0x1c,
};

enum {
	Bufsize = 64 * 1024 * 4,
	Nblocks = 256,
	Blocksize = Bufsize / Nblocks,
	BytesPerSample = 4,

	Maxrirbwait = 1000, /* microseconds */
	Maxwaitup = 500, /* microseconds */
	Codecdelay = 1000, /* microseconds */
};

enum {
	/* 12-bit cmd + 8-bit payload */
	Getparm = 0xf00,
		Vendorid = 0x00,
		Revid = 0x02,
		Subnodecnt = 0x04,
		Fungrtype = 0x05,
			Graudio = 0x01,
			Grmodem = 0x02,
		Fungrcap = 0x08,
		Widgetcap = 0x09,
			Waout = 0,
			Wain = 1,
			Wamix = 2,
			Wasel = 3,
			Wpin = 4,
			Wpower = 5,
			Wknob = 6,
			Wbeep = 7,
			Winampcap = 0x0002,
			Woutampcap = 0x0004,
			Wampovrcap = 0x0008,
			Wfmtovrcap = 0x0010,
			Wstripecap = 0x0020,
			Wproccap = 0x0040,
			Wunsolcap = 0x0080,
			Wconncap = 0x0100,
			Wdigicap = 0x0200,
			Wpwrcap = 0x0400,
			Wlrcap = 0x0800,
			Wcpcap = 0x1000,			 
		Streamrate = 0x0a,
		Streamfmt = 0x0b,
		Pincap = 0x0c,
			Psense = 1<<0,
			Ptrigreq = 1<<1,
			Pdetect = 1<<2,
			Pheadphone = 1<<3,
			Pout = 1<<4,
			Pin = 1<<5,
			Pbalanced = 1<<6,
			Phdmi = 1<<7,
			Peapd = 1<<16,
		Inampcap = 0x0d,
		Outampcap = 0x12,
		Connlistlen = 0x0e,
		Powerstates = 0x0f,
		Processcap = 0x10,
		Gpiocount = 0x11,
		Knobcap = 0x13,
	Getconn = 0xf01,
	Setconn = 0x701,
	Getconnlist = 0xf02,
	Getstate = 0xf03,
	Setstate = 0x703,
	Setpower = 0x705,
	Getpower = 0xf05,
	Getstream = 0xf06,
	Setstream = 0x706,
	Getpinctl = 0xf07,
	Setpinctl = 0x707,
		Pinctlin = 1<<5,
		Pinctlout = 1<<6,
		Pinctlhphn = 1<<7,
	Getunsolresp = 0xf08,
	Setunsolresp = 0x708,
	Getpinsense = 0xf09,
	Exepinsense = 0x709,
	Getgpi = 0xf10,
	Setgpi = 0x710,
	Getbeep = 0xf0a,
	Setbeep = 0x70a,
	Seteapd = 0x70c,
		Btlenable = 1,
		Eapdenable = 2,
		LRswap = 4,
	Getknob = 0xf0f,
	Setknob = 0x70f,
	Getdefault = 0xf1c,
	Funreset = 0x7ff,
	Getchancnt = 0xf2d,
	Setchancnt = 0x72d,
	
	/* 4-bit cmd + 16-bit payload */
	Getcoef = 0xd,
	Setcoef = 0x5,
	Getproccoef = 0xc,
	Setproccoef = 0x4,
	Getamp = 0xb,
	Setamp = 0x3,
		Asetout = 1<<15,
		Asetin = 1<<14,
		Asetleft = 1<<13,
		Asetright = 1<<12,
		Asetmute = 1<<7,
		Asetidx = 8,
		Agetin = 0<<15,
		Agetout = 1<<15,
		Agetleft = 1<<13,
		Agetright = 1<<15,
		Agetidx = 0,
		Again = 0,
		Againmask = 0x7f,
	Getconvfmt = 0xa,
	Setconvfmt = 0x2,
};

enum {
	Maxcodecs = 16,
	Maxwidgets = 256,
};

struct Ring {
	Rendez r;

	uchar	*buf;
	ulong	nbuf;

	ulong	ri;
	ulong	wi;
};

struct Stream {
	Ring	ring;

	Bld	*blds;

	uint	sdctl;
	uint	sdintr;
	uint	sdnum;

	uint	afmt;
	uint	atag;
	int	active;

	uint	pin;
	uint	cad;

	Widget	*conv;	/* DAC or ADC */
	Widget	*jack;	/* the pin jack */
};

struct Id {
	Ctlr *ctlr;
	uint codec, nid;
};

struct Widget {
	Id id;
	Fungroup *fg;
	uint cap, type;
	uint nlist;
	Widget **list;
	union {
		struct {
			uint pin, pincap;
		};
		struct {
			uint convrate, convfmt;
		};
	};
	Widget *next;	/* next in function group */
	Widget *path;	/* next in audio path */

	Widget *link;	/* temporary for findpath */
};

struct Fungroup {
	Id id;
	Codec *codec;
	uint type;
	Widget *first;
	Fungroup *next;
};

struct Codec {
	Id id;
	uint vid, rid;
	Widget *widgets[Maxwidgets];
	Fungroup *fgroup;
};

/* hardware structures */

struct Bld {
	ulong addrlo;
	ulong addrhi;
	ulong len;
	ulong flags;
};

struct Ctlr {
	Ctlr *next;
	int no;

	Lock;			/* interrupt lock */
	QLock;			/* command lock */

	Audio *adev;

	uvlong port;
	Pcidev *pcidev;
	
	uchar *mem;
	ulong size;
	
	Queue *q;
	ulong *corb;
	ulong corbsize;
	ulong *rirb;
	ulong rirbsize;
	
	Stream sout;
	Stream sin;

	uint iss, oss, bss;

	uint codecmask;	
	Codec *codec[Maxcodecs];
};

#define csr32(c, r)	(*(ulong *)&(c)->mem[r])
#define csr16(c, r)	(*(ushort *)&(c)->mem[r])
#define csr8(c, r)	(*(uchar *)&(c)->mem[r])

static char *widtype[] = {
	"aout",
	"ain",
	"amix",
	"asel",
	"pin",
	"power",
	"knob",
	"beep",
};

static char *pinport[] = {
	"jack",
	"nothing",
	"fix",
	"jack+fix",
};

static char *pinfunc[] = {
	"lineout",
	"speaker",
	"hpout",
	"cd",
	"spdifout",
	"digiout",
	"modemline",
	"modemhandset",
	"linein",
	"aux",
	"micin",
	"telephony",
	"spdifin",
	"digiin",
	"resvd",
	"other",
};


static char *pincol[] = {
	"?",
	"black",
	"grey",
	"blue",
	"green",
	"red",
	"orange",
	"yellow",
	"purple",
	"pink",
	"resvd",
	"resvd",
	"resvd",
	"resvd",
	"white",
	"other",
};

static char *pinloc[] = {
	"N/A",
	"rear",
	"front",
	"left",
	"right",
	"top",
	"bottom",
	"special",
	"special",
	"special",
	"resvd",
	"resvd",
	"resvd",
	"resvd",
	"resvd",
	"resvd",
};

static char *pinloc2[] = {
	"ext",
	"int",
	"sep",
	"other",
};

Ctlr *lastcard;

static int
waitup8(Ctlr *ctlr, int reg, uchar mask, uchar set)
{
	int i;
	for(i=0; i<Maxwaitup; i++){
		if((csr8(ctlr, reg) & mask) == set)
			return 0;
		microdelay(1);
	}
	print("#A%d: waitup timeout for reg=%x, mask=%x, set=%x\n",
		ctlr->no, reg, mask, set);
	return -1;
}

static int
waitup16(Ctlr *ctlr, int reg, ushort mask, ushort set)
{
	int i;
	for(i=0; i<Maxwaitup; i++){
		if((csr16(ctlr, reg) & mask) == set)
			return 0;
		microdelay(1);
	}
	print("#A%d: waitup timeout for reg=%x, mask=%x, set=%x\n",
		ctlr->no, reg, mask, set);
	return -1;
}

static int
waitup32(Ctlr *ctlr, int reg, uint mask, uint set)
{
	int i;
	for(i=0; i<Maxwaitup; i++){
		if((csr32(ctlr, reg) & mask) == set)
			return 0;
		microdelay(1);
	}
	print("#A%d: waitup timeout for reg=%x, mask=%x, set=%x\n",
		ctlr->no, reg, mask, set);
	return -1;
}

static int
hdacmd(Ctlr *ctlr, uint request, uint reply[2])
{
	uint rp, wp;
	uint re;
	int wait;
	
	re = csr16(ctlr, Rirbwp);
	rp = csr16(ctlr, Corbrp);
	wp = (csr16(ctlr, Corbwp) + 1) % ctlr->corbsize;
	if(rp == wp){
		print("#A%d: corb full\n", ctlr->no);
		return -1;
	}
	ctlr->corb[wp] = request;
	coherence();
	csr16(ctlr, Corbwp) = wp;
	for(wait=0; wait < Maxrirbwait; wait++){
		if(csr16(ctlr, Rirbwp) != re){
			re = (re + 1) % ctlr->rirbsize;
			memmove(reply, &ctlr->rirb[re*2], 8);
			return 1;
		}
		microdelay(1);
	}
	return 0;
}

static int
cmderr(Id id, uint verb, uint par, uint *ret)
{
	uint q, w[2];
	q = (id.codec << 28) | (id.nid << 20);
	if((verb & 0x700) == 0x700)
		q |= (verb << 8) | par;
	else
		q |= (verb << 16) | par;
	if(hdacmd(id.ctlr, q, w) != 1)
		return -1;
	if(w[1] != id.codec)
		return -1;
	*ret = w[0];
	return 0;
}

static uint
cmd(Id id, uint verb, uint par)
{
	uint w[2];
	if(cmderr(id, verb, par, w) == -1)
		return ~0;
	return w[0];
}

static Id
newnid(Id id, uint nid)
{
	id.nid = nid;
	return id;
}

static uint
getoutamprange(Widget *w)
{
	uint r;

	if((w->cap & Woutampcap) == 0)
		return 0;
	if((w->cap & Wampovrcap) == 0)
		r = cmd(w->fg->id, Getparm, Outampcap);
	else
		r = cmd(w->id, Getparm, Outampcap);
	return (r >> 8) & 0x7f;
}

static void
getoutamp(Widget *w, int vol[2])
{
	vol[0] = vol[1] = 0;
	if((w->cap & Woutampcap) == 0)
		return;
	vol[0] = cmd(w->id, Getamp, Agetout | Agetleft) & Againmask;
	vol[1] = cmd(w->id, Getamp, Agetout | Agetright) & Againmask;
}

/* vol is 0...range or nil for 0dB; mute is 0/1 */
static void
setoutamp(Widget *w, int mute, int *vol)
{
	uint q, r, i;
	uint zerodb;

	if((w->cap & Woutampcap) == 0)
		return;
	if((w->cap & Wampovrcap) == 0)
		r = cmd(w->fg->id, Getparm, Outampcap);
	else
		r = cmd(w->id, Getparm, Outampcap);
	zerodb = r & 0x7f;
	
	for(i=0; i<2; i++){
		q = Asetout | (i == 0 ? Asetleft : Asetright);
		if(mute)
			q |= Asetmute;
		else if(vol == nil)
			q |= zerodb << Again;
		else
			q |= vol[i] << Again;
		cmd(w->id, Setamp, q);
	}
}

static uint
getinamprange(Widget *w)
{
	uint r;

	if((w->cap & Winampcap) == 0)
		return 0;
	if((w->cap & Wampovrcap) == 0)
		r = cmd(w->fg->id, Getparm, Inampcap);
	else
		r = cmd(w->id, Getparm, Inampcap);
	return (r >> 8) & 0x7f;
}

static void
getinamp(Widget *w, int vol[2])
{
	vol[0] = vol[1] = 0;
	if((w->cap & Winampcap) == 0)
		return;
	vol[0] = cmd(w->id, Getamp, Agetin | Agetleft) & Againmask;
	vol[1] = cmd(w->id, Getamp, Agetin | Agetright) & Againmask;
}

/* vol is 0...range or nil for 0dB; mute is 0/1; in is widget or nil for all */
static void
setinamp(Widget *w, Widget *in, int mute, int *vol)
{
	uint q, r, i, j;
	uint zerodb;

	if((w->cap & Winampcap) == 0)
		return;
	if((w->cap & Wampovrcap) == 0)
		r = cmd(w->fg->id, Getparm, Inampcap);
	else
		r = cmd(w->id, Getparm, Inampcap);
	zerodb = r & 0x7f;
	
	for(i=0; i<2; i++){
		q = Asetin | (i == 0 ? Asetleft : Asetright);
		if(mute)
			q |= Asetmute;
		else if(vol == nil)
			q |= zerodb << Again;
		else
			q |= vol[i] << Again;
		for(j=0; j<w->nlist; j++){
			if(in == nil || w->list[j] == in)
				cmd(w->id, Setamp, q | (j << Asetidx));
		}
	}
}

static Widget *
findpath(Widget *jack, int type, char *route)
{
	Widget *q[Maxwidgets];
	uint l, r, i;
	Widget *w, *to;
	Fungroup *fg;

	fg = jack->fg;

	l = r = 0;
	for(w=fg->first; w != nil; w = w->next)
		w->link = nil;

	if(route != nil && *route != 0){
		w = jack;
		while(*route++ == ','){
			i = strtoul(route, &route, 0);
			if(i >= Maxwidgets)
				return nil;
			to = fg->codec->widgets[i];
			if(to == nil || to->fg != fg || to->link != nil)
				return nil;
			if(type == Waout)
				to->link = w;
			else
				w->link = to;
			w = to;
		}
		if(w == jack || w->type != type)
			w = nil;
		return w;
	}

	if(type == Waout){
		q[r++] = jack;
		jack->link = jack;
	} else {
		for(w=fg->first; w != nil; w = w->next)
			if(w->type == type){
				q[r++] = w;
				w->link = w;
			}
	}

	while(l < r){
		w = q[l++];
		if(type == Waout){
			if(w->type == type)
				return w;
		} else if(w == jack){
			for(w = jack->link; w != nil; w = w->link)
				if(w->type == type)
					return w;
			break;
		}
		for(i=0; i<w->nlist; i++){
			to = w->list[i];
			if(to == nil || to->link)
				continue;
			to->link = w;
			q[r++] = to;
		}
	}

	return nil;
}

static void
disconnectpath(Widget *from, Widget *to)
{
	Widget *next;

	for(; from != nil && from != to; from = next){
		next = from->path;
		from->path = nil;
		setoutamp(from, 1, nil);
		if(next != nil)
			setinamp(next, from, 1, nil);
	}
	setoutamp(to, 1, nil);
}

static void
muteall(Ctlr *ctlr)
{
	Fungroup *fg;
	Widget *w;
	int i;

	for(i=0; i<Maxcodecs; i++){
		if(ctlr->codec[i] == nil)
			continue;
		for(fg=ctlr->codec[i]->fgroup; fg; fg=fg->next){
			for(w=fg->first; w != nil; w=w->next){
				setinamp(w, nil, 1, nil);
				setoutamp(w, 1, nil);
				switch(w->type){
				case Wain:
				case Waout:
					cmd(w->id, Setstream, 0);
					break;
				case Wpin:
					cmd(w->id, Setpinctl, 0);
					break;
				}
			}
		}
	}
}

static void
connectpath(Widget *from, Widget *to)
{
	Widget *next;
	uint i;

	for(; from != nil && from != to; from = next){
		next = from->link;
		from->path = next;
		setoutamp(from, 0, nil);
		if(next != nil){
			setinamp(next, from, 0, nil);
			for(i=0; i < next->nlist; i++){
				if(next->list[i] == from){
					cmd(next->id, Setconn, i);	
					break;
				}
			}
		}
	}
	setoutamp(to, 0, nil);
}

static void
addconn(Widget *w, uint nid)
{
	Widget *src;

	src = nil;
	if(nid < Maxwidgets)
		src = w->fg->codec->widgets[nid];
	if(src == nil || (src->fg != w->fg)){
		print("hda: invalid connection %d:%s[%d] -> %d\n",
			w->id.nid, widtype[w->type & 7], w->nlist, nid);
		src = nil;
	}
	if((w->nlist % 16) == 0){
		void *p;

		if((p = realloc(w->list, sizeof(Widget*) * (w->nlist+16))) == nil){
			print("hda: no memory for Widgetlist\n");
			return;
		}
		w->list = p;
	}
	w->list[w->nlist++] = src;
}

static void
enumconns(Widget *w)
{
	uint r, f, b, m, i, n, x, y;

	if((w->cap & Wconncap) == 0)
		return;

	r = cmd(w->id, Getparm, Connlistlen);
	n = r & 0x7f;
	b = (r & 0x80) ? 16 : 8;
	m = (1<<b)-1;
	f = (32/b)-1;
	x = 0;
	for(i=0; i<n; i++){
		if(i & f)
			r >>= b;
		else
			r = cmd(w->id, Getconnlist, i);
		y = r & (m>>1);
		if(i && (r & m) != y)
			while(++x < y)
				addconn(w, x);
		addconn(w, y);
		x = y;
	}
}

static void
enumwidget(Widget *w)
{
	w->cap = cmd(w->id, Getparm, Widgetcap);
	w->type = (w->cap >> 20) & 0x7;
	if(w->cap & Wpwrcap){
		cmd(w->id, Setpower, 0);
		delay(10);
	}	
	switch(w->type){
	case Wpin:
		w->pin = cmd(w->id, Getdefault, 0);
		w->pincap = cmd(w->id, Getparm, Pincap);
		if(w->pincap & Peapd)
			cmd(w->id, Seteapd, Eapdenable);
		break;
	}
}

static Fungroup *
enumfungroup(Codec *codec, Id id)
{
	Fungroup *fg;
	Widget *w, **tail;
	uint i, r, n, base;

	r = cmd(id, Getparm, Fungrtype) & 0x7f;
	if(r != Graudio){
		cmd(id, Setpower, 3);	/* turn off */
		return nil;
	}

	/* open eyes */
	cmd(id, Setpower, 0);
	delay(10);

	r = cmd(id, Getparm, Subnodecnt);
	n = r & 0xff;
	base = (r >> 16) & 0xff;
	if(base >= Maxwidgets){
		print("hda: enumfungroup: base %d out of range\n", base);
		return nil;
	}
	if(base+n > Maxwidgets){
		print("hda: enumfungroup: widgets %d - %d out of range\n", base, base+n);
		n = Maxwidgets - base;
	}

	fg = mallocz(sizeof *fg, 1);
	if(fg == nil){
Nomem:
		print("hda: enumfungroup: out of memory\n");
		return nil;
	}
	fg->codec = codec;
	fg->id = id;
	fg->type = r;

	tail = &fg->first;
	for(i=0; i<n; i++){
		if(codec->widgets[base + i] != nil){
			print("hda: enumfungroup: duplicate widget %d\n", base + i);
			continue;
		}
		w = mallocz(sizeof(Widget), 1);
		if(w == nil){
			while(w = fg->first){
				fg->first = w->next;
				codec->widgets[w->id.nid] = nil;
				free(w);
			}
			free(fg);
			goto Nomem;
		}
		w->id = newnid(id, base + i);
		w->fg = fg;
		*tail = w;
		tail = &w->next;
		codec->widgets[w->id.nid] = w;
	}

	for(i=0; i<n; i++)
		enumwidget(codec->widgets[base + i]);
	for(i=0; i<n; i++)
		enumconns(codec->widgets[base + i]);

	return fg;
}

static int
enumcodec(Codec *codec, Id id)
{
	Fungroup *fg;
	uint i, r, n, base;
	uint vid, rid;
	
	if(cmderr(id, Getparm, Vendorid, &vid) < 0)
		return -1;
	if(cmderr(id, Getparm, Revid, &rid) < 0)
		return -1;
	
	codec->id = id;
	codec->vid = vid;
	codec->rid = rid;

	r = cmd(id, Getparm, Subnodecnt);
	n = r & 0xff;
	base = (r >> 16) & 0xff;

	for(i=0; i<n; i++){
		fg = enumfungroup(codec, newnid(id, base + i));
		if(fg == nil)
			continue;
		fg->next = codec->fgroup;
		codec->fgroup = fg;
	}
	if(codec->fgroup == nil)
		return -1;

	print("#A%d: codec #%d, vendor %08ux, rev %08ux\n",
		id.ctlr->no, codec->id.codec, codec->vid, codec->rid);

	return 0;
}

static int
enumdev(Ctlr *ctlr)
{
	Codec *codec;
	int ret;
	Id id;
	int i;

	ret = -1;
	id.ctlr = ctlr;
	id.nid = 0;
	for(i=0; i<Maxcodecs; i++){
		if(((1<<i) & ctlr->codecmask) == 0)
			continue;
		codec = mallocz(sizeof(Codec), 1);
		if(codec == nil){
			print("hda: no memory for Codec\n");
			break;
		}
		id.codec = i;
		ctlr->codec[i] = codec;
		if(enumcodec(codec, id) < 0){
			ctlr->codec[i] = nil;
			free(codec);
			continue;
		}
		ret++;
	}
	return ret;
}

static int
connectpin(Ctlr *ctlr, Stream *s, int type, uint pin, uint cad, char *route)
{
	Widget *jack, *conv;

	if(s->atag == 0)
		return -1;
	if(cad >= Maxcodecs || pin >= Maxwidgets || ctlr->codec[cad] == nil)
		return -1;
	jack = ctlr->codec[cad]->widgets[pin];
	if(jack == nil)
		return -1;
	if(jack->type != Wpin)
		return -1;

	conv = findpath(jack, type, route);
	if(conv == nil)
		return -1;

	if(s->conv != nil && s->jack != nil){
		if(s->conv->type == Waout)
			disconnectpath(s->conv, s->jack);
		else
			disconnectpath(s->jack, s->conv);
		cmd(s->conv->id, Setstream, 0);
		cmd(s->jack->id, Setpinctl, 0);
	}

	if(type == Waout){
		connectpath(conv, jack);
		cmd(jack->id, Setpinctl, Pinctlout);
	} else {
		connectpath(jack, conv);
		cmd(jack->id, Setpinctl, Pinctlin);
	}

	cmd(conv->id, Setconvfmt, s->afmt);
	cmd(conv->id, Setstream, (s->atag << 4) | 0);
	cmd(conv->id, Setchancnt, 1);

	s->conv = conv;
	s->jack = jack;
	s->pin = pin;
	s->cad = cad;

	return 0;
}

static int
scoreout(Widget *w)
{
	int score;
	uint r;

	if((w->pincap & Pout) == 0)
		return -1;
	if(w->id.ctlr->sin.jack == w)
		return -1;

	score = 0;
	r = w->pin;
	if(((r >> 30) & 0x3) >= 2) /* fix or fix+jack */
		score |= 32;
	if(((r >> 12) & 0xf) == 4) /* green */
		score |= 32;
	if(((r >> 24) & 0xf) == 1) /* rear */
		score |= 16;
	if(((r >> 28) & 0x3) == 0) /* ext */
		score |= 8;
	if(((r >> 20) & 0xf) == 2) /* hpout */
		score |= 4;
	if(((r >> 20) & 0xf) == 0) /* lineout */
		score |= 4;
	return score;
}

static int
scorein(Widget *w)
{
	int score;
	uint r;

	if((w->pincap & Pin) == 0)
		return -1;
	if(w->id.ctlr->sout.jack == w)
		return -1;

	score = 0;
	r = w->pin;
	if(((r >> 30) & 0x3) >= 2) /* fix or fix+jack */
		score |= 4;
	return score;
}

static int
bestpin(Ctlr *ctlr, int *pcad, int (*fscore)(Widget *))
{
	Fungroup *fg;
	Widget *w;
	int best, pin, score;
	int i;

	pin = -1;
	best = -1;
	for(i=0; i<Maxcodecs; i++){
		if(ctlr->codec[i] == nil)
			continue;
		for(fg=ctlr->codec[i]->fgroup; fg != nil; fg=fg->next){
			for(w=fg->first; w != nil; w=w->next){
				if(w->type != Wpin)
					continue;
				score = (*fscore)(w);
				if(score >= 0 && score >= best){
					best = score;
					pin = w->id.nid;
					*pcad = i;
				}
			}
		}
	}
	return pin;
}

static long
buffered(Ring *r)
{
	ulong ri, wi;

	ri = r->ri;
	wi = r->wi;
	if(wi >= ri)
		return wi - ri;
	else
		return r->nbuf - (ri - wi);
}

static long
available(Ring *r)
{
	long m;

	m = (r->nbuf - BytesPerSample) - buffered(r);
	if(m < 0)
		m = 0;
	return m;
}

static long
readring(Ring *r, uchar *p, long n)
{
	long n0, m;

	n0 = n;
	while(n > 0){
		if((m = buffered(r)) <= 0)
			break;
		if(m > n)
			m = n;
		if(p){
			if(r->ri + m > r->nbuf)
				m = r->nbuf - r->ri;
			memmove(p, r->buf + r->ri, m);
			p += m;
		}
		r->ri = (r->ri + m) % r->nbuf;
		n -= m;
	}
	return n0 - n;
}

static long
writering(Ring *r, uchar *p, long n)
{
	long n0, m;

	n0 = n;
	while(n > 0){
		if((m = available(r)) <= 0)
			break;
		if(m > n)
			m = n;
		if(p){
			if(r->wi + m > r->nbuf)
				m = r->nbuf - r->wi;
			memmove(r->buf + r->wi, p, m);
			p += m;
		}
		r->wi = (r->wi + m) % r->nbuf;
		n -= m;
	}
	return n0 - n;
}

static int
streamalloc(Ctlr *ctlr, Stream *s, int num)
{
	Ring *r;
	int i;

	r = &s->ring;
	r->buf = xspanalloc(r->nbuf = Bufsize, 128, 0);
	s->blds = xspanalloc(Nblocks * sizeof(Bld), 128, 0);
	if(r->buf == nil || s->blds == nil){
		print("hda: no memory for stream\n");
		return -1;
	}
	for(i=0; i<Nblocks; i++){
		s->blds[i].addrlo = PADDR(r->buf) + i*Blocksize;
		s->blds[i].addrhi = 0;
		s->blds[i].len = Blocksize;
		s->blds[i].flags = 0x01;	/* interrupt on completion */
	}

	s->sdnum = num;
	s->sdctl = Sdctl0 + s->sdnum*0x20;
	s->sdintr = 1<<s->sdnum;
	s->atag = s->sdnum+1;
	s->afmt = Fmtstereo | Fmtsampw | Fmtdiv1 | Fmtmul1 | Fmtbase441;
	s->active = 0;

	/* perform reset */
	csr8(ctlr, s->sdctl) &= ~(Srst | Srun | Scie | Seie | Sdie);
	csr8(ctlr, s->sdctl) |= Srst;
	microdelay(Codecdelay);
	waitup8(ctlr, s->sdctl, Srst, Srst);
	csr8(ctlr, s->sdctl) &= ~Srst;
	microdelay(Codecdelay);
	waitup8(ctlr, s->sdctl, Srst, 0);

	/* set stream number */
	csr32(ctlr, s->sdctl) = (s->atag << Stagbit) |
		(csr32(ctlr, s->sdctl) & ~(0xF << Stagbit));

	/* set stream format */
	csr16(ctlr, Sdfmt+s->sdctl) = s->afmt;

	/* program stream DMA & parms */
	csr32(ctlr, Sdbdplo+s->sdctl) = PADDR(s->blds);
	csr32(ctlr, Sdbdphi+s->sdctl) = 0;
	csr32(ctlr, Sdcbl+s->sdctl) = r->nbuf;
	csr16(ctlr, Sdlvi+s->sdctl) = (Nblocks - 1) & 0xff;

	/* mask out ints */
	csr8(ctlr, Sdsts+s->sdctl) = Scompl | Sfifoerr | Sdescerr;

	/* enable global intrs for this stream */
	csr32(ctlr, Intctl) |= s->sdintr;
	csr8(ctlr, s->sdctl) |= Scie | Seie | Sdie;

	return 0;
}

static void
streamstart(Ctlr *ctlr, Stream *s)
{
	s->active = 1;
	csr8(ctlr, s->sdctl) |= Srun;
	waitup8(ctlr, s->sdctl, Srun, Srun);
}

static void
streamstop(Ctlr *ctlr, Stream *s)
{
	csr8(ctlr, s->sdctl) &= ~Srun;
	waitup8(ctlr, s->sdctl, Srun, 0);
	s->active = 0;
}

static uint
streampos(Ctlr *ctlr, Stream *s)
{
	uint p;

	p = csr32(ctlr, Sdlpib+s->sdctl);
	if(p >= s->ring.nbuf)
		p = 0;
	return p;
}

static long
hdactl(Audio *adev, void *va, long n, vlong)
{
	char *p, *e, *x, *route, *tok[4];
	int ntok;
	Ctlr *ctlr;
	uint pin, cad;
	
	ctlr = adev->ctlr;
	p = va;
	e = p + n;
	
	for(; p < e; p = x){
		route = nil;
		if(x = strchr(p, '\n'))
			*x++ = 0;
		else
			x = e;
		ntok = tokenize(p, tok, 4);
		if(ntok <= 0)
			continue;
		if(cistrcmp(tok[0], "pin") == 0 && ntok >= 2){
			cad = ctlr->sout.cad;
			pin = strtoul(tok[1], &route, 0);
			if(ntok > 2)
				cad = strtoul(tok[2], 0, 0);
			if(connectpin(ctlr, &ctlr->sout, Waout, pin, cad, route) < 0)
				error("connectpin failed");
		}else
		if(cistrcmp(tok[0], "inpin") == 0 && ntok >= 2){
			cad = ctlr->sin.cad;
			pin = strtoul(tok[1], &route, 0);
			if(ntok > 2)
				cad = strtoul(tok[2], 0, 0);
			if(connectpin(ctlr, &ctlr->sin, Wain, pin, cad, route) < 0)
				error("connectpin failed");
		}else
			error(Ebadctl);
	}
	return n;
}

static int
inavail(void *arg)
{
	Ring *r = arg;
	return buffered(r) > 0;
}

static int
outavail(void *arg)
{
	Ring *r = arg;
	return available(r) > 0;
}

static int
outrate(void *arg)
{
	Ctlr *ctlr = arg;
	int delay = ctlr->adev->delay*BytesPerSample;
	return (delay <= 0) || (buffered(&ctlr->sout.ring) <= delay) || (ctlr->sout.active == 0);
}

static long
hdabuffered(Audio *adev)
{
	Ctlr *ctlr;
	ctlr = adev->ctlr;
	return buffered(&ctlr->sout.ring);
}

static void
hdakick(Ctlr *ctlr)
{
	int delay;

	if(ctlr->sout.active)
		return;
	delay = ctlr->adev->delay*BytesPerSample;
	if(buffered(&ctlr->sout.ring) >= delay)
		streamstart(ctlr, &ctlr->sout);
}

static long
hdaread(Audio *adev, void *vp, long n, vlong)
{
	uchar *p, *e;
	Ctlr *ctlr;
	Ring *ring;

	p = vp;
	e = p + n;
	ctlr = adev->ctlr;
	ring = &ctlr->sin.ring;
	if(ring->buf == nil || ctlr->sin.conv == nil)
		return 0;
	while(p < e) {
		if((n = readring(ring, p, e - p)) <= 0){
			if(!ctlr->sin.active)
				streamstart(ctlr, &ctlr->sin);
			sleep(&ring->r, inavail, ring);
			continue;
		}
		p += n;
	}
	return p - (uchar*)vp;
}

static long
hdawrite(Audio *adev, void *vp, long n, vlong)
{
	uchar *p, *e;
	Ctlr *ctlr;
	Ring *ring;

	p = vp;
	e = p + n;
	ctlr = adev->ctlr;
	ring = &ctlr->sout.ring;
	if(ring->buf == nil || ctlr->sout.conv == nil)
		return 0;
	while(p < e) {
		if((n = writering(ring, p, e - p)) <= 0){
			hdakick(ctlr);
			sleep(&ring->r, outavail, ring);
			continue;
		}
		p += n;
	}
	hdakick(ctlr);
	while(outrate(ctlr) == 0)
		sleep(&ring->r, outrate, ctlr);
	return p - (uchar*)vp;
}

static void
hdaclose(Audio *adev, int mode)
{
	Ctlr *ctlr;
	Ring *ring;

	ctlr = adev->ctlr;
	if(mode == OREAD || mode == ORDWR){
		if(ctlr->sin.active)
			streamstop(ctlr, &ctlr->sin);
	}
	if(mode == OWRITE || mode == ORDWR){
		ring = &ctlr->sout.ring;
		while(ring->wi % Blocksize)
			if(writering(ring, (uchar*)"", 1) <= 0)
				break;
	}
}

enum {
	Vmaster,
	Vrecord,
	Vspeed,
	Vdelay,
	Nvol,
};

static Volume voltab[] = {
	[Vmaster] "master", 0, 0x7f, Stereo, 0,
	[Vrecord] "recgain", 0, 0x7f, Stereo, 0,
	[Vspeed] "speed", 0, 0, Absolute, 0,
	[Vdelay] "delay", 0, 0, Absolute, 0,
	0
};

static Widget*
findoutamp(Stream *s)
{
	Widget *w;

	for(w = s->conv; w != nil; w = w->path){
		if(w->cap & Woutampcap)
			return w;
		if(w == s->jack)
			break;
	}
	return nil;
}

static Widget*
findinamp(Stream *s)
{
	Widget *w, *p, *a;

	a = nil;
	for(p = nil, w = s->jack; w != nil; p = w, w = w->path){
		w->link = p;	/* for setinamp */
		if(w->cap & Winampcap)
			a = w;
		if(w == s->conv)
			break;
	}
	return a;
}

static int
hdagetvol(Audio *adev, int x, int a[2])
{
	Ctlr *ctlr = adev->ctlr;
	Widget *w;

	switch(x){
	case Vmaster:
		if((w = findoutamp(&ctlr->sout)) != nil)
			getoutamp(w, a);
		break;
	case Vrecord:
		if((w = findinamp(&ctlr->sin)) != nil)
			getinamp(w, a);
		break;
	case Vspeed:
		a[0] = adev->speed;
		break;
	case Vdelay:
		a[0] = adev->delay;
		break;
	}
	return 0;
}

static int
hdasetvol(Audio *adev, int x, int a[2])
{
	Ctlr *ctlr = adev->ctlr;
	Widget *w;

	switch(x){
	case Vmaster:
		if((w = findoutamp(&ctlr->sout)) != nil)
			setoutamp(w, 0, a);
		break;
	case Vrecord:
		if((w = findinamp(&ctlr->sin)) != nil)
			setinamp(w, w->link, 0, a);
		break;
	case Vspeed:
		adev->speed = a[0];
		break;
	case Vdelay:
		if(a[0] < Blocksize/BytesPerSample) {
			adev->delay = Blocksize/BytesPerSample;
		} else if(a[0] > (ctlr->sout.ring.nbuf/BytesPerSample)-1) {
			adev->delay = (ctlr->sout.ring.nbuf/BytesPerSample)-1;
		} else {
			adev->delay = a[0];
		}
		break;
	}
	return 0;
}

static void
fillvoltab(Ctlr *ctlr, Volume *vt)
{
	Widget *w;

	memmove(vt, voltab, sizeof(voltab));
	if((w = findoutamp(&ctlr->sout)) != nil)
		vt[Vmaster].range = getoutamprange(w);
	if((w = findinamp(&ctlr->sin)) != nil)
		vt[Vrecord].range = getinamprange(w);
}

static long
hdavolread(Audio *adev, void *a, long n, vlong)
{
	Volume voltab[Nvol+1];
	fillvoltab(adev->ctlr, voltab);
	return genaudiovolread(adev, a, n, 0, voltab, hdagetvol, 0);
}

static long
hdavolwrite(Audio *adev, void *a, long n, vlong)
{
	Volume voltab[Nvol+1];
	fillvoltab(adev->ctlr, voltab);
	return genaudiovolwrite(adev, a, n, 0, voltab, hdasetvol, 0);
}

static void
hdainterrupt(Ureg *, void *arg)
{
	Ctlr *ctlr;
	Audio *adev;
	Ring *r;
	uint sts;

	adev = arg;
	ctlr = adev->ctlr;
	if(ctlr == nil || ctlr->adev != adev)
		return;
	ilock(ctlr);
	sts = csr32(ctlr, Intsts);
	if(sts & ctlr->sout.sdintr){
		csr8(ctlr, Sdsts+ctlr->sout.sdctl) |= Scompl;

		r = &ctlr->sout.ring;
		r->ri = streampos(ctlr, &ctlr->sout);
		if(ctlr->sout.active && buffered(r) < Blocksize){
			streamstop(ctlr, &ctlr->sout);
			r->ri = r->wi = streampos(ctlr, &ctlr->sout);
		}
		wakeup(&r->r);
	}
	if(sts & ctlr->sin.sdintr){
		csr8(ctlr, Sdsts+ctlr->sin.sdctl) |= Scompl;

		r = &ctlr->sin.ring;
		r->wi = streampos(ctlr, &ctlr->sin);
		if(ctlr->sin.active && available(r) < Blocksize){
			streamstop(ctlr, &ctlr->sin);
			r->ri = r->wi = streampos(ctlr, &ctlr->sin);
		}
		wakeup(&r->r);
	}
	iunlock(ctlr);
}

static long
hdastatus(Audio *adev, void *a, long n, vlong)
{
	Ctlr *ctlr = adev->ctlr;
	Codec *codec;
	Widget *w;
	uint r;
	int i, j, k;
	char *s, *e;
	
	s = a;
	e = s + n;
	s = seprint(s, e, "bufsize %6d buffered %6ld\n", Blocksize, buffered(&ctlr->sout.ring));
	for(i=0; i<Maxcodecs; i++){
		if((codec = ctlr->codec[i]) == nil)
			continue;
		s = seprint(s, e, "codec %d pin %d inpin %d\n",
			codec->id.codec, ctlr->sout.pin, ctlr->sin.pin);
		for(j=0; j<Maxwidgets; j++){
			if((w = codec->widgets[j]) == nil)
				continue;
			switch(w->type){
			case Wpin:
				r = w->pin;
				s = seprint(s, e, "%s %d %s%s %s %s %s %s %s%s%s",
					widtype[w->type&7], w->id.nid,
					(w->pincap & Pin) != 0 ? "in" : "",
					(w->pincap & Pout) != 0 ? "out" : "",
					pinport[(r >> 30) & 0x3],
					pinloc2[(r >> 28) & 0x3],
					pinloc[(r >> 24) & 0xf],
					pinfunc[(r >> 20) & 0xf],
					pincol[(r >> 12) & 0xf],
					(w->pincap & Phdmi) ? " hdmi" : "",
					(w->pincap & Peapd) ? " eapd" : ""
				);
				break;
			default:
				s = seprint(s, e, "%s %d %lux",
					widtype[w->type&7], w->id.nid,
					(ulong)w->cap);
			}
			if(w->nlist > 0){
				s = seprint(s, e, " ← ");
				for(k=0; k<w->nlist; k++){
					if(k > 0)
						s = seprint(s, e, ", ");
					if(w->list[k] != nil)
						s = seprint(s, e, "%s %d", widtype[w->list[k]->type&7], w->list[k]->id.nid);
				}
			}
			s = seprint(s, e, "\n");
		}
	}

	if(ctlr->sout.conv != nil && ctlr->sout.jack != nil){
		s = seprint(s, e, "outpath ");
		for(w=ctlr->sout.conv; w != nil; w = w->path){
			s = seprint(s, e, "%s %d", widtype[w->type&7], w->id.nid);
			if(w == ctlr->sout.jack)
				break;
			s = seprint(s, e, " → ");
		}
		s = seprint(s, e, "\n");
		if((w = findoutamp(&ctlr->sout)) != nil)
			s = seprint(s, e, "outamp %s %d\n", widtype[w->type&7], w->id.nid);
	}

	if(ctlr->sin.conv != nil && ctlr->sin.jack != nil){
		s = seprint(s, e, "inpath ");
		for(w=ctlr->sin.jack; w != nil; w = w->path){
			s = seprint(s, e, "%s %d", widtype[w->type&7], w->id.nid);
			if(w == ctlr->sin.conv)
				break;
			s = seprint(s, e, " → ");
		}
		s = seprint(s, e, "\n");
		if((w = findinamp(&ctlr->sin)) != nil)
			s = seprint(s, e, "inamp %s %d\n", widtype[w->type&7], w->id.nid);
	}

	return s - (char*)a;
}


static int
hdastart(Ctlr *ctlr)
{
	static int cmdbufsize[] = { 2, 16, 256, 2048 };
	int n, size;
	uint cap;
	
	/* reset controller */
	csr32(ctlr, Gctl) &= ~Rst;
	waitup32(ctlr, Gctl, Rst, 0);
	microdelay(Codecdelay);
	csr32(ctlr, Gctl) |= Rst;
	if(waitup32(ctlr, Gctl, Rst, Rst) && 
	    waitup32(ctlr, Gctl, Rst, Rst)){
		print("#A%d: hda failed to reset\n", ctlr->no);
		return -1;
	}
	microdelay(Codecdelay);

	ctlr->codecmask = csr16(ctlr, Statests);
	if(ctlr->codecmask == 0){
		print("#A%d: hda no codecs\n", ctlr->no);
		return -1;
	}

	cap = csr16(ctlr, Gcap);
	ctlr->bss = (cap>>3) & 0x1F;
	ctlr->iss = (cap>>8) & 0xF;
	ctlr->oss = (cap>>12) & 0xF;

	csr8(ctlr, Corbctl) = 0;
	waitup8(ctlr, Corbctl, Corbdma, 0);

	csr8(ctlr, Rirbctl) = 0;
	waitup8(ctlr, Rirbctl, Rirbdma, 0);

	/* alloc command buffers */
	size = csr8(ctlr, Corbsz);
	n = cmdbufsize[size & 3];
	ctlr->corb = xspanalloc(n * 4, 128, 0);
	memset(ctlr->corb, 0, n * 4);
	ctlr->corbsize = n;

	size = csr8(ctlr, Rirbsz);
	n = cmdbufsize[size & 3];
	ctlr->rirb = xspanalloc(n * 8, 128, 0);
	memset(ctlr->rirb, 0, n * 8);
	ctlr->rirbsize = n;

	/* setup controller  */
	csr32(ctlr, Dplbase) = 0;
	csr32(ctlr, Dpubase) = 0;
	csr16(ctlr, Statests) = csr16(ctlr, Statests);
	csr8(ctlr, Rirbsts) = csr8(ctlr, Rirbsts);
	
	/* setup CORB */
	csr32(ctlr, Corblbase) = PADDR(ctlr->corb);
	csr32(ctlr, Corbubase) = 0;
	csr16(ctlr, Corbwp) = 0;
	csr16(ctlr, Corbrp) = Corbptrrst;
	waitup16(ctlr, Corbrp, Corbptrrst, Corbptrrst);
	csr16(ctlr, Corbrp) = 0;
	waitup16(ctlr, Corbrp, Corbptrrst, 0);
	csr8(ctlr, Corbctl) = Corbdma;
	waitup8(ctlr, Corbctl, Corbdma, Corbdma);
	
	/* setup RIRB */
	csr32(ctlr, Rirblbase) = PADDR(ctlr->rirb);
	csr32(ctlr, Rirbubase) = 0;
	csr16(ctlr, Rirbwp) = Rirbptrrst;
	csr8(ctlr, Rirbctl) = Rirbdma;
	waitup8(ctlr, Rirbctl, Rirbdma, Rirbdma);
	
	/* enable interrupts */
	csr32(ctlr, Intctl) |= Gie | Cie;
	
	return 0;
}

static Pcidev*
hdamatch(Pcidev *p)
{
	while(p = pcimatch(p, 0, 0))
		switch((p->vid << 16) | p->did){
		case (0x8086 << 16) | 0x2668:	/* Intel ICH6 (untested) */
		case (0x8086 << 16) | 0x27d8:	/* Intel ICH7 */
		case (0x8086 << 16) | 0x269a:	/* Intel ESB2 (untested) */
		case (0x8086 << 16) | 0x284b:	/* Intel ICH8 */
		case (0x8086 << 16) | 0x293f:	/* Intel ICH9 (untested) */
		case (0x8086 << 16) | 0x293e:	/* Intel P35 (untested) */
		case (0x8086 << 16) | 0x3b56:	/* Intel P55 (Ibex Peak) */
		case (0x8086 << 16) | 0x811b:	/* Intel SCH (Poulsbo) */
		case (0x8086 << 16) | 0x080a:	/* Intel SCH (Oaktrail) */
		case (0x8086 << 16) | 0x1c20:	/* Intel PCH */
		case (0x8086 << 16) | 0x1e20:	/* Intel (Thinkpad x230t) */
		case (0x8086 << 16) | 0x8c20:	/* Intel 8 Series/C220 Series */
		case (0x8086 << 16) | 0x8ca0:	/* Intel 9 Series */
		case (0x8086 << 16) | 0x9c20:	/* Intel 8 Series Lynx Point */
		case (0x8086 << 16) | 0x9ca0:	/* Intel Wildcat Point */
		case (0x8086 << 16) | 0xa170:   /* Intel Sunrise Point-H */
		case (0x8086 << 16) | 0x9d70:   /* Intel Sunrise Point-LP */
		case (0x8086 << 16) | 0x9d71:   /* Intel Sunrise Point-LP */
		case (0x8086 << 16) | 0x3a6e:	/* Intel ICH10 */
		case (0x8086 << 16) | 0x3198:   /* Intel Gemini-Lake */

		case (0x10de << 16) | 0x026c:	/* NVidia MCP51 (untested) */
		case (0x10de << 16) | 0x0371:	/* NVidia MCP55 (untested) */
		case (0x10de << 16) | 0x03e4:	/* NVidia MCP61 (untested) */
		case (0x10de << 16) | 0x03f0:	/* NVidia MCP61A (untested) */
		case (0x10de << 16) | 0x044a:	/* NVidia MCP65 (untested) */
		case (0x10de << 16) | 0x055c:	/* NVidia MCP67 (untested) */
		case (0x10de << 16) | 0x0fbb:	/* NVidia GM204 (untested) */

		case (0x1002 << 16) | 0x437b:	/* ATI SB450 (untested) */
		case (0x1002 << 16) | 0x4383:	/* ATI SB600 */
		case (0x1002 << 16) | 0xaa55:	/* ATI HDMI (8500 series) */
		case (0x1002 << 16) | 0x7919:	/* ATI HDMI */

		case (0x1106 << 16) | 0x3288:	/* VIA (untested) */
		case (0x1039 << 16) | 0x7502:	/* SIS (untested) */
		case (0x10b9 << 16) | 0x5461:	/* ULI (untested) */

		case (0x1022 << 16) | 0x780d:	/* AMD FCH Azalia Controller */
		case (0x1022 << 16) | 0x1457:	/* AMD Family 17h (Models 00h-0fh) HD Audio Controller */
		case (0x1022 << 16) | 0x15e3:	/* AMD Raven HD Audio Controller */

		case (0x15ad << 16) | 0x1977:	/* Vmware */
			return p;
		}
	return nil;
}

static long
hdacmdread(Chan *, void *a, long n, vlong)
{
	Ctlr *ctlr;
	
	ctlr = lastcard;
	if(ctlr == nil)
		error(Enodev);
	if(n & 7)
		error(Ebadarg);
	return qread(ctlr->q, a, n);
}

static long
hdacmdwrite(Chan *, void *a, long n, vlong)
{
	Ctlr *ctlr;
	ulong *lp;
	int i;
	uint w[2];
	
	ctlr = lastcard;
	if(ctlr == nil)
		error(Enodev);
	if(n & 3)
		error(Ebadarg);
	lp = a;
	qlock(ctlr);
	for(i=0; i<n/4; i++){
		if(hdacmd(ctlr, lp[i], w) <= 0){
			w[0] = 0;
			w[1] = ~0;
		}
		qproduce(ctlr->q, w, sizeof(w));
	}
	qunlock(ctlr);
	return n;
}

static int
hdareset1(Audio *adev, Ctlr *ctlr)
{
	int best, cad, irq, tbdf;
	Pcidev *p;

	p = ctlr->pcidev;
	irq = p->intl;
	tbdf = p->tbdf;

	if(p->vid == 0x10de){
		/* magic for NVidia */
		pcicfgw8(p, 0x4e, (pcicfgr8(p, 0x4e) & 0xf0) | 0x0f);
  	}
	if(p->vid == 0x10b9){
		/* magic for ULI */
		pcicfgw16(p, 0x40, pcicfgr16(p, 0x40) | 0x10);
		pcicfgw32(p, PciBAR1, 0);
	}
	if(p->vid == 0x8086){
		/* magic for Intel */
		switch(p->did){
		case 0x1c20:	/* PCH */
		case 0x1e20:
		case 0x811b:	/* SCH */
		case 0x080a:
		case 0x8c20:
		case 0x8ca0:
		case 0x9c20:
		case 0x9ca0:
		case 0xa170:
			pcicfgw16(p, 0x78, pcicfgr16(p, 0x78) & ~0x800);
		}
	}
	if(p->vid == 0x1002){
		/* magic for ATI */
		pcicfgw8(p, 0x42, pcicfgr8(p, 0x42) | 0x02);
	} else {
		/* TCSEL */
		pcicfgw8(p, 0x44, pcicfgr8(p, 0x44) & 0xf8);
	}

	if(p->mem[0].bar & 1){
		print("hda: bar0 %llux: not memory\n", p->mem[0].bar);
		return -1;
	}
	ctlr->size = p->mem[0].size;
	ctlr->port = p->mem[0].bar & ~0xF;
	ctlr->mem = vmap(ctlr->port, ctlr->size);
	if(ctlr->mem == nil){
		print("hda: can't map %llux\n", ctlr->port);
		return -1;
	}
	ctlr->no = adev->ctlrno;
	print("#A%d: hda mem %llux irq %d\n", ctlr->no, ctlr->port, irq);

	if(hdastart(ctlr) < 0){
		print("#A%d: unable to start hda\n", ctlr->no);
		return -1;
	}

	/* iss + oss + bss */
	if(streamalloc(ctlr, &ctlr->sout, ctlr->iss) < 0)
		print("#A%d: output streamalloc failed\n", ctlr->no);
	if(ctlr->iss > 0){
		if(streamalloc(ctlr, &ctlr->sin, 0) < 0)
			print("#A%d: input streamalloc failed\n", ctlr->no);
	}
	else if(ctlr->bss > 0){
		if(ctlr->oss > 0){
			if(streamalloc(ctlr, &ctlr->sin, ctlr->oss) < 0)
				print("#A%d: input streamalloc failed\n", ctlr->no);
		} else if(ctlr->bss > 1) {
			if(streamalloc(ctlr, &ctlr->sin, 1) < 0)
				print("#A%d: input streamalloc failed\n", ctlr->no);
		}
	}
	pcisetbme(p);

	if(enumdev(ctlr) < 0){
		print("#A%d: no audio codecs found\n", ctlr->no);
		return -1;
	}
	muteall(ctlr);

	best = bestpin(ctlr, &cad, scoreout);
	if(best < 0)
		print("#A%d: no output pins found\n", ctlr->no);
	else if(connectpin(ctlr, &ctlr->sout, Waout, best, cad, nil) < 0)
		print("#A%d: error connecting output pin\n", ctlr->no);

	best = bestpin(ctlr, &cad, scorein);
	if(best < 0)
		print("#A%d: no input pins found\n", ctlr->no);
	else if(connectpin(ctlr, &ctlr->sin, Wain, best, cad, nil) < 0)
		print("#A%d: error connecting input pin\n", ctlr->no);

	adev->ctlr = ctlr;
	adev->read = hdaread;
	adev->write = hdawrite;
	adev->close = hdaclose;
	adev->buffered = hdabuffered;
	adev->volread = hdavolread;
	adev->volwrite = hdavolwrite;
	adev->status = hdastatus;
	adev->ctl = hdactl;
	
	intrenable(irq, hdainterrupt, adev, tbdf, "hda");

	ctlr->q = qopen(256, 0, 0, 0);

	lastcard = ctlr;
	addarchfile("hdacmd", 0664, hdacmdread, hdacmdwrite);

	return 0;
}

static int
hdareset(Audio *adev)
{
	static Ctlr *cards = nil;
	Ctlr *ctlr;
	Pcidev *p;

	/* make a list of all cards if not already done */
	if(cards == nil){
		p = nil;
		while((p = hdamatch(p)) != nil){
			ctlr = mallocz(sizeof(Ctlr), 1);
			if(ctlr == nil){
				print("hda: can't allocate memory\n");
				break;
			}
			ctlr->pcidev = p;
			ctlr->next = cards;
			cards = ctlr;
		}
	}

	/* pick a card from the list */
	for(ctlr = cards; ctlr != nil; ctlr = ctlr->next){
		if(ctlr->adev == nil && ctlr->pcidev != nil){
			ctlr->adev = adev;
			pcienable(ctlr->pcidev);
			if(hdareset1(adev, ctlr) == 0)
				return 0;
			pcidisable(ctlr->pcidev);
			ctlr->pcidev = nil;
			ctlr->adev = nil;
		}
	}
	return -1;
}

void
audiohdalink(void)
{
	addaudiocard("hda", hdareset);
}

