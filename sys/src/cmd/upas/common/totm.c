#include <common.h>

static char mtab[] = "JanFebMarAprMayJunJulAugSepOctNovDec";

int
ctimetotm(char *s, Tm *tm)
{
	char buf[32];

	if(strlen(s) < 28)
		return -1;
	snprint(buf, sizeof buf, "%s", s);
	memset(tm, 0, sizeof *tm);
	buf[7] = 0;
	tm->mon = (strstr(mtab, buf+4) - mtab)/3;
	tm->mday = atoi(buf+8);
	tm->hour = atoi(buf+11);
	tm->min = atoi(buf+14);
	tm->sec = atoi(buf+17);
	tm->zone[0] = buf[20];
	tm->zone[1] = buf[21];
	tm->zone[2] = buf[22];
	tm->year = atoi(buf+24) - 1900;
	return 0;
}

int
fromtotm(char *s, Tm *tm)
{
	char buf[256], *f[3];

	snprint(buf, sizeof buf, "%s", s);
	if(getfields(buf, f, nelem(f), 0, " ") != 3)
		return -1;
	return ctimetotm(f[2], tm);
}
