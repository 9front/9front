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
	char str[STRINGSZ];
	Prog *p;
	int a;

	p = va_arg(fp->args, Prog*);
	curp = p;
	a = p->as;
	switch(a) {
	default:
		if(p->reg == NREG)
			snprint(str, sizeof str, "(%ld)	%A%C	%D,%D",
				p->line, a, p->scond, &p->from, &p->to);
		else
		if(p->from.type != D_FREG)
			snprint(str, sizeof str, "(%ld)	%A%C	%D,R%d,%D",
				p->line, a, p->scond, &p->from, p->reg, &p->to);
		else
			snprint(str, sizeof str, "(%ld)	%A%C	%D,F%d,%D",
				p->line, a, p->scond, &p->from, p->reg, &p->to);
		break;

	case ASWPW:
	case ASWPBU:
		snprint(str, sizeof str, "(%ld)	%A%C	R%d,%D,%D",
			p->line, a, p->scond, p->reg, &p->from, &p->to);
		break;

	case ADATA:
	case AINIT:
	case ADYNT:
		snprint(str, sizeof str, "(%ld)	%A%C	%D/%d,%D",
			p->line, a, p->scond, &p->from, p->reg, &p->to);
		break;
	}
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
	char str[STRINGSZ];
	char *op;
	Adr *a;
	long v;

	a = va_arg(fp->args, Adr*);
	switch(a->type) {

	default:
		snprint(str, sizeof str, "GOK-type(%d)", a->type);
		break;

	case D_NONE:
		str[0] = 0;
		if(a->name != D_NONE || a->reg != NREG || a->sym != S)
			snprint(str, sizeof str, "%N(R%d)(NONE)", a, a->reg);
		break;

	case D_CONST:
		if(a->reg == NREG)
			snprint(str, sizeof str, "$%N", a);
		else
			snprint(str, sizeof str, "$%N(R%d)", a, a->reg);
		break;

	case D_SHIFT:
		v = a->offset;
		op = "<<>>->@>" + (((v>>5) & 3) << 1);
		if(v & (1<<4))
			snprint(str, sizeof str, "R%ld%c%cR%ld", v&15, op[0], op[1], (v>>8)&15);
		else {
			long sh = (v>>7)&31;
			if(sh == 0 && (v & (3<<5)) != 0)
				sh = 32;
			snprint(str, sizeof str, "R%ld%c%c%ld", v&15, op[0], op[1], sh);
		}
		if(a->reg != NREG)
			snprint(str+strlen(str), sizeof(str)-strlen(str), "(R%d)", a->reg);
		break;

	case D_OCONST:
		snprint(str, sizeof str, "$*$%N", a);
		if(a->reg != NREG)
			snprint(str, sizeof str, "%N(R%d)(CONST)", a, a->reg);
		break;

	case D_OREG:
		if(a->reg != NREG)
			snprint(str, sizeof str, "%N(R%d)", a, a->reg);
		else
			snprint(str, sizeof str, "%N", a);
		break;

	case D_REG:
		snprint(str, sizeof str, "R%d", a->reg);
		if(a->name != D_NONE || a->sym != S)
			snprint(str, sizeof str, "%N(R%d)(REG)", a, a->reg);
		break;

	case D_REGREG:
		snprint(str, sizeof str, "(R%d,R%d)", a->reg, (int)a->offset);
		if(a->name != D_NONE || a->sym != S)
			snprint(str, sizeof str, "%N(R%d)(REG)", a, a->reg);
		break;

	case D_FREG:
		snprint(str, sizeof str, "F%d", a->reg);
		if(a->name != D_NONE || a->sym != S)
			snprint(str, sizeof str, "%N(R%d)(REG)", a, a->reg);
		break;

	case D_PSR:
		switch(a->reg) {
		case 0:
			snprint(str, sizeof str, "CPSR");
			break;
		case 1:
			snprint(str, sizeof str, "SPSR");
			break;
		default:
			snprint(str, sizeof str, "PSR%d", a->reg);
			break;
		}
		if(a->name != D_NONE || a->sym != S)
			snprint(str, sizeof str, "%N(PSR%d)(REG)", a, a->reg);
		break;

	case D_FPCR:
		switch(a->reg){
		case 0:
			snprint(str, sizeof str, "FPSR");
			break;
		case 1:
			snprint(str, sizeof str, "FPCR");
			break;
		default:
			snprint(str, sizeof str, "FCR%d", a->reg);
			break;
		}
		if(a->name != D_NONE || a->sym != S)
			snprint(str, sizeof str, "%N(FCR%d)(REG)", a, a->reg);

		break;

	case D_BRANCH:	/* botch */
		if(curp->cond != P) {
			v = curp->cond->pc;
			if(a->sym != S)
				snprint(str, sizeof str, "%s+%.5lux(BRANCH)", a->sym->name, v);
			else
				snprint(str, sizeof str, "%.5lux(BRANCH)", v);
		} else
			if(a->sym != S)
				snprint(str, sizeof str, "%s+%ld(APC)", a->sym->name, a->offset);
			else
				snprint(str, sizeof str, "%ld(APC)", a->offset);
		break;

	case D_FCONST:
		snprint(str, sizeof str, "$%e", ieeedtod(a->ieee));
		break;

	case D_SCONST:
		snprint(str, sizeof str, "$\"%S\"", a->sval);
		break;
	}
	return fmtstrcpy(fp, str);
}

int
Nconv(Fmt *fp)
{
	char str[STRINGSZ];
	Adr *a;
	Sym *s;

	a = va_arg(fp->args, Adr*);
	s = a->sym;
	switch(a->name) {
	default:
		snprint(str, sizeof str, "GOK-name(%d)", a->name);
		break;

	case D_NONE:
		snprint(str, sizeof str, "%ld", a->offset);
		break;

	case D_EXTERN:
		if(s == S)
			snprint(str, sizeof str, "%ld(SB)", a->offset);
		else
			snprint(str, sizeof str, "%s+%ld(SB)", s->name, a->offset);
		break;

	case D_STATIC:
		if(s == S)
			snprint(str, sizeof str, "<>+%ld(SB)", a->offset);
		else
			snprint(str, sizeof str, "%s<>+%ld(SB)", s->name, a->offset);
		break;

	case D_AUTO:
		if(s == S)
			snprint(str, sizeof str, "%ld(SP)", a->offset);
		else
			snprint(str, sizeof str, "%s-%ld(SP)", s->name, -a->offset);
		break;

	case D_PARAM:
		if(s == S)
			snprint(str, sizeof str, "%ld(FP)", a->offset);
		else
			snprint(str, sizeof str, "%s+%ld(FP)", s->name, a->offset);
		break;
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
