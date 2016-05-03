#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>

enum {
	Kdel = 0x7f
};

int N = 49,
	pathlen = 1000,
	nosnake;

double dt = 0.01,
	xmin = -40,
	xmax = 40,
	ymin = -40,
	ymax = 40,
	v0 = 0.1;

#define mini(a,b) (((a)<(b))?(a):(b))

typedef struct Particle Particle;
struct Particle {
	double x, y;
	double vx, vy;
	double ax, ay;
	double prevx, prevy;
	Image* col;
};

typedef struct Path Path;
struct Path {
	int *x, *y;
};

int colors[] = {
              DBlack,
              DRed,
              DGreen,
              DBlue,
              DCyan,
              DMagenta,
              DDarkyellow,
              DDarkgreen,
              DPalegreen,
              DMedgreen,
              DDarkblue,
              DPalebluegreen,
              DPaleblue,
              DBluegreen,
              DGreygreen,
              DPalegreygreen,
              DYellowgreen,
              DMedblue,
              DGreyblue,
              DPalegreyblue,
              DPurpleblue
};

Particle	*A, *B;
Particle	*prev, *cur;
Path		*paths;

void
reset(void)
{
	int j, grid = sqrt(N)+0.5;
	Particle *p;
	draw(screen, screen->r, display->white, 0, ZP);
	for(j=0;j<N;j++) {
		p = prev+j;
		p->x = 2*(j%grid)+frand()/2;
		p->y = 2*(j/grid)+frand()/2;
		p->vx = 1.*v0*frand();
		p->vy = 1.*v0*frand();
		p->prevx = p->x - p->vx * dt;
		p->prevy = p->y - p->vy * dt;
		p->col = allocimage(display, Rect(0,0,1,1), screen->chan, 1, colors[rand()%(sizeof(colors)/sizeof(int))]);
		if(!p->col) sysfatal("allocimage");
	}
}

void
reverse(void)
{
	Particle *p, *q;
	Path *pa;
	int i;

	draw(screen, screen->r, display->white, 0, ZP);
	for(i=0;i<N;i++){
		pa=paths+i;
		memset(pa->x, 0, sizeof(int) * pathlen);
		memset(pa->y, 0, sizeof(int) * pathlen);
		p=prev+i;
		q=cur+i;
		p->vx = -q->vx;
		p->vy = -q->vy;
		p->prevx = p->x;
		p->prevy = p->y;
		p->x = q->x;
		p->y = q->y;
	}
}

void
drawpath(Path *p, Image *col, int i)
{
	int j;

	if((j = i+1) == pathlen)
		j = 0;
	draw(screen, Rect(p->x[i], p->y[i], p->x[i]+1, p->y[i]+1), col, 0, ZP);
	if(nosnake)
		return;
	draw(screen, Rect(p->x[j], p->y[j], p->x[j]+1, p->y[j]+1), display->white, 0, ZP);
}

void
usage(void)
{
	print("USAGE: mole options\n");
	print(" -N number of particles [49]\n");
	print(" -x left boundary [-40]\n");
	print(" -X right boundary [40]\n");
	print(" -y top boundary [-40]\n");
	print(" -Y bottom boundary [40]\n");
	print(" -t time step [0.01]\n");
	print(" -v maximum start velocity [0.1]\n");
	print(" -P path length [1000]\n");	
	exits("usage");
}

void
main(int argc, char** argv)
{
	int i, j;
	Particle *p, *q;
	Path *pa;
	double dx, dx1, dx2, dy, dy1, dy2, R, F;
	char* f;
	
	#define FARG(c, v) case c: if(!(f=ARGF())) usage(); v = atof(f); break;
	ARGBEGIN {
	case 'N': if(!(f=ARGF())) usage(); N = atoi(f); break;
	case 'P': if(!(f=ARGF())) usage(); pathlen = atoi(f); break;
	FARG('v', v0);
	FARG('x', xmin);
	FARG('X', xmax);
	FARG('y', ymin);
	FARG('Y', ymax);
	FARG('t', dt);
	default: usage();
	} ARGEND;
	
	if(pathlen == 0) {
		nosnake = 1;
		pathlen = 1000;
	}
	A = calloc(sizeof(Particle), N);
	B = calloc(sizeof(Particle), N);
	paths = calloc(sizeof(Path), N);
	for(pa=paths;pa<paths+N;pa++){
		pa->x = calloc(sizeof(int), pathlen);
		pa->y = calloc(sizeof(int), pathlen);
	}
	prev = A;
	cur = B;
	srand(time(0));
	initdraw(0, 0, "Molecular Dynamics");
	einit(Emouse | Ekeyboard);
	reset();

	for(i=0;; i++) {
		if(i == pathlen)
			i = 0;
		memset(cur, 0, sizeof(Particle) * N);
		for(p=prev;p<prev+N;p++) {
			for(q=prev;q<p;q++) {
				dx1 = fabs(p->x - q->x);
				dx2 = xmax - xmin - dx1;
				dx = mini(dx1, dx2);
				dy1 = fabs(p->y - q->y);
				dy2 = ymax - ymin - dy1;
				dy = mini(dy1, dy2);
				R = dx*dx + dy*dy;
				if(R >= 9) continue;
				R = 1/sqrt(R);
				double R2, R4, R6, R12;
				R2 = R * R;
				R4 = R2 * R2;
				R6 = R4 * R2;
				R12 = R6 * R6;
				F = 24*(2*R12 - R6);
				if(p->x < q->x) dx = -dx;
				if(p->y < q->y) dy = -dy;
				if(dx1 > dx2) dx = -dx;
				if(dy1 > dy2) dy = -dy;
				dx *= F;
				dy *= F;
				(p-prev+cur)->ax += dx;
				(p-prev+cur)->ay += dy;
				(q-prev+cur)->ax -= dx;
				(q-prev+cur)->ay -= dy;
			}
		}
		for(j=0;j<N;j++) {
			pa = paths+j;
			p = prev+j;
			q = cur+j;
			q->x = 2*p->x - p->prevx + q->ax * dt*dt;
			q->y = 2*p->y - p->prevy + q->ay * dt*dt;
			q->vx = (q->x - p->prevx) / (2*dt);
			q->vy = (q->y - p->prevy) / (2*dt);
			q->prevx = p->x;
			q->prevy = p->y;
			if(q->x > xmax) {q->x -= xmax - xmin; q->prevx -= xmax - xmin;}
			if(q->x < xmin) {q->x += xmax - xmin; q->prevx += xmax - xmin;}
			if(q->y > ymax) {q->y -= ymax - ymin; q->prevy -= ymax - ymin;}
			if(q->y < ymin) {q->y += ymax - ymin; q->prevy += ymax - ymin;}
			q->col = p->col;
			pa->x[i] = (screen->r.max.x - screen->r.min.x) * (q->x - xmin) / (xmax - xmin) + screen->r.min.x;
			pa->y[i] = (screen->r.max.y - screen->r.min.y) * (q->y - ymin) / (ymax - ymin) + screen->r.min.y;
			drawpath(pa, p->col, i);
		}

		Particle* tmp = prev;
		prev = cur;
		cur = tmp;
		flushimage(display, 1);
		
		
		if(ecankbd()) {
			switch(ekbd()) {
				case 'q': case Kdel: exits(0); break;
				case 'r': reset(); break;
				case 'R': reverse(); break;
				case 'f': draw(screen, screen->r, display->white, 0, ZP); break;
			}
		}
	}
}

void
eresized(int new)
{
	if(new) getwindow(display, Refnone);
}
