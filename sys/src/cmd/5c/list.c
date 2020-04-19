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
			fmtprint(fp, "$%ld ", var[i].offset);
		else
			fmtprint(fp, "%s ", var[i].sym->name);
	}
	return 0;
}

char *extra [] = {
	".EQ", ".NE", ".CS", ".CC", 
	".MI", ".PL", ".VS", ".VC", 
	".HI", ".LS", ".GE", ".LT", 
	".GT", ".LE", "", ".NV",
};

int
Pconv(Fmt *fp)
{
	char sc[20];
	Prog *p;
	int a, s;

	p = va_arg(fp->args, Prog*);
	a = p->as;
	s = p->scond; 
	strcpy(sc, extra[s & C_SCOND]);
	if(s & C_SBIT)
		strcat(sc, ".S");
	if(s & C_PBIT)
		strcat(sc, ".P");
	if(s & C_WBIT)
		strcat(sc, ".W");
	if(s & C_UBIT)		/* ambiguous with FBIT */
		strcat(sc, ".U");
	if(a == AMULL || a == AMULAL || a == AMULLU || a == AMULALU)
		return fmtprint(fp, "	%A%s	%D,R%d,%D", a, sc, &p->from, p->reg, &p->to);
	else
	if(a == AMOVM) {
		if(p->from.type == D_CONST)
			return fmtprint(fp, "	%A%s	%R,%D", a, sc, &p->from, &p->to);
		else
		if(p->to.type == D_CONST)
			return fmtprint(fp, "	%A%s	%D,%R", a, sc, &p->from, &p->to);
		else
			return fmtprint(fp, "	%A%s	%D,%D", a, sc, &p->from, &p->to);
	} else
	if(a == ADATA)
		return fmtprint(fp, "	%A	%D/%d,%D", a, &p->from, p->reg, &p->to);
	else
	if(p->as == ATEXT)
		return fmtprint(fp, "	%A	%D,%d,%D", a, &p->from, p->reg, &p->to);
	else
	if(p->reg == NREG)
		return fmtprint(fp, "	%A%s	%D,%D", a, sc, &p->from, &p->to);
	else
	if(p->from.type != D_FREG)
		return fmtprint(fp, "	%A%s	%D,R%d,%D", a, sc, &p->from, p->reg, &p->to);
	else
		return fmtprint(fp, "	%A%s	%D,F%d,%D", a, sc, &p->from, p->reg, &p->to);
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
		else {
			int sh = (v>>7)&31;
			if(sh == 0 && (v & (3<<5)) != 0)
				sh = 32;
			fmtprint(fp, "R%d%c%c%d", v&15, op[0], op[1], sh);
		}
		if(a->reg != NREG)
			fmtprint(fp, "(R%d)", a->reg);
		return 0;

	case D_OREG:
		if(a->reg != NREG)
			return fmtprint(fp, "%N(R%d)", a, a->reg);
		else
			return fmtprint(fp, "%N", a);

	case D_REGREG:
		return fmtprint(fp, "(R%d,R%d)", a->reg, (char)a->offset);

	case D_REG:
		if(a->name != D_NONE || a->sym != S)
			return fmtprint(fp, "%N(R%d)(REG)", a, a->reg);
		else
			return fmtprint(fp, "R%d", a->reg);

	case D_FREG:
		if(a->name != D_NONE || a->sym != S)
			return fmtprint(fp, "%N(R%d)(REG)", a, a->reg);
		else
			return fmtprint(fp, "F%d", a->reg);

	case D_PSR:
		if(a->name != D_NONE || a->sym != S)
			return fmtprint(fp, "%N(PSR)(REG)", a);
		else
			return fmtprint(fp, "PSR");

	case D_BRANCH:
		return fmtprint(fp, "%ld(PC)", a->offset-pc);

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
		return fmtprint(fp, "%ld", a->offset);
	switch(a->name) {
	default:
		return fmtprint(fp, "GOK-name(%d)", a->name);

	case D_NONE:
		return fmtprint(fp, "%ld", a->offset);

	case D_EXTERN:
		return fmtprint(fp, "%s+%ld(SB)", s->name, a->offset);

	case D_STATIC:
		return fmtprint(fp, "%s<>+%ld(SB)", s->name, a->offset);

	case D_AUTO:
		return fmtprint(fp, "%s-%ld(SP)", s->name, -a->offset);

	case D_PARAM:
		return fmtprint(fp, "%s+%ld(FP)", s->name, a->offset);
	}
}
