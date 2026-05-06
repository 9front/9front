#include <u.h>
#include <libc.h>
#include <bio.h>
#include "diff.h"

Biobuf	stdout;
char	mode;			/* '\0', 'e', 'f', 'h' */
char	bflag;			/* ignore multiple and trailing blanks */
char	rflag;			/* recurse down directory trees */
char	mflag;			/* pseudo flag: doing multiple files, one dir */
int	anychange;

static char *tmp[] = {"/tmp/diff1XXXXXXXXXXX", "/tmp/diff2XXXXXXXXXXX"};
static int whichtmp;

void *
emalloc(unsigned n)
{
	register void *p;

	if ((p = malloc(n)) == 0)
		sysfatal("malloc: %r");
	return p;
}

void *
erealloc(void *p, unsigned n)
{
	void *rp;

	if ((rp = realloc(p, n)) == 0)
		sysfatal("realloc: %r");
	return rp;
}

int
mkpathname(char *pathname, char *path, char *name)
{
	if (strlen(path) + strlen(name) > MAXPATHLEN)
		sysfatal("pathname %s/%s too long", path, name);
	sprint(pathname, "%s/%s", path, name);
	return 0;
}

char *
mktmpfile(int input, Dir **sb)
{
	int fd, i;
	char *p;
	char buf[8192];

	p = mktemp(tmp[whichtmp++]);
	/*
	 * Because we want this file to stick around
	 * for the entire run of the program, we leak
	 * the fd intentionally here; when we exit,
	 * the system will remove the file for us.
	 */
	fd = create(p, OWRITE|ORCLOSE, 0600);
	if (fd < 0)
		sysfatal("cannot create %s: %r", p);
	while ((i = read(input, buf, sizeof(buf))) > 0) {
		if ((i = write(fd, buf, i)) < 0)
			break;
	}
	*sb = dirfstat(fd);
	if (i < 0)
		sysfatal("cannot read/write %s: %r", p);
	return p;
}

char *
statfile(char *file, Dir **sb)
{
	Dir *dir;
	int input;

	dir = dirstat(file);
	if(dir == nil) {
		if (strcmp(file, "-") || (dir = dirfstat(0)) == nil)
			sysfatal("cannot stat %s: %r", file);
		free(dir);
		return mktmpfile(0, sb);
	} else if (!REGULAR_FILE(dir) && !DIRECTORY(dir)) {
		free(dir);
		if ((input = open(file, OREAD)) == -1)
			sysfatal("cannot open %s: %r", file);
		file = mktmpfile(input, sb);
		close(input);
	} else
		*sb = dir;
	return file;
}
