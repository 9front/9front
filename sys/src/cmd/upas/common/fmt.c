#include "common.h"

int
rfc2047fmt(Fmt *fmt)
{
	char *s, *p;

	s = va_arg(fmt->args, char*);
	if(s == nil)
		return fmtstrcpy(fmt, "");
	for(p=s; *p; p++)
		if((uchar)*p >= 0x80)
			goto hard;
	return fmtstrcpy(fmt, s);

hard:
	fmtprint(fmt, "=?utf-8?q?");
	for(p = s; *p; p++){
		if(*p == ' ')
			fmtrune(fmt, '_');
		else if(*p == '_' || *p == '\t' || *p == '=' || *p == '?' ||
		    (uchar)*p >= 0x80)
			fmtprint(fmt, "=%.2uX", (uchar)*p);
		else
			fmtrune(fmt, (uchar)*p);
	}
	fmtprint(fmt, "?=");
	return 0;
}

void
mailfmtinstall(void)
{
	fmtinstall('U', rfc2047fmt);
}
