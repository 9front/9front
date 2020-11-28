#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <bio.h>
#include <avl.h>
#include "mix.h"
#include "y.tab.h"

struct Resvd {
	char *name;
	long lex;
	int c;
	int f;
} res[] = {
	{ "NOP",	LOP,	0,	F(0, 5) },
	{ "ADD",	LOP,	1,	F(0, 5) },
	{ "FADD",	LOP,	1,	6 },
	{ "SUB",	LOP,	2,	F(0, 5) },
	{ "FSUB",	LOP,	2,	6 },
	{ "MUL",	LOP,	3,	F(0, 5) },
	{ "FMUL",	LOP,	3,	6 },
	{ "DIV",	LOP,	4,	F(0, 5) },
	{ "FDIV",	LOP,	4,	6 },
	{ "NUM",	LOP,	5,	0 },
	{ "CHAR",	LOP,	5,	1 },
	{ "HLT",	LOP,	5,	2 },
	{ "SLA",	LOP,	6,	0 },
	{ "SRA",	LOP,	6,	1 },
	{ "SLAX",	LOP,	6,	2 },
	{ "SRAX",	LOP,	6,	3 },
	{ "SLC",	LOP,	6,	4 },
	{ "SRC",	LOP,	6,	5 },
	{ "MOVE",	LOP,	7,	1 },
	{ "LDA",	LOP,	8,	F(0, 5) },
	{ "LD1",	LOP,	9,	F(0, 5) },
	{ "LD2",	LOP,	10,	F(0, 5) },
	{ "LD3",	LOP,	11,	F(0, 5) },
	{ "LD4",	LOP,	12,	F(0, 5) },
	{ "LD5",	LOP,	13,	F(0, 5) },
	{ "LD6",	LOP,	14,	F(0, 5) },
	{ "LDX",	LOP,	15,	F(0, 5) },
	{ "LDAN",	LOP,	16,	F(0, 5) },
	{ "LD1N",	LOP,	17,	F(0, 5) },
	{ "LD2N",	LOP,	18,	F(0, 5) },
	{ "LD3N",	LOP,	19,	F(0, 5) },
	{ "LD4N",	LOP,	20,	F(0, 5) },
	{ "LD5N",	LOP,	21,	F(0, 5) },
	{ "LD6N",	LOP,	22,	F(0, 5) },
	{ "LDXN",	LOP,	23,	F(0, 5) },
	{ "STA",	LOP,	24,	F(0, 5) },
	{ "ST1",	LOP,	25,	F(0, 5) },
	{ "ST2",	LOP,	26,	F(0, 5) },
	{ "ST3",	LOP,	27,	F(0, 5) },
	{ "ST4",	LOP,	28,	F(0, 5) },
	{ "ST5",	LOP,	29,	F(0, 5) },
	{ "ST6",	LOP,	30,	F(0, 5) },
	{ "STX",	LOP,	31,	F(0, 5) },
	{ "STJ",	LOP,	32,	F(0, 2) },
	{ "STZ",	LOP,	33,	F(0, 5) },
	{ "JBUS",	LOP,	34, 0 },
	{ "IOC",	LOP,	35, 0 },
	{ "IN",	LOP,	36,	0 },
	{ "OUT",	LOP,	37,	0 },
	{ "JRED",	LOP,	38,	0 },
	{ "JMP",	LOP,	39, 0 },
	{ "JSJ",	LOP,	39, 1 },
	{ "JOV",	LOP,	39, 2 },
	{ "JNOV",	LOP,	39, 3 },
	{ "JL",	LOP,	39,	4 },
	{ "JE",	LOP,	39,	5 },
	{ "JG",	LOP,	39,	6 },
	{ "JGE",	LOP,	39,	7 },
	{ "JNE",	LOP,	39,	8 },
	{ "JLE",	LOP,	39,	9 },
	{ "JAN",	LOP,	40,	0 },
	{ "JAZ",	LOP,	40,	1 },
	{ "JAP",	LOP,	40,	2 },
	{ "JANN",	LOP,	40,	3 },
	{ "JANZ",	LOP,	40,	4 },
	{ "JANP",	LOP,	40,	5 },
	{ "J1N",	LOP,	41,	0 },
	{ "J1Z",	LOP,	41,	1 },
	{ "J1P",	LOP,	41,	2 },
	{ "J1NN",	LOP,	41,	3 },
	{ "J1NZ",	LOP,	41,	4 },
	{ "J1NP",	LOP,	41,	5 },
	{ "J2N",	LOP,	42,	0 },
	{ "J2Z",	LOP,	42,	1 },
	{ "J2P",	LOP,	42,	2 },
	{ "J2NN",	LOP,	42,	3 },
	{ "J2NZ",	LOP,	42,	4 },
	{ "J2NP",	LOP,	42,	5 },
	{ "J3N",	LOP,	43,	0 },
	{ "J3Z",	LOP,	43,	1 },
	{ "J3P",	LOP,	43,	2 },
	{ "J3NN",	LOP,	43,	3 },
	{ "J3NZ",	LOP,	43,	4 },
	{ "J3NP",	LOP,	43,	5 },
	{ "J4N",	LOP,	44,	0 },
	{ "J4Z",	LOP,	44,	1 },
	{ "J4P",	LOP,	44,	2 },
	{ "J4NN",	LOP,	44,	3 },
	{ "J4NZ",	LOP,	44,	4 },
	{ "J4NP",	LOP,	44,	5 },
	{ "J5N",	LOP,	45,	0 },
	{ "J5Z",	LOP,	45,	1 },
	{ "J5P",	LOP,	45,	2 },
	{ "J5NN",	LOP,	45,	3 },
	{ "J5NZ",	LOP,	45,	4 },
	{ "J5NP",	LOP,	45,	5 },
	{ "J6N",	LOP,	46,	0 },
	{ "J6Z",	LOP,	46,	1 },
	{ "J6P",	LOP,	46,	2 },
	{ "J6NN",	LOP,	46,	3 },
	{ "J6NZ",	LOP,	46,	4 },
	{ "J6NP",	LOP,	46,	5 },
	{ "JXN",	LOP,	47,	0 },
	{ "JXZ",	LOP,	47,	1 },
	{ "JXP",	LOP,	47,	2 },
	{ "JXNN",	LOP,	47,	3 },
	{ "JXNZ",	LOP,	47,	4 },
	{ "JXNP",	LOP,	47,	5 },
	{ "INCA",	LOP,	48,	0 },
	{ "DECA",	LOP,	48,	1 },
	{ "ENTA",	LOP,	48,	2 },
	{ "ENNA",	LOP,	48,	3 },
	{ "INC1",	LOP,	49,	0 },
	{ "DEC1",	LOP,	49,	1 },
	{ "ENT1",	LOP,	49,	2 },
	{ "ENN1",	LOP,	49,	3 },
	{ "INC2",	LOP,	50,	0 },
	{ "DEC2",	LOP,	50,	1 },
	{ "ENT2",	LOP,	50,	2 },
	{ "ENN2",	LOP,	50,	3 },
	{ "INC3",	LOP,	51,	0 },
	{ "DEC3",	LOP,	51,	1 },
	{ "ENT3",	LOP,	51,	2 },
	{ "ENN3",	LOP,	51,	3 },
	{ "INC4",	LOP,	52,	0 },
	{ "DEC4",	LOP,	52,	1 },
	{ "ENT4",	LOP,	52,	2 },
	{ "ENN4",	LOP,	52,	3 },
	{ "INC5",	LOP,	53,	0 },
	{ "DEC5",	LOP,	53,	1 },
	{ "ENT5",	LOP,	53,	2 },
	{ "ENN5",	LOP,	53,	3 },
	{ "INC6",	LOP,	54,	0 },
	{ "DEC6",	LOP,	54,	1 },
	{ "ENT6",	LOP,	54,	2 },
	{ "ENN6",	LOP,	54,	3 },
	{ "INCX",	LOP,	55,	0 },
	{ "DECX",	LOP,	55,	1 },
	{ "ENTX",	LOP,	55,	2 },
	{ "ENNX",	LOP,	55,	3 },
	{ "CMPA",	LOP,	56,	F(0, 5) },
	{ "FCMP",	LOP,	56,	6 },
	{ "CMP1",	LOP,	57,	F(0, 5) },
	{ "CMP2",	LOP,	58,	F(0, 5) },
	{ "CMP3",	LOP,	59,	F(0, 5) },
	{ "CMP4",	LOP,	60,	F(0, 5) },
	{ "CMP5",	LOP,	61,	F(0, 5) },
	{ "CMP6",	LOP,	62,	F(0, 5) },
	{ "CMPX",	LOP,	63,	F(0, 5) },
	{ "EQU",	LEQU,	-1,	-1 },
	{ "ORIG",	LORIG,	-1,	-1 },
	{ "CON",	LCON,	-1,	-1 },
	{ "ALF",	LALF,	-1,	-1 },
	{ "END",	LEND,	-1,	-1 },
	{ "0H",	LHERE,	0,	-1 },
	{ "1H",	LHERE,	1,	-1 },
	{ "2H",	LHERE,	2,	-1 },
	{ "3H",	LHERE,	3,	-1 },
	{ "4H",	LHERE,	4,	-1 },
	{ "5H",	LHERE,	5,	-1 },
	{ "6H",	LHERE,	6,	-1 },
	{ "7H",	LHERE,	7,	-1 },
	{ "8H",	LHERE,	8,	-1 },
	{ "9H",	LHERE,	9,	-1 },
	{ "0B",	LBACK,	0,	-1 },
	{ "1B",	LBACK,	1,	-1 },
	{ "2B",	LBACK,	2,	-1 },
	{ "3B",	LBACK,	3,	-1 },
	{ "4B",	LBACK,	4,	-1 },
	{ "5B",	LBACK,	5,	-1 },
	{ "6B",	LBACK,	6,	-1 },
	{ "7B",	LBACK,	7,	-1 },
	{ "8B",	LBACK,	8,	-1 },
	{ "9B",	LBACK,	9,	-1 },
	{ "0F",	LFORW,	0,	-1 },
	{ "1F",	LFORW,	1,	-1 },
	{ "2F",	LFORW,	2,	-1 },
	{ "3F",	LFORW,	3,	-1 },
	{ "4F",	LFORW,	4,	-1 },
	{ "5F",	LFORW,	5,	-1 },
	{ "6F",	LFORW,	6,	-1 },
	{ "7F",	LFORW,	7,	-1 },
	{ "8F",	LFORW,	8,	-1 },
	{ "9F",	LFORW,	9,	-1 },
};

int mask[] = {
	MASK1,
	MASK2,
	MASK3,
	MASK4,
	MASK5
};

int symcmp(Avl*, Avl*);
Sym *sym(char*);

void
main(int argc, char **argv)
{
	int go;
	char **ap;

	go = 0;
	ARGBEGIN {
	case 'g': go++; break;
	} ARGEND

	cinit();
	sinit();
	fmtinstall('I', Ifmt);
	vmstart = -1;
	for(ap = argv; ap < argv+argc; ap++)
		asmfile(*ap);
	repl(go);
	exits(nil);
}

void
sinit(void)
{
	struct Resvd *r;
	Sym *s;

	syms = avlcreate(symcmp);
	for(r = res; r < res + nelem(res); r++) {
		s = sym(r->name);
		s->lex = r->lex;
		s->opc = r->c;
		s->f = r->f;
		avlinsert(syms, s);
	}
}

int
asmfile(char *file)
{
	int fd;

	if((fd = open(file, OREAD)) == -1)
		return -1;
	Binit(&bin, fd, OREAD);
	line = 1;
	filename = file;
	if(setjmp(errjmp) == 0)
		yyparse();
	Bterm(&bin);
	close(fd);
	return 0;
}

int
unpack(u32int inst, int *apart, int *ipart, int *fpart)
{
	int opc;

	opc = V(inst, F(5, 5));
	*fpart = V(inst, F(4, 4));
	*ipart = V(inst, F(3, 3));
	*apart = V(inst, F(0, 2));
	return opc;
}

int
Ifmt(Fmt *f)
{
	u32int inst;
	int i, apart, ipart, fpart, opc, a, b;

	inst = va_arg(f->args, u32int);
	opc = unpack(inst, &apart, &ipart, &fpart);
	for(i = 0; i < nelem(res); i++) {
		if(res[i].c == opc)
			break;
	}
	UNF(a, b, fpart);
	if(res[i+1].c != opc || opc == 56)
		return fmtprint(f, "%s\t%d,%d(%d | %d:%d)", res[i].name, apart, ipart, fpart, a, b);
	while(res[i].c == opc && i < nelem(res)) {
		if(res[i].f == fpart)
			return fmtprint(f, "%s\t%d,%d(%d | %d:%d)", res[i].name, apart, ipart, fpart, a, b);
		i++;
	}
	return fmtprint(f, "%d\t%d,%d(%d | %d:%d)", opc, apart, ipart, fpart, a, b);
}

Rune
getr(void)
{
	static int bol = 1;
	Rune r;

	r = Bgetrune(&bin);
	switch(r) {
	case '*':
		if(!bol)
			break;
	case '#':
		skipto('\n');
	case '\n':
		line++;
		bol = 1;
		return '\n';
	}
	bol = 0;
	return r;
}

void
ungetr(Rune r)
{
	if(r == '\n')
		line--;
	Bungetrune(&bin);
}

long
yylex(void)
{
	static Rune buf[11];
	Rune r, *bp, *ep;
	static char cbuf[100];
	int isnum;

	if(yydone)
		return -1;

Loop:
	r = getr();
	switch(r) {
	case Beof:
		return -1;
	case '\t':
	case ' ':
		goto Loop;
	case '\n': case '*': case '+':
	case '-': case ':': case ',':
	case '(': case ')': case '=':
		return r;
	case '/':
		r = getr();
		if(r == '/')
			return LSS;
		else
			ungetr(r);
		return '/';
	case '"':
		for(bp = buf; bp < buf+5; bp++) {
			*bp = getr();
		}
		if(getr() != '"')
			yyerror("Bad string literal\n");
		yylval.rbuf = buf;
		return LSTR;
	}
	bp = buf;
	ep = buf+nelem(buf)-1;
	isnum = 1;
	for(;;) {
		if(runetomix(r) == -1)
			yyerror("Invalid character %C", r);
		if(bp == ep)
			yyerror("Symbol or number too long");
		*bp++ = r;
		if(isnum && (r >= Runeself || !isdigit(r)))
			isnum = 0;
		switch(r = getr()) {
		case Beof: case '\t': case '\n':
		case '+': case '-': case '*':
		case ':': case ',': case '(':
		case ')': case '=': case ' ':
		case '/': case '#':
			ungetr(r);
			*bp = '\0';
			goto End;
		}
	}
End:
	seprint(cbuf, cbuf+100, "%S", buf);
	if(isnum) {
		yylval.lval = strtol(cbuf, nil, 10);
		return LNUM;
	}
	yylval.sym = sym(cbuf);
	return yylval.sym->lex;
}

Sym*
getsym(char *name)
{
	Sym *s;

	s = emallocz(sizeof(*s) + strlen(name));
	strcpy(s->nbuf, name);
	s->name = s->nbuf;
	s->lex = LSYMREF;
	return s;
}

Sym*
sym(char *name)
{
	Sym *s, l;

	l.name = name;
	s = (Sym*)avllookup(syms, &l, 0);
	if(s != nil)
		return s;
	s = getsym(name);
	avlinsert(syms, s);
	return s;
}

int
symcmp(Avl *a, Avl *b)
{
	Sym *sa, *sb;

	sa = (Sym*)a;
	sb = (Sym*)b;
	return strcmp(sa->name, sb->name);
}

void
skipto(char c)
{
	Rune r;

	for(;;) {
		r = Bgetrune(&bin);
		if(r != c && r != Beof)
			continue;
		return;
	}
}

int
mval(u32int a, int s, u32int m)
{
	int sign, val;

	sign = a >> 31;
	val = a>>s*BITS & m;
	if(sign)
		return -val;
	return val;
}

int
V(u32int w, int f)
{
	int a, b, d;

	if(f == 0)
		return 0;

	UNF(a, b, f);
	if(a > 0)
		w &= ~SIGNB;
	else
		a++;

	d = b - a;
	if(a > 5 || b > 5 || d < 0 || d > 4)
		vmerror("Invalid fpart %d", f);

	return mval(w, 5-b, mask[d]);
}

int
M(int a, int i)
{
	int off, r;

	r = ri[i] & ~(MASK3<<2*BITS);
	off = i == 0 ? 0 : mval(r, 0, MASK2);
	return a + off;
}

void mixfadd(int){}

void
mixadd(int m, int f)
{
	int rval;
	
	rval = mval(ra, 0, MASK5);
	rval += V(cells[m], f);
	ra = rval < 0 ? -rval|SIGNB : rval;
	if(ra & OVERB) {
		ra &= ~OVERB;
		ot = 1;
	}
}

void mixfsub(int){}

void
mixsub(int m, int f)
{
	int rval;

	rval = mval(ra, 0, MASK5);
	rval -= V(cells[m], f);
	ra = rval < 0 ? -rval|SIGNB : rval;
	if(ra & OVERB) {
		ra &= ~OVERB;
		ot = 1;
	}
}

void mixfmul(int){}

void
mixmul(int m, int f)
{
	vlong rval;
	int signb;

	rval = mval(ra, 0, MASK5);
	rval *= V(cells[m], f);

	if(rval < 0) {
		rval = -rval;
		signb = SIGNB;
	} else
		signb = 0;

	ra = rval>>5*BITS & MASK5 | signb;
	rx = rval & MASK5 | signb;
}

void mixfdiv(int){}

void
mixdiv(int m, int f)
{
	vlong rax, quot;
	u32int xsignb, asignb;
	int rem, v;

	v = V(cells[m], f);
	if(v == 0) {
		ot = 1;
		return;
	}
	rax = ra & MASK5;
	rax <<= 5 * BITS;
	rax |= rx & MASK5;
	if(ra >> 31)
		rax = -rax;

	quot = rax / v;
	rem = rax % v;

	if(quot < 0) {
		quot = -quot;
		asignb = SIGNB;
	} else
		asignb = 0;

	if(rem < 0) {
		rem = -rem;
		xsignb = SIGNB;
	} else
		xsignb = 0;

	if(quot & ~MASK5)
		ot = 1;

	ra = quot & MASK5 | asignb;
	rx = rem & MASK5 | xsignb;
}

void
mixnum(void)
{
	int i, b;
	u32int n;

	n = 0;
	for(i = 0; i < 5; i++) {
		b = ra>>(4-i)*BITS & MASK1;
		b %= 10;
		n = 10*n + b;
	}
	for(i = 0; i < 5; i++) {
		b = rx>>(4-i)*BITS & MASK1;
		b %= 10;
		n = 10*n + b;
	}
	ra &= ~MASK5;
	ra |= n & MASK5;
}

void
mixchar(void)
{
	int i;
	u32int a, val;

	val = ra & ~SIGNB;
	for(i = 0; i < 5; i++) {
		a = val % 10;
		a += 30;
		rx &= ~(MASK1 << i*BITS);
		rx |= a << i*BITS;
		val /= 10;
	}
	for(i = 0; i < 5; i++) {
		a = val % 10;
		a += 30;
		ra &= ~(MASK1 << i*BITS);
		ra |= a << i*BITS;
		val /= 10;
	}
}

void
mixslra(int m, int left)
{
	u32int val;

	if(m < 0)
		vmerror("Bad shift A %d", m);
	if(m > 4) {
		ra &= ~MASK5;
		return;
	}
	val = ra & MASK5;
	ra &= ~MASK5;
	if(left)
		val <<= m * BITS;
	else
		val >>= m * BITS;
	ra |= val & MASK5;
}

void
mixslrax(int m, int left)
{
	u64int rax;

	if(m < 0)
		vmerror("Bad shift AX %d", m);
	if(m > 9) {
		ra &= ~MASK5;
		rx &= ~MASK5;
		return;
	}
	rax = ra & MASK5;
	ra &= ~MASK5;
	rax <<= 5 * BITS;
	rax |= rx & MASK5;
	rx &= ~MASK5;
	if(left)
		rax <<= m*BITS;
	else
		rax >>= m*BITS;
	rx |= rax & MASK5;
	ra |= rax>>5*BITS & MASK5;
}

void
mixslc(int m)
{
	u64int rax, s;

	if(m < 0)
		vmerror("Bad shift SLC %d", m);

	m %= 10;

	rax = ra & MASK5;
	ra &= ~MASK5;
	rax <<= 5 * BITS;
	rax |= rx & MASK5;
	rx &= ~MASK5;

	s = rax & mask[m]<<10-m;
	rax <<= m;
	rax &= ~mask[m];
	rax |= s;

	rx |= rax & MASK5;
	ra |= rax>>5*BITS & MASK5;
}

void
mixsrc(int m)
{
	u64int rax, s;

	if(m < 0)
		vmerror("Bad shift SRC %d", m);

	m %= 10;

	rax = ra & MASK5;
	ra &= ~MASK5;
	rax <<= 5 * BITS;
	rax |= rx & MASK5;
	rx &= ~MASK5;

	s = rax & mask[m];
	rax >>= m;
	rax &= ~mask[m] << 10-m;
	rax |= s<<10-m;

	rx |= rax & MASK5;
	ra |= rax>>5*BITS & MASK5;
}

void
mixmove(int s, int f)
{
	int d;
	u32int *p, *q, *pe;

	if(f == 0)
		return;
	if(s < 0 || s >= 4000 || s+f < 0 || s+f > 4000)
		vmerror("Bad src range MOVE %d:%d", s, s+f-1);
	d = mval(ri[1], 0, MASK2);
	if(d < 0 || d >= 4000 || d+f < 0 || d+f > 4000)
		vmerror("Bad dst range MOVE %d:%d", d, d+f-1);
	p = cells+d;
	q = cells+s;
	pe = p+f;
	while(p < pe)
		*p++ = *q++;
	d += f;
	d &= MASK2;
	ri[1] = d < 0 ? -d|SIGNB : d;
}

u32int
mixld(u32int v, int f)
{
	u32int w;
	int a, b, d;

	if(f == 5)
		return v;

	UNF(a, b, f);
	w = 0;
	if(a == 0) {
		if(v >> 31)
			w = SIGNB;
		if(b == 0)
			return w;
		a++;
	}

	d = b - a;
	if(a > 5 || b > 5 || d < 0 || d > 4)
		vmerror("Bad fpart (%d:%d)", a, b);
	v &= mask[d] << (5-b) * BITS;
	v >>= (5-b) * BITS;
	return w | v;
}

u32int
mixst(u32int w, u32int v, int f)
{
	int a, b, d;

	if(f == 5)
		return v;

	UNF(a, b, f);
	if(a == 0) {
		w = v>>31 ? w|SIGNB : w&~SIGNB;
		if(b == 0)
			return w;
		a++;
	}

	d = b - a;
	if(a > 5 || b > 5 || d < 0 || d > 4)
		vmerror("Bad fpart (%d:%d)", a, b);
	v &= mask[d];
	v <<= (5-b) * BITS;
	w &= ~(mask[d] << (5-b)*BITS);
	return w | v;
}

int
mixjbus(int /*m*/, int /*f*/, int ip)
{
	return ip+1;
}

void
mixioc(int, int f)
{
	switch(f) {
	case 18:
	case 19:
		print("\n");
		break;
	}
}

void mixin(int, int){}

void
mixout(int m, int f)
{
	switch(f) {
	case 18:
		mixprint(m, 24);
		break;
	case 19:
		mixprint(m, 14);
		break;
	}
}

int
mixjred(int m, int /*f*/, int /*ip*/)
{
	return m;
}

int
mixjmp(int m, int ip)
{
	ri[0] = ip+1 & MASK2;
	return m;
}

int
mixjov(int m, int ip)
{
	if(ot) {
		ot = 0;
		ri[0] = ip+1 & MASK2;
		return m;
	}
	return ip + 1;
}

int
mixjnov(int m, int ip)
{
	if(ot) {
		ot = 0;
		return ip + 1;
	}
	ri[0] = ip+1 & MASK2;
	return m;
}

int
mixjc(int m, int ip, int c1, int c2)
{
	if(c1 || c2) {
		ri[0] = ip+1 & MASK2;
		return m;
	}
	return ip + 1;
}

int
mixjaxic(int m, int ip, u32int r, u32int msk, int f)
{
	int v, c;

	v = mval(r, 0, msk);
	switch(f) {
	default:	vmerror("Bad instruction JA condition: %d", f);
	case 0:	c = v < 0;	break;
	case 1:	c = v == 0;	break;
	case 2:	c = v > 0;	break;
	case 3:	c = v >= 0;	break;
	case 4:	c = v != 0;	break;
	case 5:	c = v <= 0;	break;
	}

	if(c) {
		ri[0] = ip+1 & MASK2;
		return m;
	}
	return ip + 1;
}

void
mixinc(int m, u32int *r)
{
	int v;

	v = mval(*r, 0, MASK5);
	v += m;
	*r = v < 0 ? -v|SIGNB : v;
}

void mixfcmp(void){}

void
mixcmp(int m, int f, u32int r)
{
	int v1, v2;

	ce = cg = cl = 0;

	v1 = V(r, f);
	v2 = V(cells[m], f);
	if(v1 < v2)
		cl = 1;
	else if(v1 > v2)
		cg = 1;
	else
		ce = 1;
}

int
mixvm(int ip, int once)
{
	u32int r;
	int a, i, f, c, m, inst;

	curpc = ip;
	for (;;) {
		if(curpc < 0 || curpc > 4000)
			vmerror("Bad PC %d", curpc);
		if(bp[curpc] && !once)
			return curpc;
		inst = cells[curpc];
		a = V(inst, F(0, 2));
		i = V(inst, F(3, 3));
		f = V(inst, F(4, 4));
		c = V(inst, F(5, 5));
		m = M(a, i);
		switch(c) {
		default:
			fprint(2, "Bad op!\n");
			exits("error");
		case 0:
			break;
		case 1:
			if(f == 6)
				mixfadd(inst);
			else
				mixadd(m, f);
			break;
		case 2:
			if(f == 6)
				mixfsub(inst);
			else
				mixsub(m, f);
			break;
		case 3:
			if(f == 6)
				mixfmul(inst);
			else
				mixmul(m, f);
			break;
		case 4:
			if(f == 6)
				mixfdiv(inst);
			else
				mixdiv(m, f);
			break;
		case 5:
			switch(f) {
			default:
				vmerror("Bad instruction NUM or CHAR: %d", f);
			case 0:
				mixnum();
				break;
			case 1:
				mixchar();
				break;
			case 2:
				return -1;	/* HLT */
			}
			break;
		case 6:
			switch(f) {
			default: vmerror("Bad instruction shift: %d", f);
			case 0: mixslra(m, 1);	break;
			case 1: mixslra(m, 0);	break;
			case 2: mixslrax(m, 1);	break;
			case 3: mixslrax(m, 0);	break;
			case 4: mixslc(m);	break;
			case 5: mixsrc(m);	break;
			}
			break;
		case 7:
			mixmove(m, f);
			break;
		case 8:
			ra = mixld(cells[m], f);
			break;
		case 9: case 10: case 11:
		case 12: case 13: case 14:
			ri[c-8] = mixld(cells[m], f);
			break;
		case 15:
			rx = mixld(cells[m], f);
			break;
		case 16:
			ra = mixld(cells[m], f) ^ SIGNB;
			break;
		case 17: case 18: case 19:
		case 20: case 21: case 22:
			ri[c-16] = mixld(cells[m], f) ^ SIGNB;
			break;
		case 23:
			rx = mixld(cells[m], f) ^ SIGNB;
			break;
		case 24:
			cells[m] = mixst(cells[m], ra, f);
			break;
		case 25: case 26: case 27:
		case 28: case 29: case 30:
			r = ri[c-24] & ~(MASK3 << 2*BITS);
			cells[m] = mixst(cells[m], r, f);
			break;
		case 31:
			cells[m] = mixst(cells[m], rx, f);
			break;
		case 32:
			r = ri[0] & ~(MASK3 << 2*BITS);
			cells[m] = mixst(cells[m], r, f);
			break;
		case 33:
			cells[m] = mixst(cells[m], 0, f);
			break;
		case 34:
			curpc = mixjbus(m, f, curpc);
			goto Again;
		case 35:
			mixioc(m, f);
			break;
		case 36:
			mixin(m, f);
			break;
		case 37:
			mixout(m, f);
			break;
		case 38:
			curpc = mixjred(m, f, curpc);
			break;
		case 39:
			switch(f) {
			default: vmerror("Bad jmp instruction: %d", f);
			case 0: curpc = mixjmp(m, curpc);	break;
			case 1: curpc = m;	break;	/* JSJ */
			case 2: curpc = mixjov(m, curpc);	break;
			case 3: curpc = mixjnov(m, curpc);	break;
			case 4: curpc = mixjc(m, curpc, cl, 0);	break;
			case 5: curpc = mixjc(m, curpc, ce, 0);	break;
			case 6: curpc = mixjc(m, curpc, cg, 0);	break;
			case 7: curpc = mixjc(m, curpc, cg, ce);	break;
			case 8: curpc = mixjc(m, curpc, cl, cg);	break;
			case 9: curpc = mixjc(m, curpc, cl, ce);	break;
			}
			goto Again;
		case 40:
			curpc = mixjaxic(m, curpc, ra, MASK5, f);
			goto Again;
		case 41: case 42: case 43:
		case 44: case 45: case 46:
			curpc = mixjaxic(m, curpc, ri[c-40], MASK2, f);
			goto Again;
		case 47:
			curpc = mixjaxic(m, curpc, rx, MASK5, f);
			goto Again;
		case 48:
			switch(f) {
			case 0:	mixinc(m, &ra);	break;
			case 1: mixinc(-m, &ra);	break;
			case 2:
				ra = m == 0
					? inst & SIGNB
					: m < 0 ? -m|SIGNB : m;
				break;	/* ENTA */
			case 3:
				ra = m == 0
					? ~inst & SIGNB
					: m > 0 ? m|SIGNB : -m;
				break;	/* ENNA */
			}
			break;
		case 49: case 50: case 51:
		case 52: case 53: case 54:
			switch(f) {
			case 0:	mixinc(m, ri+(c-48));	break;
			case 1:	mixinc(-m, ri+(c-48));	break;
			case 2:
				ri[c-48] = m == 0
					? inst & SIGNB
					: m < 0 ? -m|SIGNB : m;
				break;	/* ENT[1-6] */
			case 3:
				ri[c-48] = m == 0
					? ~inst & SIGNB
					: m > 0 ? m|SIGNB : -m;
				break;	/* ENN[1-6] */
			}
			break;
		case 55:
			switch(f) {
			case 0:	mixinc(m, &rx);	break;
			case 1: mixinc(-m, &rx);	break;
			case 2:	rx = m == 0
					? inst & SIGNB
					: m < 0 ? -m|SIGNB : m;
				break;	/* ENTX */
			case 3:	rx = m == 0
					? ~inst & SIGNB
					: m > 0 ? m|SIGNB : -m;
				break;	/* ENNX */
			}
			break;
		case 56:
			if(f == 6)
				mixfcmp();
			else
				mixcmp(m, f, ra);
			break;
		case 57: case 58: case 59:
		case 60: case 61: case 62:
			mixcmp(m, f, ri[c-56] & ~(MASK3<<2*BITS));
			break;
		case 63:
			mixcmp(m, f, rx);
			break;
		}
		curpc++;
Again:
		if(once)
			return curpc;
	}
}
