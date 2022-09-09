#include <u.h>
#include <libc.h>
#include <auth.h>
#include <fcall.h>
#include <bio.h>

uint messagesize = 65536;	/* just a buffer size */
int aflag;
int srvfd;

void
usage(void)
{
	fprint(2, "usage: aux/9pcon [-m messagesize] /srv/service | -c command | -n networkaddress\n");
	exits("usage");
}

int
connectcmd(char *cmd)
{
	int p[2];

	if(pipe(p) < 0)
		return -1;
	switch(fork()){
	case -1:
		fprint(2, "fork failed: %r\n");
		_exits("exec");
	case 0:
		dup(p[0], 0);
		dup(p[0], 1);
		close(p[1]);
		execl("/bin/rc", "rc", "-c", cmd, nil);
		fprint(2, "exec failed: %r\n");
		_exits("exec");
	default:
		close(p[0]);
		return p[1];
	}
}

static int rendez;

void
watch(int fd)
{
	int n;
	uchar *buf;
	Fcall f, *p;
	
	buf = malloc(messagesize);
	if(buf == nil)
		sysfatal("out of memory");

	while((n = read9pmsg(fd, buf, messagesize)) > 0){
		memset(&f, 0, sizeof f);
		if(convM2S(buf, n, &f) != n){
			print("convM2S: %r\n");
			continue;
		}
		if(aflag){
			p = malloc(sizeof *p);
			if(p == nil)
				sysfatal("out of memory");
			memmove(p, &f, sizeof f);
			rendezvous(&rendez, p);
		}
		print("\t<- %F\n", &f);
	}
	if(n == 0)
		print("server eof\n");
	else if(n == -1)
		print("read9pmsg from server: %r\n");
}

char*
version(Fcall *f, int, char **argv)
{
	f->msize = strtol(argv[0], 0, 0);
	if(f->msize > messagesize)
		return "message size too big; use -m option on command line";
	f->version = argv[1];
	return nil;
}

char*
tauth(Fcall *f, int, char **argv)
{
	f->afid = strtol(argv[0], 0, 0);
	f->uname = argv[1];
	f->aname = argv[2];
	return nil;
}

char*
strtoqid(char *s, Qid *q)
{
	char *dot;
	int state;
	char buf[1024];
	char *p;

	state = 0;
	p = buf;
	for(dot = s; *dot; dot++){
		assert(p - buf < sizeof buf);
		switch(*dot){
		case '{':
			continue;
		default:
			*p++ = *dot;
			break;
		case '}':
		case ',':
			*p = '\0';
			switch(state){
			case 0:
				q->path = strtoull(buf, 0, 0);
				break;
			case 1:
				q->vers = strtoul(buf, 0, 0);
				break;
			case 2:
				if(buf[0] == 'f' || strcmp("QTFILE", buf) == 0)
					q->type = QTFILE;
				else if(buf[0] == 'd' || strcmp("QTDIR", buf) == 0)
					q->type = QTDIR;
				else
					q->type = (uchar)strtol(buf, 0, 0);
				break;
			}
			p = buf;
			state++;
		}
	}
	if(state != 3)
		return "malformed qid";
	return nil;
}

char*
rauth(Fcall *f, int, char **argv)
{
	return strtoqid(argv[0], &f->aqid);
}

char*
rerror(Fcall *f, int, char **argv)
{
	f->ename = argv[0];
	return nil;
}

char*
tflush(Fcall *f, int, char **argv)
{
	f->oldtag = strtol(argv[0], 0, 0);
	return nil;
}

char*
tattach(Fcall *f, int, char **argv)
{
	f->fid = strtol(argv[0], 0, 0);
	f->afid = strtol(argv[1], 0, 0);
	f->uname = argv[2];
	f->aname = argv[3];
	return nil;
}

char*
rattach(Fcall *f, int, char **argv)
{
	return strtoqid(argv[0], &f->qid);
}

char*
twalk(Fcall *f, int argc, char **argv)
{
	int i;

	if(argc < 2)
		return "usage: Twalk tag fid newfid [name...]";
	f->fid = strtol(argv[0], 0, 0);
	f->newfid = strtol(argv[1], 0, 0);
	f->nwname = argc-2;
	if(f->nwname > MAXWELEM)
		return "too many names";
	for(i=0; i<argc-2; i++)
		f->wname[i] = argv[2+i];
	return nil;
}

char*
rwalk(Fcall *f, int argc, char **argv)
{
	int i;
	char *e;

	if(argc >= MAXWELEM)
		return "too many names";

	f->nwqid = argc;
	for(i = 0; i < argc; i++){
		e = strtoqid(argv[i], &f->wqid[i]);
		if(e != nil)
			return e;
	}
	return nil;
}

char*
topen(Fcall *f, int, char **argv)
{
	f->fid = strtol(argv[0], 0, 0);
	f->mode = strtol(argv[1], 0, 0);
	return nil;
}

char*
ropen(Fcall *f, int, char **argv)
{
	f->iounit = strtol(argv[1], 0, 0);
	return strtoqid(argv[0], &f->qid);
}

char*
tcreate(Fcall *f, int, char **argv)
{
	f->fid = strtol(argv[0], 0, 0);
	f->name = argv[1];
	f->perm = strtoul(argv[2], 0, 8);
	f->mode = strtol(argv[3], 0, 0);
	return nil;
}

char*
tread(Fcall *f, int, char **argv)
{
	f->fid = strtol(argv[0], 0, 0);
	f->offset = strtoll(argv[1], 0, 0);
	f->count = strtol(argv[2], 0, 0);
	return nil;
}

char*
rread(Fcall *f, int, char **argv)
{
	f->data = argv[0];
	f->count = strlen(argv[0]);
	return nil;
}

char*
twrite(Fcall *f, int, char **argv)
{
	f->fid = strtol(argv[0], 0, 0);
	f->offset = strtoll(argv[1], 0, 0);
	f->data = argv[2];
	f->count = strlen(argv[2]);
	return nil;
}

char*
rwrite(Fcall *f, int, char **argv)
{
	f->count = strtol(argv[0], 0, 0);
	return nil;
}

char*
tclunk(Fcall *f, int, char **argv)
{
	f->fid = strtol(argv[0], 0, 0);
	return nil;
}

char*
tremove(Fcall *f, int, char **argv)
{
	f->fid = strtol(argv[0], 0, 0);
	return nil;
}

char*
tstat(Fcall *f, int, char **argv)
{
	f->fid = strtol(argv[0], 0, 0);
	return nil;
}

ulong
xstrtoul(char *s)
{
	if(strcmp(s, "~0") == 0)
		return ~0UL;
	return strtoul(s, 0, 0);
}

uvlong
xstrtoull(char *s)
{
	if(strcmp(s, "~0") == 0)
		return ~0ULL;
	return strtoull(s, 0, 0);
}

char*
twstat(Fcall *f, int, char **argv)
{
	static uchar buf[DIRMAX];
	Dir d;

	//We function as Rstat as well
	if(f->type == Twstat){
		f->fid = strtol(argv[0], 0, 0);
		argv++;
	}

	memset(&d, 0, sizeof d);
	nulldir(&d);
	d.name = argv[0];
	d.uid = argv[1];
	d.gid = argv[2];
	d.mode = xstrtoul(argv[3]);
	d.mtime = xstrtoul(argv[4]);
	d.length = xstrtoull(argv[5]);

	f->stat = buf;
	f->nstat = convD2M(&d, buf, sizeof buf);
	if(f->nstat < BIT16SZ)
		return "convD2M failed (internal error)";

	return nil;
}

char*
nop(Fcall*, int, char**)
{
	/* Rwstat,Rremove,Rclunk,Rflush */
	return nil;
}

enum{
	Xsource = Tmax+1,
	Xdef,
	Xend,

	Xnexttag,
};

int taggen;

char*
settag(Fcall*, int, char **argv)
{
	static char buf[120];

	taggen = strtol(argv[0], 0, 0)-1;
	snprint(buf, sizeof buf, "next tag is %d", taggen+1);
	return buf;
}

char* shell9p(int);

char*
source(Fcall*, int, char **argv)
{
	int fd;
	char *e;

	fd = open(argv[0], OREAD);
	if(fd < 0)
		return smprint("^could not open %s: %r", argv[0]);
	e = shell9p(fd);
	close(fd);
	return e;
}

typedef struct Func Func;
struct Func {
	Func *link;
	char *name;
	char *lines[128];
	int n;
};

Func *globals;
Func *local;

char*
funcdef(Fcall*, int, char **argv)
{
	if(local != nil)
		return smprint("^can not define func %s; %s not terminated", argv[0], local->name);
	local = mallocz(sizeof *local, 1);
	if(local == nil)
		return "!out of memory";
	local->name = strdup(argv[0]);
	return nil;
}

char*
funcend(Fcall*, int, char**)
{
	Func **l;
	Func *p;
	int i;

	if(local == nil)
		return "?no function defined";
	l = &globals;
	for(p = globals; p != nil; p = p->link){
		if(strcmp(local->name, p->name) == 0)
			break;
		l = &p->link;
	}
	if(p != nil){
		for(i=0; i<p->n; i++)
			free(p->lines[i]);
		free(p->name);
		free(p);
	}
	*l = local;
	local = nil;

	return nil;
}

typedef struct Cmd Cmd;
struct Cmd {
	char *name;
	int type;
	int argc;
	char *usage;
	char *(*fn)(Fcall *f, int, char**);
};

Cmd msg9p[] = {
	"Tversion", Tversion, 2, "messagesize version", version,
	"Rversion", Rversion, 2, "messagesize version", version,

	"Tauth", Tauth, 3, "afid uname aname", tauth,
	"Rauth", Rauth, 1, "aqid", rauth,

	"Rerror", Rerror, 1, "ename", rerror,

	"Tflush", Tflush, 1, "oldtag", tflush,
	"Rflush", Rflush, 0, "", nop,

	"Tattach", Tattach, 4, "fid afid uname aname", tattach,
	"Rattach", Rattach, 1, "qid", rattach,

	"Twalk", Twalk, 0, "fid newfid [name...]", twalk,
	"Rwalk", Rwalk, 0, "name...", rwalk,

	"Topen", Topen, 2, "fid mode", topen,
	"Ropen", Ropen, 2, "qid iounit", ropen,

	"Tcreate", Tcreate, 4, "fid name perm mode", tcreate,
	"Rcreate", Rcreate, 2, "qid iounit", ropen,

	"Tread", Tread, 3, "fid offset count", tread,
	"Rread", Rread, 1, "data", rread,

	"Twrite", Twrite, 3, "fid offset data", twrite,
	"Rwrite", Rwrite, 1, "count", rwrite,

	"Tclunk", Tclunk, 1, "fid", tclunk,
	"Rclunk", Rclunk, 0, "", nop,

	"Tremove", Tremove, 1, "fid", tremove,
	"Rremove", Rremove, 0, "", nop,

	"Tstat", Tstat, 1, "fid", tstat,
	"Rstat", Rstat, 6, "name uid gid mode mtime length", twstat,

	"Twstat", Twstat, 7, "fid name uid gid mode mtime length", twstat,
	"Rwstat", Rwstat, 0, "", nop,

	".",	Xsource, 1, "file", source,
	"def",	Xdef, 1, "name", funcdef,
	"end",	Xend, 0, "", funcend,

	"nexttag", Xnexttag, 0, "", settag,
};

char*
run(char *p)
{
	char *e, *f[10];
	int i, n, nf, n2;
	Fcall t, *r;
	Func *func;
	char *cp;
	static uchar *buf = nil;
	static uchar *buf2 = nil;

	if(buf == nil)
		buf = malloc(messagesize);
	if(buf2 == nil)
		buf2 = malloc(messagesize);
	if(buf == nil || buf2 == nil)
		return "!out of memory";

	if(p[0] == '#')
		return nil;
	if(local != nil && strstr(p, "end") == nil){
		local->lines[local->n++] = strdup(p);
		return nil;
	}
	if((nf = tokenize(p, f, nelem(f))) == 0)
		return nil;
	for(i=0; i<nelem(msg9p); i++)
		if(strcmp(f[0], msg9p[i].name) == 0)
			break;
	if(i == nelem(msg9p)){
		if(local != nil)
			return "?unknown message";
		for(func = globals; func != nil; func = func->link){
			if(strcmp(func->name, f[0]) != 0)
				continue;
			for(i = 0; i < func->n; i++){
				cp = strdup(func->lines[i]);
				if(e = run(cp)){
					free(cp);
					return e;
				}
				free(cp);
			}
			return nil;
		}
		return "?unknown message";
	}

	memset(&t, 0, sizeof t);
	t.type = msg9p[i].type;
	if(t.type == Tversion)
		t.tag = NOTAG;
	else
		t.tag = ++taggen;
	if(nf < 1 || (msg9p[i].argc && nf != 1+msg9p[i].argc))
		return smprint("^usage: %s %s", msg9p[i].name, msg9p[i].usage);

	if((e = msg9p[i].fn(&t, nf-1, f+1)) || t.type > Tmax)
		return e;

	n = convS2M(&t, buf, messagesize);
	if(n <= BIT16SZ)
		return "?message too large for buffer";

	switch(msg9p[i].name[0]){
	case 'R':
		if(!aflag)
			break;
		r = rendezvous(&rendez, nil);
		r->tag = t.tag;
		n2 = convS2M(r, buf2, messagesize);
		if(n != n2 || memcmp(buf, buf2, n) != 0){
			fprint(2, "?mismatch %F != %F\n", r, &t);
			return "!assert fail";
		}
		free(r);
		break;
	case 'T':
		if(write(srvfd, buf, n) != n)
			return "!write fails";
		print("\t-> %F\n", &t);
	}	
	return nil;
}

char*
shell9p(int fd)
{
	char *e, *p;
	Biobuf b;

	Binit(&b, fd, OREAD);
	while(p = Brdline(&b, '\n')){
		p[Blinelen(&b)-1] = '\0';
		e = run(p);
		if(e == nil)
			continue;
		switch(*e){
		case 0:
			break;
		case '?':
		default:
			fprint(2, "%s\n", e);
			break;
		case '^':
			e[0] = '?';
			fprint(2, "%s\n", e);
			free(e);
			break;
		case '!':
			Bterm(&b);
			return e;
		}
	}
	Bterm(&b);
	return nil;
}
		
void
main(int argc, char **argv)
{
	int pid, cmd, net;
	char *status;

	cmd = 0;
	net = 0;
	aflag = 0;
	taggen = 0;
	ARGBEGIN{
	case 'a':
		aflag = 1;
		break;
	case 'c':
		cmd = 1;
		break;
	case 'm':
		messagesize = strtol(EARGF(usage()), 0, 0);
		break;
	case 'n':
		net = 1;
		break;
	default:
		usage();
	}ARGEND

	fmtinstall('F', fcallfmt);
	fmtinstall('D', dirfmt);
	fmtinstall('M', dirmodefmt);

	if(argc != 1)
		usage();

	if(cmd && net)
		usage();

	if(cmd)
		srvfd = connectcmd(argv[0]);
	else if(net){
		srvfd = dial(netmkaddr(argv[0], "net", "9fs"), 0, 0, 0);
		if(srvfd < 0)
			sysfatal("dial: %r");
	}else{
		srvfd = open(argv[0], ORDWR);
		if(srvfd < 0)
			sysfatal("open: %r");
	}

	status = nil;
	switch(pid = rfork(RFPROC|RFMEM)){
	case -1:
		sysfatal("rfork: %r");
		break;
	case 0:
		watch(srvfd);
		postnote(PNPROC, getppid(), "kill");
		break;
	default:
		status = shell9p(0);
		postnote(PNPROC, pid, "kill");
		break;
	}
	exits(status);
}
