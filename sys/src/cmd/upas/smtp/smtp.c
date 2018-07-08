#include "common.h"
#include "smtp.h"
#include <ctype.h>
#include <mp.h>
#include <libsec.h>
#include <auth.h>

static	char*	connect(char*, Mx*);
static	char*	wraptls(void);
static	char*	dotls(char*);
static	char*	doauth(char*);

void	addhostdom(String*, char*);
String*	bangtoat(char*);
String*	convertheader(String*);
int	dBprint(char*, ...);
#pragma varargck argpos dBprint 1
int	dBputc(int);
char*	data(String*, Biobuf*, Mx*);
char*	domainify(char*, char*);
String*	fixrouteaddr(String*, Node*, Node*);
char*	getcrnl(String*);
int	getreply(void);
char*	hello(char*, int);
char*	mailfrom(char*);
int	printdate(Node*);
int	printheader(void);
void	putcrnl(char*, int);
void	quit(char*);
char*	rcptto(char*);
char	*rewritezone(char *);

#define Retry	"Retry, Temporary Failure"
#define Giveup	"Permanent Failure"

String	*reply;		/* last reply */
String	*toline;

int	alarmscale;
int	autistic;
int	debug;		/* true if we're debugging */
int	filter;
int	insecure;
int	last = 'n';	/* last character sent by putcrnl() */
int	ping;
int	quitting;	/* when error occurs in quit */
int	tryauth;	/* Try to authenticate, if supported */
int	trysecure;	/* Try to use TLS if the other side supports it */

char	*quitrv;	/* deferred return value when in quit */
char	ddomain[1024];	/* domain name of destination machine */
char	*gdomain;	/* domain name of gateway */
char	*uneaten;	/* first character after rfc822 headers */
char	*farend;	/* system we are trying to send to */
char	*user;		/* user we are authenticating as, if authenticating */
char	hostdomain[256];
Mx	*tmmx;		/* global for timeout */

Biobuf	bin;
Biobuf	bout;
Biobuf	berr;
Biobuf	bfile;

int
Dfmt(Fmt *fmt)
{
	Mx *mx;

	mx = va_arg(fmt->args, Mx*);
	if(mx == nil || mx->host[0] == 0)
		return fmtstrcpy(fmt, "");
	else
		return fmtprint(fmt, "(%s:%s)", mx->host, mx->ip);
}
#pragma	varargck	type	"D"	Mx*

char*
deliverytype(void)
{
	if(ping)
		return "ping";
	return "delivery";
}

void
usage(void)
{
	fprint(2, "usage: smtp [-aAdfipst] [-b busted-mx] [-g gw] [-h host] "
		"[-u user] [.domain] net!host[!service] sender rcpt-list\n");
	exits(Giveup);
}

int
timeout(void *, char *msg)
{
	syslog(0, "smtp.fail", "%s interrupt: %s: %s %D", deliverytype(), farend,  msg, tmmx);
	if(strstr(msg, "alarm")){
		fprint(2, "smtp timeout: connection to %s timed out\n", farend);
		if(quitting)
			exits(quitrv);
		exits(Retry);
	}
	if(strstr(msg, "closed pipe")){
		fprint(2, "smtp timeout: connection closed to %s\n", farend);
		if(quitting){
			syslog(0, "smtp.fail", "%s closed pipe to %s %D", deliverytype(), farend, tmmx);
			_exits(quitrv);
		}
		/* call _exits() to prevent Bio from trying to flush closed pipe */
		_exits(Retry);
	}
	return 0;
}

void
removenewline(char *p)
{
	int n = strlen(p) - 1;

	if(n < 0)
		return;
	if(p[n] == '\n')
		p[n] = 0;
}

void
main(int argc, char **argv)
{
	char *phase, *addr, *rv, *trv, *host, *domain;
	char **errs, *p, *e, hellodomain[256], allrx[512];
	int i, ok, rcvrs, bustedmx;
	String *from, *fromm, *sender;
	Mx mx;

	alarmscale = 60*1000;	/* minutes */
	quotefmtinstall();
	mailfmtinstall();		/* 2047 encoding */
	fmtinstall('D', Dfmt);
	fmtinstall('[', encodefmt);
	fmtinstall('H', encodefmt);
	errs = malloc(argc*sizeof(char*));
	reply = s_new();
	host = 0;
	bustedmx = 0;
	ARGBEGIN{
	case 'a':
		tryauth = 1;
		if(trysecure == 0)
			trysecure = 1;
		break;
	case 'A':	/* autistic: won't talk to us until we talk (Verizon) */
		autistic = 1;
		break;
	case 'b':
		if(bustedmx >= Maxbustedmx)
			sysfatal("more than %d busted mxs given", Maxbustedmx);
		bustedmxs[bustedmx++] = EARGF(usage());
		break;
	case 'd':
		debug = 1;
		break;
	case 'f':
		filter = 1;
		break;
	case 'g':
		gdomain = EARGF(usage());
		break;
	case 'h':
		host = EARGF(usage());
		break;
	case 'i':
		insecure = 1;
		break;
	case 'p':
		alarmscale = 10*1000;	/* tens of seconds */
		ping = 1;
		break;
	case 's':
		if(trysecure == 0)
			trysecure = 1;
		break;
	case 't':
		trysecure = 2;
		break;
	case 'u':
		user = EARGF(usage());
		break;
	default:
		usage();
		break;
	}ARGEND;

	Binit(&berr, 2, OWRITE);
	Binit(&bfile, 0, OREAD);

	/*
	 *  get domain and add to host name
	 */
	if(*argv && **argv=='.'){
		domain = *argv;
		argv++; argc--;
	} else
		domain = domainname_read();
	if(host == 0)
		host = sysname_read();
	if(user == nil)
		user = getuser();
	strcpy(hostdomain, domainify(host, domain));
	strcpy(hellodomain, domainify(sysname_read(), domain));

	/*
	 *  get destination address
	 */
	if(*argv == 0)
		usage();
	addr = *argv++; argc--;
	farend = addr;
	if((rv = strrchr(addr, '!')) && rv[1] == '['){
		syslog(0, "smtp.fail", "%s to %s failed: illegal address",
			deliverytype(), addr);
		exits(Giveup);
	}

	/*
	 *  get sender's machine.
	 *  get sender in internet style.  domainify if necessary.
	 */
	if(*argv == 0)
		usage();
	sender = unescapespecial(s_copy(*argv++));
	argc--;
	fromm = s_clone(sender);
	rv = strrchr(s_to_c(fromm), '!');
	if(rv)
		*rv = 0;
	else
		*s_to_c(fromm) = 0;
	from = bangtoat(s_to_c(sender));

	/*
	 *  send the mail
	 */
	rcvrs = 0;
	phase = "";
	USED(phase);			/* just in case */
	if(filter){
		Binit(&bout, 1, OWRITE);
		rv = data(from, &bfile, nil);
		if(rv != 0){
			phase = "filter";
			goto error;
		}
		exits(0);
	}

	/* mxdial uses its own timeout handler */
	if((rv = connect(addr, &mx)) != 0)
		exits(rv);

	tmmx = &mx;
	/* 10 minutes to get through the initial handshake */
	atnotify(timeout, 1);
	alarm(10*alarmscale);
	if((rv = hello(hellodomain, 0)) != 0){
		phase = "hello";
		goto error;
	}
	alarm(10*alarmscale);
	if((rv = mailfrom(s_to_c(from))) != 0){
		phase = "mailfrom";
		goto error;
	}

	ok = 0;
	/* if any rcvrs are ok, we try to send the message */
	phase = "rcptto";
	for(i = 0; i < argc; i++){
		if((trv = rcptto(argv[i])) != 0){
			/* remember worst error */
			if(rv != Giveup)
				rv = trv;
			errs[rcvrs] = strdup(s_to_c(reply));
			removenewline(errs[rcvrs]);
		} else {
			ok++;
			errs[rcvrs] = 0;
		}
		rcvrs++;
	}

	/* if no ok rcvrs or worst error is retry, give up */
	if(ok == 0 && rcvrs == 0)
		phase = "rcptto; no addresses";
	if(ok == 0 || rv == Retry)
		goto error;

	if(ping){
		quit(0);
		exits(0);
	}

	rv = data(from, &bfile, &mx);
	if(rv != 0)
		goto error;
	quit(0);
	if(rcvrs == ok)
		exits(0);

	/*
	 *  here when some but not all rcvrs failed
	 */
	fprint(2, "%s connect to %s: %D %s:\n", thedate(), addr, &mx, phase);
	for(i = 0; i < rcvrs; i++){
		if(errs[i]){
			syslog(0, "smtp.fail", "delivery to %s at %s %D %s, failed: %s",
				argv[i], addr, &mx, phase, errs[i]);
			fprint(2, "  mail to %s failed: %s", argv[i], errs[i]);
		}
	}
	exits(Giveup);

	/*
	 *  here when all rcvrs failed
	 */
error:
	alarm(0);
	removenewline(s_to_c(reply));
	if(rcvrs > 0){
		p = allrx;
		e = allrx + sizeof allrx;
		seprint(p, e, "to ");
		for(i = 0; i < rcvrs - 1; i++)
			p = seprint(p, e, "%s,", argv[i]);
		seprint(p, e, "%s ", argv[i]);
	}
	syslog(0, "smtp.fail", "%s %s at %s %D %s failed: %s",
		deliverytype(), allrx, addr, &mx, phase, s_to_c(reply));
	fprint(2, "%s connect to %s %D %s:\n%s\n", thedate(), addr, &mx, phase, s_to_c(reply));
	if(!filter)
		quit(rv);
	exits(rv);
}

/*
 *  connect to the remote host
 */
static char *
connect(char* net, Mx *mx)
{
	char buf[ERRMAX];
	int fd;

	fd = mxdial(net, ddomain, gdomain, mx);

	if(fd < 0){
		rerrstr(buf, sizeof buf);
		Bprint(&berr, "smtp: %s (%s) %D\n", buf, net, mx);
		syslog(0, "smtp.fail", "%s %s (%s) %D", deliverytype(), buf, net, mx);
		if(strstr(buf, "illegal")
		|| strstr(buf, "unknown")
		|| strstr(buf, "can't translate"))
			return Giveup;
		else
			return Retry;
	}
	Binit(&bin, fd, OREAD);
	fd = dup(fd, -1);
	Binit(&bout, fd, OWRITE);
	return 0;
}

static char smtpthumbs[] =	"/sys/lib/tls/smtp";
static char smtpexclthumbs[] =	"/sys/lib/tls/smtp.exclude";

static int
tracetls(char *fmt, ...)
{
	va_list ap;
	
	va_start(ap, fmt);
	Bvprint(&berr, fmt, ap);
	Bprint(&berr, "\n");
	Bflush(&berr);
	va_end(ap);
	return 0;
}

static char*
wraptls(void)
{
	TLSconn *c;
	Thumbprint *goodcerts;
	char *err;
	int fd;

	goodcerts = nil;
	err = Giveup;
	c = mallocz(sizeof(*c), 1);
	if (c == nil)
		return err;

	if (debug)
		c->trace = tracetls;

	fd = tlsClient(Bfildes(&bout), c);
	if (fd < 0) {
		syslog(0, "smtp", "tlsClient to %q: %r", ddomain);
		goto Out;
	}
	Bterm(&bout);
	Binit(&bout, fd, OWRITE);
	fd = dup(fd, Bfildes(&bin));
	Bterm(&bin);
	Binit(&bin, fd, OREAD);

	goodcerts = initThumbprints(smtpthumbs, smtpexclthumbs, "x509");
	if (goodcerts == nil) {
		syslog(0, "smtp", "bad thumbprints in %s", smtpthumbs);
		goto Out;
	}
	if (!okCertificate(c->cert, c->certlen, goodcerts)) {
		syslog(0, "smtp", "cert for %s not recognized: %r", ddomain);
		goto Out;
	}
	syslog(0, "smtp", "started TLS to %q", ddomain);
	err = nil;
Out:
	freeThumbprints(goodcerts);
	free(c->cert);
	free(c->sessionID);
	free(c);
	return err;
}

/*
 *  exchange names with remote host, attempt to
 *  enable encryption and optionally authenticate.
 *  not fatal if we can't.
 */
static char *
dotls(char *me)
{
	char *err;

	dBprint("STARTTLS\r\n");
	if (getreply() != 2)
		return Giveup;

	err = wraptls();
	if (err != nil)
		return err;

	return(hello(me, 1));
}

static char*
smtpcram(DS *ds)
{
	char *p, ch[128], usr[64], rbuf[128], ubuf[128], ebuf[192];
	int i, n, l;

	dBprint("AUTH CRAM-MD5\r\n");
	if(getreply() != 3)
		return Retry;
	p = s_to_c(reply) + 4;
	l = dec64((uchar*)ch, sizeof ch, p, strlen(p));
	ch[l] = 0;
	n = auth_respond(ch, l, usr, sizeof usr, rbuf, sizeof rbuf, auth_getkey,
		"proto=cram role=client server=%q user=%q",
		ds->host, user);
	if(n == -1)
		return "cannot find SMTP password";
	if(usr[0] == 0)
		return "cannot find user name";
	for(i = 0; i < n; i++)
		rbuf[i] = tolower(rbuf[i]);
	l = snprint(ubuf, sizeof ubuf, "%s %.*s", usr, n, rbuf);
	snprint(ebuf, sizeof ebuf, "%.*[", l, ubuf);

	dBprint("%s\r\n", ebuf);
	if(getreply() != 2)
		return Retry;
	return nil;
}

static char *
doauth(char *methods)
{
	char buf[1024], *err;
	UserPasswd *p;
	DS ds;
	int n;

	dialstringparse(farend, &ds);
	if(strstr(methods, "CRAM-MD5"))
		return smtpcram(&ds);
	p = auth_getuserpasswd(nil,
		"proto=pass service=smtp server=%q user=%q",
		ds.host, user);
	if (p == nil) {
		syslog(0, "smtp.fail", "failed to get userpasswd: %r");
		return Giveup;
	}
	err = Retry;
	if (strstr(methods, "LOGIN")){
		dBprint("AUTH LOGIN\r\n");
		if (getreply() != 3)
			goto out;

		dBprint("%.*[\r\n", (int)strlen(p->user), p->user);
		if (getreply() != 3)
			goto out;

		dBprint("%.*[\r\n", (int)strlen(p->passwd), p->passwd);
		if (getreply() != 2)
			goto out;

		err = nil;
	}
	else if (strstr(methods, "PLAIN")){
		n = snprint(buf, sizeof(buf), "%c%s%c%s", 0, p->user, 0, p->passwd);
		dBprint("AUTH PLAIN %.*[\r\n", n, buf);
		memset(buf, 0, sizeof(buf));
		if (getreply() != 2)
			goto out;
		err = nil;
	} else
		err = "No supported AUTH method";
out:
	memset(p->user, 0, strlen(p->user));
	memset(p->passwd, 0, strlen(p->passwd));
	free(p);
	return err;
}

char*
hello(char *me, int encrypted)
{
	char *ret, *s, *t;
	int ehlo;
	String *r;

	if(!encrypted){
		if(trysecure > 1){
			if((ret = wraptls()) != nil)
				return ret;
			encrypted = 1;
		}

		/*
		 * Verizon fails to print the smtp greeting banner when it
		 * answers a call.  Send a no-op in the hope of making it
		 * talk.
		 */
		if(autistic){
			dBprint("NOOP\r\n");
			getreply();	/* consume the smtp greeting */
			/* next reply will be response to noop */
		}
		switch(getreply()){
		case 2:
			break;
		case 5:
			return Giveup;
		default:
			return Retry;
		}
	}

	ehlo = 1;
  Again:
	if(ehlo)
		dBprint("EHLO %s\r\n", me);
	else
		dBprint("HELO %s\r\n", me);
	switch(getreply()){
	case 2:
		break;
	case 5:
		if(ehlo){
			ehlo = 0;
			goto Again;
		}
		return Giveup;
	default:
		return Retry;
	}
	r = s_clone(reply);
	if(r == nil)
		return Retry;	/* Out of memory or couldn't get string */

	/* Invariant: every line has a newline, a result of getcrlf() */
	for(s = s_to_c(r); (t = strchr(s, '\n')) != nil; s = t + 1){
		*t = '\0';
		if(!encrypted && trysecure &&
		    (cistrcmp(s, "250-STARTTLS") == 0 ||
		     cistrcmp(s, "250 STARTTLS") == 0)){
			s_free(r);
			return dotls(me);
		}
		if(tryauth && (encrypted || insecure) &&
		    (cistrncmp(s, "250 AUTH", strlen("250 AUTH")) == 0 ||
		     cistrncmp(s, "250-AUTH", strlen("250 AUTH")) == 0)){
			ret = doauth(s + strlen("250 AUTH "));
			s_free(r);
			return ret;
		}
	}
	s_free(r);
	return 0;
}

/*
 *  report sender to remote
 */
char *
mailfrom(char *from)
{
	if(!returnable(from))
		dBprint("MAIL FROM:<>\r\n");
	else if(strchr(from, '@'))
		dBprint("MAIL FROM:<%s>\r\n", from);
	else
		dBprint("MAIL FROM:<%s@%s>\r\n", from, hostdomain);
	switch(getreply()){
	case 2:
		return 0;
	case 5:
		return Giveup;
	default:
		return Retry;
	}
}

/*
 *  report a recipient to remote
 */
char *
rcptto(char *to)
{
	String *s;

	s = unescapespecial(bangtoat(to));
	if(toline == 0)
		toline = s_new();
	else
		s_append(toline, ", ");
	s_append(toline, s_to_c(s));
	if(strchr(s_to_c(s), '@'))
		dBprint("RCPT TO:<%s>\r\n", s_to_c(s));
	else {
		s_append(toline, "@");
		s_append(toline, ddomain);
		dBprint("RCPT TO:<%s@%s>\r\n", s_to_c(s), ddomain);
	}
	alarm(10*alarmscale);
	switch(getreply()){
	case 2:
		break;
	case 5:
		return Giveup;
	default:
		return Retry;
	}
	return 0;
}

/*
 *  send the damn thing
 */
char *
data(String *from, Biobuf *b, Mx *mx)
{
	char *buf, *cp, errmsg[ERRMAX];
	int n, nbytes, bufsize, eof;
	String *fromline;

	/*
	 *  input the header.
	 */

	buf = malloc(1);
	if(buf == 0){
		s_append(s_restart(reply), "out of memory");
		return Retry;
	}
	n = 0;
	eof = 0;
	for(;;){
		cp = Brdline(b, '\n');
		if(cp == nil){
			eof = 1;
			break;
		}
		nbytes = Blinelen(b);
		buf = realloc(buf, n + nbytes + 1);
		if(buf == 0){
			s_append(s_restart(reply), "out of memory");
			return Retry;
		}
		strncpy(buf + n, cp, nbytes);
		n += nbytes;
		if(nbytes == 1)		/* end of header */
			break;
	}
	buf[n] = 0;
	bufsize = n;

	/*
	 *  parse the header, turn all addresses into @ format
	 */
	yyinit(buf, n);
	yyparse();

	/*
	 *  print message observing '.' escapes and using \r\n for \n
	 */
	alarm(20*alarmscale);
	if(!filter){
		dBprint("DATA\r\n");
		switch(getreply()){
		case 3:
			break;
		case 5:
			free(buf);
			return Giveup;
		default:
			free(buf);
			return Retry;
		}
	}
	/*
	 *  send header.  add a message-id, a sender, and a date if there
	 *  isn't one
	 */
	nbytes = 0;
	fromline = convertheader(from);
	uneaten = buf;

	if(messageid == 0){
		uchar id[16];

		genrandom(id, sizeof(id));
		nbytes += dBprint("Message-ID: <%.*H@%s>\r\n",
			sizeof(id), id, hostdomain);
	}

	if(originator == 0)
		nbytes += dBprint("From: %s\r\n", s_to_c(fromline));
	s_free(fromline);

	if(destination == 0 && toline){
		if(*s_to_c(toline) == '@')	/* route addr */
			nbytes += dBprint("To: <%s>\r\n", s_to_c(toline));
		else
			nbytes += dBprint("To: %s\r\n", s_to_c(toline));
	}

	if(date == 0 && udate)
		nbytes += printdate(udate);
	if(usys)
		uneaten = usys->end + 1;
	nbytes += printheader();
	if(*uneaten != '\n')
		putcrnl("\n", 1);

	/*
	 *  send body
	 */

	putcrnl(uneaten, buf + n - uneaten);
	nbytes += buf + n - uneaten;
	if(eof == 0){
		for(;;){
			n = Bread(b, buf, bufsize);
			if(n < 0){
				rerrstr(errmsg, sizeof(errmsg));
				s_append(s_restart(reply), errmsg);
				free(buf);
				return Retry;
			}
			if(n == 0)
				break;
			alarm(10*alarmscale);
			putcrnl(buf, n);
			nbytes += n;
		}
	}
	free(buf);
	if(!filter){
		if(last != '\n')
			dBprint("\r\n.\r\n");
		else
			dBprint(".\r\n");
		alarm(10*alarmscale);
		switch(getreply()){
		case 2:
			break;
		case 5:
			return Giveup;
		default:
			return Retry;
		}
		syslog(0, "smtp", "%s sent %d bytes to %s %D", s_to_c(from),
				nbytes, s_to_c(toline), mx);
	}
	return 0;
}

/*
 *  we're leaving
 */
void
quit(char *rv)
{
		/* 60 minutes to quit */
	quitting = 1;
	quitrv = rv;
	alarm(60*alarmscale);
	dBprint("QUIT\r\n");
	getreply();
	Bterm(&bout);
	Bterm(&bfile);
}

/*
 *  read a reply into a string, return the reply code
 */
int
getreply(void)
{
	char *line;
	int rv;

	reply = s_reset(reply);
	for(;;){
		line = getcrnl(reply);
		if(debug)
			Bflush(&berr);
		if(line == 0)
			return -1;
		if(!isdigit(line[0]) || !isdigit(line[1]) || !isdigit(line[2]))
			return -1;
		if(line[3] != '-')
			break;
	}
	if(debug)
		Bflush(&berr);
	rv = atoi(line)/100;
	return rv;
}
void
addhostdom(String *buf, char *host)
{
	s_append(buf, "@");
	s_append(buf, host);
}

/*
 *	Convert from `bang' to `source routing' format.
 *
 *	   a.x.y!b.p.o!c!d ->	@a.x.y:c!d@b.p.o
 */
String *
bangtoat(char *addr)
{
	char *field[128];
	int i, j, d;
	String *buf;

	/* parse the '!' format address */
	buf = s_new();
	for(i = 0; addr; i++){
		field[i] = addr;
		addr = strchr(addr, '!');
		if(addr)
			*addr++ = 0;
	}
	if(i == 1){
		s_append(buf, field[0]);
		return buf;
	}

	/*
	 *  count leading domain fields (non-domains don't count)
	 */
	for(d = 0; d < i - 1; d++)
		if(strchr(field[d], '.') == 0)
			break;
	/*
	 *  if there are more than 1 leading domain elements,
	 *  put them in as source routing
	 */
	if(d > 1){
		addhostdom(buf, field[0]);
		for(j = 1; j< d - 1; j++){
			s_append(buf, ",");
			s_append(buf, "@");
			s_append(buf, field[j]);
		}
		s_append(buf, ":");
	}

	/*
	 *  throw in the non-domain elements separated by '!'s
	 */
	s_append(buf, field[d]);
	for(j = d + 1; j <= i - 1; j++){
		s_append(buf, "!");
		s_append(buf, field[j]);
	}
	if(d)
		addhostdom(buf, field[d-1]);
	return buf;
}

/*
 *  convert header addresses to @ format.
 *  if the address is a source address, and a domain is specified,
 *  make sure it falls in the domain.
 */
String*
convertheader(String *from)
{
	char *s, buf[64];
	Field *f;
	Node *p, *lastp;
	String *a;

	if(!returnable(s_to_c(from))){
		from = s_new();
		s_append(from, "Postmaster");
		addhostdom(from, hostdomain);
	} else
	if(strchr(s_to_c(from), '@') == 0){
		if(s = username(s_to_c(from))){
			/* this has always been here, but username() was broken */
			snprint(buf, sizeof buf, "%U", s);
			s_append(a = s_new(), buf);
			s_append(a, " <");
			s_append(a, s_to_c(from));
			addhostdom(a, hostdomain);
			s_append(a, ">");
			from = a;
		} else {
			from = s_copy(s_to_c(from));
			addhostdom(from, hostdomain);
		}
	} else
		from = s_copy(s_to_c(from));
	for(f = firstfield; f; f = f->next){
		lastp = 0;
		for(p = f->node; p; lastp = p, p = p->next){
			if(!p->addr)
				continue;
			a = bangtoat(s_to_c(p->s));
			s_free(p->s);
			if(strchr(s_to_c(a), '@') == 0)
				addhostdom(a, hostdomain);
			else if(*s_to_c(a) == '@')
				a = fixrouteaddr(a, p->next, lastp);
			p->s = a;
		}
	}
	return from;
}
/*
 *	ensure route addr has brackets around it
 */
String*
fixrouteaddr(String *raddr, Node *next, Node *last)
{
	String *a;

	if(last && last->c == '<' && next && next->c == '>')
		return raddr;			/* properly formed already */

	a = s_new();
	s_append(a, "<");
	s_append(a, s_to_c(raddr));
	s_append(a, ">");
	s_free(raddr);
	return a;
}

/*
 *  print out the parsed header
 */
int
printheader(void)
{
	char *cp, c[1];
	int n, len;
	Field *f;
	Node *p;

	n = 0;
	for(f = firstfield; f; f = f->next){
		for(p = f->node; p; p = p->next){
			if(p->s)
				n += dBprint("%s", s_to_c(p->s));
			else {
				c[0] = p->c;
				putcrnl(c, 1);
				n++;
			}
			if(p->white){
				cp = s_to_c(p->white);
				len = strlen(cp);
				putcrnl(cp, len);
				n += len;
			}
			uneaten = p->end;
		}
		putcrnl("\n", 1);
		n++;
		uneaten++;		/* skip newline */
	}
	return n;
}

/*
 *  add a domain onto an name, return the new name
 */
char *
domainify(char *name, char *domain)
{
	char *p;
	static String *s;

	if(domain == 0 || strchr(name, '.') != 0)
		return name;

	s = s_reset(s);
	s_append(s, name);
	p = strchr(domain, '.');
	if(p == 0){
		s_append(s, ".");
		p = domain;
	}
	s_append(s, p);
	return s_to_c(s);
}

/*
 *  print message observing '.' escapes and using \r\n for \n
 */
void
putcrnl(char *cp, int n)
{
	int c;

	for(; n; n--, cp++){
		c = *cp;
		if(c == '\n')
			dBputc('\r');
		else if(c == '.' && last=='\n')
			dBputc('.');
		dBputc(c);
		last = c;
	}
}

/*
 *  Get a line including a crnl into a string.  Convert crnl into nl.
 */
char *
getcrnl(String *s)
{
	int c, count;

	count = 0;
	for(;;){
		c = Bgetc(&bin);
		if(debug)
			Bputc(&berr, c);
		switch(c){
		case -1:
			s_append(s, "connection closed unexpectedly by remote system");
			s_terminate(s);
			return 0;
		case '\r':
			c = Bgetc(&bin);
			if(c == '\n'){
		case '\n':
				s_putc(s, c);
				if(debug)
					Bputc(&berr, c);
				count++;
				s_terminate(s);
				return s->ptr - count;
			}
			Bungetc(&bin);
			s_putc(s, '\r');
			if(debug)
				Bputc(&berr, '\r');
			count++;
			break;
		default:
			s_putc(s, c);
			count++;
			break;
		}
	}
}

/*
 *  print out a parsed date
 */
int
printdate(Node *p)
{
	int n, sep;

	n = dBprint("Date: %s,", s_to_c(p->s));
	sep = 0;
	for(p = p->next; p; p = p->next){
		if(p->s){
			if(sep == 0){
				dBputc(' ');
				n++;
			}
			if(p->next)
				n += dBprint("%s", s_to_c(p->s));
			else
				n += dBprint("%s", rewritezone(s_to_c(p->s)));
			sep = 0;
		} else {
			dBputc(p->c);
			n++;
			sep = 1;
		}
	}
	n += dBprint("\r\n");
	return n;
}

char *
rewritezone(char *z)
{
	char s;
	int mindiff;
	Tm *tm;
	static char x[7];

	tm = localtime(time(0));
	mindiff = tm->tzoff/60;

	/* if not in my timezone, don't change anything */
	if(strcmp(tm->zone, z) != 0)
		return z;

	if(mindiff < 0){
		s = '-';
		mindiff = -mindiff;
	} else
		s = '+';

	sprint(x, "%c%.2d%.2d", s, mindiff/60, mindiff%60);
	return x;
}

/*
 *  stolen from libc/port/print.c
 */

int
dBprint(char *fmt, ...)
{
	char buf[4096], *out;
	int n;
	va_list arg;

	va_start(arg, fmt);
	out = vseprint(buf, buf + sizeof buf, fmt, arg);
	va_end(arg);
	if(debug){
		Bwrite(&berr, buf, out - buf);
		Bflush(&berr);
	}
	n = Bwrite(&bout, buf,out - buf);
	Bflush(&bout);
	return n;
}

int
dBputc(int x)
{
	if(debug)
		Bputc(&berr, x);
	return Bputc(&bout, x);
}
