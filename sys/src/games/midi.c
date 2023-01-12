#include <u.h>
#include <libc.h>

enum { SAMPLE = 44100 };

struct Tracker {
	uchar *data;
	char ended;
	vlong t;
	vlong Δ;
	uchar notes[16][128];
	int cmd;
} *tr;

typedef struct Tracker Tracker;

int debug;
int fd, ofd, div, tempo = 500000, ntrack;
int freq[128];
uchar out[8192], *outp = out;

void *
emallocz(int size)
{
	void *v;
	
	v = malloc(size);
	if(v == nil)
		sysfatal("malloc: %r");
	memset(v, 0, size);
	return v;
}

void
dprint(char *fmt, ...)
{
	char s[256];
	va_list arg;

	if(!debug)
		return;
	va_start(arg, fmt);
	vseprint(s, s+sizeof s, fmt, arg);
	va_end(arg);
	fprint(2, "%s", s);
}

int
get8(Tracker *src)
{
	uchar c;

	if(src == nil){
		if(read(fd, &c, 1) == 0)
			sysfatal("unexpected eof");
		return c;
	}
	dprint("%#p:%02ux", src, *src->data);
	return *src->data++;
}

int
get16(Tracker *src)
{
	int x;
	
	x = get8(src) << 8;
	return x | get8(src);
}

int
get32(Tracker *src)
{
	int x;
	x = get16(src) << 16;
	return x | get16(src);
}

int
getvar(Tracker *src)
{
	int k, x;

	x = get8(src);
	k = x & 0x7F;
	while(x & 0x80){
		k <<= 7;
		x = get8(src);
		k |= x & 0x7F;
	}
	return k;
}

int
peekvar(Tracker *src)
{
	uchar *p;
	int v;
	
	p = src->data;
	v = getvar(src);
	src->data = p;
	return v;
}

void
skip(Tracker *src, int x)
{
	if(x) do
		get8(src);
	while(--x);
}

double
tconv(int n)
{
	double v;
	
	v = n;
	v *= tempo;
	v *= SAMPLE;
	v /= div;
	v /= 1000000;
	return v;
}

void
run(double n)
{
	int j, k, no[128];
	int t, f;
	short u;
	uvlong samp;
	double Δ;
	Tracker *x;
	static double T, ε;
	static uvlong τ;

	Δ = tconv(n) + ε;
	samp = Δ;
	ε = Δ - samp;
	if(samp <= 0)
		return;
	memset(no, 0, sizeof no);
	for(x = tr; x < tr + ntrack; x++){
		if(x->ended)
			continue;
		for(j = 0; j < 16; j++)
			for(k = 0; k < 128; k++)
				no[k] += x->notes[j][k];
	}
	while(samp--){
		t = 0;
		for(k = 0; k < 128; k++){
			f = (τ % freq[k]) >= freq[k]/2 ? 1 : 0;
			t += f * no[k];
		}
		u = t*10;
		outp[0] = outp[2] = u;
		outp[1] = outp[3] = u >> 8;
		outp += 4;
		if(outp == out + sizeof out){
			write(ofd, out, sizeof out);
			outp = out;
		}
		τ++;
	}
}

void
readevent(Tracker *src)
{
	int n,t;

	dprint(" [%zd] ", src - tr);
	t = get8(src);
	if((t & 0x80) == 0){
		src->data--;
		t = src->cmd;
		if((t & 0x80) == 0)
			sysfatal("invalid midi");
	}else
		src->cmd = t;
	dprint("(%02ux) ", t >> 4);
	switch(t >> 4){
	case 0x8:
		n = get8(src);
		get8(src);
		src->notes[t & 15][n] = 0;
		break;
	case 0x9:
		n = get8(src);
		src->notes[t & 15][n] = get8(src);
		break;
	case 0xA:
	case 0xD:
	case 0xC:
		get8(src);
		break;
	case 0xB:
	case 0xE:
		get16(src);
		break;
	case 0xF:
		if((t & 0xF) == 0){
			while(get8(src) != 0xF7)
				;
			return;
		}
		t = get8(src);
		n = get8(src);
		switch(t){
		case 0x2F:
			src->ended = 1;
			break;
		case 0x51:
			tempo = get16(src) << 8;
			tempo |= get8(src);
			break;
		case 5:
			write(2, src->data, n);
			skip(src, n);
			break;
		default:
			dprint("unknown meta event type %.2x\n", t);
		case 3: case 1: case 2: case 0x58: case 0x59: case 0x21:
			skip(src, n);
		}
		break;
	default:
		sysfatal("unknown event type %x", t>>4);
	}
	dprint("\n");
}

void
main(int argc, char **argv)
{
	int i, size, end;
	uvlong z;
	Tracker *x;

	ARGBEGIN{
	case 'D':
		debug = 1;
		break;
	case 'c':
		ofd = 1;
		break;
	}ARGEND;
	if(*argv != nil)
		fd = open(*argv, OREAD);
	if(ofd == 0)
		ofd = open("/dev/audio", OWRITE);
	if(fd < 0 || ofd < 0)
		sysfatal("open: %r");
	if(get32(nil) != 0x4D546864 || get32(nil) != 6)
		sysfatal("invalid file header");
	get16(nil);
	ntrack = get16(nil);
	div = get16(nil);
	tr = emallocz(ntrack * sizeof(*tr));
	for(x=tr, z=-1UL; x<tr+ntrack; x++){
		if(get32(nil) != 0x4D54726B)
			sysfatal("invalid track header");
		size = get32(nil);
		x->data = emallocz(size);
		readn(fd, x->data, size);
		x->Δ = getvar(x);	/* prearm */
		if(x->Δ < z)
			z = x->Δ;
	}
	for(x=tr; x<tr+ntrack; x++)
		x->Δ -= z;
	for(i = 0; i < 128; i++)
		freq[i] = SAMPLE / (440 * pow(1.05946, i - 69));
	for(end=0; !end;){
		end = 1;
		for(x=tr; x<tr+ntrack; x++){
			if(x->ended)
				continue;
			end = 0;
			x->Δ--;
			x->t += tconv(1);
			while(x->Δ <= 0){
				readevent(x);
				if(x->ended)
					break;
				x->Δ = getvar(x);
			}
		}
		run(1);
	}
	exits(nil);
}
