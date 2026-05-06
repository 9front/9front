#include <u.h>
#include <libc.h>
#include <bio.h>
#include "diff.h"

static int
itemcmp(void *v1, void *v2)
{
	char **d1 = v1, **d2 = v2;

	return strcmp(*d1, *d2);
}

static char **
scandir(char *name)
{
	char **cp;
	Dir *db;
	int nitems;
	int fd, n;

	if ((fd = open(name, OREAD)) < 0) {
		fprint(2, "%s: can't open %s: %r\n", argv0, name);
		/* fake an empty directory */
		cp = emalloc(sizeof(char*));
		cp[0] = 0;
		return cp;
	}
	cp = 0;
	nitems = 0;
	if((n = dirreadall(fd, &db)) > 0){
		while (n--) {
			cp = erealloc(cp, (nitems+1)*sizeof(char*));
			cp[nitems] = emalloc(strlen((db+n)->name)+1);
			strcpy(cp[nitems], (db+n)->name);
			nitems++;
		}
		free(db);
	}
	cp = erealloc(cp, (nitems+1)*sizeof(char*));
	cp[nitems] = 0;
	close(fd);
	qsort((char *)cp, nitems, sizeof(char*), itemcmp);
	return cp;
}

static int
isdotordotdot(char *p)
{
	if (*p == '.') {
		if (!p[1])
			return 1;
		if (p[1] == '.' && !p[2])
			return 1;
	}
	return 0;
}

void
diffdir(char *f, char *t, int level)
{
	char  **df, **dt, **dirf, **dirt;
	char *from, *to;
	int res;
	char fb[MAXPATHLEN+1], tb[MAXPATHLEN+1];

	df = scandir(f);
	dt = scandir(t);
	dirf = df;
	dirt = dt;
	while (*df || *dt) {
		from = *df;
		to = *dt;
		if (from && isdotordotdot(from)) {
			df++;
			continue;
		}
		if (to && isdotordotdot(to)) {
			dt++;
			continue;
		}
		if (!from)
			res = 1;
		else if (!to)
			res = -1;
		else
			res = strcmp(from, to);
		if (res < 0) {
			if (mode == 0 || mode == 'n')
				Bprint(&stdout, "Only in %s: %s\n", f, from);
			df++;
			continue;
		}
		if (res > 0) {
			if (mode == 0 || mode == 'n')
				Bprint(&stdout, "Only in %s: %s\n", t, to);
			dt++;
			continue;
		}
		if (mkpathname(fb, f, from))
			continue;
		if (mkpathname(tb, t, to))
			continue;
		diff(fb, tb, level+1);
		df++; dt++;
	}
	for (df = dirf; *df; df++)
		free(*df);
	for (dt = dirt; *dt; dt++)
		free(*dt);
	free(dirf);
	free(dirt);
}

void
diff(char *f, char *t, int level)
{
	char *fp, *tp, *p, fb[MAXPATHLEN+1], tb[MAXPATHLEN+1];
	Dir *fsb, *tsb;

	fsb = nil;
	tsb = nil;
	if ((fp = statfile(f, &fsb)) == 0)
		goto Return;
	if ((tp = statfile(t, &tsb)) == 0)
		goto Return;
	if (DIRECTORY(fsb) && DIRECTORY(tsb)) {
		if (rflag || level == 0)
			diffdir(fp, tp, level);
		else
			Bprint(&stdout, "Common subdirectories: %s and %s\n", fp, tp);
	}
	else if (REGULAR_FILE(fsb) && REGULAR_FILE(tsb))
		diffreg(fp, f, tp, t);
	else {
		if (REGULAR_FILE(fsb)) {
			if ((p = utfrrune(f, '/')) == 0)
				p = f;
			else
				p++;
			if (mkpathname(tb, tp, p) == 0)
				diffreg(fp, f, tb, t);
		} else {
			if ((p = utfrrune(t, '/')) == 0)
				p = t;
			else
				p++;
			if (mkpathname(fb, fp, p) == 0)
				diffreg(fb, f, tp, t);
		}
	}
Return:
	free(fsb);
	free(tsb);
}
