#include <u.h>
#include <libc.h>
#include <bio.h>
#include <libsec.h>
#include <authsrv.h>
#include "authcmdlib.h"

void	install(char*, char*, Authkey*, long, int);
int	exists(char*, char*);

void
usage(void)
{
	fprint(2, "usage: changeuser [-pn] user\n");
	exits("usage");
}

void
main(int argc, char *argv[])
{
	char *u, pass[32];
	int which, newkey, newbio, dosecret;
	long t;
	Authkey key;
	Acctbio a;
	Fs *f;

	fmtinstall('K', deskeyfmt);

	which = 0;
	ARGBEGIN{
	case 'p':
		which |= Plan9;
		break;
	case 'n':
		which |= Securenet;
		break;
	default:
		usage();
	}ARGEND
	argv0 = "changeuser";

	if(argc != 1)
		usage();
	u = *argv;
	if(memchr(u, '\0', ANAMELEN) == 0)
		error("bad user name");

	if(!which)
		which = Plan9;

	private();
	newbio = 0;
	t = 0;
	a.user = 0;
	memset(&key, 0, sizeof(key));
	if(which & Plan9){
		f = &fs[Plan9];
		newkey = !exists(f->keys, u) || answer("assign new Plan 9 password?");
		if(newkey)
			getpass(&key, pass, 1, 1);
		dosecret = answer("assign new Inferno/POP secret?");
		if(dosecret)
			if(!newkey || !answer("make it the same as Plan 9 password?"))
				getpass(nil, pass, 0, 1);
		t = getexpiration(f->keys, u);
		install(f->keys, u, &key, t, newkey);
		if(dosecret && setsecret(KEYDB, u, pass) == 0)
			error("error writing Inferno/POP secret");
		if(querybio(f->who, u, &a))
			wrbio(f->who, &a);
		print("user %s installed for Plan 9\n", u);
		syslog(0, AUTHLOG, "user %s installed for plan 9", u);
	}
	if(which & Securenet){
		f = &fs[Securenet];
		newkey = !exists(f->keys, u) || answer("assign new Securenet key?");
		if(newkey)
			genrandom((uchar*)key.des, DESKEYLEN);
		if(a.user == 0){
			t = getexpiration(f->keys, u);
			newbio = querybio(f->who, u, &a);
		}
		install(f->keys, u, &key, t, newkey);
		if(newbio)
			wrbio(f->who, &a);
		if(!finddeskey(f->keys, u, key.des))
			error("error reading Securenet key");
		print("user %s: SecureNet key: %K\n", u, key.des);
		checksum(key.des, pass);
		print("verify with checksum %s\n", pass);
		print("user %s installed for SecureNet\n", u);
		syslog(0, AUTHLOG, "user %s installed for securenet", u);
	}
	exits(0);
}

void
install(char *db, char *u, Authkey *key, long t, int newkey)
{
	char buf[KEYDBBUF+ANAMELEN+20];
	int fd;

	if(!exists(db, u)){
		snprint(buf, sizeof(buf), "%s/%s", db, u);
		fd = create(buf, OREAD, 0777|DMDIR);
		if(fd < 0)
			error("can't create user %s: %r", u);
		close(fd);
	}

	if(newkey && !setkey(db, u, key))
		error("can't set key: %r");

	if(t == -1)
		return;
	snprint(buf, sizeof(buf), "%s/%s/expire", db, u);
	fd = open(buf, OWRITE);
	if(fd < 0 || fprint(fd, "%ld", t) < 0)
		error("can't write expiration time");
	close(fd);
}

int
exists(char *db, char *u)
{
	char buf[KEYDBBUF+ANAMELEN+6];

	snprint(buf, sizeof(buf), "%s/%s/expire", db, u);
	if(access(buf, 0) < 0)
		return 0;
	return 1;
}
