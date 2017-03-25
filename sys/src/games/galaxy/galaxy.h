typedef struct QB QB;
typedef struct Body Body;
typedef struct Quad Quad;
typedef struct Vector Vector;

struct QB {
	union {
		Quad *q;
		Body *b;
	};
	int t;
};

struct Vector {
	double x, y;
};

struct Body {
	Vector;
	Vector v, a, newa;
	double size, mass;
	Image *col;
};

struct Quad {
	Vector;
	QB c[4];
	double mass;
};

#pragma varargck type "B" Body*

struct {
	QLock;
	Body *a;
	int nb, max;
} glxy;

struct {
	Quad *a;
	int l, max;
} quads;

#define π2 6.28318530718e0

enum {
	EMPTY,
	QUAD,
	BODY,
};

void quit(char*);

Image *randcol(void);
Point topoint(Vector);
Vector tovector(Point);

Body *body(void);
void drawbody(Body*);
Vector center(void);
void glxyinit(void);
int Bfmt(Fmt*);
void readglxy(int);
void writeglxy(int);
void drawglxy(void);

void simulate(void*);

void quadcalc(Body*, QB, double);
int quadins(Body*, double);
void growquads(void);
void quadsinit(void);

int stats, quaddepth, calcs, extraproc, throttle;
double G, θ, scale, ε, LIM, dt, dt², avgcalcs;
Point orig;
QB space;
Body ZB;

enum {
	STK = 8192,
};

#define STATS(e) if(stats) {e}

#define CHECKLIM(b, f) \
	if(((f) = fabs((b)->x)) > LIM/2)	\
		LIM = (f)*2;	\
	if(((f) = fabs((b)->y)) > LIM/2)	\
		LIM = (f)*2
