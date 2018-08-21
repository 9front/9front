#include "vnc.h"
#include "vncv.h"
#include <cursor.h>

typedef struct Cursor Cursor;

typedef struct Mouse Mouse;
struct Mouse {
	int buttons;
	Point xy;
};

void
adjustwin(Vnc *v, int force)
{
	int fd;
	Point d;

	if(force)
		d = v->dim.max;
	else {
		/*
		 * limit the window to at most the vnc server's size
		 */
		d = subpt(screen->r.max, screen->r.min);
		if(d.x > v->dim.max.x){
			d.x = v->dim.max.x;
			force = 1;
		}
		if(d.y > v->dim.max.y){
			d.y = v->dim.max.y;
			force = 1;
		}
	}
	if(force) {
		fd = open("/dev/wctl", OWRITE);
		if(fd >= 0){
			fprint(fd, "resize -dx %d -dy %d", d.x+2*Borderwidth, d.y+2*Borderwidth);
			close(fd);
		}
	}
}

static void
resized(int first)
{
	lockdisplay(display);
	if(getwindow(display, Refnone) < 0)
		sysfatal("internal error: can't get the window image");
	if((vnc->canresize&2) == 0)
		adjustwin(vnc, first);
	unlockdisplay(display);
	requestupdate(vnc, 0);
}

static Cursor dotcursor = {
	{-7, -7},
	{0x00, 0x00,
	 0x00, 0x00,
	 0x00, 0x00,
	 0x00, 0x00, 
	 0x03, 0xc0,
	 0x07, 0xe0,
	 0x0f, 0xf0, 
	 0x0f, 0xf0,
	 0x0f, 0xf0,
	 0x07, 0xe0,
	 0x03, 0xc0,
	 0x00, 0x00, 
	 0x00, 0x00,
	 0x00, 0x00,
	 0x00, 0x00,
	 0x00, 0x00, },
	{0x00, 0x00,
	 0x00, 0x00,
	 0x00, 0x00,
	 0x00, 0x00, 
	 0x00, 0x00,
	 0x03, 0xc0,
	 0x07, 0xe0, 
	 0x07, 0xe0,
	 0x07, 0xe0,
	 0x03, 0xc0,
	 0x00, 0x00,
	 0x00, 0x00, 
	 0x00, 0x00,
	 0x00, 0x00,
	 0x00, 0x00,
	 0x00, 0x00, }
};

static void
mouseevent(Vnc *v, Mouse m)
{
	vnclock(v);
	vncwrchar(v, MMouse);
	vncwrchar(v, m.buttons);
	vncwrpoint(v, m.xy);
	vncflush(v);
	vncunlock(v);
}

void
mousewarp(Point pt)
{
	pt = addpt(pt, screen->r.min);
	if(fprint(mousefd, "m%d %d", pt.x, pt.y) < 0)
		fprint(2, "mousefd write: %r\n");
}

void
initmouse(void)
{
	char buf[1024];

	snprint(buf, sizeof buf, "%s/mouse", display->devdir);
	if((mousefd = open(buf, ORDWR)) < 0)
		sysfatal("open %s: %r", buf);
}

enum {
	EventSize = 1+4*12
};
void
readmouse(Vnc *v)
{
	int cursorfd, len, n;
	char buf[10*EventSize], *start, *end;
	uchar curs[2*4+2*2*16];
	Cursor *cs;
	Mouse m;

	cs = &dotcursor;

	snprint(buf, sizeof buf, "%s/cursor", display->devdir);
	if((cursorfd = open(buf, OWRITE)) < 0)
		sysfatal("open %s: %r", buf);

	BPLONG(curs+0*4, cs->offset.x);
	BPLONG(curs+1*4, cs->offset.y);
	memmove(curs+2*4, cs->clr, 2*2*16);
	write(cursorfd, curs, sizeof curs);

	resized(1);
	start = end = buf;
	len = 0;
	for(;;){
		if((n = read(mousefd, end, sizeof(buf) - (end - buf))) < 0)
			sysfatal("read mouse failed");

		len += n;
		end += n;
		while(len >= EventSize){
			if(*start == 'm'){
				m.xy.x = atoi(start+1);
				m.xy.y = atoi(start+1+12);
				m.buttons = atoi(start+1+2*12) & 0x1F;
				m.xy = subpt(m.xy, screen->r.min);
				if(ptinrect(m.xy, v->dim)){
					mouseevent(v, m);
					/* send wheel button *release* */ 
					if ((m.buttons & 0x7) != m.buttons) {
						m.buttons &= 0x7;
						mouseevent(v, m);
					}
				}
			} else
				resized(0);
			start += EventSize;
			len -= EventSize;
		}
		if(start - buf > sizeof(buf) - EventSize){
			memmove(buf, start, len);
			start = buf;
			end = start+len;
		}
	}
}

static int
tcs(int fd0, int fd1)
{
	int pfd[2];

	if(strcmp(charset, "utf-8") == 0)
		goto Dup;
	if(pipe(pfd) < 0)
		goto Dup;
	switch(rfork(RFPROC|RFFDG|RFMEM)){
	case -1:
		close(pfd[0]);
		close(pfd[1]);
		goto Dup;
	case 0:
		if(fd0 < 0){
			dup(pfd[0], 0);
			dup(fd1, 1);
			close(fd1);	
		} else {
			dup(pfd[0], 1);
			dup(fd0, 0);
			close(fd0);	
		}
		close(pfd[0]);
		close(pfd[1]);
		execl("/bin/tcs", "tcs", fd0 < 0 ? "-f" : "-t", charset, nil);
		execl("/bin/cat", "cat", nil);
		_exits(0);
	}
	close(pfd[0]);
	return pfd[1];
Dup:
	return dup(fd0 < 0 ? fd1 : fd0, -1);
}

static int snarffd = -1;
static ulong snarfvers;

static int
gotsnarf(void)
{
	Dir *dir;
	int ret;

	if(snarffd < 0 || (dir = dirfstat(snarffd)) == nil)
		return 0;

	ret = dir->qid.vers != snarfvers;
	snarfvers = dir->qid.vers;
	free(dir);

	return ret;
}

void 
writesnarf(Vnc *v, long n)
{
	uchar buf[8192];
	int fd, sfd;
	long m;

	vnclock(v);
	fd = -1;
	if((sfd = create("/dev/snarf", OWRITE, 0666)) >= 0){
		fd = tcs(-1, sfd);
		close(sfd);
	}
	if(fd < 0)
		vncgobble(v, n);
	else {
		while(n > 0){
			m = n;
			if(m > sizeof(buf))
				m = sizeof(buf);
			vncrdbytes(v, buf, m);
			n -= m;
			write(fd, buf, m);
		}
		close(fd);
		waitpid();
	}
	gotsnarf();
	vncunlock(v);
}

char *
getsnarf(int *sz)
{
	char *snarf, *p;
	int fd, n, c;

	*sz =0;
	n = 8192;
	p = snarf = malloc(n);

	seek(snarffd, 0, 0);
	if((fd = tcs(snarffd, -1)) >= 0){
		while((c = read(fd, p, n)) > 0){
			p += c;
			n -= c;
			*sz += c;
			if (n == 0){
				snarf = realloc(snarf, *sz + 8192);
				n = 8192;
			}
		}
		close(fd);
		waitpid();
	}
	return snarf;
}

void
checksnarf(Vnc *v)
{
	char *snarf;
	int len;

	if(snarffd < 0){
		snarffd = open("/dev/snarf", OREAD);
		if(snarffd < 0)
			sysfatal("can't open /dev/snarf: %r");
	}

	for(;;){
		sleep(1000);

		vnclock(v);
		if(gotsnarf()){
			snarf = getsnarf(&len);

			vncwrchar(v, MCCut);
			vncwrbytes(v, "pad", 3);
			vncwrlong(v, len);
			vncwrbytes(v, snarf, len);
			vncflush(v);

			free(snarf);
		}
		vncunlock(v);
	}
}
