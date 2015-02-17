#include	"l.h"

void
listinit(void)
{

	fmtinstall('R', Rconv);
	fmtinstall('A', Aconv);
	fmtinstall('D', Dconv);
	fmtinstall('S', Sconv);
	fmtinstall('P', Pconv);
}

static	Prog	*bigP;

int
Pconv(Fmt *fp)
{
	char str[STRINGSZ];
	Prog *p;

	p = va_arg(fp->args, Prog*);
	bigP = p;
	snprint(str, sizeof str, "(%ld)	%A	%D,%D",
		p->line, p->as, &p->from, &p->to);
	if(p->from.field)
		return fmtprint(fp, "%s,%d,%d", str, p->to.field, p->from.field);
	bigP = P;
	return fmtstrcpy(fp, str);
}

int
Aconv(Fmt *fp)
{
	return fmtstrcpy(fp, anames[va_arg(fp->args, int)]);
}

int
Dconv(Fmt *fp)
{
	char str[40];
	Adr *a;
	int i, j;
	long d;

	a = va_arg(fp->args, Adr*);
	i = a->type;
	j = i & I_MASK;
	if(j) {
		a->type = i & D_MASK;
		d = a->offset;
		a->offset = 0;
		switch(j) {
		case I_INDINC:
			snprint(str, sizeof str, "(%D)+", a);
			break;

		case I_INDDEC:
			snprint(str, sizeof str, "-(%D)", a);
			break;

		case I_INDIR:
			if(d)
				snprint(str, sizeof str, "%ld(%D)", d, a);
			else
				snprint(str, sizeof str, "(%D)", a);
			break;

		case I_ADDR:
			a->offset = d;
			snprint(str, sizeof str, "$%D", a);
			break;
		}
		a->type = i;
		a->offset = d;
		goto out;
	}
	switch(i) {

	default:
		snprint(str, sizeof str, "%R", i);
		break;

	case D_NONE:
		str[0] = 0;
		break;

	case D_BRANCH:
		if(bigP != P && bigP->pcond != P)
			if(a->sym != S)
				snprint(str, sizeof str, "%lux+%s", bigP->pcond->pc,
					a->sym->name);
			else
				snprint(str, sizeof str, "%lux", bigP->pcond->pc);
		else
			snprint(str, sizeof str, "%ld(PC)", a->offset);
		break;

	case D_EXTERN:
		snprint(str, sizeof str, "%s+%ld(SB)", a->sym->name, a->offset);
		break;

	case D_STATIC:
		snprint(str, sizeof str, "%s<%d>+%ld(SB)", a->sym->name,
			a->sym->version, a->offset);
		break;

	case D_AUTO:
		snprint(str, sizeof str, "%s+%ld(SP)", a->sym->name, a->offset);
		break;

	case D_PARAM:
		if(a->sym)
			snprint(str, sizeof str, "%s+%ld(FP)", a->sym->name, a->offset);
		else
			snprint(str, sizeof str, "%ld(FP)", a->offset);
		break;

	case D_CONST:
		snprint(str, sizeof str, "$%ld", a->offset);
		break;

	case D_STACK:
		snprint(str, sizeof str, "TOS+%ld", a->offset);
		break;

	case D_QUICK:
		snprint(str, sizeof str, "$Q%ld", a->offset);
		break;

	case D_FCONST:
		snprint(str, sizeof str, "$(%.8lux,%.8lux)", a->ieee.h, a->ieee.l);
		goto out;

	case D_SCONST:
		snprint(str, sizeof str, "$\"%S\"", a->scon);
		goto out;
	}
	if(a->displace)
		return fmtprint(fp, "%s/%ld", str, a->displace);
out:
	return fmtstrcpy(fp, str);
}

int
Rconv(Fmt *fp)
{
	char str[20];
	int r;

	r = va_arg(fp->args, int);
	if(r >= D_R0 && r < D_R0+NREG)
		snprint(str, sizeof str, "R%d", r-D_R0);
	else
	if(r >= D_A0 && r < D_A0+NREG)
		snprint(str, sizeof str, "A%d", r-D_A0);
	else
	if(r >= D_F0 && r < D_F0+NREG)
		snprint(str, sizeof str, "F%d", r-D_F0);
	else
	switch(r) {

	default:
		snprint(str, sizeof str, "gok(%d)", r);
		break;

	case D_NONE:
		snprint(str, sizeof str, "NONE");
		break;

	case D_TOS:
		snprint(str, sizeof str, "TOS");
		break;

	case D_CCR:
		snprint(str, sizeof str, "CCR");
		break;

	case D_SR:
		snprint(str, sizeof str, "SR");
		break;

	case D_SFC:
		snprint(str, sizeof str, "SFC");
		break;

	case D_DFC:
		snprint(str, sizeof str, "DFC");
		break;

	case D_CACR:
		snprint(str, sizeof str, "CACR");
		break;

	case D_USP:
		snprint(str, sizeof str, "USP");
		break;

	case D_VBR:
		snprint(str, sizeof str, "VBR");
		break;

	case D_CAAR:
		snprint(str, sizeof str, "CAAR");
		break;

	case D_MSP:
		snprint(str, sizeof str, "MSP");
		break;

	case D_ISP:
		snprint(str, sizeof str, "ISP");
		break;

	case D_FPCR:
		snprint(str, sizeof str, "FPCR");
		break;

	case D_FPSR:
		snprint(str, sizeof str, "FPSR");
		break;

	case D_FPIAR:
		snprint(str, sizeof str, "FPIAR");
		break;

	case D_TREE:
		snprint(str, sizeof str, "TREE");
		break;

	case D_TC:
		snprint(str, sizeof str, "TC");
		break;

	case D_ITT0:
		snprint(str, sizeof str, "ITT0");
		break;

	case D_ITT1:
		snprint(str, sizeof str, "ITT1");
		break;

	case D_DTT0:
		snprint(str, sizeof str, "DTT0");
		break;

	case D_DTT1:
		snprint(str, sizeof str, "DTT1");
		break;

	case D_MMUSR:
		snprint(str, sizeof str, "MMUSR");
		break;
	case D_URP:
		snprint(str, sizeof str, "URP");
		break;

	case D_SRP:
		snprint(str, sizeof str, "SRP");
		break;
	}
	return fmtstrcpy(fp, str);
}

int
Sconv(Fmt *fp)
{
	int i, c;
	char str[30], *p, *a;

	a = va_arg(fp->args, char*);
	p = str;
	for(i=0; i<sizeof(double); i++) {
		c = a[i] & 0xff;
		if(c >= 'a' && c <= 'z' ||
		   c >= 'A' && c <= 'Z' ||
		   c >= '0' && c <= '9') {
			*p++ = c;
			continue;
		}
		*p++ = '\\';
		switch(c) {
		default:
			if(c < 040 || c >= 0177)
				break;	/* not portable */
			p[-1] = c;
			continue;
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
