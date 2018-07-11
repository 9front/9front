#include <u.h>
#include <libc.h>
#include <bio.h>
#include <mp.h>
#include <libsec.h>
#include <auth.h>
#include <authsrv.h>
#include <ndb.h>

int havenvram, havekeyfs;
Nvrsafe nvr;
uchar keyfskey[AESKEYLEN];
char eve[128];
Ndb *db;

void
geteve(void)
{
	int fd;
	
	fd = open("#c/hostowner", OREAD);
	if(fd < 0) sysfatal("open: %r");
	memset(eve, 0, sizeof(eve));
	if(read(fd, eve, sizeof(eve)-1) < 0) sysfatal("read: %r");
	close(fd);
	if(strcmp(getuser(), eve) != 0) print("hostowner is %#q, but running as %#q\n", eve, getuser());
}

void
ndb(void)
{
	db = ndbopen(nil);
	if(db == nil){
		print("ndbopen: %r\n");
		return;
	}
}

void
nvram(void)
{
	char *auth;

	if(readnvram(&nvr, 0) < 0){
		print("readnvram: %r\n");
		return;
	}
	havenvram = 1;
	print("GOOD: found nvram key for user '%s@%s'\n", nvr.authid, nvr.authdom);
	if(strcmp(eve, nvr.authid) != 0) print("BAD: nvram authid doesn't match hostowner %#q\n", eve);
	if(db != nil){
		auth = ndbgetvalue(db, nil, "authdom", nvr.authdom, "auth", nil);
		if(auth == nil) print("BAD: authdom %#q not found in ndb\n", nvr.authdom);
		else{
			print("ndb says authdom %#q corresponds to auth server %#q\n", nvr.authdom, auth);
			free(auth);
		}
	}
}

void
keyfs(void)
{
	char *buf;
	int fd;
	char aes[AESKEYLEN];

	if(!havenvram) return;
	if(access("/adm/keys", AREAD) < 0){
		print("no access to /adm/keys\n");
		return;
	}
	print("starting keyfs\n");
	rfork(RFNAMEG);
	switch(fork()){
	case -1:
		sysfatal("fork: %r");
	case 0:
		if(execl("/bin/auth/keyfs", "auth/keyfs", "-r", nil) < 0)
			sysfatal("execl: %r");
	}
	waitpid();
	buf = smprint("/mnt/keys/%s/aeskey", nvr.authid);
	fd = open(buf, OREAD);
	if(fd < 0){
		print("BAD: can't get key from keyfs: %r\n");
		return;
	}
	werrstr("short read");
	if(read(fd, aes, sizeof(aes)) < sizeof(aes)){
		print("read: %r\n");
		close(fd);
		return;
	}
	havekeyfs = 1;
	memmove(keyfskey, aes, AESKEYLEN);
	if(memcmp(nvr.aesmachkey, aes, AESKEYLEN) != 0)
		print("BAD: key in keyfs does not match nvram\n");
	else
		print("GOOD: key in keyfs matches nvram\n");
	close(fd);
}

int
checkkey(void)
{
	Biobuf *bp;
	Attr *at, *ap;
	char *l;
	int proto, dom, user;

	bp = Bopen("/mnt/factotum/ctl", OREAD);
	if(bp == nil){
		print("can't open /mnt/factotum/ctl: %r\n");
		return 0;
	}
	proto = dom = user = 0;
	while(l = Brdstr(bp, '\n', 1), l != nil){
		if(strncmp(l, "key ", 4) != 0) continue;
		at = _parseattr(l + 4);
		free(l);
		proto = dom = user = 0;
		for(ap = at; ap != nil; ap = ap->next){
			if(strcmp(ap->name, "proto") == 0 && strcmp(ap->val, "dp9ik") == 0)
				proto++;
			if(strcmp(ap->name, "dom") == 0 && strcmp(ap->val, nvr.authdom) == 0)
				dom++;
			if(strcmp(ap->name, "user") == 0 && strcmp(ap->val, nvr.authid) == 0)
				user++;
			if(strcmp(ap->name, "disabled") == 0){
				proto = dom = user = 0;
				break;
			}
		}
		_freeattr(at);
		if(proto && dom && user)
			break;
	}
	Bterm(bp);
	if(!proto || !dom || !user){
		print("can't test factotum key: no key for '%s@%s' in factotum\n", nvr.authdom, nvr.authid);
		return 0;
	}
	return 1;
}

int
trykey(int keyfs)
{
	int fd, rc;
	AuthRpc *rpc;
	Authkey ak;
	PAKpriv pr;
	Ticketreq tr;
	Ticket t;
	Authenticator a;
	char *s;
	uchar chal[CHALLEN], paky[PAKYLEN];
	char tick[MAXTICKETLEN+MAXAUTHENTLEN];
	char errbuf[ERRMAX];
	char *source;
	
	source = keyfs ? "keyfs" : "nvram";
	fd = open("/mnt/factotum/rpc", ORDWR);
	if(fd < 0){ print("open: %r\n"); return -1; }
	print("trying %s key for %s@%s with factotum\n", source, nvr.authdom, nvr.authid);
	rpc = auth_allocrpc(fd);
	if(rpc == nil){ print("auth_allocrpc: %r\n"); return -1; }
	
	s = smprint("proto=dp9ik dom=%q user=%q role=server", nvr.authdom, nvr.authid);
	if(auth_rpc(rpc, "start", s, strlen(s)) != ARok){ print("auth_rpc start: %r\n"); goto err; } 
	free(s);
	
	genrandom(chal, CHALLEN);
	if(auth_rpc(rpc, "write", chal, CHALLEN) != ARok){ print("BAD: auth_rpc write challenge: %r\n"); goto err; }

	if(auth_rpc(rpc, "read", nil, 0) != ARok){ print("BAD: auth_rpc read ticket request: %r\n"); goto err; }
	rc = convM2TR(rpc->arg, rpc->narg, &tr);
	memset(&ak, 0, sizeof(ak));
	memmove(ak.aes, nvr.aesmachkey, AESKEYLEN);
	
	authpak_hash(&ak, nvr.authid);
	authpak_new(&pr, &ak, paky, 0);
	authpak_finish(&pr, &ak, (uchar *)rpc->arg + rc);
	if(auth_rpc(rpc, "write", paky, PAKYLEN) != ARok){ print("BAD: auth_rpc write public key: %r\n"); goto err; }

	t.num = AuthTs;	
	memmove(t.chal, tr.chal, CHALLEN);
	strcpy(t.cuid, nvr.authid);
	strcpy(t.suid, nvr.authid);
	genrandom(t.key, sizeof(t.key));
	t.form = 1;
	rc = convT2M(&t, tick, MAXTICKETLEN, &ak);
	
	a.num = AuthAc;
	memmove(a.chal, tr.chal, CHALLEN);
	genrandom(a.rand, NONCELEN);
	rc += convA2M(&a, tick+rc, MAXAUTHENTLEN, &t);
	if(auth_rpc(rpc, "write", tick, rc) != ARok){
		rerrstr(errbuf, sizeof(errbuf));
		if(strcmp(errbuf, "auth server protocol botch") == 0)
			print("BAD: factotum key doesn't seem to match %s (auth_rpc write ticket+authenticator: %r)\n", source);
		else
			print("BAD: auth_rpc write ticket+authenticator: %r\n");
		goto err;
	}
	
	if(auth_rpc(rpc, "read", nil, 0) != ARok){ print("BAD: auth_rpc read authenticator: %r\n"); goto err; }
	if(convM2A(rpc->arg, rpc->narg, &a, &t) <= 0) goto botch;
	if(a.num != AuthAs || memcmp(a.chal, chal, CHALLEN) != 0) goto botch;
	print("GOOD: key in factotum matches %s\n", source);
	auth_freerpc(rpc);
	close(fd);
	return 0;
botch:
	print("BAD: factotum: protocol botch\n");	
err:
	auth_freerpc(rpc);
	close(fd);
	return -1;
}

void
factotum(void)
{
	if(!havenvram || !checkkey()) return;
	if(trykey(0) < 0 && havekeyfs && memcmp(keyfskey, nvr.aesmachkey, AESKEYLEN) != 0)
		trykey(1);
}

void
main()
{
	quotefmtinstall();
	geteve();
	ndb();
	nvram();
	keyfs();
	factotum();
}
