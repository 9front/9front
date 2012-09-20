/*
 * smart monitoring for scsi and ata
 *	copyright © 2009 erik quanstrom
 */
#include <u.h>
#include <libc.h>
#include <fis.h>
#include "smart.h"

enum{
	Checksec	= 600,
	Opensec		= 60 * 60,
	Relogsec		= 38400 / 4,
};

static	Sdisk	*disks;
static	Dtype	dtab[] = {
	Tata,	"ata",	ataprobe,	ataenable,	atastatus,
	Tscsi,	"scsi",	scsiprobe,	scsienable,	scsistatus,
};
static	char	*logfile = "smart";
static	int	aflag;
static	int	tflag;
static	int	vflag;

void
eprint(Sdisk *d, char *s, ...)
{
	char buf[256];
	va_list arg;

	va_start(arg, s);
	vseprint(buf, buf + sizeof buf, s, arg);
	va_end(arg);
//	syslog(0, logfile, "%s: %s", d->name, buf);
	if(vflag)
		fprint(2, "%s: %s", d->name, buf);
}

void
smartlog(Sdisk *d, char *s, ...)
{
	char buf[256];
	va_list arg;

	va_start(arg, s);
	vseprint(buf, buf + sizeof buf, s, arg);
	va_end(arg);
	if(!tflag)
		syslog(0, logfile, "%s: %s", d->name, buf);
	if(tflag || vflag)
		fprint(2, "%s: %s\n", d->name, buf);
}

static void
diskclose(Sdisk *d)
{
	close(d->fd);
	d->fd = -1;
}

static int
diskopen(Sdisk *d)
{
	char buf[128];

	snprint(buf, sizeof buf, "%s/raw", d->path);
	werrstr("");
	return d->fd = open(buf, ORDWR);
}

static int
noexist(void)
{
	char buf[ERRMAX];

	errstr(buf, sizeof buf);
	if(strstr(buf, "exist"))
		return -1;
	return 0;
}

static void
lognew(Sdisk *d)
{
	if(aflag && !tflag)
		smartlog(d, d->t->tname);
}

static int
newdisk(char *s)
{
	char buf[128], *p;
	int i;
	Sdisk d;

	memset(&d, 0, sizeof d);
	snprint(d.path, sizeof d.path, "%s", s);
	if(p = strrchr(s, '/'))
		p++;
	else
		p = s;
	snprint(d.name, sizeof d.name, "%s", p);
	snprint(buf, sizeof buf, "%s/raw", s);
	if(diskopen(&d) == -1)
		return noexist();
	for(i = 0; i < nelem(dtab); i++)
		if(dtab[i].probe(&d) == 0)
		if(dtab[i].enable(&d) == 0){
			d.t = dtab + i;
			lognew(&d);
			break;
		}
	diskclose(&d);
	if(d.t != 0){
		d.next = disks;
		disks = malloc(sizeof d);
		memmove(disks, &d, sizeof d);
	}
	return 0;
}

static int
probe0(char *s, int l)
{
	char *p, *f[3], buf[16];
	int i;

	s[l] = 0;
	for(; p = strchr(s, '\n'); s = p + 1){
		if(tokenize(s, f, nelem(f)) < 1)
			continue;
		for(i = 0; i < 0x10; i++){
			snprint(buf, sizeof buf, "/dev/%s%ux", f[0], i);
			if(newdisk(buf) == -1 && i > 2)
				break;
		}
	}
	return -1;
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

void
run(void)
{
	char buf[1024];
	int e, s0;
	uvlong t, t0;
	Sdisk *d;

	e = 0;
	t = time(0);
	for(d = disks; d; d = d->next){
		t0 = d->lastcheck;
		if(t0 != 0 && t - t0 < Checksec)
			continue;
		if(diskopen(d) == -1){
			if(t - t0 > Opensec)
				smartlog(d, "can't open in %ullds\n", t - t0);
			continue;
		}
		s0 = d->status;
		d->status = d->t->status(d, buf, sizeof buf);
		diskclose(d);
		if(d->status == -1)
			e++;
		if((aflag || d->status != s0 || d->status != 0) && !d->silent){
			t0 = d->lastlog;
			if(t0 == 0 || t - t0 >= Relogsec){
				smartlog(d, buf);
				d->lastlog = t;
			}
		}else
			d->lastlog = 0;
		d->lastcheck = t;
	}
	if(tflag)
		exits(e? "smart errors": "");
}

void
usage(void)
{
	fprint(2, "usage: disk/smart [-aptv] [/dev/sdXX] ...\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int pflag;

	pflag = 0;
	ARGBEGIN{
	case 'a':
		aflag = 1;
		break;
	case 'p':
		pflag = 1;
		break;
	case 't':
		tflag = 1;
	case 'v':
		vflag = 1;
		break;
	default:
		usage();
	}ARGEND

	for(; *argv; argv++)
		newdisk(*argv);
	if(argc == 0 || pflag)
		probe();
	if(disks == nil)
		sysfatal("no disks");
	for(;; sleep(30*1000))
		run();
}
