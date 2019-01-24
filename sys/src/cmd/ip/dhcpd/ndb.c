/*
 *  this currently only works for ethernet bootp's -- presotto
 */
#include <u.h>
#include <libc.h>
#include <ip.h>
#include <bio.h>
#include <ndb.h>
#include "dat.h"

static Ndb *db;
char *ndbfile;

/*
 * open ndbfile as db if not already open.  also check for stale data
 * and reload as needed.
 */
static Ndb *
opendb(void)
{
	static ulong lastcheck;

	/* check no more often than once every minute */
	if(db == nil) {
		db = ndbopen(ndbfile);
		if(db != nil)
			lastcheck = now;
	} else if(now >= lastcheck + 60) {
		if (ndbchanged(db))
			ndbreopen(db);
		lastcheck = now;
	}
	return db;
}

Ipifc*
findifc(uchar *ip)
{
	Ipifc *ifc;
	Iplifc *lifc;

	for(ifc = ipifcs; ifc != nil; ifc = ifc->next){
		for(lifc = ifc->lifc; lifc != nil; lifc = lifc->next)
			if(ipcmp(ip, lifc->ip) == 0)
				return ifc;
	}
	return nil;
}

Iplifc*
localonifc(uchar *ip, Ipifc *ifc)
{
	uchar x[IPaddrlen];
	Iplifc *lifc;

	if(ifc == nil)
		return nil;

	for(lifc = ifc->lifc; lifc != nil; lifc = lifc->next){
		maskip(ip, lifc->mask, x);
		if(ipcmp(x, lifc->net) == 0)
			return lifc;
	}
	return nil;
}

void
localip(uchar *laddr, uchar *raddr, Ipifc *ifc)
{
	Iplifc *lifc;

	if((lifc = localonifc(raddr, ifc)) != nil)
		ipmove(laddr, lifc->ip);
	else if(ipcmp(laddr, IPv4bcast) == 0)
		ipmove(laddr, IPnoaddr);
}


static void
setipaddr(uchar *addr, char *ip)
{
	if(ipcmp(addr, IPnoaddr) == 0)
		parseip(addr, ip);
}

static void
setipmask(uchar *mask, char *ip)
{
	if(ipcmp(mask, IPnoaddr) == 0)
		parseipmask(mask, ip);
}

/*
 *  do an ipinfo with defaults
 */
int
lookupip(uchar *ipaddr, char *hwattr, char *hwval, Info *iip, int gate)
{
	char ip[32];
	Ndbtuple *t, *nt;
	char *attrs[32], **p;

	if(opendb() == nil){
		warning("can't open db: %r");
		return -1;
	}

	p = attrs;
	*p++ = "ip";
	*p++ = "ipmask";
	*p++ = "@ipgw";
	if(!gate){
		*p++ = "dom";
		*p++ = "bootf";
		*p++ = "bootf2";
		*p++ = "@tftp";
		*p++ = "@tftp2";
		*p++ = "@fs";
		*p++ = "@auth";
		*p++ = "rootpath";
		*p++ = "@rootserver";
		*p++ = "vendor";
	}
	if(hwattr != nil)
		*p++ = hwattr;
	*p = 0;

	memset(iip, 0, sizeof(*iip));
	snprint(ip, sizeof(ip), "%I", ipaddr);
	t = ndbipinfo(db, "ip", ip, attrs, p - attrs);
	if(t == nil)
		return -1;
	
	for(nt = t; nt != nil; nt = nt->entry){
		if(strcmp(nt->attr, "ip") == 0)
			setipaddr(iip->ipaddr, nt->val);
		else
		if(strcmp(nt->attr, "ipmask") == 0)
			setipmask(iip->ipmask, nt->val);
		else
		if(strcmp(nt->attr, "ipgw") == 0)
			setipaddr(iip->gwip, nt->val);
		else
		if(strcmp(nt->attr, "dom") == 0){
			if(iip->domain[0] == 0)
				strncpy(iip->domain, nt->val, sizeof(iip->domain)-1);
		}
		else
		if(strcmp(nt->attr, "bootf") == 0){
			if(iip->bootf[0] == 0)
				strncpy(iip->bootf, nt->val, sizeof(iip->bootf)-1);
		}
		else
		if(strcmp(nt->attr, "bootf2") == 0){
			if(iip->bootf2[0] == 0)
				strncpy(iip->bootf2, nt->val, sizeof(iip->bootf2)-1);
		}
		else
		if(strcmp(nt->attr, "tftp") == 0)
			setipaddr(iip->tftp, nt->val);
		else
		if(strcmp(nt->attr, "tftp2") == 0)
			setipaddr(iip->tftp2, nt->val);
		else
		if(strcmp(nt->attr, "fs") == 0)
			setipaddr(iip->fsip, nt->val);
		else
		if(strcmp(nt->attr, "auth") == 0)
			setipaddr(iip->auip, nt->val);
		else
		if(strcmp(nt->attr, "rootpath") == 0){
			if(iip->rootpath[0] == 0)
				strncpy(iip->rootpath, nt->val, sizeof(iip->rootpath)-1);
		}
		else
		if(strcmp(nt->attr, "rootserver") == 0)
			setipaddr(iip->rootserverip, nt->val);
		else
		if(strcmp(nt->attr, "vendor") == 0){
			if(iip->vendor[0] == 0)
				strncpy(iip->vendor, nt->val, sizeof(iip->vendor)-1);
		}

		if(hwattr != nil && strcmp(nt->attr, hwattr) == 0)
			if(strcmp(hwval, nt->val) == 0)
				iip->indb = 1;
	}
	ndbfree(t);
	maskip(iip->ipaddr, iip->ipmask, iip->ipnet);
	return 0;
}

/*
 *  lookup info about a client in the database.  Find an address on the
 *  same net as riip.
 */
int
lookup(Bootp *bp, Info *iip, Info *riip)
{
	char *hwattr, hwval[Maxhwlen*2+1];
	uchar ciaddr[IPaddrlen];
	Ndbtuple *t, *nt;
	Ndbs s;

	memset(iip, 0, sizeof(*iip));

	if(opendb() == nil){
		warning("can't open db: %r");
		return -1;
	}

	switch(bp->htype){
	case 1:
		hwattr = "ether";
		snprint(hwval, sizeof(hwval), "%E", bp->chaddr);
		break;
	default:
		hwattr = nil;
	}

	/* client knows its address? */
	v4tov6(ciaddr, bp->ciaddr);
	if(validip(ciaddr)){
		if(!samenet(ciaddr, riip)){
			if(riip->ifc != nil)
				warning("%I not on %s", ciaddr, riip->ifc->dev);
			else
				warning("%I not on %I", ciaddr, riip->ipnet);
			return -1;
		}
		if(lookupip(ciaddr, hwattr, hwval, iip, 0) < 0) {
			if (debug)
				warning("don't know %I", ciaddr);
			return -1;	/* don't know anything about it */
		}
		return 0;
	}

	if(hwattr == nil)
		return -1;

	/*
	 *  use hardware address to find an ip address on
	 *  same net as riip
	 */
	t = ndbsearch(db, &s, hwattr, hwval);
	while(t != nil){
		for(nt = t; nt != nil; nt = nt->entry){
			if(strcmp(nt->attr, "ip") != 0)
				continue;
			if(parseip(ciaddr, nt->val) == -1)
				continue;
			if(!validip(ciaddr) || !samenet(ciaddr, riip))
				continue;
			if(lookupip(ciaddr, hwattr, hwval, iip, 0) < 0)
				continue;
			ndbfree(t);
			return 0;
		}
		ndbfree(t);
		t = ndbsnext(&s, hwattr, hwval);
	}
	return -1;
}

/*
 *  interface to ndbipinfo
 */
Ndbtuple*
lookupinfo(uchar *ipaddr, char **attr, int n)
{
	char ip[64];

	snprint(ip, sizeof ip, "%I", ipaddr);
	return ndbipinfo(db, "ip", ip, attr, n);
}

/*
 *  return the ip addresses for a type of server for system ip
 */
int
lookupserver(char *attr, uchar **ipaddrs, int naddrs, Ndbtuple *t)
{
	Ndbtuple *nt;
	int rv = 0;

	for(nt = t; rv < naddrs && nt != nil; nt = nt->entry){
		if(strcmp(nt->attr, attr) != 0)
			continue;
		if(parseip(ipaddrs[rv], nt->val) == -1)
			continue;
		rv++;
	}
	return rv;
}

/*
 *  just lookup the name
 */
void
lookupname(char *val, int len, Ndbtuple *t)
{
	Ndbtuple *nt;

	for(nt = t; nt != nil; nt = nt->entry)
		if(strcmp(nt->attr, "dom") == 0){
			if(utf2idn(nt->val, val, len) < 0)
				strncpy(val, nt->val, len-1);
			val[len-1] = 0;
			break;
		}
}
