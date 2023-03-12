#include "imap4d.h"

/*
 * reverse string [s:e) in place
 */
void
strrev(char *s, char *e)
{
	int c;

	while(--e > s){
		c = *s;
		*s++ = *e;
		*e = c;
	}
}

int
isdotdot(char *s)
{
	return s[0] == '.' && s[1] == '.' && (s[2] == '/' || s[2] == 0);
}

int
issuffix(char *suf, char *s)
{
	int n;

	n = strlen(s) - strlen(suf);
	if(n < 0)
		return 0;
	return strcmp(s + n, suf) == 0;
}

int
isprefix(char *pre, char *s)
{
	return strncmp(pre, s, strlen(pre)) == 0;
}

char*
readfile(int fd)
{
	char *s;
	long length;
	Dir *d;

	d = dirfstat(fd);
	if(d == nil)
		return nil;
	length = d->length;
	free(d);
	s = binalloc(&parsebin, length + 1, 0);
	if(s == nil || readn(fd, s, length) != length)
		return nil;
	s[length] = 0;
	return s;
}

/*
 * create the imap tmp file.
 * it just happens that we don't need multiple temporary files.
 */
int
imaptmp(void)
{
	char buf[ERRMAX], name[Pathlen];
	int tries, fd;

	snprint(name, sizeof name, "/mail/box/%s/mbox.tmp.imp", username);
	for(tries = 0; tries < Locksecs*2; tries++){
		fd = create(name, ORDWR|ORCLOSE|OCEXEC, DMEXCL|0600);
		if(fd >= 0)
			return fd;
		errstr(buf, sizeof buf);
		if(cistrstr(buf, "locked") == nil)
			break;
		sleep(500);
	}
	return -1;
}

/*
 * open a file which might be locked.
 * if it is, spin until available
 */
static char *etab[] = {
	"not found",
	"does not exist",
	"file locked",		// hjfs
	"file is locked",
	"exclusive lock",
	"already exists",
};

static int
bad(int idx)
{
	char buf[ERRMAX];
	int i;

	rerrstr(buf, sizeof buf);
	for(i = idx; i < nelem(etab); i++)
		if(strstr(buf, etab[i]))
			return 0;
	return 1;
}

int
openlocked(char *dir, char *file, int mode)
{
	int i, fd;

	for(i = 0; i < 30; i++){
		if((fd = cdopen(dir, file, mode)) >= 0 || bad(0))
			return fd;
		if((fd = cdcreate(dir, file, mode|OEXCL, DMEXCL|0600)) >= 0  || bad(2))
			return fd;
		sleep(1000);
	}
	werrstr("lock timeout");
	return -1;
}

int
fqid(int fd, Qid *qid)
{
	Dir *d;

	d = dirfstat(fd);
	if(d == nil)
		return -1;
	*qid = d->qid;
	free(d);
	return 0;
}

uint
mapint(Namedint *map, char *name)
{
	int i;

	for(i = 0; map[i].name != nil; i++)
		if(cistrcmp(map[i].name, name) == 0)
			break;
	return map[i].v;
}

char*
estrdup(char *s)
{
	char *t;

	t = emalloc(strlen(s) + 1);
	strcpy(t, s);
	return t;
}

void*
emalloc(ulong n)
{
	void *p;

	p = malloc(n);
	if(p == nil)
		bye("server out of memory");
	setmalloctag(p, getcallerpc(&n));
	return p;
}

void*
ezmalloc(ulong n)
{
	void *p;

	p = malloc(n);
	if(p == nil)
		bye("server out of memory");
	setmalloctag(p, getcallerpc(&n));
	memset(p, 0, n);
	return p;
}

void*
erealloc(void *p, ulong n)
{
	p = realloc(p, n);
	if(p == nil)
		bye("server out of memory");
	setrealloctag(p, getcallerpc(&p));
	return p;
}

void
setname(char *fmt, ...)
{
	char buf[128], *p;
	int fd;
	va_list arg;

	snprint(buf, sizeof buf, "/proc/%d/args", getpid());
	if((fd = open(buf, OWRITE)) < 0)
		return;

	va_start(arg, fmt);
	p = vseprint(buf, buf + sizeof buf, fmt, arg);
	va_end(arg);

	write(fd, buf, p - buf);
	close(fd);
}
