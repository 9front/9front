#include <u.h>
#include <libc.h>
#include <bio.h>

enum{
	NRUNES = 1<<21
};

typedef struct Param Param;
typedef struct Lvl Lvl;
struct Lvl{
	int bits;
	int max;
	int mask;
};
struct Param{
	Lvl idx1;
	Lvl idx2;
	Lvl data;

	int round1max;
};

static void
derive(Lvl *l)
{
	l->max = 1 << l->bits;
	l->mask = l->max - 1;
}

static void
param(Param *p, int idx1, int idx2)
{

	assert(idx1 + idx2 < 21);
	p->idx1.bits = idx1;
	p->idx2.bits = idx2;
	p->data.bits = 21 - idx1 - idx2;
	derive(&p->idx1);
	derive(&p->idx2);
	derive(&p->data);

	p->round1max = NRUNES/p->data.max;
}

static int
lkup(Param *p, int *idx1, int *idx2, int *data, int x)
{
	int y, z;

	y = (((x)>>(p->data.bits+p->idx2.bits))&p->idx1.mask);
	z = (((x)>>p->data.bits)&p->idx2.mask);
	return data[idx2[idx1[y] + z] + (x&p->data.mask)];
}

static int
mkarrvar(int fd, char *name, int *d, int len)
{
	int i, sz;
	int max, min;
	char *t;

	max = min = 0;
	for(i = 0; i < len; i++){
		if(d[i] > max)
			max = d[i];
		if(d[i] < min)
			min = d[i];
	}
	if(min == 0){
		if(max < 0xFF)
			t = "uchar", sz = 1;
		else if(max < 0xFFFF)
			t = "ushort", sz = 2;
		else
			t = "uint", sz = 4;
	} else {
		if(max < 1<<7)
			t = "char", sz = 1;
		else if(max < 1<<15)
			t = "short", sz = 2;
		else
			t = "int", sz = 4;
	}
	if(fd < 0)
		return sz * len;

	fprint(fd, "static\n%s\t%s[%d] =\n{\n\t", t, name, len);
	for(i = 0; i < len; i++){
		fprint(fd, "%d,", d[i]);
		if((i+1) % 16 == 0)
			fprint(fd, "\n\t");
	}
	fprint(fd, "\n};\n");

	return sz * len;
}

static int
mkexceptarr(int fd, char *name, int *d, int n, int all)
{
	int i;
	fprint(fd, "static\nRune %s[][%d] =\n{\n\t", name, all ? 3 : 2);
	for(i = 0; i < n*3; i += 3){
		if(all && d[i] != 0)
			fprint(fd, "{0x%X, 0x%X, 0x%X},", d[i], d[i+1], d[i+2]);
		else if(!all)
			fprint(fd, "{0x%X, 0x%X},", d[i+1], d[i+2]);	
		if((i+3) % (8*3) == 0)
			fprint(fd, "\n\t");
	}
	fprint(fd, "\n};\n");
	return n * sizeof(Rune) * 2;
}

static int
compact(int *data, int *idx, int nidx, int *src, int chunksize)
{
	int i, n, ndata, best;
	int *dot, *lp, *rp;

	dot = src;
	ndata = 0;
	idx[0] = 0;
	for(i = 1; i <= nidx; i++){
		rp = dot + chunksize;
		lp = rp - 1;

		for(best = 0, n = 0; i != nidx && n < chunksize; n++, lp--){
			if(memcmp(lp, rp, (n+1) * sizeof data[0]) == 0)
				best = n+1;
		}
		memmove(data + ndata, dot, (chunksize - best) * sizeof data[0]);
		ndata += (chunksize - best);
		idx[i] = idx[i - 1] + (chunksize - best);
		dot = rp;
	}
	return ndata;
}


static int
mklkup(int fd, char *label, int *map, Param *p)
{
	static int data[NRUNES];
	static int idx2[NRUNES];
	static int idx2dest[NRUNES];
	static int idx1[NRUNES];
	int i, nidx2, ndata;
	int size;

	ndata = compact(data, idx2, p->round1max, map, p->data.max);
	nidx2 = compact(idx2dest, idx1, p->idx1.max, idx2, p->idx2.max);

	if(fd >= 0){
		for(i = 0; i < NRUNES; i++)
			if(map[i] != lkup(p, idx1, idx2dest, data, i))
				sysfatal("mismatch in %s at %d %d %d\n", label, i, map[i], lkup(p, idx1, idx2dest, data, i));
	}

	size = mkarrvar(fd, smprint("_%sdata", label), data, ndata);
	size += mkarrvar(fd, smprint("_%sidx2", label), idx2dest, nidx2);
	size += mkarrvar(fd, smprint("_%sidx1", label), idx1, p->idx1.max);
	if(fd >= 0){
		fprint(fd, "\n");
		fprint(fd, "#define %sindex1(x) (((x)>>(%d+%d))&0x%X)\n", label, p->data.bits, p->idx2.bits, p->idx1.mask);
		fprint(fd, "#define %sindex2(x) (((x)>>%d)&0x%X)\n", label, p->data.bits, p->idx2.mask);
		fprint(fd, "#define %soffset(x) ((x)&0x%X)\n", label, p->data.mask);
		fprint(fd, "#define %slkup(x) (_%sdata[_%sidx2[_%sidx1[%sindex1(x)] + %sindex2(x)] + %soffset(x)] )\n\n",
			label, label, label, label, label, label, label);
	}
	return size;
}

static void
mklkupmatrix(char *label, int *map, Param *p)
{
	int bestsize, size, bestx, besty;
	int x, y;

	bestsize = bestx = besty = -1;
	for(x = 4; x <= 12; x++)
		for(y=4; y <= (19 - x); y++){
			param(p, x, y);
			size = mklkup(-1, label, map, p);
			if(bestsize == -1 || size < bestsize){
				bestx = x;
				besty = y;
				bestsize = size;
			}
		}

	assert(bestsize != -1);
	fprint(2, "label: %s best: %d %d (%d)\n", label, bestx, besty, bestsize);
	param(p, bestx, besty);
}

static int myismerged[NRUNES];
static int mytoupper[NRUNES];
static int mytolower[NRUNES];
static int mytotitle[NRUNES];
static int mybreak[NRUNES];

enum{ DSTART = 0xEEEE };
static int mydecomp[NRUNES];
static int mydespecial[256*3];
static int nspecial;
static int myccc[NRUNES];

typedef struct KV KV;
struct KV{
	uint key;
	uint val;
	ushort next;
};

static KV myrecomp[2000];
static int nrecomp;

static int recompext[256*3];
static int nrecompext;

static uint
hash(uint x)
{
	x ^= x >> 16;
	x *= 0x21f0aaad;
	x ^= x >> 15;
	x *= 0xd35a2d97;
	x ^= x >> 15;
	return x;
}

static void
mkrecomp(int fd)
{
	int i;
	KV *p;
	static KV vals[512];
	static KV coll[1000];
	int over;
	int maxchain;

	for(i = 0; i < nelem(vals); i++)
		vals[i] = (KV){0, 0, 0};
	for(i = 0; i < nelem(coll); i++)
		coll[i] = (KV){0, 0, 0};
	over = 1;
	for(i = 0; i < nrecomp; i++){
		p = vals + (hash(myrecomp[i].key) % nelem(vals));
		maxchain = 0;
		while(p->key != 0){
			maxchain++;
			if(p->next == 0){
				p->next = over;
				p = coll + over - 1;
				over++;
			} else
				p = coll + p->next - 1;
		}
		p->key = myrecomp[i].key;
		p->val = myrecomp[i].val;
	}
	fprint(2, "recomp map [%d][%d]: %d\n", nelem(vals), over-1, (nelem(vals) + over-1) * (4+2+2));
	fprint(fd, "static\nuint\t_recompdata[] =\n{\n\t");
	for(p = vals, i = 0;; i++){
		assert(p->val < 0xFFFF);
		assert(p->next < 0xFFFF);
		fprint(fd, "%udU,%udU,", p->key, p->val | (p->next<<16));
		if((i+1) % 8 == 0)
			fprint(fd, "\n\t");

		if(p == vals+nelem(vals)-1)
			p = coll;
		else if(p == coll + over - 2)
			break;
		else
			p++;
	}
	fprint(fd, "\n};\n");
	fprint(fd, "static uint *_recompcoll = _recompdata+%d*2;\n", nelem(vals));
}

static void
mktables(void)
{
	Param p;
	int tofd, isfd, normfd, breakfd;
	int size;

	tofd = create("runetotypedata", OWRITE, 0664);
	if(tofd < 0)
		sysfatal("could not create runetotypedata: %r");
	param(&p, 10, 7);
	size = mklkup(tofd, "upper", mytoupper, &p);
	fprint(2, "%s: %d\n", "upper", size);

	size = mklkup(tofd, "lower", mytolower, &p);
	fprint(2, "%s: %d\n", "lower", size);

	size = mklkup(tofd, "title", mytotitle, &p);
	fprint(2, "%s: %d\n", "title", size);
	close(tofd);

	isfd = create("runeistypedata", OWRITE, 0664);
	if(isfd < 0)
		sysfatal("could not create runeistypedata: %r");
	param(&p, 11, 6);
	size = mklkup(isfd, "merged", myismerged, &p);
	fprint(2, "%s: %d\n", "merged", size);
	fprint(isfd, "static\nenum {\n");
	fprint(isfd, "\tL%s = %s,\n", "space", "1<<0");
	fprint(isfd, "\tL%s = %s,\n", "alpha", "1<<1");
	fprint(isfd, "\tL%s = %s,\n", "digit", "1<<2");
	fprint(isfd, "\tL%s = %s,\n", "upper", "1<<3");
	fprint(isfd, "\tL%s = %s,\n", "lower", "1<<4");
	fprint(isfd, "\tL%s = %s,\n", "title", "1<<5");
	fprint(isfd, "};\n");
	close(isfd);

	normfd = create("runenormdata", OWRITE, 0664);
	if(normfd < 0)
		sysfatal("could not create runenormdata: %r");
	param(&p, 10, 7);
	size = mklkup(normfd, "decomp", mydecomp, &p);
	fprint(2, "%s: %d\n", "decomp", size);

	param(&p, 9, 7);
	size = mklkup(normfd, "ccc", myccc, &p);
	fprint(2, "%s: %d\n", "ccc", size);

	mkexceptarr(normfd, "_decompexceptions", mydespecial, nspecial, 0);
	mkexceptarr(normfd, "_recompexceptions", recompext, nrecompext, 1);
	mkrecomp(normfd);
	close(normfd);

	param(&p, 10, 6);
	breakfd = create("runebreakdata", OWRITE, 0644);
	if(breakfd < 0)
		sysfatal("could not create runebreakdata: %r");
	size = mklkup(breakfd, "break", mybreak, &p);
	fprint(2, "%s: %d\n", "break", size);
}

enum {
	FIELD_CODE,
	FIELD_NAME,
	FIELD_CATEGORY,
	FIELD_COMBINING,
	FIELD_BIDIR,
	FIELD_DECOMP,
	FIELD_DECIMAL_DIG,
	FIELD_DIG,
	FIELD_NUMERIC_VAL,
	FIELD_MIRRORED,
	FIELD_UNICODE_1_NAME,
	FIELD_COMMENT,
	FIELD_UPPER,
	FIELD_LOWER,
	FIELD_TITLE,
	NFIELDS,
};

static int
getunicodeline(Biobuf *in, char **fields)
{
	char *p;

	if((p = Brdline(in, '\n')) == nil)
		return 0;

	p[Blinelen(in)-1] = '\0';

	if (getfields(p, fields, NFIELDS + 1, 0, ";") != NFIELDS)
		sysfatal("bad number of fields");

	return 1;
}

static int
estrtoul(char *s, int base)
{
	char *epr;
	Rune code;

	code = strtoul(s, &epr, base);
	if(s == epr)
		sysfatal("bad code point hex string");
	return code;
}

enum {
	OTHER, 
	Hebrew_Letter, Newline, Extend, Format,
	Katakana, ALetter, MidLetter, MidNum,
	MidNumLet, Numeric, ExtendNumLet, WSegSpace,
	PREPEND = 0x10, CONTROL = 0x20, EXTEND = 0x30, REGION = 0x40,
	L = 0x50, V = 0x60, T = 0x70, LV = 0x80, LVT = 0x90, SPACEMK = 0xA0,
	EMOJIEX = 0xB0,
};

static void
markbreak(void)
{
	Biobuf *b;
	char *p, *dot;
	int i, s, e;
	uchar v;

	b = Bopen("/lib/ucd/WordBreakProperty.txt", OREAD);
	if(b == nil)
		sysfatal("could not load word breaks: %r");

	while((p = Brdline(b, '\n')) != nil){
		p[Blinelen(b)-1] = 0;
		if(p[0] == 0 || p[0] == '#')
			continue;
		if((dot = strstr(p, "..")) != nil){
			*dot = 0;
			dot += 2;
			s = estrtoul(p, 16);
			e = estrtoul(dot, 16);
		} else {
			s = e = estrtoul(p, 16);
			dot = p;
		}
		v = 0;
		if(strstr(dot, "ExtendNumLet") != nil)
			v = ExtendNumLet;
		else if(strstr(dot, "Hebrew_Letter") != nil)
			v = Hebrew_Letter;
		else if(strstr(dot, "Newline") != nil)
			v = Newline;
		else if(strstr(dot, "Extend") != nil)
			v = Extend;
		else if(strstr(dot, "Format") != nil)
			v = Format;
		else if(strstr(dot, "Katakana") != nil)
			v = Katakana;
		else if(strstr(dot, "ALetter") != nil)
			v = ALetter;
		else if(strstr(dot, "MidLetter") != nil)
			v = MidLetter;
		else if(strstr(dot, "MidNum") != nil)
			v = MidNum;
		else if(strstr(dot, "Numeric") != nil)
			v = Numeric;
		else if(strstr(dot, "WSegSpace") != nil)
			v = WSegSpace;
		for(i = s; i <= e; i++)
			mybreak[i] = v;
	}
	Bterm(b);
	b = Bopen("/lib/ucd/GraphemeBreakProperty.txt", OREAD);
	if(b == nil)
		sysfatal("could not load Grapheme breaks: %r");

	while((p = Brdline(b, '\n')) != nil){
		p[Blinelen(b)-1] = 0;
		if(p[0] == 0 || p[0] == '#')
			continue;
		if((dot = strstr(p, "..")) != nil){
			*dot = 0;
			dot += 2;
			s = estrtoul(p, 16);
			e = estrtoul(dot, 16);
		} else {
			s = e = estrtoul(p, 16);
			dot = p;
		}
		v = 0;
		if(strstr(dot, "; Prepend #") != nil)
			v = PREPEND;
		else if(strstr(dot, "; Control #") != nil)
			v = CONTROL;
		else if(strstr(dot, "; Extend #") != nil)
			v = EXTEND;
		else if(strstr(dot, "; Regional_Indicator #") != nil)
			v = REGION;
		else if(strstr(dot, "; SpacingMark #") != nil)
			v = SPACEMK;
		else if(strstr(dot, "; L #") != nil)
			v = L;
		else if(strstr(dot, "; V #") != nil)
			v = V;
		else if(strstr(dot, "; T #") != nil)
			v = T;
		else if(strstr(dot, "; LV #") != nil)
			v = LV;
		else if(strstr(dot, "; LVT #") != nil)
			v = LVT;
		for(i = s; i <= e; i++)
			mybreak[i] |= v;
	}
	Bterm(b);

	b = Bopen("/lib/ucd/emoji-data.txt", OREAD);
	if(b == nil)
		sysfatal("could not load emoji-data: %r");

	while((p = Brdline(b, '\n')) != nil){
		p[Blinelen(b)-1] = 0;
		if(p[0] == 0 || p[0] == '#')
			continue;
		if((dot = strstr(p, "..")) != nil){
			*dot = 0;
			dot += 2;
			s = estrtoul(p, 16);
			e = estrtoul(dot, 16);
		} else {
			s = e = estrtoul(p, 16);
			dot = p;
		}
		v = 0;
		if(strstr(dot, "; Extended_Pictographic") != nil)
			v = EMOJIEX;
		for(i = s; i <= e; i++)
			mybreak[i] |= v;
	}
	Bterm(b);
}

static void
markexclusions(void)
{
	Biobuf *b;
	char *p;
	int i;
	uint x;

	b = Bopen("/lib/ucd/CompositionExclusions.txt", OREAD);
	if(b == nil)
		sysfatal("could not load composition exclusions: %r");

	while((p = Brdline(b, '\n')) != nil){
		p[Blinelen(b)-1] = 0;
		if(p[0] == 0 || p[0] == '#')
			continue;
		x = estrtoul(p, 16);
		for(i = 0; i < nrecomp; i++){
			if(myrecomp[i].val == x){
				myrecomp[i].val = 0;
				break;
			}
		}
		if(i == nrecomp){
			for(i = 0; i < nrecompext; i++){
				if(recompext[i*3] == x){
					recompext[i*3] = 0;
					break;
				}
			}
		}
	}
	Bterm(b);
}

void
main(int, char)
{
	static char myisspace[NRUNES];
	static char myisalpha[NRUNES];
	static char myisdigit[NRUNES];
	static char myisupper[NRUNES];
	static char myislower[NRUNES];
	static char myistitle[NRUNES];
	Biobuf *in;
	char *fields[NFIELDS + 1], *fields2[NFIELDS + 1];
	char *p, *d;
	int i, code, last;
	int decomp[2], *ip;

	in = Bopen("/lib/ucd/UnicodeData.txt", OREAD);
	if(in == nil)
		sysfatal("can't open UnicodeData.txt: %r");

	for(i = 0; i < NRUNES; i++){
		mytoupper[i] = -1;
		mytolower[i] = -1;
		mytotitle[i] = -1;
		mydecomp[i] = 0;
		myccc[i] = 0;
		mybreak[i] = 0;
	}

	myisspace['\t'] = 1;
	myisspace['\n'] = 1;
	myisspace['\r'] = 1;
	myisspace['\f'] = 1;
	myisspace['\v'] = 1;
	myisspace[0x85] = 1;	/* control char, "next line" */
	myisspace[0xfeff] = 1;	/* zero-width non-break space */

	last = -1;
	nspecial = nrecomp = nrecompext =  0;
	while(getunicodeline(in, fields)){
		code = estrtoul(fields[FIELD_CODE], 16);
		if (code >= NRUNES)
			sysfatal("code-point value too big: %x", code);
		if(code <= last)
			sysfatal("bad code sequence: %x then %x", last, code);
		last = code;

		p = fields[FIELD_CATEGORY];
		if(strstr(fields[FIELD_NAME], ", First>") != nil){
			if(!getunicodeline(in, fields2))
				sysfatal("range start at eof");
			if (strstr(fields2[FIELD_NAME], ", Last>") == nil)
				sysfatal("range start not followed by range end");
			last = estrtoul(fields2[FIELD_CODE], 16);
			if(last <= code)
				sysfatal("range out of sequence: %x then %x", code, last);
			if(strcmp(p, fields2[FIELD_CATEGORY]) != 0)
				sysfatal("range with mismatched category");
		}

		d = fields[FIELD_DECOMP];
		if(strlen(d) > 0 && strstr(d, "<") == nil){
			decomp[0] = estrtoul(d, 16);
			d = strstr(d, " ");
			if(d == nil){
				/* singleton recompositions are verboden */
				decomp[1] = 0;
				if(decomp[0] > 0xFFFF){
					ip = mydespecial + nspecial*3;
					ip[0] = code;
					ip[1] = decomp[0];
					ip[2] = 0;
					mydecomp[code] = (DSTART+nspecial)<<16;
					nspecial++;
				} else
					mydecomp[code] = decomp[0]<<16;
			} else {
				d++;
				decomp[1] = estrtoul(d, 16);
				if(decomp[0] > 0xFFFF || decomp[1] > 0xFFFF){
					ip = mydespecial + nspecial*3;
					ip[0] = code;
					ip[1] = decomp[0];
					ip[2] = decomp[1];
					mydecomp[code] = (DSTART+nspecial)<<16;
					nspecial++;
					ip = recompext + nrecompext*3;
					ip[0] = code;
					ip[1] = decomp[0];
					ip[2] = decomp[1];
					nrecompext++;
				} else {
					mydecomp[code] = decomp[0]<<16 | decomp[1];
					myrecomp[nrecomp++] = (KV){decomp[0]<<16 | decomp[1], code, 0};
				}
			}
		}

		for (; code <= last; code++){
			if(p[0] == 'L')
				myisalpha[code] = 1;
			if(p[0] == 'Z')
				myisspace[code] = 1;

			if(strcmp(p, "Lu") == 0)
				myisupper[code] = 1;
			if(strcmp(p, "Ll") == 0)
				myislower[code] = 1;

			if(strcmp(p, "Lt") == 0)
				myistitle[code] = 1;

			if(strcmp(p, "Nd") == 0)
				myisdigit[code] = 1;

			if(fields[FIELD_UPPER][0] != '\0')
				mytoupper[code] = estrtoul(fields[FIELD_UPPER], 16);

			if(fields[FIELD_LOWER][0] != '\0')
				mytolower[code] = estrtoul(fields[FIELD_LOWER], 16);

			if(fields[FIELD_TITLE][0] != '\0')
				mytotitle[code] = estrtoul(fields[FIELD_TITLE], 16);

			myccc[code] = estrtoul(fields[FIELD_COMBINING], 10);
		}
	}

	Bterm(in);
	markexclusions();

	/*
	 * according to standard, if totitle(x) is not defined in ucd
	 * but toupper(x) is, then totitle is defined to be toupper(x)
	 */
	for(i = 0; i < NRUNES; i++){
		if(mytotitle[i] == -1
		&& mytoupper[i] != -1
		&& !myistitle[i])
			mytotitle[i] = mytoupper[i];
	}

	/*
	 * A couple corrections:
	 * is*(to*(x)) should be true.
	 * restore undefined transformations.
	 * store offset instead of value, makes them sparse.
	 */
	for(i = 0; i < NRUNES; i++){
		if(mytoupper[i] != -1)
			myisupper[mytoupper[i]] = 1;
		else
			mytoupper[i] = i;

		if(mytolower[i] != -1)
			myislower[mytolower[i]] = 1;
		else
			mytolower[i] = i;

		if(mytotitle[i] != -1)
			myistitle[mytotitle[i]] = 1;
		else
			mytotitle[i] = i;

		mytoupper[i] = mytoupper[i] - i;
		mytolower[i] = mytolower[i] - i;
		mytotitle[i] = mytotitle[i] - i;
	}

	uchar b;
	for(i = 0; i < NRUNES; i++){
		b = 0;
		if(myisspace[i])
			b |= 1<<0;
		if(myisalpha[i])
			b |= 1<<1;
		if(myisdigit[i])
			b |= 1<<2;
		if(myisupper[i])
			b |= 1<<3;
		if(myislower[i])
			b |= 1<<4;
		if(myistitle[i])
			b |= 1<<5;

		myismerged[i] = b;
	}

	markbreak();
	mktables();
	exits(nil);
}
