#include "l.h"

void
listinit(void)
{

	fmtinstall('A', Aconv);
	fmtinstall('C', Cconv);
	fmtinstall('D', Dconv);
	fmtinstall('P', Pconv);
	fmtinstall('S', Sconv);
	fmtinstall('N', Nconv);
}

void
prasm(Prog *p)
{
	print("%P\n", p);
}

int
Pconv(Fmt *fp)
{
	Prog *p;
	int a;

	p = va_arg(fp->args, Prog*);
	curp = p;
	a = p->as;
	switch(a) {
	default:
		if(p->reg == NREG)
			return fmtprint(fp, "(%ld)	%A%C	%D,%D",
				p->line, a, p->scond, &p->from, &p->to);
		else
		if(p->from.type != D_FREG)
			return fmtprint(fp, "(%ld)	%A%C	%D,R%d,%D",
				p->line, a, p->scond, &p->from, p->reg, &p->to);
		else
			return fmtprint(fp, "(%ld)	%A%C	%D,F%d,%D",
				p->line, a, p->scond, &p->from, p->reg, &p->to);

	case ASWPW:
	case ASWPBU:
		return fmtprint(fp, "(%ld)	%A%C	R%d,%D,%D",
			p->line, a, p->scond, p->reg, &p->from, &p->to);

	case ADATA:
	case AINIT:
	case ADYNT:
		return fmtprint(fp, "(%ld)	%A%C	%D/%d,%D",
			p->line, a, p->scond, &p->from, p->reg, &p->to);
	}
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

char*	strcond[16] =
{
	".EQ",
	".NE",
	".HS",
	".LO",
	".MI",
	".PL",
	".VS",
	".VC",
	".HI",
	".LS",
	".GE",
	".LT",
	".GT",
	".LE",
	"",
	".NV"
};

int
Cconv(Fmt *fp)
{
	char s[20];
	int c;

	c = va_arg(fp->args, int);
	strcpy(s, strcond[c & C_SCOND]);
	if(c & C_SBIT)
		strcat(s, ".S");
	if(c & C_PBIT)
		strcat(s, ".P");
	if(c & C_WBIT)
		strcat(s, ".W");
	if(c & C_UBIT)		/* ambiguous with FBIT */
		strcat(s, ".U");
	return fmtstrcpy(fp, s);
}

int
Dconv(Fmt *fp)
{
	char *op;
	Adr *a;
	long v;

	a = va_arg(fp->args, Adr*);
	switch(a->type) {
	default:
		return fmtprint(fp, "GOK-type(%d)", a->type);

	case D_NONE:
		if(a->name != D_NONE || a->reg != NREG || a->sym != S)
			return fmtprint(fp, "%N(R%d)(NONE)", a, a->reg);
		return 0;

	case D_CONST:
		if(a->reg == NREG)
			return fmtprint(fp, "$%N", a);
		else
			return fmtprint(fp, "$%N(R%d)", a, a->reg);

	case D_SHIFT:
		v = a->offset;
		op = "<<>>->@>" + (((v>>5) & 3) << 1);
		if(v & (1<<4))
			fmtprint(fp, "R%ld%c%cR%ld", v&15, op[0], op[1], (v>>8)&15);
		else {
			long sh = (v>>7)&31;
			if(sh == 0 && (v & (3<<5)) != 0)
				sh = 32;
			fmtprint(fp, "R%ld%c%c%ld", v&15, op[0], op[1], sh);
		}
		if(a->reg != NREG)
			fmtprint(fp, "(R%d)", a->reg);
		return 0;

	case D_OCONST:
		if(a->reg != NREG)
			return fmtprint(fp, "%N(R%d)(CONST)", a, a->reg);
		else
			return fmtprint(fp, "$*$%N", a);

	case D_OREG:
		if(a->reg != NREG)
			return fmtprint(fp, "%N(R%d)", a, a->reg);
		else
			return fmtprint(fp, "%N", a);

	case D_REG:
		if(a->name != D_NONE || a->sym != S)
			return fmtprint(fp, "%N(R%d)(REG)", a, a->reg);
		else
			return fmtprint(fp, "R%d", a->reg);

	case D_REGREG:
		if(a->name != D_NONE || a->sym != S)
			return fmtprint(fp, "%N(R%d)(REG)", a, a->reg);
		else
			return fmtprint(fp, "(R%d,R%d)", a->reg, (int)a->offset);

	case D_FREG:
		if(a->name != D_NONE || a->sym != S)
			return fmtprint(fp, "%N(R%d)(REG)", a, a->reg);
		else
			return fmtprint(fp, "F%d", a->reg);

	case D_PSR:
		if(a->name != D_NONE || a->sym != S)
			return fmtprint(fp, "%N(PSR%d)(REG)", a, a->reg);
		switch(a->reg) {
		case 0:
			return fmtprint(fp, "CPSR");
		case 1:
			return fmtprint(fp, "SPSR");
		default:
			return fmtprint(fp, "PSR%d", a->reg);
		}

	case D_FPCR:
		if(a->name != D_NONE || a->sym != S)
			return fmtprint(fp, "%N(FCR%d)(REG)", a, a->reg);
		switch(a->reg){
		case 0:
			return fmtprint(fp, "FPSR");
		case 1:
			return fmtprint(fp, "FPCR");
		default:
			return fmtprint(fp, "FCR%d", a->reg);
		}

	case D_BRANCH:	/* botch */
		if(curp->cond != P) {
			v = curp->cond->pc;
			if(a->sym != S)
				return fmtprint(fp, "%s+%.5lux(BRANCH)", a->sym->name, v);
			else
				return fmtprint(fp, "%.5lux(BRANCH)", v);
		} else {
			if(a->sym != S)
				return fmtprint(fp, "%s+%ld(APC)", a->sym->name, a->offset);
			else
				return fmtprint(fp, "%ld(APC)", a->offset);
		}

	case D_FCONST:
		return fmtprint(fp, "$%e", ieeedtod(a->ieee));

	case D_SCONST:
		return fmtprint(fp, "$\"%S\"", a->sval);
	}
}

int
Nconv(Fmt *fp)
{
	Adr *a;
	Sym *s;

	a = va_arg(fp->args, Adr*);
	s = a->sym;
	switch(a->name) {
	default:
		return fmtprint(fp, "GOK-name(%d)", a->name);

	case D_NONE:
		return fmtprint(fp, "%ld", a->offset);

	case D_EXTERN:
		if(s == S)
			return fmtprint(fp, "%ld(SB)", a->offset);
		else
			return fmtprint(fp, "%s+%ld(SB)", s->name, a->offset);

	case D_STATIC:
		if(s == S)
			return fmtprint(fp, "<>+%ld(SB)", a->offset);
		else
			return fmtprint(fp, "%s<>+%ld(SB)", s->name, a->offset);

	case D_AUTO:
		if(s == S)
			return fmtprint(fp, "%ld(SP)", a->offset);
		else
			return fmtprint(fp, "%s-%ld(SP)", s->name, -a->offset);

	case D_PARAM:
		if(s == S)
			return fmtprint(fp, "%ld(FP)", a->offset);
		else
			return fmtprint(fp, "%s+%ld(FP)", s->name, a->offset);
	}
}

int
Sconv(Fmt *fp)
{
	int i, c;
	char str[STRINGSZ], *p, *a;

	a = va_arg(fp->args, char*);
	p = str;
	for(i=0; i<sizeof(long); i++) {
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
		}
		*p++ = (c>>6) + '0';
		*p++ = ((c>>3) & 7) + '0';
		*p++ = (c & 7) + '0';
	}
	*p = 0;
	return fmtstrcpy(fp, str);
}

void
diag(char *fmt, ...)
{
	char buf[STRINGSZ], *tn;
	va_list arg;

	tn = "??none??";
	if(curtext != P && curtext->from.sym != S)
		tn = curtext->from.sym->name;
	va_start(arg, fmt);
	vseprint(buf, buf+sizeof(buf), fmt, arg);
	va_end(arg);
	print("%s: %s\n", tn, buf);

	nerrors++;
	if(nerrors > 10) {
		print("too many errors\n");
		errorexit();
	}
}
