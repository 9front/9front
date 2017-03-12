#include "imap4d.h"

/*
 * these should be in libraries
 */
char	*csquery(char *attr, char *val, char *rattr);

/*
 * implemented:
 * /lib/rfc/rfc3501	imap4rev1
 * /lib/rfc/rfc2683	implementation advice
 * /lib/rfc/rfc2342	namespace capability
 * /lib/rfc/rfc2222	security protocols
 * /lib/rfc/rfc1731	security protocols
 * /lib/rfc/rfc2177	idle capability
 * /lib/rfc/rfc2195	cram-md5 authentication
 * /lib/rfc/rfc4315	uidplus capability
 *
 * not implemented, priority:
 * /lib/rfc/rfc5256	sort and thread
 *	requires missing support from upas/fs.
 *
 * not implemented, low priority:
 * /lib/rfc/rfc2088	literal+ capability
 * /lib/rfc/rfc2221	login-referrals
 * /lib/rfc/rfc2193	mailbox-referrals
 * /lib/rfc/rfc1760	s/key authentication
 *
 */

typedef struct	Parsecmd	Parsecmd;
struct Parsecmd
{
	char	*name;
	void	(*f)(char*, char*);
};

static	void	appendcmd(char*, char*);
static	void	authenticatecmd(char*, char*);
static	void	capabilitycmd(char*, char*);
static	void	closecmd(char*, char*);
static	void	copycmd(char*, char*);
static	void	createcmd(char*, char*);
static	void	deletecmd(char*, char*);
static	void	expungecmd(char*, char*);
static	void	fetchcmd(char*, char*);
static	void	getquotacmd(char*, char*);
static	void	getquotarootcmd(char*, char*);
static	void	idlecmd(char*, char*);
static	void	listcmd(char*, char*);
static	void	logincmd(char*, char*);
static	void	logoutcmd(char*, char*);
static	void	namespacecmd(char*, char*);
static	void	noopcmd(char*, char*);
static	void	renamecmd(char*, char*);
static	void	searchcmd(char*, char*);
static	void	selectcmd(char*, char*);
static	void	setquotacmd(char*, char*);
static	void	statuscmd(char*, char*);
static	void	storecmd(char*, char*);
static	void	subscribecmd(char*, char*);
static	void	uidcmd(char*, char*);
static	void	unsubscribecmd(char*, char*);
static	void	xdebugcmd(char*, char*);
static	void	copyucmd(char*, char*, int);
static	void	fetchucmd(char*, char*, int);
static	void	searchucmd(char*, char*, int);
static	void	storeucmd(char*, char*, int);

static	void	imap4(int);
static	void	status(int expungeable, int uids);
static	void	cleaner(void);
static	void	check(void);
static	int	catcher(void*, char*);

static	Search	*searchkey(int first);
static	Search	*searchkeys(int first, Search *tail);
static	char	*astring(void);
static	char	*atomstring(char *disallowed, char *initial);
static	char	*atom(void);
static	void	clearcmd(void);
static	char	*command(void);
static	void	crnl(void);
static	Fetch	*fetchatt(char *s, Fetch *f);
static	Fetch	*fetchwhat(void);
static	int	flaglist(void);
static	int	flags(void);
static	int	getc(void);
static	char	*listmbox(void);
static	char	*literal(void);
static	uint	litlen(void);
static	Msgset	*msgset(int);
static	void	mustbe(int c);
static	uint	number(int nonzero);
static	int	peekc(void);
static	char	*quoted(void);
static	void	secttext(Fetch *, int);
static	uint	seqno(void);
static	Store	*storewhat(void);
static	char	*tag(void);
static	uint	uidno(void);
static	void	ungetc(void);

static	Parsecmd	Snonauthed[] =
{
	{"capability",		capabilitycmd},
	{"logout",		logoutcmd},
	{"noop",		noopcmd},
	{"x-exit",		logoutcmd},

	{"authenticate",	authenticatecmd},
	{"login",		logincmd},

	nil
};

static	Parsecmd	Sauthed[] =
{
	{"capability",		capabilitycmd},
	{"logout",		logoutcmd},
	{"noop",		noopcmd},
	{"x-exit",		logoutcmd},
	{"xdebug",		xdebugcmd},

	{"append",		appendcmd},
	{"create",		createcmd},
	{"delete",		deletecmd},
	{"examine",		selectcmd},
	{"select",		selectcmd},
	{"idle",		idlecmd},
	{"list",		listcmd},
	{"lsub",		listcmd},
	{"namespace",		namespacecmd},
	{"rename",		renamecmd},
	{"setquota",		setquotacmd},
	{"getquota",		getquotacmd},
	{"getquotaroot",		getquotarootcmd},
	{"status",		statuscmd},
	{"subscribe",		subscribecmd},
	{"unsubscribe",		unsubscribecmd},

	nil
};

static	Parsecmd	Sselected[] =
{
	{"capability",		capabilitycmd},
	{"xdebug",		xdebugcmd},
	{"logout",		logoutcmd},
	{"x-exit",		logoutcmd},
	{"noop",		noopcmd},

	{"append",		appendcmd},
	{"create",		createcmd},
	{"delete",		deletecmd},
	{"examine",		selectcmd},
	{"select",		selectcmd},
	{"idle",		idlecmd},
	{"list",		listcmd},
	{"lsub",		listcmd},
	{"namespace",		namespacecmd},
	{"rename",		renamecmd},
	{"status",		statuscmd},
	{"subscribe",		subscribecmd},
	{"unsubscribe",		unsubscribecmd},

	{"check",		noopcmd},
	{"close",		closecmd},
	{"copy",		copycmd},
	{"expunge",		expungecmd},
	{"fetch",		fetchcmd},
	{"search",		searchcmd},
	{"store",		storecmd},
	{"uid",			uidcmd},

	nil
};

static	char		*atomstop = "(){%*\"\\";
static	Parsecmd	*imapstate;
static	jmp_buf		parsejmp;
static	char		*parsemsg;
static	int		allowpass;
static	int		allowcr;
static	int		exiting;
static	QLock		imaplock;
static	int		idlepid = -1;

Biobuf	bout;
Biobuf	bin;
char	username[Userlen];
char	mboxdir[Pathlen];
char	*servername;
char	*site;
char	*remote;
char	*binupas;
Box	*selected;
Bin	*parsebin;
int	debug;
Uidplus	*uidlist;
Uidplus	**uidtl;

void
usage(void)
{
	fprint(2, "usage: upas/imap4d [-acpv] [-l logfile] [-b binupas] [-d site] [-r remotehost] [-s servername]\n");
	bye("usage");
}

void
main(int argc, char *argv[])
{
	int preauth;

	Binit(&bin, dup(0, -1), OREAD);
	close(0);
	Binit(&bout, 1, OWRITE);
	quotefmtinstall();
	fmtinstall('F', Ffmt);
	fmtinstall('D', Dfmt);	/* rfc822; # imap date %Z */
	fmtinstall(L'δ', Dfmt);	/* rfc822; # imap date %s */
	fmtinstall('X', Xfmt);
	fmtinstall('Y', Zfmt);
	fmtinstall('Z', Zfmt);

	/* for auth */
	fmtinstall('H', encodefmt);
	fmtinstall('[', encodefmt);

	preauth = 0;
	allowpass = 0;
	allowcr = 0;
	ARGBEGIN{
	case 'a':
		preauth = 1;
		break;
	case 'b':
		binupas = EARGF(usage());
		break;
	case 'c':
		allowcr = 1;
		break;
	case 'd':
		site = EARGF(usage());
		break;
	case 'l':
		snprint(logfile, sizeof logfile, "%s", EARGF(usage()));
		break;
	case 'p':
		allowpass = 1;
		break;
	case 'r':
		remote = EARGF(usage());
		break;
	case 's':
		servername = EARGF(usage());
		break;
	case 'v':
		debug ^= 1;
		break;
	default:
		usage();
		break;
	}ARGEND

	if(allowpass && allowcr){
		fprint(2, "imap4d: -c and -p are mutually exclusive\n");
		usage();
	}

	if(preauth)
		setupuser(nil);

	if(servername == nil){
		servername = csquery("sys", sysname(), "dom");
		if(servername == nil)
			servername = sysname();
		if(servername == nil){
			fprint(2, "ip/imap4d can't find server name: %r\n");
			bye("can't find system name");
		}
	}
	if(site == nil)
		site = getenv("site");
	if(site == nil){
		site = strchr(servername, '.');
		if(site)
			site++;
		else
			site = servername;
	}

	rfork(RFNOTEG|RFREND);

	atnotify(catcher, 1);
	qlock(&imaplock);
	atexit(cleaner);
	imap4(preauth);
}

static void
imap4(int preauth)
{
	char *volatile tg;
	char *volatile cmd;
	Parsecmd *st;

	if(preauth){
		Bprint(&bout, "* preauth %s IMAP4rev1 server ready user %s authenticated\r\n", servername, username);
		imapstate = Sauthed;
	}else{
		Bprint(&bout, "* OK %s IMAP4rev1 server ready\r\n", servername);
		imapstate = Snonauthed;
	}
	if(Bflush(&bout) < 0)
		writeerr();

	tg = nil;
	cmd = nil;
	if(setjmp(parsejmp)){
		if(tg == nil)
			Bprint(&bout, "* bad empty command line: %s\r\n", parsemsg);
		else if(cmd == nil)
			Bprint(&bout, "%s BAD no command: %s\r\n", tg, parsemsg);
		else
			Bprint(&bout, "%s BAD %s %s\r\n", tg, cmd, parsemsg);
		clearcmd();
		if(Bflush(&bout) < 0)
			writeerr();
		binfree(&parsebin);
	}
	for(;;){
		if(mblocked())
			bye("internal error: mailbox lock held");
		tg = nil;
		cmd = nil;
		tg = tag();
		mustbe(' ');
		cmd = atom();

		/*
		 * note: outlook express is broken: it requires echoing the
		 * command as part of matching response
		 */
		for(st = imapstate; st->name != nil; st++)
			if(cistrcmp(cmd, st->name) == 0){
				st->f(tg, cmd);
				break;
			}
		if(st->name == nil){
			clearcmd();
			Bprint(&bout, "%s BAD %s illegal command\r\n", tg, cmd);
		}

		if(Bflush(&bout) < 0)
			writeerr();
		binfree(&parsebin);
	}
}

void
bye(char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	Bprint(&bout, "* bye ");
	Bvprint(&bout, fmt, arg);
	Bprint(&bout, "\r\n");
	Bflush(&bout);
	exits(0);
}

void
parseerr(char *msg)
{
	debuglog("parse error: %s", msg);
	parsemsg = msg;
	longjmp(parsejmp, 1);
}

/*
 * an error occured while writing to the client
 */
void
writeerr(void)
{
	cleaner();
	_exits("connection closed");
}

static int
catcher(void *, char *msg)
{
	if(strstr(msg, "closed pipe") != nil)
		return 1;
	return 0;
}

/*
 * wipes out the idlecmd backgroung process if it is around.
 * this can only be called if the current proc has qlocked imaplock.
 * it must be the last piece of imap4d code executed.
 */
static void
cleaner(void)
{
	int i;

	debuglog("cleaner");
	if(idlepid < 0)
		return;
	exiting = 1;
	close(0);
	close(1);
	close(2);
	close(bin.fid);
	bin.fid = -1;
	/*
	 * the other proc is either stuck in a read, a sleep,
	 * or is trying to lock imap4lock.
	 * get him out of it so he can exit cleanly
	 */
	qunlock(&imaplock);
	for(i = 0; i < 4; i++)
		postnote(PNGROUP, getpid(), "die");
}

/*
 * send any pending status updates to the client
 * careful: shouldn't exit, because called by idle polling proc
 *
 * can't always send pending info
 * in particular, can't send expunge info
 * in response to a fetch, store, or search command.
 * 
 * rfc2060 5.2:	server must send mailbox size updates
 * rfc2060 5.2:	server may send flag updates
 * rfc2060 5.5:	servers prohibited from sending expunge while fetch, store, search in progress
 * rfc2060 7:	in selected state, server checks mailbox for new messages as part of every command
 * 		sends untagged EXISTS and RECENT respsonses reflecting new size of the mailbox
 * 		should also send appropriate untagged FETCH and EXPUNGE messages if another agent
 * 		changes the state of any message flags or expunges any messages
 * rfc2060 7.4.1	expunge server response must not be sent when no command is in progress,
 * 		nor while responding to a fetch, stort, or search command (uid versions are ok)
 * 		command only "in progress" after entirely parsed.
 *
 * strategy for third party deletion of messages or of a mailbox
 *
 * deletion of a selected mailbox => act like all message are expunged
 *	not strictly allowed by rfc2180, but close to method 3.2.
 *
 * renaming same as deletion
 *
 * copy
 *	reject iff a deleted message is in the request
 *
 * search, store, fetch operations on expunged messages
 *	ignore the expunged messages
 *	return tagged no if referenced
 */
static void
status(int expungeable, int uids)
{
	int tell;

	if(!selected)
		return;
	tell = 0;
	if(expungeable)
		tell = expungemsgs(selected, 1);
	if(selected->sendflags)
		sendflags(selected, uids);
	if(tell || selected->toldmax != selected->max){
		Bprint(&bout, "* %ud EXISTS\r\n", selected->max);
		selected->toldmax = selected->max;
	}
	if(tell || selected->toldrecent != selected->recent){
		Bprint(&bout, "* %ud RECENT\r\n", selected->recent);
		selected->toldrecent = selected->recent;
	}
	if(tell)
		closeimp(selected, checkbox(selected, 1));
}

/*
 * careful: can't exit, because called by idle polling proc
 */
static void
check(void)
{
	if(!selected)
		return;
	checkbox(selected, 0);
	status(1, 0);
}

static void
appendcmd(char *tg, char *cmd)
{
	char *mbox, head[128];
	uint t, n, now;
	int flags, ok;
	Uidplus u;

	mustbe(' ');
	mbox = astring();
	mustbe(' ');
	flags = 0;
	if(peekc() == '('){
		flags = flaglist();
		mustbe(' ');
	}
	now = time(nil);
	if(peekc() == '"'){
		t = imap4datetime(quoted());
		if(t == ~0)
			parseerr("illegal date format");
		mustbe(' ');
		if(t > now)
			t = now;
	}else
		t = now;
	n = litlen();

	mbox = mboxname(mbox);
	if(mbox == nil){
		check();
		Bprint(&bout, "%s NO %s bad mailbox\r\n", tg, cmd);
		return;
	}
	/* bug.  this is upas/fs's job */
	if(!cdexists(mboxdir, mbox)){
		check();
		Bprint(&bout, "%s NO [TRYCREATE] %s mailbox does not exist\r\n", tg, cmd);
		return;
	}

	snprint(head, sizeof head, "From %s %s", username, ctime(t));
	ok = appendsave(mbox, flags, head, &bin, n, &u);
	crnl();
	check();
	if(ok)
		Bprint(&bout, "%s OK [APPENDUID %ud %ud] %s completed\r\n",
			tg, u.uidvalidity, u.uid, cmd);
	else
		Bprint(&bout, "%s NO %s message save failed\r\n", tg, cmd);
}

static void
authenticatecmd(char *tg, char *cmd)
{
	char *s, *t;

	mustbe(' ');
	s = atom();
	if(cistrcmp(s, "cram-md5") == 0){
		crnl();
		t = cramauth();
		if(t == nil){
			Bprint(&bout, "%s OK %s\r\n", tg, cmd);
			imapstate = Sauthed;
		}else
			Bprint(&bout, "%s NO %s failed %s\r\n", tg, cmd, t);
	}else if(cistrcmp(s, "plain") == 0){
		s = nil;
		if(peekc() == ' '){
			mustbe(' ');
			s = astring();
		}
		crnl();
		if(!allowpass)
			Bprint(&bout, "%s NO %s plaintext passwords disallowed\r\n", tg, cmd);
		else if(t = plainauth(s))
			Bprint(&bout, "%s NO %s failed %s\r\n", tg, cmd, t);
		else{
			Bprint(&bout, "%s OK %s\r\n", tg, cmd);
			imapstate = Sauthed;
		}
	}else
		Bprint(&bout, "%s NO %s unsupported authentication protocol\r\n", tg, cmd);
}

static void
capabilitycmd(char *tg, char *cmd)
{
	crnl();
	check();
	Bprint(&bout, "* CAPABILITY IMAP4REV1 IDLE NAMESPACE QUOTA XDEBUG");
	Bprint(&bout, " UIDPLUS");
	if(allowpass || allowcr)
		Bprint(&bout, " AUTH=CRAM-MD5 AUTH=PLAIN");
	else
		Bprint(&bout, " LOGINDISABLED AUTH=CRAM-MD5");
	Bprint(&bout, "\r\n%s OK %s\r\n", tg, cmd);
}

static void
closecmd(char *tg, char *cmd)
{
	crnl();
	imapstate = Sauthed;
	closebox(selected, 1);
	selected = nil;
	Bprint(&bout, "%s OK %s mailbox closed, now in authenticated state\r\n", tg, cmd);
}

/*
 * note: message id's are before any pending expunges
 */
static void
copycmd(char *tg, char *cmd)
{
	copyucmd(tg, cmd, 0);
}

static char *uidpsep;
static int
printuid(Box*, Msg *m, int, void*)
{
	Bprint(&bout, "%s%ud", uidpsep, m->uid);
	uidpsep = ",";
	return 1;
}

static void
copyucmd(char *tg, char *cmd, int uids)
{
	char *uid, *mbox;
	int ok;
	uint max;
	Msgset *ms;
	Uidplus *u;

	mustbe(' ');
	ms = msgset(uids);
	mustbe(' ');
	mbox = astring();
	crnl();

	uid = "";
	if(uids)
		uid = "UID ";

	mbox = mboxname(mbox);
	if(mbox == nil){
		status(1, uids);
		Bprint(&bout, "%s NO %s%s bad mailbox\r\n", tg, uid, cmd);
		return;
	}
	if(!cdexists(mboxdir, mbox)){
		check();
		Bprint(&bout, "%s NO [TRYCREATE] %s mailbox does not exist\r\n", tg, cmd);
		return;
	}

	uidlist = 0;
	uidtl = &uidlist;

	max = selected->max;
	checkbox(selected, 0);
	ok = formsgs(selected, ms, max, uids, copycheck, nil);
	if(ok)
		ok = formsgs(selected, ms, max, uids, copysaveu, mbox);
	status(1, uids);
	if(ok && uidlist){
		u = uidlist;
		Bprint(&bout, "%s OK [COPYUID %ud", tg, u->uidvalidity);
		uidpsep = " ";
		formsgs(selected, ms, max, uids, printuid, mbox);
		Bprint(&bout, " %ud", u->uid);
		for(u = u->next; u; u = u->next)
			Bprint(&bout, ",%ud", u->uid);
		Bprint(&bout, "] %s%s completed\r\n", uid, cmd);
	}else if(ok)
		Bprint(&bout, "%s OK %s%s completed\r\n", tg, uid, cmd);
	else
		Bprint(&bout, "%s NO %s%s failed\r\n", tg, uid, cmd);
}

static void
createcmd(char *tg, char *cmd)
{
	char *mbox;

	mustbe(' ');
	mbox = astring();
	crnl();
	check();

	mbox = mboxname(mbox);
	if(mbox == nil){
		Bprint(&bout, "%s NO %s bad mailbox\r\n", tg, cmd);
		return;
	}
	if(cistrcmp(mbox, "mbox") == 0){
		Bprint(&bout, "%s NO %s cannot remotely create INBOX\r\n", tg, cmd);
		return;
	}
	if(creatembox(mbox) == -1)
		Bprint(&bout, "%s NO %s cannot create mailbox %#Y\r\n", tg, cmd, mbox);
	else
		Bprint(&bout, "%s OK %#Y %s completed\r\n", tg, mbox, cmd);
}

static void
xdebugcmd(char *tg, char *)
{
	char *s, *t;

	mustbe(' ');
	s = astring();
	t = 0;
	if(!cistrcmp(s, "file")){
		mustbe(' ');
		t = astring();
	}
	crnl();
	check();
	if(!cistrcmp(s, "on") || !cistrcmp(s, "1")){
		Bprint(&bout, "%s OK debug on\r\n", tg);
		debug = 1;
	}else if(!cistrcmp(s, "file")){
		if(!strstr(t, ".."))
			snprint(logfile, sizeof logfile, "%s", t);
		Bprint(&bout, "%s OK debug file %#Z\r\n", tg, logfile);
	}else{
		Bprint(&bout, "%s OK debug off\r\n", tg);
		debug = 0;
	}
}

static void
deletecmd(char *tg, char *cmd)
{
	char *mbox;

	mustbe(' ');
	mbox = astring();
	crnl();
	check();

	mbox = mboxname(mbox);
	if(mbox == nil){
		Bprint(&bout, "%s NO %s bad mailbox\r\n", tg, cmd);
		return;
	}

	/*
	 * i don't know if this is a hack or not.  a delete of the
	 * currently-selected box seems fishy.  the standard doesn't
	 * specify any behavior.
	 */
	if(selected && strcmp(selected->name, mbox) == 0){
		ilog("delete: client bug? close of selected mbox %s", selected->fs);
		imapstate = Sauthed;
		closebox(selected, 1);
		selected = nil;
		setname("[none]");
	}

	if(!cistrcmp(mbox, "mbox") || !removembox(mbox) == -1)
		Bprint(&bout, "%s NO %s cannot delete mailbox %#Y\r\n", tg, cmd, mbox);
	else
		Bprint(&bout, "%s OK %#Y %s completed\r\n", tg, mbox, cmd);
}

static void
expungeucmd(char *tg, char *cmd, int uids)
{
	int ok;
	Msgset *ms;

	ms = 0;
	if(uids){
		mustbe(' ');
		ms = msgset(uids);
	}
	crnl();
	ok = deletemsg(selected, ms);
	check();
	if(ok)
		Bprint(&bout, "%s OK %s completed\r\n", tg, cmd);
	else
		Bprint(&bout, "%s NO %s some messages not expunged\r\n", tg, cmd);
}

static void
expungecmd(char *tg, char *cmd)
{
	expungeucmd(tg, cmd, 0);
}

static void
fetchcmd(char *tg, char *cmd)
{
	fetchucmd(tg, cmd, 0);
}

static void
fetchucmd(char *tg, char *cmd, int uids)
{
	char *uid;
	int ok;
	uint max;
	Fetch *f;
	Msgset *ms;
	Mblock *ml;

	mustbe(' ');
	ms = msgset(uids);
	mustbe(' ');
	f = fetchwhat();
	crnl();
	uid = "";
	if(uids)
		uid = "uid ";
	max = selected->max;
	ml = checkbox(selected, 1);
	if(ml != nil)
		formsgs(selected, ms, max, uids, fetchseen, f);
	closeimp(selected, ml);
	ok = ml != nil && formsgs(selected, ms, max, uids, fetchmsg, f);
	status(uids, uids);
	if(ok)
		Bprint(&bout, "%s OK %s%s completed\r\n", tg, uid, cmd);
	else{
		if(ml == nil)
			ilog("nil maillock\n");
		Bprint(&bout, "%s NO %s%s failed\r\n", tg, uid, cmd);
	}
}

static void
idlecmd(char *tg, char *cmd)
{
	int c, pid;

	crnl();
	Bprint(&bout, "+ idling, waiting for done\r\n");
	if(Bflush(&bout) < 0)
		writeerr();

	if(idlepid < 0){
		pid = rfork(RFPROC|RFMEM|RFNOWAIT);
		if(pid == 0){
			setname("imap idle");
			for(;;){
				qlock(&imaplock);
				if(exiting)
					break;

				/*
				 * parent may have changed curdir, but it doesn't change our .
				 */
				resetcurdir();

				check();
				if(Bflush(&bout) < 0)
					writeerr();
				qunlock(&imaplock);
				sleep(15*1000);
				enableforwarding();
			}
			_exits(0);
		}
		idlepid = pid;
	}

	qunlock(&imaplock);

	/*
	 * clear out the next line, which is supposed to contain (case-insensitive)
	 * done\n
	 * this is special code since it has to dance with the idle polling proc
	 * and handle exiting correctly.
	 */
	for(;;){
		c = getc();
		if(c < 0){
			qlock(&imaplock);
			if(!exiting)
				cleaner();
			_exits(0);
		}
		if(c == '\n')
			break;
	}

	qlock(&imaplock);
	if(exiting)
		_exits(0);

	/*
	 * child may have changed curdir, but it doesn't change our .
	 */
	resetcurdir();
	check();
	Bprint(&bout, "%s OK %s terminated\r\n", tg, cmd);
}

static void
listcmd(char *tg, char *cmd)
{
	char *s, *t, *ref, *mbox;

	mustbe(' ');
	s = astring();
	mustbe(' ');
	t = listmbox();
	crnl();
	check();
	ref = mutf7str(s);
	mbox = mutf7str(t);
	if(ref == nil || mbox == nil){
		Bprint(&bout, "%s BAD %s modified utf-7\r\n", tg, cmd);
		return;
	}

	/*
	 * special request for hierarchy delimiter and root name
	 * root name appears to be name up to and including any delimiter,
	 * or the empty string, if there is no delimiter.
	 *
	 * this must change if the # namespace convention is supported.
	 */
	if(*mbox == '\0'){
		s = strchr(ref, '/');
		if(s == nil)
			ref = "";
		else
			s[1] = '\0';
		Bprint(&bout, "* %s (\\Noselect) \"/\" \"%s\"\r\n", cmd, ref);
		Bprint(&bout, "%s OK %s\r\n", tg, cmd);
		return;
	}

	/*
	 * hairy exception: these take non-fsencoded strings.  BUG?
	 */
	if(cistrcmp(cmd, "lsub") == 0)
		lsubboxes(cmd, ref, mbox);
	else
		listboxes(cmd, ref, mbox);
	Bprint(&bout, "%s OK %s completed\r\n", tg, cmd);
}

static void
logincmd(char *tg, char *cmd)
{
	char *r, *s, *t;

	mustbe(' ');
	s = astring();	/* uid */
	mustbe(' ');
	t = astring();	/* password */
	crnl();
	if(allowcr){
		if(r = crauth(s, t)){
			Bprint(&bout, "* NO [ALERT] %s\r\n", r);
			Bprint(&bout, "%s NO %s succeeded\r\n", tg, cmd);
		}else{
			Bprint(&bout, "%s OK %s succeeded\r\n", tg, cmd);
			imapstate = Sauthed;
		}
		return;
	}else if(allowpass){
		if(r = passauth(s, t))
			Bprint(&bout, "%s NO %s failed check [%s]\r\n", tg, cmd, r);
		else{
			Bprint(&bout, "%s OK %s succeeded\r\n", tg, cmd);
			imapstate = Sauthed;
		}
		return;
	}
	Bprint(&bout, "%s NO %s plaintext passwords disallowed\r\n", tg, cmd);
}

/*
 * logout or x-exit, which doesn't expunge the mailbox
 */
static void
logoutcmd(char *tg, char *cmd)
{
	crnl();

	if(cmd[0] != 'x' && selected){
		closebox(selected, 1);
		selected = nil;
	}
	Bprint(&bout, "* bye\r\n");
	Bprint(&bout, "%s OK %s completed\r\n", tg, cmd);
	exits(0);
}

static void
namespacecmd(char *tg, char *cmd)
{
	crnl();
	check();

	/*
	 * personal, other users, shared namespaces
	 * send back nil or descriptions of (prefix heirarchy-delim) for each case
	 */
	Bprint(&bout, "* NAMESPACE ((\"\" \"/\")) nil nil\r\n");
	Bprint(&bout, "%s OK %s completed\r\n", tg, cmd);
}

static void
noopcmd(char *tg, char *cmd)
{
	crnl();
	check();
	Bprint(&bout, "%s OK %s completed\r\n", tg, cmd);
	enableforwarding();
}

static void
getquota0(char *tg, char *cmd, char *r)
{
extern vlong getquota(void);
	vlong v;

	if(r[0]){
		Bprint(&bout, "%s NO %s no such quota root\r\n", tg, cmd);
		return;
	}
	v = getquota();
	if(v == -1){
		Bprint(&bout, "%s NO %s bad [%r]\r\n", tg, cmd);
		return;
	}
	Bprint(&bout, "* %s "" (storage %llud %d)\r\n", cmd, v/1024, 256*1024);
	Bprint(&bout, "%s OK %s completed\r\n", tg, cmd);
}

static void
getquotacmd(char *tg, char *cmd)
{
	char *r;

	mustbe(' ');
	r = astring();
	crnl();
	check();
	getquota0(tg, cmd, r);
}

static void
getquotarootcmd(char *tg, char *cmd)
{
	char *r;

	mustbe(' ');
	r = astring();
	crnl();
	check();

	Bprint(&bout, "* %s %s \"\"\r\n", cmd, r);
	getquota0(tg, cmd, "");
}

static void
setquotacmd(char *tg, char *cmd)
{
	mustbe(' ');
	astring();
	mustbe(' ');
	mustbe('(');
	for(;;){
		astring();
		mustbe(' ');
		number(0);
		if(peekc() == ')')
			break;
	}
	getc();
	crnl();
	check();
	Bprint(&bout, "%s NO %s error: can't set that data\r\n", tg, cmd);
}

/*
 * this is only a partial implementation
 * should copy files to other directories,
 * and copy & truncate inbox
 */
static void
renamecmd(char *tg, char *cmd)
{
	char *from, *to;

	mustbe(' ');
	from = astring();
	mustbe(' ');
	to = astring();
	crnl();
	check();

	to = mboxname(to);
	if(to == nil || cistrcmp(to, "mbox") == 0){
		Bprint(&bout, "%s NO %s bad mailbox destination name\r\n", tg, cmd);
		return;
	}
	if(access(to, AEXIST) >= 0){
		Bprint(&bout, "%s NO %s mailbox already exists\r\n", tg, cmd);
		return;
	}
	from = mboxname(from);
	if(from == nil){
		Bprint(&bout, "%s NO %s bad mailbox destination name\r\n", tg, cmd);
		return;
	}
	if(renamebox(from, to, strcmp(from, "mbox")))
		Bprint(&bout, "%s OK %s completed\r\n", tg, cmd);
	else
		Bprint(&bout, "%s NO %s failed\r\n", tg, cmd);
}

static void
searchcmd(char *tg, char *cmd)
{
	searchucmd(tg, cmd, 0);
}

/*
 * mail.app has a vicious habit of appending a message to
 * a folder and then immediately searching for it by message-id.
 * for a 10,000 message sent folder, this can be quite painful.
 *
 * evil strategy.  for message-id searches, check the last
 * message in the mailbox!  if that fails, use the normal algorithm.
 */
static Msg*
mailappsucks(Search *s)
{
	Msg *m;

	if(s->key == SKuid)
		s = s->next;
	if(s && s->next == nil)
	if(s->key == SKheader && cistrcmp(s->hdr, "message-id") == 0){
		for(m = selected->msgs; m && m->next; m = m->next)
			;
		if(m != nil)
		if(m->matched = searchmsg(m, s, 0))
			return m;
	}
	return 0;
}

static void
searchucmd(char *tg, char *cmd, int uids)
{
	char *uid;
	uint id, ld;
	Msg *m;
	Search rock;

	mustbe(' ');
	rock.next = nil;
	searchkeys(1, &rock);
	crnl();
	uid = "";
	if(uids)
		uid = "UID ";		/* android needs caps */
	if(rock.next != nil && rock.next->key == SKcharset){
		if(cistrcmp(rock.next->s, "utf-8") != 0
		&& cistrcmp(rock.next->s, "us-ascii") != 0){
			Bprint(&bout, "%s NO [BADCHARSET] (\"US-ASCII\" \"UTF-8\") %s%s failed\r\n", tg, uid, cmd);
			checkbox(selected, 0);
			status(uids, uids);
			return;
		}
		rock.next = rock.next->next;
	}
	Bprint(&bout, "* search");
	if(m = mailappsucks(rock.next))
			goto cheat;
	ld = searchld(rock.next);
	for(m = selected->msgs; m != nil; m = m->next)
		m->matched = searchmsg(m, rock.next, ld);
	for(m = selected->msgs; m != nil; m = m->next){
cheat:
		if(m->matched){
			if(uids)
				id = m->uid;
			else
				id = m->seq;
			Bprint(&bout, " %ud", id);
		}
	}
	Bprint(&bout, "\r\n");
	checkbox(selected, 0);
	status(uids, uids);
	Bprint(&bout, "%s OK %s%s completed\r\n", tg, uid, cmd);
}

static void
selectcmd(char *tg, char *cmd)
{
	char *s, *m0, *mbox, buf[Pathlen];
	Msg *m;

	mustbe(' ');
	m0 = astring();
	crnl();

	if(selected){
		imapstate = Sauthed;
		closebox(selected, 1);
		selected = nil;
		setname("[none]");
	}
	debuglog("select %s", m0);

	mbox = mboxname(m0);
	if(mbox == nil){
		debuglog("select %s [%s] -> no bad", mbox, m0);
		Bprint(&bout, "%s NO %s bad mailbox\r\n", tg, cmd);
		return;
	}

	selected = openbox(mbox, "imap", cistrcmp(cmd, "select") == 0);
	if(selected == nil){
		Bprint(&bout, "%s NO %s can't open mailbox %#Y: %r\r\n", tg, cmd, mbox);
		return;
	}

	setname("%s", decfs(buf, sizeof buf, selected->name));
	imapstate = Sselected;

	Bprint(&bout, "* FLAGS (\\Seen \\Answered \\Flagged \\Deleted \\Draft)\r\n");
	Bprint(&bout, "* %ud EXISTS\r\n", selected->max);
	selected->toldmax = selected->max;
	Bprint(&bout, "* %ud RECENT\r\n", selected->recent);
	selected->toldrecent = selected->recent;
	for(m = selected->msgs; m != nil; m = m->next){
		if(!m->expunged && (m->flags & Fseen) != Fseen){
			Bprint(&bout, "* OK [UNSEEN %ud]\r\n", m->seq);
			break;
		}
	}
	Bprint(&bout, "* OK [PERMANENTFLAGS (\\Seen \\Answered \\Flagged \\Draft \\Deleted)]\r\n");
	Bprint(&bout, "* OK [UIDNEXT %ud]\r\n", selected->uidnext);
	Bprint(&bout, "* OK [UIDVALIDITY %ud]\r\n", selected->uidvalidity);
	s = "READ-ONLY";
	if(selected->writable)
		s = "READ-WRITE";
	Bprint(&bout, "%s OK [%s] %s %#Y completed\r\n", tg, s, cmd, mbox);
}

static Namedint	statusitems[] =
{
	{"MESSAGES",	Smessages},
	{"RECENT",	Srecent},
	{"UIDNEXT",	Suidnext},
	{"UIDVALIDITY",	Suidvalidity},
	{"UNSEEN",	Sunseen},
	{nil,		0}
};

static void
statuscmd(char *tg, char *cmd)
{
	char *s, *mbox;
	int si, i, opened;
	uint v;
	Box *box;
	Msg *m;

	mustbe(' ');
	mbox = astring();
	mustbe(' ');
	mustbe('(');
	si = 0;
	for(;;){
		s = atom();
		i = mapint(statusitems, s);
		if(i == 0)
			parseerr("illegal status item");
		si |= i;
		if(peekc() == ')')
			break;
		mustbe(' ');
	}
	mustbe(')');
	crnl();

	mbox = mboxname(mbox);
	if(mbox == nil){
		check();
		Bprint(&bout, "%s NO %s bad mailbox\r\n", tg, cmd);
		return;
	}

	opened = 0;
	if(selected && !strcmp(mbox, selected->name))
		box = selected;
	else{
		box = openbox(mbox, "status", 1);
		if(box == nil){
			check();
			Bprint(&bout, "%s NO [TRYCREATE] %s can't open mailbox %#Y: %r\r\n", tg, cmd, mbox);
			return;
		}
		opened = 1;
	}

	Bprint(&bout, "* STATUS %#Y (", mbox);
	s = "";
	for(i = 0; statusitems[i].name != nil; i++)
		if(si & statusitems[i].v){
			v = 0;
			switch(statusitems[i].v){
			case Smessages:
				v = box->max;
				break;
			case Srecent:
				v = box->recent;
				break;
			case Suidnext:
				v = box->uidnext;
				break;
			case Suidvalidity:
				v = box->uidvalidity;
				break;
			case Sunseen:
				v = 0;
				for(m = box->msgs; m != nil; m = m->next)
					if((m->flags & Fseen) != Fseen)
						v++;
				break;
			default:
				Bprint(&bout, ")");
				bye("internal error: status item not implemented");
				break;
			}
			Bprint(&bout, "%s%s %ud", s, statusitems[i].name, v);
			s = " ";
		}
	Bprint(&bout, ")\r\n");
	if(opened)
		closebox(box, 1);

	check();
	Bprint(&bout, "%s OK %s completed\r\n", tg, cmd);
}

static void
storecmd(char *tg, char *cmd)
{
	storeucmd(tg, cmd, 0);
}

static void
storeucmd(char *tg, char *cmd, int uids)
{
	char *uid;
	int ok;
	uint max;
	Mblock *ml;
	Msgset *ms;
	Store *st;

	mustbe(' ');
	ms = msgset(uids);
	mustbe(' ');
	st = storewhat();
	crnl();
	uid = "";
	if(uids)
		uid = "uid ";
	max = selected->max;
	ml = checkbox(selected, 1);
	ok = ml != nil && formsgs(selected, ms, max, uids, storemsg, st);
	closeimp(selected, ml);
	status(uids, uids);
	if(ok)
		Bprint(&bout, "%s OK %s%s completed\r\n", tg, uid, cmd);
	else
		Bprint(&bout, "%s NO %s%s failed\r\n", tg, uid, cmd);
}

/*
 * minimal implementation of subscribe
 * all folders are automatically subscribed,
 * and can't be unsubscribed
 */
static void
subscribecmd(char *tg, char *cmd)
{
	char *mbox;
	int ok;
	Box *box;

	mustbe(' ');
	mbox = astring();
	crnl();
	check();
	mbox = mboxname(mbox);
	ok = 0;
	if(mbox != nil && (box = openbox(mbox, "subscribe", 0))){
		ok = subscribe(mbox, 's');
		closebox(box, 1);
	}
	if(!ok)
		Bprint(&bout, "%s NO %s bad mailbox\r\n", tg, cmd);
	else
		Bprint(&bout, "%s OK %s completed\r\n", tg, cmd);
}

static void
uidcmd(char *tg, char *cmd)
{
	char *sub;

	mustbe(' ');
	sub = atom();
	if(cistrcmp(sub, "copy") == 0)
		copyucmd(tg, sub, 1);
	else if(cistrcmp(sub, "fetch") == 0)
		fetchucmd(tg, sub, 1);
	else if(cistrcmp(sub, "search") == 0)
		searchucmd(tg, sub, 1);
	else if(cistrcmp(sub, "store") == 0)
		storeucmd(tg, sub, 1);
	else if(cistrcmp(sub, "expunge") == 0)
		expungeucmd(tg, sub, 1);
	else{
		clearcmd();
		Bprint(&bout, "%s BAD %s illegal uid command %s\r\n", tg, cmd, sub);
	}
}

static void
unsubscribecmd(char *tg, char *cmd)
{
	char *mbox;

	mustbe(' ');
	mbox = astring();
	crnl();
	check();
	mbox = mboxname(mbox);
	if(mbox == nil || !subscribe(mbox, 'u'))
		Bprint(&bout, "%s NO %s can't unsubscribe\r\n", tg, cmd);
	else
		Bprint(&bout, "%s OK %s completed\r\n", tg, cmd);
}

static char *gbuf;
static void
badsyn(void)
{
	debuglog("syntax error [%s]", gbuf);
	parseerr("bad syntax");
}

static void
clearcmd(void)
{
	int c;

	for(;;){
		c = getc();
		if(c < 0)
			bye("end of input");
		if(c == '\n')
			return;
	}
}

static void
crnl(void)
{
	int c;

	c = getc();
	if(c == '\n')
		return;
	if(c != '\r' || getc() != '\n')
		badsyn();
}

static void
mustbe(int c)
{
	int x;

	if((x = getc()) != c){
		ungetc();
		ilog("must be '%c' got %c", c, x);
		badsyn();
	}
}

/*
 * flaglist	: '(' ')' | '(' flags ')'
 */
static int
flaglist(void)
{
	int f;

	mustbe('(');
	f = 0;
	if(peekc() != ')')
		f = flags();

	mustbe(')');
	return f;
}

/*
 * flags	: flag | flags ' ' flag
 * flag		: '\' atom | atom
 */
static int
flags(void)
{
	char *s;
	int ff, flags, c;

	flags = 0;
	for(;;){
		c = peekc();
		if(c == '\\'){
			mustbe('\\');
			s = atomstring(atomstop, "\\");
		}else if(strchr(atomstop, c) != nil)
			s = atom();
		else
			break;
		ff = mapflag(s);
		if(ff == 0)
			parseerr("flag not supported");
		flags |= ff;
		if(peekc() != ' ')
			break;
		mustbe(' ');
	}
	if(flags == 0)
		parseerr("no flags given");
	return flags;
}

/*
 * storewhat	: osign 'FLAGS' ' ' storeflags
 *		| osign 'FLAGS.SILENT' ' ' storeflags
 * osign	:
 *		| '+' | '-'
 * storeflags	: flaglist | flags
 */
static Store*
storewhat(void)
{
	char *s;
	int c, f, w;

	c = peekc();
	if(c == '+' || c == '-')
		mustbe(c);
	else
		c = 0;
	s = atom();
	w = 0;
	if(cistrcmp(s, "flags") == 0)
		w = Stflags;
	else if(cistrcmp(s, "flags.silent") == 0)
		w = Stflagssilent;
	else
		parseerr("illegal store attribute");
	mustbe(' ');
	if(peekc() == '(')
		f = flaglist();
	else
		f = flags();
	return mkstore(c, w, f);
}

/*
 * fetchwhat	: "ALL" | "FULL" | "FAST" | fetchatt | '(' fetchatts ')'
 * fetchatts	: fetchatt | fetchatts ' ' fetchatt
 */
static char *fetchatom	= "(){}%*\"\\[]";
static Fetch*
fetchwhat(void)
{
	char *s;
	Fetch *f;

	if(peekc() == '('){
		getc();
		f = nil;
		for(;;){
			s = atomstring(fetchatom, "");
			f = fetchatt(s, f);
			if(peekc() == ')')
				break;
			mustbe(' ');
		}
		getc();
		return revfetch(f);
	}

	s = atomstring(fetchatom, "");
	if(cistrcmp(s, "all") == 0)
		f = mkfetch(Fflags, mkfetch(Finternaldate, mkfetch(Frfc822size, mkfetch(Fenvelope, nil))));
	else if(cistrcmp(s, "fast") == 0)
		f = mkfetch(Fflags, mkfetch(Finternaldate, mkfetch(Frfc822size, nil)));
	else if(cistrcmp(s, "full") == 0)
		f = mkfetch(Fflags, mkfetch(Finternaldate, mkfetch(Frfc822size, mkfetch(Fenvelope, mkfetch(Fbody, nil)))));
	else
		f = fetchatt(s, nil);
	return f;
}

/*
 * fetchatt	: "ENVELOPE" | "FLAGS" | "INTERNALDATE"
 *		| "RFC822" | "RFC822.HEADER" | "RFC822.SIZE" | "RFC822.TEXT"
 *		| "BODYSTRUCTURE"
 *		| "UID"
 *		| "BODY"
 *		| "BODY" bodysubs
 *		| "BODY.PEEK" bodysubs
 * bodysubs	: sect
 *		| sect '<' number '.' nz-number '>'
 * sect		: '[' sectspec ']'
 * sectspec	: sectmsgtext
 *		| sectpart
 *		| sectpart '.' secttext
 * sectpart	: nz-number
 *		| sectpart '.' nz-number
 */
Nlist*
mknlist(void)
{
	Nlist *nl;

	nl = binalloc(&parsebin, sizeof *nl, 1);
	if(nl == nil)
		parseerr("out of memory");
	nl->n = number(1);
	return nl;
}

static Fetch*
fetchatt(char *s, Fetch *f)
{
	int c;
	Nlist *n;

	if(cistrcmp(s, "envelope") == 0)
		return mkfetch(Fenvelope, f);
	if(cistrcmp(s, "flags") == 0)
		return mkfetch(Fflags, f);
	if(cistrcmp(s, "internaldate") == 0)
		return mkfetch(Finternaldate, f);
	if(cistrcmp(s, "RFC822") == 0)
		return mkfetch(Frfc822, f);
	if(cistrcmp(s, "RFC822.header") == 0)
		return mkfetch(Frfc822head, f);
	if(cistrcmp(s, "RFC822.size") == 0)
		return mkfetch(Frfc822size, f);
	if(cistrcmp(s, "RFC822.text") == 0)
		return mkfetch(Frfc822text, f);
	if(cistrcmp(s, "bodystructure") == 0)
		return mkfetch(Fbodystruct, f);
	if(cistrcmp(s, "uid") == 0)
		return mkfetch(Fuid, f);

	if(cistrcmp(s, "body") == 0){
		if(peekc() != '[')
			return mkfetch(Fbody, f);
		f = mkfetch(Fbodysect, f);
	}else if(cistrcmp(s, "body.peek") == 0)
		f = mkfetch(Fbodypeek, f);
	else
		parseerr("illegal fetch attribute");

	mustbe('[');
	c = peekc();
	if(c >= '1' && c <= '9'){
		n = f->sect = mknlist();
		while(peekc() == '.'){
			getc();
			c = peekc();
			if(c < '1' || c > '9')
				break;
			n->next = mknlist();
			n = n->next;
		}
	}
	if(peekc() != ']')
		secttext(f, f->sect != nil);
	mustbe(']');

	if(peekc() != '<')
		return f;

	f->partial = 1;
	mustbe('<');
	f->start = number(0);
	mustbe('.');
	f->size = number(1);
	mustbe('>');
	return f;
}

/*
 * secttext	: sectmsgtext | "MIME"
 * sectmsgtext	: "HEADER"
 *		| "TEXT"
 *		| "HEADER.FIELDS" ' ' hdrlist
 *		| "HEADER.FIELDS.NOT" ' ' hdrlist
 * hdrlist	: '(' hdrs ')'
 * hdrs:	: astring
 *		| hdrs ' ' astring
 */
static void
secttext(Fetch *f, int mimeok)
{
	char *s;
	Slist *h;

	s = atomstring(fetchatom, "");
	if(cistrcmp(s, "header") == 0){
		f->part = FPhead;
		return;
	}
	if(cistrcmp(s, "text") == 0){
		f->part = FPtext;
		return;
	}
	if(mimeok && cistrcmp(s, "mime") == 0){
		f->part = FPmime;
		return;
	}
	if(cistrcmp(s, "header.fields") == 0)
		f->part = FPheadfields;
	else if(cistrcmp(s, "header.fields.not") == 0)
		f->part = FPheadfieldsnot;
	else
		parseerr("illegal fetch section text");
	mustbe(' ');
	mustbe('(');
	h = nil;
	for(;;){
		h = mkslist(astring(), h);
		if(peekc() == ')')
			break;
		mustbe(' ');
	}
	mustbe(')');
	f->hdrs = revslist(h);
}

/*
 * searchwhat	: "CHARSET" ' ' astring searchkeys | searchkeys
 * searchkeys	: searchkey | searchkeys ' ' searchkey
 * searchkey	: "ALL" | "ANSWERED" | "DELETED" | "FLAGGED" | "NEW" | "OLD" | "RECENT"
 *		| "SEEN" | "UNANSWERED" | "UNDELETED" | "UNFLAGGED" | "DRAFT" | "UNDRAFT"
 *		| astrkey ' ' astring
 *		| datekey ' ' date
 *		| "KEYWORD" ' ' flag | "UNKEYWORD" flag
 *		| "LARGER" ' ' number | "SMALLER" ' ' number
 * 		| "HEADER" astring ' ' astring
 *		| set | "UID" ' ' set
 *		| "NOT" ' ' searchkey
 *		| "OR" ' ' searchkey ' ' searchkey
 *		| '(' searchkeys ')'
 * astrkey	: "BCC" | "BODY" | "CC" | "FROM" | "SUBJECT" | "TEXT" | "TO"
 * datekey	: "BEFORE" | "ON" | "SINCE" | "SENTBEFORE" | "SENTON" | "SENTSINCE"
 */
static Namedint searchmap[] =
{
	{"ALL",		SKall},
	{"ANSWERED",	SKanswered},
	{"DELETED",	SKdeleted},
	{"FLAGGED",	SKflagged},
	{"NEW",		SKnew},
	{"OLD",		SKold},
	{"RECENT",	SKrecent},
	{"SEEN",	SKseen},
	{"UNANSWERED",	SKunanswered},
	{"UNDELETED",	SKundeleted},
	{"UNFLAGGED",	SKunflagged},
	{"DRAFT",	SKdraft},
	{"UNDRAFT",	SKundraft},
	{"UNSEEN",	SKunseen},
	{nil,		0}
};

static Namedint searchmapstr[] =
{
	{"CHARSET",	SKcharset},
	{"BCC",		SKbcc},
	{"BODY",	SKbody},
	{"CC",		SKcc},
	{"FROM",	SKfrom},
	{"SUBJECT",	SKsubject},
	{"TEXT",	SKtext},
	{"TO",		SKto},
	{nil,		0}
};

static Namedint searchmapdate[] =
{
	{"BEFORE",	SKbefore},
	{"ON",		SKon},
	{"SINCE",	SKsince},
	{"SENTBEFORE",	SKsentbefore},
	{"SENTON",	SKsenton},
	{"SENTSINCE",	SKsentsince},
	{nil,		0}
};

static Namedint searchmapflag[] =
{
	{"KEYWORD",	SKkeyword},
	{"UNKEYWORD",	SKunkeyword},
	{nil,		0}
};

static Namedint searchmapnum[] =
{
	{"SMALLER",	SKsmaller},
	{"LARGER",	SKlarger},
	{nil,		0}
};

static Search*
searchkeys(int first, Search *tail)
{
	Search *s;

	for(;;){
		if(peekc() == '('){
			getc();
			tail = searchkeys(0, tail);
			mustbe(')');
		}else{
			s = searchkey(first);
			tail->next = s;
			tail = s;
		}
		first = 0;
		if(peekc() != ' ')
			break;
		getc();
	}
	return tail;
}

static Search*
searchkey(int first)
{
	char *a;
	int i, c;
	Search *sr, rock;
	Tm tm;

	sr = binalloc(&parsebin, sizeof *sr, 1);
	if(sr == nil)
		parseerr("out of memory");

	c = peekc();
	if(c >= '0' && c <= '9'){
		sr->key = SKset;
		sr->set = msgset(0);
		return sr;
	}

	a = atom();
	if(i = mapint(searchmap, a))
		sr->key = i;
	else if(i = mapint(searchmapstr, a)){
		if(!first && i == SKcharset)
			parseerr("illegal search key");
		sr->key = i;
		mustbe(' ');
		sr->s = astring();
	}else if(i = mapint(searchmapdate, a)){
		sr->key = i;
		mustbe(' ');
		c = peekc();
		if(c == '"')
			getc();
		a = atom();
		if(a == nil || !imap4date(&tm, a))
			parseerr("bad date format");
		sr->year = tm.year;
		sr->mon = tm.mon;
		sr->mday = tm.mday;
		if(c == '"')
			mustbe('"');
	}else if(i = mapint(searchmapflag, a)){
		sr->key = i;
		mustbe(' ');
		c = peekc();
		if(c == '\\'){
			mustbe('\\');
			a = atomstring(atomstop, "\\");
		}else
			a = atom();
		i = mapflag(a);
		if(i == 0)
			parseerr("flag not supported");
		sr->num = i;
	}else if(i = mapint(searchmapnum, a)){
		sr->key = i;
		mustbe(' ');
		sr->num = number(0);
	}else if(cistrcmp(a, "HEADER") == 0){
		sr->key = SKheader;
		mustbe(' ');
		sr->hdr = astring();
		mustbe(' ');
		sr->s = astring();
	}else if(cistrcmp(a, "UID") == 0){
		sr->key = SKuid;
		mustbe(' ');
		sr->set = msgset(0);
	}else if(cistrcmp(a, "NOT") == 0){
		sr->key = SKnot;
		mustbe(' ');
		rock.next = nil;
		searchkeys(0, &rock);
		sr->left = rock.next;
	}else if(cistrcmp(a, "OR") == 0){
		sr->key = SKor;
		mustbe(' ');
		rock.next = nil;
		searchkeys(0, &rock);
		sr->left = rock.next;
		mustbe(' ');
		rock.next = nil;
		searchkeys(0, &rock);
		sr->right = rock.next;
	}else
		parseerr("illegal search key");
	return sr;
}

/*
 * set	: seqno
 *	| seqno ':' seqno
 *	| set ',' set
 * seqno: nz-number
 *	| '*'
 *
 */
static Msgset*
msgset(int uids)
{
	uint from, to;
	Msgset head, *last, *ms;

	last = &head;
	head.next = nil;
	for(;;){
		from = uids ? uidno() : seqno();
		to = from;
		if(peekc() == ':'){
			getc();
			to = uids? uidno(): seqno();
		}
		ms = binalloc(&parsebin, sizeof *ms, 0);
		if(ms == nil)
			parseerr("out of memory");
		if(to < from){
			ms->from = to;
			ms->to = from;
		}else{
			ms->from = from;
			ms->to = to;
		}
		ms->next = nil;
		last->next = ms;
		last = ms;
		if(peekc() != ',')
			break;
		getc();
	}
	return head.next;
}

static uint
seqno(void)
{
	if(peekc() == '*'){
		getc();
		return ~0UL;
	}
	return number(1);
}

static uint
uidno(void)
{
	if(peekc() == '*'){
		getc();
		return ~0UL;
	}
	return number(0);
}

/*
 * 7 bit, non-ctl chars, no (){%*"\
 * NIL is special case for nstring or parenlist
 */
static char *
atom(void)
{
	return atomstring(atomstop, "");
}

/*
 * like an atom, but no +
 */
static char *
tag(void)
{
	return atomstring("+(){%*\"\\", "");
}

/*
 * string or atom allowing %*
 */
static char *
listmbox(void)
{
	int c;

	c = peekc();
	if(c == '{')
		return literal();
	if(c == '"')
		return quoted();
	return atomstring("(){\"\\", "");
}

/*
 * string or atom
 */
static char *
astring(void)
{
	int c;

	c = peekc();
	if(c == '{')
		return literal();
	if(c == '"')
		return quoted();
	return atom();
}

/*
 * 7 bit, non-ctl chars, none from exception list
 */
static char *
atomstring(char *disallowed, char *initial)
{
	char *s;
	int c, ns, as;

	ns = strlen(initial);
	s = binalloc(&parsebin, ns + Stralloc, 0);
	if(s == nil)
		parseerr("out of memory");
	strcpy(s, initial);
	as = ns + Stralloc;
	for(;;){
		c = getc();
		if(c <= ' ' || c >= 0x7f || strchr(disallowed, c) != nil){
			ungetc();
			break;
		}
		s[ns++] = c;
		if(ns >= as){
			s = bingrow(&parsebin, s, as, as + Stralloc, 0);
			if(s == nil)
				parseerr("out of memory");
			as += Stralloc;
		}
	}
	if(ns == 0)
		badsyn();
	s[ns] = '\0';
	return s;
}

/*
 * quoted: '"' chars* '"'
 * chars:	1-128 except \r and \n
 */
static char *
quoted(void)
{
	char *s;
	int c, ns, as;

	mustbe('"');
	s = binalloc(&parsebin, Stralloc, 0);
	if(s == nil)
		parseerr("out of memory");
	as = Stralloc;
	ns = 0;
	for(;;){
		c = getc();
		if(c == '"')
			break;
		if(c < 1 || c > 0x7f || c == '\r' || c == '\n')
			badsyn();
		if(c == '\\'){
			c = getc();
			if(c != '\\' && c != '"')
				badsyn();
		}
		s[ns++] = c;
		if(ns >= as){
			s = bingrow(&parsebin, s, as, as + Stralloc, 0);
			if(s == nil)
				parseerr("out of memory");
			as += Stralloc;
		}
	}
	s[ns] = '\0';
	return s;
}

/*
 * litlen: {number}\r\n
 */
static uint
litlen(void)
{
	uint v;

	mustbe('{');
	v = number(0);
	mustbe('}');
	crnl();
	return v;
}

/*
 * literal: litlen data<0:litlen>
 */
static char*
literal(void)
{
	char *s;
	uint v;

	v = litlen();
	s = binalloc(&parsebin, v + 1, 0);
	if(s == nil)
		parseerr("out of memory");
	Bprint(&bout, "+ Ready for literal data\r\n");
	if(Bflush(&bout) < 0)
		writeerr();
	if(v != 0 && Bread(&bin, s, v) != v)
		badsyn();
	s[v] = '\0';
	return s;
}

/*
 * digits; number is 32 bits
 */
enum{
	Max = 0xffffffff/10,
};

static uint
number(int nonzero)
{
	uint n, nn;
	int c, first, ovfl;

	n = 0;
	first = 1;
	ovfl = 0;
	for(;;){
		c = getc();
		if(c < '0' || c > '9'){
			ungetc();
			if(first)
				badsyn();
			break;
		}
		c -= '0';
		first = 0;
		if(n > Max)
			ovfl = 1;
		nn = n*10 + c;
		if(nn < n)
			ovfl = 1;
		n = nn;
	}
	if(nonzero && n == 0)
		badsyn();
	if(ovfl)
		parseerr("number out of range\r\n");
	return n;
}

static void
logit(char *o)
{
	char *s, *p, *q;

	if(!debug)
		return;
	s = strdup(o);
	p = strchr(s, ' ');
	if(!p)
		goto emit;
	q = strchr(++p, ' ');
	if(!q)
		goto emit;
	if(!cistrncmp(p, "login", 5)){
		q = strchr(++q, ' ');
		if(!q)
			goto emit;
		for(q = q + 1; *q != ' ' && *q; q++)
			*q = '*';
	}
emit:
	for(p = s + strlen(s) - 1; p >= s && (/**p == '\r' ||*/ *p == '\n'); )
		*p-- = 0;
	ilog("%s", s);
	free(s);
}

static char *gbuf;
static char *gbufp = "";

static int
getc(void)
{
	if(*gbufp == 0){
		free(gbuf);
		werrstr("");
		gbufp = gbuf = Brdstr(&bin, '\n', 0);
		if(gbuf == 0){
			ilog("empty line [%d]: %r", bin.fid);
			gbufp = "";
			return -1;
		}
		logit(gbuf);
	}
	return *gbufp++;
}

static void
ungetc(void)
{
	if(gbufp > gbuf)
		gbufp--;
}

static int
peekc(void)
{
	return *gbufp;
}

#ifdef normal

static int
getc(void)
{
	return Bgetc(&bin);
}

static void
ungetc(void)
{
	Bungetc(&bin);
}

static int
peekc(void)
{
	int c;

	c = Bgetc(&bin);
	Bungetc(&bin);
	return c;
}
#endif
