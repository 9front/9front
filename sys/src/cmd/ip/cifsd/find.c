#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

static int
iswild(char *pattern)
{
	return strchrs(pattern, "*?<>\"") != nil;
}

static int
matchpattern(char *name, char *pattern, int casesensitive)
{
	Rune p, r;
	int n;

	while(*pattern){
		pattern += chartorune(&p, pattern);
		n = chartorune(&r, name);
		switch(p){
		case '?':
			if(r == 0)
				return 0;
			name += n;
			break;
		case '>':
			switch(r){
			case '.':
				if(!name[1] && matchpattern(name+1, pattern, casesensitive))
					return 1;
			case 0:
				return matchpattern(name, pattern, casesensitive);
			}
			name += n;
			break;
		case '*':
		case '<':
			while(r){
				if(matchpattern(name, pattern, casesensitive))
					return 1;
				if(p == '<' && r == '.' && !strchrs(name+1, ".")){
					name++;
					break;
				}
				n = chartorune(&r, name += n);
			}
			break;
		case '"':
			if(r == 0 && matchpattern(name, pattern, casesensitive))
				return 1;
			if(r != '.')
				return 0;
			name += n;
			break;
		default:
			if(p != r && casesensitive || toupperrune(p) != toupperrune(r))
				return 0;
			name += n;
		}
	}
	return *name == 0;
}

int
matchattr(Dir *d, int s)
{
	int a, m;

	m = ATTR_HIDDEN | ATTR_SYSTEM | ATTR_DIRECTORY;
	a = dosfileattr(d);
	if((a & ~s) & m)
		return 0;
	m = (s >> 8) & m;
	if(m && ((m & a) != m))
		return 0;
	return 1;
}


Find*
openfind(char *path, int (*namecmp)(char *, char *), int attr, int withdot, int *perr)
{
	char *base, *pattern, *parent;
	Dir *dir;
	int ndir, err;
	Find *f;

	f = nil;
	path = strdup(path);
	base = pattern = parent = nil;
	if(!splitpath(path, &base, &pattern)){
		err = STATUS_OBJECT_PATH_SYNTAX_BAD;
		goto out;
	}
	if(debug)
		fprint(2, "base %s\npattern %s\nattr %x\nwithdot %d\n", base, pattern, attr, withdot);

	if(iswild(pattern)){
		if((ndir = xdirread(&base, namecmp, &dir)) < 0){
			err = smbmkerror();
			goto out;
		}
	} else {
		ndir = 0;
		withdot = 0;
		if(dir = xdirstat(&path, namecmp)){	
			free(base);
			free(pattern);
			splitpath(path, &base, &pattern);
			ndir++;
		}
	}

	f = mallocz(sizeof(*f), 1);
	f->ref = 1;
	f->base = base;
	f->pattern = pattern;
	f->attr = attr;
	f->dir = dir;
	f->ndir = ndir;
	f->index = 0;
	f->casesensitive = (namecmp == strcmp);

	if(withdot){
		if(f->dot = dirstat(base))
			f->dot->name = ".";
		if(splitpath(base, &parent, nil))
			if(f->dotdot = dirstat(parent))
				f->dotdot->name = "..";
	} 

	base = nil;
	pattern = nil;
	err = 0;

out:
	if(perr)
		*perr = err;

	free(base);
	free(pattern);
	free(parent);
	free(path);

	return f;
}

int
readfind(Find *f, int i, Dir **dp)
{
	Dir *d;
	int x;

	x = i;
	if(f->dot && f->dotdot)
		x -= 2;
	for(;;){
		if(x == -2){
			d = f->dot;
		} else if(x == -1){
			d = f->dotdot;
		} else if(x < f->ndir){
			d = f->dir + x;
		} else {
			d = nil;
			i = -1;
			break;
		}
		if(matchattr(d, f->attr) && matchpattern(d->name, f->pattern, f->casesensitive))
			break;
		i++; x++;
	}
	if(debug && d)
		fprint(2, "readfile [%d] attr=%x name=%s\n", i, extfileattr(d), d->name);
	*dp = d;
	return i;
}

void
putfind(Find *f)
{
	if(f == nil || --f->ref)
		return;
	free(f->pattern);
	free(f->base);
	free(f->dot);
	free(f->dotdot);
	free(f->dir);
	free(f);
}
