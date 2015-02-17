#include "l.h"

void
listinit(void)
{

	fmtinstall('A', Aconv);
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
	if(a == ADATA || a == ADYNT || a == AINIT)
		snprint(str, sizeof str, "(%ld)	%A	%D/%d,%D",
			p->line, a, &p->from, p->reg, &p->to);
	else{
		if(p->reg == NREG)
			snprint(str, sizeof str, "(%ld)%s	%A	%D,%D",
				p->line, p->mark & NOSCHED ? "*" : "",
				a, &p->from, &p->to);
		else
		if(p->from.type != D_FREG)
			snprint(str, sizeof str, "(%ld)%s	%A	%D,R%d,%D",
				p->line, p->mark & NOSCHED ? "*" : "",
				a, &p->from, p->reg, &p->to);
		else
			snprint(str, sizeof str, "(%ld)%s	%A	%D,F%d,%D",
				p->line, p->mark & NOSCHED ? "*" : "",
				a, &p->from, p->reg, &p->to);
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

int
Dconv(Fmt *fp)
{
	char str[STRINGSZ];
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
		snprint(str, sizeof str, "$%N", a);
		if(a->reg != NREG)
			snprint(str, sizeof str, "%N(R%d)(CONST)", a, a->reg);
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

	case D_MREG:
		snprint(str, sizeof str, "M%d", a->reg);
		if(a->name != D_NONE || a->sym != S)
			snprint(str, sizeof str, "%N(R%d)(REG)", a, a->reg);
		break;

	case D_FREG:
		snprint(str, sizeof str, "F%d", a->reg);
		if(a->name != D_NONE || a->sym != S)
			snprint(str, sizeof str, "%N(R%d)(REG)", a, a->reg);
		break;

	case D_FCREG:
		snprint(str, sizeof str, "FC%d", a->reg);
		if(a->name != D_NONE || a->sym != S)
			snprint(str, sizeof str, "%N(R%d)(REG)", a, a->reg);
		break;

	case D_LO:
		snprint(str, sizeof str, "LO");
		if(a->name != D_NONE || a->sym != S)
			snprint(str, sizeof str, "%N(LO)(REG)", a);
		break;

	case D_HI:
		snprint(str, sizeof str, "HI");
		if(a->name != D_NONE || a->sym != S)
			snprint(str, sizeof str, "%N(HI)(REG)", a);
		break;

	case D_BRANCH:	/* botch */
		if(curp->cond != P) {
			v = curp->cond->pc;
			if(v >= INITTEXT)
				v -= INITTEXT-HEADR;
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
