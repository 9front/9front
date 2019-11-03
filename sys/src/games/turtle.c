#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <event.h>
#include <keyboard.h>

Biobuf *bin;

double px, py;
double θ;

double *stack;
int sp;
int stacksize;

double *lines;
int lp;
int *frames;
int fp;

int curframe;

double minx = -10, maxx = 10, miny = -10, maxy = 10;

Point
cvt(double x, double y)
{
	return Pt((x - minx) * Dx(screen->r) / (maxx - minx) + screen->r.min.x, (maxy - y) * Dy(screen->r) / (maxy - miny) + screen->r.min.y);
}

void
opdraw(int, char **argv)
{
	double npx, npy, l;
	
	l = atof(argv[1]);
	npx = px + sin(θ * PI / 180) * l;
	npy = py + cos(θ * PI / 180) * l;
	lines = realloc(lines, (lp + 4) * sizeof(double));
	lines[lp++] = px;
	lines[lp++] = py;
	lines[lp++] = npx;
	lines[lp++] = npy;
	px = npx;
	py = npy;
}

void
opturn(int, char **argv)
{
	θ += atof(argv[1]);
}

void
oppush(int, char **)
{
	if(sp + 3 > stacksize){
		stack = realloc(stack, (stacksize + 3) * sizeof(double));
		stacksize += 3;
	}
	stack[sp++] = px;
	stack[sp++] = py;
	stack[sp++] = θ;
}

void
oppop(int, char **)
{
	if(sp == 0) sysfatal("stack underflow");
	θ = stack[--sp];
	py = stack[--sp];
	px = stack[--sp];
}

void
opend(int, char **)
{
	θ = 0;
	px = 0;
	py = 0;
	frames = realloc(frames, (fp + 1) * sizeof(int));
	frames[fp++] = lp;
}

typedef struct Cmd Cmd;
struct Cmd {
	char *name;
	int nargs;
	void (*op)(int, char**);
};

Cmd cmdtab[] = {
	"draw", 1, opdraw,
	"turn", 1, opturn,
	"push", 0, oppush,
	"pop", 0, oppop,
	"end", 0, opend,
};

void
runline(char *s)
{
	char *f[10];
	int nf;
	Cmd *p;
	
	nf = tokenize(s, f, nelem(f));
	if(nf == 0) return;
	for(p = cmdtab; p < cmdtab + nelem(cmdtab); p++)
		if(strcmp(p->name, f[0]) == 0){
			if(nf != p->nargs + 1 && p->nargs >= 0)
				sysfatal("wrong number of arguments for %s", f[0]);
			p->op(nf, f);
			return;
		}
	sysfatal("unknown command %s", f[0]);
}

void
redraw(void)
{
	int i;

	minx = maxx = lines[frames[curframe]];
	miny = maxy = lines[frames[curframe]+1];
	
	for(i = frames[curframe]; i < frames[curframe + 1]; i += 2){
		if(lines[i] < minx) minx = lines[i];
		if(lines[i] > maxx) maxx = lines[i];
		if(lines[i+1] < miny) miny = lines[i+1];
		if(lines[i+1] > maxy) maxy = lines[i+1];
	}
	maxx += (maxx - minx) * 0.05;
	minx -= (maxx - minx) * 0.05;
	maxy += (maxy - miny) * 0.05;
	miny -= (maxy - miny) * 0.05;
	if(minx == maxx){ minx -= 0.05; maxx += 0.05; }
	if(miny == maxy){ miny -= 0.05; maxy += 0.05; }
	draw(screen, screen->r, display->white, nil, ZP);
	for(i = frames[curframe]; i < frames[curframe + 1]; i += 4)
		line(screen, cvt(lines[i], lines[i+1]), cvt(lines[i+2], lines[i+3]), 0, 0, 0, display->black, ZP);
	flushimage(display, 1);
}

void
eresized(int new)
{
	if(new && getwindow(display, Refnone) < 0){
		fprint(2, "colors: can't reattach to window: %r\n");
		exits("resized");
	}
	redraw();
}

void
main()
{
	char *s;

	bin = Bfdopen(0, OREAD);
	if(bin == nil) sysfatal("Bfdopen: %r");
	
	frames = malloc(sizeof(int));
	frames[fp++] = 0;
		
	for(;;){
		s = Brdstr(bin, '\n', 1);
		if(s == nil) break;
		runline(s);
	}
	if(lines == nil)
		exits(nil);

	if(initdraw(nil, nil, nil) < 0)
		sysfatal("initdraw: %r");
	einit(Emouse | Ekeyboard);
	
	redraw();
	for(;;){
		switch(ekbd()){
		case Khome:
			curframe = 0;
			break;
		case Kend:
			curframe = fp - 2;
			break;
		case Kup: case Kleft:
			if(curframe > 0)
				curframe--;
			break;
		case ' ': case Kdown: case Kright:
			if(curframe < fp - 2)
				curframe++;
			break;
		case 'q': case Kdel:
			exits(nil);
		}
		redraw();
	}
}
