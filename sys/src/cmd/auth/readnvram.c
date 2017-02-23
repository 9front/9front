/* readnvram */
#include <u.h>
#include <libc.h>
#include <auth.h>
#include <authsrv.h>

void
main(int, char **)
{
	static uchar zeros[16];
	static Nvrsafe safe;
	int printed = 0;

	quotefmtinstall();

	/*
	 * readnvram can return -1 meaning nvram wasn't written,
	 * but safe still holds good data.
	 */
	if(readnvram(&safe, 0) < 0 && safe.authid[0] == '\0') 
		sysfatal("readnvram: %r");

	fmtinstall('H', encodefmt);

	if(memcmp(safe.machkey, zeros, DESKEYLEN) != 0){
		print("key proto=p9sk1 user=%q dom=%q !hex=%.*H !password=______\n", 
			safe.authid, safe.authdom, DESKEYLEN, safe.machkey);
		printed++;
	}
	if(memcmp(safe.aesmachkey, zeros, AESKEYLEN) != 0){
		print("key proto=dp9ik user=%q dom=%q !hex=%.*H !password=______\n", 
			safe.authid, safe.authdom, AESKEYLEN, safe.aesmachkey);
		printed++;
	}
	if(!printed)
		sysfatal("no keys");

	exits(0);
}
