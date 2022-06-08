#include <u.h>
#include <libc.h>
#include <auth.h>

static int debug;

static void
binderr(char *new, char *old, int flag)
{
	char dash[4] = { '-' };

	if(debug){
		if(flag & MCREATE){
			dash[2] = 'c';
			flag &= ~MCREATE;
		}
		switch(flag){
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
		print("bind %s %s %s\n", dash, new, old);
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

	snprint(rootskel, sizeof rootskel, "#zd/newroot.%d", getpid());
	binderr(rootskel, "/", MBEFORE);

	newroot = rootskel + strlen("#zd");

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
				snprint(skel, sizeof skel, "#zd/%s", parts[i]);
			else
				snprint(skel, sizeof skel, "#zf/%s", parts[i]);
			free(d);
			binderr(skel, dir, MBEFORE);
		}
		binderr(names[j], targ, flags[j]);
	}
	binderr(newroot, "/", MREPL);
}

void
usage(void)
{
	fprint(2, "usage %s: [ -d ] [ -r file ] [ -c dir ] [ -e devs ] cmd args...\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *b;
	Dir *d;
	char devs[1024];
	int dfd;
	char *parts[256];
	int mflags[256];
	int nparts;

	nparts = 0;
	memset(devs, 0, sizeof devs);
	ARGBEGIN{
	case 'd':
		debug = 1;
		break;
	case 'r':
		parts[nparts] = EARGF(usage());
		mflags[nparts++] = MREPL;
		break;
	case 'c':
		parts[nparts] = EARGF(usage());
		mflags[nparts++] = MCREATE|MREPL;
		break;
	case 'e':
		snprint(devs, sizeof devs, "%s%s", devs, EARGF(usage()));
		break;
	default:
		usage();
		break;
	}ARGEND
	if(argc == 0)
		usage();

	b = argv[0];
	d = dirstat(b);
	if(d == nil){
		b = smprint("/bin/%s", b);
		d = dirstat(b);
		if(d == nil)
			sysfatal("could not stat %s %r", argv[0]);
	}
	free(d);
	parts[nparts] = b;
	mflags[nparts++] = MREPL;
	argv[0] = b;

	rfork(RFNAMEG|RFENVG);
	dfd = open("/dev/drivers", OWRITE|OCEXEC);
	if(dfd < 0)
		sysfatal("could not /dev/drivers: %r");

	resolvenames(parts, nparts);

	if(procsetuser("none") < 0)
		sysfatal("cant become none: %r");
	putenv("user", "none");

	sandbox(parts, mflags, nparts);
	
	if(debug)
		print("chdev %s\n", devs);

	if(devs[0] != '\0'){
		if(fprint(dfd, "chdev & %s", devs) <= 0)
			sysfatal("could not write chdev: %r");
	} else {
		if(fprint(dfd, "chdev ~") <= 0)
			sysfatal("could not write chdev: %r");
	}
	close(dfd);
	exec(argv[0], argv);
}
