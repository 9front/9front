#include <u.h>
#include <libc.h>
#include <bio.h>
#include <authsrv.h>
#include <ndb.h>

int havenvram;
Nvrsafe nvr;
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
	print("found nvram key for user '%s@%s'\n", nvr.authid, nvr.authdom);
	if(strcmp(eve, nvr.authid) != 0) print("nvram authid doesn't match hostowner %#q\n", eve);
	if(db != nil){
		auth = ndbgetvalue(db, nil, "authdom", nvr.authdom, "auth", nil);
		if(auth == nil) print("authdom %#q not found in ndb\n", nvr.authdom);
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
		print("can't get key from keyfs: %r\n");
		return;
	}
	werrstr("short read");
	if(read(fd, aes, sizeof(aes)) < sizeof(aes)){
		print("read: %r\n");
		close(fd);
		return;
	}
	if(memcmp(nvr.aesmachkey, aes, AESKEYLEN) != 0)
		print("key in keyfs does not match nvram\n");
	else
		print("key in keyfs matches nvram\n");
	close(fd);
}

void
main()
{
	quotefmtinstall();
	geteve();
	ndb();
	nvram();
	keyfs();
}
