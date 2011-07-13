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
		Sismask = 0xff,
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
	/* stream register base */
	Sdinput0 = 0x80,
	Sdinput1 = 0xa0,
	Sdinput2 = 0xc0,
	Sdinput3 = 0xe0,
	Sdoutput0 = 0x100,
	Sdoutput1 = 0x120,
	Sdoutput2 = 0x140,
	Sdoutput3 = 0x160,
	/* Warning: Sdctl is 24bit register */
	Sdctl = Sdoutput0 + 0x00,
		Srst = 1<<0,
		Srun = 1<<1,
		Scie = 1<<2,
		Seie = 1<<3,
		Sdie = 1<<4,
		Stagbit = 20,
		Stagmask = 0xf00000,
	Sdsts = Sdoutput0 + 0x03,
		Scompl = 1<<2,
		Sfifoerr = 1<<3,
		Sdescerr = 1<<4,
		Sfifordy = 1<<5,
	Sdlpib = Sdoutput0 + 0x04,
	Sdcbl = Sdoutput0 + 0x08,
	Sdlvi = Sdoutput0 + 0x0c,
	Sdfifow = Sdoutput0 + 0x0e,
	Sdfifos = Sdoutput0 + 0x10,
	Sdfmt = Sdoutput0 + 0x12,
		Fmtmono = 0,
		Fmtstereo = 1,
		Fmtsampw = 1<<4,
		Fmtsampb = 0<<4,
		Fmtdiv1 = 0<<8,
		Fmtmul1 = 0<<11,
		Fmtbase441 = 1<<14,
		Fmtbase48 = 0<<14,
	Sdbdplo = Sdoutput0 + 0x18,
	Sdbdphi = Sdoutput0 + 0x1c,
};

enum {
	Bufsize = 64 * 1024 * 4,
	Nblocks = 256,
	Blocksize = Bufsize / Nblocks,
	Streamtag = 1,
	Streamno = 4,
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
	Getstream = 0xf06,
	Setstream = 0x706,
	Getpinctl = 0xf07,
	Setpinctl = 0x707,
		Pinctlin = 1<<5,
		Pinctlout = 1<<6,
	Getunsolresp = 0xf08,
	Setunsolresp = 0x708,
	Getpinsense = 0xf09,
	Exepinsense = 0x709,
	Getgpi = 0xf10,
	Setgpi = 0x710,
	Getbeep = 0xf0a,
	Setbeep = 0x70a,
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
	uint rp, wp, cp;
	uint size, blocksize;
	uchar *buf;
};

struct Bld {
	uint addrlo, addrhi;
	uint len, flags;
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
	Widget *next;
	Widget *from;
};

struct Fungroup {
	Id id;
	Codec *codec;
	uint type;
	Widget *first;
	Widget *mixer;
	Widget *src, *dst;
	Fungroup *next;
};

struct Codec {
	Id id;
	uint vid, rid;
	Widget *widgets[Maxwidgets];
	Fungroup *fgroup;
};

struct Ctlr {
	Ctlr *next;
	uint no;

	Lock;			/* interrupt lock */
	QLock;			/* command lock */
	Rendez outr;

	Audio *adev;
	Pcidev *pcidev;
	
	uchar *mem;
	ulong size;
	
	Queue *q;
	ulong *corb;
	ulong corbsize;
	ulong *rirb;
	ulong rirbsize;
	
	Bld *blds;
	Ring ring;
	
	Codec codec;
	Widget *amp, *src;
	uint pin;

	int active;
	uint afmt, atag;
};

#define csr32(c, r)	(*(ulong *)&(c)->mem[r])
#define csr16(c, r)	(*(ushort *)&(c)->mem[r])
#define csr8(c, r)	(*(uchar *)&(c)->mem[r])

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

/* vol is 0...range or nil for 0dB; mute is 0/1; in is widget or nil for all */
static void
setinamp(Widget *w, Widget *in, int mute, int *vol)
{
	uint q, r, i, j;
	uint zerodb;

	if((w->cap & Winampcap) == 0)
		return;

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
findpath(Widget *src)
{
	Widget *q[Maxwidgets];
	uint l, r, i;
	Widget *w, *v;
	
	l = r = 0;
	q[r++] = src;
	for(w=src->fg->first; w; w=w->next)
		w->from = nil;
	src->from = src;

	while(l < r){
		w = q[l++];
		if(w->type == Waout)
			break;
		for(i=0; i<w->nlist; i++){
			v = w->list[i];
			if(v->from)
				continue;
			v->from = w;
			q[r++] = v;
		}
	}
	if(w->type != Waout)
		return nil;
	return w;
}

static void
connectpath(Widget *src, Widget *dst, uint stream)
{
	Widget *w, *v;
	uint i;

	for(w=src->fg->first; w != nil; w=w->next){
		setoutamp(w, 1, nil);
		setinamp(w, nil, 1, nil);
		cmd(w->id, Setstream, 0);
	}
	for(w=dst; w != src; w=v){
		v = w->from;
		setoutamp(w, 0, nil);
		setinamp(v, w, 0, nil);
		if(v->type == Waout || v->type == Wamix)
			continue;
		if(v->nlist == 1)
			continue;
		for(i=0; i < v->nlist && v->list[i] != w; i++)
			;
		cmd(v->id, Setconn, i);
	}
	setoutamp(src, 0, nil);
	cmd(src->id, Setpinctl, Pinctlout);
	cmd(dst->id, Setstream, (stream << 4) | 0);
	cmd(dst->id, Setconvfmt, (1 << 14) | (1 << 4) | 1);
	cmd(dst->id, Setchancnt, 1);
}

static void
enumconns(Widget *w)
{
	uint r, i, mask, bits, nlist;
	Widget **ws, **list;
	
	ws = w->fg->codec->widgets;
	r = cmd(w->id, Getparm, Connlistlen);
	bits = (r & 0x80) == 0 ? 8 : 16;
	nlist = r & 0x7f;
	mask = (1 << bits) - 1;
	list = malloc(sizeof *list * nlist);
	for(i=0; i<nlist; i++){
		if(i * bits % 32 == 0)
			r = cmd(w->id, Getconnlist, i);		
		list[i] = ws[(r >> (i * bits % 32)) & mask];
	}
	w->nlist = nlist;
	w->list = list;
}

static void
enumwidget(Widget *w)
{
	w->cap = cmd(w->id, Getparm, Widgetcap);
	w->type = (w->cap >> 20) & 0x7;
	
	enumconns(w);
	
	switch(w->type){
		case Wpin:
			w->pin = cmd(w->id, Getdefault, 0);
			w->pincap = cmd(w->id, Getparm, Pincap);
			break;
	}
}

static Fungroup *
enumfungroup(Codec *codec, Id id)
{
	Fungroup *fg;
	Widget *w, *next;
	uint i, r, n, base;

	r = cmd(id, Getparm, Fungrtype) & 0x7f;
	if(r != Graudio)
		return nil;

	fg = mallocz(sizeof *fg, 1);
	fg->codec = codec;
	fg->id = id;
	fg->type = r;

	r = cmd(id, Getparm, Subnodecnt);
	n = r & 0xff;
	base = (r >> 8) & 0xff;
	
	if(base + n > Maxwidgets)
		return nil;
	
	for(i=n, next=nil; i--; next=w){
		w = mallocz(sizeof(Widget), 1);
		w->id = newnid(id, base + i);
		w->fg = fg;
		w->next = next;
		codec->widgets[base + i] = w;
	}
	fg->first = next;

	for(i=0; i<n; i++)
		enumwidget(codec->widgets[base + i]);

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
	return 0;
}

static int
enumdev(Ctlr *ctlr)
{
	Id id;
	int i;
	
	id.ctlr = ctlr;
	id.nid = 0;
	for(i=0; i<Maxcodecs; i++){
		id.codec = i;
		if(enumcodec(&ctlr->codec, id) == 0)
			return 0;
	}
	return -1;
}

static int
connectpin(Ctlr *ctlr, uint pin)
{
	Widget *src, *dst;
	
	src = ctlr->codec.widgets[pin];
	if(src == nil)
		return -1;
	if(src->type != Wpin)
		return -1;
	if((src->pincap & Pout) == 0)
		return -1;
	dst = findpath(src);
	if(!dst)
		return -1;
		
	connectpath(src, dst, Streamtag);
	ctlr->amp = dst;
	ctlr->src = src;
	ctlr->pin = pin;
	return 0;
}

static int
bestpin(Ctlr *ctlr)
{
	Fungroup *fg;
	Widget *w;
	int best, pin, score;
	uint r;
	
	pin = -1;
	best = -1;
	for(fg=ctlr->codec.fgroup; fg; fg=fg->next){
		for(w=fg->first; w; w=w->next){
			if(w->type != Wpin)
				continue;
			if((w->pincap & Pout) == 0)
				continue;
			score = 0;
			r = w->pin;
			if(((r >> 12) & 0xf) == 4)	/* green */
				score |= 32;
			if(((r >> 24) & 0xf) == 1)	/* rear */
				score |= 16;
			if(((r >> 28) & 0x3) == 0) /* ext */
				score |= 8;
			if(((r >> 20) & 0xf) == 2) /* hpout */
				score |= 4;
			if(((r >> 20) & 0xf) == 0) /* lineout */
				score |= 4;
			if(score >= best){
				best = score;
				pin = w->id.nid;
			}
		}
	}
	return pin;
}

static void
ringreset(Ring *r)
{
	memset(r->buf, 0, r->size);
	r->rp = 0;
	r->wp = 0;
	r->cp = 0;
}

static uint
ringused(Ring *r)
{
	return (r->wp - r->rp) % r->size;
}

static uint
ringavail(Ring *r)
{
	return r->size - r->blocksize - ringused(r);
}

static uint
ringdirty(Ring *r)
{
	return (r->rp - r->cp) % r->size;
}

static void
ringalign(Ring *r)
{
	r->wp += r->blocksize - 1;
	r->wp -= r->wp % r->blocksize;
	r->wp %= r->size;
}

static uint
ringwrite(Ring *r, uchar *ap, uint n)
{
	uchar *p;
	uint a, c;

	p = ap;
	a = ringavail(r);
	if(n > a)
		n = a;
		
	c = ringdirty(r);
	while(c > 0){
		a = r->size - r->cp;
		if(a > c)
			a = c;
		memset(r->buf + r->cp, 0, a);
		r->cp = (r->cp + a) % r->size;
		c -= a;
	}
	
	while(n > 0){
		a = r->size - r->wp;
		if(a > n)
			a = n;
		memmove(r->buf + r->wp, p, a);
		r->wp = (r->wp + a) % r->size;
		p += a;
		n -= a;
	}
	return p - ap;
}


static int
ringupdate(Ring *r, uint np)
{
	uint rp, wp, bs, s;
	
	rp = r->rp;
	bs = r->blocksize;
	s = r->size;
	
	np += bs / 2;
	np %= s;
	np -= np % bs;
	wp = r->wp;
	wp -= wp % bs;
	r->rp = np;
	if((np - rp) % s >= (wp - rp) % s)
		return 1;
	return 0;
}

static int
streamalloc(Ctlr *ctlr)
{
	uchar *p;
	Bld *b;
	uint i;
	Ring *r;
	
	r = &ctlr->ring;
	r->size = Bufsize;
	r->blocksize = Blocksize;
	r->buf = xspanalloc(r->size, 128, 0);
	if(r->buf == nil)
		return -1;
	ringreset(r);
	
	ctlr->active = 0;
	ctlr->atag = Streamtag;
	ctlr->afmt = Fmtstereo | Fmtsampw | Fmtdiv1 |
		Fmtmul1 | Fmtbase441;
	
	ctlr->blds = xspanalloc(Nblocks * sizeof(Bld), 128, 0);
	if(ctlr->blds == nil)
		return -1;
	b = ctlr->blds;
	p = r->buf;
	for(i=0; i<Nblocks; i++){
		b->addrlo = PADDR(p);
		b->addrhi = 0;
		b->flags = ~0;
		b->len = Blocksize;
		p += Blocksize;
		b++;
	}
	return 0;
}

static void
streamstart(Ctlr *ctlr)
{
	Ring *r = &ctlr->ring;
		
	/* perform reset */
	csr8(ctlr, Sdctl) = Srst;
	waitup8(ctlr, Sdctl, Srst, Srst);
	csr8(ctlr, Sdctl) = 0;
	waitup8(ctlr, Sdctl, Srst, 0);
	
	/* program stream DMA & parms */
	csr32(ctlr, Sdcbl) = r->size;
	csr16(ctlr, Sdlvi) = (r->size / r->blocksize - 1) & 0xff;
	csr32(ctlr, Sdfmt) = ctlr->afmt;
	csr32(ctlr, Sdbdplo) = PADDR(ctlr->blds);
	csr32(ctlr, Sdbdphi) = 0;
	
	/* enable global intrs for this stream */
	csr32(ctlr, Intctl) |= (1 << Streamno);
	
	/* enable stream intrs */
	csr32(ctlr, Sdctl) = (ctlr->atag << Stagbit) | Srun | Scie | Seie | Sdie;
	waitup32(ctlr, Sdctl, Srun, Srun);
	
	/* mark as running */
	ctlr->active = 1;
}

static void
streamstop(Ctlr *ctlr)
{
	/* disble stream intrs */
	csr32(ctlr, Sdctl) = 0;
	
	/* disable global intrs for this stream */
	csr32(ctlr, Intctl) &= ~(1 << Streamno);
	
	/* mark as stopped */
	ctlr->active = 0;
}


static void
streamupdate(Ctlr *ctlr)
{
	uint pos;
	Ring *r;
	
	r = &ctlr->ring;
	
	/* ack interrupt and wake writer */
	csr8(ctlr, Sdsts) |= 0x4;
	wakeup(&ctlr->outr);
	pos = csr32(ctlr, Sdlpib);
	
	/* underrun? */
	if(ringupdate(r, pos) == 1)
		streamstop(ctlr);
}

static int
outavail(void *arg)
{
	return ringavail(arg) > 0;
}

static int
outrate(void *arg)
{
	Ctlr *ctlr = arg;
	int delay = ctlr->adev->delay*4;
	return (delay <= 0) || (ringused(&ctlr->ring) <= delay) || (ctlr->active == 0);
}

static int
checkptr(Ctlr *ctlr)
{
	Ring *r;
	
	r = &ctlr->ring;
	if(ctlr->active == 1)
		return 1;
	if(r->rp == 0)
		return 1;
	ringreset(r);
	return 0;
}

static void
hdakick(Ctlr *ctlr)
{
	Ring *r = &ctlr->ring;
	
	ilock(ctlr);
	if(ctlr->active == 0){
		if(ringused(r) >= r->blocksize){
			iunlock(ctlr);
			streamstart(ctlr);
			return;
		}
	}
	iunlock(ctlr);
}

static long
hdabuffered(Audio *adev)
{
	Ctlr *ctlr;
	ctlr = adev->ctlr;
	return ringused(&ctlr->ring);
}

static long
hdactl(Audio *adev, void *va, long n, vlong)
{
	char *p, *e, *x, *tok[4];
	int ntok;
	Ctlr *ctlr;
	
	ctlr = adev->ctlr;
	p = va;
	e = p + n;
	
	for(; p < e; p = x){
		if(x = strchr(p, '\n'))
			*x++ = 0;
		else
			x = e;
		ntok = tokenize(p, tok, 4);
		if(ntok <= 0)
			continue;
		if(cistrcmp(tok[0], "pin") == 0 && ntok == 2){
			connectpin(ctlr, strtoul(tok[1], 0, 0));
		}else
			error(Ebadctl);
	}
	return n;
}

static long
hdawrite(Audio *adev, void *vp, long vn, vlong)
{
	uchar *p;
	uint n, k;
	Ring *r;
	Ctlr *ctlr;
	
	p = vp;
	n = vn;
	ctlr = adev->ctlr;
	r = &ctlr->ring;
	
	checkptr(ctlr);
	while(n > 0){
		k = ringwrite(r, p, n);
		if(checkptr(ctlr) == 0)
			continue;
		if(k == 0){
			hdakick(ctlr);
			sleep(&ctlr->outr, outavail, r);
		}else{
			p += k;
			n -= k;
		}
	}
	hdakick(ctlr);
	sleep(&ctlr->outr, outrate, ctlr);
	return vn;
}

static void
hdaclose(Audio *adev)
{
	Ctlr *ctlr;
	ctlr = adev->ctlr;
	ringalign(&ctlr->ring);
	hdakick(ctlr);
}

enum {
	Vmaster,
	Vspeed,
	Vdelay,
	Nvol,
};

static Volume voltab[] = {
	[Vmaster] "master", 0, 0x7f, Stereo, 0,
	[Vspeed] "speed", 0, 0, Absolute, 0,
	[Vdelay] "delay", 0, 0, Absolute, 0,
	0
};

static int
hdagetvol(Audio *adev, int x, int a[2])
{
	Ctlr *ctlr = adev->ctlr;

	switch(x){
	case Vmaster:
		if(ctlr->amp != nil)
			getoutamp(ctlr->amp, a);
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

	switch(x){
	case Vmaster:
		if(ctlr->amp != nil)
			setoutamp(ctlr->amp, 0, a);
		break;
	case Vspeed:
		adev->speed = a[0];
		break;
	case Vdelay:
		adev->delay = a[0];
		break;
	}
	return 0;
}

static void
fillvoltab(Ctlr *ctlr, Volume *vt)
{
	memmove(vt, voltab, sizeof(voltab));
	if(ctlr->amp != nil)
		vt[Vmaster].range = getoutamprange(ctlr->amp);
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
	uint sts;
	
	adev = arg;
	ctlr = adev->ctlr;
	
	ilock(ctlr);
	sts = csr32(ctlr, Intsts);
	if(sts & Sismask)
		streamupdate(ctlr);
	iunlock(ctlr);
}

static long
hdastatus(Audio *adev, void *a, long n, vlong)
{
	Ctlr *ctlr = adev->ctlr;
	Fungroup *fg;
	Widget *w;
	uint r;
	int k;
	char *s;
	
	s = a;
	k = snprint(s, n, "bufsize %6d buffered %6ud\ncodec %2d pin %3d\n",
		ctlr->ring.blocksize, ringused(&ctlr->ring),
		ctlr->codec.id.codec, ctlr->pin);
	
	for(fg=ctlr->codec.fgroup; fg; fg=fg->next){
		for(w=fg->first; w; w=w->next){
			if(w->type != Wpin)
				continue;
			r = w->pin;
			k += snprint(s+k, n-k, "pin %3d %s %s %s %s %s %s\n",
				w->id.nid,
				(w->pincap & Pout) != 0 ? "out" : "in",
				pinport[(r >> 30) & 0x3],
				pinloc2[(r >> 28) & 0x3],
				pinloc[(r >> 24) & 0xf],
				pinfunc[(r >> 20) & 0xf],
				pincol[(r >> 12) & 0xf]
			);
			
		}
	}
	return k;
}


static int
hdastart(Ctlr *ctlr)
{
	static int cmdbufsize[] = { 2, 16, 256, 2048 };
	int n, size;
	
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
	
	/* stop command buffers */
	csr16(ctlr, Wakeen) = 0;
	csr32(ctlr, Intctl) = 0;
	csr8(ctlr, Corbctl) = 0;
	csr8(ctlr, Rirbctl) = 0;
	waitup8(ctlr, Corbctl, Corbdma, 0);
	waitup8(ctlr, Rirbctl, Rirbdma, 0);
	
	/* reset controller */
	csr32(ctlr, Gctl) = 0;
	waitup32(ctlr, Gctl, Rst, 0);
	microdelay(Codecdelay);
	csr32(ctlr, Gctl) = Rst;
	waitup32(ctlr, Gctl, Rst, Rst);
	
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
	csr32(ctlr, Intctl) = Gie | Cie;
	
	return 0;
}

static Pcidev*
hdamatch(Pcidev *p)
{
	while(p = pcimatch(p, 0, 0))
		switch((p->vid << 16) | p->did){
		case (0x8086 << 16) | 0x27d8:
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
hdareset(Audio *adev)
{
	static Ctlr *cards = nil;
	Pcidev *p;
	int irq, tbdf, best;
	Ctlr *ctlr;

	/* make a list of all ac97 cards if not already done */
	if(cards == nil){
		p = nil;
		while(p = hdamatch(p)){
			ctlr = xspanalloc(sizeof(Ctlr), 8, 0);
			memset(ctlr, 0, sizeof(Ctlr));
			ctlr->pcidev = p;
			ctlr->next = cards;
			cards = ctlr;
		}
	}

	/* pick a card from the list */
	for(ctlr = cards; ctlr; ctlr = ctlr->next){
		if(p = ctlr->pcidev){
			ctlr->pcidev = nil;
			goto Found;
		}
	}
	return -1;

Found:
	adev->ctlr = ctlr;
	ctlr->adev = adev;

	irq = p->intl;
	tbdf = p->tbdf;

	pcisetbme(p);
	pcisetpms(p, 0);
	
	ctlr->no = adev->ctlrno;
	ctlr->size = p->mem[0].size;
	ctlr->q = qopen(256, 0, 0, 0);
	ctlr->mem = vmap(p->mem[0].bar & ~0x0F, ctlr->size);
	if(ctlr->mem == nil){
		print("#A%d: can't map %.8lux\n", ctlr->no, p->mem[0].bar);
		return -1;
	}
	print("#A%d: hda mem %p irq %d\n", ctlr->no, ctlr->mem, irq);

	if(hdastart(ctlr) < 0){
		print("#A%d: unable to start hda\n", ctlr->no);
		return -1;
	}
	if(streamalloc(ctlr) < 0){
		print("#A%d: unable to allocate stream buffer\n", ctlr->no);
		return -1;
	}
	if(enumdev(ctlr) < 0){
		print("#A%d: no audio codecs found\n", ctlr->no);
		return -1;
	}
	print("#A%d: using codec #%d, vendor %08x\n",
		ctlr->no, ctlr->codec.id.codec, ctlr->codec.vid);
	
	best = bestpin(ctlr);
	if(best < 0){
		print("#A%d: no output pins found!\n", ctlr->no);
		return -1;
	}
	if(connectpin(ctlr, best) < 0){
		print("#A%d: error connecting pin\n", ctlr->no);
		return -1;
	}

	adev->write = hdawrite;
	adev->close = hdaclose;
	adev->buffered = hdabuffered;
	adev->volread = hdavolread;
	adev->volwrite = hdavolwrite;
	adev->status = hdastatus;
	adev->ctl = hdactl;
	
	intrenable(irq, hdainterrupt, adev, tbdf, "hda");
	lastcard = ctlr;
	addarchfile("hdacmd", 0664, hdacmdread, hdacmdwrite);
	
	return 0;
}

void
audiohdalink(void)
{
	addaudiocard("hda", hdareset);
}

