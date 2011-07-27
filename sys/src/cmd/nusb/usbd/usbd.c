#include <u.h>
#include <libc.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>
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

void
main()
{
	readrules();
	parserules(rules);
	luser = getuser();
	usbdsrv.tree = alloctree(luser, luser, 0555, nil);
	usbdb = createfile(usbdsrv.tree->root, "usbdb", luser, 0775, nil);
	postsharesrv(&usbdsrv, nil, "usb", "usbd", "b");
}
