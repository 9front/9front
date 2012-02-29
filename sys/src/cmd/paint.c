#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>

void
eresized(int)
{
	if(getwindow(display, Refnone) < 0)
		sysfatal("resize failed");
}

int
loadimg(char *name)
{
	Image *b;
	int fd;

	if((fd = open(name, OREAD)) < 0)
		return -1;
	else {
		if((b = readimage(display, fd, 0)) == nil){
			close(fd);
			return -1;
		} else {
			draw(screen, screen->r, b, 0, b->r.min);
			flushimage(display, 1);
			freeimage(b);
		}
		close(fd);
	}
}

int
saveimg(char *name)
{
	int fd;

	if((fd = create(name, OWRITE|OTRUNC, 0666)) < 0)
		return -1;
	writeimage(fd, screen, 0);
	close(fd);
}

void
main(int argc, char *argv[])
{
	Event e;
	Point last;
	int haslast;
	int brushsize = 1;
	char brush[128];
	char file[128];
	
	haslast = 0;
	if(initdraw(0, 0, "paint") < 0){
		fprint(2, "paint: initdraw failed: %r\n");
		exits("initdraw");
	}
	einit(Emouse | Ekeyboard);
	draw(screen, screen->r, display->white, 0, ZP);
	flushimage(display, 1);

	ARGBEGIN{
	default:
		goto Usage;
	}ARGEND
	switch(argc){
	default:
	Usage:
		fprint(2, "Usage: [file]\n");
		exits("usage");
	case 0:
		break;
	case 1:
		if(loadimg(argv[0]) < 0)
			sysfatal("%r");
		break;
	}

	while(1){
		switch(event(&e)){
		case Emouse:
			if(e.mouse.buttons & 1){
				if(haslast)
					line(screen, last, e.mouse.xy, Enddisc, Enddisc, brushsize, display->black, ZP);
				else
					fillellipse(screen, e.mouse.xy, brushsize, brushsize, display->black, ZP);
				
				last = e.mouse.xy;
				haslast = 1;
				flushimage(display, 1);
			} else
				haslast = 0;
			if(e.mouse.buttons & 4){
				fillellipse(screen, e.mouse.xy, brushsize, brushsize, display->white, ZP);
				flushimage(display, 1);
			}
			break;
		case Ekeyboard:
			if(e.kbdc == 'b'){
				if(eenter("Brush", brush, sizeof(brush), &e.mouse) <= 0)
					break;
				brushsize = atoi(brush);
			}
			if(e.kbdc == 'c')
				draw(screen, screen->r, display->white, 0, ZP);
			if(e.kbdc == 'o'){
				if(eenter("Open file", file, sizeof(file), &e.mouse) <= 0)
					break;
				if(loadimg(file) < 0){
					rerrstr(file, sizeof(file));
					eenter(file, 0, 0, &e.mouse);
				}
			}
			if(e.kbdc == 'q')
				exits(nil);
			if(e.kbdc == 's'){
				if(eenter("Save to", file, sizeof(file), &e.mouse) <= 0)
					break;
				if(saveimg(file) < 0){
					rerrstr(file, sizeof(file));
					eenter(file, 0, 0, &e.mouse);
				}
			}
			break;
		}
	}
}
