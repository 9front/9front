#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include "galaxy.h"

int extraproc = -1, throttle;

static QLock *golock;
static Rendez *gorend;
static int *go;

static QLock runninglock;
static Rendez runningrend;
static int running;

static void
calcproc(void *v)
{
	Body *b, *e;
	int nbody;
	int pid;

	pid = (uintptr)v;

	for(;;) {
		qlock(golock+pid);
		while(!go[pid])
			rsleep(gorend+pid);
		go[pid] = 0;
		qunlock(golock+pid);

		nbody = glxy.nb / (extraproc+1);
		b = glxy.a + nbody * pid;
		e = b + nbody;
		while(b < e) {
			b->a.x = b->newa.x;
			b->a.y = b->newa.y;
			b->newa.x = b->newa.y = 0;
			quadcalc(b++, space, LIM);
		}	

		qlock(&runninglock);
		if(--running == 0)
			rwakeup(&runningrend);
		qunlock(&runninglock);
	}
}

static void
startprocs(void)
{
	int pid;

	golock = calloc(extraproc, sizeof(*golock));
	if(golock == nil)
		sysfatal("Could not create go locks: %r\n");

	gorend = calloc(extraproc, sizeof(*gorend));
	if(gorend == nil)
		sysfatal("Could not create go rendez: %r\n");

	go = calloc(extraproc, sizeof(*go));
	if(go == nil)
		sysfatal("Could not create go flags: %r\n");

	for(pid = 0; pid < extraproc; pid++)
		gorend[pid].l = golock+pid;
	runningrend.l = &runninglock;

	for(pid = 0; pid < extraproc; pid++) 
		proccreate(calcproc, (void*)pid, STK);
}

/* verlet barnes-hut */
void
simulate(void*)
{
	Body *b, *s;
	int nbody, pid;
	double f;

	threadsetname("simulate");

	startprocs();

	for(;;) {
		qlock(&glxy);

		if(throttle)
			sleep(throttle);

		drawglxy();

Again:
		space.t = EMPTY;
		quads.l = 0;
		STATS(quaddepth = 0;)
		for(b = glxy.a; b < glxy.a + glxy.nb; b++) {
			if(quadins(b, LIM) == -1) {
				growquads();
				goto Again;
			}
		}

		running = extraproc;
		for(pid = 0; pid < extraproc; pid++) {
			qlock(golock+pid);
			go[pid] = 1;
			rwakeup(gorend+pid);
			qunlock(golock+pid);
		}

		STATS(avgcalcs = 0;)
		nbody = glxy.nb / (extraproc+1);
		s = glxy.a + nbody * (extraproc);
		for(b = s; b < glxy.a + glxy.nb; b++) {
			b->a.x = b->newa.x;
			b->a.y = b->newa.y;
			b->newa.x = b->newa.y = 0;
			STATS(calcs = 0;)
			quadcalc(b, space, LIM);
			STATS(avgcalcs += calcs;)
		}
		STATS(avgcalcs /= glxy.nb;)

		qlock(&runninglock);
		while(running > 0)
			rsleep(&runningrend);
		qunlock(&runninglock);

		for(b = glxy.a; b < glxy.a + glxy.nb; b++) {
			b->x += dt*b->v.x + dt²*b->a.x/2;
			b->y += dt*b->v.y + dt²*b->a.y/2;
			b->v.x += dt*(b->a.x + b->newa.x)/2;
			b->v.y += dt*(b->a.y + b->newa.y)/2;
			CHECKLIM(b, f);
		}

		qunlock(&glxy);
	}
}
