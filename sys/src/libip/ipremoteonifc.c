#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <ip.h>

Iplifc*
ipremoteonifc(Ipifc *ifc, uchar *ip)
{
	uchar net[IPaddrlen];
	Iplifc *lifc;

	for(lifc = ifc->lifc; lifc != nil; lifc = lifc->next){
		maskip(ip, lifc->mask, net);
		if(ipcmp(net, lifc->net) == 0)
			return lifc;
	}
	return nil;
}
