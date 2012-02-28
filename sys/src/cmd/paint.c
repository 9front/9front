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

void
loadimg(char *name)
{
	Image *b;
	int fd;

	fd=open(name, OREAD);
	if(fd==-1)
		sysfatal("can't open file");
	if((b=readimage(display, fd, 0)) == nil)
		sysfatal("can't read image");
	draw(screen, screen->r, b, 0, b->r.min);
	flushimage(display, 1);
	close(fd);
}

/* stolen from mothra */
void
screendump(char *name, int full)
{
	Image *b;
	int fd;

	fd=create(name, OWRITE|OTRUNC, 0666);
	if(fd==-1)
		sysfatal("can't create file");
	if(full){
		writeimage(fd, screen, 0);
	} else {
		if((b=allocimage(display, screen->r, screen->chan, 0, DNofill)) == nil){
			close(fd);
			sysfatal("can't allocate image");
		}
		draw(b, b->r, screen, 0, b->r.min);
		writeimage(fd, b, 0);
		freeimage(b);
	}
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
		loadimg(argv[0]);
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
				loadimg(file);
			}
			if(e.kbdc == 'q')
				exits(nil);
			if(e.kbdc == 's'){
				snprint(file, sizeof(file), "out.bit");
				if(eenter("Save to", file, sizeof(file), &e.mouse) <= 0)
					break;
				screendump(file, 0);
			}
			break;
		}
	}
}
