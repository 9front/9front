#include "common.h"
#include <auth.h>
#include <ndb.h>

/*
 *  return the date
 */
Tmfmt
thedate(Tm *tm)
{
	Tzone *tz;

	/* if the local time is screwed, just do gmt */
	tz = tzload("local");
	tmnow(tm, tz);
	return tmfmt(tm, Timefmt);
}

/*
 *  return the user id of the current user
 */
char *
getlog(void)
{
	return getuser();
}

/*
 *  return the lock name (we use one lock per directory)
 */
static void
lockname(Mlock *l, char *path)
{
	char *e, *q;

	seprint(l->name, e = l->name+sizeof l->name, "%s", path);
	q = strrchr(l->name, '/');
	if(q == nil)
		q = l->name;
	else
		q++;
	seprint(q, e, "%s", "L.mbox");
}

int
syscreatelocked(char *path, int mode, int perm)
{
	return create(path, mode, DMEXCL|perm);
}

int
sysopenlocked(char *path, int mode)
{
/*	return open(path, OEXCL|mode);/**/
	return open(path, mode);		/* until system call is fixed */
}

int
sysunlockfile(int fd)
{
	return close(fd);
}

/*
 *  try opening a lock file.  If it doesn't exist try creating it.
 */
static int
openlockfile(Mlock *l)
{
	int fd;
	Dir *d, nd;
	char *p;

	l->fd = open(l->name, OREAD);
	if(l->fd >= 0)
		return 0;
	if(d = dirstat(l->name)){
		free(d);
		return 1;	/* try again later */
	}
	l->fd = create(l->name, OREAD, DMEXCL|0666);
	if(l->fd >= 0){
		nulldir(&nd);
		nd.mode = DMEXCL|0666;
		if(dirfwstat(l->fd, &nd) < 0){
			/* if we can't chmod, don't bother */
			/* live without the lock but log it */
			close(l->fd);
			l->fd = -1;
			syslog(0, "mail", "lock error: %s: %r", l->name);
			remove(l->name);
		}
		return 0;
	}
	/* couldn't create; let's see what we can whine about */
	p = strrchr(l->name, '/');
	if(p != 0){
		*p = 0;
		fd = access(l->name, 2);
		*p = '/';
	}else
		fd = access(".", 2);
	if(fd < 0)
		/* live without the lock but log it */
		syslog(0, "mail", "lock error: %s: %r", l->name);
	close(fd);
	return 0;
}

#define LSECS 5*60

/*
 *  Set a lock for a particular file.  The lock is a file in the same directory
 *  and has L. prepended to the name of the last element of the file name.
 */
Mlock*
syslock(char *path)
{
	Mlock *l;
	int tries;

	l = mallocz(sizeof(Mlock), 1);
	if(l == 0)
		return nil;

	lockname(l, path);
	/*
	 *  wait LSECS seconds for it to unlock
	 */
	for(tries = 0; tries < LSECS*2; tries++)
		switch(openlockfile(l)){
		case 0:
			return l;
		case 1:
			sleep(500);
			break;
		}
	free(l);
	return nil;
}

/*
 *  like lock except don't wait
 */
Mlock *
trylock(char *path)
{
	char buf[1];
	int fd;
	Mlock *l;

	l = mallocz(sizeof(Mlock), 1);
	if(l == 0)
		return 0;

	lockname(l, path);
	if(openlockfile(l) != 0){
		free(l);
		return 0;
	}
	
	/* fork process to keep lock alive */
	switch(l->pid = rfork(RFPROC)){
	default:
		break;
	case 0:
		fd = l->fd;
		for(;;){
			sleep(1000*60);
			if(pread(fd, buf, 1, 0) < 0)
				break;
		}
		_exits(nil);
	}
	return l;
}

void
syslockrefresh(Mlock *l)
{
	char buf[1];

	pread(l->fd, buf, 1, 0);
}

void
sysunlock(Mlock *l)
{
	if(l == 0)
		return;
	close(l->fd);
	if(l->pid > 0)
		postnote(PNPROC, l->pid, "time to die");
	free(l);
}

/*
 *  Open a file.  The modes are:
 *
 *	l	- locked
 *	a	- set append permissions
 *	r	- readable
 *	w	- writable
 *	A	- append only (doesn't exist in Bio)
 */
Biobuf*
sysopen(char *path, char *mode, ulong perm)
{
	int sysperm, sysmode, fd, docreate, append, truncate;
	Dir *d, nd;
	Biobuf *bp;

	/*
	 *  decode the request
	 */
	sysperm = 0;
	sysmode = -1;
	docreate = 0;
	append = 0;
	truncate = 0;
 	for(; *mode; mode++)
		switch(*mode){
		case 'A':
			sysmode = OWRITE;
			append = 1;
			break;
		case 'c':
			docreate = 1;
			break;
		case 'l':
			sysperm |= DMEXCL;
			break;
		case 'a':
			sysperm |= DMAPPEND;
			break;
		case 'w':
			if(sysmode == -1)
				sysmode = OWRITE;
			else
				sysmode = ORDWR;
			break;
		case 'r':
			if(sysmode == -1)
				sysmode = OREAD;
			else
				sysmode = ORDWR;
			break;
		case 't':
			truncate = 1;
			break;
		default:
			break;
		}
	switch(sysmode){
	case OREAD:
	case OWRITE:
	case ORDWR:
		break;
	default:
		if(sysperm&DMAPPEND)
			sysmode = OWRITE;
		else
			sysmode = OREAD;
		break;
	}

	/*
	 *  create file if we need to
	 */
	if(truncate)
		sysmode |= OTRUNC;
	fd = open(path, sysmode);
	if(fd < 0){
		d = dirstat(path);
		if(d == nil){
			if(docreate == 0)
				return 0;

			fd = create(path, sysmode, sysperm|perm);
			if(fd < 0)
				return 0;
			nulldir(&nd);
			nd.mode = sysperm|perm;
			dirfwstat(fd, &nd);
		} else {
			free(d);
			return 0;
		}
	}

	bp = (Biobuf*)malloc(sizeof(Biobuf));
	if(bp == 0){
		close(fd);
		return 0;
	}
	memset(bp, 0, sizeof(Biobuf));
	Binit(bp, fd, sysmode&~OTRUNC);

	if(append)
		Bseek(bp, 0, 2);
	return bp;
}

/*
 *  close the file, etc.
 */
int
sysclose(Biobuf *bp)
{
	int rv;

	rv = Bterm(bp);
	close(Bfildes(bp));
	free(bp);
	return rv;
}

/*
 *  make a directory
 */
int
sysmkdir(char *file, ulong perm)
{
	int fd;

	if((fd = create(file, OREAD, DMDIR|perm)) < 0)
		return -1;
	close(fd);
	return 0;
}

/*
 *  read in the system name
 */
char *
sysname_read(void)
{
	static char name[128];
	char *s, *c;

	c = s = getenv("site");
	if(!c)
		c = alt_sysname_read();
	if(!c)
		c = "kremvax";
	strecpy(name, name+sizeof name, c);
	free(s);
	return name;
}

char *
alt_sysname_read(void)
{
	return sysname();
}

/*
 *  get all names
 */
char**
sysnames_read(void)
{
	int n;
	Ndbtuple *t, *nt;
	static char **namev;

	if(namev)
		return namev;

	free(csgetvalue(0, "sys", sysname(), "dom", &t));

	n = 0;
	for(nt = t; nt; nt = nt->entry)
		if(strcmp(nt->attr, "dom") == 0)
			n++;

	namev = (char**)malloc(sizeof(char *)*(n+3));
	if(namev){
		namev[0] = strdup(sysname_read());
		namev[1] = strdup(alt_sysname_read());
		n = 2;
		for(nt = t; nt; nt = nt->entry)
			if(strcmp(nt->attr, "dom") == 0)
				namev[n++] = strdup(nt->val);
		namev[n] = 0;
	}
	if(t)
		ndbfree(t);

	return namev;
}

/*
 *  read in the domain name
 */
char*
domainname_read(void)
{
	char **p;

	for(p = sysnames_read(); *p; p++)
		if(strchr(*p, '.'))
			return *p;
	return 0;
}

/*
 *  rename a file, fails unless both are in the same directory
 */
int
sysrename(char *old, char *new)
{
	Dir d;
	char *obase;
	char *nbase;

	obase = strrchr(old, '/');
	nbase = strrchr(new, '/');
	if(obase){
		if(nbase == 0)
			return -1;
		if(strncmp(old, new, obase-old) != 0)
			return -1;
		nbase++;
	} else {
		if(nbase)
			return -1;
		nbase = new;
	}
	nulldir(&d);
	d.name = nbase;
	return dirwstat(old, &d);
}

int
sysexist(char *file)
{
	return access(file, AEXIST) == 0;
}

static char yankeepig[] = "die: yankee pig dog";

int
syskill(int pid)
{
	return postnote(PNPROC, pid, yankeepig);
}

int
syskillpg(int pid)
{
	return postnote(PNGROUP, pid, yankeepig);
}

int
sysdetach(void)
{
	if(rfork(RFENVG|RFNAMEG|RFNOTEG) < 0) {
		werrstr("rfork failed");
		return -1;
	}
	return 0;
}

/*
 *  catch a write on a closed pipe
 */
static int *closedflag;
static int
catchpipe(void *, char *msg)
{
	static char *foo = "sys: write on closed pipe";

	if(strncmp(msg, foo, strlen(foo)) == 0){
		if(closedflag)
			*closedflag = 1;
		return 1;
	}
	return 0;
}
void
pipesig(int *flagp)
{
	closedflag = flagp;
	atnotify(catchpipe, 1);
}
void
pipesigoff(void)
{
	atnotify(catchpipe, 0);
}

int
islikeatty(int fd)
{
	char buf[64];
	int l;

	if(fd2path(fd, buf, sizeof buf) != 0)
		return 0;

	/* might be /mnt/term/dev/cons */
	l = strlen(buf);
	return l >= 9 && strcmp(buf+l-9, "/dev/cons") == 0;
}

int
holdon(void)
{
	int fd;

	if(!islikeatty(0))
		return -1;

	fd = open("/dev/consctl", OWRITE);
	write(fd, "holdon", 6);

	return fd;
}

int
sysopentty(void)
{
	return open("/dev/cons", ORDWR);
}

void
holdoff(int fd)
{
	write(fd, "holdoff", 7);
	close(fd);
}

int
sysfiles(void)
{
	return 128;
}

/*
 *  expand a path relative to the user's mailbox directory
 *
 *  if the path starts with / or ./, don't change it
 *
 */
char*
mboxpathbuf(char *to, int n, char *user, char *path)
{
	if(*path == '/' || !strncmp(path, "./", 2) || !strncmp(path, "../", 3))
		snprint(to, n, "%s", path);
	else
		snprint(to, n, "%s/box/%s/%s", MAILROOT, user, path);
	return to;
}

/*
 * warning: we're not quoting bad characters.  we're not encoding
 * non-ascii characters.  basically this function sucks.  don't use.
 */
char*
username0(Biobuf *b, char *from)
{
	char *p, *f[6];
	int n;
	static char buf[32];

	n = strlen(from);
	buf[0] = 0;
	for(;; free(p)) {
		p = Brdstr(b, '\n', 1);
		if(p == 0)
			break;
		if(strncmp(p, from, n)  || p[n] != '|')
			continue;
		if(getfields(p, f, nelem(f), 0, "|") < 3)
			continue;
		snprint(buf, sizeof buf, "\"%s\"", f[2]);
		/* no break; last match wins */
	}
	return buf[0]? buf: 0;
}

char*
username(char *from)
{
	char *s;
	Biobuf *b;

	s = 0;
	if(b = Bopen("/adm/keys.who", OREAD)){
		s = username0(b, from);
		Bterm(b);
	}
	if(s == 0 && (b = Bopen("/adm/netkeys.who", OREAD))){
		s = username0(b, from);
		Bterm(b);
	}
	return s;
}

/*
 * create a file and 
 *	1) ensure the modes we asked for
 *	2) make gid == uid
 */
static int
docreate(char *file, int perm)
{
	int fd;
	Dir ndir;
	Dir *d;

	/*  create the mbox */
	fd = create(file, OREAD, perm);
	if(fd < 0){
		fprint(2, "couldn't create %s\n", file);
		return -1;
	}
	d = dirfstat(fd);
	if(d == nil){
		fprint(2, "couldn't stat %s\n", file);
		return -1;
	}
	nulldir(&ndir);
	ndir.mode = perm;
	ndir.gid = d->uid;
	if(dirfwstat(fd, &ndir) < 0)
		fprint(2, "couldn't chmod %s: %r\n", file);
	close(fd);
	return 0;
}

static int
createfolder0(char *user, char *folder, char *ftype)
{
	char *p, *s, buf[Pathlen];
	int isdir, mode;
	Dir *d;

	assert(folder != 0);
	mboxpathbuf(buf, sizeof buf, user, folder);
	if(access(buf, 0) == 0){
		fprint(2, "%s already exists\n", ftype);
		return -1;
	}
	fprint(2, "creating new %s: %s\n", ftype, buf);

	/*
	 * if we can deliver to this mbox, it needs
	 * to be read/execable all the way down
	 */
	mode = 0711;
	if(!strncmp(buf, "/mail/box/", 10))
	if((s = strrchr(buf, '/')) && !strcmp(s+1, "mbox"))
		mode = 0755;
	for(p = buf; p; p++) {
		if(*p == '/')
			continue;
		p = strchr(p, '/');
		if(p == 0)
			break;
		*p = 0;
		if(access(buf, 0) != 0)
		if(docreate(buf, DMDIR|mode) < 0)
			return -1;
		*p = '/';
	}
	/* must match folder.c:/^openfolder */
	isdir = create(buf, OREAD, DMDIR|0777);

	/*
	 *  make sure everyone can write here if it's a mailbox
	 * rather than a folder
	 */
	if(mode == 0755)
	if(isdir >= 0 && (d = dirfstat(isdir))){
		d->mode |= 0773;
		dirfwstat(isdir, d);
		free(d);
	}

	if(isdir == -1){
		fprint(2, "can't create %s: %s\n", ftype, buf);
		return -1;
	}
	close(isdir);
	return 0;
}

int
createfolder(char *user, char *folder)
{
	return createfolder0(user, folder, "folder");
}

int
creatembox(char *user, char *mbox)
{
	char buf[Pathlen];

	if(mbox == 0)
		snprint(buf, sizeof buf, "mbox");
	else
		snprint(buf, sizeof buf, "%s/mbox", mbox);
	return createfolder0(user, buf, "mbox");
}
