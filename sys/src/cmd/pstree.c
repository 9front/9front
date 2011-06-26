#include <u.h>
#include <libc.h>

enum { NBUCKETS = 1024 };

typedef struct Proc Proc;
typedef struct Bucket Bucket;

struct Proc {
	int pid;
	Proc *first, *last, *parent, *next;
	Proc *bnext;
};

struct Bucket {
	Proc *first, *last;
} buck[NBUCKETS];

Proc *
getproc(int pid)
{
	Proc *p;
	
	for(p = buck[pid % NBUCKETS].first; p; p = p->next)
		if(p->pid == pid)
			return p;

	return nil;
}

void
addproc(int pid)
{
	Proc *p;
	Bucket *b;
	
	p = mallocz(sizeof(*p), 1);
	if(p == nil)
		sysfatal("malloc: %r");
	p->pid = pid;
	b = buck + pid % NBUCKETS;
	if(b->first != nil)
		b->last->bnext = p;
	else
		b->first = p;
	b->last = p;
}

int
theppid(int pid)
{
	char *file;
	int fd;
	char buf[12];
	int ppid;
	
	file = smprint("/proc/%d/ppid", pid);
	fd = open(file, OREAD);
	free(file);
	if(fd < 0)
		return 0;
	ppid = 0;
	if(read(fd, buf, sizeof buf) >= 0)
		ppid = atoi(buf);
	close(fd);
	return ppid;
}

void
addppid(int pid)
{
	int ppid;
	Proc *p, *par;
	
	p = getproc(pid);
	ppid = theppid(pid);
	par = getproc(ppid);
	if(par == nil)
		par = getproc(0);
	p->parent = par;
	if(par->first != nil)
		par->last->next = p;
	else
		par->first = p;
	par->last = p;
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

Rune buf[512];

int
readout(char *file)
{
	int fd, rc, n;
	char b[512];

	fd = open(file, OREAD);
	free(file);
	if(fd < 0)
		return -1;
	n = 0;
	while(rc = read(fd, b, 512), rc > 0) {
		write(1, b, rc);
		n += rc;
	}
	close(fd);
	return n;
}

void
printargs(int pid)
{
	char *file;
	char b[28], *p;
	int fd;

	if(pid == 0)
		return;
	if(readout(smprint("/proc/%d/args", pid)) > 0)
		return;
	file = smprint("/proc/%d/status", pid);
	fd = open(file, OREAD);
	free(file);
	if(fd < 0)
		return;
	memset(b, 0, sizeof b);
	if(read(fd, b, 27) <= 0)
		return;
	p = b + strlen(b) - 1;
	while(*p == ' ')
		*p-- = 0;
	print("%s", b);
}

void
descend(Proc *p, Rune *r)
{
	Rune last;
	Proc *q;
	
	last = *--r;
	*r = last == L' ' ? L'└' : L'├';
	print("%S", buf);
	printargs(p->pid);
	print(" [%d]\n", p->pid);
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
