#include <u.h>
#include <libc.h>
#include <bio.h>

void	opl3out(uchar *, int);
void	opl3wr(int, int);
void	opl3init(int);

void
usage(void)
{
	fprint(2, "usage: %s [-r rate] [file]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	int rate, r, v, dt, fd;
	uchar sb[65536 * 4], u[5];
	Biobuf *bi;

	fd = 0;
	rate = 44100;
	ARGBEGIN{
	case 'r':
		rate = atoi(EARGF(usage()));
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
	opl3init(rate);
	while(Bread(bi, u, sizeof u) > 0){
		r = u[1] << 8 | u[0];
		v = u[2];
		opl3wr(r, v);
		if(dt = (u[4] << 8 | u[3]) * 4){	/* 16-bit stereo */
			opl3out(sb, dt);
			write(1, sb, dt);
		}
	}
}
