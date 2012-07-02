#include <u.h>
#include <libc.h>

int	multi;
int	nlines;
vlong	nchars;
char	*status = nil;

int
line(int fd, char *file)
{
	char c;
	int m, n, nalloc;
	char *buf;

	nalloc = 0;
	buf = nil;
	for(m=0; ; ){
		n = read(fd, &c, 1);
		if(n < 0){
			fprint(2, "read: error reading %s: %r\n", file);
			exits("read error");
		}
		if(n == 0){
			if(m == 0)
				status = "eof";
			break;
		}
		if(m == nalloc){
			nalloc += 1024;
			buf = realloc(buf, nalloc);
			if(buf == nil){
				fprint(2, "read: malloc error: %r\n");
				exits("malloc");
			}
		}
		buf[m++] = c;
		if(c == '\n')
			break;
	}
	if(m > 0)
		write(1, buf, m);
	free(buf);
	return m;
}

void
lines(int fd, char *file)
{
	do{
		if(line(fd, file) == 0)
			break;
	}while(multi || --nlines>0);
}

void
chars(int fd, char *file)
{
	char buf[8*1024];
	vlong m;
	int n;

	for(m = 0; m < nchars; m += n){
		n = sizeof(buf);
		if(n > (nchars - m))
			n = nchars - m;
		if((n = read(fd, buf, n)) < 0){
			fprint(2, "read: error reading %s: %r\n", file);
			exits("read error");
		}
		if(n == 0){
			if(m == 0)
				status = "eof";
			break;
		}
		write(1, buf, n);
	}
}

void
usage(void)
{
	fprint(2, "usage: read [-m] [-n nlines] [-c nbytes] [files...]\n");
	exits("usage");
}

void
main(int argc, char *argv[])
{
	void (*proc)(int, char*);
	int i, fd;

	proc = lines;
	ARGBEGIN{
	case 'c':
		nchars = atoll(EARGF(usage()));
		proc = chars;
		break;
	case 'n':
		nlines = atoi(EARGF(usage()));
		break;
	case 'm':
		multi = 1;
		break;
	default:
		usage();
	}ARGEND

	if(argc == 0)
		(*proc)(0, "<stdin>");
	else
		for(i=0; i<argc; i++){
			fd = open(argv[i], OREAD);
			if(fd < 0){
				fprint(2, "read: can't open %s: %r\n", argv[i]);
				exits("open");
			}
			(*proc)(fd, argv[i]);
			close(fd);
		}

	exits(status);
}
