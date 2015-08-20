#include "all.h"
#include "io.h"
#include <authsrv.h>
#include <auth.h>

Nvrsafe	nvr;

static int gotnvr;	/* flag: nvr contains nvram; it could be bad */

char*
nvrgetconfig(void)
{
	return conf.confdev;
}

/*
 * we shouldn't be writing nvram any more.
 * the secstore/config field is now just secstore key.
 */

int
nvrcheck(void)
{
	uchar csum;

	if (readnvram(&nvr, NVread) < 0) {
		fprint(2, "nvrcheck: can't read nvram\n");
		return 1;
	} else
		gotnvr = 1;

	if(chatty)
		print("nvr read\n");

	csum = nvcsum(nvr.machkey, sizeof nvr.machkey);
	if(csum != nvr.machsum) {
		fprint(2, "\n\n ** NVR key checksum is incorrect  **\n");
		fprint(2, " ** set password to allow attaches **\n\n");
		memset(nvr.machkey, 0, sizeof nvr.machkey);
		return 1;
	}

	return 0;
}

int
nvrsetconfig(char* word)
{
	/* config block is on device `word' */
	USED(word);
	return 0;
}

int
conslock(void)
{
	char *ln;
	Authkey nkey1;
	static char zeroes[DESKEYLEN];

	if(memcmp(nvr.machkey, zeroes, DESKEYLEN) == 0) {
		print("no password set\n");
		return 0;
	}

	for(;;) {
		print("%s password:", service);
		/* could turn off echo here */

		if ((ln = Brdline(&bin, '\n')) == nil)
			return 0;
		ln[Blinelen(&bin)-1] = '\0';

		/* could turn on echo here */
		passtokey(&nkey1, ln);
		if(memcmp(nkey1.des, nvr.machkey, DESKEYLEN) == 0) {
			prdate();
			break;
		}

		print("Bad password\n");
		delay(1000);
	}
	return 1;
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

