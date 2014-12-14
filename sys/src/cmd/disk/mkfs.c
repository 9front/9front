#include <u.h>
#include <libc.h>
#include <disk.h>
#include <auth.h>
#include <bio.h>

enum{
	LEN = 4096,

	/*
	 * types of destination file sytems
	 */
	Kfs = 0,
	Fs,
	Archive,
};

void	protowarn(char *msg, void *);
void	protoenum(char *new, char *old, Dir *d, void *);

void	arch(Dir*);
void	copy(Dir*);
void	error(char *, ...);
void	kfscmd(char *);
void	mkdir(Dir*);
void	mountkfs(char*);
int	uptodate(Dir*, char*);
void	usage(void);
void	warn(char *, ...);

Biobufhdr bout;			/* stdout when writing archive */
uchar	boutbuf[2*LEN];
char	newfile[LEN];
char	oldfile[LEN];
char	*proto;
char	*cputype;
char	*users;
char	*oldroot;
char	*newroot;
char	*prog = "mkfs";
char	*buf;
char	*zbuf;
int	buflen = 1024-8;
int	verb;
int	modes;
int	ream;
int	debug;
int	xflag;
int	oflag;
int	sfd;
int	fskind;			/* Kfs, Fs, Archive */
int	setuid;			/* on Fs: set uid and gid? */
char	*user;

void
main(int argc, char **argv)
{
	char *name;
	int i, errs;

	quotefmtinstall();
	user = getuser();
	name = "";
	oldroot = "";
	newroot = "/n/kfs";
	users = 0;
	fskind = Kfs;
	ARGBEGIN{
	case 'a':
		if(fskind != Kfs) {
			fprint(2, "cannot use -a with -d\n");
			usage();
		}
		fskind = Archive;
		newroot = "";
		Binits(&bout, 1, OWRITE, boutbuf, sizeof boutbuf);
		break;
	case 'd':
		if(fskind != Kfs) {
			fprint(2, "cannot use -d with -a\n");
			usage();
		}
		fskind = Fs;
		newroot = ARGF();
		break;
	case 'D':
		debug = 1;
		break;
	case 'n':
		name = EARGF(usage());
		break;
	case 'p':
		modes = 1;
		break;
	case 'r':
		ream = 1;
		break;
	case 's':
		oldroot = ARGF();
		break;
	case 'u':
		users = ARGF();
		break;
	case 'U':
		setuid = 1;
		break;
	case 'v':
		verb = 1;
		break;
	case 'o':
		oflag = 1;
		break;
	case 'x':
		xflag = 1;
		break;
	case 'z':
		buflen = atoi(ARGF())-8;
		break;
	default:
		usage();
	}ARGEND

	if(!argc)
		usage();

	if((xflag || oflag) && fskind != Archive){
		fprint(2, "cannot use -x and -o without -a\n");
		usage();
	}

	buf = malloc(buflen);
	zbuf = malloc(buflen);
	memset(zbuf, 0, buflen);

	mountkfs(name);
	kfscmd("allow");
	cputype = getenv("cputype");
	if(cputype == 0)
		cputype = "386";

	errs = 0;
	for(i = 0; i < argc; i++){
		proto = argv[i];
		fprint(2, "processing %q\n", proto);
		if(rdproto(proto, oldroot, protoenum, protowarn, nil) < 0){
			fprint(2, "%q: can't open %q: skipping\n", prog, proto);
			errs++;
			continue;
		}
	}
	fprint(2, "file system made\n");
	kfscmd("disallow");
	kfscmd("sync");
	if(errs)
		exits("skipped protos");
	if(fskind == Archive){
		if(!xflag && !oflag)
			Bprint(&bout, "end of archive\n");
		Bterm(&bout);
	}
	exits(0);
}

/*
 * check if file to is up to date with
 * respect to the file represented by df
 */
int
uptodate(Dir *df, char *to)
{
	int ret;
	Dir *dt;

	if(fskind == Archive || ream || (dt = dirstat(to)) == nil)
		return 0;
	ret = dt->mtime >= df->mtime;
	free(dt);
	return ret;
}

void
copy(Dir *d)
{
	char cptmp[LEN], *p;
	int f, t, n, needwrite, nowarnyet = 1;
	vlong tot, len;
	Dir nd;

	f = open(oldfile, OREAD);
	if(f < 0){
		warn("can't open %q: %r", oldfile);
		return;
	}
	t = -1;
	if(fskind == Archive)
		arch(d);
	else{
		strcpy(cptmp, newfile);
		p = utfrrune(cptmp, L'/');
		if(!p)
			error("internal temporary file error");
		strcpy(p+1, "__mkfstmp");
		t = create(cptmp, OWRITE, 0666);
		if(t < 0){
			warn("can't create %q: %r", newfile);
			close(f);
			return;
		}
	}

	needwrite = 0;
	for(tot = 0; tot < d->length; tot += n){
		len = d->length - tot;
		/* don't read beyond d->length */
		if (len > buflen)
			len = buflen;
		n = read(f, buf, len);
		if(n <= 0) {
			if(n < 0 && nowarnyet) {
				warn("can't read %q: %r", oldfile);
				nowarnyet = 0;
			}
			/*
			 * don't quit: pad to d->length (in pieces) to agree
			 * with the length in the header, already emitted.
			 */
			memset(buf, 0, len);
			n = len;
		}
		if(fskind == Archive){
			if(Bwrite(&bout, buf, n) != n)
				error("write error: %r");
		}else if(memcmp(buf, zbuf, n) == 0){
			if(seek(t, n, 1) < 0)
				error("can't write zeros to %q: %r", newfile);
			needwrite = 1;
		}else{
			if(write(t, buf, n) < n)
				error("can't write %q: %r", newfile);
			needwrite = 0;
		}
	}
	close(f);
	if(needwrite){
		if(seek(t, -1, 1) < 0 || write(t, zbuf, 1) != 1)
			error("can't write zero at end of %q: %r", newfile);
	}
	if(tot != d->length){
		/* this should no longer happen */
		warn("wrong number of bytes written to %q (was %lld should be %lld)\n",
			newfile, tot, d->length);
		if(fskind == Archive){
			warn("seeking to proper position\n");
			/* does no good if stdout is a pipe */
			Bseek(&bout, d->length - tot, 1);
		}
	}
	if(fskind == Archive)
		return;
	remove(newfile);
	nulldir(&nd);
	nd.mode = d->mode;
	nd.gid = d->gid;
	nd.mtime = d->mtime;
	nd.name = d->name;
	if(dirfwstat(t, &nd) < 0)
		error("can't move tmp file to %q: %r", newfile);
	nulldir(&nd);
	nd.uid = d->uid;
	dirfwstat(t, &nd);
	close(t);
}

void
mkdir(Dir *d)
{
	Dir *d1;
	Dir nd;
	int fd;

	if(fskind == Archive){
		arch(d);
		return;
	}
	fd = create(newfile, OREAD, d->mode);
	nulldir(&nd);
	nd.mode = d->mode;
	nd.gid = d->gid;
	nd.mtime = d->mtime;
	if(fd < 0){
		if((d1 = dirstat(newfile)) == nil || !(d1->mode & DMDIR)){
			free(d1);
			error("can't create %q", newfile);
		}
		free(d1);
		if(dirwstat(newfile, &nd) < 0)
			warn("can't set modes for %q: %r", newfile);
		nulldir(&nd);
		nd.uid = d->uid;
		dirwstat(newfile, &nd);
		return;
	}
	if(dirfwstat(fd, &nd) < 0)
		warn("can't set modes for %q: %r", newfile);
	nulldir(&nd);
	nd.uid = d->uid;
	dirfwstat(fd, &nd);
	close(fd);
}

void
arch(Dir *d)
{
	Bprint(&bout, "%q %luo %q %q %lud %lld\n",
		newfile, d->mode, d->uid, d->gid, d->mtime, d->length);
}

void
protowarn(char *msg, void *)
{
	warn("%s", msg);
}

void
protoenum(char *new, char *old, Dir *d, void *)
{
	Dir nd;

	sprint(newfile, "%s%s", newroot, new);
	sprint(oldfile, "%s", old);

	if(oflag){
		if(!(d->mode & DMDIR))
			Bprint(&bout, "%q\n", cleanname(oldfile));
		return;
	}
	if(xflag){
		Bprint(&bout, "%q\t%ld\t%lld\n", new, d->mtime, d->length);
		return;
	}
	if(verb && (fskind == Archive || ream))
		fprint(2, "%q\n", new);
	if(fskind == Fs && !setuid){
		d->uid = "";
		d->gid = "";
	}
	if(!uptodate(d, newfile)){
		if(verb && (fskind != Archive && ream == 0))
			fprint(2, "%q\n", new);
		if(d->mode & DMDIR)
			mkdir(d);
		else
			copy(d);
	}else if(modes){
		nulldir(&nd);
		nd.mode = d->mode;
		nd.gid = d->gid;
		nd.mtime = d->mtime;
		if(verb && (fskind != Archive && ream == 0))
			fprint(2, "%q\n", new);
		if(dirwstat(newfile, &nd) < 0)
			warn("can't set modes for %q: %r", new);
		nulldir(&nd);
		nd.uid = d->uid;
		dirwstat(newfile, &nd);
	}
}

void
mountkfs(char *name)
{
	char kname[64];

	if(fskind != Kfs)
		return;
	if(name[0])
		snprint(kname, sizeof kname, "/srv/kfs.%s", name);
	else
		strcpy(kname, "/srv/kfs");
	sfd = open(kname, ORDWR);
	if(sfd < 0){
		fprint(2, "can't open %q\n", kname);
		exits("open /srv/kfs");
	}
	if(mount(sfd, -1, "/n/kfs", MREPL|MCREATE, "") < 0){
		fprint(2, "can't mount kfs on /n/kfs\n");
		exits("mount kfs");
	}
	close(sfd);
	strcat(kname, ".cmd");
	sfd = open(kname, ORDWR);
	if(sfd < 0){
		fprint(2, "can't open %q\n", kname);
		exits("open /srv/kfs");
	}
}

void
kfscmd(char *cmd)
{
	char buf[4*1024];
	int n;

	if(fskind != Kfs)
		return;
	if(write(sfd, cmd, strlen(cmd)) != strlen(cmd)){
		fprint(2, "%q: error writing %q: %r", prog, cmd);
		return;
	}
	for(;;){
		n = read(sfd, buf, sizeof buf - 1);
		if(n <= 0)
			return;
		buf[n] = '\0';
		if(strcmp(buf, "done") == 0 || strcmp(buf, "success") == 0)
			return;
		if(strcmp(buf, "unknown command") == 0){
			fprint(2, "%q: command %q not recognized\n", prog, cmd);
			return;
		}
	}
}

void
error(char *fmt, ...)
{
	char buf[1024];
	va_list arg;

	sprint(buf, "%q: %q: ", prog, proto);
	va_start(arg, fmt);
	vseprint(buf+strlen(buf), buf+sizeof(buf), fmt, arg);
	va_end(arg);
	fprint(2, "%s\n", buf);
	kfscmd("disallow");
	kfscmd("sync");
	exits(0);
}

void
warn(char *fmt, ...)
{
	char buf[1024];
	va_list arg;

	sprint(buf, "%q: %q: ", prog, proto);
	va_start(arg, fmt);
	vseprint(buf+strlen(buf), buf+sizeof(buf), fmt, arg);
	va_end(arg);
	fprint(2, "%s\n", buf);
}

void
usage(void)
{
	fprint(2, "usage: %q [-adprvoxUD] [-d root] [-n name] [-s source] [-u users] [-z n] proto ...\n", prog);
	exits("usage");
}
