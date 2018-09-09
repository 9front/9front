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

// find first ip addr that isn't the friggin loopback address
// unless there are no others
int
myipaddr(uchar *ip, char *net)
{
	Ipifc *nifc;
	Iplifc *lifc;
	static Ipifc *ifc;
	uchar mynet[IPaddrlen];

	ifc = readipifc(net, ifc, -1);
	for(nifc = ifc; nifc; nifc = nifc->next)
		for(lifc = nifc->lifc; lifc; lifc = lifc->next){
			/* unspecified */
			if(ipcmp(lifc->ip, IPnoaddr) == 0)
				continue;

			/* ipv6 loopback */
			if(ipcmp(lifc->ip, loopback6) == 0)
				continue;

			/* ipv4 loopback */
			maskip(lifc->ip, loopbackmask, mynet);
			if(ipcmp(mynet, loopbacknet) == 0)
				continue;
	
			/* ipv6 linklocal */
			if(ISIPV6LINKLOCAL(lifc->ip))
				continue;

			ipmove(ip, lifc->ip);
			return 0;
		}
	ipmove(ip, IPnoaddr);
	return -1;
}
