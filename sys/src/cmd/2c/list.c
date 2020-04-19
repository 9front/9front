#define EXTERN
#include "gc.h"

void
listinit(void)
{

	fmtinstall('R', Rconv);
	fmtinstall('A', Aconv);
	fmtinstall('D', Dconv);
	fmtinstall('P', Pconv);
	fmtinstall('S', Sconv);
	fmtinstall('X', Xconv);
	fmtinstall('B', Bconv);
}

static Index
indexv(int i, int j)
{
	Index x;

	x.o0 = i;
	x.o1 = j;
	return x;
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

int
Pconv(Fmt *fp)
{
	char str[STRINGSZ];
	Prog *p;

	p = va_arg(fp->args, Prog*);
	snprint(str, sizeof str, "	%A	%D,%D", p->as, &p->from, &p->to);
	if(p->from.field)
		return fmtprint(fp, "%s,%d,%d", str, p->to.field, p->from.field);
	return fmtstrcpy(fp, str);
}

int
Aconv(Fmt *fp)
{
	int r;

	r = va_arg(fp->args, int);
	return fmtstrcpy(fp, anames[r]);
}

int
Xconv(Fmt *fp)
{
	char str[20];
	Index x;
	int i, j;

	x = va_arg(fp->args, Index);
	str[0] = 0;
	i = x.o0 & D_MASK;
	if(i != D_NONE){
		j = x.o1;
		return fmtprint(fp, "(%R.%c*%c)", i, "WWWWLLLL"[j], "12481248"[j]);
	}
	return fmtstrcpy(fp, str);
}

int
Dconv(Fmt *fp)
{
	char str[40];
	Adr *a;
	int i, j;
	long d;

	a = va_arg(fp->args, Adr*);
	i = a->index;
	if(i != D_NONE) {
		a->index = D_NONE;
		d = a->displace;
		j = a->scale;
		a->displace = 0;
		switch(i & I_MASK) {
		default:
			snprint(str, sizeof str, "???%ld(%D%X)", d, a, indexv(i, j));
			break;

		case I_INDEX1:
			snprint(str, sizeof str, "%D%X", a, indexv(i, a->scale));
			break;

		case I_INDEX2:
			if(d)
				snprint(str, sizeof str, "%ld(%D)%X", d, a, indexv(i, j));
			else
				snprint(str, sizeof str, "(%D)%X", a, indexv(i, j));
			break;

		case I_INDEX3:
			if(d)
				snprint(str, sizeof str, "%ld(%D%X)", d, a, indexv(i, j));
			else
				snprint(str, sizeof str, "(%D%X)", a, indexv(i, j));
			break;
		}
		a->displace = d;
		a->index = i;
		goto out;
	}
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
			if(a->type == D_CONST)
				snprint(str, sizeof str, "%ld", d);
			else
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
		snprint(str, sizeof str, "%ld(PC)", a->offset-pc);
		break;

	case D_EXTERN:
		snprint(str, sizeof str, "%s+%ld(SB)", a->sym->name, a->offset);
		break;

	case D_STATIC:
		snprint(str, sizeof str, "%s<>+%ld(SB)", a->sym->name, a->offset);
		break;

	case D_AUTO:
		snprint(str, sizeof str, "%s-%ld(SP)", a->sym->name, -a->offset);
		break;

	case D_PARAM:
		snprint(str, sizeof str, "%s+%ld(FP)", a->sym->name, a->offset);
		break;

	case D_CONST:
		snprint(str, sizeof str, "$%ld", a->offset);
		break;

	case D_STACK:
		snprint(str, sizeof str, "TOS+%ld", a->offset);
		break;

	case D_FCONST:
		snprint(str, sizeof str, "$%.17e", a->dval);
		goto out;

	case D_SCONST:
		snprint(str, sizeof str, "$\"%S\"", a->sval);
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

	case D_TREE:
		snprint(str, sizeof str, "TREE");
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
	char str[30], *p, *s;

	s = va_arg(fp->args, char*);
	p = str;
	for(i=0; i<sizeof(double); i++) {
		c = s[i] & 0xff;
		if(c != '\\' && c != '"' && isprint(c)) {
			*p++ = c;
			continue;
		}
		*p++ = '\\';
		switch(c) {
		case 0:
			*p++ = '0';
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
		*p++ = ((c>>6) & 7) + '0';
		*p++ = ((c>>3) & 7) + '0';
		*p++ = ((c>>0) & 7) + '0';
	}
	*p = 0;
	return fmtstrcpy(fp, str);
}
