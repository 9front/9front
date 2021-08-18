/*
 * oexportfs - legacy exportfs for cpu and import
 */
#include <u.h>
#include <libc.h>
#include <auth.h>
#include <fcall.h>
#include <libsec.h>
#define Extern
#include "exportfs.h"

enum {
	Encnone,
	Encssl,
	Enctls,
};

int	srvfd = -1;
int	nonone = 1;
char	*filterp;
char	*ealgs = "rc4_256 sha1";
char	*aanfilter = "/bin/aan";
int	encproto = Encnone;
int	readonly;

static char *anstring  = "tcp!*!0";

static void
filter(int fd, char *cmd, char *host)
{
	char addr[128], buf[256], *s, *file, *argv[16];
	int lfd, p[2], len, argc;

	if(host == nil){
		/* Get a free port and post it to the client. */
		if (announce(anstring, addr) < 0)
			fatal("filter: Cannot announce %s: %r", anstring);

		snprint(buf, sizeof(buf), "%s/local", addr);
		if ((lfd = open(buf, OREAD)) < 0)
			fatal("filter: Cannot open %s: %r", buf);
		if ((len = read(lfd, buf, sizeof buf - 1)) < 0)
			fatal("filter: Cannot read %s: %r", buf);
		close(lfd);
		buf[len] = '\0';
		if ((s = strchr(buf, '\n')) != nil)
			len = s - buf;
		if (write(fd, buf, len) != len) 
			fatal("filter: cannot write port; %r");
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

	DEBUG(DFD, "filter: %s\n", addr);

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
		fatal("filter: rfork; %r\n");
	case 0:
		close(fd);
		if (dup(p[0], 1) < 0)
			fatal("filter: Cannot dup to 1; %r");
		if (dup(p[0], 0) < 0)
			fatal("filter: Cannot dup to 0; %r");
		close(p[0]);
		close(p[1]);
		exec(file, argv);
		fatal("filter: exec; %r");
	default:
		dup(p[1], fd);
		close(p[0]);
		close(p[1]);
	}
}

static void
mksecret(char *t, uchar *f)
{
	sprint(t, "%2.2ux%2.2ux%2.2ux%2.2ux%2.2ux%2.2ux%2.2ux%2.2ux%2.2ux%2.2ux",
		f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9]);
}

void
usage(void)
{
	fprint(2, "usage: %s [-adnsR] [-f dbgfile] [-m msize] [-r root] "
		"[-S srvfile] [-e 'crypt hash'] [-P exclusion-file] "
		"[-A announce-string] [-B address]\n", argv0);
	fatal("usage");
}

void
main(int argc, char **argv)
{
	char buf[ERRMAX], ebuf[ERRMAX], initial[4], *ini, *srvfdfile;
	char *dbfile, *srv, *na, *nsfile, *keyspec;
	int doauth, n, fd;
	AuthInfo *ai;
	Fsrpc *r;

	dbfile = "/tmp/exportdb";
	srv = nil;
	srvfd = -1;
	srvfdfile = nil;
	na = nil;
	nsfile = nil;
	keyspec = "";
	doauth = 0;

	ai = nil;
	ARGBEGIN{
	case 'a':
		doauth = 1;
		break;

	case 'd':
		dbg++;
		break;

	case 'e':
		ealgs = EARGF(usage());
		if(*ealgs == 0 || strcmp(ealgs, "clear") == 0)
			ealgs = nil;
		break;

	case 'f':
		dbfile = EARGF(usage());
		break;

	case 'k':
		keyspec = EARGF(usage());
		break;

	case 'm':
		messagesize = strtoul(EARGF(usage()), nil, 0);
		break;

	case 'n':
		nonone = 0;
		break;

	case 'r':
		srv = EARGF(usage());
		break;

	case 's':
		srv = "/";
		break;

	case 'A':
		anstring = EARGF(usage());
		break;

	case 'B':
		na = EARGF(usage());
		break;

	case 'F':
		/* accepted but ignored, for backwards compatibility */
		break;

	case 'N':
		nsfile = EARGF(usage());
		break;

	case 'P':
		patternfile = EARGF(usage());
		break;

	case 'R':
		readonly = 1;
		break;

	case 'S':
		if(srvfdfile != nil)
			usage();
		srvfdfile = EARGF(usage());
		break;

	default:
		usage();
	}ARGEND
	USED(argc, argv);

	if(na == nil && doauth){
		/*
		 * We use p9any so we don't have to visit this code again, with the
		 * cost that this code is incompatible with the old world, which
		 * requires p9sk2. (The two differ in who talks first, so compatibility
		 * is awkward.)
		 */
		ai = auth_proxy(0, auth_getkey, "proto=p9any role=server %s", keyspec);
		if(ai == nil)
			fatal("auth_proxy: %r");
		if(nonone && strcmp(ai->cuid, "none") == 0)
			fatal("exportfs by none disallowed");
		if(auth_chuid(ai, nsfile) < 0)
			fatal("auth_chuid: %r");
		else {	/* chown network connection */
			Dir nd;
			nulldir(&nd);
			nd.mode = 0660;
			nd.uid = ai->cuid;
			dirfwstat(0, &nd);
		}
		putenv("service", "exportfs");
	}

	if(srvfdfile != nil){
		if((srvfd = open(srvfdfile, ORDWR)) < 0)
			fatal("open %s: %r", srvfdfile);
	}

	if(na != nil){
		if(srv == nil)
			fatal("-B requires -s");

		if((fd = dial(netmkaddr(na, 0, "importfs"), 0, 0, 0)) < 0)
			fatal("can't dial %s: %r", na);
	
		ai = auth_proxy(fd, auth_getkey, "proto=p9any role=client %s", keyspec);
		if(ai == nil)
			fatal("%r: %s", na);

		dup(fd, 0);
		dup(fd, 1);
		close(fd);
	}

	exclusions();

	if(dbg) {
		n = create(dbfile, OWRITE|OTRUNC, 0666);
		dup(n, DFD);
		close(n);
	}

	if(srvfd >= 0 && srv != nil){
		fprint(2, "%s: -S cannot be used with -r or -s\n", argv0);
		usage();
	}

	DEBUG(DFD, "%s: started\n", argv0);

	rfork(RFNOTEG|RFREND);

	if(messagesize == 0){
		messagesize = iounit(0);
		if(messagesize == 0)
			messagesize = 8192+IOHDRSZ;
	}
	fhash = emallocz(sizeof(Fid*)*FHASHSIZE);

	fmtinstall('F', fcallfmt);

	/*
	 * Get tree to serve from network connection,
	 * check we can get there and ack the connection
 	 */
	if(srvfd != -1) {
		/* do nothing */
	}
	else if(srv != nil) {
		if(chdir(srv) < 0) {
			ebuf[0] = '\0';
			errstr(ebuf, sizeof ebuf);
			DEBUG(DFD, "chdir(\"%s\"): %s\n", srv, ebuf);
			mounterror(ebuf);
		}
		DEBUG(DFD, "invoked as server for %s", srv);
		strncpy(buf, srv, sizeof buf);
	}
	else {
		buf[0] = 0;
		n = read(0, buf, sizeof(buf)-1);
		if(n < 0) {
			errstr(buf, sizeof buf);
			fprint(0, "read(0): %s\n", buf);
			DEBUG(DFD, "read(0): %s\n", buf);
			exits(buf);
		}
		buf[n] = 0;
		if(chdir(buf) < 0) {
			errstr(ebuf, sizeof ebuf);
			fprint(0, "chdir(%d:\"%s\"): %s\n", n, buf, ebuf);
			DEBUG(DFD, "chdir(%d:\"%s\"): %s\n", n, buf, ebuf);
			exits(ebuf);
		}
	}

	DEBUG(DFD, "\niniting root\n");
	initroot();

	DEBUG(DFD, "%s: %s\n", argv0, buf);

	if(srv == nil && srvfd == -1 && write(0, "OK", 2) != 2)
		fatal("open ack write");

	ini = initial;
	n = readn(0, initial, sizeof(initial));
	if(n == 0)
		fatal(nil);	/* port scan or spurious open/close on exported /srv file (unmount) */
	if(n < sizeof(initial))
		fatal("can't read initial string: %r");

	if(memcmp(ini, "impo", 4) == 0) {
		char buf[128], *p, *args[3];

		ini = nil;
		p = buf;
		for(;;){
			if((n = read(0, p, 1)) < 0)
				fatal("can't read impo arguments: %r");
			if(n == 0)
				fatal("connection closed while reading arguments");
			if(*p == '\n') 
				*p = '\0';
			if(*p++ == '\0')
				break;
			if(p >= buf + sizeof(buf))
				fatal("import parameters too long");
		}
		
		if(tokenize(buf, args, nelem(args)) != 2)
			fatal("impo arguments invalid: impo%s...", buf);

		if(strcmp(args[0], "aan") == 0)
			filterp = aanfilter;
		else if(strcmp(args[0], "nofilter") != 0)
			fatal("import filter argument unsupported: %s", args[0]);

		if(strcmp(args[1], "ssl") == 0)
			encproto = Encssl;
		else if(strcmp(args[1], "tls") == 0)
			encproto = Enctls;
		else if(strcmp(args[1], "clear") != 0)
			fatal("import encryption proto unsupported: %s", args[1]);

		if(encproto == Enctls)
			fatal("%s: tls has not yet been implemented", argv[0]);
	}

	if(encproto != Encnone && ealgs != nil && ai != nil) {
		uchar key[16], digest[SHA1dlen];
		char fromclientsecret[21];
		char fromserversecret[21];
		int i;

		if(ai->nsecret < 8)
			fatal("secret too small for ssl");
		memmove(key+4, ai->secret, 8);

		/* exchange random numbers */
		srand(truerand());
		for(i = 0; i < 4; i++)
			key[i+12] = rand();

		if(ini != nil) 
			fatal("Protocol botch: old import");
		if(readn(0, key, 4) != 4)
			fatal("can't read key part; %r");

		if(write(0, key+12, 4) != 4)
			fatal("can't write key part; %r");

		/* scramble into two secrets */
		sha1(key, sizeof(key), digest, nil);
		mksecret(fromclientsecret, digest);
		mksecret(fromserversecret, digest+10);

		if(filterp != nil)
			filter(0, filterp, na);

		switch(encproto) {
		case Encssl:
			fd = pushssl(0, ealgs, fromserversecret, fromclientsecret, nil);
			if(fd < 0)
				fatal("can't establish ssl connection: %r");
			if(fd != 0){
				dup(fd, 0);
				close(fd);
			}
			break;
		case Enctls:
		default:
			fatal("Unsupported encryption protocol");
		}
	}
	else if(filterp != nil) {
		if(ini != nil)
			fatal("Protocol botch: don't know how to deal with this");
		filter(0, filterp, na);
	}
	dup(0, 1);

	if(ai != nil)
		auth_freeAI(ai);

	if(ini != nil){
		extern void (*fcalls[])(Fsrpc*);

		r = getsbuf();
		memmove(r->buf, ini, BIT32SZ);
		n = GBIT32(r->buf);
		if(n <= BIT32SZ || n > messagesize)
			fatal("bad length in 9P2000 message header");
		n -= BIT32SZ;
		if(readn(0, r->buf+BIT32SZ, n) != n)
			fatal(nil);
		n += BIT32SZ;

		if(convM2S(r->buf, n, &r->work) != n)
			fatal("convM2S format error");
		DEBUG(DFD, "%F\n", &r->work);
		(fcalls[r->work.type])(r);
	}
	io();
}
