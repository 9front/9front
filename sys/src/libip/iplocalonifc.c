#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <ip.h>

Iplifc*
iplocalonifc(Ipifc *ifc, uchar *ip)
{
	Iplifc *lifc;

	for(lifc = ifc->lifc; lifc != nil; lifc = lifc->next){
		if(ipcmp(ip, lifc->ip) == 0)
			return lifc;
	}
	return nil;
}
