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
 * we still use authid, authdom and machkey for authentication.
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

/* authentication structure */
struct	Auth
{
	int	inuse;
	char	uname[NAMELEN];	/* requestor's remote user name */
	char	aname[NAMELEN];	/* requested aname */
	Userid	uid;		/* uid decided on */
	AuthRpc *rpc;
};

Auth*	auths;
Lock	authlock;

void
authinit(void)
{
	auths = malloc(conf.nauth * sizeof(*auths));
}

static int
failure(Auth *s, char *why)
{
	AuthRpc *rpc;

	if(why && *why)print("authentication failed: %s: %r\n", why);
	s->uid = -1;
	if(rpc = s->rpc){
		s->rpc = 0;
		auth_freerpc(rpc);
	}
	return -1;
}

Auth*
authnew(char *uname, char *aname)
{
	static int si = 0;
	int afd, i, nwrap;
	Auth *s;

	i = si;
	nwrap = 0;
	for(;;){
		if(i < 0 || i >= conf.nauth){
			if(++nwrap > 1)
				return nil;
			i = 0;
		}
		s = &auths[i++];
		if(s->inuse)
			continue;
		lock(&authlock);
		if(s->inuse == 0){
			s->inuse = 1;
			strncpy(s->uname, uname, NAMELEN-1);
			strncpy(s->aname, aname, NAMELEN-1);
			failure(s, "");
			si = i;
			unlock(&authlock);
			break;
		}
		unlock(&authlock);
	}
	if((afd = open("/mnt/factotum/rpc", ORDWR)) < 0){
		failure(s, "open /mnt/factotum/rpc");
		return s;
	}
	if((s->rpc = auth_allocrpc(afd)) == 0){
		failure(s, "auth_allocrpc");
		close(afd);
		return s;
	}
	if(auth_rpc(s->rpc, "start", "proto=p9any role=server", 23) != ARok)
		failure(s, "auth_rpc: start");
	return s;
}

void
authfree(Auth *s)
{
	if(s){
		failure(s, "");
		s->inuse = 0;
	}
}

int
authread(File* file, uchar* data, int n)
{
	AuthInfo *ai;
	Auth *s;

	s = file->auth;
	if(s == nil)
		return -1;
	if(s->rpc == nil)
		return -1;
	switch(auth_rpc(s->rpc, "read", nil, 0)){
	default:
		failure(s, "auth_rpc: read");
		break;
	case ARdone:
		if((ai = auth_getinfo(s->rpc)) == nil){
			failure(s, "auth_getinfo failed");
			break;
		}
		if(ai->cuid == nil || *ai->cuid == '\0'){
			failure(s, "auth with no cuid");
			auth_freeAI(ai);
			break;
		}
		failure(s, "");
		s->uid = strtouid(ai->cuid);
		auth_freeAI(ai);
		return 0;
	case ARok:
		if(n < s->rpc->narg)
			break;
		memmove(data, s->rpc->arg, s->rpc->narg);
		return s->rpc->narg;
	}
	return -1;
}

int
authwrite(File* file, uchar *data, int n)
{
	Auth *s;

	s = file->auth;
	if(s == nil)
		return -1;
	if(s->rpc == nil)
		return -1;
	if(auth_rpc(s->rpc, "write", data, n) != ARok){
		failure(s, "auth_rpc: write");
		return -1;
	}
	return n;
}

int
authuid(Auth* s)
{
	return s->uid;
}

char*
authaname(Auth* s)
{
	return s->aname;
}

char*
authuname(Auth* s)
{
	return s->uname;
}
