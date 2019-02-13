#include <u.h>
#include <libc.h>
#include <ip.h>

static uchar loopbacknet[IPaddrlen] = {
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0xff, 0xff,
	127, 0, 0, 0
};
static uchar loopbackmask[IPaddrlen] = {
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
	0xff, 0, 0, 0
};
static uchar loopback6[IPaddrlen] = {
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 1
};

// find first ip that isn't a friggin loopback or
// link-local address. prefer v4 over v6.
int
myipaddr(uchar *ip, char *net)
{
	Ipifc *nifc;
	Iplifc *lifc;
	static Ipifc *ifc;
	uchar mynet[IPaddrlen];

	ipmove(ip, IPnoaddr);
	ifc = readipifc(net, ifc, -1);
	for(nifc = ifc; nifc != nil; nifc = nifc->next){
		for(lifc = nifc->lifc; lifc != nil; lifc = lifc->next){
			/* unspecified */
			if(ipcmp(lifc->ip, IPnoaddr) == 0)
				continue;

			if(isv4(lifc->ip)){
				/* ipv4 loopback */
				maskip(lifc->ip, loopbackmask, mynet);
				if(ipcmp(mynet, loopbacknet) == 0)
					continue;
	
				ipmove(ip, lifc->ip);
				return 0;
			}

			/* already got a v6 address? */
			if(ipcmp(ip, IPnoaddr) != 0)
				continue;

			/* ipv6 loopback */
			if(ipcmp(lifc->ip, loopback6) == 0)
				continue;

			/* ipv6 linklocal */
			if(ISIPV6LINKLOCAL(lifc->ip))
				continue;

			/* save first v6 address */
			ipmove(ip, lifc->ip);
		}
	}
	return ipcmp(ip, IPnoaddr) != 0 ? 0 : -1;
}
