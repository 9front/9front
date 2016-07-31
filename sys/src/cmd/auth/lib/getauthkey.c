#include <u.h>
#include <libc.h>
#include <bio.h>
#include <authsrv.h>
#include "authcmdlib.h"

int
getauthkey(Authkey *authkey)
{
	Nvrsafe safe;

	memset(authkey, 0, sizeof(Authkey));
	if(readnvram(&safe, 0) < 0){
		print("can't read NVRAM, please enter machine key\n");
		getpass(authkey, nil, 0, 1);
	} else {
		memmove(authkey->des, safe.machkey, DESKEYLEN);
		memmove(authkey->aes, safe.aesmachkey, AESKEYLEN);
		memset(&safe, 0, sizeof safe);
	}
	return 1;
}
