#include <u.h>
#include <libc.h>
#include <bio.h>
#include "midifile.h"

Track *tracks;
Chan chan[Nchan];
Inst *inst;
int mfmt, ntrk, div = 1, tempo = 500000;
int samprate = Rate;
int trace, stream, writeback;
vlong tic;
Biobuf *inbf, *outbf;
int rate = Rate;

void *
emalloc(ulong n)
{
	void *p;

	p = mallocz(n, 1);
	if(p == nil)
		sysfatal("mallocz: %r");
	setmalloctag(p, getcallerpc(&n));
	return p;
}

void *
erealloc(void *p, usize n, usize oldn)
{
	if((p = realloc(p, n)) == nil)
		sysfatal("realloc: %r");
	setrealloctag(p, getcallerpc(&p));
	if(n > oldn)
		memset((uchar *)p + oldn, 0, n - oldn);
	return p;
}

void
dprint(char *fmt, ...)
{
	char s[256];
	va_list arg;

	if(!trace)
		return;
	va_start(arg, fmt);
	vseprint(s, s+sizeof s, fmt, arg);
	va_end(arg);
	fprint(2, "%s", s);
}

Biobuf *
efdopen(int fd, int mode)
{
	Biobuf *bf;

	if((bf = Bfdopen(fd, mode)) == nil)
		sysfatal("efdopen: %r");
	Blethal(bf, nil);
	return bf;
}

Biobuf *
eopen(char *file, int mode)
{
	int fd;

	if((fd = open(file, mode)) < 0)
		sysfatal("eopen: %r");
	return efdopen(fd, mode);
}

u8int
get8(Track *t)
{
	u8int v;

	if(t == nil || t->cur == nil || t->buf == nil)
		Bread(inbf, &v, 1);
	else{
		if(t->cur >= t->buf + t->bufsz || t->ended)
			sysfatal("track overflow");
		v = *t->cur++;
	}
	return v;
}

u16int
get16(Track *t)
{
	u16int v;

	v = get8(t) << 8;
	return v | get8(t);
}

u32int
get32(Track *t)
{
	u32int v;

	v = get16(t) << 16;
	return v | get16(t);
}

static void
growmid(Track *t, int n)
{
	usize cur, run;

	/* one extra byte for delay prefetch */
	if(t->cur + n + 1 < t->buf + t->bufsz)
		return;
	cur = t->cur - t->buf;
	run = t->run - t->buf;
	if(n < 8192)
		n = 8192;
	t->buf = erealloc(t->buf, t->bufsz + n + 1, t->bufsz);
	t->bufsz += n + 1;
	t->cur = t->buf + cur;
	t->run = t->buf + run;
}

static void
put8(Track *t, u8int v)
{
	if(t != nil){
		growmid(t, 1);
		*t->cur++ = v;
	}else
		Bwrite(outbf, &v, 1);
}

static void
put16(Track *t, u16int v)
{
	put8(t, v >> 8);
	put8(t, v);
}

static void
put32(Track *t, u32int v)
{
	put16(t, v >> 16);
	put16(t, v);
}

static void
putvar(Track *t)
{
	int w;
	uchar u[4], *p;

	p = u + nelem(u) - 1;
	w = t->Δ;
	if(w & 1<<31)
		sysfatal("invalid variable-length number %08ux", w);
	*p-- = w;
	while(w >= 0x80){
		w >>= 8;
		*p-- = w;
	}
	Bwrite(outbf, p, u + sizeof(u) - 1 - p);
}

static int
getvar(Track *t)
{
	int n, v, w;

	w = get8(t);
	v = w & 0x7f;
	for(n=0; w&0x80; n++){
		if(n == 3)
			sysfatal("invalid variable-length number");
		v <<= 7;
		w = get8(t);
		v |= w & 0x7f;
	}
	return v;
}

u32int
peekvar(Track *t)
{
	uchar *cur;
	uint v;

	cur = t->cur;
	v = getvar(t);
	t->cur = cur;
	return v;
}

static void
skip(Track *t, int n)
{
	while(n-- > 0)
		get8(t);
}

double
delay(double n)
{
	return (n * tempo * samprate / div) / 1e6;
}

vlong
ns2tic(double n)
{
	return n * div * 1e3 / tempo;
}

int
nextev(Track *t)
{
	int e;

	if(writeback)
		putvar(t);
	t->run = t->cur;
	e = get8(t);
	if((e & 0x80) == 0){
		if(t->cur != nil){
			t->cur--;
			t->run--;
		}
		e = t->latch;
		if((e & 0x80) == 0)
			sysfatal("invalid event %#ux", e);
	}else
		t->latch = e;
	return e;
}

static void
newmsg(Msg *m, int c, int type, int arg1, int arg2, usize size)
{
	m->chan = chan + c;
	m->type = type;
	m->arg1 = arg1;
	m->arg2 = arg2;
	m->size = size;
}
void
translate(Track *t, int e, Msg *msg)
{
	int c, n, m, type;
	uchar *p;

	c = e & 0xf;
	dprint("Δ %.2f ch%02d ", t->Δ, c);
	n = get8(t);
	m = -1;
	type = Cunknown;
	switch(e >> 4){
	case 0x8:
		m = get8(t);
		dprint("note off\t%02ux\taftertouch\t%02ux", n, m);
		type = Cnoteoff;
		break;
	case 0x9:
		m = get8(t);
		dprint("note on\t%02ux\tvelocity\t%02ux", n, m);
		type = Cnoteon;
		break;
	case 0xb:
		m = get8(t);
		dprint("control change: ");
		switch(n){
		case 0x00:
			dprint("bank select msb\t%02ux", m);
			type = Cbankmsb;
			break;
		case 0x07:
			dprint("channel volume\t%02ux", m);
			chan[c].vol = m;
			type = Cchanvol;
			break;
		case 0x0a:
			dprint("pan\t%02ux", m);
			chan[c].pan = m < 32 ? 1<<4 : m > 96 ? 1<<5 : 3<<4; 
			type = Cpan;
			break;
		default:
			dprint("unknown controller %.4ux", n);
			break;
		}
		break;
	case 0xc:
		dprint("program change\t%02ux", n);
		type = Cprogram;
		break;
	case 0xe:
		n = (get8(t) << 7 | n) - 0x4000 / 2;
		chan[c].bend = n;
		dprint("pitch bend\t%02x", n);
		type = Cpitchbend;
		break;
	case 0xf:
		dprint("sysex:\t");
		if((e & 0xf) == 0){
			m = 0;
			while(get8(t) != 0xf7)
				m++;
			fprint(2, "sysex n %d m %d\n", n, m);
			type = Csysex;
			break;
		}
		m = get8(t);
		switch(n){
		case 0x2f:
			dprint("... so long!");
			t->ended = 1;
			type = Ceot;
			break;
		case 0x51:
			tempo = get16(t) << 8;
			tempo |= get8(t);
			dprint("tempo change\t%d", tempo);
			type = Ctempo;
			break;
		default:
			dprint("skipping unhandled event %02ux", n);
			skip(t, m);
			break;
		}
		break;
	case 0xa:
		m = get8(t);
		dprint("polyphonic key pressure/aftertouch\t%02ux\t%02ux", n, m);
		type = Ckeyafter;
		break;
	case 0xd:
		m = get8(t);
		dprint("channel pressure/aftertouch\t%02ux\t%02ux", n, m);
		type = Cchanafter;
		break;
	default: sysfatal("invalid event %#ux", e >> 4);
	}
	newmsg(msg, c, type, n, m, t->cur - t->run);
	dprint("\t[");
	for(p=t->run; p<t->cur; p++)
		dprint("%02ux", *p);
	dprint("]\n");
}

void
writemid(char *file)
{
	u32int n;
	Track *t;

	outbf = file == nil ? efdopen(1, OWRITE) : eopen(file, OWRITE);
	put32(nil, 0x4d546864);	/* MThd */
	put32(nil, 6);
	put16(nil, mfmt);
	put16(nil, ntrk);
	put16(nil, div);
	for(t=tracks; t<tracks+ntrk; t++){
		put32(nil, 0x4d54726b);	/* MTrack */
		n = t->cur - t->buf;
		put32(nil, n);
		Bwrite(outbf, t->buf, n);
	}
	Bterm(outbf);
	outbf = nil;
}

int
readmid(char *file)
{
	u32int n, z;
	Track *t;

	inbf = file == nil ? efdopen(0, OREAD) : eopen(file, OREAD);
	if(stream){
		mfmt = 0;
		ntrk = 1;
		tracks = emalloc(ntrk * sizeof *tracks);
		return 0;
	}
	if(get32(nil) != 0x4d546864 || get32(nil) != 6){
		werrstr("invalid header");
		return -1;
	}
	mfmt = get16(nil);
	ntrk = get16(nil);
	if(ntrk == 1)
		mfmt = 0;
	if(mfmt < 0 || mfmt > 1){
		werrstr("unsupported format %d", mfmt);
		return -1;
	}
	div = get16(nil);
	tracks = emalloc(ntrk * sizeof *tracks);
	for(t=tracks, z=-1UL; t<tracks+ntrk; t++){
		if(get32(nil) != 0x4d54726b){
			werrstr("invalid track");
			return -1;
		}
		n = get32(nil);
		growmid(t, n);
		Bread(inbf, t->buf, n);
		t->Δ = getvar(t);	/* prearm */
		if(t->Δ < z)
			z = t->Δ;
	}
	for(t=tracks; t<tracks+ntrk; t++)
		t->Δ -= z;
	Bterm(inbf);
	inbf = nil;
	return 0;
}

void
evloop(void)
{
	int end;
	Track *t;

	if(stream){
		for(t=tracks;;){
			t->Δ = getvar(t);
			event(t);
			if(t->ended)
				return;
			samp(1);
			tic++;
		}
	}
	for(;;){
		end = 1;
		for(t=tracks; t<tracks+ntrk; t++){
			if(t->ended)
				continue;
			end = 0;
			t->Δ--;
			t->t += delay(1);
			while(t->Δ <= 0){
				event(t);
				if(t->ended)
					break;
				t->Δ = getvar(t);
			}
		}
		if(end)
			break;
		samp(1);
		tic++;
	}
}

void
initmid(void)
{
	Chan *c;

	for(c=chan; c<chan+nelem(chan); c++){
		c->vol = 0x5a;
		c->bend = 0;
		c->pan = 3<<4;
	}
}
