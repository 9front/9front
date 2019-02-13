/*
 * ipconfig - configure parameters of an ip stack
 */
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ip.h>
#include <ndb.h>
#include "ipconfig.h"
#include "../dhcp.h"

#include <libsec.h> /* genrandom() */

Conf	conf;
int	myifc = -1;
int	beprimary = -1;
int	noconfig;
Ipifc	*ifc;
Ctl	*firstctl, **ctll = &firstctl;

int	debug;
int	dolog;

int	plan9 = 1;
int	Oflag;
int	rflag;

int	dodhcp;
int	nodhcpwatch;
int	sendhostname;
char	*ndboptions;

int	ipv6auto;
int	dupl_disc = 1;		/* flag: V6 duplicate neighbor discovery */

int	dondbconfig;
char	*dbfile;

static char logfile[] = "ipconfig";

static void	binddevice(void);
static void	controldevice(void);
extern void	pppbinddev(void);

static void	doadd(void);
static void	doremove(void);
static void	dounbind(void);
static void	ndbconfig(void);

static int	Ufmt(Fmt*);
#pragma varargck type "U" char*

void
usage(void)
{
	fprint(2, "usage: %s [-6dDGnNOpPruX][-b baud][-c ctl]* [-g gw]"
		"[-h host][-m mtu]\n"
		"\t[-f dbfile][-x mtpt][-o dhcpopt] type dev [verb] [laddr [mask "
		"[raddr [fs [auth]]]]]\n", argv0);
	exits("usage");
}

static void
init(void)
{
	srand(truerand());

	fmtinstall('H', encodefmt);
	fmtinstall('E', eipfmt);
	fmtinstall('I', eipfmt);
	fmtinstall('M', eipfmt);
	fmtinstall('V', eipfmt);
	fmtinstall('U', Ufmt);
	nsec();			/* make sure time file is open before forking */

	conf.cfd = -1;
	conf.rfd = -1;

	setnetmtpt(conf.mpoint, sizeof conf.mpoint, nil);
	conf.cputype = getenv("cputype");
	if(conf.cputype == nil)
		conf.cputype = "386";

	v6paraminit(&conf);

	dhcpinit();
}

void
warning(char *fmt, ...)
{
	char buf[1024];
	va_list arg;

	va_start(arg, fmt);
	vseprint(buf, buf + sizeof buf, fmt, arg);
	va_end(arg);
	if (dolog)
		syslog(0, logfile, "%s", buf);
	else
		fprint(2, "%s: %s\n", argv0, buf);
}

static void
parsenorm(int argc, char **argv)
{
	switch(argc){
	case 5:
		 if (parseip(conf.auth, argv[4]) == -1)
			usage();
		/* fall through */
	case 4:
		 if (parseip(conf.fs, argv[3]) == -1)
			usage();
		/* fall through */
	case 3:
		 if (parseip(conf.raddr, argv[2]) == -1)
			usage();
		/* fall through */
	case 2:
		if (strcmp(argv[1], "0") != 0){
			if (parseipandmask(conf.laddr, conf.mask, argv[0], argv[1]) == -1)
				usage();
			break;
		}
		/* fall through */
	case 1:
		 if (parseip(conf.laddr, argv[0]) == -1)
			usage();
		/* fall through */
	case 0:
		break;
	default:
		usage();
	}
}

static char*
finddev(char *dir, char *name, char *dev)
{
	int fd, i, nd;
	Dir *d;

	fd = open(dir, OREAD);
	if(fd >= 0){
		d = nil;
		nd = dirreadall(fd, &d);
		close(fd);
		for(i=0; i<nd; i++){
			if(strncmp(d[i].name, name, strlen(name)))
				continue;
			if(strstr(d[i].name, "ctl") != nil)
				continue;	/* ignore ctl files */
			dev = smprint("%s/%s", dir, d[i].name);
			break;
		}
		free(d);
	}
	return dev;
}

/* look for an action */
static int
parseverb(char *name)
{
	static char *verbs[] = {
		[Vadd]		"add",
		[Vremove]	"remove",
		[Vunbind]	"unbind",
		[Vether]	"ether",
		[Vgbe]		"gbe",
		[Vppp]		"ppp",
		[Vloopback]	"loopback",
		[Vaddpref6]	"add6",
		[Vra6]		"ra6",
		[Vtorus]	"torus",
		[Vtree]		"tree",
		[Vpkt]		"pkt",
	};
	int i;

	for(i = 0; i < nelem(verbs); i++)
		if(verbs[i] != nil && strcmp(name, verbs[i]) == 0)
			return i;
	return -1;
}

static int
parseargs(int argc, char **argv)
{
	char *p;
	int action, verb;

	/* default to any host name we already have */
	if(*conf.hostname == 0){
		p = getenv("sysname");
		if(p == nil || *p == 0)
			p = sysname();
		if(p != nil)
			utf2idn(p, conf.hostname, sizeof(conf.hostname));
	}

	/* defaults */
	conf.type = "ether";
	conf.dev = nil;
	action = Vadd;

	/* get optional medium and device */
	if (argc > 0){
		verb = parseverb(*argv);
		switch(verb){
		case Vether:
		case Vgbe:
		case Vppp:
		case Vloopback:
		case Vtorus:
		case Vtree:
		case Vpkt:
			conf.type = *argv++;
			argc--;
			if(argc > 0){
				conf.dev = *argv++;
				argc--;
			} else if(verb == Vppp)
				conf.dev = finddev("/dev", "eia", "/dev/eia0");
			break;
		}
	}
	if(conf.dev == nil)
		conf.dev = finddev(conf.mpoint, "ether", "/net/ether0");

	/* get optional verb */
	if (argc > 0){
		verb = parseverb(*argv);
		switch(verb){
		case Vether:
		case Vgbe:
		case Vppp:
		case Vloopback:
		case Vtorus:
		case Vtree:
		case Vpkt:
			sysfatal("medium %s already specified", conf.type);
		case Vadd:
		case Vremove:
		case Vunbind:
		case Vaddpref6:
		case Vra6:
			argv++;
			argc--;
			action = verb;
			break;
		}
	}

	/* get verb-dependent arguments */
	switch (action) {
	case Vadd:
	case Vremove:
	case Vunbind:
		parsenorm(argc, argv);
		break;
	case Vaddpref6:
		parse6pref(argc, argv);
		break;
	case Vra6:
		parse6ra(argc, argv);
		break;
	}
	return action;
}

static int
findifc(char *net, char *dev)
{
	Ipifc *nifc;

	ifc = readipifc(net, ifc, -1);
	for(nifc = ifc; nifc != nil; nifc = nifc->next)
		if(strcmp(nifc->dev, dev) == 0)
			return nifc->index;

	return -1;
}

static int
isether(void)
{
	return strcmp(conf.type, "ether") == 0 || strcmp(conf.type, "gbe") == 0;
}

/* create a client id */
static void
mkclientid(void)
{
	if(isether() && myetheraddr(conf.hwa, conf.dev) == 0){
		conf.hwalen = 6;
		conf.hwatype = 1;
		conf.cid[0] = conf.hwatype;
		memmove(&conf.cid[1], conf.hwa, conf.hwalen);
		conf.cidlen = conf.hwalen+1;
	} else {
		conf.hwatype = -1;
		snprint((char*)conf.cid, sizeof conf.cid,
			"plan9_%ld.%d", lrand(), getpid());
		conf.cidlen = strlen((char*)conf.cid);
		genrandom(conf.hwa, sizeof(conf.hwa));
	}
	ea2lla(conf.lladdr, conf.hwa);
}

void
main(int argc, char **argv)
{
	int action;
	Ctl *cp;

	init();
	ARGBEGIN {
	case '6': 			/* IPv6 auto config */
		ipv6auto = 1;
		break;
	case 'b':
		conf.baud = EARGF(usage());
		break;
	case 'c':
		cp = malloc(sizeof *cp);
		if(cp == nil)
			sysfatal("%r");
		*ctll = cp;
		ctll = &cp->next;
		cp->next = nil;
		cp->ctl = EARGF(usage());
		break;
	case 'd':
		dodhcp = 1;
		break;
	case 'D':
		debug = 1;
		break;
	case 'f':
		dbfile = EARGF(usage());
		break;
	case 'g':
		if (parseip(conf.gaddr, EARGF(usage())) == -1)
			usage();
		break;
	case 'G':
		plan9 = 0;
		break;
	case 'h':
		if(utf2idn(EARGF(usage()), conf.hostname, sizeof(conf.hostname)) <= 0)
			sysfatal("bad hostname");
		sendhostname = 1;
		break;
	case 'm':
		conf.mtu = atoi(EARGF(usage()));
		break;
	case 'n':
		noconfig = 1;
		break;
	case 'N':
		dondbconfig = 1;
		break;
	case 'o':
		if(addoption(EARGF(usage())) < 0)
			usage();
		break;
	case 'O':
		Oflag = 1;
		break;
	case 'p':
		beprimary = 1;
		break;
	case 'P':
		beprimary = 0;
		break;
	case 'r':
		rflag = 1;
		break;
	case 'u':		/* IPv6: duplicate neighbour disc. off */
		dupl_disc = 0;
		break;
	case 'x':
		setnetmtpt(conf.mpoint, sizeof conf.mpoint, EARGF(usage()));
		break;
	case 'X':
		nodhcpwatch = 1;
		break;
	default:
		usage();
	} ARGEND;
	argv0 = "ipconfig";		/* boot invokes us as tcp? */

	action = parseargs(argc, argv);

	myifc = findifc(conf.mpoint, conf.dev);
	if(myifc < 0) {
		switch(action){
		default:
			if(noconfig)
				break;
			/* bind new interface */
			controldevice();
			binddevice();
			myifc = findifc(conf.mpoint, conf.dev);
		case Vremove:
		case Vunbind:
			break;
		}
		if(myifc < 0)
			sysfatal("interface not found for: %s", conf.dev);
	} else if(!noconfig) {
		/* open old interface */
		binddevice();
	}

	switch(action){
	case Vadd:
		mkclientid();
		if(dondbconfig){
			dodhcp = 0;
			ndbconfig();
			break;
		}
		doadd();
		break;
	case Vra6:
	case Vaddpref6:
		mkclientid();
		doipv6(action);
		break;
	case Vremove:
		doremove();
		break;
	case Vunbind:
		dounbind();
		break;
	}
	exits(nil);
}

static void
doadd(void)
{
	if(!validip(conf.laddr)){
		if(ipv6auto){
			ipmove(conf.laddr, conf.lladdr);
			dodhcp = 0;
		} else
			dodhcp = 1;
	}

	/* run dhcp if we need something */
	if(dodhcp){
		fprint(conf.rfd, "tag dhcp");
		dhcpquery(!noconfig, Sselecting);
	}

	if(!validip(conf.laddr))
		if(rflag && dodhcp && !noconfig){
			warning("couldn't determine ip address, retrying");
			dhcpwatch(1);
			return;
		} else
			sysfatal("no success with DHCP");

	DEBUG("adding address %I %M on %s", conf.laddr, conf.mask, conf.dev);
	if(noconfig)
		return;

	if(!isv4(conf.laddr)){
		if(ip6cfg() < 0)
			sysfatal("can't start IPv6 on %s, address %I", conf.dev, conf.laddr);
	} else {
		if(ip4cfg() < 0)
			sysfatal("can't start IPv4 on %s, address %I", conf.dev, conf.laddr);
		else if(dodhcp && conf.lease != Lforever)
			dhcpwatch(0);
	}

	/* leave everything we've learned somewhere other procs can find it */
	if(beprimary && !dondbconfig && !ipv6auto)
		putndb();
	refresh();
}

static void
doremove(void)
{
	if(!validip(conf.laddr))
		sysfatal("remove requires an address");

	DEBUG("removing address %I %M on %s", conf.laddr, conf.mask, conf.dev);
	if(conf.cfd < 0)
		return;

	if(fprint(conf.cfd, "remove %I %M", conf.laddr, conf.mask) < 0)
		warning("can't remove %I %M: %r", conf.laddr, conf.mask);
}

static void
dounbind(void)
{
	if(conf.cfd < 0)
		return;

	if(fprint(conf.cfd, "unbind") < 0)
		warning("can't unbind %s: %r", conf.dev);
}

/* send some ctls to a device */
static void
controldevice(void)
{
	char ctlfile[256];
	int fd;
	Ctl *cp;

	if (firstctl == nil || !isether())
		return;

	snprint(ctlfile, sizeof ctlfile, "%s/clone", conf.dev);
	fd = open(ctlfile, ORDWR);
	if(fd < 0)
		sysfatal("can't open %s", ctlfile);

	for(cp = firstctl; cp != nil; cp = cp->next){
		if(write(fd, cp->ctl, strlen(cp->ctl)) < 0)
			sysfatal("ctl message %s: %r", cp->ctl);
		seek(fd, 0, 0);
	}
//	close(fd);		/* or does it need to be left hanging? */
}

/* bind an ip stack to a device, leave the control channel open */
static void
binddevice(void)
{
	char buf[256];

	if(myifc >= 0){
		/* open the old interface */
		snprint(buf, sizeof buf, "%s/ipifc/%d/ctl", conf.mpoint, myifc);
		conf.cfd = open(buf, ORDWR);
		if(conf.cfd < 0)
			sysfatal("open %s: %r", buf);
	} else if(strcmp(conf.type, "ppp") == 0)
		pppbinddev();
	else {
		/* get a new ip interface */
		snprint(buf, sizeof buf, "%s/ipifc/clone", conf.mpoint);
		conf.cfd = open(buf, ORDWR);
		if(conf.cfd < 0)
			sysfatal("opening %s/ipifc/clone: %r", conf.mpoint);

		/* specify medium as ethernet, bind the interface to it */
		if(fprint(conf.cfd, "bind %s %s", conf.type, conf.dev) < 0)
			sysfatal("%s: bind %s %s: %r", buf, conf.type, conf.dev);
	}
	snprint(buf, sizeof buf, "%s/iproute", conf.mpoint);
	conf.rfd = open(buf, OWRITE);
}

/* add a logical interface to the ip stack */
int
ip4cfg(void)
{
	char buf[256];
	int n;

	if(!validip(conf.laddr) || !isv4(conf.laddr))
		return -1;

	n = sprint(buf, "add");
	n += snprint(buf+n, sizeof buf-n, " %I", conf.laddr);

	if(!validip(conf.mask))
		ipmove(conf.mask, defmask(conf.laddr));
	n += snprint(buf+n, sizeof buf-n, " %M", conf.mask);

	if(validip(conf.raddr)){
		n += snprint(buf+n, sizeof buf-n, " %I", conf.raddr);
		if(conf.mtu != 0)
			n += snprint(buf+n, sizeof buf-n, " %d", conf.mtu);
	}

	if(write(conf.cfd, buf, n) < 0){
		warning("write(%s): %r", buf);
		return -1;
	}

	if(validip(conf.gaddr) && isv4(conf.gaddr))
		adddefroute(conf.gaddr, conf.laddr, conf.laddr, conf.mask);

	return 0;
}

/* remove a logical interface from the ip stack */
void
ipunconfig(void)
{
	if(!validip(conf.laddr))
		return;

	if(!validip(conf.mask))
		ipmove(conf.mask, defmask(conf.laddr));

	if(validip(conf.gaddr))
		removedefroute(conf.gaddr, conf.laddr, conf.laddr, conf.mask);

	doremove();

	ipmove(conf.laddr, IPnoaddr);
	ipmove(conf.raddr, IPnoaddr);
	ipmove(conf.mask, IPnoaddr);
}

/* return true if this is not a null address */
int
validip(uchar *addr)
{
	return ipcmp(addr, IPnoaddr) != 0 && ipcmp(addr, v4prefix) != 0;
}

/* put server ip addresses into the ndb entry */
static char*
putaddrs(char *p, char *e, char *attr, uchar *a, int len)
{
	int i;

	for(i = 0; i < len && validip(a); i += IPaddrlen, a += IPaddrlen)
		p = seprint(p, e, "%s=%I\n", attr, a);
	return p;
}

/* put space separated names into ndb entry */
static char*
putnames(char *p, char *e, char *attr, char *s)
{
	char *x;

	for(; *s != 0; s = x+1){
		if((x = strchr(s, ' ')) != nil)
			*x = 0;
		p = seprint(p, e, "%s=%U\n", attr, s);
		if(x == nil)
			break;
		*x = ' ';
	}
	return p;
}

/* make an ndb entry and put it into /net/ndb for the servers to see */
void
putndb(void)
{
	static char buf[16*1024];
	char file[64], *p, *e, *np;
	Ndbtuple *t, *nt;
	Ndb *db;
	int fd;

	p = buf;
	e = buf + sizeof buf;
	p = seprint(p, e, "ip=%I ipmask=%M ipgw=%I\n",
		conf.laddr, conf.mask, conf.gaddr);
	if(np = strchr(conf.hostname, '.')){
		if(*conf.domainname == 0)
			strcpy(conf.domainname, np+1);
		*np = 0;
	}
	if(*conf.hostname)
		p = seprint(p, e, "\tsys=%U\n", conf.hostname);
	if(*conf.domainname)
		p = seprint(p, e, "\tdom=%U.%U\n",
			conf.hostname, conf.domainname);
	if(*conf.dnsdomain)
		p = putnames(p, e, "\tdnsdomain", conf.dnsdomain);
	if(validip(conf.dns))
		p = putaddrs(p, e, "\tdns", conf.dns, sizeof conf.dns);
	if(validip(conf.fs))
		p = putaddrs(p, e, "\tfs", conf.fs, sizeof conf.fs);
	if(validip(conf.auth))
		p = putaddrs(p, e, "\tauth", conf.auth, sizeof conf.auth);
	if(validip(conf.ntp))
		p = putaddrs(p, e, "\tntp", conf.ntp, sizeof conf.ntp);
	if(ndboptions)
		p = seprint(p, e, "%s\n", ndboptions);

	/* append preexisting entries not matching our ip */
	snprint(file, sizeof file, "%s/ndb", conf.mpoint);
	db = ndbopen(file);
	if(db != nil ){
		while((t = ndbparse(db)) != nil){
			uchar ip[IPaddrlen];

			if((nt = ndbfindattr(t, t, "ip")) == nil
			|| parseip(ip, nt->val) == -1
			|| ipcmp(ip, conf.laddr) != 0){
				p = seprint(p, e, "\n");
				for(nt = t; nt != nil; nt = nt->entry)
					p = seprint(p, e, "%s=%s%s", nt->attr, nt->val,
						nt->entry==nil? "\n": nt->line!=nt->entry? "\n\t": " ");
			}
			ndbfree(t);
		}
		ndbclose(db);
	}

	if((fd = open(file, OWRITE|OTRUNC)) < 0)
		return;
	write(fd, buf, p-buf);
	close(fd);
}

static int
issrcspec(uchar *src, uchar *smask)
{
	return isv4(src)? memcmp(smask+IPv4off, IPnoaddr+IPv4off, 4): ipcmp(smask, IPnoaddr);
}

static void
routectl(char *cmd, uchar *dst, uchar *mask, uchar *gate, uchar *ia, uchar *src, uchar *smask)
{
	char *ctl;

	if(issrcspec(src, smask))
		ctl = "%s %I %M %I %I %I %M";
	else
		ctl = "%s %I %M %I %I";
	DEBUG(ctl, cmd, dst, mask, gate, ia, src, smask);
	if(conf.rfd < 0)
		return;
	fprint(conf.rfd, ctl, cmd, dst, mask, gate, ia, src, smask);
}

static void
defroutectl(char *cmd, uchar *gaddr, uchar *ia, uchar *src, uchar *smask)
{
	uchar dst[IPaddrlen], mask[IPaddrlen];

	if(isv4(gaddr)){
		parseipandmask(dst, mask, "0.0.0.0", "0.0.0.0");
		if(src == nil)
			src = dst;
		if(smask == nil)
			smask = mask;
	} else {
		parseipandmask(dst, mask, "2000::", "/3");
		if(src == nil)
			src = IPnoaddr;
		if(smask == nil)
			smask = IPnoaddr;
	}
	routectl(cmd, dst, mask, gaddr, ia, src, smask);

	/* also add a source specific route */
	if(ipcmp(src, IPnoaddr) != 0 && ipcmp(src, v4prefix) != 0)
		routectl(cmd, dst, mask, gaddr, ia, src, IPallbits);
}

void
adddefroute(uchar *gaddr, uchar *ia, uchar *src, uchar *smask)
{
	defroutectl("add", gaddr, ia, src, smask);
}

void
removedefroute(uchar *gaddr, uchar *ia, uchar *src, uchar *smask)
{
	defroutectl("remove", gaddr, ia, src, smask);
}

void
refresh(void)
{
	char file[64];
	int fd;

	snprint(file, sizeof file, "%s/cs", conf.mpoint);
	if((fd = open(file, OWRITE)) >= 0){
		write(fd, "refresh", 7);
		close(fd);
	}
	snprint(file, sizeof file, "%s/dns", conf.mpoint);
	if((fd = open(file, OWRITE)) >= 0){
		write(fd, "refresh", 7);
		close(fd);
	}
}

void
catch(void*, char *msg)
{
	if(strstr(msg, "alarm"))
		noted(NCONT);
	noted(NDFLT);
}

/* return pseudo-random integer in range low...(hi-1) */
ulong
randint(ulong low, ulong hi)
{
	if (hi < low)
		return low;
	return low + nrand(hi - low);
}

long
jitter(void)		/* compute small pseudo-random delay in ms */
{
	return randint(0, 10*1000);
}

int
countaddrs(uchar *a, int len)
{
	int i;

	for(i = 0; i < len && validip(a); i += IPaddrlen, a += IPaddrlen)
		;
	return i / IPaddrlen;
}

void
addaddrs(uchar *to, int nto, uchar *from, int nfrom)
{
	int i, j;

	for(i = 0; i < nfrom; i += IPaddrlen, from += IPaddrlen){
		if(!validip(from))
			continue;
		for(j = 0; j < nto && validip(to+j); j += IPaddrlen){
			if(ipcmp(to+j, from) == 0)
				return;
		}
		if(j == nto)
			return;
		ipmove(to+j, from);
	}
}

void
addnames(char *d, char *s, int len)
{
	char *p, *e, *f;
	int n;

	for(;;s++){
		if((e = strchr(s, ' ')) == nil)
			e = strchr(s, 0);
		n = e - s;
		if(n == 0)
			goto next;
		for(p = d;;p++){
			if((f = strchr(p, ' ')) == nil)
				f = strchr(p, 0);
			if(f - p == n && memcmp(s, p, n) == 0)
				goto next;
			p = f;
			if(*p == 0)
				break;
		}
		if(1 + n + p - d >= len)
			break;
		if(p > d)
			*p++ = ' ';
		p[n] = 0;
		memmove(p, s, n);
next:
		s = e;
		if(*s == 0)
			break;
	}
}

int
pnames(uchar *d, int nd, char *s)
{
	uchar *de = d + nd;
	int l;

	if(nd < 1)
		return -1;
	for(; *s != 0; s++){
		for(l = 0; *s != 0 && *s != '.' && *s != ' '; l++)
			s++;

		d += l+1;
		if(d >= de || l > 077)
			return -1;

		d[-l-1] = l;
		memmove(d-l, s-l, l);

		if(*s != '.')
			*d++ = 0;
	}
	return d - (de - nd);
}

int
gnames(char *d, int nd, uchar *s, int ns)
{
	char  *de = d + nd;
	uchar *se = s + ns;
	uchar *c = nil;
	int l, p = 0;

	if(ns < 1 || nd < 1)
		return -1;
	while(s < se){
		l = *s++;
		if((l & 0300) == 0300){
			if(++p > 100 || s >= se)
				break;
			l = (l & 077)<<8 | *s++;
			if(c == nil)
				c = s;
			s = (se - ns) + l;
			continue;
		}
		l &= 077;
		if(l == 0){
			if(d <= de - nd)
				break;
			d[-1] = ' ';
			if(c != nil){
				s = c;
				c = nil;
				p = 0;
			}
			continue;
		}
		if(s+l >= se || d+l >= de)
			break;
		memmove(d, s, l);
		s += l;
		d += l;
		*d++ = '.';
	}
	if(p != 0 || s != se || d <= de - nd || d[-1] != ' ')
		return -1;
	*(--d) = 0;
	return d - (de - nd);
}

static int
Ufmt(Fmt *f)
{
	char d[256], *s;

	s = va_arg(f->args, char*);
	if(idn2utf(s, d, sizeof(d)) >= 0)
		s = d;
	fmtprint(f, "%s", s);
	return 0;
}

static Ndbtuple*
uniquent(Ndbtuple *t)
{
	Ndbtuple **l, *x;

	l = &t->entry;
	while((x = *l) != nil){
		if(strcmp(t->attr, x->attr) != 0){
			l = &x->entry;
			continue;
		}
		*l = x->entry;
		x->entry = nil;
		ndbfree(x);
	}
	return t;
}

/* read configuration (except laddr) for myip from ndb */
void
ndb2conf(Ndb *db, uchar *myip)
{
	int nattr;
	char *attrs[10], val[256];
	uchar ip[IPaddrlen];
	Ndbtuple *t, *nt;

	ipmove(conf.mask, defmask(conf.laddr));

	memset(conf.gaddr, 0, sizeof(conf.gaddr));
	memset(conf.dns, 0, sizeof(conf.dns));
	memset(conf.ntp, 0, sizeof(conf.ntp));
	memset(conf.fs, 0, sizeof(conf.fs));
	memset(conf.auth, 0, sizeof(conf.auth));
	memset(conf.dnsdomain, 0, sizeof(conf.dnsdomain));

	if(db == nil)
		return;

	nattr = 0;
	attrs[nattr++] = "ipmask";
	attrs[nattr++] = "ipgw";

	attrs[nattr++] = "@dns";
	attrs[nattr++] = "@ntp";
	attrs[nattr++] = "@fs";
	attrs[nattr++] = "@auth";

	attrs[nattr++] = "dnsdomain";

	snprint(val, sizeof(val), "%I", myip);
	t = ndbipinfo(db, "ip", val, attrs, nattr);
	for(nt = t; nt != nil; nt = nt->entry) {
		if(strcmp(nt->attr, "dnsdomain") == 0) {
			if(utf2idn(nt->val, val, sizeof(val)) <= 0)
				continue;
			addnames(conf.dnsdomain, val, sizeof(conf.dnsdomain));
			continue;
		}
		if(strcmp(nt->attr, "ipmask") == 0) {
			nt = uniquent(nt);
			if(parseipmask(conf.mask, nt->val, isv4(myip)) == -1)
				goto Badip;
			continue;
		}
		if(parseip(ip, nt->val) == -1) {
		Badip:
			fprint(2, "%s: bad %s address in ndb: %s\n", argv0, nt->attr, nt->val);
			continue;
		}
		if(strcmp(nt->attr, "ipgw") == 0) {
			nt = uniquent(nt);
			ipmove(conf.gaddr, ip);
		} else if(strcmp(nt->attr, "dns") == 0) {
			addaddrs(conf.dns, sizeof(conf.dns), ip, IPaddrlen);
		} else if(strcmp(nt->attr, "ntp") == 0) {
			addaddrs(conf.ntp, sizeof(conf.ntp), ip, IPaddrlen);
		} else if(strcmp(nt->attr, "fs") == 0) {
			addaddrs(conf.fs, sizeof(conf.fs), ip, IPaddrlen);
		} else if(strcmp(nt->attr, "auth") == 0) {
			addaddrs(conf.auth, sizeof(conf.auth), ip, IPaddrlen);
		}
	}
	ndbfree(t);
}

Ndb*
opendatabase(void)
{
	static Ndb *db;

	if(db != nil)
		ndbclose(db);
	db = ndbopen(dbfile);
	return db;
}

/* add addresses for my ethernet address from ndb */
static void
ndbconfig(void)
{
	uchar ips[128*IPaddrlen];
	char etheraddr[32], *attr;
	Ndbtuple *t, *nt;
	Ndb *db;
	int n, i;

	db = opendatabase();
	if(db == nil)
		sysfatal("can't open ndb: %r");

	if(validip(conf.laddr)){
		ndb2conf(db, conf.laddr);
		doadd();
		return;
	}

	memset(ips, 0, sizeof(ips));

	if(conf.hwatype != 1)
		sysfatal("can't read hardware address");
	snprint(etheraddr, sizeof(etheraddr), "%E", conf.hwa);

	attr = "ip";
	t = ndbipinfo(db, "ether", etheraddr, &attr, 1);
	for(nt = t; nt != nil; nt = nt->entry) {
		if(parseip(conf.laddr, nt->val) == -1){
			fprint(2, "%s: bad %s address in ndb: %s\n", argv0,
				nt->attr, nt->val);
			continue;
		}
		addaddrs(ips, sizeof(ips), conf.laddr, IPaddrlen);
	}
	ndbfree(t);

	n = countaddrs(ips, sizeof(ips));
	if(n == 0)
		sysfatal("no ip addresses found in ndb");

	/* add link local address first, if not already done */
	if(!findllip(conf.lladdr, ifc)){
		for(i = 0; i < n; i++){
			ipmove(conf.laddr, ips+i*IPaddrlen);
			if(ISIPV6LINKLOCAL(conf.laddr)){
				ipv6auto = 0;
				ipmove(conf.lladdr, conf.laddr);
				ndb2conf(db, conf.laddr);
				doadd();
				break;
			}
		}
		if(ipv6auto){
			ipmove(conf.laddr, IPnoaddr);
			doadd();
		}
	}

	/* add v4 addresses and v6 if link local address is available */
	for(i = 0; i < n; i++){
		ipmove(conf.laddr, ips+i*IPaddrlen);
		if(isv4(conf.laddr) || ipcmp(conf.laddr, conf.lladdr) != 0){
			ndb2conf(db, conf.laddr);
			doadd();
		}
	}
}
