/*
 * tee-- pipe fitting
 */

#include <u.h>
#include <libc.h>

enum {
	FDSTART = 3,
};

int	aflag;

char in[IOUNIT];

int	intignore(void*, char*);

void
main(int argc, char **argv)
{
	int i;
	int r, n;

	ARGBEGIN {
	case 'a':
		aflag++;
		break;

	case 'i':
		atnotify(intignore, 1);
		break;

	case 'u':
		/* uflag is ignored and undocumented; it's a relic from Unix */
		break;

	default:
		fprint(2, "usage: tee [-ai] [file ...]\n");
		exits("usage");
	} ARGEND

	USED(argc);
	n = 0;
	while(*argv) {
		if(aflag) {
			i = open(argv[0], OWRITE);
			if(i < 0)
				i = create(argv[0], OWRITE, 0666);
			seek(i, 0L, 2);
		} else
			i = create(argv[0], OWRITE, 0666);
		if(i < 0) {
			fprint(2, "tee: cannot open %s: %r\n", argv[0]);
		} else {
			if(i != n+FDSTART)
				dup(i, n+FDSTART);
			n++;
		}
		argv++;
	}

	for(;;) {
		r = read(0, in, sizeof in);
		if(r <= 0)
			exits(nil);
		for(i=0; i<n; i++)
			write(i+FDSTART, in, r);
		write(1, in, r);
	}
}

int
intignore(void *a, char *msg)
{
	USED(a);
	if(strcmp(msg, "interrupt") == 0)
		return 1;
	return 0;
}
