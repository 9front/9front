#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <draw.h>
#include <event.h>

typedef struct Op Op;
typedef struct Operator Operator;
typedef struct Token Token;
typedef struct Constant Constant;
typedef struct Code Code;

enum {
	ONONE,
	ONUMBER,
	OVAR,
	OUNARY,
	OBINARY,
};

struct Op {
	int type;
	union {
		void (*f)(void);
		double val;
	};
};

enum {
	TNONE,
	TNUMBER,
	TVAR,
	TOP,
	TPARENL,
	TPARENR,
};

struct Token {
	int type;
	union {
		Operator *op;
		double val;
	};
	Token *next;
};

double *stack, *sp;
void add(void) { sp--; *sp += *(sp+1); }
void sub(void) { sp--; *sp -= *(sp+1); }
void mul(void) { sp--; *sp *= *(sp+1); }
void div(void) { sp--; *sp /= *(sp+1); }
void pot(void) { sp--; *sp = pow(*sp, *(sp+1)); }
void osin(void) { *sp = sin(*sp); }
void ocos(void) { *sp = cos(*sp); }
void otan(void) { *sp = tan(*sp); }
void oasin(void) { *sp = asin(*sp); }
void oacos(void) { *sp = acos(*sp); }
void oatan(void) { *sp = atan(*sp); }
void osqrt(void) { *sp = sqrt(*sp); }
void oexp(void) { *sp = sqrt(*sp); }
void olog(void) { *sp = log10(*sp); }
void oln(void) { *sp = log(*sp); }

struct Operator {
	char *s;
	char type;
	char rassoc;
	short prec;
	void (*f)(void);
} ops[] = {
	"+",	OBINARY,	0,	0,	add,
	"-",	OBINARY,	0,	0,	sub,
	"*",	OBINARY,	0,	100,	mul,
	"/",	OBINARY,	0,	100,	div,
	"^",	OBINARY,	1,	200,	pot,
	"sin",	OUNARY,		0,	50,	osin,
	"cos",	OUNARY,		0,	50,	ocos,
	"tan",	OUNARY,		0,	50,	otan,
	"asin",	OUNARY,		0,	50,	oasin,
	"acos",	OUNARY,		0,	50,	oacos,
	"atan",	OUNARY,		0,	50,	oatan,
	"sqrt",	OUNARY,		0,	50,	osqrt,
	"exp",	OUNARY,		0,	50,	oexp,
	"log",	OUNARY,		0,	50,	olog,
	"ln",	OUNARY,		0,	50,	oln,
};

struct Constant {
	char *s;
	double val;
} consts[] = {
	"pi",	3.14159265359,
	"π",	3.14159265359,
	"e",	2.71828182846,
};

struct Code {
	Op* p;
	int len, cap;
} *fns;
int nfns;

Token *opstackbot;
double xmin = -10, xmax = 10;
double ymin = -10, ymax = 10;
Image *color;
int cflag;
char *imagedata;
int picx = 640, picy = 480;

void *
emalloc(int size)
{
	void *v;
	
	v = mallocz(size, 1);
	if(v == nil)
		sysfatal("emalloc: %r");
	setmalloctag(v, getcallerpc(&size));
	return v;
}

void
addop(Code *c, Op o)
{
	if(c->len >= c->cap) {
		c->cap += 32;
		c->p = realloc(c->p, sizeof(Op) * c->cap);
		if(c->p == nil)
			sysfatal("realloc: %r");
	}
	c->p[c->len++] = o;
}

void
push(Token *t)
{
	t->next = opstackbot;
	opstackbot = t;
}

void
pop(Code *c)
{
	Token *t;
	Op o;
	
	t = opstackbot;
	if(t == nil)
		sysfatal("stack underflow");
	opstackbot = t->next;
	if(t->type != TOP)
		sysfatal("non-operator pop");
	o.type = t->op->type;
	o.f = t->op->f;
	addop(c, o);
	free(t);
}

void
popdel(void)
{
	Token *t;
	
	t = opstackbot;
	opstackbot = t->next;
	free(t);
}

Token *
lex(char **s)
{
	Token *t;
	Operator *o;
	Constant *c;

	while(isspace(**s))
		(*s)++;
	if(**s == 0)
		return nil;
	
	t = emalloc(sizeof(*t));
	if(isdigit(**s)) {
		t->type = TNUMBER;
		t->val = strtod(*s, s);
		return t;
	}
	if(**s == '(') {
		t->type = TPARENL;
		(*s)++;
		return t;
	}
	if(**s == ')') {
		t->type = TPARENR;
		(*s)++;
		return t;
	}
	if(**s == 'x') {
		t->type = TVAR;
		(*s)++;
		return t;
	}
	for(o = ops; o < ops + nelem(ops); o++)
		if(strncmp(*s, o->s, strlen(o->s)) == 0) {
			t->type = TOP;
			t->op = o;
			*s += strlen(o->s);
			return t;
		}
	for(c = consts; c < consts + nelem(consts); c++)
		if(strncmp(*s, c->s, strlen(c->s)) == 0) {
			t->type = TNUMBER;
			t->val = c->val;
			*s += strlen(c->s);
			return t;
		}
	sysfatal("syntax error at %s", *s);
	return nil;
}

void
parse(Code *c, char *s)
{
	Token *t;
	Op o;
	
	while(t = lex(&s)) {
		switch(t->type) {
		case TNUMBER:
			o.type = ONUMBER;
			o.val = t->val;
			addop(c, o);
			free(t);
			break;
		case TVAR:
			o.type = OVAR;
			addop(c, o);
			free(t);
			break;
		case TOP:
			if(t->op->type == OBINARY)
				while(opstackbot != nil && opstackbot->type == TOP &&
					(opstackbot->op->prec > t->op->prec ||
					t->op->rassoc && opstackbot->op->prec == t->op->prec))
					pop(c);
			push(t);
			break;
		case TPARENL:
			push(t);
			break;
		case TPARENR:
			while(opstackbot != nil && opstackbot->type == TOP)
				pop(c);
			if(opstackbot == nil)
				sysfatal("mismatched parentheses");
			popdel();
			free(t);
			break;
		default:
			sysfatal("unknown token type %d", t->type);
		}
	}
	while(opstackbot != nil)
		switch(opstackbot->type) {
		case TOP:
			pop(c);
			break;
		case TPARENL:
			sysfatal("mismatched parentheses");
		default:
			sysfatal("syntax error");
		}
}

int
calcstack(Code *c)
{
	int cur, max;
	Op *o;
	
	cur = 0;
	max = 0;
	for(o = c->p; o < c->p + c->len; o++)
		switch(o->type) {
		case ONUMBER: case OVAR:
			if(++cur > max)
				max = cur;
			break;
		case OUNARY:
			if(cur == 0)
				sysfatal("syntax error");
			break;
		case OBINARY:
			if(cur <= 1)
				sysfatal("syntax error");
			cur--;
			break;
		}

	if(cur != 1)
		sysfatal("syntax error");
	return max;
}

double
calc(Code *c, double x)
{
	Op *o;
	
	sp = stack - 1;
	for(o = c->p; o < c->p + c->len; o++)
		switch(o->type) {
		case ONUMBER:
			*++sp = o->val;
			break;
		case OVAR:
			*++sp = x;
			break;
		case OUNARY: case OBINARY:
			o->f();
		}
	return *sp;
}

double
convx(Rectangle *r, int x)
{
	return (xmax - xmin) * (x - r->min.x) / (r->max.x - r->min.x) + xmin;
}

int
deconvx(Rectangle *r, double dx)
{
	return (dx - xmin) * (r->max.x - r->min.x) / (xmax - xmin) + r->min.x + 0.5;
}

double
convy(Rectangle *r, int y)
{
	return (ymax - ymin) * (r->max.y - y) / (r->max.y - r->min.y) + ymin;
}

int
deconvy(Rectangle *r, double dy)
{
	return (ymax - dy) * (r->max.y - r->min.y) / (ymax - ymin) + r->min.y + 0.5;
}

void
pixel(int x, int y)
{
	char *p;

	if(cflag) {
		if(x >= picx || y >= picy || x < 0 || y < 0)
			return;
		p = imagedata + (picx * y + x) * 3;
		p[0] = p[1] = p[2] = 0;
	} else
		draw(screen, Rect(x, y, x + 1, y + 1), color, nil, ZP);
}

void
drawinter(Code *co, Rectangle *r, double x1, double x2, int n)
{
	double y1, y2;
	int iy1, iy2;
	int ix1, ix2;

	ix1 = deconvx(r, x1);
	ix2 = deconvx(r, x2);
	iy1 = iy2 = 0;
	y1 = calc(co, x1);
	if(!isNaN(y1)) {
		iy1 = deconvy(r, y1);
		pixel(ix1, iy1);
	}
	y2 = calc(co, x2);
	if(!isNaN(y2)) {
		iy2 = deconvy(r, y2);
		pixel(ix2, iy2);
	}
	if(isNaN(y1) || isNaN(y2))
		return;
	if(n >= 10)
		return;
	if(iy2 >= iy1 - 1 && iy2 <= iy1 + 1)
		return;
	if(iy1 > r->max.y && iy2 > r->max.y)
		return;
	if(iy1 < r->min.y && iy2 < r->min.y)
		return;
	drawinter(co, r, x1, (x1 + x2) / 2, n + 1);
	drawinter(co, r, (x1 + x2) / 2, x2, n + 1);
}

void
drawgraph(Code *co, Rectangle *r)
{
	int x;
	
	for(x = r->min.x; x < r->max.x; x++)
		drawinter(co, r, convx(r, x), convx(r, x + 1), 0);
}

void
drawgraphs(void)
{
	int i;
	
	color = display->black;
	for(i = 0; i < nfns; i++)
		drawgraph(&fns[i], &screen->r);
	flushimage(display, 1);
}

void
usage(void)
{
	fprint(2, "usage: fplot [-c [-s size]] [-r range] functions ...\n");
	exits("usage");
}

void
zoom(void)
{
	Mouse m;
	Rectangle r;
	double xmin_, xmax_, ymin_, ymax_;
	
	m.buttons = 7;
	r = egetrect(1, &m);
	if(r.min.x == 0 && r.min.y == 0 && r.max.x == 0 && r.max.y == 0)
		return;
	xmin_ = convx(&screen->r, r.min.x);
	xmax_ = convx(&screen->r, r.max.x);
	ymin_ = convy(&screen->r, r.max.y);
	ymax_ = convy(&screen->r, r.min.y);
	xmin = xmin_;
	xmax = xmax_;
	ymin = ymin_;
	ymax = ymax_;
	draw(screen, screen->r, display->white, nil, ZP);
	drawgraphs();
}

void
parsefns(int n, char **s)
{
	int i, max, cur;

	max = 0;
	nfns = n;
	fns = emalloc(sizeof(*fns) * n);
	for(i = 0; i < nfns; i++) {
		parse(&fns[i], s[i]);
		cur = calcstack(&fns[i]);
		if(cur > max)
			max = cur;
	}
	stack = emalloc(sizeof(*stack) * max);
}

void
parserange(char *s)
{
	while(*s && !isdigit(*s)) s++;
	if(*s == 0) return;
	xmin = strtod(s, &s);
	while(*s && !isdigit(*s)) s++;
	if(*s == 0) return;
	xmax = strtod(s, &s);
	while(*s && !isdigit(*s)) s++;
	if(*s == 0) return;
	ymin = strtod(s, &s);
	while(*s && !isdigit(*s)) s++;
	if(*s == 0) return;
	ymax = strtod(s, &s);
}

void
parsesize(char *s)
{
	while(*s && !isdigit(*s)) s++;
	if(*s == 0) return;
	picx = strtol(s, &s, 0);
	while(*s && !isdigit(*s)) s++;
	if(*s == 0) return;
	picy = strtol(s, &s, 0);
}

void
main(int argc, char **argv)
{
	Event e;
	Rectangle r;
	int i;

	ARGBEGIN {
	case 'r': parserange(EARGF(usage())); break;
	case 's': parsesize(EARGF(usage())); break;
	case 'c': cflag++; break;
	default: usage();
	} ARGEND;
	if(argc < 1)
		usage();
	setfcr(getfcr() & ~(FPZDIV | FPINVAL));
	parsefns(argc, argv);
	if(cflag) {
		imagedata = emalloc(picx * picy * 3);
		memset(imagedata, 0xFF, picx * picy * 3);
		print("%11s %11d %11d %11d %11d ", "r8g8b8", 0, 0, picx, picy);
		r.min.x = r.min.y = 0;
		r.max.x = picx;
		r.max.y = picy;
		for(i = 0; i < nfns; i++)
			drawgraph(&fns[i], &r);
		if(write(1, imagedata, picx * picy * 3) < picx * picy * 3)
			sysfatal("write: %r");
	} else {
		if(initdraw(nil, nil, "fplot") < 0)
			sysfatal("initdraw: %r");
		einit(Emouse | Ekeyboard);
		drawgraphs();
		for(;;) {
			switch(event(&e)) {
			case Ekeyboard:
				switch(e.kbdc) {
				case 'q': exits(nil);
				case 'z': zoom();
				}
			}
		}
	}
}

void
eresized(int new)
{
	if(new) {
		if(getwindow(display, Refnone) < 0)
			sysfatal("getwindow: %r");
		drawgraphs();
	}
}
