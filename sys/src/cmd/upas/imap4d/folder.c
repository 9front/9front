#include "imap4d.h"

static	Mblock	mblck = {
.fd = -1
};

static char curdir[Pathlen];

void
resetcurdir(void)
{
	curdir[0] = 0;
}

int
mychdir(char *dir)
{
	if(strcmp(dir, curdir) == 0)
		return 0;
	if(dir[0] != '/' || strlen(dir) > Pathlen)
		return -1;
	strcpy(curdir, dir);
	if(chdir(dir) < 0){
		werrstr("mychdir failed: %r");
		return -1;
	}
	return 0;
}

int
cdcreate(char *dir, char *file, int mode, ulong perm)
{
	if(mychdir(dir) < 0)
		return -1;
	return create(file, mode, perm);
}

Dir*
cddirstat(char *dir, char *file)
{
	if(mychdir(dir) < 0)
		return nil;
	return dirstat(file);
}

int
cdexists(char *dir, char *file)
{
	Dir *d;

	d = cddirstat(dir, file);
	if(d == nil)
		return 0;
	free(d);
	return 1;
}

int
cddirwstat(char *dir, char *file, Dir *d)
{
	if(mychdir(dir) < 0)
		return -1;
	return dirwstat(file, d);
}

int
cdopen(char *dir, char *file, int mode)
{
	if(mychdir(dir) < 0)
		return -1;
	return open(file, mode);
}

int
cdremove(char *dir, char *file)
{
	if(mychdir(dir) < 0)
		return -1;
	return remove(file);
}

/*
 * open the one true mail lock file
 */
Mblock*
mblock(void)
{
	if(mblck.fd >= 0)
		bye("mail lock deadlock");
	mblck.fd = openlocked(mboxdir, "L.mbox", OREAD);
	if(mblck.fd >= 0)
		return &mblck;
	ilog("mblock: %r");
	return nil;
}

void
mbunlock(Mblock *ml)
{
	if(ml != &mblck)
		bye("bad mail unlock");
	if(ml->fd < 0)
		bye("mail unlock when not locked");
	close(ml->fd);
	ml->fd = -1;
}

void
mblockrefresh(Mblock *ml)
{
	char buf[1];

	seek(ml->fd, 0, 0);
	read(ml->fd, buf, 1);
}

int
mblocked(void)
{
	return mblck.fd >= 0;
}

char*
impname(char *name)
{
	char *s, buf[Pathlen];
	int n;

	encfs(buf, sizeof buf, name);
	n = strlen(buf) + STRLEN(".imp") + 1;
	s = binalloc(&parsebin, n, 0);
	if(s == nil)
		return nil;
	snprint(s, n, "%s.imp", name);
	return s;
}

/*
 * massage the mailbox name into something valid
 * eliminates all .', and ..',s, redundatant and trailing /'s.
 */
char *
mboxname(char *s)
{
	char *ss, *p;

	ss = mutf7str(s);
	if(ss == nil)
		return nil;
	cleanname(ss);
	if(!okmbox(ss))
		return nil;
	p = binalloc(&parsebin, Pathlen, 0);
	return encfs(p, Pathlen, ss);
}

char*
strmutf7(char *s)
{
	char *m;
	int n;

	n = strlen(s) * Mutf7max + 1;
	m = binalloc(&parsebin, n, 0);
	if(m == nil)
		return nil;
	return encmutf7(m, n, s);
}

char*
mutf7str(char *s)
{
	char *m;
	int n;

	/*
	 * n = strlen(s) * UTFmax / (2.67) + 1
	 * UTFmax / 2.67 == 3 / (8/3) == 9 / 8
	 */
	n = strlen(s);
	n = (n * 9 + 7) / 8 + 1;
	m = binalloc(&parsebin, n, 0);
	if(m == nil)
		return nil;
	return decmutf7(m, n, s);
}
