#include <u.h>
#include <libc.h>
#include <auth.h>
#include <authsrv.h>
#include <bio.h>
#include <ndb.h>

char *authdom;

void
setenv(char *var, char *val)
{
	int fd;

	fd = create(var, OWRITE, 0644);
	if(fd < 0)
		print("init: can't open %s\n", var);
	else{
		fprint(fd, val);
		close(fd);
	}
}

/*
 *  become the authenticated user
 */
void
chuid(AuthInfo *ai)
{
	int rv, fd;

	/* change uid */
	fd = open("/dev/capuse", OCEXEC|OWRITE);
	if(fd < 0)
		sysfatal("can't change uid: %r");
	rv = write(fd, ai->cap, strlen(ai->cap));
	close(fd);
	if(rv < 0)
		sysfatal("can't change uid: %r");
}

/*
 *  mount a factotum
 */
void
mountfactotum(char *srvname)
{
	int fd;

	/* mount it */
	fd = open(srvname, ORDWR);
	if(fd < 0)
		sysfatal("opening factotum: %r");
	mount(fd, -1, "/mnt", MBEFORE, "");
	close(fd);
}

/*
 * find authdom
 */
char*
getauthdom(void)
{
	char *sysname, *s;
	Ndbtuple *t, *p;

	if(authdom != nil)
		return authdom;

	sysname = getenv("sysname");
	if(sysname == nil)
		return strdup("cs.bell-labs.com");

	s = "authdom";
	t = csipinfo(nil, "sys", sysname, &s, 1);
	free(sysname);
	for(p = t; p != nil; p = p->entry)
		if(strcmp(p->attr, s) == 0){
			authdom = strdup(p->val);
			break;
		}
	ndbfree(t);
	return authdom;
}

/*
 *  start a new factotum and pass it the username and password
 */
void
startfactotum(char *user, char *password, char *srvname)
{
	int fd;

	strcpy(srvname, "/srv/factotum.XXXXXXXXXXX");
	mktemp(srvname);

	switch(fork()){
	case -1:
		sysfatal("can't start factotum: %r");
	case 0:
		execl("/boot/factotum", "loginfactotum", "-ns", srvname+5, nil);
		sysfatal("starting factotum: %r");
		break;
	}
	waitpid();

	/* mount it */
	mountfactotum(srvname);

	/* write in new key */
	fd = open("/mnt/factotum/ctl", ORDWR);
	if(fd < 0)
		sysfatal("opening factotum: %r");
	fprint(fd, "key proto=dp9ik dom=%q user=%q !password=%q\n", getauthdom(), user, password);
	fprint(fd, "key proto=p9sk1 dom=%q user=%q !password=%q\n", getauthdom(), user, password);
	close(fd);
}

void
usage(void)
{
	fprint(2, "usage: %s [-a authdom] user\n", argv0);
	exits("");
}

void
main(int argc, char *argv[])
{
	char buf[2*ANAMELEN];
	char home[2*ANAMELEN];
	char srvname[2*ANAMELEN];
	char *user, *pass, *sysname, *tz, *cputype, *service;
	AuthInfo *ai;

	quotefmtinstall();

	ARGBEGIN{
	case 'a':
		authdom = EARGF(usage());
		break;
	default:
		usage();
		break;
	}ARGEND;

	if(argc != 1)
		usage();

	rfork(RFENVG|RFNAMEG);

	service = getenv("service");
	if(strcmp(service, "cpu") == 0)
		fprint(2, "login: warning: running on a cpu server!\n");
	if(argc != 1){
		fprint(2, "usage: login username\n");
		exits("usage");
	}
	user = argv[0];
	pass = readcons("Password", nil, 1);
	if(pass == nil)
		exits("no password");

	/* authenticate */
	ai = auth_userpasswd(user, pass);
	if(ai == nil || ai->cap == nil)
		sysfatal("login incorrect");

	/* change uid */
	chuid(ai);

	/* start a new factotum and hand it a new key */
	startfactotum(user, pass, srvname);

	memset(pass, 0, strlen(pass));
	free(pass);

	/* set up new namespace */
	newns(ai->cuid, nil);
	auth_freeAI(ai);

	/* remount the factotum */
	mountfactotum(srvname);

	/* get rid of srvname */
	remove(srvname);

	/* set up a new environment */
	cputype = getenv("cputype");
	sysname = getenv("sysname");
	tz = getenv("timezone");
	rfork(RFCENVG);
	setenv("#e/service", "con");
	setenv("#e/user", user);
	snprint(home, sizeof(home), "/usr/%s", user);
	setenv("#e/home", home);
	setenv("#e/cputype", cputype);
	setenv("#e/objtype", cputype);
	if(sysname != nil)
		setenv("#e/sysname", sysname);
	if(tz != nil)
		setenv("#e/timezone", tz);

	/* go to new home directory */
	snprint(buf, sizeof(buf), "/usr/%s", user);
	if(chdir(buf) < 0)
		chdir("/");

	/* read profile and start interactive rc */
	execl("/bin/rc", "rc", "-li", nil);
	exits(nil);
}
