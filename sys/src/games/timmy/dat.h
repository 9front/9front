typedef struct Hinge Hinge;
typedef struct ObjT ObjT;
typedef struct Obj Obj;
typedef struct Poly Poly;
typedef struct Vec Vec;

struct Vec {
	double x, y;
};

struct Poly {
	int nvert;
	Vec *vert;
	double invI;
};

struct Hinge {
	Vec p;
	Vec p0;
	Obj *o;
	Hinge *onext;
	Hinge *cnext, *cprev;
	Hinge *anext;
};

struct ObjT {
	int flags;
	Poly *poly;
	Hinge *hinge;
	Image *line, *fill;
	double imass;
	void (*draw)(Obj *, Image *);
	void (*move)(Obj *, double, double, double);
	void (*init)(Obj *);
};

struct Obj {
	ObjT *tab;
	Vec p, v;
	double θ, ω;
	Rectangle bbox;
	Poly *poly;
	Hinge *hinge;
	Obj *next, *prev;
	int idx;
};

enum {
	TrayH = 100,
	TraySpc = 20,
};

#define DEG 0.01745329251994330
#define Slop 0.5

#define HingeSep 4.0
