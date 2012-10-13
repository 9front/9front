#include <u.h>
#include <libc.h>
#include <bio.h>

Biobuf	bin;
Biobuf	bout;
int	dflag;

char	hex[] = "0123456789abcdef";
char	Hex[] = "0123456789ABCDEF";

int
hexdigit(int c)
{
	char *p;

	if(c > 0){
		if((p = strchr(Hex, c)) != 0)
			return p - Hex;
		if((p = strchr(hex, c)) != 0)
			return p - hex;
	}
	return -1;
}

void
usage(void)
{
	fprint(2, "Usage: %s [ -d ] [ file ]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	int c;

	ARGBEGIN {
	case 'd':
		dflag = 1;
		break;
	default:
		usage();
	} ARGEND;

	if(argc == 1){
		int fd;

		fd = open(*argv, OREAD);
		if(fd < 0)
			sysfatal("%r");
		if(fd != 0) dup(fd, 0);
	} else if(argc > 1)
		usage();

	Binit(&bin, 0, OREAD);
	Binit(&bout, 1, OWRITE);

	if(dflag){
		while((c = Bgetc(&bin)) >= 0){
			if(c == '%'){
				int c1, c2, x1, x2;

				if((c1 = Bgetc(&bin)) < 0)
					break;
				if((x1 = hexdigit(c1)) < 0){
					Bungetc(&bin);
					Bputc(&bout, c);
					continue;
				}
				if((c2 = Bgetc(&bin)) < 0)
					break;
				if((x2 = hexdigit(c2)) < 0){
					Bungetc(&bin);
					Bputc(&bout, c);
					Bputc(&bout, c1);
					continue;
				}
				c = x1<<4 | x2;
			} else if(c == '+')
				c = ' ';
			Bputc(&bout, c);
		}
	} else {
		while((c = Bgetc(&bin)) >= 0){
			if(c>0 && strchr("/$-_@.!*'(),", c)
			|| 'a'<=c && c<='z'
			|| 'A'<=c && c<='Z'
			|| '0'<=c && c<='9')
				Bputc(&bout, c);
			else if(c == ' ')
				Bputc(&bout, '+');
			else {
				Bputc(&bout, '%');
				Bputc(&bout, Hex[c>>4]);
				Bputc(&bout, Hex[c&15]);
			}
		}
	}

	Bflush(&bout);
	exits(0);
}
