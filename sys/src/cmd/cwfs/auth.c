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
		print("nvrcheck: can't read nvram\n");
		return 1;
	} else
		gotnvr = 1;
	print("nvr read\n");

	csum = nvcsum(nvr.machkey, sizeof nvr.machkey);
	if(csum != nvr.machsum) {
		print("\n\n ** NVR key checksum is incorrect  **\n");
		print(" ** set password to allow attaches **\n\n");
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
	char nkey1[DESKEYLEN];
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
		memset(nkey1, 0, DESKEYLEN);
		passtokey(nkey1, ln);
		if(memcmp(nkey1, nvr.machkey, DESKEYLEN) == 0) {
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
		auth_freerpc(rpc);
		return nil;
	}
	return rpc;
}

void
authfree(void *auth)
{
	AuthRpc *rpc;

	if(rpc = auth)
		auth_freerpc(rpc);
}

int
authread(File *file, uchar *data, int count)
{
	AuthInfo *ai;
	AuthRpc *rpc;

	if((rpc = file->auth) == nil)
		return -1;
	switch(auth_rpc(rpc, "read", nil, 0)){
	case ARdone:
		if((ai = auth_getinfo(rpc)) == nil)
			return -1;
		file->uid = strtouid(ai->cuid);
		auth_freeAI(ai);
		if(file->uid < 0)
			return -1;
		return 0;
	case ARok:
		if(count < rpc->narg)
			return -1;
		memmove(data, rpc->arg, rpc->narg);
		return rpc->narg;
	case ARphase:
		return -1;
	default:
		return -1;
	}
}

int
authwrite(File *file, uchar *data, int count)
{
	AuthRpc *rpc;

	if((rpc = file->auth) == nil)
		return -1;
	if(auth_rpc(rpc, "write", data, count) != ARok)
		return -1;
	return count;
}

