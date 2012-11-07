#include <u.h>
#include <libc.h>

typedef struct Proc Proc;
struct Proc {
	int pid;
	Proc *first, *parent, *next;
	Proc *hash;
};

Proc *hash[1024];
Rune buf[512];

Proc *
getproc(int pid)
{
	Proc *p;

	for(p = hash[pid % nelem(hash)]; p; p = p->hash)
		if(p->pid == pid)
			return p;
	return nil;
}

void
addproc(int pid)
{
	Proc *p;
	
	p = mallocz(sizeof(*p), 1);
	if(p == nil)
		sysfatal("malloc: %r");
	p->pid = pid;
	p->hash = hash[pid % nelem(hash)];
	hash[pid % nelem(hash)] = p;
}

int
theppid(int pid)
{
	char b[128];
	int fd, ppid;
	
	ppid = 0;
	snprint(b, sizeof(b), "/proc/%d/ppid", pid);
	fd = open(b, OREAD);
	if(fd >= 0){
		memset(b, 0, sizeof b);
		if(read(fd, b, sizeof b-1) >= 0){
			ppid = atoi(b);
			if(ppid < 0)
				ppid = 0;
		}
		close(fd);
	}
	return ppid;
}

void
addppid(int pid)
{
	Proc *p, *par, **l;
	
	p = getproc(pid);
	par = getproc(theppid(pid));
	if(par == nil)
		par = getproc(0);
	p->parent = par;
	for(l = &par->first; *l; l = &((*l)->next))
		if((*l)->pid > pid)
			break;
	p->next = *l;
	*l = p;
}

void
addprocs(void)
{
	int fd, rc, i;
	Dir *d;
	
	fd = open("/proc", OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	rc = dirreadall(fd, &d);
	if(rc < 0)
		sysfatal("dirreadall: %r");
	close(fd);
	for(i = 0; i < rc; i++)
		if(d[i].mode & DMDIR)
			addproc(atoi(d[i].name));
	for(i = 0; i < rc; i++)
		if(d[i].mode & DMDIR)
			addppid(atoi(d[i].name));
	free(d);
}

int
readout(char *file)
{
	int fd, rc, i, n;
	char b[512];

	fd = open(file, OREAD);
	if(fd < 0)
		return -1;
	n = 0;
	while((rc = read(fd, b, sizeof b)) > 0){
		for(i=0; i<rc; i++)
			if(b[i] == '\n')
				b[i] = ' ';
		write(1, b, rc);
		n += rc;
	}
	close(fd);
	return n;
}

void
printargs(int pid)
{
	char b[128], *p;
	int fd;

	if(pid == 0)
		return;
	snprint(b, sizeof(b), "/proc/%d/args", pid);
	if(readout(b) > 0)
		return;
	snprint(b, sizeof(b), "/proc/%d/status", pid);
	fd = open(b, OREAD);
	if(fd < 0)
		return;
	memset(b, 0, sizeof b);
	if(read(fd, b, 27) <= 0){
		close(fd);
		return;
	}
	p = b + strlen(b)-1;
	while(p > b && *p == ' ')
		*p-- = 0;
	print("%s", b);
	close(fd);
}

void
descend(Proc *p, Rune *r)
{
	Rune last;
	Proc *q;
	
	last = *--r;
	*r = last == L' ' ? L'└' : L'├';
	if(p->pid != 0){
		print("%S", buf);
		printargs(p->pid);
		print(" [%d]\n", p->pid);
	}
	*r = last;
	*++r = L'│';
	for(q = p->first; q; q = q->next) {
		if(q->next == nil)
			*r = L' ';
		descend(q, r + 1);
	}
	*r = 0;
}

void
printprocs(void)
{
	descend(getproc(0), buf);
}

void
main()
{
	addproc(0);
	addprocs();
	printprocs();
}
