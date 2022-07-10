#include <u.h>
#include <libc.h>
#include <auth.h>

static int debug;

static void
binderr(char *new, char *old, int flag)
{
	char dash[4] = { '-' };

	if(debug){
		if(flag & MCREATE)
			dash[2] = 'c';

		switch(flag){
		case MCREATE|MREPL:
		case MREPL:
			dash[0] = ' ';
			if(dash[2] == 'c')
				dash[1] = '-';
			else
				dash[1] = ' ';
			break;
		case MBEFORE:
			dash[1] = 'b';
			break;
		case MAFTER:
			dash[1] = 'a';
			break;
		}
		fprint(2, "bind %s %s %s\n", dash, new, old);
	}
	if(bind(new, old, flag) < 0)
		sysfatal("bind: %r");
}

static void
resolvenames(char **names, int nname)
{
	int i;
	char buf[8192];
	int fd;

	fd = open(".", OREAD|OCEXEC);
	if(fd < 0)
		sysfatal("could not open .: %r");
	fd2path(fd, buf, sizeof buf);
	for(i = 0; i < nname; i++){
		if(names[i] == nil)
			continue;
		cleanname(names[i]);
		switch(names[i][0]){
		case '#':
		case '/':
			break;
		default:
			names[i] = cleanname(smprint("%s/%s", buf, names[i]));
		}	
	}
	close(fd);
}

static void
sandbox(char **names, int *flags, int nname)
{
	char *parts[32];
	char rootskel[128];
	char src[8192], targ[8192], dir[8192], skel[8192];
	char name[8192];
	char *newroot;
	Dir *d;
	int i, j, n;

	snprint(rootskel, sizeof rootskel, "/mnt/d/newroot.%d", getpid());
	binderr(rootskel, "/", MBEFORE);

	newroot = rootskel + strlen("/mnt/d");

	for(j = 0; j < nname; j++){
		if(names[j] == nil)
			continue;
		utfecpy(name, &name[sizeof name-1], names[j]);
		n = gettokens(name, parts, nelem(parts), "/");
		utfecpy(targ, &targ[sizeof targ-1], newroot);
		memset(src, 0, sizeof src);
		for(i = 0; i < n; i++){
			utfecpy(dir, &dir[sizeof dir-1], targ);
			snprint(targ, sizeof targ, "%s/%s", targ, parts[i]);
			snprint(src, sizeof src, "%s/%s", src, parts[i]);
			d = dirstat(targ);
			if(d != nil){
				free(d);
				continue;
			}
			d = dirstat(src);
			if(d == nil)
				continue;
			if(d->mode & DMDIR)
				snprint(skel, sizeof skel, "/mnt/d/%s", parts[i]);
			else
				snprint(skel, sizeof skel, "/mnt/f/%s", parts[i]);
			free(d);
			binderr(skel, dir, MBEFORE);
		}
		binderr(names[j], targ, flags[j]);
	}
	binderr(newroot, "/", MREPL);
}

void
skelfs(void)
{
	int p[2];
	int dfd;

	pipe(p);
	switch(rfork(RFFDG|RFREND|RFPROC|RFNAMEG)){
	case -1:
		sysfatal("fork");
	case 0:
		close(p[1]);
		dup(p[0], 0);
		dup(p[0], 1);
		execl("/bin/skelfs", "skelfs", debug > 1 ? "-Di" : "-i", nil);
		sysfatal("exec /bin/skelfs: %r");
	}
	close(p[0]);
	dfd = dup(p[1], -1);
	if(mount(p[1], -1, "/mnt/f", MREPL, "file") < 0)
		sysfatal("/mnt/f mount setup: %r");
	if(mount(dfd, -1, "/mnt/d", MREPL, "dir") < 0)
		sysfatal("/mnt/d mount setup: %r");
}

static char *parts[256];
static int  mflags[nelem(parts)];
static int  nparts;
static char *rc[] = { "/bin/rc", nil , nil};

static void
push(char *path, int flag)
{
	if(nparts == nelem(parts))
		sysfatal("component overflow");
	parts[nparts] = path;
	mflags[nparts++] = flag;
}

void
usage(void)
{
	fprint(2, "usage %s: [ -d ] [ -r file ] [ -c dir ] [ -e devs ] [ -. path ] cmd args...\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char devs[1024];
	int dfd;
	char *path;
	char *a;
	int sflag;

	nparts = 0;
	path = "/";
	memset(devs, 0, sizeof devs);
	sflag = 0;
	ARGBEGIN{
	case 'D':
		debug++;
	case 'd':
		debug++;
		break;
	case 'r':
		a = EARGF(usage());
		push(a, MREPL);
		break;
	case 'c':
		a = EARGF(usage());
		push(a, MREPL|MCREATE);
		break;
	case 'e':
		snprint(devs, sizeof devs, "%s%s", devs, EARGF(usage()));
		break;
	case '.':
		path = EARGF(usage());
		break;
	case 's':
		sflag = 1;
		break;
	default:
		usage();
		break;
	}ARGEND

	if(argc == 0)
		usage();

	if(sflag){
		snprint(devs, sizeof devs, "%s%s", devs, "|d");
		push("/srv", MREPL|MCREATE);
		push("/env", MREPL|MCREATE);
		push("/rc", MREPL);
		push("/bin", MREPL);
		push(argv[0], MREPL);
		rc[1] = argv[0];
		argv = rc;
	} else {
		if(access(argv[0], AEXIST) == -1){
			if((argv[0] = smprint("/bin/%s", argv[0])) == nil)
				sysfatal("smprint: %r");
			if(access(argv[0], AEXIST) == -1)
				sysfatal("could not stat %s %r", argv[0]);
		}
		push(argv[0], MREPL);
	}

	rfork(RFNAMEG|RFFDG);
	skelfs();
	dfd = open("/dev/drivers", OWRITE|OCEXEC);
	if(dfd < 0)
		sysfatal("could not /dev/drivers: %r");

	resolvenames(parts, nparts);
	sandbox(parts, mflags, nparts);
	
	if(debug)
		fprint(2, "chdev %s\n", devs);

	if(devs[0] != '\0'){
		if(fprint(dfd, "chdev & %s", devs) <= 0)
			sysfatal("could not write chdev: %r");
	} else {
		if(fprint(dfd, "chdev ~") <= 0)
			sysfatal("could not write chdev: %r");
	}
	close(dfd);

	if(chdir(path) < 0)
		sysfatal("can not cd to %s", path);
	exec(argv[0], argv);
	sysfatal("exec: %r");
}
