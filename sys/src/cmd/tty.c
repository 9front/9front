#include <u.h>
#include <libc.h>
#include <auth.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>

#define MIN(a,b) ((a) < (b) ? (a) : (b))

enum {
	Stacksz = 8192,
};

typedef struct Prog {
	Channel *pidc;
	char **argv;
} Prog;

typedef struct Line {
	uchar *buf;
	int x, len;
} Line;

int cons, consctl;
Channel *rchan, *wchan, *ichan;
File *devcons;

static void
killer(void *arg)
{
	int *pid = arg;
	ulong yes;
	for(;;){
		yes = recvul(ichan);
		if(yes)
			postnote(PNGROUP, *pid, "interrupt");
	}
}

static void
sendline(Channel *c, Line *L)
{
	int n, k;
	Req *r;
	uchar *s;
	
	s = L->buf;
	n = L->x;
	do{
		while(!(r = recvp(c)));
		k = MIN(n, r->ifcall.count);
		memcpy(r->ofcall.data, s, k);
		r->ofcall.count = k;
		respond(r, nil);
		s += k;
		n -= k;
	}while(n > 0);
	L->x = 0;
}

static void
senderr(Channel *c, char *err)
{
	Req *r;
	while(!(r = recvp(c)));
	respond(r, err);
}

static void
echo(uchar c)
{
	write(cons, &c, 1);
}

static int
addchar(Line *L, uchar c)
{
	int send;
	
	send = 0;
	switch(c){
	case '\n':
		send = 1;
		break;
	case 4:
		return 1;
	case 8:
		if(L->x > 0){
			echo(8);
			L->x--;
		}
		return 0;
	case 21:
		while(L->x > 0){
			echo(8);
			L->x--;
		}
		return 0;
	case 23:
		while(L->x > 0){
			c = L->buf[--L->x];
			echo(8);
			if(c == ' ' || c == '\t')
				break;
		}
		return 0;
	case 127:
		L->x = 0;
		sendul(ichan, 1);
		return 1;
	}
	echo(c);
	L->buf[L->x++] = c;
	if(L->x == L->len)
		send = 1;
	return send;
}

static Line *
makeline(int len)
{
	Line *L;
	L = malloc(sizeof(*L));
	L->buf = malloc(len);
	L->x = 0;
	L->len = len;
	return L;
}

static void
reader(void *)
{
	int n;
	uchar c;
	Line *L;
	char err[ERRMAX];
	
	L = makeline(128);
	for(;;){
		n = read(cons, &c, 1);
		if(n < 0){
			rerrstr(err, sizeof(err));
			if(L->x > 0)
				sendline(rchan, L);
			senderr(rchan, err);
			continue;
		}
		if(addchar(L, c))
			sendline(rchan, L);
	}
}

static void
writer(void *)
{
	Req *r;
	int n;
	char err[ERRMAX];
	
	for(;;){
		do
			r = recvp(wchan);
		while(!r);
		
		n = write(cons, r->ifcall.data, r->ifcall.count);
		if(n < 0){
			rerrstr(err, sizeof(err));
			respond(r, err);
		}else{
			r->ofcall.count = n;
			respond(r, nil);
		}
	}
}

static void
fsread(Req *r)
{
	sendp(rchan, r);
}

static void
fswrite(Req *r)
{
	sendp(wchan, r);
}

/* adapted from acme/win */
int
lookinbin(char *s)
{
	if(s[0] == '/')
		return 0;
	if(s[0]=='.' && s[1]=='/')
		return 0;
	if(s[0]=='.' && s[1]=='.' && s[2]=='/')
		return 0;
	return 1;
}

char*
estrstrdup(char *s, char *t)
{
	char *u;

	u = malloc(strlen(s)+strlen(t)+1);
	sprint(u, "%s%s", s, t);
	return u;
}

void
cmdproc(void *arg)
{
	Prog *p = arg;
	char **av = p->argv;
	char *cmd;
	
	rfork(RFCFDG | RFNOTEG);
	open("/dev/cons", OREAD);
	open("/dev/cons", OWRITE);
	dup(1, 2);
	procexec(p->pidc, av[0], av);
	if(lookinbin(av[0])){
		cmd = estrstrdup("/bin/", av[0]);
		procexec(p->pidc, cmd, av);
	}
	threadexitsall("exec");
}

void
runcmd(char **argv)
{
	Channel *waitc;
	Channel *pidc;
	Waitmsg *w;
	Prog prog;
	int pid;

	waitc = threadwaitchan();
	pidc = chancreate(sizeof(int), 0);
	prog.argv = argv;
	prog.pidc = pidc;
	proccreate(cmdproc, &prog, Stacksz);
	while(recv(pidc, &pid) == -1 || pid == -1);
	threadcreate(killer, &pid, Stacksz);
	while(recv(waitc, &w) == -1);
	free(w);
}

void
usage(void)
{
	print("usage: tty [-D] cmd arg1 arg2 ...\n");
	exits("usage");
}

Srv fs = {
	.read = fsread,
	.write = fswrite,
};

void
threadmain(int argc, char *argv[])
{
	char *user;
	
	ARGBEGIN{
		case 'D':
			chatty9p++;
			break;
		default:
			usage();
			break;
	}ARGEND;
	
	if(argc == 0)
		usage();

	rfork(RFNAMEG);
	
	cons = open("/dev/cons", ORDWR);
	if(cons < 0)
		sysfatal("cons: %r");
	consctl = open("/dev/consctl", OWRITE);
	if(consctl < 0)
		sysfatal("ctl: %r");
	fprint(consctl, "rawon\n");
	
	rchan = chancreate(sizeof(void *), 8);
	wchan = chancreate(sizeof(void *), 8);
	ichan = chancreate(sizeof(ulong), 8);

	proccreate(reader, nil, Stacksz);
	proccreate(writer, nil, Stacksz);
	
	user = getuser();
	fs.tree = alloctree(user, getuser(), DMDIR|0555, nil);
	devcons = createfile(fs.tree->root, "cons", user, 0666, nil);
	threadpostmountsrv(&fs, nil, "/dev", MBEFORE);

	runcmd(argv);
	
	close(consctl);
	close(cons);
	threadexitsall(nil);
}
