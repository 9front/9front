#include "imap4d.h"
#include <libsec.h>

static char Ebadch[]	= "can't get challenge";
static char Ecantstart[]	= "can't initialize mail system: %r";
static char Ecancel[]	= "client cancelled authentication";
static char Ebadau[]	= "login failed";

/*
 * hack to allow smtp forwarding.
 * hide the peer IP address under a rock in the ratifier FS.
 */
void
enableforwarding(void)
{
	char buf[64], peer[64], *p;
	int fd;
	ulong now;
	static ulong last;

	if(remote == nil)
		return;

	now = time(0);
	if(now < last + 5*60)
		return;
	last = now;

	fd = open("/srv/ratify", ORDWR);
	if(fd < 0)
		return;
	if(mount(fd, -1, "/mail/ratify", MBEFORE, "") == -1){
		close(fd);
		return;
	}

	strncpy(peer, remote, sizeof peer);
	peer[sizeof peer - 1] = 0;
	p = strchr(peer, '!');
	if(p != nil)
		*p = 0;

	snprint(buf, sizeof buf, "/mail/ratify/trusted/%s#32", peer);

	/*
	 * if the address is already there and the user owns it,
	 * remove it and recreate it to give him a new time quanta.
	 */
	if(access(buf, 0) >= 0 && remove(buf) < 0)
		return;

	fd = create(buf, OREAD, 0666);
	if(fd >= 0)
		close(fd);
}

void
setupuser(AuthInfo *ai)
{
	int pid;
	Waitmsg *w;

	if(ai){
		strecpy(username, username + sizeof username, ai->cuid);
		if(auth_chuid(ai, nil) < 0)
			bye("user auth failed: %r");
		else {	/* chown network connection */
			Dir nd;
			nulldir(&nd);
			nd.mode = 0660;
			nd.uid = ai->cuid;
			dirfwstat(Bfildes(&bin), &nd);
		}
		auth_freeAI(ai);
	}else
		strecpy(username, username + sizeof username, getuser());

	if(strcmp(username, "none") == 0 || newns(username, 0) == -1)
		bye("user login failed: %r");
	if(binupas){
		if(bind(binupas, "/bin/upas", MREPL) > 0)
			ilog("bound %s on /bin/upas", binupas);
		else
			bye("bind %s failed: %r", binupas);
	}

	/*
	 * hack to allow access to outgoing smtp forwarding
	 */
	enableforwarding();

	snprint(mboxdir, Pathlen, "/mail/box/%s", username);
	if(mychdir(mboxdir) < 0)
		bye("can't open user's mailbox");

	switch(pid = fork()){
	case -1:
		bye(Ecantstart);
		break;
	case 0:
if(!strstr(argv0, "8.out"))
		execl("/bin/upas/fs", "upas/fs", "-np", nil);
else{
ilog("using /sys/src/cmd/upas/fs/8.out");
execl("/sys/src/cmd/upas/fs/8.out", "upas/fs", "-np", nil);
}
		_exits(0);
		break;
	default:
		break;
	}
	if((w = wait()) == nil || w->pid != pid || w->msg[0] != 0)
		bye(Ecantstart);
	free(w);
}

static char*
authread(int *len)
{
	char *t;
	int n;

	t = Brdline(&bin, '\n');
	n = Blinelen(&bin);
	if(n < 2)
		return nil;
	n--;
	if(t[n-1] == '\r')
		n--;
	t[n] = 0;
	if(n == 0 || strcmp(t, "*") == 0)
		return nil;
	*len = n;
	return t;
}

static char*
authresp(void)
{
	char *s, *t;
	int n;

	t = authread(&n);
	if(t == nil)
		return nil;
	s = binalloc(&parsebin, n + 1, 1);
	n = dec64((uchar*)s, n, t, n);
	s[n] = 0;
	return s;
}

/*
 * rfc 2195 cram-md5 authentication
 */
char*
cramauth(void)
{
	char *s, *t;
	AuthInfo *ai;
	Chalstate *cs;

	if((cs = auth_challenge("proto=cram role=server")) == nil)
		return Ebadch;

	Bprint(&bout, "+ %.*[\r\n", cs->nchal, cs->chal);
	if(Bflush(&bout) < 0)
		writeerr();

	s = authresp();
	if(s == nil)
		return Ecancel;

	t = strchr(s, ' ');
	if(t == nil)
		return Ebadch;
	*t++ = 0;
	strncpy(username, s, Userlen);
	username[Userlen - 1] = 0;

	cs->user = username;
	cs->resp = t;
	cs->nresp = strlen(t);
	if((ai = auth_response(cs)) == nil)
		return Ebadau;
	auth_freechal(cs);
	setupuser(ai);
	return nil;
}

char*
crauth(char *u, char *p)
{
	char response[64];
	AuthInfo *ai;
	static char nchall[64];
	static Chalstate *ch;

again:
	if(ch == nil){
		if(!(ch = auth_challenge("proto=p9cr role=server user=%q", u)))
			return Ebadch;
		snprint(nchall, 64, " encrypt challenge: %s", ch->chal);
		return nchall;
	} else {
		strncpy(response, p, 64);
		ch->resp = response;
		ch->nresp = strlen(response);
		ai = auth_response(ch);
		auth_freechal(ch);
		ch = nil;
		if(ai == nil)
			goto again;
		setupuser(ai);
		return nil;
	}
}

char*
passauth(char *u, char *secret)
{
	char response[2*MD5dlen + 1];
	uchar digest[MD5dlen];
	AuthInfo *ai;
	Chalstate *cs;

	if((cs = auth_challenge("proto=cram role=server")) == nil)
		return Ebadch;
	hmac_md5((uchar*)cs->chal, strlen(cs->chal),
		(uchar*)secret, strlen(secret), digest, nil);
	snprint(response, sizeof(response), "%.*H", MD5dlen, digest);
	cs->user = u;
	cs->resp = response;
	cs->nresp = strlen(response);
	ai = auth_response(cs);
	if(ai == nil)
		return Ebadau;
	auth_freechal(cs);
	setupuser(ai);
	return nil;
}

static int
niltokenize(char *buf, int n, char **f, int nelemf)
{
	int i, nf;

	f[0] = buf;
	nf = 1;
	for(i = 0; i < n - 1; i++)
		if(buf[i] == 0){
			f[nf++] = buf + i + 1;
			if(nf == nelemf)
				break;
		}
	return nf;
}

char*
plainauth(char *ch)
{
	char buf[256*3 + 2], *f[4];
	int n, nf;

	if(ch == nil){
		Bprint(&bout, "+ \r\n");
		if(Bflush(&bout) < 0)
			writeerr();
		ch = authread(&n);
	}
	if(ch == nil || strlen(ch) == 0)
		return Ecancel;
	n  = dec64((uchar*)buf, sizeof buf, ch, strlen(ch));
	nf = niltokenize(buf, n, f, nelem(f));
	if(nf != 3)
		return Ebadau;
	return passauth(f[1], f[2]);
}
