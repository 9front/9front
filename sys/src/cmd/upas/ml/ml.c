#include "common.h"
#include "dat.h"

Biobuf	in;
Addr	*al;
int	na;
String	*from;
String	*sender;

char*
trim(char *s)
{
	while(*s == ' ' || *s == '\t')
		s++;
	return s;
}

/* add the listname to the subject */
void
printsubject(int fd, Field *f, char *listname)
{
	char *s, *e, *ln;
	Node *p;

	if(f == nil || f->node == nil){
		fprint(fd, "Subject: [%s]\n", listname);
		return;
	}
	s = e = f->node->end + 1;
	for(p = f->node; p; p = p->next)
		e = p->end;
	*e = 0;
	ln = smprint("[%s]", listname);
	if(ln != nil && strstr(s, ln) == nil)
		fprint(fd, "Subject: %s %s\n", ln, trim(s));
	else
		fprint(fd, "Subject: %s\n", trim(s));
	free(ln);
	*e = '\n';
}

/* send message filtering Reply-to out of messages */
void
printmsg(int fd, String *msg, char *replyto, char *listname)
{
	Field *f, *subject;
	Node *p;
	char *cp, *ocp;

	subject = nil;
	cp = s_to_c(msg);
	for(f = firstfield; f; f = f->next){
		ocp = cp;
		for(p = f->node; p; p = p->next)
			cp = p->end+1;
		switch(f->node->c){
		case SUBJECT:
			subject = f;
		case REPLY_TO:
		case PRECEDENCE:
			continue;
		}
		write(fd, ocp, cp-ocp);
	}
	printsubject(fd, subject, listname);
	fprint(fd, "Reply-To: %s\nPrecedence: bulk\n", replyto);
	write(fd, cp, s_len(msg) - (cp - s_to_c(msg)));
}

/* if the mailbox exists, cat the mail to the end of it */
void
appendtoarchive(char* listname, String *firstline, String *msg)
{
	char *f;
	Biobuf *b;

	f = foldername(nil, listname, "mbox");
	if(access(f, 0) < 0)
		return;
	if((b = openfolder(f, time(0))) == nil)
		return;
	Bwrite(b, s_to_c(firstline), s_len(firstline));
	Bwrite(b, s_to_c(msg), s_len(msg));
	Bwrite(b, "\n", 1);
	closefolder(b);
}

void
usage(void)
{
	fprint(2, "usage: %s address-list-file listname\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *listname, *alfile, *replytoname;
	int fd, private;
	String *msg, *firstline;
	Waitmsg *w;

	private = 0;
	replytoname = nil;
	ARGBEGIN{
	default:
		usage();
	case 'p':
		private = 1;
		break;
	case 'r':
		replytoname = EARGF(usage());
		break;
	}ARGEND;

	rfork(RFENVG|RFREND);

	if(argc < 2)
		usage();
	alfile = argv[0];
	listname = argv[1];
	if(replytoname == nil)
		replytoname = listname;

	readaddrs(alfile);

	if(Binit(&in, 0, OREAD) < 0)
		sysfatal("opening input: %r");

	msg = s_new();
	firstline = s_new();

	/* discard the 'From ' line */
	if(s_read_line(&in, firstline) == nil)
		sysfatal("reading input: %r");

	/*
	 * read up to the first 128k of the message.  more is ridiculous. 
	 *   Not if word documents are distributed.  Upped it to 2MB (pb)
	 */
	if(s_read(&in, msg, 2*1024*1024) <= 0)
		sysfatal("reading input: %r");

	/* parse the header */
	yyinit(s_to_c(msg), s_len(msg));
	yyparse();

	/* get the sender */
	getaddrs();
	if(from == nil)
		from = sender;
	if(from == nil)
		sysfatal("message must contain From: or Sender:");
	if(strcmp(listname, s_to_c(from)) == 0)
		sysfatal("can't remail messages from myself");
	if(addaddr(s_to_c(from)) != 0 && private)
		sysfatal("not a list member");

	/* start the mailer up and return a pipe to it */
	fd = startmailer(listname);

	/* send message adding our own reply-to and precedence */
	printmsg(fd, msg, replytoname, listname);
	close(fd);

	/* wait for mailer to end */
	while(w = wait()){
		if(w->msg != nil && w->msg[0])
			sysfatal("%s", w->msg);
		free(w);
	}

	/* if the mailbox exists, cat the mail to the end of it */
	appendtoarchive(listname, firstline, msg);
	exits(0);
}
