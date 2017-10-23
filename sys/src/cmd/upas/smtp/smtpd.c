#include "common.h"
#include "smtpd.h"
#include "smtp.h"
#include <ctype.h>
#include <ip.h>
#include <ndb.h>
#include <mp.h>
#include <libsec.h>
#include <auth.h>
#include "../smtp/y.tab.h"

char	*me;
char	*him="";
char	*dom;
process	*pp;
String	*mailer;
NetConnInfo *nci;

int	filterstate = ACCEPT;
int	trusted;
int	logged;
int	rejectcount;
int	hardreject;

Biobuf	bin;

int	debug;
int	Dflag;
int	Eflag;
int	eflag;
int	fflag;
int	gflag;
int	qflag;
int	rflag;
int	sflag;
int	authenticate;
int	authenticated;
int	passwordinclear;
char	*tlscert;

uchar	rsysip[IPaddrlen];

List	senders;
List	rcvers;

char	pipbuf[ERRMAX];
char	*piperror;

String*	mailerpath(char*);
int	pipemsg(int*);
int	rejectcheck(void);
String*	startcmd(void);

static int
catchalarm(void*, char *msg)
{
	int ign;

	ign = strstr(msg, "closed pipe") != nil;
	if(ign)
		return 0;
	if(pp)
		syskill(pp->pid);
	return strstr(msg, "alarm") != nil;
}

/* override string error functions to do something reasonable */
void
s_error(char *f, char *status)
{
	char errbuf[ERRMAX];

	errbuf[0] = 0;
	rerrstr(errbuf, sizeof(errbuf));
	if(f && *f)
		reply("452 4.3.0 out of memory %s: %s\r\n", f, errbuf);
	else
		reply("452 4.3.0 out of memory %s\r\n", errbuf);
	syslog(0, "smtpd", "++Malloc failure %s [%s]", him, nci->rsys);
	exits(status);
}

static void
usage(void)
{
	fprint(2, "usage: smtpd [-DEadefghpqrs] [-c cert] [-k ip] [-m mailer] [-n net]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *netdir;
	char buf[1024];

	netdir = nil;
	quotefmtinstall();
	fmtinstall('I', eipfmt);
	fmtinstall('[', encodefmt);
	ARGBEGIN{
	case 'a':
		authenticate = 1;
		break;
	case 'c':
		tlscert = EARGF(usage());
		break;
	case 'D':
		Dflag++;
		break;
	case 'd':
		debug++;
		break;
	case 'E':
		Eflag = 1;
		break;			/* if you fail extra helo checks, you must authenticate */
	case 'e':
		eflag = 1;		/* disable extra helo checks */
		break;
	case 'f':				/* disallow relaying */
		fflag = 1;
		break;
	case 'g':
		gflag = 1;
		break;
	case 'h':				/* default domain name */
		dom = EARGF(usage());
		break;
	case 'k':				/* prohibited ip address */
		addbadguy(EARGF(usage()));
		break;
	case 'm':				/* set mail command */
		mailer = mailerpath(EARGF(usage()));
		break;
	case 'n':				/* log peer ip address */
		netdir = EARGF(usage());
		break;
	case 'p':
		passwordinclear = 1;
		break;
	case 'q':
		qflag = 1;		/* don't log invalid hello */
		break;
	case 'r':
		rflag = 1;			/* verify sender's domain */
		break;
	case 's':				/* save blocked messages */
		sflag = 1;
		break;
	default:
		usage();
	}ARGEND;

	nci = getnetconninfo(netdir, 0);
	if(nci == nil)
		sysfatal("can't get remote system's address");
	parseip(rsysip, nci->rsys);

	if(mailer == nil)
		mailer = mailerpath("send");

	if(debug){
		close(2);
		snprint(buf, sizeof(buf), "%s/smtpd.db", UPASLOG);
		if (open(buf, OWRITE) >= 0) {
			seek(2, 0, 2);
			fprint(2, "%d smtpd %s\n", getpid(), thedate());
		} else
			debug = 0;
	}
	getconf();
	if(isbadguy())
		exits("");
	Binit(&bin, 0, OREAD);

	if (chdir(UPASLOG) < 0)
		syslog(0, "smtpd", "no %s: %r", UPASLOG);
	me = sysname_read();
	if(dom == 0 || dom[0] == 0)
		dom = domainname_read();
	if(dom == 0 || dom[0] == 0)
		dom = me;
	sayhi();
	parseinit();

	/* allow 45 minutes to parse the header */
	atnotify(catchalarm, 1);
	alarm(45*60*1000);
	zzparse();
	exits(0);
}

void
listfree(List *l)
{
	Link *lp, *next;

	for(lp = l->first; lp; lp = next){
		next = lp->next;
		s_free(lp->p);
		free(lp);
	}
	l->first = l->last = 0;
}

void
listadd(List *l, String *path)
{
	Link *lp;

	lp = (Link *)malloc(sizeof *lp);
	lp->p = path;
	lp->next = 0;

	if(l->last)
		l->last->next = lp;
	else
		l->first = lp;
	l->last = lp;
}

int
reply(char *fmt, ...)
{
	char buf[4096], *out;
	int n;
	va_list arg;

	va_start(arg, fmt);
	out = vseprint(buf, buf + 4096, fmt, arg);
	va_end(arg);

	n = out - buf;
	if(debug) {
		seek(2, 0, 2);
		write(2, buf, n);
	}
	write(1, buf, n);
	return n;
}

void
reset(void)
{
	if(rejectcheck())
		return;
	listfree(&rcvers);
	listfree(&senders);
	if(filterstate != DIALUP){
		logged = 0;
		filterstate = ACCEPT;
	}
	reply("250 2.0.0 ok\r\n");
}

void
sayhi(void)
{
	reply("220 %s ESMTP\r\n", dom);
}

Ndbtuple*
rquery(char *d)
{
	Ndbtuple *t, *p;

	t = dnsquery(nci->root, nci->rsys, "ptr");
	for(p = t; p != nil; p = p->entry)
		if(strcmp(p->attr, "dom") == 0
		&& strcmp(p->val, d) == 0){
			syslog(0, "smtpd", "ptr only from %s as %s",
				nci->rsys, d);
			return t;
		}
	ndbfree(t);
	return nil;
}

int
dnsexists(char *d)
{
	int r;
	Ndbtuple *t;

	r = -1;
	if((t = dnsquery(nci->root, d, "any")) != nil || (t = rquery(d)) != nil)
		r = 0;
	ndbfree(t);
	return r;
}

/*
 * make callers from class A networks infested by spammers
 * wait longer.
 */

static char netaspam[256] = {
	[58]	1,
	[66]	1,
	[71]	1,

	[76]	1,
	[77]	1,
	[78]	1,
	[79]	1,
	[80]	1,
	[81]	1,
	[82]	1,
	[83]	1,
	[84]	1,
	[85]	1,
	[86]	1,
	[87]	1,
	[88]	1,
	[89]	1,

	[190]	1,
	[201]	1,
	[217]	1,
};

static int
delaysecs(void)
{
	if (netaspam[rsysip[0]])
		return 60;
	return 15;
}

static char *badtld[] = {
	"localdomain",
	"localhost",
	"local",
	"example",
	"invalid",
	"lan",
	"test",
};

static char *bad2ld[] = {
	"example.com",
	"example.net",
	"example.org"
};

int
badname(void)
{
	char *p;

	/*
	 * similarly, if the claimed domain is not an address-literal,
	 * require at least one letter, which there will be in
	 * at least the last component (e.g., .com, .net) if it's real.
	 * this rejects non-address-literal IP addresses,
	 * among other bogosities.
	 */
	for (p = him; *p; p++)
		if(isascii(*p) && isalpha(*p))
			return 0;
	return -1;
}

int
ckhello(void)
{
	char *ldot, *rdot;
	int i;

	/*
	 * it is unacceptable to claim any string that doesn't look like
	 * a domain name (e.g., has at least one dot in it), but
	 * Microsoft mail client software gets this wrong, so let trusted
	 * (local) clients omit the dot.
	 */
	rdot = strrchr(him, '.');
	if(rdot && rdot[1] == '\0') {
		*rdot = '\0';			/* clobber trailing dot */
		rdot = strrchr(him, '.');	/* try again */
	}
	if(rdot == nil)
		return -1;
	/*
	 * Reject obviously bogus domains and those reserved by RFC 2606.
	 */
	if(rdot == nil)
		rdot = him;
	else
		rdot++;
	for(i = 0; i < nelem(badtld); i++)
		if(!cistrcmp(rdot, badtld[i]))
			return -1;
	/* check second-level RFC 2606 domains: example\.(com|net|org) */
	if(rdot != him)
		*--rdot = '\0';
	ldot = strrchr(him, '.');
	if(rdot != him)
		*rdot = '.';
	if(ldot == nil)
		ldot = him;
	else
		ldot++;
	for(i = 0; i < nelem(bad2ld); i++)
		if(!cistrcmp(ldot, bad2ld[i]))
			return -1;
	if(badname() == -1)
		return -1;
	if(dnsexists(him) == -1)
		return -1;
	return 0;
}

int
heloclaims(void)
{
	char **s;

	/*
	 * We don't care if he lies about who he is, but it is
	 * not okay to pretend to be us.  Many viruses do this,
	 * just parroting back what we say in the greeting.
	 */
	if(strcmp(nci->rsys, nci->lsys) == 0)
		return 0;
	if(strcmp(him, dom) == 0)
		return -1;
	for(s = sysnames_read(); s && *s; s++)
		if(cistrcmp(*s, him) == 0)
			return -1;
	if(him[0] != '[' && badname() == -1)
		return -1;

	return 0;
}

void
hello(String *himp, int extended)
{
	int ck;

	him = s_to_c(himp);
	if(!qflag)
		syslog(0, "smtpd", "%s from %s as %s", extended? "ehlo": "helo",
			nci->rsys, him);
	if(rejectcheck())
		return;

	ck = -1;
	if(!trusted && nci)
	if(heloclaims() || (!eflag && (ck = ckhello())))
	if(ck && Eflag){
		reply("250-you lie.  authentication required.\r\n");
		authenticate = 1;
	}else{
		if(Dflag)
			sleep(delaysecs()*1000);
		if(!qflag)
			syslog(0, "smtpd", "Hung up on %s; claimed to be %s",
				nci->rsys, him);
		rejectcount++;
		reply("554 5.7.0 Liar!\r\n");
		exits("client pretended to be us");
		return;
	}

	if(strchr(him, '.') == 0 && nci != nil && strchr(nci->rsys, '.') != nil)
		him = nci->rsys;

	if(qflag)
		syslog(0, "smtpd", "%s from %s as %s", extended? "ehlo": "helo",
			nci->rsys, him);
	if(Dflag)
		sleep(delaysecs()*1000);
	reply("250%c%s you are %s\r\n", extended ? '-' : ' ', dom, him);
	if (extended) {
		reply("250-ENHANCEDSTATUSCODES\r\n");	/* RFCs 2034 and 3463 */
		if(tlscert != nil)
			reply("250-STARTTLS\r\n");
		if (passwordinclear)
			reply("250 AUTH CRAM-MD5 PLAIN LOGIN\r\n");
		else
			reply("250 AUTH CRAM-MD5\r\n");
	}
}

void
sender(String *path)
{
	String *s;

	if(rejectcheck())
		return;
	if (authenticate && !authenticated) {
		rejectcount++;
		reply("530 5.7.0 Authentication required\r\n");
		return;
	}
	if(him == 0 || *him == 0){
		rejectcount++;
		reply("503 Start by saying HELO, please.\r\n", s_to_c(path));
		return;
	}

	/* don't add the domain onto black holes or we will loop */
	if(strchr(s_to_c(path), '!') == 0 && strcmp(s_to_c(path), "/dev/null") != 0){
		s = s_new();
		s_append(s, him);
		s_append(s, "!");
		s_append(s, s_to_c(path));
		s_terminate(s);
		s_free(path);
		path = s;
	}
	if(shellchars(s_to_c(path))){
		rejectcount++;
		reply("501 5.1.3 Bad character in sender address %s.\r\n",
			s_to_c(path));
		return;
	}

	/*
	 * see if this ip address, domain name, user name or account is blocked
	 */
	filterstate = blocked(path);

	logged = 0;
	listadd(&senders, path);
	reply("250 2.0.0 sender is %s\r\n", s_to_c(path));
}

enum { Rcpt, Domain, Ntoks };

typedef struct Sender Sender;
struct Sender {
	Sender	*next;
	char	*rcpt;
	char	*domain;
};
static Sender *sendlist, *sendlast;

static int
rdsenders(void)
{
	int lnlen, nf, ok = 1;
	char *line, *senderfile;
	char *toks[Ntoks];
	Biobuf *sf;
	Sender *snd;
	static int beenhere = 0;

	if (beenhere)
		return 1;
	beenhere = 1;

	/*
	 * we're sticking with a system-wide sender list because
	 * per-user lists would require fully resolving recipient
	 * addresses to determine which users they correspond to
	 * (barring exploiting syntactic conventions).
	 */
	senderfile = smprint("%s/senders", UPASLIB);
	sf = Bopen(senderfile, OREAD);
	free(senderfile);
	if (sf == nil)
		return 1;
	while ((line = Brdline(sf, '\n')) != nil) {
		if (line[0] == '#' || line[0] == '\n')
			continue;
		lnlen = Blinelen(sf);
		line[lnlen-1] = '\0';		/* clobber newline */
		nf = tokenize(line, toks, nelem(toks));
		if (nf != nelem(toks))
			continue;		/* malformed line */

		snd = malloc(sizeof *snd);
		if (snd == nil)
			sysfatal("out of memory: %r");
		memset(snd, 0, sizeof *snd);
		snd->next = nil;

		if (sendlast == nil)
			sendlist = snd;
		else
			sendlast->next = snd;
		sendlast = snd;
		snd->rcpt = strdup(toks[Rcpt]);
		snd->domain = strdup(toks[Domain]);
	}
	Bterm(sf);
	return ok;
}

/*
 * read (recipient, sender's DNS) pairs from /mail/lib/senders.
 * Only allow mail to recipient from any of sender's IPs.
 * A recipient not mentioned in the file is always permitted.
 */
static int
senderok(char *rcpt)
{
	int mentioned = 0, matched = 0;
	uchar dnsip[IPaddrlen];
	Sender *snd;
	Ndbtuple *nt, *next, *first;

	rdsenders();
	for (snd = sendlist; snd != nil; snd = snd->next) {
		if (strcmp(rcpt, snd->rcpt) != 0)
			continue;
		/*
		 * see if this domain's ips match nci->rsys.
		 * if not, perhaps a later entry's domain will.
		 */
		mentioned = 1;
		if (parseip(dnsip, snd->domain) != -1 &&
		    memcmp(rsysip, dnsip, IPaddrlen) == 0)
			return 1;
		/*
		 * NB: nt->line links form a circular list(!).
		 * we need to make one complete pass over it to free it all.
		 */
		first = nt = dnsquery(nci->root, snd->domain, "ip");
		if (first == nil)
			continue;
		do {
			if (strcmp(nt->attr, "ip") == 0 &&
			    parseip(dnsip, nt->val) != -1 &&
			    memcmp(rsysip, dnsip, IPaddrlen) == 0)
				matched = 1;
			next = nt->line;
			free(nt);
			nt = next;
		} while (nt != first);
	}
	if (matched)
		return 1;
	else
		return !mentioned;
}

void
receiver(String *path)
{
	char *sender, *rcpt;

	if(rejectcheck())
		return;
	if(him == 0 || *him == 0){
		rejectcount++;
		reply("503 Start by saying HELO, please\r\n");
		return;
	}
	if(senders.last)
		sender = s_to_c(senders.last->p);
	else
		sender = "<unknown>";

	if(!recipok(s_to_c(path))){
		rejectcount++;
		syslog(0, "smtpd",
		 "Disallowed %s (%s/%s) to blocked name %s",
			sender, him, nci->rsys, s_to_c(path));
		reply("550 5.1.1 %s ... user unknown\r\n", s_to_c(path));
		return;
	}
	rcpt = s_to_c(path);
	if (!senderok(rcpt)) {
		rejectcount++;
		syslog(0, "smtpd", "Disallowed sending IP of %s (%s/%s) to %s",
			sender, him, nci->rsys, rcpt);
		reply("550 5.7.1 %s ... sending system not allowed\r\n", rcpt);
		return;
	}

	logged = 0;

	/* forwarding() can modify 'path' on loopback request */
	if(filterstate == ACCEPT && fflag && !authenticated && forwarding(path)) {
		rejectcount++;
		syslog(0, "smtpd", "Bad Forward %s (%s/%s) (%s)",
			sender, him, nci->rsys, rcpt);
		reply("550 5.7.1 we don't relay.  send to your-path@[] for "
			"loopback.\r\n");
		return;
	}
	listadd(&rcvers, path);
	reply("250 2.0.0 receiver is %s\r\n", s_to_c(path));
}

void
quit(void)
{
	reply("221 2.0.0 Successful termination\r\n");
	close(0);
	exits(0);
}

void
noop(void)
{
	if(rejectcheck())
		return;
	reply("250 2.0.0 Nothing to see here. Move along ...\r\n");
}

void
help(String *cmd)
{
	if(rejectcheck())
		return;
	if(cmd)
		s_free(cmd);
	reply("250 2.0.0 See http://www.ietf.org/rfc/rfc2821\r\n");
}

void
verify(String *path)
{
	char *p, *q;
	char *av[4];
	static uint nverify;

	if(rejectcheck())
		return;
	if(nverify++ >= 2)
		sleep(1000 * (4 << nverify - 2));
	if(shellchars(s_to_c(path))){
		rejectcount++;
		reply("503 5.1.3 Bad character in address %s.\r\n", s_to_c(path));
		return;
	}
	av[0] = s_to_c(mailer);
	av[1] = "-x";
	av[2] = s_to_c(path);
	av[3] = 0;

	pp = noshell_proc_start(av, 0, outstream(), 0, 1, 0);
	if (pp == 0) {
		reply("450 4.3.2 We're busy right now, try later\r\n");
		return;
	}

	p = Brdline(pp->std[1]->fp, '\n');
	if(p == 0){
		reply("550 5.1.0 String does not match anything.\r\n");
	} else {
		p[Blinelen(pp->std[1]->fp) - 1] = 0;
		if(strchr(p, ':'))
			reply("550 5.1.0  String does not match anything.\r\n");
		else{
			q = strrchr(p, '!');
			if(q)
				p = q + 1;
			reply("250 2.0.0 %s <%s@%s>\r\n", s_to_c(path), p, dom);
		}
	}
	proc_wait(pp);
	proc_free(pp);
	pp = 0;
}

/*
 *  get a line that ends in crnl or cr, turn terminating crnl into a nl
 *
 *  return 0 on EOF
 */
static int
getcrnl(String *s, Biobuf *fp)
{
	int c;

	for(;;){
		c = Bgetc(fp);
		if(debug) {
			seek(2, 0, 2);
			fprint(2, "%c", c);
		}
		switch(c){
		case 0:
			/* idiot html email! */
			break;
		case -1:
			goto out;
		case '\r':
			c = Bgetc(fp);
			if(c == '\n'){
				if(debug) {
					seek(2, 0, 2);
					fprint(2, "%c", c);
				}
				s_putc(s, '\n');
				goto out;
			}
			Bungetc(fp);
			s_putc(s, '\r');
			break;
		case '\n':
			s_putc(s, c);
			goto out;
		default:
			s_putc(s, c);
			break;
		}
	}
out:
	s_terminate(s);
	return s_len(s);
}

void
logcall(int nbytes)
{
	Link *l;
	String *to, *from;

	to = s_new();
	from = s_new();
	for(l = senders.first; l; l = l->next){
		if(l != senders.first)
			s_append(from, ", ");
		s_append(from, s_to_c(l->p));
	}
	for(l = rcvers.first; l; l = l->next){
		if(l != rcvers.first)
			s_append(to, ", ");
		s_append(to, s_to_c(l->p));
	}
	syslog(0, "smtpd", "[%s/%s] %s sent %d bytes to %s", him, nci->rsys,
		s_to_c(from), nbytes, s_to_c(to));
	s_free(to);
	s_free(from);
}

static void
logmsg(char *action)
{
	Link *l;

	if(logged)
		return;

	logged = 1;
	for(l = rcvers.first; l; l = l->next)
		syslog(0, "smtpd", "%s %s (%s/%s) (%s)", action,
			s_to_c(senders.last->p), him, nci->rsys, s_to_c(l->p));
}

static int
optoutall(int filterstate)
{
	Link *l;

	switch(filterstate){
	case ACCEPT:
	case TRUSTED:
		return filterstate;
	}

	for(l = rcvers.first; l; l = l->next)
		if(!optoutofspamfilter(s_to_c(l->p)))
			return filterstate;

	return ACCEPT;
}

String*
startcmd(void)
{
	int n;
	char *filename;
	char **av;
	Link *l;
	String *cmd;

	/*
	 *  ignore the filterstate if the all the receivers prefer it.
	 */
	filterstate = optoutall(filterstate);

	switch (filterstate){
	case BLOCKED:
	case DELAY:
		rejectcount++;
		logmsg("Blocked");
		filename = dumpfile(s_to_c(senders.last->p));
		cmd = s_new();
		s_append(cmd, "cat > ");
		s_append(cmd, filename);
		pp = proc_start(s_to_c(cmd), instream(), 0, outstream(), 0, 0);
		break;
	case DIALUP:
		logmsg("Dialup");
		rejectcount++;
		reply("554 5.7.1 We don't accept mail from dial-up ports.\r\n");
		/*
		 * we could exit here, because we're never going to accept mail
		 * from this ip address, but it's unclear that RFC821 allows
		 * that.  Instead we set the hardreject flag and go stupid.
		 */
		hardreject = 1;
		return 0;
	case DENIED:
		logmsg("Denied");
		rejectcount++;
		reply("554-5.7.1 We don't accept mail from %s.\r\n",
			s_to_c(senders.last->p));
		reply("554 5.7.1 Contact postmaster@%s for more information.\r\n",
			dom);
		return 0;
	case REFUSED:
		logmsg("Refused");
		rejectcount++;
		reply("554 5.7.1 Sender domain must exist: %s\r\n",
			s_to_c(senders.last->p));
		return 0;
	default:
	case NONE:
		logmsg("Confused");
		rejectcount++;
		reply("554-5.7.0 We have had an internal mailer error "
			"classifying your message.\r\n");
		reply("554-5.7.0 Filterstate is %d\r\n", filterstate);
		reply("554 5.7.0 Contact postmaster@%s for more information.\r\n",
			dom);
		return 0;
	case ACCEPT:
	case TRUSTED:
		/*
		 * now that all other filters have been passed,
		 * do grey-list processing.
		 */
		if(gflag)
			vfysenderhostok();

		/*
		 *  set up mail command
		 */
		cmd = s_clone(mailer);
		n = 3;
		for(l = rcvers.first; l; l = l->next)
			n++;
		av = malloc(n * sizeof(char*));
		if(av == nil){
			reply("450 4.3.2 We're busy right now, try later\r\n");
			s_free(cmd);
			return 0;
		}

		n = 0;
		av[n++] = s_to_c(cmd);
		av[n++] = "-r";
		for(l = rcvers.first; l; l = l->next)
			av[n++] = s_to_c(l->p);
		av[n] = 0;
		/*
		 *  start mail process
		 */
		pp = noshell_proc_start(av, instream(), outstream(),
			outstream(), 0, 0);
		free(av);
		break;
	}
	if(pp == 0) {
		reply("450 4.3.2 We're busy right now, try later\r\n");
		s_free(cmd);
		return 0;
	}
	return cmd;
}

/*
 *  print out a header line, expanding any domainless addresses into
 *  address@him
 */
char*
bprintnode(Biobuf *b, Node *p, int *nbytes)
{
	int n, m;

	if(p->s){
		if(p->addr && strchr(s_to_c(p->s), '@') == nil)
			n = Bprint(b, "%s@%s", s_to_c(p->s), him);
		else
			n = Bwrite(b, s_to_c(p->s), s_len(p->s));
	}else
		n = Bputc(b, p->c) == -1? -1: 1;
	m = 0;
	if(n != -1 && p->white)
		m = Bwrite(b, s_to_c(p->white), s_len(p->white));
	if(n == -1 || m == -1)
		return nil;
	*nbytes += n + m;
	return p->end + 1;
}

static String*
getaddr(Node *p)
{
	for(; p; p = p->next)
		if(p->s && p->addr)
			return p->s;
	return nil;
}

/*
 *  add warning headers of the form
 *	X-warning: <reason>
 *  for any headers that looked like they might be forged.
 *
 *  return byte count of new headers
 */
static int
forgedheaderwarnings(void)
{
	int nbytes;
	Field *f;

	nbytes = 0;

	/* warn about envelope sender */
	if(strcmp(s_to_c(senders.last->p), "/dev/null") != 0 &&
	    masquerade(senders.last->p, nil))
		nbytes += Bprint(pp->std[0]->fp,
			"X-warning: suspect envelope domain\n");

	/*
	 *  check Sender: field.  If it's OK, ignore the others because this
	 *  is an exploded mailing list.
	 */
	for(f = firstfield; f; f = f->next)
		if(f->node->c == SENDER)
			if(masquerade(getaddr(f->node), him))
				nbytes += Bprint(pp->std[0]->fp,
					"X-warning: suspect Sender: domain\n");
			else
				return nbytes;

	/* check From: */
	for(f = firstfield; f; f = f->next){
		if(f->node->c == FROM && masquerade(getaddr(f->node), him))
			nbytes += Bprint(pp->std[0]->fp,
				"X-warning: suspect From: domain\n");
	}
	return nbytes;
}

static int
parseheader(String *hdr, int *nbytesp, int *status)
{
	char *cp;
	int nbytes, n;
	Field *f;
	Link *l;
	Node *p;

	nbytes = *nbytesp;
	yyinit(s_to_c(hdr), s_len(hdr));
	yyparse();

	/*
	 *  Look for masquerades.  Let Sender: trump From: to allow mailing list
	 *  forwarded messages.
	 */
	if(fflag)
		nbytes += forgedheaderwarnings();

	/*
	 *  add an orginator and/or destination if either is missing
	 */
	if(originator == 0){
		if(senders.last == nil)
			nbytes += Bprint(pp->std[0]->fp, "From: /dev/null@%s\n", him);
		else
			nbytes += Bprint(pp->std[0]->fp, "From: %s\n",
				s_to_c(senders.last->p));
	}
	if(destination == 0){
		nbytes += Bprint(pp->std[0]->fp, "To: ");
		for(l = rcvers.first; l; l = l->next){
			if(l != rcvers.first)
				nbytes += Bprint(pp->std[0]->fp, ", ");
			nbytes += Bprint(pp->std[0]->fp, "%s", s_to_c(l->p));
		}
		nbytes += Bprint(pp->std[0]->fp, "\n");
	}

	/*
	 *  add sender's domain to any domainless addresses
	 *  (to avoid forging local addresses)
	 */
	cp = s_to_c(hdr);
	for(f = firstfield; cp != nil && f; f = f->next){
		for(p = f->node; cp != 0 && p; p = p->next)
			cp = bprintnode(pp->std[0]->fp, p, &nbytes);
		if(*status == 0 && Bprint(pp->std[0]->fp, "\n") < 0){
			piperror = "write error";
			*status = 1;
		}
		nbytes++;
	}
	if(cp == nil){
		piperror = "sender domain";
		*status = 1;
	}
	/* write anything we read following the header */
	if(*status == 0){
		n = Bwrite(pp->std[0]->fp, cp, s_to_c(hdr) + s_len(hdr) - cp);
		if(n == -1){
			piperror = "write error 2";
			*status = 1;
		}
		nbytes += n;
	}

	*nbytesp = nbytes;
	return *status;
}

static int
chkhdr(char *s, int n)
{
	int i;
	Rune r;

	for(i = 0; i < n; ){
		if(!fullrune(s + i, n - i))
			return -1;
		i += chartorune(&r, s + i);
		if(r == Runeerror)
			return -1;
	}
	return 0;
}

static void
fancymsg(int status)
{
	static char msg[2*ERRMAX], *p, *e;

	if(!status)
		return;

	p = seprint(msg, msg+ERRMAX, "%s: ", piperror);
	rerrstr(p, ERRMAX);
	piperror = msg;
}

/*
 *  pipe message to mailer with the following transformations:
 *	- change \r\n into \n.
 *	- add sender's domain to any addrs with no domain
 *	- add a From: if none of From:, Sender:, or Replyto: exists
 *	- add a Received: line
 *	- elide leading dot
 */
int
pipemsg(int *byteswritten)
{
	char *cp;
	int n, nbytes, sawdot, status;
	String *hdr, *line;

	pipesig(&status);	/* set status to 1 on write to closed pipe */
	sawdot = 0;
	status = 0;
	werrstr("");
	piperror = nil;

	/*
	 *  add a 'From ' line as envelope and Received: stamp
	 */
	nbytes = 0;
	nbytes += Bprint(pp->std[0]->fp, "From %s %s remote from \n",
		s_to_c(senders.first->p), thedate());
	nbytes += Bprint(pp->std[0]->fp, "Received: from %s ", him);
	if(nci->rsys)
		nbytes += Bprint(pp->std[0]->fp, "([%s]) ", nci->rsys);
	nbytes += Bprint(pp->std[0]->fp, "by %s; %s\n", me, thedate());

	/*
	 *  read first 16k obeying '.' escape.  we're assuming
	 *  the header will all be there.
	 */
	line = s_new();
	hdr = s_new();
	while(s_len(hdr) < 16*1024){
		n = getcrnl(s_reset(line), &bin);

		/* eof or error ends the message */
		if(n <= 0){
			piperror = "header read error";
			status = 1;
			break;
		}

		cp = s_to_c(line);
		if(chkhdr(cp, s_len(line)) == -1){
			status = 1;
			piperror = "mail refused: illegal header chars";
			break;
		}

		/* a line with only a '.' ends the message */
		if(cp[0] == '.' && cp[1] == '\n'){
			sawdot = 1;
			break;
		}
		if(cp[0] == '.'){
			cp++;
			n--;
		}
		s_append(hdr, cp);
		nbytes += n;
		if(*cp == '\n')
			break;
	}
	if(status == 0)
		parseheader(hdr, &nbytes, &status);
	s_free(hdr);

	/*
	 *  pass rest of message to mailer.  take care of '.'
	 *  escapes.
	 */
	for(;;){
		n = getcrnl(s_reset(line), &bin);

		/* eof or error ends the message */
		if(n < 0){
			piperror = "body read error";
			status = 1;
		}
		if(n <= 0)
			break;

		/* a line with only a '.' ends the message */
		cp = s_to_c(line);
		if(cp[0] == '.' && cp[1] == '\n'){
			sawdot = 1;
			break;
		}
		if(cp[0] == '.'){
			cp++;
			n--;
		}
		nbytes += n;
		if(status == 0 && Bwrite(pp->std[0]->fp, cp, n) < 0){
			piperror = "write error 3";
			status = 1;
			break;
		}
	}
	s_free(line);
	if(status == 0 && sawdot == 0){
		/* message did not terminate normally */
		snprint(pipbuf, sizeof pipbuf, "network eof no dot: %r");
		piperror = pipbuf;
		status = 1;
	}
	if(status == 0 && Bflush(pp->std[0]->fp) < 0){
		piperror = "write error 4";
		status = 1;
	}
	if(status != 0)
		syskill(pp->pid);
	stream_free(pp->std[0]);
	pp->std[0] = 0;
	*byteswritten = nbytes;
	pipesigoff();
	if(status && piperror == nil)
		piperror = "write on closed pipe";
	fancymsg(status);
	return status;
}

char*
firstline(char *x)
{
	char *p;
	static char buf[128];

	strncpy(buf, x, sizeof buf);
	buf[sizeof buf - 1] = 0;
	p = strchr(buf, '\n');
	if(p)
		*p = 0;
	return buf;
}

int
sendermxcheck(void)
{
	int pid;
	char *cp, *senddom, *user, *who;
	Waitmsg *w;

	senddom = 0;
	who = s_to_c(senders.first->p);
	if(strcmp(who, "/dev/null") == 0){
		/* /dev/null can only send to one rcpt at a time */
		if(rcvers.first != rcvers.last){
			werrstr("rejected: /dev/null sending to multiple "
				"recipients");
			return -1;
		}
		/* 4408 spf §2.2 notes that 2821 says /dev/null == postmaster@domain */
		senddom = smprint("%s!postmaster", him);
	}

	if(access("/mail/lib/validatesender", AEXEC) < 0)
		return 0;
	if(!senddom)
		senddom = strdup(who);
	if((cp = strchr(senddom, '!')) == nil){
		werrstr("rejected: domainless sender %s", who);
		free(senddom);
		return -1;
	}
	*cp++ = 0;
	user = cp;
	/* shellchars isn't restrictive.  should probablly disallow specialchars */
	if(shellchars(senddom) || shellchars(user) || shellchars(him)){
		werrstr("rejected: evil sender/domain/helo");
		free(senddom);
		return -1;
	}

	switch(pid = fork()){
	case -1:
		werrstr("deferred: fork: %r");
		return -1;
	case 0:
		/*
		 * Could add an option with the remote IP address
		 * to allow validatesender to implement SPF eventually.
		 */
		execl("/mail/lib/validatesender", "validatesender",
			"-n", nci->root, senddom, user, nci->rsys, him, nil);
		_exits("exec validatesender: %r");
	default:
		break;
	}

	free(senddom);
	w = wait();
	if(w == nil){
		werrstr("deferred: wait failed: %r");
		return -1;
	}
	if(w->pid != pid){
		werrstr("deferred: wait returned wrong pid %d != %d",
			w->pid, pid);
		free(w);
		return -1;
	}
	if(w->msg[0] == 0){
		free(w);
		return 0;
	}
	/*
	 * skip over validatesender 143123132: prefix from rc.
	 */
	cp = strchr(w->msg, ':');
	if(cp && cp[1] == ' ')
		werrstr("%s", cp + 2);
	else
		werrstr("%s", w->msg);
	free(w);
	return -1;
}

int
refused(char *e)
{
	return e && strstr(e, "mail refused") != nil;
}

/*
 * if a message appeared on stderr, despite good status,
 * log it.  this can happen if rewrite.in contains a bad
 * r.e., for example.
 */
void
logerrors(String *err)
{
	char *s;

	s = s_to_c(err);
	if(*s == 0)
		return;
	syslog(0, "smtpd", "%s returned good status, but said: %s",
		s_to_c(mailer), s);
}

void
data(void)
{
	char *cp, *ep, *e, buf[ERRMAX];
	int status, nbytes;
	Link *l;
	String *cmd, *err;

	if(rejectcheck())
		return;
	if(senders.last == 0){
		reply("503 2.5.2 Data without MAIL FROM:\r\n");
		rejectcount++;
		return;
	}
	if(rcvers.last == 0){
		reply("503 2.5.2 Data without RCPT TO:\r\n");
		rejectcount++;
		return;
	}
	if(!trusted && sendermxcheck()){
		rerrstr(buf, sizeof buf);
		if(strncmp(buf, "rejected:", 9) == 0)
			reply("554 5.7.1 %s\r\n", buf);
		else
			reply("450 4.7.0 %s\r\n", buf);
		for(l=rcvers.first; l; l=l->next)
			syslog(0, "smtpd", "[%s/%s] %s -> %s sendercheck: %s",
				him, nci->rsys, s_to_c(senders.first->p),
				s_to_c(l->p), buf);
		rejectcount++;
		return;
	}

	/*
	 *  allow 145 more minutes to move the data
	 */
	cmd = startcmd();
	if(cmd == 0)
		return;
	reply("354 Input message; end with <CRLF>.<CRLF>\r\n");
	alarm(145*60*1000);
	piperror = nil;
	status = pipemsg(&nbytes);
	err = s_new();
	while(s_read_line(pp->std[2]->fp, err))
		;
	alarm(0);
	atnotify(catchalarm, 0);

	status |= proc_wait(pp);
	if(debug){
		seek(2, 0, 2);
		fprint(2, "%d status %ux\n", getpid(), status);
		if(*s_to_c(err))
			fprint(2, "%d error %s\n", getpid(), s_to_c(err));
	}

	/*
	 *  if process terminated abnormally, send back error message
	 */
	if(status && (refused(piperror) || refused(s_to_c(err)))){
		filterstate = BLOCKED;
		status = 0;
	}
	if(status){
		buf[0] = 0;
		if(piperror != nil)
			snprint(buf, sizeof buf, "pipemesg: %s; ", piperror);
		syslog(0, "smtpd", "++[%s/%s] %s %s %sreturned %#q %s",
			him, nci->rsys, s_to_c(senders.first->p),
			s_to_c(cmd), buf,
			pp->waitmsg->msg, firstline(s_to_c(err)));
		for(cp = s_to_c(err); ep = strchr(cp, '\n'); cp = ep){
			*ep++ = 0;
			reply("450-4.0.0 %s\r\n", cp);
		}
		reply("450 4.0.0 mail process terminated abnormally\r\n");
		rejectcount++;
	} else {
		if(filterstate == BLOCKED){
			e = firstline(s_to_c(err));
			if(e[0] == 0)
				e = piperror;
			if(e == nil)
				e = "we believe this is spam.";
			syslog(0, "smtpd", "++[%s/%s] blocked: %s", him, nci->rsys, e);
			reply("554 5.7.1 %s\r\n", e);
			rejectcount++;
		}else if(filterstate == DELAY){
			logerrors(err);
			reply("450 4.3.0 There will be a delay in delivery "
				"of this message.\r\n");
		}else{
			logerrors(err);
			reply("250 2.5.0 sent\r\n");
			logcall(nbytes);
		}
	}
	proc_free(pp);
	pp = 0;
	s_free(cmd);
	s_free(err);

	listfree(&senders);
	listfree(&rcvers);
}

/*
 * when we have blocked a transaction based on IP address, there is nothing
 * that the sender can do to convince us to take the message.  after the
 * first rejection, some spammers continually RSET and give a new MAIL FROM:
 * filling our logs with rejections.  rejectcheck() limits the retries and
 * swiftly rejects all further commands after the first 500-series message
 * is issued.
 */
int
rejectcheck(void)
{
	if(rejectcount)
		sleep(1000 * (4<<rejectcount));
	if(rejectcount > MAXREJECTS){
		syslog(0, "smtpd", "Rejected (%s/%s)", him, nci->rsys);
		reply("554 5.5.0 too many errors.  transaction failed.\r\n");
		exits("errcount");
	}
	if(hardreject){
		rejectcount++;
		reply("554 5.7.1 We don't accept mail from dial-up ports.\r\n");
	}
	return hardreject;
}

/*
 *  create abs path of the mailer
 */
String*
mailerpath(char *p)
{
	String *s;

	if(p == nil)
		return nil;
	if(*p == '/')
		return s_copy(p);
	s = s_new();
	s_append(s, UPASBIN);
	s_append(s, "/");
	s_append(s, p);
	return s;
}

String *
s_dec64(String *sin)
{
	int lin, lout;
	String *sout;

	lin = s_len(sin);

	/*
	 * if the string is coming from smtpd.y, it will have no nl.
	 * if it is coming from getcrnl below, it will have an nl.
	 */
	if (*(s_to_c(sin) + lin - 1) == '\n')
		lin--;
	sout = s_newalloc(lin + 1);
	lout = dec64((uchar *)s_to_c(sout), lin, s_to_c(sin), lin);
	if (lout < 0) {
		s_free(sout);
		return nil;
	}
	sout->ptr = sout->base + lout;
	s_terminate(sout);
	return sout;
}

void
starttls(void)
{
	int certlen, fd;
	uchar *cert;
	TLSconn conn;

	if (tlscert == nil) {
		reply("500 5.5.1 illegal command or bad syntax\r\n");
		return;
	}
	cert = readcert(tlscert, &certlen);
	if (cert == nil) {
		reply("454 4.7.5 TLS not available\r\n");
		return;
	}
	reply("220 2.0.0 Go ahead make my day\r\n");
	memset(&conn, 0, sizeof(conn));
	conn.cert = cert;
	conn.certlen = certlen;
	fd = tlsServer(Bfildes(&bin), &conn);
	if (fd < 0) {
		syslog(0, "smtpd", "TLS start-up failed with %s", him);
		exits("tls failed");
	}
	Bterm(&bin);
	if (dup(fd, 0) < 0 || dup(fd, 1) < 0)
		fprint(2, "dup of %d failed: %r\n", fd);
	close(fd);
	Binit(&bin, 0, OREAD);
	free(conn.cert);
	free(conn.sessionID);
	passwordinclear = 1;
	syslog(0, "smtpd", "started TLS with %s", him);
}

int
passauth(char *u, char *secret)
{
	char response[2*MD5dlen + 1];
	uchar digest[MD5dlen];
	int i;
	AuthInfo *ai;
	Chalstate *cs;

	if((cs = auth_challenge("proto=cram role=server")) == nil)
		return -1;
	hmac_md5((uchar*)cs->chal, strlen(cs->chal),
		(uchar*)secret, strlen(secret), digest, nil);
	for(i = 0; i < MD5dlen; i++)
		snprint(response + 2*i, sizeof response - 2*i, "%2.2ux", digest[i]);
	cs->user = u;
	cs->resp = response;
	cs->nresp = strlen(response);
	ai = auth_response(cs);
	if(ai == nil)
		return -1;
	auth_freechal(cs);
	auth_freeAI(ai);
	return 0;
}

void
auth(String *mech, String *resp)
{
	char *user, *pass;
	AuthInfo *ai = nil;
	Chalstate *chs = nil;
	String *s_resp1_64 = nil, *s_resp2_64 = nil, *s_resp1 = nil;
	String *s_resp2 = nil;

	if(rejectcheck())
		goto bomb_out;

	syslog(0, "smtpd", "auth(%s, %s) from %s", s_to_c(mech),
		"(protected)", him);

	if(authenticated) {
	bad_sequence:
		rejectcount++;
		reply("503 5.5.2 Bad sequence of commands\r\n");
		goto bomb_out;
	}
	if(cistrcmp(s_to_c(mech), "plain") == 0){
		if (!passwordinclear) {
			rejectcount++;
			reply("538 5.7.1 Encryption required for requested "
				"authentication mechanism\r\n");
			goto bomb_out;
		}
		s_resp1_64 = resp;
		if (s_resp1_64 == nil) {
			reply("334 \r\n");
			s_resp1_64 = s_new();
			if (getcrnl(s_resp1_64, &bin) <= 0)
				goto bad_sequence;
		}
		s_resp1 = s_dec64(s_resp1_64);
		if (s_resp1 == nil) {
			rejectcount++;
			reply("501 5.5.4 Cannot decode base64\r\n");
			goto bomb_out;
		}
		memset(s_to_c(s_resp1_64), 'X', s_len(s_resp1_64));
		user = s_to_c(s_resp1) + strlen(s_to_c(s_resp1)) + 1;
		pass = user + strlen(user) + 1;
//		ai = auth_userpasswd(user, pass);
//		authenticated = ai != nil;
authenticated = passauth(user, pass) != -1;
		memset(pass, 'X', strlen(pass));
		goto windup;
	}
	else if(cistrcmp(s_to_c(mech), "login") == 0){
		if (!passwordinclear) {
			rejectcount++;
			reply("538 5.7.1 Encryption required for requested "
				"authentication mechanism\r\n");
			goto bomb_out;
		}
		if (resp == nil) {
			reply("334 VXNlcm5hbWU6\r\n");
			s_resp1_64 = s_new();
			if (getcrnl(s_resp1_64, &bin) <= 0)
				goto bad_sequence;
		}else
			s_resp1_64 = resp;
		reply("334 UGFzc3dvcmQ6\r\n");
		s_resp2_64 = s_new();
		if (getcrnl(s_resp2_64, &bin) <= 0)
			goto bad_sequence;
		s_resp1 = s_dec64(s_resp1_64);
		s_resp2 = s_dec64(s_resp2_64);
		memset(s_to_c(s_resp2_64), 'X', s_len(s_resp2_64));
		if (s_resp1 == nil || s_resp2 == nil) {
			rejectcount++;
			reply("501 5.5.4 Cannot decode base64\r\n");
			goto bomb_out;
		}
		ai = auth_userpasswd(s_to_c(s_resp1), s_to_c(s_resp2));
		authenticated = ai != nil;
		memset(s_to_c(s_resp2), 'X', s_len(s_resp2));
windup:
		if (authenticated) {
			/* if you authenticated, we trust you despite your IP */
			trusted = 1;
			reply("235 2.0.0 Authentication successful\r\n");
		} else {
			rejectcount++;
			reply("535 5.7.1 Authentication failed\r\n");
			syslog(0, "smtpd", "authentication failed: %r");
		}
		goto bomb_out;
	}
	else if(cistrcmp(s_to_c(mech), "cram-md5") == 0){
		char *resp, *t;

		chs = auth_challenge("proto=cram role=server");
		if (chs == nil) {
			rejectcount++;
			reply("501 5.7.5 Couldn't get CRAM-MD5 challenge\r\n");
			goto bomb_out;
		}
		reply("334 %.*[\r\n", chs->nchal, chs->chal);
		s_resp1_64 = s_new();
		if (getcrnl(s_resp1_64, &bin) <= 0)
			goto bad_sequence;
		s_resp1 = s_dec64(s_resp1_64);
		if (s_resp1 == nil) {
			rejectcount++;
			reply("501 5.5.4 Cannot decode base64\r\n");
			goto bomb_out;
		}
		/* should be of form <user><space><response> */
		resp = s_to_c(s_resp1);
		t = strchr(resp, ' ');
		if (t == nil) {
			rejectcount++;
			reply("501 5.5.4 Poorly formed CRAM-MD5 response\r\n");
			goto bomb_out;
		}
		*t++ = 0;
		chs->user = resp;
		chs->resp = t;
		chs->nresp = strlen(t);
		ai = auth_response(chs);
		authenticated = ai != nil;
		goto windup;
	}
	rejectcount++;
	reply("501 5.5.1 Unrecognised authentication type %s\r\n", s_to_c(mech));
bomb_out:
	if (ai)
		auth_freeAI(ai);
	if (chs)
		auth_freechal(chs);
	if (s_resp1)
		s_free(s_resp1);
	if (s_resp2)
		s_free(s_resp2);
	if (s_resp1_64)
		s_free(s_resp1_64);
	if (s_resp2_64)
		s_free(s_resp2_64);
}
