#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>

#define NCOLORS 6

Image	*colors[NCOLORS];

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
	if((b = readimage(display, fd, 0)) == nil){
		close(fd);
		return -1;
	}
	draw(screen, screen->r, b, 0, b->r.min);
	flushimage(display, 1);
	freeimage(b);
	close(fd);
	return 0;
}

int
saveimg(char *name)
{
	int fd;

	if((fd = create(name, OWRITE|OTRUNC, 0666)) < 0)
		return -1;
	writeimage(fd, screen, 0);
	close(fd);
	return 0;
}

void
main(int argc, char *argv[])
{
	Event e;
	Point last;
	int b = 1;
	int c = 0;
	int cn, f;
	int haslast = 0;
	char brush[128];
	char color[NCOLORS];
	char file[128];
	char fill[NCOLORS];
	
	if(initdraw(0, 0, "paint") < 0){
		fprint(2, "paint: initdraw failed: %r\n");
		exits("initdraw");
	}
	einit(Emouse | Ekeyboard);
	draw(screen, screen->r, display->white, 0, ZP);
	flushimage(display, 1);

	colors[0] = display->black;
	colors[1] = display->white;
	colors[2] = allocimage(display, Rect(0,0,1,1), CMAP8, 1, DRed);
	colors[3] = allocimage(display, Rect(0,0,1,1), CMAP8, 1, DGreen);
	colors[4] = allocimage(display, Rect(0,0,1,1), CMAP8, 1, DBlue);
	colors[5] = allocimage(display, Rect(0,0,1,1), CMAP8, 1, DYellow);

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
		strncpy(file, argv[0], sizeof(file)-1);
		if(loadimg(file) < 0)
			sysfatal("%r");
		break;
	}

	while(1){
		switch(event(&e)){
		case Emouse:
			if(e.mouse.buttons & 1){
				if(haslast)
					line(screen, last, e.mouse.xy, Enddisc, Enddisc, b, colors[c], ZP);
				else
					fillellipse(screen, e.mouse.xy, b, b, colors[c], ZP);
				last = e.mouse.xy;
				haslast = 1;
				flushimage(display, 1);
			} else
				haslast = 0;
			break;
		case Ekeyboard:
			if(e.kbdc == 'b'){
				if(eenter("Brush", brush, sizeof(brush), &e.mouse) <= 0)
					break;
				b = atoi(brush);
			}
			if(e.kbdc == 'c'){
				if(eenter("Color", color, sizeof(color), &e.mouse) <= 0)
					break;
				cn = atoi(color);
				if(cn >= 0 && cn < NCOLORS)
					c = cn;
			}
			if(e.kbdc == 'f'){
				if(eenter("Fill", fill, sizeof(fill), &e.mouse) <= 0)
					break;
				f = atoi(fill);
				if(f >= 0 && f < NCOLORS)
					draw(screen, screen->r, colors[f], 0, ZP);
			}
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
