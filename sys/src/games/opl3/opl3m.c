#include <u.h>
#include <libc.h>
#include <bio.h>

void	opl3out(uchar *, int);
void	opl3wr(int, int);
void	opl3init(int);

enum{
	OPLrate = 49716,	/* 14318180Hz master clock / 288 */
};

void
usage(void)
{
	fprint(2, "usage: %s [-r rate] [file]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	int rate, n, r, v, fd, pfd[2];
	uchar sb[65536 * 4], u[5];
	double f, dt;
	Biobuf *bi;

	fd = 0;
	rate = OPLrate;
	ARGBEGIN{
	case 'r':
		rate = atoi(EARGF(usage()));
		if(rate <= 0 || rate > OPLrate)
			usage();
		break;
	default:
		usage();
	}ARGEND;
	if(*argv != nil)
		if((fd = open(*argv, OREAD)) < 0)
			sysfatal("open: %r");
	bi = Bfdopen(fd, OREAD);
	if(bi == nil)
		sysfatal("Bfdopen: %r");
	opl3init(OPLrate);
	if(pipe(pfd) < 0)
		sysfatal("pipe: %r");
	switch(rfork(RFPROC|RFFDG)){
	case -1:
		sysfatal("rfork: %r");
	case 0:
		close(0);
		close(pfd[1]);
		dup(pfd[0], 0);
		execl("/bin/audio/pcmconv", "pcmconv", "-i", "s16c2r49716", "-o", "s16c2r44100", nil);
		sysfatal("execl: %r");
	default:
		close(1);
		close(pfd[0]);
	}
	f = (double)OPLrate / rate;
	dt = 0;
	while((n = Bread(bi, u, sizeof u)) > 0){
		r = u[1] << 8 | u[0];
		v = u[2];
		opl3wr(r, v);
		dt += (u[4] << 8 | u[3]) * f;
		while((n = dt) > 0){
			if(n > sizeof sb / 4)
				n = sizeof sb / 4;
			dt -= n;
			n *= 4;
			opl3out(sb, n);
			write(pfd[1], sb, n);
		}
	}
	if(n < 0)
		sysfatal("read: %r");
	close(pfd[1]);
	waitpid();
	exits(nil);
}
