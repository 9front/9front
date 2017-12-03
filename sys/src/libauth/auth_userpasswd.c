#include <u.h>
#include <libc.h>
#include <auth.h>
#include "authlocal.h"

AuthInfo*
auth_userpasswd(char *user, char *passwd)
{
	AuthRpc *rpc;
	AuthInfo *ai;
	char *s;
	int afd;

	afd = open("/mnt/factotum/rpc", ORDWR);
	if(afd < 0)
		return nil;
	ai = nil;
	rpc = auth_allocrpc(afd);
	if(rpc == nil)
		goto Out;
	s = "proto=dp9ik role=login";
	if(auth_rpc(rpc, "start", s, strlen(s)) != ARok){
		s = "proto=p9sk1 role=login";
		if(auth_rpc(rpc, "start", s, strlen(s)) != ARok)
			goto Out;
	}
	if(auth_rpc(rpc, "write", user, strlen(user)) != ARok
	|| auth_rpc(rpc, "write", passwd, strlen(passwd)) != ARok)
		goto Out;
	ai = auth_getinfo(rpc);
Out:
	if(rpc != nil)
		auth_freerpc(rpc);
	close(afd);
	return ai;
}
