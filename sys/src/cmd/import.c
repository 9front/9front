#include <u.h>
#include <libc.h>
#include <auth.h>
#include <libsec.h>

enum {
	Encnone,
	Encssl,
	Enctls,
};

static char *encprotos[] = {
	[Encnone] =	"clear",
	[Encssl] =	"ssl",
	[Enctls] = 	"tls",
			nil,
};

char		*keyspec = "";
char		*filterp;
char		*ealgs = "rc4_256 sha1";
int		encproto = Encnone;
char		*aan = "/bin/aan";
char		*anstring  = "tcp!*!0";
AuthInfo 	*ai;
int		debug;
int		doauth = 1;
int		timedout;
int		skiptree;

int	connect(char*, char*);
int	passive(void);
void	catcher(void*, char*);
void	sysfatal(char*, ...);
void	usage(void);
int	filter(int, char *, char *);

static void	mksecret(char *, uchar *);

void
post(char *name, char *envname, int srvfd)
{
	int fd;
	char buf[32];

	fd = create(name, OWRITE, 0600);
	if(fd < 0)
		return;
	snprint(buf, sizeof(buf), "%d", srvfd);
	if(write(fd, buf, strlen(buf)) != strlen(buf))
		sysfatal("srv write: %r");
	close(fd);
	putenv(envname, name);
}

static int
lookup(char *s, char *l[])
{
	int i;

	for (i = 0; l[i] != 0; i++)
		if (strcmp(l[i], s) == 0)
			return i;
	return -1;
}

void
main(int argc, char **argv)
{
	char *mntpt, *srvpost, srvfile[64];
	int backwards = 0, fd, mntflags;

	quotefmtinstall();
	srvpost = nil;
	mntflags = MREPL;
	ARGBEGIN{
	case 'A':
		doauth = 0;
		break;
	case 'a':
		mntflags = MAFTER;
		break;
	case 'b':
		mntflags = MBEFORE;
		break;
	case 'c':
		mntflags |= MCREATE;
		break;
	case 'C':
		mntflags |= MCACHE;
		break;
	case 'd':
		debug++;
		break;
	case 'f':
		/* ignored but allowed for compatibility */
		break;
	case 'E':
		if ((encproto = lookup(EARGF(usage()), encprotos)) < 0)
			usage();
		break;
	case 'e':
		ealgs = EARGF(usage());
		if(*ealgs == 0 || strcmp(ealgs, "clear") == 0)
			ealgs = nil;
		break;
	case 'k':
		keyspec = EARGF(usage());
		break;
	case 'p':
		filterp = aan;
		break;
	case 'n':
		anstring = EARGF(usage());
		break;
	case 's':
		srvpost = EARGF(usage());
		break;
	case 'B':
		backwards = 1;
		break;
	case 'z':
		skiptree = 1;
		break;
	default:
		usage();
	}ARGEND;

	mntpt = 0;		/* to shut up compiler */
	if(backwards){
		switch(argc) {
		default:
			mntpt = argv[0];
			break;
		case 0:
			usage();
		}
	} else {
		switch(argc) {
		case 2:
			mntpt = argv[1];
			break;
		case 3:
			mntpt = argv[2];
			break;
		default:
			usage();
		}
	}

	if (encproto == Enctls)
		sysfatal("%s: tls has not yet been implemented", argv[0]);

	notify(catcher);
	alarm(60*1000);

	if (backwards)
		fd = passive();
	else
		fd = connect(argv[0], argv[1]);

	fprint(fd, "impo %s %s\n", filterp? "aan": "nofilter", encprotos[encproto]);

	if (encproto != Encnone && ealgs && ai) {
		uchar key[16], digest[SHA1dlen];
		char fromclientsecret[21];
		char fromserversecret[21];
		int i;

		if(ai->nsecret < 8)
			sysfatal("secret too small to ssl");
		memmove(key+4, ai->secret, 8);

		/* exchange random numbers */
		srand(truerand());
		for(i = 0; i < 4; i++)
			key[i] = rand();
		if(write(fd, key, 4) != 4)
			sysfatal("can't write key part: %r");
		if(readn(fd, key+12, 4) != 4)
			sysfatal("can't read key part: %r");

		/* scramble into two secrets */
		sha1(key, sizeof(key), digest, nil);
		mksecret(fromclientsecret, digest);
		mksecret(fromserversecret, digest+10);

		if (filterp)
			fd = filter(fd, filterp, backwards ? nil : argv[0]);

		/* set up encryption */
		procsetname("pushssl");
		fd = pushssl(fd, ealgs, fromclientsecret, fromserversecret, nil);
		if(fd < 0)
			sysfatal("can't establish ssl connection: %r");
	}
	else if (filterp)
		fd = filter(fd, filterp, backwards ? nil : argv[0]);

	if(ai)
		auth_freeAI(ai);

	if(srvpost){
		snprint(srvfile, sizeof(srvfile), "/srv/%s", srvpost);
		remove(srvfile);
		post(srvfile, srvpost, fd);
	}
	procsetname("mount on %s", mntpt);
	if(mount(fd, -1, mntpt, mntflags, "") < 0)
		sysfatal("can't mount %s: %r", argv[1]);
	alarm(0);

	if(backwards && argc > 1){
		exec(argv[1], &argv[1]);
		sysfatal("exec: %r");
	}
	exits(0);
}

void
catcher(void*, char *msg)
{
	timedout = 1;
	if(strcmp(msg, "alarm") == 0)
		noted(NCONT);
	noted(NDFLT);
}

int
connect(char *system, char *tree)
{
	char buf[ERRMAX], dir[128], *na;
	int fd, n;

	na = netmkaddr(system, 0, "exportfs");
	procsetname("dial %s", na);
	if((fd = dial(na, 0, dir, 0)) < 0)
		sysfatal("can't dial %s: %r", system);

	if(doauth){
		procsetname("auth_proxy auth_getkey proto=p9any role=client %s", keyspec);
		ai = auth_proxy(fd, auth_getkey, "proto=p9any role=client %s", keyspec);
		if(ai == nil)
			sysfatal("%r: %s", system);
	}

	if(!skiptree){
		procsetname("writing tree name %s", tree);
		n = write(fd, tree, strlen(tree));
		if(n < 0)
			sysfatal("can't write tree: %r");

		strcpy(buf, "can't read tree");

		procsetname("awaiting OK for %s", tree);
		n = read(fd, buf, sizeof buf - 1);
		if(n!=2 || buf[0]!='O' || buf[1]!='K'){
			if (timedout)
				sysfatal("timed out connecting to %s", na);
			buf[sizeof buf - 1] = '\0';
			sysfatal("bad remote tree: %s", buf);
		}
	}
	return fd;
}

int
passive(void)
{
	int fd;

	/*
	 * Ignore doauth==0 on purpose.  Is it useful here?
	 */

	procsetname("auth_proxy auth_getkey proto=p9any role=server");
	ai = auth_proxy(0, auth_getkey, "proto=p9any role=server");
	if(ai == nil)
		sysfatal("auth_proxy: %r");
	if(auth_chuid(ai, nil) < 0)
		sysfatal("auth_chuid: %r");
	putenv("service", "import");

	fd = dup(0, -1);
	close(0);
	open("/dev/null", ORDWR);
	close(1);
	open("/dev/null", ORDWR);

	return fd;
}

void
usage(void)
{
	fprint(2, "usage: import [-abcC] [-A] [-E clear|ssl|tls] "
"[-e 'crypt auth'|clear] [-k keypattern] [-p] [-n address ] [-z] host remotefs [mountpoint]\n");
	exits("usage");
}

int
filter(int fd, char *cmd, char *host)
{
	char addr[128], buf[256], *s, *file, *argv[16];
	int lfd, p[2], len, argc;

	if(host == nil){
		/* Get a free port and post it to the client. */
		if (announce(anstring, addr) < 0)
			sysfatal("filter: Cannot announce %s: %r", anstring);

		snprint(buf, sizeof(buf), "%s/local", addr);
		if ((lfd = open(buf, OREAD)) < 0)
			sysfatal("filter: Cannot open %s: %r", buf);
		if ((len = read(lfd, buf, sizeof buf - 1)) < 0)
			sysfatal("filter: Cannot read %s: %r", buf);
		close(lfd);
		buf[len] = '\0';
		if ((s = strchr(buf, '\n')) != nil)
			len = s - buf;
		if (write(fd, buf, len) != len) 
			sysfatal("filter: cannot write port; %r");
	} else {
		/* Read address string from connection */
		if ((len = read(fd, buf, sizeof buf - 1)) < 0)
			sysfatal("filter: cannot write port; %r");
		buf[len] = '\0';

		if ((s = strrchr(buf, '!')) == nil)
			sysfatal("filter: illegally formatted port %s", buf);
		strecpy(addr, addr+sizeof(addr), netmkaddr(host, "tcp", s+1));
		strecpy(strrchr(addr, '!'), addr+sizeof(addr), s);
	}

	if(debug)
		fprint(2, "filter: %s\n", addr);

	snprint(buf, sizeof(buf), "%s", cmd);
	argc = tokenize(buf, argv, nelem(argv)-3);
	if (argc == 0)
		sysfatal("filter: empty command");

	if(host != nil)
		argv[argc++] = "-c";
	argv[argc++] = addr;
	argv[argc] = nil;

	file = argv[0];
	if((s = strrchr(argv[0], '/')) != nil)
		argv[0] = s+1;

	if(pipe(p) < 0)
		sysfatal("pipe: %r");

	switch(rfork(RFNOWAIT|RFPROC|RFMEM|RFFDG|RFREND)) {
	case -1:
		sysfatal("filter: rfork; %r\n");
	case 0:
		close(fd);
		if (dup(p[0], 1) < 0)
			sysfatal("filter: Cannot dup to 1; %r");
		if (dup(p[0], 0) < 0)
			sysfatal("filter: Cannot dup to 0; %r");
		close(p[0]);
		close(p[1]);
		exec(file, argv);
		sysfatal("filter: exec; %r");
	default:
		dup(p[1], fd);
		close(p[0]);
		close(p[1]);
	}
	return fd;
}

static void
mksecret(char *t, uchar *f)
{
	sprint(t, "%2.2ux%2.2ux%2.2ux%2.2ux%2.2ux%2.2ux%2.2ux%2.2ux%2.2ux%2.2ux",
		f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9]);
}
