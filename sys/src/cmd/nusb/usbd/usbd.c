#include <u.h>
#include <libc.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>
#include "usb.h"
#include "dat.h"
#include "fns.h"

char *luser;
char *rules;

static File *usbdb;
static char Enonexist[] = "does not exist";

static void
usbdread(Req *req)
{
	if(usbdb->qid.path == req->fid->qid.path){
		readstr(req, rules);
		respond(req, nil);
		return;
	}
	respond(req, Enonexist);
}

Srv usbdsrv = {
	.read = usbdread,
};

static void
readrules(void)
{
	int fd, rc, n;
	char buf[4096];
	
	fd = open("/lib/usbdb", OREAD);
	if(fd < 0)
		sysfatal("open /lib/usbdb: %r");
	rules = nil;
	n = 0;
	for(;;){
		rc = readn(fd, buf, sizeof buf);
		if(rc == 0)
			break;
		if(rc < 0)
			sysfatal("read: %r");
		rules = realloc(rules, 1 + n + rc);
		if(rules == nil)
			sysfatal("realloc: %r");
		memmove(rules + n, buf, rc);
		n += rc;
		rules[n] = 0;
	}
	if(rules == nil)
		rules = "";
	close(fd);
}

int
startdev(Port *p)
{
	Rule *r;
	char buf[14];

	if(p->dev == nil || p->dev->usb == nil){
		fprint(2, "okay what?\n");
		return -1;
	}
	rlock(&rulelock);
	r = rulesmatch(p->dev->usb);
	if(r == nil || r->argv == nil){
		fprint(2, "no driver for device\n");
		runlock(&rulelock);
		return -1;
	}
	snprint(buf, sizeof buf, "%d", p->dev->id);
	r->argv[r->argc] = buf;
	closedev(p->dev);
	switch(fork()){
	case -1:
		fprint(2, "fork: %r");
		runlock(&rulelock);
		return -1;
	case 0:
		chdir("/bin");
		exec(r->argv[0], r->argv);
		sysfatal("exec: %r");
	}
	runlock(&rulelock);
	return 0;
}

void
main(int argc, char **argv)
{
	int fd, i, nd;
	Dir *d;

	readrules();
	parserules(rules);
	luser = getuser();
	
	argc--; argv++;

	rfork(RFNOTEG);
	switch(rfork(RFPROC|RFMEM)){
	case -1: sysfatal("rfork: %r");
	case 0: work(); exits(nil);
	}
	if(argc == 0){
		fd = open("/dev/usb", OREAD);
		if(fd < 0)
			sysfatal("/dev/usb: %r");
		nd = dirreadall(fd, &d);
		close(fd);
		if(nd < 2)
			sysfatal("/dev/usb: no hubs");
		for(i = 0; i < nd; i++)
			if(strcmp(d[i].name, "ctl") != 0)
				rendezvous(work, smprint("/dev/usb/%s", d[i].name));
		free(d);
	}else
		for(i = 0; i < argc; i++)
			rendezvous(work, strdup(argv[i]));
	rendezvous(work, nil);
	usbdsrv.tree = alloctree(luser, luser, 0555, nil);
	usbdb = createfile(usbdsrv.tree->root, "usbdb", luser, 0775, nil);
	postsharesrv(&usbdsrv, nil, "usb", "usbd", "b");
}
