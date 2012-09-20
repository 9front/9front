#include <u.h>
#include <libc.h>
#include <fis.h>
#include "atazz.h"

static int
ckprint(char *s)
{
	char buf[ERRMAX];
	int st;
	Dev d;

	squelch = 1;
	d.fd = -1;
	st = opendev(s, &d);
	squelch = 0;
	if(st == -1){
		rerrstr(buf, sizeof buf);
		if(strstr(buf, "ata command") != nil)
			return 0;
		return 0 /* -1 */;
	}
	close(d.fd);
	print("%s\t%llud; %ud\t%llux\n", s, d.nsect, d.secsize, d.wwn);
	return 1;
}

static int
probe0(char *s, int l)
{
	char *p, *f[3], buf[16];
	int i, r;

	s[l] = 0;
	r = 0;
	for(; p = strchr(s, '\n'); s = p + 1){
		if(tokenize(s, f, nelem(f)) < 1)
			continue;
		for(i = 0; i < 10; i++){
			snprint(buf, sizeof buf, "/dev/%s%d", f[0], i);
			switch(ckprint(buf)){
			case -1:
				eprint("!device error %s: %r\n", buf);
				break;
			case 0:
				goto nextdev;
			case 1:
				r++;
				break;
			}
		nextdev:
			;
		}
	}
	return r;
}

int
probe(void)
{
	char *s;
	int fd, l, r;

	fd = open("/dev/sdctl", OREAD);
	if(fd == -1)
		return -1;
	r = -1;
	l = 1024;	/* #S/sdctl has 0 size; guess */
	if(s = malloc(l + 1))
	if((l = read(fd, s, l)) > 0)
		r = probe0(s, l);
	free(s);
	close(fd);
	return r;
}
