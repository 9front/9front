#include <u.h>
#include <libc.h>
#include <tos.h>

static int
stillopen(int fd, char *name)
{
	char buf[64];

	return fd >= 0 && fd2path(fd, buf, sizeof(buf)) == 0 && strcmp(buf, name) == 0;
}

static int
readbintime(void *buf, int n)
{
	static char name[] = "/dev/bintime";
	static int *pidp = nil, *fdp = nil, fd = -1;
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
				return -1;
		}
		fd = f;
		if(fdp == nil){
			fdp = (int*)privalloc();
			pidp = (int*)privalloc();
		}
		*fdp = f;
		*pidp = _tos->pid;
	}
	if(pread(f, buf, n, 0) != n){
		if(!stillopen(f, name))
			goto Reopen;
		close(f);
		return -1;
	}
	return 0;
}

static vlong
gb64(uchar *p)
{
	return	(vlong)p[0] << 56 |
		(vlong)p[1] << 48 |
		(vlong)p[2] << 40 |
		(vlong)p[3] << 32 |
		(vlong)p[4] << 24 |
		(vlong)p[5] << 16 |
		(vlong)p[6] <<  8 |
		(vlong)p[7];
}

/* /dev/bintime fields */
enum {
	NSEC = 0,
	FTICKS,
	FTICKSHZ,
	UPTIME,
};

vlong
nsec(void)
{
	vlong v[NSEC+1];

	if(readbintime(v, sizeof(v)))
		return -1;
	return gb64((uchar*)&v[NSEC]);
}

vlong
uptime(void)
{
	vlong v[UPTIME+1];

	if(readbintime(v, sizeof(v)))
		return -1;
	return gb64((uchar*)&v[UPTIME]);
}

uvlong
fastticks(uvlong *hz)
{
	vlong v[FTICKSHZ+1];

	if(readbintime(v, sizeof(v))) {
		if(hz != nil)
			*hz = 0;
		return -1;
	}
	if(hz != nil) *hz = gb64((uchar*)&v[FTICKSHZ]);
	return gb64((uchar*)&v[FTICKS]);
}
