#include <u.h>
#include <libc.h>
#include <bio.h>
#include "midifile.h"

/* some devices want delta-time in a certain format, others want it zero
 * not sure why yet */
static int magic;

void
samp(double n)
{
	double Δt;
	long s;
	static double t0;

	if(t0 == 0.0)
		t0 = nsec();
	t0 += n * 1000 * tempo / div;
	Δt = t0 - nsec();
	s = floor(Δt / 1000000);
	if(s > 0)
		sleep(s);
}

/* set delay to 0, and translate running status: the receiver
 * only sees one track whereas running status is a property of
 * each track (stream); don't send EOT for the same reason */
void
event(Track *t)
{
	int e, n;
	uchar u[16], *cur, *q;
	Msg msg;

	q = u + 1;
	e = nextev(t);
	*q++ = e;
	cur = t->cur;
	translate(t, e, &msg);
	if(msg.type == Ceot)
		return;
	u[0] = magic ? e >> 4 | (e & 0xf) << 4 : 0;
	n = t->cur - cur;
	if(msg.type == Csysex || n > nelem(u) - (q - u)){
		write(1, u, q - u);
		write(1, cur, n);
	}else{
		memcpy(q, cur, n);
		write(1, u, n + (q - u));
	}
}

void
usage(void)
{
	fprint(2, "usage: %s [-Dm] [mid]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	uchar eot[] = {0x00, 0xff, 0x2f, 0x00};

	ARGBEGIN{
	case 'D': trace = 1; break;
	case 'm': magic = 1; break;
	default: usage();
	}ARGEND
	if(readmid(*argv) < 0)
		sysfatal("readmid: %r");
	evloop();
	write(1, eot, sizeof eot);
	exits(nil);
}
