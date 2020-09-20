#include <u.h>
#include <libc.h>
#include <fcall.h>

char	buf[1048576];
enum{ NARG = 5 };
uintptr	arg[NARG];

/* system calls not defined in libc.h */
int	sysr1(void);
int	_stat(char*, char*);
int	_fstat(int, char*);
int	_errstr(char*);
int	_wstat(char*, char*);
int	_fwstat(int, char*);
int	_read(int, void*, int);
int	_write(int, void*, int);
int	_read9p(int, void*, int);
int	_write9p(int, void*, int);
int	brk_(void*);
int	_nfstat(int, void*, int);
int	_nstat(char*, void*, int);
int	_nfwstat(int, void*, int);
int	_nwstat(char*, void*, int);
int	_fsession(char*, void*, int);
int	_mount(int, char*, int, char*);
int	_wait(void*);
int	_nsec(vlong*);

struct Call{
	char	*name;
	int	(*func)(...);
};
#include "tab.h"

void
usage(void)
{
	fprint(2, "usage: %s [-os] entry [arg ...]\n", argv0);
	exits("usage");
}

uintptr
parse(char *s)
{
	char *t;
	uintptr l;

	if(strncmp(s, "buf", 3) == 0)
		return (uintptr)buf;
	
	l = strtoull(s, &t, 0);
	if(t > s && *t == 0)
		return l;

	return (uintptr)s; 
}

void
catch(void *, char *msg)
{
	fprint(2, "syscall: received note: %s\n", msg);
	noted(NDFLT);
}

void
main(int argc, char *argv[])
{
	int i;
	int oflag, sflag;
	vlong r, nbuf;
	Dir d;
	char strs[1024];
	char ebuf[ERRMAX];

	fmtinstall('D', dirfmt);

	oflag = 0;
	sflag = 0;
	ARGBEGIN{
	case 'o':
		oflag++;
		break;
	case 's':
		sflag++;
		break;
	default:
		usage();
	}ARGEND
	if(argc < 1 || argc > 1+NARG)
		usage();

	for(i = 1; i < argc; i++)
		arg[i-1] = parse(argv[i]);
	for(i = 0; tab[i].name; i++)
		if(strcmp(tab[i].name, argv[0]) == 0)
			break;
	if(i == NTAB){
		fprint(2, "syscall: %s not known\n", argv[0]);
		exits("unknown");
	}
	notify(catch);
	/* special case for seek, pread, pwrite; vlongs are problematic */
	switch(i){
	default:
		r = (*tab[i].func)(arg[0], arg[1], arg[2], arg[3], arg[4]);
		break;
	case SEEK:
		r = seek(arg[0], strtoll(argv[2], 0, 0), arg[2]);
		break;
	case PREAD:
		r = pread(arg[0], (void*)arg[1], arg[2], strtoll(argv[4], 0, 0));
		break;
	case PWRITE:
		r = pwrite(arg[0], (void*)arg[1], arg[2], strtoll(argv[4], 0, 0));
		break;
	}
	if(r == -1){
		errstr(ebuf, sizeof ebuf);
		fprint(2, "syscall: return: %lld error: %s\n", r, ebuf);
		exits(ebuf);
	}
	fprint(2, "syscall: return: %lld\n", r);
	if(oflag){
		nbuf = r;
		switch(i){
		case _ERRSTR: case ERRSTR: case FD2PATH:
			nbuf = strlen(buf);
		}
		if(write(1, buf, nbuf) != nbuf)
			sysfatal("write: %r");
	}else if(sflag){
		r = convM2D((uchar*)buf, r, &d, strs);
		if(r <= BIT16SZ)
			print("short stat message\n");
		else
			print("%D\n", &d);
	}
	exits(nil);
}
