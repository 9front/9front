/* screenlock - lock a terminal */
#include <u.h>
#include <libc.h>
#include <libsec.h>
#include <draw.h>
#include <thread.h>
#include <auth.h>

char pic[] = "/lib/bunny.bit";
int debug;
long blank;

void
usage(void)
{
	fprint(2, "usage: %s [-d]\n", argv0);
	exits("usage");
}

void
readline(char *buf, int nbuf)
{
	char c;
	int i;

	i = 0;
	while(i < nbuf-1){
		if(read(0, &c, 1) != 1 || c == '\04' || c == '\177'){
			i = 0;
			break;
		} else if(c == '\n')
			break;
		else if(c == '\b' && i > 0)
			--i;
		else if(c == ('u' & 037))
			i = 0;
		else
			buf[i++] = c;
		blank = time(0);
	}
	buf[i] = '\0';
}

void
checkpassword(void)
{
	char buf[256];
	AuthInfo *ai;

	for(;;){
		memset(buf, 0, sizeof buf);
		readline(buf, sizeof buf);

		border(screen, screen->r, 8, display->white, ZP);
		flushimage(display, 1);

		/* authenticate */
		ai = auth_userpasswd(getuser(), buf);
		if(ai != nil && ai->cap != nil)
			break;

		rerrstr(buf, sizeof buf);
		if(strncmp(buf, "needkey ", 8) == 0)
			break;

		auth_freeAI(ai);
		border(screen, screen->r, 8, display->black, ZP);
		flushimage(display, 1);
	}
	auth_freeAI(ai);
	memset(buf, 0, sizeof buf);
}

void
blanker(void *)
{
	int fd;

	if((fd = open("/dev/mousectl", OWRITE)) < 0)
		return;

	for(;;){
		if(blank != 0 && ((ulong)time(0) - (ulong)blank) >= 5){
			blank = 0;
			write(fd, "blank", 5);
		}
		sleep(1000);
	}
}

void
grabmouse(void*)
{
	int fd, x, y;
	char ibuf[256], obuf[256];

	if((fd = open("/dev/mouse", ORDWR)) < 0)
		sysfatal("can't open /dev/mouse: %r");

	snprint(obuf, sizeof obuf, "m %d %d",
		screen->r.min.x + Dx(screen->r)/2,
		screen->r.min.y + Dy(screen->r)/2);

	while(read(fd, ibuf, sizeof ibuf) > 0){
		ibuf[12] = 0;
		ibuf[24] = 0;
		x = atoi(ibuf+1);
		y = atoi(ibuf+13);
		if(x != screen->r.min.x + Dx(screen->r)/2 ||
		   y != screen->r.min.y + Dy(screen->r)/2){
			if(!debug)
				fprint(fd, "%s", obuf);
			blank = time(0);
		}
	}
}

void
lockscreen(void)
{
	enum { Nfld = 5, Fldlen = 12, Cursorlen = 2*4 + 2*2*16, };
	char *s;
	char buf[Nfld*Fldlen], *flds[Nfld], newcmd[128], cbuf[Cursorlen];
	int fd, dx, dy;
	Image *i;
	Point p;
	Rectangle r;
	Tm *tm;

	if((fd = open("/dev/screen", OREAD)) < 0)
		sysfatal("can't open /dev/screen: %r");
	if(read(fd, buf, Nfld*Fldlen) != Nfld*Fldlen)
		sysfatal("can't read /dev/screen: %r");
	close(fd);
	buf[sizeof buf-1] = 0;
	if(tokenize(buf, flds, Nfld) != Nfld)
		sysfatal("can't tokenize /dev/screen header");
	snprint(newcmd, sizeof newcmd, "-r %s %s %s %s",
		flds[1], flds[2], flds[3], flds[4]);

	newwindow(newcmd);
	if((fd = open("/dev/consctl", OWRITE)) >= 0)
		write(fd, "rawon", 5);

	if((fd = open("/dev/cons", OREAD)) < 0)
		sysfatal("can't open cons: %r");
	dup(fd, 0);

	if((fd = open("/dev/cons", OWRITE)) < 0)
		sysfatal("can't open cons: %r");
	dup(fd, 1);
	dup(fd, 2);

	if(initdraw(nil, nil, "screenlock") < 0)
		sysfatal("initdraw failed");
	screen = _screen->image;	/* fullscreen */

	if((fd = open(pic, OREAD)) >= 0){
		if((i = readimage(display, fd, 0)) != nil){
 			r = screen->r;
			p = Pt(r.max.x / 2, r.max.y * 2 / 3); 
			dx = (Dx(screen->r) - Dx(i->r)) / 2;
			r.min.x += dx;
			r.max.x -= dx;
			dy = (Dy(screen->r) - Dy(i->r)) / 2;
			r.min.y += dy;
			r.max.y -= dy;
			draw(screen, screen->r, display->black, nil, ZP);
			draw(screen, r, i, nil, i->r.min);
		}
		close(fd);

		/* identify the user on screen, centered */
		tm = localtime(time(&blank));
		s = smprint("user %s at %d:%02.2d", getuser(), tm->hour, tm->min);
		p = subpt(p, Pt(stringwidth(font, "m") * strlen(s) / 2, 0));
		string(screen, p, screen->display->white, ZP, font, s);
	}
	flushimage(display, 1);

	/* screen is now open and covered.  grab mouse and hold on tight */
	procrfork(grabmouse, nil, 8*1024, RFFDG);
	procrfork(blanker, nil, 8*1024, RFFDG);

	/* clear the cursor */
	if((fd = open("/dev/cursor", OWRITE)) >= 0){
		memset(cbuf, 0, sizeof cbuf);
		write(fd, cbuf, sizeof cbuf);
		/* leave it open */
	}
}

void
threadmain(int argc, char *argv[])
{
	ARGBEGIN{
	case 'd':
		debug++;
		break;
	default:
		usage();
	}ARGEND

	if(argc != 0)
		usage();

	lockscreen();
	checkpassword();
	threadexitsall(nil);
}
