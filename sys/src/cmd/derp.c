#include <u.h>
#include <libc.h>

int	permcheck = 0;
int	usercheck = 0;
int	dumpcheck = 1;
int	sizecheck = 1;
int	timecheck = 0;

int	quiet = 0;
int	errors = 0;
int	noerror = 0;

void
error(char *fmt, ...)
{
	char buf[ERRMAX];
	va_list a;

	errors++;
	if(!quiet){
		va_start(a, fmt);
		vsnprint(buf, sizeof(buf), fmt, a);
		va_end(a);

		fprint(2, "%s: %s\n", argv0, buf);
	}
	if(!noerror)
		exits("errors");
}

#pragma	varargck	argpos	error	1

void*
emalloc(int n)
{
	void *v;

	if((v = malloc(n)) == nil){
		noerror = 0;
		error("out of memory");
	}
	return v;
}

enum {
	BUFSIZE = 8*1024,
};

int
cmpfile(char *a, char *b)
{
	static uchar buf1[BUFSIZE], buf2[BUFSIZE];
	int r, n, m, fd1, fd2;
	
	if((fd1 = open(a, OREAD)) < 0)
		error("can't open %s: %r", a);
	if((fd2 = open(b, OREAD)) < 0)
		error("can't open %s: %r", b);

	r = fd1 != fd2;
	if(fd1 >= 0 && fd2 >= 0)
		do{
			if((n = readn(fd1, buf1, sizeof(buf1))) < 0){
				error("can't read %s: %r", a);
				break;
			}
			m = n;
			if(m == 0)
				m++;	/* read past eof to verify size */
			if((m = readn(fd2, buf2, m)) < 0){
				error("can't read %s: %r", b);
				break;
			}
			if(m != n)
				break;
			if(n == 0){
				r = 0;
				break;
			}
		} while(memcmp(buf1, buf2, n) == 0);

	if(fd1 >= 0)
		close(fd1);
	if(fd2 >= 0)
		close(fd2);

	return r;
}

int
samefile(Dir *a, Dir *b)
{
	if(a == b)
		return 1;

	if(a->type == b->type && a->dev == b->dev &&
		a->qid.type == b->qid.type &&
		a->qid.path == b->qid.path &&
		a->qid.vers == b->qid.vers){

		if((a->qid.type & QTDIR) == 0)
			return 1;

		/*
		 * directories in /n/dump have the same qid, but
		 * atime can be used to skip potentially
		 * untouched subtrees.
		 */
		if(dumpcheck && a->atime == b->atime)
			return 1;
	}

	return 0;
}

int
dcmp(Dir *a, Dir *b)
{
	if(a == nil || b == nil)
		return a != b;

	if(samefile(a, b))
		return 0;

	if((a->qid.type | b->qid.type) & QTDIR)
		return 1;

	if((a->mode ^ b->mode) & permcheck)
		return 1;

	if(usercheck){
		if(strcmp(a->uid, b->uid) != 0)
			return 1;
		if(strcmp(a->gid, b->gid) != 0)
			return 1;
	}

	if(sizecheck)
		if(a->length != b->length)
			return 1;

	if(timecheck)
		if(a->mtime != b->mtime)
			return 1;

	if(sizecheck && timecheck)
		return 0;

	return cmpfile(a->name, b->name);
}

Dir*
statdir(char *path)
{
	Dir *d;

	d = dirstat(path);
	if(d == nil)
		error("can't stat %s: %r", path);
	else {
		d->name = emalloc(strlen(path)+1);
		strcpy(d->name, path);
	}
	return d;
}

char*
pjoin(char *path, char *name)
{
	char *s;
	int n;

	n = strlen(path);
	s = emalloc(n+strlen(name)+2);
	strcpy(s, path);
	if(path[0] != '\0' && path[n-1] != '/')
		s[n++] = '/';
	strcpy(s+n, name);
	return s;
}

Dir*
absdir(Dir *d, char *path)
{
	if(d != nil)
		d->name = pjoin(path, d->name);
	return d;
}

void
cleardir(Dir *d)
{
	if(d != nil){
		free(d->name);
		d->name = "";
	}
}

void
freedir(Dir *d)
{
	cleardir(d);
	free(d);
}

int
dnamecmp(void *a, void *b)
{
	return strcmp(((Dir*)a)->name, ((Dir*)b)->name);
}

int
readifdir(Dir **dp)
{
	int n, fd;
	Dir *d;

	d = *dp;
	*dp = nil;
	if(d == nil || (d->qid.type & QTDIR) == 0)
		return 0;
	fd = open(d->name, OREAD);
	if(fd < 0){
		error("can't open %s: %r", d->name);
		return -1;
	}
	if((n = dirreadall(fd, dp)) < 0)
		error("can't read %s: %r", d->name);
	close(fd);
	if(n > 1)
		qsort(*dp, n, sizeof(Dir), dnamecmp);
	return n;
}

void
diffgen(Dir *ld, Dir *rd, Dir *ad, char *path);

void
diffdir(Dir *ld, Dir *rd, Dir *ad, char *path)
{
	int n, m, o, i, j, k, t, v;
	char *sp, *lp, *rp, *ap;
	Dir *od;

	lp = rp = ap = nil;
	if(ld != nil)
		lp = ld->name;
	if(rd != nil)
		rp = rd->name;
	if(ad != nil){
		/* check if ad is the same as ld or rd */
		if(ld != nil && samefile(ad, ld)){
			ap = ld->name;
			ad = nil;	/* don't read directory twice */
		}
		else if(rd != nil && samefile(ad, rd)){
			ap = rd->name;
			ad = nil;	/* don't read directory twice */
		}
		else
			ap = ad->name;
	}

	n = readifdir(&ld);
	m = readifdir(&rd);
	if(n <= 0 && m <= 0)
		return;

	/* at least one side is directory */
	o = readifdir(&ad);

	i = j = k = 0;
	for(;;){
		if(i < n)
			t = (j < m) ? strcmp(ld[i].name, rd[j].name) : -1;
		else if(j < m)
			t = 1;
		else
			break;

		od = nil;
		if(t < 0){
			sp = pjoin(path, ld[i].name);
			if(ap == lp)
				od = &ld[i];
			else while(k < o){
				v = strcmp(ad[k].name, ld[i].name);
				if(v == 0){
					od = absdir(&ad[k++], ap);
					break;
				} else if(v > 0)
					break;
				k++;
			}
			diffgen(absdir(&ld[i], lp), nil, od, sp);
			cleardir(&ld[i]);
			if(&ld[i] == od)
				od = nil;
			i++;
		} else {
			sp = pjoin(path, rd[j].name);
			if(ap == rp)
				od = &rd[j];
			else while(k < o){
				v = strcmp(ad[k].name, rd[j].name);
				if(v == 0){
					od = absdir(&ad[k++], ap);
					break;
				} else if(v > 0)
					break;
				k++;
			}
			if(t > 0)
				diffgen(nil, absdir(&rd[j], rp), od, sp);
			else {
				if(ap == lp)
					od = &ld[i];
				diffgen(absdir(&ld[i], lp), absdir(&rd[j], rp), od, sp);
				cleardir(&ld[i]);
				if(&ld[i] == od)
					od = nil;
				i++;
			}
			cleardir(&rd[j]);
			if(&rd[j] == od)
				od = nil;
			j++;
		}
		cleardir(od);
		free(sp);
	}

	free(ld);
	free(rd);
	free(ad);
}

void
diffgen(Dir *ld, Dir *rd, Dir *ad, char *path)
{
	if(dcmp(ld, rd) == 0)
		return;

	if(ld == nil || rd == nil){
		/* one side doesnt exit anymore */
		if(ad != nil){
			/* existed before, is deletion */
			if(ld != nil && (ad->qid.type & QTDIR) && (ld->qid.type & QTDIR)){
				/* remote deleted direcotry, remote newer */
				diffdir(ld, nil, ad, path);
				print("nd\t%s\n", path);
				return;
			} else if(rd != nil && (ad->qid.type & QTDIR) && (rd->qid.type & QTDIR)){
				/* local deleted direcotry, local newer */
				diffdir(nil, rd, ad, path);
				print("dn\t%s\n", path);
				return;
			} else if(dcmp(rd, ad) == 0){
				/* local deleted file, local newer */
				print("dn\t%s\n", path);
			} else if(dcmp(ld, ad) == 0){
				/* remote deleted file, remote newer */
				print("nd\t%s\n", path);
			} else if(ld != nil){
				if((ld->qid.type ^ ad->qid.type) & QTDIR){
					/* local file type change, remote deleted, no conflict */
					diffgen(ld, nil, nil, path);
					return;
				}
				/* local modified, remote deleted, conflict */
				print("md!\t%s\n", path);
				return;
			} else {
				if((rd->qid.type ^ ad->qid.type) & QTDIR){
					/* remote file type change, local deleted, no conflict */
					diffgen(nil, rd, nil, path);
					return;
				}
				/* remote modified, local deleted, conflict */
				print("dm!\t%s\n", path);
				return;
			}
		} else {
			/* didnt exist before, is addition */
			if(ld != nil){
				/* local added file, local newer */
				print("an\t%s\n", path);
			} else {
				/* remote added file, remote newer */
				print("na\t%s\n", path);
			}
		}
	} else {
		if(ad != nil){
			/* existed before, is modification */
			if((ad->qid.type & QTDIR) && (ld->qid.type & QTDIR) && (rd->qid.type & QTDIR)){
				/* all still directories, no problem */
			} else if(dcmp(rd, ad) == 0){
				if((ld->qid.type ^ ad->qid.type) & QTDIR){
					/* local file type change */
					diffgen(nil, ad, ad, path);
					diffgen(ld, nil, nil, path);
					return;
				}
				/* local modified file, local newer */
				print("mn\t%s\n", path);
			} else if(dcmp(ld, ad) == 0){
				if((rd->qid.type ^ ad->qid.type) & QTDIR){
					/* remote file type change */
					diffgen(ad, nil, ad, path);
					diffgen(nil, rd, nil, path);
					return;
				}
				/* remote modified file, remote newer */
				print("nm\t%s\n", path);
			} else {
				if((ld->qid.type & QTDIR) != (ad->qid.type & QTDIR) &&
				   (rd->qid.type & QTDIR) != (ad->qid.type & QTDIR) &&
				   (ld->qid.type & QTDIR) == (rd->qid.type & QTDIR)){
					if((ad->qid.type & QTDIR) == 0){
						/* local and remote became directories, was file before */
						diffdir(ld, rd, nil, path);
					} else {
						/* local and remote became diverging files, conflict */
						print("aa!\t%s\n", path);
					}
				} else {
					/* local and remote modified, conflict */
					print("mm!\t%s\n", path);
				}
				return;
			}
		} else {
			/* didnt exist before, is addition from both */
			if((ld->qid.type & QTDIR) && (rd->qid.type & QTDIR)){
				/* local and remote added directories, no problem */
			} else {
				/* local and remote added diverging files, conflict */
				print("aa!\t%s\n", path);
				return;
			}
		}
	}

	diffdir(ld, rd, ad, path);
}

void
diff3(char *lp, char *ap, char *rp)
{
	Dir *ld, *rd, *ad;

	ad = nil;
	ld = statdir(lp);
	rd = statdir(rp);
	if(ld == nil && rd == nil)
		goto Out;
	else if(strcmp(ap, lp) == 0)
		ad = ld;
	else if(strcmp(ap, rp) == 0)
		ad = rd;
	else if((ad = statdir(ap)) != nil){
		if(ld != nil && samefile(ad, ld)){
			freedir(ad);
			ad = ld;
		}
		else if(rd != nil && samefile(ad, rd)){
			freedir(ad);
			ad = rd;
		}
	}
	diffgen(ld, rd, ad, "");
Out:
	freedir(ld);
	freedir(rd);
	if(ad != nil && ad != ld && ad != rd)
		freedir(ad);
}

void
usage(void)
{
	fprint(2, "usage: %s [ -qcutDL ] [ -p perms ] myfile oldfile yourfile\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	ARGBEGIN {
	case 'q':
		quiet = 1;
		break;
	case 'c':
		noerror = 1;
		break;
	case 'u':
		usercheck = 1;
		break; 
	case 't':
		timecheck = 1;
		break;
	case 'D':
		dumpcheck = 0;
		break;
	case 'L':
		sizecheck = 0;
		break;
	case 'p':
		permcheck = strtol(EARGF(usage()), nil, 8) & 0777;
		break;
	default:
		usage();
	} ARGEND;

	if(argc != 3)
		usage();

	diff3(argv[0], argv[1], argv[2]);

	if(errors)
		exits("errors");

	exits(0);
}
