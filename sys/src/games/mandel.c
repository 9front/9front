#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>
#include <cursor.h>

enum { ncolors = 600 };

double xmin = -2, xmax = 1;
double ymin = -1, ymax = 1;
Mousectl *mctl;
Rendez rend;
int stopdraw;
uchar *imagedata;
Image *image;
extern Cursor reading;
uchar colors[3 * (ncolors + 1)];

double
convx(Rectangle *r, int x)
{
	return (xmax - xmin) * (x - r->min.x) / (r->max.x - r->min.x) + xmin;
}

double
convy(Rectangle *r, int y)
{
	return (ymax - ymin) * (r->max.y - y) / (r->max.y - r->min.y) + ymin;
}

void
pixel(int x, int y)
{
	draw(screen, Rect(x, y, x + 1, y + 1), display->black, nil, ZP);
}

ulong
iterate(double x, double y)
{
	int i;
	double zx, zy, zx2, zy2, v;

	zx = zy = zx2 = zy2 = 0;
	for(i = 0; i < 100; i++){
		zy = 2 * zx * zy + y;
		zx = zx2 - zy2 + x;
		zx2 = zx * zx;
		zy2 = zy * zy;
		if(zx2 + zy2 >= 4){
			v = 2 + i - log(log(sqrt(zx2 + zy2)) / log(i + 2)) / 0.69314718;
			return (int)(v * 1000) % ncolors;
		}
	}
	return 0;
}

void
redrawproc(void *)
{
	int x, y;
	uchar *p, *q;

	qlock(rend.l);
	for(;;){
		setcursor(mctl, &reading);
		p = imagedata;
		for(y = screen->r.min.y; y < screen->r.max.y; y++)
			for(x = screen->r.min.x; x < screen->r.max.x; x++){
				if(stopdraw)
					goto check;
				q = colors + 3 * iterate(convx(&screen->r, x), convy(&screen->r, y));
				*p++ = *q++;
				*p++ = *q++;
				*p++ = *q;
			}
		if(stopdraw)
			goto check;
		lockdisplay(display);
		loadimage(image, image->r, imagedata, Dx(image->r) * Dy(image->r) * 3);
		draw(screen, screen->r, image, nil, screen->r.min);
		flushimage(display, 1);
		unlockdisplay(display);
	check:
		stopdraw = 0;
		setcursor(mctl, nil);
		rsleep(&rend);
		stopdraw = 0;
	}
}

void
zoom(void)
{
	Rectangle r;
	double xmin_, xmax_, ymin_, ymax_;

	r = getrect(3, mctl);
	if(r.min.x == 0 && r.min.y == 0 && r.max.x == 0 && r.max.y == 0)
		return;
	xmin_ = convx(&screen->r, r.min.x);
	xmax_ = convx(&screen->r, r.max.x);
	ymin_ = convy(&screen->r, r.max.y);
	ymax_ = convy(&screen->r, r.min.y);
	stopdraw = 1;
	qlock(rend.l);
	xmin = xmin_;
	xmax = xmax_;
	ymin = ymin_;
	ymax = ymax_;
	rwakeup(&rend);
	qunlock(rend.l);
}

char *menus[] = {
	"zoom in",
	"reset",
	"quit",
	nil,
};

Menu menu = {
	.item = menus
};

void
resizethread(void *)
{
	ulong l;

	for(;;){
		if(recv(mctl->resizec, &l) < 1)
			continue;
		stopdraw = 1;
		qlock(rend.l);
		if(getwindow(display, Refnone) < 0)
			sysfatal("getwindow: %r");
		freeimage(image);
		free(imagedata);
		image = allocimage(display, screen->r, RGB24, 0, DBlack);
		imagedata = malloc(3 * Dx(screen->r) * Dy(screen->r));
		rwakeup(&rend);
		qunlock(rend.l);
	}
}

void
initcolors(void)
{
	uchar *p;
	int h, x;

	for(p = colors + 3, h = 0; p < colors + nelem(colors); h++){
		x = 0xFF - abs(h % (ncolors / 3) - ncolors / 6) * 0xFF / (ncolors / 6);
		if(h < ncolors/6){
			*p++ = 0xFF;
			*p++ = x;
			*p++ = 0;
		}else if(h < ncolors/3){
			*p++ = x;
			*p++ = 0xFF;
			*p++ = 0;
		}else if(h < ncolors/2){
			*p++ = 0;
			*p++ = 0xFF;
			*p++ = x;
		}else if(h < 2*ncolors/3){
			*p++ = 0;
			*p++ = x;
			*p++ = 0xFF;
		}else if(h < 5*ncolors/6){
			*p++ = x;
			*p++ = 0;
			*p++ = 0xFF;
		}else{
			*p++ = 0xFF;
			*p++ = 0;
			*p++ = x;
		}
	}
}

void
threadmain()
{
	static QLock ql;
	int rc;

	if(initdraw(nil, nil, "mandelbrot") < 0)
		sysfatal("initdraw: %r");
	display->locking = 1;
	unlockdisplay(display);
	if((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	rend.l = &ql;
	image = allocimage(display, screen->r, RGB24, 0, DBlack);
	imagedata = malloc(3 * Dx(screen->r) * Dy(screen->r));
	initcolors();
	proccreate(redrawproc, nil, mainstacksize);
	threadcreate(resizethread, nil, mainstacksize);
	for(;;){
		readmouse(mctl);
		if(mctl->buttons & 4){
			lockdisplay(display);
			rc = menuhit(3, mctl, &menu, nil);
			unlockdisplay(display);
			switch(rc){
			case 0:
				zoom();
				break;
			case 1:
				stopdraw = 1;
				qlock(rend.l);
				xmin = -2;
				xmax = 1;
				ymin = -1;
				ymax = 1;
				rwakeup(&rend);
				qunlock(rend.l);
				break;
			case 2:
				threadexitsall(nil);
			}
		}
	}
}

Cursor reading = {
	{-1, -1},
	{0xff, 0x80, 0xff, 0x80, 0xff, 0x00, 0xfe, 0x00, 
	 0xff, 0x00, 0xff, 0x80, 0xff, 0xc0, 0xef, 0xe0, 
	 0xc7, 0xf0, 0x03, 0xf0, 0x01, 0xe0, 0x00, 0xc0, 
	 0x03, 0xff, 0x03, 0xff, 0x03, 0xff, 0x03, 0xff, },
	{0x00, 0x00, 0x7f, 0x00, 0x7e, 0x00, 0x7c, 0x00, 
	 0x7e, 0x00, 0x7f, 0x00, 0x6f, 0x80, 0x47, 0xc0, 
	 0x03, 0xe0, 0x01, 0xf0, 0x00, 0xe0, 0x00, 0x40, 
	 0x00, 0x00, 0x01, 0xb6, 0x01, 0xb6, 0x00, 0x00, }
};
