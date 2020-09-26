#include "common.h"
#include "send.h"

/* globals to all files */
int	flagn;
int	flagx;
int	debug;
int	flagi  = 1;
int	rmail;
int	nosummary;
char	*thissys;
char	*altthissys;

/* global to this file */
static	String	*errstring;
static	message	*mp;
static	int	interrupt;
static	int	savemail;
static	Biobuf	in;
static	int	forked;
static	int	add822headers = 1;
static	String	*arglist;

/* predeclared */
static	int	send(dest*, message*, int);
static	void	lesstedious(void);
static	void	save_mail(message*);
static	int	complain_mail(dest*, message*);
static	int	pipe_mail(dest*, message*);
static	int	catchint(void*, char*);

void
usage(void)
{
	fprint(2, "usage: send [-#bdirx] list-of-addresses\n");
	exits("usage");
}

void
main(int argc, char *argv[])
{
	int rv;
	dest *dp;

	ARGBEGIN{
	case '#':
		flagn = 1;
		break;
	case 'b':
		add822headers = 0;
		break;
	case 'd':
		debug = 1;
		break;
	case 'i':
		flagi = 0;
		break;
	case 'r':
		rmail++;
		break;
	case 'x':
		flagn = 1;
		flagx = 1;
		break;
	default:
		usage();
	}ARGEND

	tmfmtinstall();
	if(*argv == 0)
		usage();
	dp = 0;
	for(; *argv; argv++){
		if(shellchars(*argv)){
			fprint(2, "illegal characters in destination\n");
			exits("syntax");
		}
		d_insert(&dp, d_new(s_copy(*argv)));
	}
	arglist = d_to(dp);

	thissys = sysname_read();
	altthissys = alt_sysname_read();
	if(rmail)
		add822headers = 0;

	/*
	 *  read the mail.  If an interrupt occurs while reading, save in
	 *  dead.letter
	 */
	if (!flagn) {
		Binit(&in, 0, OREAD);
		if(!rmail)
			atnotify(catchint, 1);
		mp = m_read(&in, rmail, !flagi);
		if (mp == 0)
			exits(0);
		if (interrupt != 0) {
			save_mail(mp);
			exits("interrupt");
		}
	} else {
		mp = m_new();
		if(default_from(mp) < 0){
			fprint(2, "%s: can't determine login name\n", argv0);
			exits("fail");
		}
	}
	errstring = s_new();
	getrules();

	/*
	 *  If this is a gateway, translate the sender address into a local
	 *  address.  This only happens if mail to the local address is 
	 *  forwarded to the sender.
	 */
	gateway(mp);

	/*
	 *  Protect against shell characters in the sender name for
	 *  security reasons.
	 */
	mp->sender = escapespecial(mp->sender);
	if(shellchars(s_to_c(mp->sender)))
		mp->replyaddr = s_copy("postmaster");
	else
		mp->replyaddr = s_clone(mp->sender);

	/*
	 *  reject messages that have been looping for too long
	 */
	if(mp->received > 32)
		exits(refuse(dp, mp, "possible forward loop", 0, 0)? "refuse": "");

	/*
	 *  reject messages that are too long.  We don't do it earlier
	 *  in m_read since we haven't set up enough things yet.
	 */
	if(mp->size < 0)
		exits(refuse(dp, mp, "message too long", 0, 0)? "refuse": "");

	rv = send(dp, mp, rmail);
	if(savemail)
		save_mail(mp);
	if(mp)
		m_free(mp);
	exits(rv? "fail": "");
}

/* send a message to a list of sites */
static int
send(dest *destp, message *mp, int checkforward)
{
	dest *dp;		/* destination being acted upon */
	dest *bound;		/* bound destinations */
	int errors=0;

	/* bind the destinations to actions */
	bound = up_bind(destp, mp, checkforward);
	if(add822headers && mp->haveto == 0){
		if(nosummary)
			mp->to = d_to(bound);
		else
			mp->to = arglist;
	}

	/* loop through and execute commands */
	for (dp = d_rm(&bound); dp != 0; dp = d_rm(&bound)) {
		switch (dp->status) {
		case d_cat:
			errors += cat_mail(dp, mp);
			break;
		case d_pipeto:
		case d_pipe:
			lesstedious();
			errors += pipe_mail(dp, mp);
			break;
		default:
			errors += complain_mail(dp, mp);
			break;
		}
	}

	return errors;
}

/* avoid user tedium (as Mike Lesk said in a previous version) */
static void
lesstedious(void)
{
	int i;

	if(debug)
		return;
	if(rmail || flagn || forked)
		return;
	switch(fork()){
	case -1:
		break;
	case 0:
		sysdetach();
		for(i=0; i<3; i++)
			close(i);
		savemail = 0;
		forked = 1;
		break;
	default:
		exits("");
	}
}


/* save the mail */
static void
save_mail(message *mp)
{
	char buf[Pathlen];
	Biobuf *fp;

	mboxpathbuf(buf, sizeof buf, getlog(), "dead.letter");
	fp = sysopen(buf, "cAt", 0660);
	if (fp == 0)
		return;
	m_bprint(mp, fp);
	sysclose(fp);
	fprint(2, "saved in %s\n", buf);
}

/* remember the interrupt happened */

static int
catchint(void*, char *msg)
{
	if(strstr(msg, "interrupt") || strstr(msg, "hangup")) {
		interrupt = 1;
		return 1;
	}
	return 0;
}

/* dispose of incorrect addresses */
static int
complain_mail(dest *dp, message *mp)
{
	char *msg;

	switch (dp->status) {
	case d_undefined:
		msg = "Invalid address"; /* a little different, for debugging */
		break;
	case d_syntax:
		msg = "invalid address";
		break;
	case d_unknown:
		msg = "unknown user";
		break;
	case d_eloop:
	case d_loop:
		msg = "forwarding loop";
		break;
	case d_noforward:
		if(dp->pstat && *s_to_c(dp->repl2))
			return refuse(dp, mp, s_to_c(dp->repl2), dp->pstat, 0);
		else
			msg = "destination unknown or forwarding disallowed";
		break;
	case d_pipe:
		msg = "broken pipe";
		break;
	case d_cat:
		msg = "broken cat";
		break;
	case d_translate:
		if(dp->pstat && *s_to_c(dp->repl2))
			return refuse(dp, mp, s_to_c(dp->repl2), dp->pstat, 0);
		else
			msg = "name translation failed";
		break;
	case d_alias:
		msg = "broken alias";
		break;
	case d_badmbox:
		msg = "corrupted mailbox";
		break;
	case d_resource:
		return refuse(dp, mp, "out of some resource.  Try again later.", 0, 1);
	default:
		msg = "unknown d_";
		break;
	}
	if (flagn) {
		print("%s: %s\n", msg, s_to_c(dp->addr));
		return 0;
	}
	return refuse(dp, mp, msg, 0, 0);
}

/* dispose of remote addresses */
static int
pipe_mail(dest *dp, message *mp)
{
	int status;
	char *none;
	dest *next, *list;
	process *pp;
	String *cmd;
	String *errstring;

	errstring = s_new();
	list = 0;

	/*
	 * we're just protecting users from their own
	 * pipeto scripts with this none business.
	 * this depends on none being able to append
	 * to a mail file.
	 */

	if (dp->status == d_pipeto)
		none = "none";
	else
		none = 0;
	/*
	 *  collect the arguments
	 */
	next = d_rm_same(&dp);
	if(flagx)
		cmd = s_new();
	else
		cmd = s_clone(next->repl1);
	for(; next != 0; next = d_rm_same(&dp)){
		if(flagx){
			s_append(cmd, s_to_c(next->addr));
			s_append(cmd, "\n");
		} else {
			if (next->repl2 != 0) {
				s_append(cmd, " ");
				s_append(cmd, s_to_c(next->repl2));
			}
		}
		d_insert(&list, next);
	}

	if (flagn) {
		if(flagx)
			print("%s", s_to_c(cmd));
		else
			print("%s\n", s_to_c(cmd));
		s_free(cmd);
		return 0;
	}

	/*
	 *  run the process
	 */
	pp = proc_start(s_to_c(cmd), instream(), 0, outstream(), 1, none);
	if(pp==0 || pp->std[0]==0 || pp->std[2]==0)
		return refuse(list, mp, "out of processes, pipes, or memory", 0, 1);
	pipesig(0);
	m_print(mp, pp->std[0]->fp, thissys, 0);
	pipesigoff();
	stream_free(pp->std[0]);
	pp->std[0] = 0;
	while(s_read_line(pp->std[2]->fp, errstring))
		;
	status = proc_wait(pp);
	proc_free(pp);
	s_free(cmd);

	/*
	 *  return status
	 */
	if (status != 0)
		return refuse(list, mp, s_to_c(errstring), status, 0);
	loglist(list, mp, "remote");
	return 0;
}

/*
 *  create a new boundary
 */
static String*
mkboundary(void)
{
	char buf[32];
	int i;
	static int already;

	if(already == 0){
		srand((time(0)<<16)|getpid());
		already = 1;
	}
	strcpy(buf, "upas-");
	for(i = 5; i < sizeof(buf)-1; i++)
		buf[i] = 'a' + nrand(26);
	buf[i] = 0;
	return s_copy(buf);
}

/*
 *  reply with up to 1024 characters of the
 *  original message
 */
static int
replymsg(String *errstring, message *mp, dest *dp)
{
	message *refp = m_new();
	String *boundary;
	dest *ndp;
	char *rcvr, now[128];
	int rv;
	Tm tm;

	boundary = mkboundary();

	refp->bulk = 1;
	refp->rfc822headers = 1;
	snprint(now, sizeof(now), "%τ", thedate(&tm));
	rcvr = dp->status==d_eloop ? "postmaster" : s_to_c(mp->replyaddr);
	ndp = d_new(s_copy(rcvr));
	s_append(refp->sender, "postmaster");
	s_append(refp->replyaddr, "/dev/null");
	s_append(refp->date, now);
	refp->haveto = 1;
	s_append(refp->body, "To: ");
	s_append(refp->body, rcvr);
	s_append(refp->body, "\n"
		"Subject: bounced mail\n"
		"MIME-Version: 1.0\n"
		"Content-Type: multipart/mixed;\n"
		"\tboundary=\"");
	s_append(refp->body, s_to_c(boundary));
	s_append(refp->body, "\"\n"
		"Content-Disposition: inline\n"
		"\n"
		"This is a multi-part message in MIME format.\n"
		"--");
	s_append(refp->body, s_to_c(boundary));
	s_append(refp->body, "\n"
		"Content-Disposition: inline\n"
		"Content-Type: text/plain; charset=\"US-ASCII\"\n"
		"Content-Transfer-Encoding: 7bit\n"
		"\n"
		"The attached mail");
	s_append(refp->body, s_to_c(errstring));
	s_append(refp->body, "--");
	s_append(refp->body, s_to_c(boundary));
	s_append(refp->body, "\n"
		"Content-Type: message/rfc822\n"
		"Content-Disposition: inline\n\n");
	s_append(refp->body, s_to_c(mp->body));
	s_append(refp->body, "--");
	s_append(refp->body, s_to_c(boundary));
	s_append(refp->body, "--\n");

	refp->size = s_len(refp->body);
	rv = send(ndp, refp, 0);
	m_free(refp);
	d_free(ndp);
	return rv;
}

static void
appaddr(String *sp, dest *dp)
{
	dest *p;
	String *s;

	if (dp->parent != 0) {
		for(p = dp->parent; p->parent; p = p->parent)
			;
		s = unescapespecial(s_clone(p->addr));
		s_append(sp, s_to_c(s));
		s_free(s);
		s_append(sp, "' alias `");
	}
	s = unescapespecial(s_clone(dp->addr));
	s_append(sp, s_to_c(s));
	s_free(s);
}

/* make the error message */
static void
mkerrstring(String *errstring, message *mp, dest *dp, dest *list, char *cp, int status)
{
	dest *next;
	char smsg[64];
	String *sender;

	sender = unescapespecial(s_clone(mp->sender));

	/* list all aliases */
	s_append(errstring, " from '");
	s_append(errstring, s_to_c(sender));
	s_append(errstring, "'\nto '");
	appaddr(errstring, dp);
	for(next = d_rm(&list); next != 0; next = d_rm(&list)) {
		s_append(errstring, "'\nand '");
		appaddr(errstring, next);
		d_insert(&dp, next);
	}
	s_append(errstring, "'\nfailed with error '");
	s_append(errstring, cp);
	s_append(errstring, "'.\n");

	/* >> and | deserve different flavored messages */
	switch(dp->status) {
	case d_pipe:
		s_append(errstring, "The mailer `");
		s_append(errstring, s_to_c(dp->repl1));
		sprint(smsg, "' returned error status %x.\n\n", status);
		s_append(errstring, smsg);
		break;
	}

	s_free(sender);
}

/*
 *  reject delivery
 *
 *  returns	0	- if mail has been disposed of
 *		other	- if mail has not been disposed
 */
int
refuse(dest *list, message *mp, char *cp, int status, int outofresources)
{
	int rv;
	String *errstring;
	dest *dp;

	errstring = s_new();
	dp = d_rm(&list);
	mkerrstring(errstring, mp, dp, list, cp, status);

	/*
	 *  log first in case we get into trouble
	 */
	logrefusal(dp, mp, s_to_c(errstring));

	rv = 1;
	if(rmail){
		/* accept it or request a retry */
		if(outofresources){
			fprint(2, "Mail %s\n", s_to_c(errstring));
		} else {
			/*
			 *  reject without generating a reply, smtpd returns
			 *  5.0.0 status when it sees "mail refused"
			 */
			fprint(2, "mail refused: %s\n",  s_to_c(errstring));
		}
	} else {
		/* aysnchronous delivery only happens if !rmail */
		if(forked){
			/*
			 *  if spun off for asynchronous delivery, we own the mail now.
			 *  return it or dump it on the floor.  rv really doesn't matter.
			 */
			rv = 0;
			if(!outofresources && !mp->bulk)
				replymsg(errstring, mp, dp);
		} else {
			fprint(2, "Mail %s\n", s_to_c(errstring));
			savemail = 1;
		}
	}

	s_free(errstring);
	return rv;
}
