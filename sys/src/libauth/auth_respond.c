#include <u.h>
#include <libc.h>
#include <auth.h>
#include "authlocal.h"

enum {
	ARgiveup = 100,
};

static int
dorpc(AuthRpc *rpc, char *verb, char *val, int len, AuthGetkey *getkey)
{
	int ret;

	for(;;){
		if((ret = auth_rpc(rpc, verb, val, len)) != ARneedkey && ret != ARbadkey)
			return ret;
		if(getkey == nil)
			return ARgiveup;	/* don't know how */
		if((*getkey)(rpc->arg) < 0)
			return ARgiveup;	/* user punted */
	}
}

static int
dorespond(void *chal, uint nchal, char *user, uint nuser, void *resp, uint nresp,
	AuthInfo **ai, AuthGetkey *getkey, char *fmt, va_list arg)
{
	char *p, *s;
	int afd;
	AuthRpc *rpc;
	Attr *a;

	if((afd = open("/mnt/factotum/rpc", ORDWR|OCEXEC)) < 0)
		return -1;
	
	if((rpc = auth_allocrpc(afd)) == nil){
		close(afd);
		return -1;
	}

	quotefmtinstall();	/* just in case */
	
	if((p = vsmprint(fmt, arg))==nil
	|| dorpc(rpc, "start", p, strlen(p), getkey) != ARok
	|| dorpc(rpc, "write", chal, nchal, getkey) != ARok
	|| dorpc(rpc, "read", nil, 0, getkey) != ARok){
		free(p);
		close(afd);
		auth_freerpc(rpc);
		return -1;
	}
	free(p);

	if(rpc->narg < nresp)
		nresp = rpc->narg;
	memmove(resp, rpc->arg, nresp);

	if(ai != nil)
		*ai = auth_getinfo(rpc);

	if((a = auth_attr(rpc)) != nil
	&& (s = _strfindattr(a, "user")) != nil && strlen(s) < nuser)
		strcpy(user, s);
	else if(nuser > 0)
		user[0] = '\0';

	_freeattr(a);
	close(afd);
	auth_freerpc(rpc);
	return nresp;	
}

int
auth_respond(void *chal, uint nchal, char *user, uint nuser, void *resp, uint nresp,
	AuthGetkey *getkey, char *fmt, ...)
{
	va_list arg;
	int ret;

	va_start(arg, fmt);
	ret = dorespond(chal, nchal, user, nuser, resp, nresp, nil, getkey, fmt, arg);
	va_end(arg);
	return ret;
}

int
auth_respondAI(void *chal, uint nchal, char *user, uint nuser, void *resp, uint nresp,
	AuthInfo **ai, AuthGetkey *getkey, char *fmt, ...)
{
	va_list arg;
	int ret;

	va_start(arg, fmt);
	ret = dorespond(chal, nchal, user, nuser, resp, nresp, ai, getkey, fmt, arg);
	va_end(arg);
	return ret;
}
