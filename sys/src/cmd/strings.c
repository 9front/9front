#include	<u.h>
#include 	<libc.h>
#include	<bio.h>

Biobuf	fin;
Biobuf	fout;

void stringit(int);
int isprint(Rune);

int minspan = 6;	/* Min characters in string (default) */
Rune *span;

static void
usage(void)
{
	fprint(2, "usage: %s [-m min] [file...]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	int i, fd;

	ARGBEGIN{
	case 'm':
		minspan = atoi(EARGF(usage()));
		if(minspan <= 0)
			usage();
		break;
	default:
		usage();
		break;
	}ARGEND

	span = malloc(sizeof(Rune)*(minspan+1));
	if(span == nil)
		sysfatal("out of memory");

	Binit(&fout, 1, OWRITE);
	if(argc < 1) {
		stringit(0);
		exits(0);
	}

	for(i = 0; i < argc; i++) {
		if(argc > 1){
			Bprint(&fout, "%s:\n", argv[i]);
			Bflush(&fout);
		}
		if((fd = open(argv[i], OREAD)) < 0){
			perror("open");
			continue;
		}
		stringit(fd);
		close(fd);
	}

	exits(0);
}

void
stringit(int fd)
{
	Rune *sp;
	long c;

	Binit(&fin, fd, OREAD);
	sp = span;
	while((c = Bgetrune(&fin)) >= 0) {
		if(isprint(c)) {
			if(sp == nil){
				Bputrune(&fout, c);
				continue;
			}
			*sp++ = c;
			if((sp-span) < minspan)
				continue;
			*sp = 0;
			Bprint(&fout, "%8lld: %S", Boffset(&fin)-minspan, span);
			sp = nil;
		} else {
			if(sp == nil)
				Bputrune(&fout, '\n');
			sp = span;
		}
	}
	if(sp == nil)
		Bputrune(&fout, '\n');
	Bterm(&fin);
}

int
isprint(Rune r)
{
	if (r != Runeerror)
	if ((r >= ' ' && r < 0x7F) || r > 0xA0)
		return 1;
	return 0;
}
