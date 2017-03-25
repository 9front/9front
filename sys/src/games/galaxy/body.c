#include <u.h>
#include <libc.h>
#include <draw.h>
#include <bio.h>
#include "galaxy.h"

void
glxyinit(void)
{

	glxy.a = calloc(5, sizeof(Body));
	if(glxy.a == nil)
		sysfatal("could not allocate glxy: %r");
	glxy.nb = 0;
	glxy.max = 5;
}

Body*
body(void)
{
	Body *b;

	if(glxy.nb == glxy.max) {
		glxy.max *= 2;
		glxy.a = realloc(glxy.a, sizeof(Body) * glxy.max);
		if(glxy.a == nil)
			sysfatal("could not realloc glxy: %r");
	}
	b = glxy.a + glxy.nb++;
	*b = ZB;
	return b;
}

Point
topoint(Vector v)
{
	Point p;

	p.x = v.x/scale + orig.x;
	p.y = v.y/scale + orig.y;
	return p;
}

void
drawbody(Body *b)
{
	Point pos, v;
	int s;

	pos = topoint(b->Vector);
	s = b->size/scale;
	fillellipse(screen, pos, s, s, b->col, ZP);
	v.x = b->v.x/scale*10;
	v.y = b->v.y/scale*10;
	if(v.x != 0 || v.y != 0)
		line(screen, pos, addpt(pos, v), Enddisc, Endarrow, 0, b->col, ZP);
	flushimage(display, 1);
}

Vector
center(void)
{
	Body *b;
	Vector gc, gcv;
	double mass;

	if(glxy.nb == 0)
		return (Vector){0, 0};

	gc.x = gc.y = gcv.x = gcv.y = mass = 0;
	for(b = glxy.a; b < glxy.a+glxy.nb; b++) {
		gc.x += b->x * b->mass;
		gc.y += b->y * b->mass;
		gcv.x += b->v.x * b->mass;
		gcv.y += b->v.y * b->mass;
		mass += b->mass;
	}
	gc.x /= mass;
	gc.y /= mass;
	gcv.x /= mass;
	gcv.y /= mass;
	for(b = glxy.a; b < glxy.a+glxy.nb; b++) {
		b->x -= gc.x;
		b->y -= gc.y;
		b->v.x -= gcv.x;
		b->v.y -= gcv.y;
	}
	return gc;
}

int
Bfmt(Fmt *f)
{
	Body *b;
	int r;

	b = va_arg(f->args, Body*);

	r = fmtprint(f, "MKBODY %g %g ", b->x, b->y);
	if(r < 0)
		return -1;

	r = fmtprint(f, "%g %g ", b->v.x, b->v.y);
	if(r < 0)
		return -1;

	return fmtprint(f, "%g", b->size);
}

enum {
	MKBODY,
	ORIG,
	DT,
	SCALE,
	GRAV,
	NOCMD,
};

int
getcmd(char *l)
{
	static char *cmds[] = {
		[MKBODY]	"MKBODY",
		[ORIG]	"ORIG",
		[DT]	"DT",
		[SCALE]	"SCALE",
		[GRAV]	"GRAV",
	};
	int cmd;

	for(cmd = 0; cmd < nelem(cmds); cmd++) {
		if(strcmp(l, cmds[cmd]) == 0)
			return cmd;
	}
	sysfatal("getcmd: no such command %s", l);
	return NOCMD;
}

void
readglxy(int fd)
{
	static Biobuf bin;
	char *line;
	double f;
	int cmd, len;
	Body *b;

	glxy.nb = 0;
	Binit(&bin, fd, OREAD);
	for(;;) {
		line = Brdline(&bin, ' ');
		len = Blinelen(&bin);
		if(line == nil) {
			if(len == 0)
				break;
			sysfatal("load: malformed command");
		}

		line[len-1] = '\0';
		cmd = getcmd(line);

		line = Brdline(&bin, '\n');
		if(line == nil) {
			if(len == 0)
				sysfatal("load: malformed command");
			sysfatal("load: read error: %r");
		}
		len = Blinelen(&bin);
		line[len-1] = '\0';

		switch(cmd) {
		case MKBODY:
			b = body();
			b->x = strtod(line, &line);
			b->y = strtod(line, &line);
			b->v.x = strtod(line, &line);
			b->v.y = strtod(line, &line);
			b->size = strtod(line, nil);
			b->mass = b->size*b->size*b->size;
			b->col = randcol();
			CHECKLIM(b, f);
			break;
		case ORIG:
			orig.x = strtol(line, &line, 10);
			orig.y = strtol(line, nil, 10);
			break;
		case DT:
			dt = strtod(line, nil);
			dt² = dt*dt;
			break;
		case SCALE:
			scale = strtod(line, nil);
			break;
		case GRAV:
			G = strtod(line, nil);
			break;
		}
	}
	Bterm(&bin);
}

void
writeglxy(int fd)
{
	static Biobuf bout;
	Body *b;

	Binit(&bout, fd, OWRITE);
	
	Bprint(&bout, "ORIG %d %d\n", orig.x, orig.y);
	Bprint(&bout, "SCALE %g\n", scale);
	Bprint(&bout, "DT %g\n", dt);
	Bprint(&bout, "GRAV %g\n", G);

	for(b = glxy.a; b < glxy.a + glxy.nb; b++)
		Bprint(&bout, "%B\n", b);

	Bterm(&bout);
}
