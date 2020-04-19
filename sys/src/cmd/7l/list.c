#include "l.h"

void
listinit(void)
{

	fmtinstall('A', Aconv);
	fmtinstall('D', Dconv);
	fmtinstall('P', Pconv);
	fmtinstall('S', Sconv);
	fmtinstall('N', Nconv);
	fmtinstall('R', Rconv);
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
	case ADATA:
	case AINIT:
	case ADYNT:
		return fmtprint(fp, "(%ld)	%A	%D/%d,%D",
			p->line, a, &p->from, p->reg, &p->to);

	default:
		if(p->reg == NREG && p->from3.type == D_NONE)
			return fmtprint(fp, "(%ld)	%A	%D,%D",
				p->line, a, &p->from, &p->to);
		else if(p->from.type == D_FREG)
			return fmtprint(fp, "(%ld)	%A	%D,F%d,%D",
				p->line, a, &p->from, p->reg, &p->to);

		fmtprint(fp, "(%ld)	%A	%D", p->line, a, &p->from);
		if(p->from3.type != D_NONE)
			fmtprint(fp, ",%D", &p->from3);
		if(p->reg != NREG)
			fmtprint(fp, ",R%d", p->reg);
		fmtprint(fp, ",%D", &p->to);
		return 0;
	}
}

int
Aconv(Fmt *fp)
{
	char *s;
	int a;

	a = va_arg(fp->args, int);
	s = "???";
	if(a >= AXXX && a < ALAST && anames[a])
		s = anames[a];
	return fmtstrcpy(fp, s);
}

char*	strcond[16] =
{
	"EQ",
	"NE",
	"HS",
	"LO",
	"MI",
	"PL",
	"VS",
	"VC",
	"HI",
	"LS",
	"GE",
	"LT",
	"GT",
	"LE",
	"AL",
	"NV"
};

int
Dconv(Fmt *fp)
{
	char *op;
	Adr *a;
	long v;
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
		if(a->reg == NREG || a->reg == REGZERO)
			return fmtprint(fp, "$%N", a);
		else
			return fmtprint(fp, "$%N(R%d)", a, a->reg);

	case D_SHIFT:
		v = a->offset;
		op = "<<>>->@>" + (((v>>22) & 3) << 1);
		if(a->reg == NREG)
			return fmtprint(fp, "R%ld%c%c%ld",
				(v>>16)&0x1F, op[0], op[1], (v>>10)&0x3F);
		else
			return fmtprint(fp, "R%ld%c%c%ld(R%d)",
				(v>>16)&0x1F, op[0], op[1], (v>>10)&0x3F, a->reg);

	case D_OCONST:
		if(a->reg != NREG)
			return fmtprint(fp, "$*$%N(R%d)(CONST)", a, a->reg);
		else
			return fmtprint(fp, "$*$%N", a);

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
			return fmtprint(fp, "R%ld%s<<%ld", (v>>16)&31, extop[(v>>13)&7], (v>>10)&7);
		else
			return fmtprint(fp, "R%ld%s", (v>>16)&31, extop[(v>>13)&7]);

	case D_ROFF:
		v = a->offset;
		if(v & (1<<16))
			return fmtprint(fp, "(R%d)[R%ld%s]", a->reg, v&31, extop[(v>>8)&7]);
		else
			return fmtprint(fp, "(R%d)(R%ld%s)", a->reg, v&31, extop[(v>>8)&7]);

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

	case D_COND:
		return fmtprint(fp, "%s", strcond[a->reg & 0xF]);

	case D_FREG:
		if(a->name != D_NONE || a->sym != S)
			return fmtprint(fp, "%N(R%d)(REG)", a, a->reg);
		else
			return fmtprint(fp, "F%d", a->reg);

	case D_SPR:
		if(a->name != D_NONE || a->sym != S)
			return fmtprint(fp, "%N(SPR%lld)(REG)", a, a->offset);
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

	case D_BRANCH:	/* botch */
		if(curp->cond != P) {
			v = curp->cond->pc;
			if(a->sym != S)
				return fmtprint(fp, "%s+%#.5lux(BRANCH)", a->sym->name, v);
			else
				return fmtprint(fp, "%.5lux(BRANCH)", v);
		} else {
			if(a->sym != S)
				return fmtprint(fp, "%s+%lld(APC)", a->sym->name, a->offset);
			else
				return fmtprint(fp, "%lld(APC)", a->offset);
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
		return fmtprint(fp, "%lld", a->offset);

	case D_EXTERN:
		if(s == S)
			return fmtprint(fp, "%lld(SB)", a->offset);
		else
			return fmtprint(fp, "%s+%lld(SB)", s->name, a->offset);

	case D_STATIC:
		if(s == S)
			return fmtprint(fp, "<>+%lld(SB)", a->offset);
		else
			return fmtprint(fp, "%s<>+%lld(SB)", s->name, a->offset);

	case D_AUTO:
		if(s == S)
			return fmtprint(fp, "%lld(SP)", a->offset);
		else
			return fmtprint(fp, "%s-%lld(SP)", s->name, -a->offset);

	case D_PARAM:
		if(s == S)
			return fmtprint(fp, "%lld(FP)", a->offset);
		else
			return fmtprint(fp, "%s+%lld(FP)", s->name, a->offset);
	}
}

int
Rconv(Fmt *fp)
{
	char *s;
	int a;

	a = va_arg(fp->args, int);
	s = "C_??";
	if(a >= C_NONE && a <= C_NCLASS)
		s = cnames[a];
	return fmtstrcpy(fp, s);
}

void
prasm(Prog *p)
{
	print("%P\n", p);
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
