/* cached-worm file server */
#include "all.h"
#include "io.h"

Map *devmap;

Biobuf bin;
int chatty = 0;
int sfd = -1;

void
machinit(void)
{
	active.exiting = 0;
}

void
panic(char *fmt, ...)
{
	int n;
	va_list arg;
	char buf[PRINTSIZE];

	va_start(arg, fmt);
	n = vseprint(buf, buf + sizeof buf, fmt, arg) - buf;
	va_end(arg);
	buf[n] = '\0';
	fprint(2, "panic: %s\n", buf);
	exit();
}

int
okay(char *quest)
{
	char *ln;

	print("okay to %s? ", quest);
	if ((ln = Brdline(&bin, '\n')) == nil)
		return 0;
	ln[Blinelen(&bin)-1] = '\0';
	if (isascii(*ln) && isupper(*ln))
		*ln = tolower(*ln);
	return *ln == 'y';
}

static void
mapinit(char *mapfile)
{
	int nf;
	char *ln;
	char *fields[2];
	Biobuf *bp;
	Map *map;

	if (mapfile == nil)
		return;
	bp = Bopen(mapfile, OREAD);
	if (bp == nil)
		sysfatal("can't read %s", mapfile);
	devmap = nil;
	while((ln = Brdline(bp, '\n')) != nil) {
		ln[Blinelen(bp)-1] = '\0';
		if(*ln == '\0' || *ln == '#')
			continue;
		nf = tokenize(ln, fields, nelem(fields));
		if(nf != 2)
			continue;
		if(testconfig(fields[0]) != 0) {
			print("bad `from' device %s in %s\n",
				fields[0], mapfile);
			continue;
		}
		map = ialloc(sizeof(Map), 0);
		map->from = strdup(fields[0]);
		map->to =   strdup(fields[1]);
		map->fdev = iconfig(fields[0]);
		map->tdev = nil;
		if(access(map->to, AEXIST) < 0) {
			/*
			 * map->to isn't an existing file, so it had better be
			 * a config string for a device.
			 */
			if(testconfig(fields[1]) == 0)
				map->tdev = iconfig(fields[1]);
		}
		/* else map->to is the replacement file name */
		map->next = devmap;
		devmap = map;
	}
	Bterm(bp);
}

static void
confinit(void)
{
	conf.nmach = 1;

	conf.nuid = 1000;
	conf.nserve = 15;		/* tunable */
	conf.nfile = 30000;
	conf.nlgmsg = 100;
	conf.nsmmsg = 500;

	localconfinit();

	conf.nwpath = conf.nfile*8;
	conf.gidspace = conf.nuid*3;

	cons.flags = 0;

	if (conf.devmap)
		mapinit(conf.devmap);
}

static int
srvfd(char *s, int mode, int sfd)
{
	int fd;
	char buf[32];

	fd = create(s, ORCLOSE|OWRITE, mode);
	if(fd < 0){
		remove(s);
		fd = create(s, ORCLOSE|OWRITE, mode);
		if(fd < 0)
			panic(s);
	}
	sprint(buf, "%d", sfd);
	if(write(fd, buf, strlen(buf)) != strlen(buf))
		panic("srv write");
	return sfd;
}

static void
postservice(char *name)
{
	char buf[3*NAMELEN];
	int p[2];

	if(name == nil || *name == 0)
		panic("no service name");

	/* serve 9p for -s */
	if(sfd >= 0){
		srvchan(sfd, "stdio");
		sfd = -1;
	}

	/* post 9p service */
	if(pipe(p) < 0)
		panic("can't make a pipe");
	snprint(buf, sizeof(buf), "/srv/%s", name);
	srvfd(buf, 0666, p[0]);
	close(p[0]);
	srvchan(p[1], buf);

	/* post cmd service */
	if(pipe(p) < 0)
		panic("can't make a pipe");
	snprint(buf, sizeof(buf), "/srv/%s.cmd", name);
	srvfd(buf, 0660, p[0]);
	close(p[0]);

	/* use it as stdin */
	dup(p[1], 0);
	close(p[1]);
}

/*
 * compute BUFSIZE*(NDBLOCK+INDPERBUF+INDPERBUF²+INDPERBUF³+INDPERBUF⁴)
 * while watching for overflow; in that case, return 0.
 */

static uvlong
adduvlongov(uvlong a, uvlong b)
{
	uvlong r = a + b;

	if (r < a || r < b)
		return 0;
	return r;
}

static uvlong
muluvlongov(uvlong a, uvlong b)
{
	uvlong r = a * b;

	if (a != 0 && r/a != b || r < a || r < b)
		return 0;
	return r;
}

static uvlong
maxsize(void)
{
	int i;
	uvlong max = NDBLOCK, ind = 1;

	for (i = 0; i < NIBLOCK; i++) {
		ind = muluvlongov(ind, INDPERBUF);	/* power of INDPERBUF */
		if (ind == 0)
			return 0;
		max = adduvlongov(max, ind);
		if (max == 0)
			return 0;
	}
	return muluvlongov(max, BUFSIZE);
}

enum {
	INDPERBUF² = ((uvlong)INDPERBUF*INDPERBUF),
	INDPERBUF⁴ = ((uvlong)INDPERBUF²*INDPERBUF²),
};

static void
printsizes(void)
{
	uvlong max = maxsize();

	print("\tblock size = %d; ", RBUFSIZE);
	if (max == 0)
		print("max file size exceeds 2⁶⁴ bytes\n");
	else {
		uvlong offlim = 1ULL << (sizeof(Off)*8 - 1);

		if (max >= offlim)
			max = offlim - 1;
		print("max file size = %,llud\n", (Wideoff)max);
	}
	if (INDPERBUF²/INDPERBUF != INDPERBUF)
		print("overflow computing INDPERBUF²\n");
	if (INDPERBUF⁴/INDPERBUF² != INDPERBUF²)
		print("overflow computing INDPERBUF⁴\n");
	print("\tINDPERBUF = %d, INDPERBUF^4 = %,lld, ", INDPERBUF,
		(Wideoff)INDPERBUF⁴);
	print("CEPERBK = %d\n", CEPERBK);
	print("\tsizeofs: Dentry = %d, Cache = %d\n",
		sizeof(Dentry), sizeof(Cache));
}

void
usage(void)
{
	fprint(2, "usage: %s [ -csC ] [-n service] [ -a ann-str ] [ -m dev-map ] [-f config-dev ]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	int i, nets = 0;
	char *ann, *sname = nil;
	
	rfork(RFNOTEG);
	formatinit();
	machinit();
	conf.confdev = "/dev/sdC0/fscache";
	conf.newcache = 0;

	ARGBEGIN{
	case 'a':			/* announce on this net */
		ann = EARGF(usage());
		if (nets >= Maxnets) {
			fprint(2, "%s: too many networks to announce: %s\n",
				argv0, ann);
			exits("too many nets");
		}
		annstrs[nets++] = ann;
		break;
	case 'n':
		sname = EARGF(usage());
		break;
	case 's':
		dup(0, -1);
		sfd = dup(1, -1);
		close(0);
		if(open("/dev/cons", OREAD) < 0)
			open("#c/cons", OREAD);
		close(1);
		if(open("/dev/cons", OWRITE) < 0)
			open("#c/cons", OWRITE);
		break;
	case 'C':			/* use new, faster cache layout */
		conf.newcache = 1;
		break;
	case 'c':
		conf.configfirst++;
		break;
	case 'f':			/* device / partition / file  */
		conf.confdev = EARGF(usage());
		break;
	case 'm':			/* name device-map file */
		conf.devmap = EARGF(usage());
		break;
	case 'd':
		chatty++;
		break;
	default:
		usage();
		break;
	}ARGEND

	if(argc != 0)
		usage();

	Binit(&bin, 0, OREAD);
	confinit();

	if(chatty){
		print("\nPlan 9 %d-bit cached-worm file server with %d-deep indir blks\n",
			sizeof(Off)*8 - 1, NIBLOCK);
		printsizes();
	}

	qlock(&reflock);
	qunlock(&reflock);
	serveq = newqueue(1000, "9P service");	/* tunable */
	raheadq = newqueue(1000, "readahead");	/* tunable */

	mbinit();
	netinit();
	scsiinit();

	files = ialloc((uintptr)conf.nfile * sizeof(*files), 0);
	for(i=0; i < conf.nfile; i++) {
		qlock(&files[i]);
		qunlock(&files[i]);
	}

	wpaths = ialloc((uintptr)conf.nwpath * sizeof(*wpaths), 0);
	uid = ialloc((uintptr)conf.nuid * sizeof(*uid), 0);
	gidspace = ialloc((uintptr)conf.gidspace * sizeof(*gidspace), 0);

	iobufinit();

	arginit();
	boottime = time(nil);

	sysinit();
	srvinit();

	/*
	 * post filedescriptors to /srv
	 */
	postservice(sname != nil ? sname : service);

	/*
	 * processes to read the console
	 */
	consserve();

	/*
	 * Ethernet i/o processes
	 */
	netstart();

	/*
	 * read ahead processes
	 */
	newproc(rahead, 0, "rah");

	/*
	 * server processes
	 */
	for(i=0; i < conf.nserve; i++)
		newproc(serve, 0, "srv");

	/*
	 * worm "dump" copy process
	 */
	newproc(wormcopy, 0, "wcp");

	/*
	 * "sync" copy process
	 */
	newproc(synccopy, 0, "scp");

	/* success */
	exits(nil);
}

/*
 * read ahead processes.
 * read message from q and then
 * read the device.
 */
int
rbcmp(void *va, void *vb)
{
	Rabuf *ra, *rb;

	ra = *(Rabuf**)va;
	rb = *(Rabuf**)vb;
	if(rb == 0)
		return 1;
	if(ra == 0)
		return -1;
	if(ra->dev > rb->dev)
		return 1;
	if(ra->dev < rb->dev)
		return -1;
	if(ra->addr > rb->addr)
		return 1;
	if(ra->addr < rb->addr)
		return -1;
	return 0;
}

void
rahead(void *)
{
	Rabuf *rb[50];
	Iobuf *p;
	int i, n;

	for (;;) {
		rb[0] = fs_recv(raheadq, 0);
		for(n = 1; n < nelem(rb); n++) {
			if(raheadq->count <= 0)
				break;
			rb[n] = fs_recv(raheadq, 0);
		}
		qsort(rb, n, sizeof rb[0], rbcmp);
		for(i = 0; i < n; i++) {
			if(rb[i] == 0)
				continue;
			p = getbuf(rb[i]->dev, rb[i]->addr, Brd);
			if(p)
				putbuf(p);
			lock(&rabuflock);
			rb[i]->link = rabuffree;
			rabuffree = rb[i];
			unlock(&rabuflock);
		}
	}
}

/*
 * main filesystem server loop.
 * entered by many processes.
 * they wait for message buffers and
 * then process them.
 */
void
serve(void *)
{
	int i;
	Chan *cp;
	Msgbuf *mb;

	for (;;) {
		qlock(&reflock);
		/* read 9P request from a network input process */
		mb = fs_recv(serveq, 0);
		/* fs kernel sets chan in /sys/src/fs/ip/il.c:/^getchan */
		cp = mb->chan;
		if (cp == nil)
			panic("serve: nil mb->chan");
		rlock(&cp->reflock);
		qunlock(&reflock);

		rlock(&mainlock);

		if (mb->data == nil)
			panic("serve: nil mb->data");
		if(cp->protocol != nil){
			/* process the request, generate an answer and reply */
			cp->protocol(mb);
		} else {
			/* do we recognise the protocol in this packet? */
			for(i = 0; fsprotocol[i] != nil; i++)
				if(fsprotocol[i](mb) != 0) {
					cp->protocol = fsprotocol[i];
					break;
				}
			if(cp->protocol == nil && (chatty > 1)){
				fprint(2, "no protocol for message\n");
				hexdump(mb->data, 12);
			}
		}

		mbfree(mb);
		runlock(&mainlock);
		runlock(&cp->reflock);
	}
}

void
exit(void)
{
	lock(&active);
	active.exiting = 1;
	unlock(&active);

	fprint(2, "halted at %T.\n", time(nil));
	postnote(PNGROUP, getpid(), "die");
	exits(nil);
}

enum {
	DUMPTIME = 5,	/* 5 am */
	WEEKMASK = 0,	/* every day (1=sun, 2=mon, 4=tue, etc.) */
};

/*
 * calculate the next dump time.
 * minimum delay is 100 minutes.
 */
Timet
nextdump(Timet t)
{
	Timet nddate = nextime(t+MINUTE(100), DUMPTIME, WEEKMASK);

	if(!conf.nodump && chatty)
		fprint(2, "next dump at %T\n", nddate);
	return nddate;
}

/*
 * process to copy dump blocks from
 * cache to worm. it runs flat out when
 * it gets work, but only looks for
 * work every 10 seconds.
 */
void
wormcopy(void *)
{
	int f, dorecalc = 1;
	Timet dt, t = 0, nddate = 0, ntoytime = 0;
	Filsys *fs;

	for (;;) {
		if (dorecalc) {
			dorecalc = 0;
			t = time(nil);
			nddate = nextdump(t);		/* chatters */
			ntoytime = time(nil);
		}
		dt = time(nil) - t;
		if(dt < 0 || dt > MINUTE(100)) {
			dorecalc = 1;
			continue;
		}
		t += dt;
		f = 0;
		if(t > ntoytime)
			ntoytime = time(nil) + HOUR(1);
		else if(t > nddate) {
			if(!conf.nodump) {
				fprint(2, "automatic dump %T\n", t);
				for(fs=filsys; fs->name; fs++)
					if(fs->dev->type == Devcw)
						cfsdump(fs);
			}
			dorecalc = 1;
		} else {
			rlock(&mainlock);
			for(fs=filsys; fs->name; fs++)
				if(fs->dev->type == Devcw)
					f |= dumpblock(fs->dev);
			runlock(&mainlock);
			if(!f)
				delay(10000);
			wormprobe();
		}
	}
}

/*
 * process to synch blocks
 * it puts out a block/cache-line every second
 * it waits 10 seconds if caught up.
 * in both cases, it takes about 10 seconds
 * to get up-to-date.
 */
void
synccopy(void *)
{
	int f;

	for (;;) {
		rlock(&mainlock);
		f = syncblock();
		runlock(&mainlock);
		if(!f)
			delay(10000);
		else
			delay(1000);
	}
}

Devsize
inqsize(char *file)
{
	int nf;
	char *ln, *end, *data;
	char *fields[4];
	Devsize rv = -1;
	Biobuf *bp;

	data = malloc(strlen(file) + 5 + 1);
	strcpy(data, file);
	end = strstr(data, "/data");
	if (end == nil)
		strcat(data, "/ctl");
	else
		strcpy(end, "/ctl");
	bp = Bopen(data, OREAD);
	if (bp) {
		while (rv < 0 && (ln = Brdline(bp, '\n')) != nil) {
			ln[Blinelen(bp)-1] = '\0';
			nf = tokenize(ln, fields, nelem(fields));
			if (nf == 3 && strcmp(fields[0], "geometry") == 0)
				rv = atoi(fields[2]);
		}
		Bterm(bp);
	}
	free(data);
	return rv;
}
