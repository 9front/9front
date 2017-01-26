#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <bio.h>
#include <authsrv.h>
#include "authcmdlib.h"

/*
 * get the date in the format yyyymmdd
 */
Tm
getdate(char *d)
{
	Tm date;
	int i;

	memset(&date, 0, sizeof(date));
	for(i = 0; i < 8; i++)
		if(!isdigit(d[i]))
			return date;
	date.year = (d[0]-'0')*1000 + (d[1]-'0')*100 + (d[2]-'0')*10 + d[3]-'0';
	date.year -= 1900;
	d += 4;
	date.mon = (d[0]-'0')*10 + d[1]-'0' - 1;
	d += 2;
	date.mday = (d[0]-'0')*10 + d[1]-'0';
	return date;
}

long
getexpiration(char *db, char *u)
{
	char buf[Maxpath];
	char *cdate;
	Tm date;
	ulong secs, now;
	int n, fd;

	/* read current expiration (if any) */
	snprint(buf, sizeof buf, "%s/%s/expire", db, u);
	fd = open(buf, OREAD);
	buf[0] = 0;
	if(fd >= 0){
		n = read(fd, buf, sizeof(buf)-1);
		if(n > 0)
			buf[n-1] = 0;
		close(fd);
	}

	if(buf[0]){
		if(strncmp(buf, "never", 5)){
			secs = strtoul(buf, nil, 10);
			memmove(&date, localtime(secs), sizeof(date));
			sprint(buf, "%4.4d%2.2d%2.2d", date.year+1900, date.mon+1, date.mday);
		} else
			buf[5] = 0;
	} else
		strcpy(buf, "never");

	for(;;free(cdate)){
		cdate = readcons("Expiration date (YYYYMMDD or never)", buf, 0);
		if(cdate == nil || *cdate == 0){
			secs = -1;
			break;
		}
		if(strcmp(cdate, "never") == 0){
			secs = 0;
			break;
		}
		date = getdate(cdate);
		secs = tm2sec(&date);
		now = time(0);
		if(secs > now && secs < now + 2*365*24*60*60)
			break;
		print("expiration time must fall between now and 2 years from now\n");
	}
	free(cdate);
	return secs;
}
