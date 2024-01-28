#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>

enum{
	/* difficulty levels (how many circles are initially occupied) */
	DEasy,	/* 10≤x<15 */
	DMed,	/* 5≤x<10 */
	DHard,	/* 0≤x<5 */

	/* dynamic? original game has a fixed grid size, but we don't need to abide by it */
	SzX = 11,
	SzY = 11, 

	Border = 10,
	/* movement directions */
	NE,
	E,
	SE,
	SW,
	W,
	NW,

	Won = 1,	/* game-ending states */
	Lost = 2,
};

Font *font;

int difficulty = DMed;
int finished;

int grid[SzX][SzY];
int ogrid[SzX][SzY];	/* so we can restart levels */

Image	*gl;	/* glenda */
Image 	*glm;	/* glenda's mask */
Image	*cc; /* clicked */
Image	*ec; /* empty; not clicked */
Image 	*bg;
Image 	*lost;
Image	*won;


char *mbuttons[] = 
{
	"Easy",
	"Medium",
	"Hard",
	0
};

char *rbuttons[] = 
{
	"New",
	"Reset",
	"Exit",
	0
};

Menu mmenu = 
{
	mbuttons,
};

Menu rmenu =
{
	rbuttons,
};

Image *
eallocimage(Rectangle r, int repl, uint color)
{
	Image *tmp;

	tmp = allocimage(display, r, screen->chan, repl, color);
	if(tmp == nil)
		sysfatal("cannot allocate buffer image: %r");

	return tmp;
}

Image *
eloadfile(char *path)
{
	Image *img;
	int fd;

	fd = open(path, OREAD);
	if(fd < 0) {
		fprint(2, "cannot open image file %s: %r\n", path);
		exits("image");
	}
	img = readimage(display, fd, 0);
	if(img == nil)
		sysfatal("cannot load image: %r");
	close(fd);
	
	return img;
}


void
allocimages(void)
{
	Rectangle one = Rect(0, 0, 1, 1);
	
	cc = eallocimage(one, 1, 0x777777FF);
	ec = eallocimage(one, 1, DPalegreen);
	bg = eallocimage(one, 1, DPurpleblue);
	lost = eallocimage(one, 1, DRed);
	won = eallocimage(one, 1, DGreen);
	gl = eloadfile("/lib/face/48x48x4/g/glenda.1");

	glm = allocimage(display, Rect(0, 0, 48, 48), gl->chan, 1, DCyan);
	if(glm == nil)
        		sysfatal("cannot allocate mask: %r");

    	draw(glm, glm->r, display->white, nil, ZP);
    	gendraw(glm, glm->r, display->black, ZP, gl, gl->r.min);
    	freeimage(gl);
    	gl = display->black;


}

/* unnecessary calculations here, but it's fine */
Point
board2pix(int x, int y)
{
	float d, rx, ry, yh;
	int nx, ny;

	d = (float)(Dx(screen->r) > Dy(screen->r)) ? Dy(screen->r) -20 : Dx(screen->r) -20;
	rx = d/(float)SzX;
	rx = rx/2.0;
	ry = d/(float)SzY;
	ry = ry/2.0;

	yh = ry/3.73205082;

	nx = (int)((float)x*rx*2.0+rx +(y%2?rx:0.0)); /* nx = x*(2rx) + rx + rx (conditional) */
	ny = (int)((float)y*(ry*2.0-(y>0?yh:0.0)) + ry); /* ny = y*(2ry-yh) +ry */
	return Pt(nx, ny);
}

Point 
pix2board(int x, int y)
{
	float d, rx, ry, yh;
	int ny, nx;

	/* XXX: float→int causes small rounding errors */

	d = (float)(Dx(screen->r) > Dy(screen->r)) ? Dy(screen->r) -20: Dx(screen->r)-20;
	rx = d/(float)SzX;
	rx = rx/2.0;
	ry =d/(float)SzY;
	ry = ry/2.0;

	yh = ry/3.73205082;

	/* reverse board2pix() */
	ny = (int)(((float)y - ry)/(2*ry - ((y>2*ry)?yh:0.0)) + 0.5); /* ny = (y - ry)/(2ry-yh) */
	nx = (int)(((float)x - rx - (ny%2?rx:0.0))/(rx*2.0) + 0.5); /* nx = (x - rx - rx)/2rx */
	
	if (nx >= SzX)
		nx = SzX-1;
	if (ny >=SzY)
		ny = SzY-1;

	return Pt(nx, ny);
}

void
initlevel(void)
{
	int i, cnt = 10, x, y;

	for(x = 0; x < SzX; x++)
		for(y = 0; y < SzY; y++)
			ogrid[x][y] = 100;

	switch(difficulty){
	case DEasy:
		cnt = 10 + nrand(5);
		break;
	case DMed:
		cnt = 5 + nrand(5);
		break;
	case DHard:
		cnt = nrand(5);
		break;
	}
	for(i = 0; i < cnt; i++) {
		do {
			x = nrand(SzX);
			y = nrand(SzY);
		} while(ogrid[x][y] != 100);
		ogrid[x][y] = 999;
	}

	ogrid[SzX/2][SzY/2] = 1000;

	memcpy(grid, ogrid, sizeof grid);

	finished = 0;

}

void
drawlevel(void)
{
	Point p;
	int  x, y, rx, ry, d;
	char *s = nil;

	if(finished)
		draw(screen, screen->r, finished==Won?won:lost, nil, ZP);
	else
		draw(screen, screen->r, bg, nil, ZP);

	d = (Dx(screen->r) > Dy(screen->r)) ? Dy(screen->r) -20: Dx(screen->r) -20;
	rx = (int)ceil((float)(d-2*Border)/(float)SzX)/2;
	ry = (int)ceil((float)(d-2*Border)/(float)SzY)/2;

	for(x = 0; x < SzX; x++) {
		for(y = 0; y < SzY; y++) {
			p = board2pix(x, y);
			switch(grid[x][y]){
			case 999: 
				fillellipse(screen, addpt(screen->r.min, p), rx, ry, cc, ZP);
				break;
			case 1000:
				p = addpt(screen->r.min, p);
				fillellipse(screen, p, rx, ry, ec, ZP);
				p = subpt(p, Pt(24, 24));
				draw(screen, Rpt(p, addpt(p, Pt(48, 48))), gl, glm, ZP);
				break;
			default:
				fillellipse(screen, addpt(screen->r.min, p), rx, ry, ec, ZP);
				USED(s);
				/* uncomment the following to see game state and field scores */
				/*s = smprint("%d", grid[x][y]);
				string(screen, addpt(screen->r.min, p), display->black, ZP, font, s);
				free(s);
				*/
				break;
			}
		}
	}
	flushimage(display, 1);
}

void
domove(int dir, int x, int y)
{
	if(x == 0 || x == SzX-1 || y == 0 || y == SzY-1)
		goto done;

	switch(dir){
	case NE:
		if(y%2)
			grid[x+1][y-1] = 1000;
		else	
			grid[x][y-1] = 1000;
		break;
	case E:
		grid[x+1][y] = 1000;
		break;
	case SE:
		if(y%2)
			grid[x+1][y+1] = 1000;
		else
			grid[x][y+1] = 1000;
		break;
	case SW:
		if(y%2)
			grid[x][y+1] = 1000;
		else
			grid[x-1][y+1] = 1000;
		break;
	case W:
		grid[x-1][y] = 1000;
		break;
	case NW:
		if(y%2)
			grid[x][y-1] = 1000;
		else
			grid[x-1][y-1] = 1000;
		break;
	}
done:
	grid[x][y] = 100;
}

Point
findglenda(void)
{
	int x, y;
	for(x = 0; x < SzX; x++)
		for(y = 0; y < SzY; y++)
			if(grid[x][y] == 1000)
				return Pt(x, y);
	return Pt(-1, -1);
}

int 
checknext(int dir, int x, int y)
{
	switch(dir){
	case NE: 
		return grid[x+(y%2?1:0)][y-1];
	case E:
		return grid[x+1][y];
	case SE:
		return grid[x+(y%2?1:0)][y+1];
	case SW:
		return grid[x+(y%2?0:-1)][y+1];
	case W:
		return grid[x-1][y];
	case NW:
		return grid[x+(y%2?0:-1)][y-1];
	default:
		sysfatal("andrey messed up big time");
	}
}
/* the following two routines constitute the "game AI"
* they score the field based on the number of moves
* required to reach the edge from a particular point
* scores > 100 are "dead spots" (this assumes the field 
* is not larger than ~100*2
* 
* routines need to run at least twice to ensure a field is properly
* scored: there are errors that creep up due to the nature of 
* traversing the board
*/
int 
score1(int x, int y) {
	int dir, min = 999, next;

	if(x == 0 || x == SzX-1 || y == 0 || y == SzY-1)
		return 1; 		/* we can always escape from the edges */

	for(dir = NE; dir <= NW; dir++) {
		next = checknext(dir, x, y);
		if(next < min)
			min = next;
	}
	if(min == 999) return 998;
	return 1+min;
}

void
calc(void)
{
	int i, x, y;
	for(i = 0; i < SzX; i++) /* assumes SzX = SzY */
		for(x = i; x < SzX-i; x++)
			for(y = i; y < SzY-i; y++)
				if(grid[x][y] != 999)
					grid[x][y] = score1(x, y);
}

void
nextglenda(void)
{
	int min =1000, next, dir, nextdir = 0, count = 0;
	Point p = findglenda();

	calc();
	calc();
	calc();

	grid[p.x][p.y] = 1000;
	
	for(dir = NE; dir <= NW; dir++) {
		next = checknext(dir, p.x, p.y);
		if(next < min) {
			min = next;
			nextdir = dir;
			++count;
		} else if(next == min) {
			nextdir = (nrand(++count) == 0)?dir:nextdir;
		}
	}
	if(min < 100 || min == 999)
		domove(nextdir, p.x, p.y);
	else
		finished = Won;

	if(eqpt(findglenda(), Pt(-1, -1)))
		finished = Lost;
}

int
checkfinished(void)
{
	int i, j;
	for(i = 0; i < SzX; i++)
		for(j = 0; j < SzY; j++)
			if(grid[i][j] == 'E')
				return 0;
	return 1;
}

void
move(Point m)
{
	Point p, nm;
	int x, y;

	nm = subpt(m, screen->r.min);

	/* figure out where the click falls */
	p = pix2board(nm.x, nm.y);
	
	if(grid[p.x][p.y] >= 999)
		return;

	/* reset the board scores */
	grid[p.x][p.y] = 999;
	for(x = 0; x < SzX; x++)
		for(y = 0; y < SzY; y++)
			if(grid[x][y] != 999 && grid[x][y] != 1000)
				grid[x][y] = 100;
	
	nextglenda();
}

void
resize(void)
{
	int fd, size = (Dx(screen->r) > Dy(screen->r)) ? Dy(screen->r) + 20 : Dx(screen->r)+20; 

	fd = open("/dev/wctl", OWRITE);
	if(fd >= 0){
		fprint(fd, "resize -dx %d -dy %d", size, size);
		close(fd);
	}

}


void
eresized(int new)
{
	if(new && getwindow(display, Refnone) < 0)
		sysfatal("can't reattach to window");
	
	drawlevel();
}

void 
main(int argc, char **argv)
{
	Mouse m;
	Event ev;
	int e, mousedown=0;

	USED(argv, argc);

	if(initdraw(nil, nil, "glendy") < 0)
		sysfatal("initdraw failed: %r");
	einit(Emouse);

	resize();

	srand(time(0));

	allocimages();
	initlevel();	/* must happen before "eresized" */
	eresized(0);

	for(;;) {
		e = event(&ev);
		switch(e) {
		case Emouse:
			m = ev.mouse;
			if(m.buttons == 0) {
				if(mousedown && !finished) {
					mousedown = 0;
					move(m.xy);
					drawlevel();
				}
			}
			if(m.buttons&1) {
				mousedown = 1;
			}
			if(m.buttons&2) {
				switch(emenuhit(2, &m, &mmenu)) {
				case 0:
					difficulty = DEasy;
					initlevel();
					break;
				case 1:				
					difficulty = DMed;
					initlevel();
					break;
				case 2:
					difficulty = DHard;
					initlevel();
					break;
				}
				drawlevel();
			}
			if(m.buttons&4) {
				switch(emenuhit(3, &m, &rmenu)) {
				case 0:
					initlevel();
					break;
				case 1:
					memcpy(grid, ogrid, sizeof grid);
					finished = 0;
					break;
				case 2:
					exits(nil);
				}
				drawlevel();
			}
			break;
		}
	}
}
