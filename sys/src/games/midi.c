#include <u.h>
#include <libc.h>

enum { SAMPLE = 44100 };

struct Tracker {
	uchar *data;
	char ended;
	uvlong t;
	uchar notes[16][128];
	int cmd;
} *tr;

typedef struct Tracker Tracker;

int fd, ofd, div, tempo = 500000, ntrack;
uvlong T;
int freq[128];

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

int
get8(Tracker *src)
{
	uchar c;

	if(src == nil){
		if(read(fd, &c, 1) == 0)
			sysfatal("unexpected eof");
		return c;
	}
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

uvlong
tconv(int n)
{
	uvlong v;
	
	v = n;
	v *= tempo;
	v *= SAMPLE;
	v /= div;
	v /= 1000000;
	return v;
}

void
run(uvlong n)
{
	int samp, j, k, l, no[128];
	uchar *s;
	int t, f;
	short u;
	Tracker *x;
	
	samp = n - T;
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
	s = emallocz(samp * 4);
	for(l = 0; l < samp; l++){
		t = 0;
		for(k = 0; k < 128; k++){
			f = (T % freq[k]) >= freq[k]/2 ? 1 : 0;
			t += f * no[k];
		}
		u = t*10;
		s[4 * l] = s[4 * l + 2] = u;
		s[4 * l + 1] = s[4 * l + 3] = u >> 8;
		T++;
	}
	write(ofd, s, samp * 4);
	free(s);
}

void
readevent(Tracker *src)
{
	uvlong l;
	int n,t;
	
	l = tconv(getvar(src));
	run(src->t += l);
	t = get8(src);
	if((t & 0x80) == 0){
		src->data--;
		t = src->cmd;
		if((t & 0x80) == 0)
			sysfatal("invalid midi");
	}else
		src->cmd = t;
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
	case 0xB:
		get16(src);
		break;
	case 0xC:
		get8(src);
		break;
	case 0xF:
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
			write(1, src->data, n);
			skip(src, n);
			break;
		default:
			print("unknown meta event type %.2x\n", t);
		case 3: case 1: case 2: case 0x58: case 0x59: case 0x21:
			skip(src, n);
		}
		break;
	default:
		sysfatal("unknown event type %x", t>>4);
	}
}

void
main(int argc, char **argv)
{
	int i, size;
	uvlong t, mint;
	Tracker *x, *minx;

	if(argc != 2)
		sysfatal("invalid arguments");
	fd = open(argv[1], OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	ofd = open("/dev/audio", OWRITE);
	if(ofd < 0)
		sysfatal("ofd: %r");
	if(get32(nil) != 0x4D546864 || get32(nil) != 6)
		sysfatal("invalid file header");
	get16(nil);
	ntrack = get16(nil);
	div = get16(nil);
	tr = emallocz(ntrack * sizeof(*tr));
	for(i = 0; i < ntrack; i++){
		if(get32(nil) != 0x4D54726B)
			sysfatal("invalid track header");
		size = get32(nil);
		tr[i].data = emallocz(size);
		read(fd, tr[i].data, size);
	}
	for(i = 0; i < 128; i++)
		freq[i] = SAMPLE / (440 * pow(1.05946, i - 69));
	for(;;){
		minx = nil;
		mint = 0;
		for(x = tr; x < tr + ntrack; x++){
			if(x->ended)
				continue;
			t = tconv(peekvar(x)) + x->t;
			if(t < mint || minx == nil){
				mint = t;
				minx = x;
			}
		}
		if(minx == nil)
			exits(nil);
		readevent(minx);
	}
}
