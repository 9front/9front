#define	EXTERN
#include "gc.h"

void
listinit(void)
{

	fmtinstall('A', Aconv);
	fmtinstall('P', Pconv);
	fmtinstall('S', Sconv);
	fmtinstall('N', Nconv);
	fmtinstall('B', Bconv);
	fmtinstall('D', Dconv);
	fmtinstall('R', Rconv);
}

int
Bconv(Fmt *fp)
{
	Bits bits;
	int i;

	bits = va_arg(fp->args, Bits);
	while(bany(&bits)) {
		i = bnum(bits);
		bits.b[i/32] &= ~(1L << (i%32));
		if(var[i].sym == S)
			fmtprint(fp, "$%lld ", var[i].offset);
		else
			fmtprint(fp, "%s ", var[i].sym->name);
	}
	return 0;
}

static char *conds[] = {
	".EQ", ".NE", ".CS", ".CC", 
	".MI", ".PL", ".VS", ".VC", 
	".HI", ".LS", ".GE", ".LT", 
	".GT", ".LE", "", ".NV",
};

int
Pconv(Fmt *fp)
{
	char str[STRINGSZ];
	Prog *p;
	int a;

	p = va_arg(fp->args, Prog*);
	a = p->as;
	if(a == ADATA)
		snprint(str, sizeof(str), "	%A	%D/%d,%D", a, &p->from, p->reg, &p->to);
	else
	if(p->as == ATEXT)
		snprint(str, sizeof(str), "	%A	%D,%d,%D", a, &p->from, p->reg, &p->to);
	else
	if(p->reg == NREG)
		snprint(str, sizeof(str), "	%A	%D,%D", a, &p->from, &p->to);
	else
	if(p->from.type != D_FREG)
		snprint(str, sizeof(str), "	%A	%D,R%d,%D", a, &p->from, p->reg, &p->to);
	else
		snprint(str, sizeof(str), "	%A	%D,F%d,%D", a, &p->from, p->reg, &p->to);
	return fmtstrcpy(fp, str);
}

int
Aconv(Fmt *fp)
{
	char *s;
	int a;

	a = va_arg(fp->args, int);
	s = "???";
	if(a >= AXXX && a < ALAST)
		s = anames[a];
	return fmtstrcpy(fp, s);
}

int
Dconv(Fmt *fp)
{
	Adr *a;
	char *op;
	int v;
	static char *extop[] = {".UB", ".UH", ".UW", ".UX", ".SB", ".SH", ".SW", ".SX"};

	a = va_arg(fp->args, Adr*);
	switch(a->type) {
	default:
		return fmtprint(fp, "GOK-type(%d)", a->type);

	case D_NONE:
		if(a->name != D_NONE || a->reg != NREG || a->sym != S)
			return fmtprint(fp, "%N(R%d)(NONE)", a, a->reg);
		return 0;

	case D_CONST:
		if(a->reg != NREG)
			return fmtprint(fp, "$%N(R%d)", a, a->reg);
		else
			return fmtprint(fp, "$%N", a);

	case D_SHIFT:
		v = a->offset;
		op = "<<>>->@>" + (((v>>5) & 3) << 1);
		if(v & (1<<4))
			fmtprint(fp, "R%d%c%cR%d", v&15, op[0], op[1], (v>>8)&15);
		else
			fmtprint(fp, "R%d%c%c%d", v&15, op[0], op[1], (v>>7)&31);
		if(a->reg != NREG)
			fmtprint(fp, "(R%d)", a->reg);
		return 0;

	case D_OREG:
		if(a->reg != NREG)
			return fmtprint(fp, "%N(R%d)", a, a->reg);
		else
			return fmtprint(fp, "%N", a);

	case D_XPRE:
		if(a->reg != NREG)
			return fmtprint(fp, "%N(R%d)!", a, a->reg);
		else
			return fmtprint(fp, "%N!", a);

	case D_XPOST:
		if(a->reg != NREG)
			return fmtprint(fp, "(R%d)%N!", a->reg, a);
		else
			return fmtprint(fp, "%N!", a);

	case D_EXTREG:
		v = a->offset;
		if(v & (7<<10))
			return fmtprint(fp, "R%d%s<<%d", (v>>16)&31, extop[(v>>13)&7], (v>>10)&7);
		else
			return fmtprint(fp, "R%d%s", (v>>16)&31, extop[(v>>13)&7]);

	case D_REG:
		if(a->name != D_NONE || a->sym != S)
			return fmtprint(fp, "%N(R%d)(REG)", a, a->reg);
		else
			return fmtprint(fp, "R%d", a->reg);

	case D_SP:
		if(a->name != D_NONE || a->sym != S)
			return fmtprint(fp, "%N(R%d)(REG)", a, a->reg);
		else
			return fmtprint(fp, "SP");

	case D_FREG:
		if(a->name != D_NONE || a->sym != S)
			return fmtprint(fp, "%N(R%d)(REG)", a, a->reg);
		else
			return fmtprint(fp, "F%d", a->reg);

	case D_SPR:
		if(a->name != D_NONE || a->sym != S)
			return fmtprint(fp, "%N(SPR(%lld))(REG)", a, a->offset);
		switch((ulong)a->offset){
		case D_FPSR:
			return fmtprint(fp, "FPSR");
		case D_FPCR:
			return fmtprint(fp, "FPCR");
		case D_NZCV:
			return fmtprint(fp, "NZCV");
		default:
			return fmtprint(fp, "SPR(%#llux)", a->offset);
		}

	case D_BRANCH:
		return fmtprint(fp, "%lld(PC)", a->offset-pc);

	case D_FCONST:
		return fmtprint(fp, "$%.17e", a->dval);

	case D_SCONST:
		return fmtprint(fp, "$\"%S\"", a->sval);
	}
}

int
Rconv(Fmt *fp)
{
	char str[STRINGSZ], *p, *e;
	Adr *a;
	int i, v;

	a = va_arg(fp->args, Adr*);
	snprint(str, sizeof(str), "GOK-reglist");
	switch(a->type) {
	case D_CONST:
		if(a->reg != NREG)
			break;
		if(a->sym != S)
			break;
		v = a->offset;
		p = str;
		e = str+sizeof(str);
		for(i=0; i<NREG; i++) {
			if(v & (1<<i)) {
				if(p == str)
					p = seprint(p, e, "[R%d", i);
				else
					p = seprint(p, e, ",R%d", i);
			}
		}
		seprint(p, e, "]");
	}
	return fmtstrcpy(fp, str);
}

int
Sconv(Fmt *fp)
{
	int i, c;
	char str[STRINGSZ], *p, *a;

	a = va_arg(fp->args, char*);
	p = str;
	for(i=0; i<NSNAME; i++) {
		c = a[i] & 0xff;
		if(c >= 'a' && c <= 'z' ||
		   c >= 'A' && c <= 'Z' ||
		   c >= '0' && c <= '9' ||
		   c == ' ' || c == '%') {
			*p++ = c;
			continue;
		}
		*p++ = '\\';
		switch(c) {
		case 0:
			*p++ = 'z';
			continue;
		case '\\':
		case '"':
			*p++ = c;
			continue;
		case '\n':
			*p++ = 'n';
			continue;
		case '\t':
			*p++ = 't';
			continue;
		case '\r':
			*p++ = 'r';
			continue;
		case '\f':
			*p++ = 'f';
			continue;
		}
		*p++ = (c>>6) + '0';
		*p++ = ((c>>3) & 7) + '0';
		*p++ = (c & 7) + '0';
	}
	*p = 0;
	return fmtstrcpy(fp, str);
}

int
Nconv(Fmt *fp)
{
	Adr *a;
	Sym *s;

	a = va_arg(fp->args, Adr*);
	s = a->sym;
	if(s == S)
		return fmtprint(fp, "%lld", a->offset);
	switch(a->name) {
	default:
		return fmtprint(fp, "GOK-name(%d)", a->name);

	case D_NONE:
		return fmtprint(fp, "%lld", a->offset);

	case D_EXTERN:
		return fmtprint(fp, "%s+%lld(SB)", s->name, a->offset);

	case D_STATIC:
		return fmtprint(fp, "%s<>+%lld(SB)", s->name, a->offset);

	case D_AUTO:
		return fmtprint(fp, "%s-%lld(SP)", s->name, -a->offset);

	case D_PARAM:
		return fmtprint(fp, "%s+%lld(FP)", s->name, a->offset);
	}
}
