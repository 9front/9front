#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>

#define minx -1.0
#define maxx 1.0
#define miny -1.0
#define maxy 1.0

double speedOff = 0.001;
double decay = 0.99;
double speedBonus = 0.001;
int regenRate = 5;
double thickFactor = 0.01;
double dispThresh = 0.001;
#define ncolours 200
int nnode = 40;

typedef struct Pos Pos;
typedef struct List List;
typedef struct Node Node;
typedef struct Pack Pack;

struct Pos
{
	double x, y;
};

struct List
{
	void *next, *prev;
};

struct Node
{
	Pos;
	int die, ref, targref;
};

struct Pack
{
	List;
	Pos;
	int targ, p, q;
	double λ, v;
	Image *c;
};

Node *nodes;
Pack plist;
Mousectl *mctl;
double *dist, *path, *speed, *speedn;
int *nextn;
Image *col[ncolours];
Image *grey;

void *
emallocz(int size)
{
	void *v;
	
	v = mallocz(size, 1);
	if(v == nil)
		sysfatal("malloc: %r");
	setmalloctag(v, getcallerpc(&size));
	return v;
}

Point
convert(Pos* p)
{
	return (Point){
		screen->r.min.x + (screen->r.max.x - screen->r.min.x) *
		((p->x - minx) / (maxx - minx)),
		screen->r.min.y + (screen->r.max.y - screen->r.min.y) *
		((p->y - minx) / (maxx - minx))};
}

Pos
deconvert(Point p)
{
	return (Pos){
		minx + (maxx - minx) *
		((double)(p.x - screen->r.min.x) / (screen->r.max.x - screen->r.min.x)),
		miny + (maxy - miny) *
		((double)(p.y - screen->r.min.y) / (screen->r.max.y - screen->r.min.y))};
}

void
rect(Pos *p, int size, Image *col)
{
	Point poi;
	Rectangle r;
	
	poi = convert(p);
	r = insetrect(Rpt(poi, poi), -size);
	draw(screen, r, col, nil, ZP);
}

List *
add(List *head, List *obj)
{
	obj->prev = head->prev;
	obj->next = head;
	((List*)head->prev)->next = obj;
	head->prev = obj;
	return obj;
}

List *
unlink(List *obj)
{
	((List*)obj->prev)->next = obj->next;
	((List*)obj->next)->prev = obj->prev;
	return obj;
}

void
calcdist(void)
{
	int i, j;
	double dx, dy;
	
	dist = realloc(dist, sizeof(*dist) * nnode * nnode);
	path = realloc(path, sizeof(*path) * nnode * nnode);
	nextn = realloc(nextn, sizeof(*nextn) * nnode * nnode);
	for(i = 0; i < nnode; i++)
		for(j = 0; j < nnode; j++){
			if(nodes[j].die == 2){
				dist[i * nnode + j] = Inf(1);
				continue;
			}
			dx = nodes[i].x - nodes[j].x;
			dy = nodes[i].y - nodes[j].y;
			dist[i * nnode + j] = sqrt(dx * dx + dy * dy);
		}
}

u32int
randomcol(void)
{
	int c[3] = {0, 255, 0};
	int *j, t;
	
	c[2] = rand() % 256;
	j = c + rand() % 3;
	t = c[2];
	c[2] = *j;
	*j = t;
	if(rand()%2){
		t = c[1];
		c[1] = c[0];
		c[0] = t;
	}
	return (c[0] << 24) | (c[1] << 16) | (c[2] << 8) | 0xFF;
}

void
createstuff(void)
{
	int i;
	plist.next = &plist;
	plist.prev = &plist;
	
	for(i = 0; i < ncolours; i++)
		col[i] = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, randomcol());
	grey = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0x888888FF);
	
	nodes = emallocz(sizeof(*nodes) * nnode);
	for(i = 0; i < nnode; i++){
		nodes[i].x = frand() * (maxx - minx) + minx;
		nodes[i].y = frand() * (maxy - miny) + miny;
	}
	calcdist();
	speed = emallocz(sizeof(*speed) * nnode * nnode);
	speedn = emallocz(sizeof(*speedn) * nnode * nnode);
}

void
resizespeed(int diff)
{
	int nnode1, i, j;
	
	nnode1 = nnode - diff;
	speedn = realloc(speedn, sizeof(*speedn) * nnode * nnode);
	for(i = 0; i < nnode; i++)
		for(j = 0; j < nnode; j++)
			if(i < nnode1 && j < nnode1)
				speedn[i * nnode + j] = speed[i * nnode1 + j];
			else
				speedn[i * nnode + j] = 0;
	speed = realloc(speed, sizeof(*speedn) * nnode * nnode);
	memcpy(speed, speedn, sizeof(*speedn) * nnode * nnode);
}

void
createpacket(void)
{
	Pack *p;
	
	p = emallocz(sizeof(*p));
	do{
		p->q = rand() % nnode;
		p->targ = rand() % nnode;
	}while(p->q == p->targ || nodes[p->q].die || nodes[p->targ].die);
	nodes[p->q].ref++;
	nodes[p->targ].targref++;
	p->p = -1;
	p->Pos = nodes[p->q].Pos;
	p->c = col[rand() % ncolours];
	add(&plist, p);
}

int
getpath(int i, int j)
{
	int k;

	i *= nnode;
	while((k = nextn[i + j]) != j)
		j = k;
	return j;
}

void
floyd(void)
{
	int i, j, k;
	double a, *b;

	for(i = 0; i < nnode; i++)
		for(j = 0; j < nnode; j++){
			path[i * nnode + j] = dist[i * nnode + j] / (speedOff + speed[i * nnode + j]);
			nextn[i * nnode + j] = j;
		}
	for(k = 0; k < nnode; k++)
		for(i = 0; i < nnode; i++)
			for(j = 0; j < nnode; j++){
				a = path[i * nnode + k] + path[k * nnode + j];
				b = path + i * nnode + j;
				if(a < *b){
					*b = a;
					nextn[i * nnode + j] = k;
				}
			}
}

void
createnode(Pos p)
{
	int i, j;
	
	j = nnode;
	for(i = 0; i < nnode; i++){
		if(nodes[i].Pos.x == p.x && nodes[i].Pos.y == p.y)
			return;
		if(nodes[i].die == 3 && j == nnode)
			j = i;
	}
	if(j == nnode){
		nodes = realloc(nodes, sizeof(*nodes) * ++nnode);
		resizespeed(1);
	}
	nodes[j].Pos = p;
	nodes[j].die = 0;
	nodes[j].ref = 0;
	nodes[j].targref = 0;
	calcdist();
	floyd();
}

Pack *
advancepacket(Pack *p)
{
	Pack *n;
	Node *np, *nq;

	if(p->p == -1){
		p->p = p->q;
		p->q = getpath(p->q, p->targ);
		nodes[p->q].ref++;
		p->λ = 0;
		p->v = (speedOff + speed[p->p * nnode + p->q]) / dist[p->p * nnode + p->q];
	}else{
		p->λ += p->v;
		if(p->λ >= 1){
			speedn[p->p * nnode + p->q] += speedBonus;
			speedn[p->q * nnode + p->p] += speedBonus;
			nodes[p->p].ref--;
			p->p = -1;
			if(p->q == p->targ){
				n = p->next;
				nodes[p->q].ref--;
				nodes[p->q].targref--;
				free(unlink(p));
				return n;
			}
			p->Pos = nodes[p->q].Pos;
			return p->next;
		}
	}
	np = nodes + p->p;
	nq = nodes + p->q;
	p->x = np->x * (1 - p->λ) + nq->x * p->λ;
	p->y = np->y * (1 - p->λ) + nq->y * p->λ;
	return p->next;
}

long sync;

void
timing()
{
	for(;;){
		semrelease(&sync, 1);
		sleep(25);
	}
}

void
simstep(void)
{
	static int regen;
	Pack *p;
	int i, j;

	for(p = plist.next; p != &plist; )
		p = advancepacket(p);
	for(i = 0; i < nnode; i++){
		if(nodes[i].die == 1 && nodes[i].targref == 0){
			nodes[i].die++;
			calcdist();
		}
		if(nodes[i].die == 2 && nodes[i].ref == 0){
			nodes[i].die++;
			for(j = 0; j < nnode; j++)
				speedn[i * nnode + j] = speedn[i + j * nnode] = 0;
		}
	}
	for(i = 0; i < nnode * nnode; i++)
		speed[i] = speedn[i] *= decay;
	floyd();
	if(regen-- == 0){
		regen = rand() % regenRate;
		createpacket();
		createpacket();
	}
}

void
domouse(void)
{
	static Mouse m;
	int lastbut;
	double dx, dy, d;
	int i;
	Point poi;
	
	if(nbrecv(mctl->resizec, &i) == 1)
		if(getwindow(display, Refnone) < 0)
			sysfatal("getwindow: %r");
	lastbut = m.buttons;
	nbrecv(mctl->c, &m);
	if(lastbut & 4 && !(m.buttons & 4))
		for(i = 0; i < nnode; i++){
			poi = convert(&nodes[i]);
			dx = poi.x - m.xy.x;
			dy = poi.y - m.xy.y;
			d = sqrt(dx * dx + dy * dy);
			if(d < 5){
				nodes[i].die = 1;
				break;
			}
		}
	if(lastbut & 1 && !(m.buttons & 1))
		createnode(deconvert(m.xy));
}

void
usage(void)
{
	fprint(2, "USAGE: %s options\n", argv0);
	fprint(2, " -n number of nodes [40]\n");
	fprint(2, " -o speed of unused connections [0.001]\n");
	fprint(2, " -d decay rate [0.99]\n");
	fprint(2, " -b speed bonus per packet [0.001]\n");
	fprint(2, " -r packet generation period [5]\n");
	fprint(2, " -t line thickness factor [0.01]\n");
	fprint(2, " -T display threshold [0.001]\n");
	exits("usage");
}

void
threadmain(int argc, char **argv)
{
	Node *n;
	Pack *p;
	int i, j;
	
	ARGBEGIN{
	case 'n': nnode = atoi(EARGF(usage())); break;
	case 'o': speedOff = atof(EARGF(usage())); break;
	case 'd': decay = atof(EARGF(usage())); break;
	case 'b': speedBonus = atof(EARGF(usage())); break;
	case 'r': regenRate = atoi(EARGF(usage())); break;
	case 't': thickFactor = atof(EARGF(usage())); break;
	case 'T': dispThresh = atof(EARGF(usage())); break;
	default: usage();
	}ARGEND;

	initdraw(nil, nil, nil);
	mctl = initmouse(nil, screen);
	srand(time(0));
	createstuff();
	floyd();
	proccreate(timing, nil, mainstacksize);
	for(;;){
		domouse();
		draw(screen, screen->r, display->white, nil, ZP);
		for(i = 0; i < nnode; i++)
			for(j = 0; j < i; j++)
				if(speed[i * nnode + j] >= dispThresh)
					line(screen, convert(nodes + i), convert(nodes + j), 0, 0, speed[i * nnode + j] / thickFactor, display->black, ZP);
		for(n = nodes; n < nodes + nnode; n++)
			if(!n->die || n->ref)
				rect(n, 3, n->die ? grey : display->black);
		for(p = plist.next; p != &plist; p = p->next)
			rect(p, 2, p->c);
		flushimage(display, 1);
		simstep();
		semacquire(&sync, 1);
	}
}
