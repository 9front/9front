#include "all.h"
#include "io.h"
#include <auth.h>

char*
nvrgetconfig(void)
{
	return conf.confdev;
}

int
nvrsetconfig(char* word)
{
	/* config block is on device `word' */
	USED(word);
	return 0;
}

static char *keyspec = "proto=p9any role=server";

void*
authnew(void)
{
	AuthRpc *rpc;
	int fd;

	if(access("/mnt/factotum", 0) < 0)
		if((fd = open("/srv/factotum", ORDWR)) >= 0)
			mount(fd, -1, "/mnt", MBEFORE, "");
	if((fd = open("/mnt/factotum/rpc", ORDWR)) < 0)
		return nil;
	if((rpc = auth_allocrpc(fd)) == nil){
		close(fd);
		return nil;
	}
	if(auth_rpc(rpc, "start", keyspec, strlen(keyspec)) != ARok){
		authfree(rpc);
		return nil;
	}
	return rpc;
}

void
authfree(void *auth)
{
	AuthRpc *rpc;

	if(rpc = auth){
		close(rpc->afd);
		auth_freerpc(rpc);
	}
}

int
authread(File *file, uchar *data, int count)
{
	AuthInfo *ai;
	AuthRpc *rpc;
	Chan *chan;

	chan = file->cp;
	if((rpc = file->auth) == nil){
		snprint(chan->err, sizeof(chan->err),
			"not an auth fid");
		return -1;
	}

	switch(auth_rpc(rpc, "read", nil, 0)){
	default:
		snprint(chan->err, sizeof(chan->err),
			"authread: auth protocol not finished");
		return -1;
	case ARdone:
		if((ai = auth_getinfo(rpc)) == nil)
			goto Phase;
		file->uid = strtouid(ai->cuid);
		if(file->uid < 0){
			snprint(chan->err, sizeof(chan->err),
				"unknown user '%s'", ai->cuid);
			auth_freeAI(ai);
			return -1;
		}
		auth_freeAI(ai);
		return 0;
	case ARok:
		if(count < rpc->narg){
			snprint(chan->err, sizeof(chan->err),
				"not enough data in auth read");
			return -1;
		}
		memmove(data, rpc->arg, rpc->narg);
		return rpc->narg;
	case ARphase:
	Phase:
		rerrstr(chan->err, sizeof(chan->err));
		return -1;
	}
}

int
authwrite(File *file, uchar *data, int count)
{
	AuthRpc *rpc;
	Chan *chan;

	chan = file->cp;
	if((rpc = file->auth) == nil){
		snprint(chan->err, sizeof(chan->err),
			"not an auth fid");
		return -1;
	}
	if(auth_rpc(rpc, "write", data, count) != ARok){
		rerrstr(chan->err, sizeof(chan->err));
		return -1;
	}
	return count;
}

