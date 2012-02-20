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
	Rendez r;

	uchar	*buf;
	ulong	nbuf;

	ulong	ri;
	ulong	wi;
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

/* hardware structures */

struct Bld {
	ulong addrlo;
	ulong addrhi;
	ulong len;
	ulong flags;
};

struct Ctlr {
	Ctlr *next;
	uint no;

	Lock;			/* interrupt lock */
	QLock;			/* command lock */

	Audio *adev;
	Pcidev *pcidev;
	
	uchar *mem;
	ulong size;
	
	Queue *q;
	ulong *corb;
	ulong corbsize;
	ulong *rirb;
	ulong rirbsize;
	
	uint sdctl;
	uint sdintr;
	uint sdnum;

	Bld *blds;

	Ring ring;

	uint iss, oss, bss;

	uint codecmask;	
	Codec *codec[Maxcodecs];

	Widget *amp, *src;
	uint pin;
	uint cad;

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
			if(v == nil || v->from)
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
addconn(Widget *w, uint nid)
{
	Widget *src;

	if(nid >= Maxwidgets)
		return;
	if((src = w->fg->codec->widgets[nid]) == nil)
		return;
	for(nid=0; nid<w->nlist; nid++)
		if(w->list[nid] == src)
			return;
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
		if(i && ((r & m) != y))
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
	Widget *w, **tail;
	uint i, r, n, base;

	r = cmd(id, Getparm, Fungrtype) & 0x7f;
	if(r != Graudio)
		return nil;

	fg = mallocz(sizeof *fg, 1);
	if(fg == nil){
Nomem:
		print("hda: enumfungroup: out of memory\n");
		return nil;
	}
	fg->codec = codec;
	fg->id = id;
	fg->type = r;

	r = cmd(id, Getparm, Subnodecnt);
	n = r & 0xff;
	base = (r >> 16) & 0xff;
	
	if(base + n > Maxwidgets){
		free(fg);
		return nil;
	}

	tail = &fg->first;
	for(i=0; i<n; i++){
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
		codec->widgets[base + i] = w;
		*tail = w;
		tail = &w->next;
	}

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

	print("#A%d: codec #%d, vendor %08x, rev %08x\n",
		id.ctlr->no, codec->id.codec, codec->vid, codec->rid);

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
connectpin(Ctlr *ctlr, uint pin, uint cad)
{
	Widget *src, *dst;

	if(cad >= Maxcodecs || pin >= Maxwidgets || ctlr->codec[cad] == nil)
		return -1;
	src = ctlr->codec[cad]->widgets[pin];
	if(src == nil)
		return -1;
	if(src->type != Wpin)
		return -1;
	if((src->pincap & Pout) == 0)
		return -1;
	dst = findpath(src);
	if(!dst)
		return -1;
	connectpath(src, dst, ctlr->atag);
	ctlr->amp = dst;
	ctlr->src = src;
	ctlr->pin = pin;
	ctlr->cad = cad;
	return 0;
}

static int
bestpin(Ctlr *ctlr, int *pcad)
{
	Fungroup *fg;
	Widget *w;
	int best, pin, score;
	uint r;
	int i;

	pin = -1;
	best = -1;
	for(i=0; i<Maxcodecs; i++){
		if(ctlr->codec[i] == nil)
			continue;
		for(fg=ctlr->codec[i]->fgroup; fg; fg=fg->next){
			for(w=fg->first; w; w=w->next){
				if(w->type != Wpin)
					continue;
				if((w->pincap & Pout) == 0)
					continue;
				score = 0;
				r = w->pin;
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
				if(score >= best){
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
streamalloc(Ctlr *ctlr)
{
	Ring *r;
	int i;

	r = &ctlr->ring;
	memset(r, 0, sizeof(*r));
	r->buf = xspanalloc(r->nbuf = Bufsize, 128, 0);
	ctlr->blds = xspanalloc(Nblocks * sizeof(Bld), 128, 0);
	if(r->buf == nil || ctlr->blds == nil){
		print("hda: no memory for stream\n");
		return -1;
	}
	for(i=0; i<Nblocks; i++){
		ctlr->blds[i].addrlo = PADDR(r->buf) + i*Blocksize;
		ctlr->blds[i].addrhi = 0;
		ctlr->blds[i].len = Blocksize;
		ctlr->blds[i].flags = 0x01;	/* interrupt on completion */
	}

	/* output dma engine starts after inputs */
	ctlr->sdnum = ctlr->iss;
	ctlr->sdctl = Sdctl0 + ctlr->sdnum*0x20;
	ctlr->sdintr = 1<<ctlr->sdnum;
	ctlr->atag = ctlr->sdnum+1;
	ctlr->afmt = Fmtstereo | Fmtsampw | Fmtdiv1 | Fmtmul1 | Fmtbase441;
	ctlr->active = 0;

	/* perform reset */
	csr8(ctlr, ctlr->sdctl) &= ~(Srst | Srun | Scie | Seie | Sdie);
	csr8(ctlr, ctlr->sdctl) |= Srst;
	microdelay(Codecdelay);
	waitup8(ctlr, ctlr->sdctl, Srst, Srst);
	csr8(ctlr, ctlr->sdctl) &= ~Srst;
	microdelay(Codecdelay);
	waitup8(ctlr, ctlr->sdctl, Srst, 0);

	/* set stream number */
	csr32(ctlr, ctlr->sdctl) = (ctlr->atag << Stagbit) |
		(csr32(ctlr, ctlr->sdctl) & ~(0xF << Stagbit));

	/* set stream format */
	csr16(ctlr, Sdfmt+ctlr->sdctl) = ctlr->afmt;

	/* program stream DMA & parms */
	csr32(ctlr, Sdbdplo+ctlr->sdctl) = PADDR(ctlr->blds);
	csr32(ctlr, Sdbdphi+ctlr->sdctl) = 0;
	csr32(ctlr, Sdcbl+ctlr->sdctl) = r->nbuf;
	csr16(ctlr, Sdlvi+ctlr->sdctl) = (Nblocks - 1) & 0xff;

	/* mask out ints */
	csr8(ctlr, Sdsts+ctlr->sdctl) = Scompl | Sfifoerr | Sdescerr;

	/* enable global intrs for this stream */
	csr32(ctlr, Intctl) |= ctlr->sdintr;
	csr8(ctlr, ctlr->sdctl) |= Scie | Seie | Sdie;

	return 0;
}

static void
streamstart(Ctlr *ctlr)
{
	ctlr->active = 1;
	
	csr8(ctlr, ctlr->sdctl) |= Srun;
	waitup8(ctlr, ctlr->sdctl, Srun, Srun);
}

static void
streamstop(Ctlr *ctlr)
{
	csr8(ctlr, ctlr->sdctl) &= ~Srun;
	waitup8(ctlr, ctlr->sdctl, Srun, 0);

	ctlr->active = 0;
}

static uint
streampos(Ctlr *ctlr)
{
	Ring *r;
	uint p;

	r = &ctlr->ring;
	p = csr32(ctlr, Sdlpib+ctlr->sdctl);
	if(p >= r->nbuf)
		p = 0;
	return p;
}

static long
hdactl(Audio *adev, void *va, long n, vlong)
{
	char *p, *e, *x, *tok[4];
	int ntok;
	Ctlr *ctlr;
	uint pin, cad;
	
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
		if(cistrcmp(tok[0], "pin") == 0 && ntok >= 2){
			cad = ctlr->cad;
			pin = strtoul(tok[1], 0, 0);
			if(ntok > 2)
				cad = strtoul(tok[2], 0, 0);
			connectpin(ctlr, pin, cad);
		}else
			error(Ebadctl);
	}
	return n;
}

static int
outavail(void *arg)
{
	Ctlr *ctlr = arg;
	return available(&ctlr->ring) > 0;
}

static int
outrate(void *arg)
{
	Ctlr *ctlr = arg;
	int delay = ctlr->adev->delay*BytesPerSample;
	return (delay <= 0) || (buffered(&ctlr->ring) <= delay) || (ctlr->active == 0);
}

static long
hdabuffered(Audio *adev)
{
	Ctlr *ctlr;
	ctlr = adev->ctlr;
	return buffered(&ctlr->ring);
}

static void
hdakick(Ctlr *ctlr)
{
	if(ctlr->active)
		return;
	if(buffered(&ctlr->ring) > Blocksize)
		streamstart(ctlr);
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
	ring = &ctlr->ring;
	while(p < e) {
		if((n = writering(ring, p, e - p)) <= 0){
			hdakick(ctlr);
			sleep(&ring->r, outavail, ctlr);
			continue;
		}
		p += n;
	}
	hdakick(ctlr);
	sleep(&ring->r, outrate, ctlr);
	return p - (uchar*)vp;
}

static void
hdaclose(Audio *adev)
{
	Ctlr *ctlr;
	uchar z[1];
	Ring *r;

	ctlr = adev->ctlr;
	if(!ctlr->active)
		return;
	z[0] = 0;
	r = &ctlr->ring;
	while(r->wi % Blocksize)
		hdawrite(adev, z, sizeof(z), 0);
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
	Ring *r;

	adev = arg;
	ctlr = adev->ctlr;
	
	ilock(ctlr);
	sts = csr32(ctlr, Intsts);
	if(sts & ctlr->sdintr){
		/* ack interrupt */
		csr8(ctlr, Sdsts+ctlr->sdctl) |= Scompl;
		r = &ctlr->ring;
		r->ri = streampos(ctlr);
		if(ctlr->active && buffered(r) < Blocksize){
			streamstop(ctlr);
			r->ri = r->wi = streampos(ctlr);
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
	Fungroup *fg;
	Widget *w;
	uint r;
	int k, i;
	char *s;
	
	s = a;
	k = snprint(s, n, "bufsize %6d buffered %6ld\n", Blocksize, buffered(&ctlr->ring));
	for(i=0; i<Maxcodecs; i++){
		if((codec = ctlr->codec[i]) == nil)
			continue;
		k += snprint(s+k, n-k, "codec %2d pin %3d\n",
			codec->id.codec, ctlr->pin);
		for(fg=codec->fgroup; fg; fg=fg->next){
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
	}
	return k;
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
		case (0x8086 << 16) | 0x27d8:
		case (0x8086 << 16) | 0x284b:	/* Intel ICH8 */
		case (0x1002 << 16) | 0x4383:	/* ATI */
		case (0x1002 << 16) | 0x7919:	/* ATI HDMI */
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
	int irq, tbdf, best, cad;
	Ctlr *ctlr;
	Pcidev *p;

	/* make a list of all cards if not already done */
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

	if(p->vid == 0x1002){
		/* magic for ATI */
		pcicfgw8(p, 0x42, pcicfgr8(p, 0x42) | 2);
	} else {
		/* TCSEL */
		pcicfgw8(p, 0x44, pcicfgr8(p, 0x44) & 0xf8);
	}

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
		print("#A%d: streamalloc failed\n", ctlr->no);
		return -1;
	}
	if(enumdev(ctlr) < 0){
		print("#A%d: no audio codecs found\n", ctlr->no);
		return -1;
	}
	best = bestpin(ctlr, &cad);
	if(best < 0){
		print("#A%d: no output pins found!\n", ctlr->no);
		return -1;
	}
	if(connectpin(ctlr, best, cad) < 0){
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

