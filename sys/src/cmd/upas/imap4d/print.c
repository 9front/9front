#include "imap4d.h"

int
Ffmt(Fmt *f)
{
	char *s, buf[128], buf2[128];

	s = va_arg(f->args, char*);
	if(strncmp("/imap", s, 5) && strncmp("/pop", s, 4)){
		snprint(buf, sizeof buf, "/mail/box/%s/%s", username, s);
		s = buf;
	}
	snprint(buf2, sizeof buf2, "%q", s);
	return fmtstrcpy(f, buf2);
}

enum {
	Qok		= 0,
	Qquote		= 1<<0,
	Qbackslash	= 1<<1,
	Qliteral		= 1<<2,
};

static int
needtoquote(Rune r)
{
	if(r >= 0x7f || r == '\n' || r == '\r')
		return Qliteral;
	if(r <= ' ')
		return Qquote;
	if(r == '\\' || r == '"')
		return Qbackslash;
	return Qok;
}

int
Zfmt(Fmt *f)
{
	char *s, *t, buf[Pathlen], buf2[Pathlen];
	int w, quotes, alt;
	Rune r;

	s = va_arg(f->args, char*);
	alt = f->flags & FmtSharp;
	if(s == 0 && !alt)
		return fmtstrcpy(f, "NIL");
	if(s == 0 || *s == 0)
		return fmtstrcpy(f, "\"\"");
	switch(f->r){
	case 'Y':
		s = decfs(buf, sizeof buf, s);
		s = encmutf7(buf2, sizeof buf2, s);
		break;
	}
	quotes = 0;
	for(t = s; *t; t += w){
		w = chartorune(&r, t);
		quotes |= needtoquote(r);
		if(quotes & Qliteral && alt)
			ilog("[%s] bad at [%s] %.2ux\n", s, t, r);
	}
	if(alt){
		if(!quotes)
			return fmtstrcpy(f, s);
		if(quotes & Qliteral)
			return fmtstrcpy(f, "GOK");
	}else if(quotes & Qliteral)
		return fmtprint(f, "{%lud}\r\n%s", strlen(s), s);

	fmtrune(f, '"');
	for(t = s; *t; t += w){
		w = chartorune(&r, t);
		if(needtoquote(r) == Qbackslash)
			fmtrune(f, '\\');
		fmtrune(f, r);
	}
	return fmtrune(f, '"');
}

int
Xfmt(Fmt *f)
{
	char *s, buf[Pathlen], buf2[Pathlen];

	s = va_arg(f->args, char*);
	if(s == 0)
		return fmtstrcpy(f, "NIL");
	s = decmutf7(buf2, sizeof buf2, s);
	cleanname(s);
	return fmtstrcpy(f, encfs(buf, sizeof buf, s));
}

static char *day[] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
};

static char *mon[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

int
Dfmt(Fmt *f)
{
	char buf[128], *p, *e, *sgn, *fmt;
	int off;
	Tm *tm;

	tm = va_arg(f->args, Tm*);
	if(tm == nil)
		tm = localtime(time(0));
	sgn = "+";
	if(tm->tzoff < 0)
		sgn = "";
	e = buf + sizeof buf;
	p = buf;
	off = (tm->tzoff/3600)*100 + (tm->tzoff/60)%60;
	if((f->flags & FmtSharp) == 0){
		/* rfc822 style */
		fmt = "%.2d %s %.4d %.2d:%.2d:%.2d %s%.4d";
		p = seprint(p, e, "%s, ", day[tm->wday]);
	}else
		fmt = "%2d-%s-%.4d %2.2d:%2.2d:%2.2d %s%4.4d";
	seprint(p, e, fmt,
		tm->mday, mon[tm->mon], tm->year + 1900, tm->hour, tm->min, tm->sec,
		sgn, off);
	if(f->r == L'δ')
		return fmtstrcpy(f, buf);
	return fmtprint(f, "%Z", buf);
}
