#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ip.h>
#include <libsec.h>
#include <auth.h>

#include <String.h>
#include "glob.h"

enum {
	Tascii,
	Timage,

	Maxpath = 512,
	Maxwait = 1000 * 60 * 30, /* 30 minutes */
};

typedef struct Passive Passive;
typedef struct Ftpd Ftpd;
typedef struct Cmd Cmd;

struct Passive {
	int inuse;
	char adir[40];
	int afd;
	int port;
	uchar ipaddr[IPaddrlen];
};

struct Ftpd {
	Biobuf *in, *out;

	struct conn {
		int tlson, tlsondata;
		NetConnInfo *nci;
		TLSconn *tls;
		uchar *cert;
		int certlen;
		char data[64];
		Passive pasv;
	} conn;

	struct user {
		char cwd[Maxpath];
		char name[Maxpath];
		int loggedin;
		int isnone;
	} user;

	int type;
	vlong offset;
	int cmdpid;
	char *renamefrom;
};

struct Cmd {
	char *name;
	int (*fn)(Ftpd *, char *);
	int needlogin;
	int needtls;
	int asproc;
};

char *certpath;
char *namespace = "/lib/namespace.ftp";
int implicittls;
int debug;
int anonok;
int anononly;
int anonall;

void 
dprint(char *fmt, ...)
{
	char *msg;
	va_list arg;

	if(!debug) return;

	va_start(arg, fmt);
	msg = vsmprint(fmt, arg);
	va_end(arg);

	syslog(0, "ftp", msg);
	free(msg);
}

void 
logit(char *fmt, ...)
{
	char *msg;
	va_list arg;

	va_start(arg, fmt);
	msg = vsmprint(fmt, arg);
	va_end(arg);

	syslog(0, "ftp", msg);
	free(msg);
}

int 
reply(Biobuf *bio, char *fmt, ...)
{
	va_list arg;
	char buf[Maxpath], *s;

	va_start(arg, fmt);
	s = vseprint(buf, buf + sizeof(buf) - 3, fmt, arg);
	va_end(arg);

	dprint("rpl: %s", buf);

	*s++ = '\r';
	*s++ = '\n';
	Bwrite(bio, buf, s - buf);
	Bflush(bio);

	return 0;
}

void
asproc(Ftpd *ftpd, int (*f)(Ftpd *, char *), char *arg)
{
	int i;

	if(ftpd->cmdpid) {
		for(;;) {
			i = waitpid();
			if(i == ftpd->cmdpid || i < 0)
				break;
		}
	}

	switch(ftpd->cmdpid = rfork(RFFDG|RFPROC|RFNOTEG)){
	case -1:
		reply(ftpd->out, "450 Out of processes: %r");
		return;
	case 0:
		(*f)(ftpd, arg);
		dprint("proc exiting");
		exits(nil);
	default:
		break;
	}
}

int 
mountnet(Ftpd *ftpd)
{
	if(bind("#/", "/", MAFTER) == -1) {
		reply(ftpd->out, "500 can't bind #/ to /: %r");
		return -1;
	}

	if(bind(ftpd->conn.nci->spec, "/net", MBEFORE) == -1) {
		reply(ftpd->out, "500 can't bind %s to /net: %r", ftpd->conn.nci->spec);
		unmount("#/", "/");
		return -1;
	}

	return 0;
}

void 
unmountnet(void)
{
	unmount(nil, "/net");
	unmount("#/", "/");
}

Biobuf *
dialdata(Ftpd *ftpd, int read)
{
	Biobuf *bio;
	TLSconn *tls;
	int fd, cfd;
	char ldir[40];

	if(mountnet(ftpd) < 0)
		return nil;

	if(!ftpd->conn.pasv.inuse) {
		fd = dial(ftpd->conn.data, "20", 0, 0);
	} else {
		fd = -1;
		alarm(30 * 1000); /* wait 30 seconds */
		dprint("dbg: waiting for passive connection");
		cfd = listen(ftpd->conn.pasv.adir, ldir);
		alarm(0);

		if(cfd >= 0) {
			fd = accept(cfd, ldir);
			close(cfd);
		}
	}

	if(fd < 0) {
		reply(ftpd->out, "425 Error opening data connection");
		unmountnet();
		return nil;
	}

	reply(ftpd->out, "150 Opened data connection");

	tls = nil;
	if(ftpd->conn.tlsondata) {
		dprint("dbg: using tls on data channel");

		tls = mallocz(sizeof(TLSconn), 1);
		tls->cert = malloc(ftpd->conn.certlen);
		memcpy(tls->cert, ftpd->conn.cert, ftpd->conn.certlen);
		tls->certlen = ftpd->conn.certlen;
		fd = tlsServer(fd, tls);

		if(fd < 0) {
			reply(ftpd->out, "425 TLS on data connection failed");
			unmountnet();
			return nil;
		}

		dprint("dbg: tlsserver done");
	}

	unmountnet();
	if(read)
		bio = Bfdopen(fd, OREAD);
	else
		bio = Bfdopen(fd, OWRITE);
	bio->aux = tls;

	return bio;
}

void 
closedata(Ftpd *ftpd, Biobuf *bio, int fail)
{
	TLSconn *conn;

	conn = bio->aux;

	Bflush(bio);
	Bterm(bio);
	if(!fail)
		reply(ftpd->out, "226 Transfer complete");

	if(conn) {
		free(conn->cert);
		free(conn);
	}
}

int 
starttls(Ftpd *ftpd)
{
	int fd;

	fd = tlsServer(0, ftpd->conn.tls);
	if(fd < 0)
		return -1;

	dup(fd, 0);
	dup(fd, 1);
	ftpd->conn.tlson = 1;

	return 0;
}

int
abortcmd(Ftpd *ftpd, char *arg)
{
	USED(arg);

	if(ftpd->cmdpid){
		if(postnote(PNPROC, ftpd->cmdpid, "kill") == 0)
			reply(ftpd->out, "426 Command aborted");
		else
			logit("postnote pid %d %r", ftpd->cmdpid);
	}
	return reply(ftpd->out, "226 Abort processed");
}

int 
authcmd(Ftpd *ftpd, char *arg)
{
	if((cistrcmp(arg, "TLS") == 0) || (cistrcmp(arg, "TLS-C") == 0) || (cistrcmp(arg, "SSL") == 0)) {

		if(!ftpd->conn.tls)
			return reply(ftpd->out, "431 tls not enabled");

		reply(ftpd->out, "234 starting tls");
		if(starttls(ftpd) < 0)
			return reply(ftpd->out, "431 tls failed");
	} else {
		return reply(ftpd->out, "502 security method %s not understood", arg);
	}

	return 0;
}

int 
cwdcmd(Ftpd *ftpd, char *arg)
{
	char buf[Maxpath];

	if(!arg || *arg == '\0') {
		if(ftpd->user.isnone)
			snprint(buf, Maxpath, "/");
		else
			snprint(buf, Maxpath, "/usr/%s", ftpd->user.name);
	} else {
		strncpy(buf, arg, Maxpath);
		cleanname(buf);
	}

	if(chdir(buf) < 0)
		return reply(ftpd->out, "550 CWD failed: %r");

	getwd(ftpd->user.cwd, Maxpath);
	return reply(ftpd->out, "200 Directory changed to %s", ftpd->user.cwd);
}

int 
deletecmd(Ftpd *ftpd, char *arg)
{
	if(!arg)
		return reply(ftpd->out, "501 Rmdir/Delete command needs an argument");
	if(ftpd->user.isnone)
		return reply(ftpd->out, "550 Permission denied");
	if(remove(cleanname(arg)) < 0)
		return reply(ftpd->out, "550 Can't remove %s: %r", arg);
	else
		return reply(ftpd->out, "226 \"%s\" removed", arg);
}

int 
featcmd(Ftpd *ftpd, char *arg)
{
	USED(arg);
	reply(ftpd->out, "211-Features supported");
	reply(ftpd->out, " UTF8");
	reply(ftpd->out, " PBSZ");
	reply(ftpd->out, " PROT");
	reply(ftpd->out, " AUTH TLS");
	reply(ftpd->out, " MLST Type*;Size*;Modify*;Unix.groupname*;UNIX.ownername*;");
	return reply(ftpd->out, "211 End");
}

int 
dircmp(void *va, void *vb)
{
	Dir *a, *b;

	a = va;
	b = vb;

	return strcmp(a->name, b->name);
}

void
listdir(Ftpd *ftpd, Biobuf *data, char *path, void (*fn)(Biobuf *, Dir *d, char *dirname))
{
	Dir *dirbuf;
	int fd;
	long ndirs;
	long i;

	fd = open(path, OREAD);
	if(!fd)
		return;

	ndirs = dirreadall(fd, &dirbuf);
	if(ndirs < 1)
		return;
	close(fd);

	qsort(dirbuf, ndirs, sizeof(Dir), dircmp);
	for(i=0;i<ndirs;i++)
		(*fn)(data, &dirbuf[i], (strcmp(path, ftpd->user.cwd) == 0 ? nil : path));

	free(dirbuf);
}

int
list(Ftpd *ftpd, char *arg, void (*fn)(Biobuf *, Dir *d, char *dirname))
{
	Biobuf *data;
	int argc, i;
	char *argv[32];
	Globlist *gl;
	char *path;
	Dir *d;

	if(arg) {
		argc = getfields(arg, argv, sizeof(argv)-1, 1, " \t");
	} else {
		argc = 1;
		argv[0] = ftpd->user.cwd;
	}

	data = dialdata(ftpd, 0);
	if(!data)
		return reply(ftpd->out, "500 List failed: couldn't dial data");

	for(i=0;i<argc;i++) {
		gl = glob(argv[i]);
		if(!gl)
			continue;

		while(path = globiter(gl)) {
			cleanname(path);

			logit("list: path %s user %s", path, ftpd->user.name);

			d = dirstat(path);
			if(d->mode & DMDIR)
				listdir(ftpd, data, path, fn);
			else
				(*fn)(data, d, nil);

			free(d);
		}
	}

	closedata(ftpd, data, 0);

	return 0;
}

char *
mode2asc(int m)
{
	char *asc;
	char *p;

	asc = strdup("----------");
	if(DMDIR & m)
		asc[0] = 'd';
	if(DMAPPEND & m)
		asc[0] = 'a';
	else if(DMEXCL & m)
		asc[3] = 'l';

	for(p = asc + 1; p < asc + 10; p += 3, m <<= 3) {
		if(m & 0400)
			p[0] = 'r';
		if(m & 0200)
			p[1] = 'w';
		if(m & 0100)
			p[2] = 'x';
	}

	return asc;
}

void 
listprint(Biobuf *data, Dir *d, char *dirname)
{
	char *ts, *mode;

	ts = strdup(ctime(d->mtime));
	ts[16] = '\0';
	if(time(0) - d->mtime > 6 * 30 * 24 * 60 * 60)
		memmove(ts + 11, ts + 23, 5);

	mode = mode2asc(d->mode);

	if(dirname)
		reply(data, "%s %3d %-8s %-8s %7lld %s %s/%s", 
			mode, 1, d->uid, d->gid, d->length, ts + 4, dirname, d->name);
	else
		reply(data, "%s %3d %-8s %-8s %7lld %s %s",
			mode, 1, d->uid, d->gid, d->length, ts + 4, d->name);

	free(mode);
	free(ts);
}

int 
listcmd(Ftpd *ftpd, char *arg)
{
	return list(ftpd, arg, listprint);
}

int 
loginuser(Ftpd *ftpd, char *pass, char *nsfile)
{
	char *user;

	user = ftpd->user.name;

	putenv("service", "ftp");
	if(!ftpd->user.isnone) {
		if(login(user, pass, nsfile) < 0)
			return reply(ftpd->out, "530 Not logged in: bad password");
	} else {
		if(newns(user, nsfile) < 0)
			return reply(ftpd->out, "530 Not logged in: user out of service");
	}

	getwd(ftpd->user.cwd, Maxpath);

	logit("login: %s in dir %s with ns %s",
		ftpd->user.name,
		ftpd->user.cwd,
		nsfile);

	ftpd->user.loggedin = 1;
	if(ftpd->user.isnone)
		return reply(ftpd->out, "230 Logged in: anonymous access");
	else
		return reply(ftpd->out, "230 Logged in");
}

void 
nlistprint(Biobuf *data, Dir *d, char*)
{
	reply(data, "%s", d->name);
}

int 
nlistcmd(Ftpd *ftpd, char *arg)
{
	return list(ftpd, arg, nlistprint);
}

int 
noopcmd(Ftpd *ftpd, char *arg)
{
	USED(arg);
	return reply(ftpd->out, "200 Plan 9 FTP Server still alive");
}

int
mkdircmd(Ftpd *ftpd, char *arg)
{
	int fd;

	if(!arg)
		reply(ftpd->out, "501 Mkdir command requires argument.");
	if(ftpd->user.isnone)
		reply(ftpd->out, "550 Permission denied");

	cleanname(arg);
	fd = create(arg, OREAD, DMDIR|0755);
	if(fd < 0)
		return reply(ftpd->out, "550 Can't create %s: %r", arg);
	close(fd);

	return reply(ftpd->out, "226 %s created", arg);
}

void 
mlsdprint(Biobuf *data, Dir *d, char*)
{
	Tm mtime;

	tmtime(&mtime, d->mtime, nil);
	reply(data, "Type=%s;Size=%d;Modify=%τ;Unix.groupname=%s;Unix.ownername=%s; %s", 
		(d->mode & DMDIR ? "dir" : "file"), d->length, tmfmt(&mtime, "YYYYMMDDhhmmss"), 
		d->gid, d->uid, d->name);
}

int 
mlsdcmd(Ftpd *ftpd, char *arg)
{
	return list(ftpd, arg, mlsdprint);
}

int 
mlstcmd(Ftpd *ftpd, char *arg)
{
	Dir *d;
	char *path;

	if(arg != nil)
		path = arg;
	else
		path = ftpd->user.cwd;

	d = dirstat(path);
	if(!d)
		return reply(ftpd->out, "500 Mlst failed: %r");

	reply(ftpd->out, "250-MLST %s", arg);
	Bprint(ftpd->out, " ");
	mlsdprint(ftpd->out, d, nil);
	free(d);

	return reply(ftpd->out, "250 End");
}

int 
optscmd(Ftpd *ftpd, char *arg)
{
	if(cistrcmp(arg, "utf8 on") == 0)
		return reply(ftpd->out, "200 UTF8 always on");

	return reply(ftpd->out, "501 Option not implemented");
}

int 
passcmd(Ftpd *ftpd, char *arg)
{
	char *nsfile;

	if(strlen(ftpd->user.name) == 0)
		return reply(ftpd->out, "531 Specify a user first");

	nsfile = smprint("/usr/%s/lib/namespace.ftp", ftpd->user.name);
	if(ftpd->user.isnone)
		loginuser(ftpd, arg, namespace);
	else if(access(nsfile, 0) == 0)
		loginuser(ftpd, arg, nsfile);
	else
		loginuser(ftpd, arg, "/lib/namespace");
	free(nsfile);

	return 0;
}

int 
pasvcmd(Ftpd *ftpd, char *arg)
{
	NetConnInfo *nci;
	Passive *p;

	USED(arg);

	p = &ftpd->conn.pasv;
	if(p->inuse) {
		close(p->afd);
		p->inuse = 0;
	}

	if(mountnet(ftpd) < 0)
		return 0;

	p->afd = announce("tcp!*!0", p->adir);
	if(p->afd < 0) {
		unmountnet();
		return reply(ftpd->out, "500 No free ports");
	}
	nci = getnetconninfo(p->adir, -1);
	unmountnet();

	parseip(p->ipaddr, ftpd->conn.nci->lsys);
	if(ipcmp(p->ipaddr, v4prefix) == 0 || ipcmp(p->ipaddr, IPnoaddr) == 0)
		parseip(p->ipaddr, ftpd->conn.nci->lsys);
	p->port = atoi(nci->lserv);

	freenetconninfo(nci);
	p->inuse = 1;

	dprint("dbg: pasv mode port %d", p->port);
	return reply(ftpd->out, "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)", 
		p->ipaddr[IPv4off + 0], p->ipaddr[IPv4off + 1], 
		p->ipaddr[IPv4off + 2], p->ipaddr[IPv4off + 3],
		p->port >> 8, p->port & 0xff);
}

int 
pbszcmd(Ftpd *ftpd, char *arg)
{
	USED(arg);

	/* tls is streaming and the only method we support */
	return reply(ftpd->out, "200 Ok.");
}

int 
protcmd(Ftpd *ftpd, char *arg)
{
	if(!arg)
		return reply(ftpd->out, "500 Prot command needs a level");

	switch(arg[0]) {
	case 'p':
	case 'P':
		ftpd->conn.tlsondata = 1;
		return reply(ftpd->out, "200 Protection level set");
	case 'c':
	case 'C':
		ftpd->conn.tlsondata = 0;
		return reply(ftpd->out, "200 Protection level set");
	default:
		return reply(ftpd->out, "504 Unknown protection level");
	}
}

int 
portcmd(Ftpd *ftpd, char *arg)
{
	char *field[7];
	char data[64];

	if(!arg)
		return reply(ftpd->out, "501 Port command needs arguments");
	if(getfields(arg, field, 7, 0, ", ") != 6)
		return reply(ftpd->out, "501 Incorrect port specification");
	
	snprint(data, sizeof(data), "tcp!%.3s.%.3s.%.3s.%.3s!%d", 
			field[0], field[1], field[2], field[3], 
			atoi(field[4]) * 256 + atoi(field[5]));
	strncpy(ftpd->conn.data, data, sizeof(ftpd->conn.data));

	return reply(ftpd->out, "200 Data port is %s", data);
}

int
pwdcmd(Ftpd *ftpd, char *arg)
{
	USED(arg);
	return reply(ftpd->out, "257 \"%s\" is the current directory", ftpd->user.cwd);
}

int 
quitcmd(Ftpd *ftpd, char *arg)
{
	USED(arg);

	if(ftpd->user.loggedin)
		logit("quit: %s", ftpd->user.name);

	reply(ftpd->out, "200 Goodbye.");
	return -1;
}

int 
resetcmd(Ftpd *ftpd, char *arg)
{
	if(!arg)
		return reply(ftpd->out, "501 Restart command requires offset");
	ftpd->offset = atoll(arg);
	if(ftpd->offset < 0) {
		ftpd->offset = 0;
		return reply(ftpd->out, "501 Bad offset");
	}

	return reply(ftpd->out, "350 Restarting at %lld");
}

int 
retreivecmd(Ftpd *ftpd, char *arg)
{
	Dir *d;
	Biobuf *fd, *data;
	char *line;
	char buf[4096];
	long rsz;

	d = dirstat(arg);
	if(!d)
		return reply(ftpd->out, "550 Error opening %s: %r", arg);
	if(d->mode & DMDIR)
		return reply(ftpd->out, "550 %s is a directory", arg);
	free(d);

	fd = Bopen(arg, OREAD);
	if(!fd)
		return reply(ftpd->out, "550 Error opening %s: %r", arg);

	if(ftpd->offset != 0)
		Bseek(fd, ftpd->offset, 0);

	data = dialdata(ftpd, 0);
	if(ftpd->type == Tascii)
		while(line = Brdstr(fd, '\n', 1))
			reply(data, line);
	else
		while(rsz = Bread(fd, buf, sizeof(buf)))
			if(rsz > 0)
				Bwrite(data, buf, rsz);
	closedata(ftpd, data, 0);

	logit("retreive: user %s file %s", ftpd->user.name, arg);

	return 0;
}

int
renamefromcmd(Ftpd *ftpd, char *arg)
{
	if(!arg)
		return reply(ftpd->out, "501 Rename command requires an argument");
	if(ftpd->user.isnone)
		return reply(ftpd->out, "550 Permission denied");
	
	cleanname(arg);
	ftpd->renamefrom = strdup(arg);

	return reply(ftpd->out, "350 Rename %s to...", arg);	
}

int
renametocmd(Ftpd *ftpd, char *arg)
{
	Dir *from, *to, nd;

	if(!arg)
		return reply(ftpd->out, "501 Rename command requires an argument");
	if(ftpd->user.isnone)
		return reply(ftpd->out, "550 Permission denied");
	if(!ftpd->renamefrom)
		return reply(ftpd->out, "550 Rnto must be preceded by rnfr");

	from = dirstat(ftpd->renamefrom);
	if(!from) {
		free(from);
		return reply(ftpd->out, "550 Can't stat %s", ftpd->renamefrom);
	}

	to = dirstat(arg);
	if(to) {
		free(from); free(to);
		return reply(ftpd->out, "550 Can't rename: target %s exists", arg);
	}

	nulldir(&nd);
	nd.name = arg;
	if(dirwstat(ftpd->renamefrom, &nd) < 0)
		reply(ftpd->out, "550 Can't rename %s to %s: %r", ftpd->renamefrom, arg);
	else
		reply(ftpd->out, "250 %s now %s", ftpd->renamefrom, arg);
	
	free(ftpd->renamefrom);
	ftpd->renamefrom = nil;
	free(from);

	return 0;
}

int 
systemcmd(Ftpd *ftpd, char *arg)
{
	USED(arg);
	reply(ftpd->out, "215 UNIX Type: L8 Version: Plan 9");
	return 0;
}

int
storecmd(Ftpd *ftpd, char *arg)
{
	int fd;
	Biobuf *stored, *data;
	char *line;
	char buf[4096];
	long rsz;

	if(!arg)
		return reply(ftpd->out, "501 Store command needs an argument");

	arg = cleanname(arg);
	if(ftpd->offset){
		fd = open(arg, OWRITE);
		if(fd < 0)
			return reply(ftpd->out, "550 Error opening %s: %r", arg);
		if(seek(fd, ftpd->offset, 0) < 0)
			return reply(ftpd->out, "550 Error seeking in %s to %d: %r", arg, ftpd->offset);
	} else {
		fd = create(arg, OWRITE, 0660);
		if(fd < 0)
			return reply(ftpd->out, "550 Error creating %s: %r", arg);
	}

	stored = Bfdopen(fd, OWRITE);
	data = dialdata(ftpd, 1);

	if(ftpd->type == Tascii)
		while(line = Brdstr(data, '\n', 1)) {
			if(line[Blinelen(data)] == '\r')
				line[Blinelen(data)] = '\0';
			Bprint(stored, "%s\n", line);
	} else {
		while((rsz = Bread(data, buf, sizeof(buf))) > 0)
				Bwrite(stored, buf, rsz);
	}

	Bterm(stored);
	closedata(ftpd, data, 0);

	logit("store: user %s file %s", ftpd->user.name, arg);

	return 0;
}

int
typecmd(Ftpd *ftpd, char *arg)
{
	int c;
	char *x;

	if(!arg)
		return reply(ftpd->out, "501 Type command needs an argument");

	x = arg;
	while(c = *x++) {
		switch(tolower(c)) {
		case 'a':
			ftpd->type = Tascii;
			break;
		case 'i':
		case 'l':
			ftpd->type = Timage;
			break;
		case '8':
		case ' ':
		case 'n':
		case 't':
		case 'c':
			break;
		default:
			return reply(ftpd->out, "501 Unimplemented type %s", arg);
		}
	}

	return reply(ftpd->out, "200 Type %s", (ftpd->type == Tascii ? "Ascii" : "Image"));
}

int
usercmd(Ftpd *ftpd, char *arg)
{
	if(ftpd->user.loggedin)
		return reply(ftpd->out, "530 Already logged in as %s", ftpd->user.name);

	if(arg == nil)
		return reply(ftpd->out, "530 User command needs username");

	if(anonall)
		ftpd->user.isnone = 1;

	if(strcmp(arg, "anonymous") == 0 || strcmp(arg, "ftp") == 0 || strcmp(arg, "none") == 0) {
		if(!anonok && !anononly)
			return reply(ftpd->out, "530 Not logged in: anonymous access disabled");

		ftpd->user.isnone = 1;
		strncpy(ftpd->user.name, "none", Maxpath);
		return loginuser(ftpd, nil, namespace);
	} else if(anononly) {
		return reply(ftpd->out, "530 Not logged in: anonymous access only");
	}

	strncpy(ftpd->user.name, arg, Maxpath);
	return reply(ftpd->out, "331 Need password");
}

Cmd cmdtab[] = {
	/* cmd, fn, needlogin, needtls, asproc*/
	{"abor",	abortcmd,		0,	0,	0},
	{"allo",	noopcmd,		0,	0,	0},
	{"auth",	authcmd,		0,	0,	0},
	{"cwd",		cwdcmd,			1,	0,	0},
	{"dele",	deletecmd,		1,	0,	0},
	{"feat",	featcmd,		0,	0,	0},
	{"list",	listcmd,		1,	0,	1},
	{"nlst",	nlistcmd,		1,	0,	1},
	{"noop",	noopcmd,		0,	0,	0},
	{"mkd",		mkdircmd,		1,	0,	0},
	{"mlsd",	mlsdcmd,		1,	0,	0},
	{"mlst",	mlstcmd,		1,	0,	1},
	{"opts",	optscmd,		0,	0,	0},
	{"pass",	passcmd,		0,	1,	0},
	{"pasv",	pasvcmd,		0,	0,	0},
	{"pbsz",	pbszcmd,		0,	1,	0},
	{"prot",	protcmd,		0,	1,	0},
	{"port",	portcmd,		0,	0,	0},
	{"pwd",		pwdcmd,			0,	0,	0},
	{"quit",	quitcmd,		0,	0,	0},
	{"rest",	resetcmd,		0,	0,	0},
	{"retr",	retreivecmd,	1,	0,	1},
	{"rmd",		deletecmd,		1,	0,	0},
	{"rnfr",	renamefromcmd,	1,	0,	0},
	{"rnto",	renametocmd,	1,	0,	0},
	{"syst",	systemcmd,		0,	0,	0},
	{"stor",	storecmd,		1,	0,	1},
	{"type",	typecmd,		0,	0,	0},
	{"user",	usercmd,		0,	0,	0},
	{nil,		nil,			0,	0,	0},
};

void 
usage(void)
{
	fprint(2, "usage: %s [-aAdei] [-c cert-path] [-n namespace-file]\n", argv0);
	exits("usage");
}

void 
main(int argc, char **argv)
{
	Ftpd ftpd;
	char *cmd, *arg;
	Cmd *t;

	ARGBEGIN {
	case 'a':
		anonok = 1;
		break;
	case 'A':
		anononly = 1;
		break;
	case 'c':
		certpath = EARGF(usage());
		break;
	case 'd':
		debug = 1;
		break;
	case 'e':
		anonall = 1;
		break;
	case 'i':
		implicittls = 1;
		break;
	case 'n':
		namespace = EARGF(usage());
		break;
	default:
		usage();
	} ARGEND

	tmfmtinstall();

	if(argc < 1)
		ftpd.conn.nci = getnetconninfo(nil, 0);
	else
		ftpd.conn.nci = getnetconninfo(argv[argc - 1], 0);
	if(!ftpd.conn.nci)
		sysfatal("ftpd needs a network address");

	ftpd.in = mallocz(sizeof(Biobuf), 1);
	ftpd.out = mallocz(sizeof(Biobuf), 1);
	Binit(ftpd.in, 0, OREAD);
	Binit(ftpd.out, 1, OWRITE);

	/* open logfile */
	syslog(0, "ftp", nil);

	if(certpath) {
		ftpd.conn.cert = readcert(certpath, &ftpd.conn.certlen);
		ftpd.conn.tls = mallocz(sizeof(TLSconn), 1);

		/* we need a copy in case of namespace changes 
		 * NOTE: the default namespace needs to leave access to the tls device
		 * or anonymous logins with tls will be broken. */
		ftpd.conn.tls->cert = malloc(ftpd.conn.certlen);
		memcpy(ftpd.conn.tls->cert, ftpd.conn.cert, ftpd.conn.certlen);
		ftpd.conn.tls->certlen = ftpd.conn.certlen;

		if(implicittls) {
			dprint("dbg: implicit tls mode");
			starttls(&ftpd);
		}
	}

	reply(ftpd.out, "220 Plan 9 FTP server ready.");
	alarm(Maxwait);
	while(cmd = Brdstr(ftpd.in, '\n', 1)) {
		alarm(0);

		/* strip cr */
		char *p = strrchr(cmd, '\r');
		if(p)
		       	*p = '\0';

		/* strip telnet control sequences */
		while(*cmd && (uchar)*cmd == 255) {
			cmd++;
			if(*cmd)
				cmd++;
		}

		/* get the arguments */
		arg = strchr(cmd, ' ');
		if(arg) {
			*arg++ = '\0';
			while(*arg == ' ')
				arg++;
			/* some clients always send a space */
			if(*arg == '\0')
				arg = nil;
		}

		/* find the cmd and execute it */
		if(*cmd == '\0')
			continue;

		for(t = cmdtab; t->name; t++)
			if(cistrcmp(cmd, t->name) == 0) {
				if(t->needlogin && !ftpd.user.loggedin) {
					reply(ftpd.out, "530 Command requires login");
				} else if(t->needtls && !ftpd.conn.tlson) {
					reply(ftpd.out, "534 Command requires tls");
				} else {
					if(t->fn != passcmd)
						dprint("cmd: %s %s", cmd, arg);
					if(t->asproc) {
						dprint("cmd %s spawned as proc");
						asproc(&ftpd, *t->fn, arg);
					} else if((*t->fn)(&ftpd, arg) < 0)
						goto exit;
				}
				break;
			}

		/* reset the offset unless we just set it */
		if(t->fn != resetcmd)
			ftpd.offset = 0;
		if(!t->name)
			reply(ftpd.out, "502 %s command not implemented", cmd);

		free(cmd);
		alarm(Maxwait);
	}

exit:
	free(ftpd.conn.tls);
	freenetconninfo(ftpd.conn.nci);
	Bterm(ftpd.in);
	Bterm(ftpd.out);
	free(ftpd.in);
	free(ftpd.out);
	exits(nil);
}
