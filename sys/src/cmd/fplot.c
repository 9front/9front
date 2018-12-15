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
void omax(void) { sp--; if(sp[1]>*sp) *sp = sp[1]; }
void omin(void) { sp--; if(sp[1]<*sp) *sp = sp[1]; }
void add(void) { sp--; *sp += *(sp+1); }
void sub(void) { sp--; *sp -= *(sp+1); }
void mul(void) { sp--; *sp *= *(sp+1); }
void div(void) { sp--; *sp /= *(sp+1); }
void mod(void) { sp--; *sp = fmod(*sp, *(sp+1)); }
void pot(void) { sp--; *sp = pow(*sp, *(sp+1)); }
void osin(void) { *sp = sin(*sp); }
void ocos(void) { *sp = cos(*sp); }
void otan(void) { *sp = tan(*sp); }
void oasin(void) { *sp = asin(*sp); }
void oacos(void) { *sp = acos(*sp); }
void oatan(void) { *sp = atan(*sp); }
void osqrt(void) { *sp = sqrt(*sp); }
void oexp(void) { *sp = exp(*sp); }
void olog(void) { *sp = log10(*sp); }
void oln(void) { *sp = log(*sp); }

struct Operator {
	char *s;
	char type;
	char rassoc;
	short prec;
	void (*f)(void);
} ops[] = {
	"max",	OBINARY,	0,	0,	omax,
	"min",	OBINARY,	0,	0,	omax,
	"+",	OBINARY,	0,	100,	add,
	"-",	OBINARY,	0,	100,	sub,
	"*",	OBINARY,	0,	200,	mul,
	"/",	OBINARY,	0,	200,	div,
	"%",	OBINARY,	0,	200,	mod,
	"^",	OBINARY,	1,	300,	pot,
	"sin",	OUNARY,		0,	400,	osin,
	"cos",	OUNARY,		0,	400,	ocos,
	"tan",	OUNARY,		0,	400,	otan,
	"asin",	OUNARY,		0,	400,	oasin,
	"acos",	OUNARY,		0,	400,	oacos,
	"atan",	OUNARY,		0,	400,	oatan,
	"sqrt",	OUNARY,		0,	400,	osqrt,
	"exp",	OUNARY,		0,	400,	oexp,
	"log",	OUNARY,		0,	400,	olog,
	"ln",	OUNARY,		0,	400,	oln,
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
double gymin, gymax;
int icolors[] = {
	DBlack,
	0xCC0000FF,
	0x00CC00FF,
	0x0000CCFF,
	0xFF00CCFF,
	0xFFAA00FF,
	0xCCCC00FF,
};
Image *colors[nelem(icolors)];
int cflag, aflag;
char *imagedata;
char *pixels;
int picx = 640, picy = 480;

typedef struct FRectangle FRectangle;
struct FRectangle {
	double xmin, xmax, ymin, ymax;
} *zoomst;
int nzoomst;

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
					!t->op->rassoc && opstackbot->op->prec == t->op->prec))
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
	if(*sp < gymin) gymin = *sp;
	if(*sp > gymax) gymax = *sp;
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
pixel(int x, int y, int c)
{
	char *p;

	if(cflag) {
		if(x >= picx || y >= picy || x < 0 || y < 0)
			return;
		p = imagedata + (picx * y + x) * 3;
		p[0] = icolors[c] >> 24;
		p[1] = icolors[c] >> 16;
		p[2] = icolors[c] >> 8;
	}else{
		draw(screen, Rect(x, y, x + 1, y + 1), colors[c], nil, ZP);
		if(ptinrect(Pt(x, y), screen->r))
			pixels[picx * (y - screen->r.min.y) + (x - screen->r.min.x)] = 1;
	}
}

void
drawinter(Code *co, Rectangle *r, double x1, double x2, int n, int c)
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
		pixel(ix1, iy1, c);
	}
	y2 = calc(co, x2);
	if(!isNaN(y2)) {
		iy2 = deconvy(r, y2);
		pixel(ix2, iy2, c);
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
	drawinter(co, r, x1, (x1 + x2) / 2, n + 1, c);
	drawinter(co, r, (x1 + x2) / 2, x2, n + 1, c);
}

void
drawgraph(Code *co, Rectangle *r, int c)
{
	int x;
	
	for(x = r->min.x; x < r->max.x; x++)
		drawinter(co, r, convx(r, x), convx(r, x + 1), 0, c);
}

void
tickfmt(double d, double m, int n, char *fmt)
{
	double e1, e2;
	int x;
	
	e1 = log10(fabs(m));
	e2 = log10(fabs(m + n * d));
	if(e2 > e1) e1 = e2;
	if(e2 >= 4 || e2 < -3){
		x = ceil(e1-log10(d)-1);
		snprint(fmt, 32, "%%.%de", x);
	}else{
		x = ceil(-log10(d));
		snprint(fmt, 32, "%%.%df", x);
	}
}

int
xticklabel(char *fmt, double g, int p, int x, int y)
{
	char buf[32];
	Rectangle lr;
	int ny;

	snprint(buf, sizeof(buf), fmt, g);
	lr.min = Pt(p, y+2);
	lr.max = addpt(lr.min, stringsize(display->defaultfont, buf));
	lr = rectsubpt(lr, Pt(Dx(lr) / 2-1, 0));
	if(lr.max.y >= screen->r.max.y){
		ny = y - 7 - Dy(lr);
		lr = rectsubpt(lr, Pt(0, lr.min.y - ny));
	}
	if(rectinrect(lr, screen->r) && (lr.min.x > x || lr.max.x <= x)){
		string(screen, lr.min, display->black, ZP, display->defaultfont, buf);
		return 1;
	}
	return 0;
}

int
yticklabel(char *fmt, double g, int p, int x, int y)
{
	char buf[32];
	Rectangle lr;
	int nx;

	snprint(buf, sizeof(buf), fmt, g);
	lr.min = Pt(0, 0);
	lr.max = stringsize(display->defaultfont, buf);
	lr = rectaddpt(lr, Pt(x-Dx(lr)-2, p - Dy(lr) / 2));
	if(lr.min.x < screen->r.min.x){
		nx = x + 7;
		lr = rectsubpt(lr, Pt(lr.min.x - nx, 0));
	}
	if(rectinrect(lr, screen->r) && (lr.min.y > y || lr.max.y <= y)){
		string(screen, lr.min, display->black, ZP, display->defaultfont, buf);
		return 1;
	}
	return 0;
}

int
calcm(double min, double max, int e, double *dp, double *mp)
{
	double d, m, r;

	d = pow(10, e>>1);
	if((e & 1) != 0) d *= 5;
	m = min;
	if(min < 0 && max > 0)
		m += fmod(-m, d);
	else{
		r = fmod(m, d);
		if(r < 0)
			m -= r;
		else
			m += d - r;
	}
	if(dp != nil) *dp = d;
	if(mp != nil) *mp = m;
	return (max-m)*0.999/d;
}

int
ticks(double min, double max, double *dp, double *mp)
{
	int e, n;
	double m;
	int beste;
	double bestm;
	
	e = 2 * ceil(log10(max - min));
	beste = 0;
	bestm = Inf(1);
	for(;e>-100;e--){
		n = calcm(min, max, e, nil, nil);
		if(n <= 0) continue;
		if(n < 10) m = 10.0 / n;
		else m = n / 10.0;
		if(m < bestm){
			beste = e;
			bestm = m;
		}
		if(n > 10) break;
	}
	calcm(min, max, beste, dp, mp);
	return (max - *mp) / *dp;
}

void
drawaxes(void)
{
	int x, y, p;
	double dx, dy, mx, my;
	int nx, ny;
	int i;
	char fmt[32];

	if(xmin < 0 && xmax > 0)
		x = deconvx(&screen->r, 0);
	else
		x = screen->r.min.x+5;
	line(screen, Pt(x, screen->r.min.y), Pt(x, screen->r.max.y), Endarrow, 0, 0, display->black, ZP);
	if(ymin < 0 && ymax > 0)
		y = deconvy(&screen->r, 0);
	else
		y = screen->r.max.y-5;
	line(screen, Pt(screen->r.min.x, y), Pt(screen->r.max.x, y), 0, Endarrow, 0, display->black, ZP);
	nx = ticks(xmin, xmax, &dx, &mx);
	tickfmt(dx, mx, nx, fmt);
	for(i = 0; i <= nx; i++){
		p = deconvx(&screen->r, dx*i+mx);
		if(xticklabel(fmt, dx*i+mx, p, x, y))
			line(screen, Pt(p, y), Pt(p, y-5), 0, 0, 0, display->black, ZP);
	}
	ny = ticks(ymin, ymax, &dy, &my);
	tickfmt(dy, my, ny, fmt);
	for(i = 0; i <= ny; i++){
		p = deconvy(&screen->r, dy*i+my);
		if(yticklabel(fmt, dy*i+my, p, x, y))
			line(screen, Pt(x, p), Pt(x+5, p), 0, 0, 0, display->black, ZP);
	}
}

void
drawgraphs(void)
{
	int i;

	gymin = Inf(1);
	gymax = Inf(-1);
	memset(pixels, 0, picx * picy);
	for(i = 0; i < nfns; i++)
		drawgraph(&fns[i], &screen->r, i % nelem(icolors));
	if(!aflag)
		drawaxes();
	flushimage(display, 1);
}

void
usage(void)
{
	fprint(2, "usage: fplot [-a] [-c [-s size]] [-r range] functions ...\n");
	exits("usage");
}

void
zoom(void)
{
	Mouse m;
	Rectangle r;
	double xmin_, xmax_, ymin_, ymax_;
	
	m.buttons = 0;
	r = egetrect(1, &m);
	if(Dx(r) < 1 || Dy(r) < 1)
		return;
	zoomst = realloc(zoomst, sizeof(FRectangle) * (nzoomst + 1));
	if(zoomst == nil) sysfatal("realloc: %r");
	zoomst[nzoomst++] = (FRectangle){xmin, xmax, ymin, ymax};
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
unzoom(void)
{
	if(nzoomst == 0) return;
	xmin = zoomst[nzoomst - 1].xmin;
	xmax = zoomst[nzoomst - 1].xmax;
	ymin = zoomst[nzoomst - 1].ymin;
	ymax = zoomst[nzoomst - 1].ymax;
	zoomst = realloc(zoomst, sizeof(FRectangle) * --nzoomst);
	if(zoomst == nil && nzoomst != 0) sysfatal("realloc: %r");
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
	while(*s && !isdigit(*s) && *s != '-') s++;
	if(*s == 0) return;
	xmin = strtod(s, &s);
	while(*s && !isdigit(*s) && *s != '-') s++;
	if(*s == 0) return;
	xmax = strtod(s, &s);
	while(*s && !isdigit(*s) && *s != '-') s++;
	if(*s == 0) return;
	ymin = strtod(s, &s);
	while(*s && !isdigit(*s) && *s != '-') s++;
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
alloccolors(void)
{
	int i;
	
	for(i = 0; i < nelem(icolors); i++){
		freeimage(colors[i]);
		colors[i] = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, icolors[i]);
	}
}

void
readout(Point p)
{
	int i, j;
	double x, y;
	vlong d, best;
	Point bestp;
	double ny, besty;
	char buf[64];

	/*TODO: do something more intelligent*/
	best = (uvlong)(-1)>>1;
	for(j = screen->r.min.y; j < screen->r.max.y; j++)
		for(i = screen->r.min.x; i < screen->r.max.x; i++){
			if(!pixels[(j - screen->r.min.y) * picx + (i - screen->r.min.x)]) continue;
			d = (i - p.x) * (i - p.x) + (j - p.y) * (j - p.y);
			if(d < best){
				best = d;
				bestp = Pt(i, j);
			}
		}
	ellipse(screen, bestp, 3, 3, 0, display->black, ZP);
	x = convx(&screen->r, bestp.x);
	y = convy(&screen->r, bestp.y);
	besty = calc(&fns[0], x);
	for(i = 1; i < nfns; i++){
		ny = calc(&fns[i], x);
		if(abs(ny - y) < abs(besty - y))
			besty = ny;
	}
	snprint(buf, sizeof(buf), "%#.4g %#.4g", x, besty);
	string(screen, addpt(Pt(10, 10), screen->r.min), display->black, ZP, display->defaultfont, buf);
}

void
main(int argc, char **argv)
{
	Event e;
	Rectangle r;
	int i;
	static int lbut;

	ARGBEGIN {
	case 'a': aflag++; break;
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
			drawgraph(&fns[i], &r, i % nelem(icolors));
		if(write(1, imagedata, picx * picy * 3) < picx * picy * 3)
			sysfatal("write: %r");
	} else {
		if(initdraw(nil, nil, "fplot") < 0)
			sysfatal("initdraw: %r");
		einit(Emouse | Ekeyboard);
		picx = Dx(screen->r);
		picy = Dy(screen->r);
		pixels = emalloc(picx * picy);
		alloccolors();
		drawgraphs();
		for(;;) {
			switch(event(&e)) {
			case Emouse:
				if((e.mouse.buttons & 1) != 0)
					zoom();
				if(((lbut|e.mouse.buttons) & 2) != 0){
					draw(screen, screen->r, display->white, nil, ZP);
					drawgraphs();
				}
				if((e.mouse.buttons & 2) != 0)
					readout(e.mouse.xy);
				if((~e.mouse.buttons & lbut & 4) != 0)
					unzoom();
				lbut = e.mouse.buttons;
				break;
			case Ekeyboard:
				switch(e.kbdc) {
				case 'q': case 127: exits(nil); break;
				case 'y':
					if(!isInf(ymin, 1) && !isInf(ymax, -1)){
						zoomst = realloc(zoomst, sizeof(FRectangle) * (nzoomst + 1));
						if(zoomst == nil) sysfatal("realloc: %r");
						zoomst[nzoomst++] = (FRectangle){xmin, xmax, ymin, ymax};
						ymin = gymin-0.05*(gymax-gymin);
						ymax = gymax+0.05*(gymax-gymin);
						draw(screen, screen->r, display->white, nil, ZP);
						drawgraphs();
					}
					break;
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
		picx = Dx(screen->r);
		picy = Dy(screen->r);
		pixels = realloc(pixels, picx * picy);
		if(pixels == nil) sysfatal("realloc: %r");
		alloccolors();
		drawgraphs();
	}
}
