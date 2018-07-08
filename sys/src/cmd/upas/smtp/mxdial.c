#include "common.h"
#include "smtp.h"
#include <ndb.h>

char	*bustedmxs[Maxbustedmx];

static void
expand(DS *ds)
{
	char *s;
	Ndbtuple *t;

	s = ds->host + 1;
	t = csipinfo(ds->netdir, "sys", sysname(), &s, 1);
	if(t != nil){
		strecpy(ds->expand, ds->expand+sizeof ds->expand, t->val);
		ds->host = ds->expand;
	}
	ndbfree(t);
}

/* break up an address to its component parts */
void
dialstringparse(char *str, DS *ds)
{
	char *p, *p2;

	strecpy(ds->buf, ds->buf + sizeof ds->buf, str);
	p = strchr(ds->buf, '!');
	if(p == 0) {
		ds->netdir = 0;
		ds->proto = "net";
		ds->host = ds->buf;
	} else {
		if(*ds->buf != '/'){
			ds->netdir = 0;
			ds->proto = ds->buf;
		} else {
			for(p2 = p; *p2 != '/'; p2--)
				;
			*p2++ = 0;
			ds->netdir = ds->buf;
			ds->proto = p2;
		}
		*p = 0;
		ds->host = p + 1;
	}
	ds->service = strchr(ds->host, '!');
	if(ds->service)
		*ds->service++ = 0;
	else
		ds->service = "smtp";
	if(*ds->host == '$')
		expand(ds);
}

void
mxtabfree(Mxtab *mx)
{
	free(mx->mx);
	memset(mx, 0, sizeof *mx);
}

static void
mxtabrealloc(Mxtab *mx)
{
	if(mx->nmx < mx->amx)
		return;
	if(mx->amx == 0)
		mx->amx = 1;
	mx->amx <<= 1;
	mx->mx = realloc(mx->mx, sizeof mx->mx[0] * mx->amx);
	if(mx->mx == nil)
		sysfatal("no memory for mx");
}

static void
mxtabadd(Mxtab *mx, char *host, char *ip, char *net, int pref)
{
	int i;
	Mx *x;

	mxtabrealloc(mx);
	x = mx->mx;
	for(i = mx->nmx; i>0 && x[i-1].pref>pref && x[i-1].netdir == net; i--)
		x[i] = x[i-1];
	strecpy(x[i].host, x[i].host + sizeof x[i].host, host);
	if(ip != nil)
		strecpy(x[i].ip, x[i].ip + sizeof x[i].ip, ip);
	else
		x[i].ip[0] = 0;
	x[i].netdir = net;
	x[i].pref = pref;
	x[i].valid = 1;
	mx->nmx++;
}

static int
timeout(void*, char *msg)
{
	if(strstr(msg, "alarm"))
		return 1;
	return 0;
}

static long
timedwrite(int fd, void *buf, long len, long ms)
{
	long n, oalarm;

	atnotify(timeout, 1);
	oalarm = alarm(ms);
	n = pwrite(fd, buf, len, 0);
	alarm(oalarm);
	atnotify(timeout, 0);
	return n;
}

static int
dnslookup(Mxtab *mx, int fd, char *query, char *domain, char *net, int pref0)
{
	int n;
	char buf[1024], *f[4];

	n = timedwrite(fd, query, strlen(query), 60*1000);
	if(n < 0){
		rerrstr(buf, sizeof buf);
		dprint("dns: %s\n", buf);
		if(strstr(buf, "dns failure")){
			/* if dns fails for the mx lookup, we have to stop */
			close(fd);
			return -1;
		}
		return 0;
	}

	seek(fd, 0, 0);
	for(;;){
		if((n = read(fd, buf, sizeof buf - 1)) < 1)
			break;
		buf[n] = 0;
	//	chat("dns: %s\n", buf);
		n = tokenize(buf, f, nelem(f));
		if(n < 2)
			continue;
		if(strcmp(f[1], "mx") == 0 && n == 4){
			if(strchr(domain, '.') == 0)
				strcpy(domain, f[0]);
			mxtabadd(mx, f[3], nil, net, atoi(f[2]));
		}
		else if (strcmp(f[1], "ip") == 0 && n == 3){
			if(strchr(domain, '.') == 0)
				strcpy(domain, f[0]);
			mxtabadd(mx, f[0], f[2], net, pref0);
		}
	}

	return 0;
}

static int
busted(char *mx)
{
	char **bmp;

	for (bmp = bustedmxs; *bmp != nil; bmp++)
		if (strcmp(mx, *bmp) == 0)
			return 1;
	return 0;
}

static void
complain(Mxtab *mx, char *domain)
{
	char buf[1024], *e, *p;
	int i;

	p = buf;
	e = buf + sizeof buf;
	for(i = 0; i < mx->nmx; i++)
		p = seprint(p, e, "%s ", mx->mx[i].ip);
	syslog(0, "smtpd.mx", "loopback for %s %s", domain, buf);
}

static int
okaymx(Mxtab *mx, char *domain)
{
	int i;
	Mx *x;

	/* look for malicious dns entries; TODO use badcidr in ../spf/ to catch more than ip4 */
	for(i = 0; i < mx->nmx; i++){
		x = mx->mx + i;
		if(x->valid && strcmp(x->ip, "127.0.0.1") == 0){
			dprint("illegal: domain %s lists 127.0.0.1 as mail server", domain);
			complain(mx, domain);
			werrstr("illegal: domain %s lists 127.0.0.1 as mail server", domain);
			return -1;
		}
		if(x->valid && busted(x->host)){
			dprint("lookup: skipping busted mx %s\n", x->host);
			x->valid = 0;
		}
	}
	return 0;
}

static int
lookup(Mxtab *mx, char *net, char *host, char *domain, char *type)
{
	char dns[128], buf[1024];
	int fd, i;
	Mx *x;

	snprint(dns, sizeof dns, "%s/dns", net);
	fd = open(dns, ORDWR);
	if(fd == -1)
		return -1;

	snprint(buf, sizeof buf, "%s %s", host, type);
	dprint("sending %s '%s'\n", dns, buf);
	dnslookup(mx, fd, buf, domain, net, 10000);

	for(i = 0; i < mx->nmx; i++){
		x = mx->mx + i;
		if(x->ip[0] != 0)
			continue;
		x->valid = 0;

		snprint(buf, sizeof buf, "%s %s", x->host, "ip");
		dprint("sending %s '%s'\n", dns, buf);
		dnslookup(mx, fd, buf, domain, net, x->pref);
	}

	close(fd);

	if(strcmp(type, "mx") == 0){
		if(okaymx(mx, domain) == -1)
			return -1;
		for(i = 0; i < mx->nmx; i++){
			x = mx->mx + i;
			dprint("mx list: %s	%d	%s\n", x->host, x->pref, x->ip);
		}
		dprint("\n");
	}

	return 0;
}

static int
lookcall(Mxtab *mx, DS *d, char *domain, char *type)
{
	char buf[1024];
	int i;
	Mx *x;

	if(lookup(mx, d->netdir, d->host, domain, type) == -1){
		for(i = 0; i < mx->nmx; i++)
			if(mx->mx[i].netdir == d->netdir)
				mx->mx[i].valid = 0;
		return -1;
	}

	for(i = 0; i < mx->nmx; i++){
		x = mx->mx + i;
		if(x->ip[0] == 0 || x->valid == 0){
			x->valid = 0;
			continue;
		}
		snprint(buf, sizeof buf, "%s/%s!%s!%s", d->netdir, d->proto,
			x->ip /*x->host*/, d->service);
		dprint("mxdial trying %s	[%s]\n", x->host, buf);
		atnotify(timeout, 1);
		alarm(10*1000);
		mx->fd = dial(buf, 0, 0, 0);
		alarm(0);
		atnotify(timeout, 0);
		if(mx->fd >= 0){
			mx->pmx = i;
			return mx->fd;
		}
		dprint("	failed %r\n");
		x->valid = 0;
	}

	return -1;
}

int
mxdial0(char *addr, char *ddomain, char *gdomain, Mxtab *mx)
{
	int nd, i, j;
	DS *d;
	static char *tab[] = {"mx", "ip", };

	dprint("mxdial(%s, %s, %s, mx)\n", addr, ddomain, gdomain);
	memset(mx, 0, sizeof *mx);
	d = mx->ds;
	dialstringparse(addr, d + 0);
	nd = 1;
	if(d[0].netdir == nil){
		d[1] = d[0];
		d[0].netdir = "/net";
		d[1].netdir = "/net.alt";
		nd = 2;
	}

	/* search all networks for mx records; then ip records */
	for(j = 0; j < nelem(tab); j++)
		for(i = 0; i < nd; i++)
			if(lookcall(mx, d + i, ddomain, tab[j]) != -1)
				return mx->fd;

	/* grotty: try gateway machine by ip only (fixme: try cs lookup) */
	if(gdomain != nil){
		dialstringparse(netmkaddr(gdomain, 0, "smtp"), d + 0);
		if(lookcall(mx, d + 0, gdomain, "ip") != -1)
			return mx->fd;
	}

	return -1;
}

int
mxdial(char *addr, char *ddomain, char *gdomain, Mx *x)
{
	int fd;
	Mxtab mx;

	memset(x, 0, sizeof *x);
	fd = mxdial0(addr, ddomain, gdomain, &mx);
	if(fd >= 0 && mx.pmx >= 0)
		*x = mx.mx[mx.pmx];
	mxtabfree(&mx);
	return fd;
}
