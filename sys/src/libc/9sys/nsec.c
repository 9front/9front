#include <u.h>
#include <libc.h>
#include <tos.h>

static uvlong order = 0x0001020304050607ULL;

static void
be2vlong(vlong *to, uchar *f)
{
	uchar *t, *o;
	int i;

	t = (uchar*)to;
	o = (uchar*)&order;
	for(i = 0; i < sizeof order; i++)
		t[o[i]] = f[i];
}

static int
stillopen(int fd, char *name)
{
	char buf[64];

	return fd >= 0 && fd2path(fd, buf, sizeof(buf)) == 0 && strcmp(buf, name) == 0;
}

vlong
nsec(void)
{
	static char name[] = "/dev/bintime";
	static int *pidp = nil, *fdp = nil, fd = -1;
	uchar b[8];
	vlong t;
	int f;

	if(pidp != nil && *pidp == _tos->pid)
		f = *fdp;
	else{
Reopen:
		f = fd;
		if(fdp != nil && *fdp != f && stillopen(*fdp, name))
			f = *fdp;
		else if(!stillopen(f, name)){
			if((f = open(name, OREAD|OCEXEC)) < 0)
				return 0;
		}
		fd = f;
		if(fdp == nil){
			fdp = (int*)privalloc();
			pidp = (int*)privalloc();
		}
		*fdp = f;
		*pidp = _tos->pid;
	}
	if(pread(f, b, sizeof b, 0) != sizeof b){
		if(!stillopen(f, name))
			goto Reopen;
		close(f);
		return 0;
	}
	be2vlong(&t, b);
	return t;
}
