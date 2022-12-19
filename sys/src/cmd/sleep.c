#include <u.h>
#include <libc.h>

void
main(int argc, char *argv[])
{
	enum { MAXSEC = 0x7ffffc00/1000 };
	long n, m;
	char *p, *q;

	if(argc>1){
		for(n = strtol(argv[1], &p, 0); n > MAXSEC; n -= MAXSEC)
			sleep(MAXSEC*1000);
		/*
		 * no floating point because it is useful to
		 * be able to run sleep when bootstrapping
		 * a machine.
		 */
		if(*p++ == '.' && (m = strtol(p, &q, 10)) > 0){
			switch(q - p){
			case 0:
				break;
			case 1:
				m *= 100;
				break;
			case 2:
				m *= 10;
				break;
			default:
				p[3] = 0;
				m = strtol(p, 0, 10);
				break;
			}
		} else {
			m = 0;
		}
		m += n*1000;
		if(m > 0)
			sleep(m);
	}
	exits(0);
}
