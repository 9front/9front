#include "common.h"
#include <ctype.h>
#include <plumb.h>
#include <regexp.h>

typedef struct Cmd Cmd;
typedef struct Ctype Ctype;
typedef struct Dirstats Dirstats;
typedef struct Message Message;
typedef Message* (Mfn)(Cmd*,Message*);

enum{
	/* nflags */
	Nmissing	= 1<<0,
	Nnoflags	= 1<<1,

	Narg	= 32,
};

struct Message {
	Message	*next;
	Message	*prev;
	Message	*cmd;
	Message	*child;
	Message	*parent;
	char	*path;
	int	id;
	int	len;
	int	fileno;	/* number of directory */
	char	*info;
	char	*from;
	char	*to;
	char	*cc;
	char	*replyto;
	char	*date;
	char	*subject;
	char	*type;
	char	*disposition;
	char	*filename;
	uchar	flags;
	uchar	nflags;
};
#pragma varargck	type	"D"	Message*

enum{
	Display	= 1<<0,
	Rechk	= 1<<1,	/* always use file to check content type */
};

struct Ctype {
	char	*type;
	char 	*ext;
	uchar	flag;
	char	*plumbdest;
	Ctype	*next;
};

/* first element is the default return value */
Ctype ctype[] = {
	{ "application/octet-stream", 	"bin", 	Rechk, 	0,	0,	},
	{ "text/plain",			"txt",	Display,	0	},
	{ "text/html",			"htm",	Display,	0	},
	{ "text/html",			"html",	Display,	0	},
	{ "text/tab-separated-values",	"tsv",	Display,	0	},
	{ "text/richtext",			"rtx",	Display,	0	},
	{ "text/rtf",			"rtf",	Display,	0	},
	{ "text",				"txt",	Display,	0	},
	{ "message/rfc822",		"msg",	0,	0	},
	{ "image/bmp",			"bmp",	0,	"image"	},
	{ "image/jpg",			"jpg",	0,	"image"	},
	{ "image/jpeg",			"jpg",	0,	"image"	},
	{ "image/gif",			"gif",	0,	"image"	},
	{ "image/png",			"png",	0,	"image"	},
	{ "image/x-png",			"png",	0,	"image"	},
	{ "image/tiff",			"tif",	0,	"image"	},
	{ "application/pdf",		"pdf",	0,	"postscript"	},
	{ "application/postscript",		"ps",	0,	"postscript"	},
	{ "application/vnd.openxmlformats-officedocument.wordprocessingml.document",		"docx",	0,	"docx"	},
	{ "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",		"xlsx",	0,	"xlsx"	},
	{ "application/",			0,	0,	0	},
	{ "image/",			0,	0,	0	},
	{ "multipart/",			"mul",	0,	0	},

};

struct Dirstats {
	int	new;
	int	del;
	int	old;
	int	unread;
};

Mfn	acmd;
Mfn	bangcmd;
Mfn	bcmd;
Mfn	dcmd;
Mfn	eqcmd;
Mfn	Fcmd;
Mfn	fcmd;
Mfn	fqcmd;
Mfn	Hcmd;
Mfn	hcmd;
Mfn	helpcmd;
Mfn	icmd;
Mfn	Kcmd;
Mfn	kcmd;
Mfn	mbcmd;
Mfn	mcmd;
Mfn	Pcmd;
Mfn	pcmd;
Mfn	pipecmd;
Mfn	qcmd;
Mfn	quotecmd;
Mfn	rcmd;
Mfn	rpipecmd;
Mfn	scmd;
Mfn	tcmd;
Mfn	ucmd;
Mfn	wcmd;
Mfn	xcmd;
Mfn	ycmd;

struct {
	char	*cmd;
	int	args;
	int	addr;
	Mfn	*f;
	char	*help;
} cmdtab[] = {
	{ "a",	1, 1,	acmd,	"a\t"		"reply to sender and recipients" },
	{ "A",	1, 0,	acmd,	"A\t"		"reply to sender and recipients with copy" },
	{ "b",	0, 0,	bcmd,	"b\t"		"print the next 10 headers" },
	{ "d",	0, 1,	dcmd,	"d\t"		"mark for deletion" },
	{ "F",	1, 1,	Fcmd,	"f\t"		"set message flags [+-][aDdfrSs]" },
	{ "f",	0, 1,	fcmd,	"f\t"		"file message by from address" },
	{ "fq",	0, 1,	fqcmd,	"fq\t"		"print mailbox f appends" },
	{ "H",	0, 0,	Hcmd,	"H\t"		"print message's MIME structure" },
	{ "h",	0, 0,	hcmd,	"h\t"		"print message summary (,h for all)" },
	{ "help", 0, 0,	helpcmd, "help\t"		"print this info" },
	{ "i",	0, 0,	icmd,	"i\t"		"incorporate new mail" },
	{ "k",	1, 1,	kcmd,	"k [flags]\t"	"mark mail" },
	{ "K",	1, 1,	Kcmd,	"K [flags]\t"	"unmark mail" },
	{ "m",	1, 1,	mcmd,	"m addr\t"	"forward mail" },
	{ "M",	1, 0,	mcmd,	"M addr\t"	"forward mail with message" },
	{ "mb",	1, 0,	mbcmd,	"mb mbox\t"	"switch mailboxes" },
	{ "p",	1, 0,	pcmd,	"p\t"		"print the processed message" },
	{ "P",	0, 0,	Pcmd,	"P\t"		"print the raw message" },
	{ "\"",	0, 0,	quotecmd, "\"\t"		"print a quoted version of msg" },
	{ "\"\"",	0, 0,	quotecmd, "\"\"\t"		"format and quote message" },
	{ "q",	0, 0,	qcmd,	"q\t"		"exit and remove all deleted mail" },
	{ "r",	1, 1,	rcmd,	"r [addr]\t"	"reply to sender plus any addrs specified" },
	{ "rf",	1, 0,	rcmd,	"rf [addr]\t"	"file message and reply" },
	{ "R",	1, 0,	rcmd,	"R [addr]\t"	"reply including copy of message" },
	{ "Rf",	1, 0,	rcmd,	"Rf [addr]\t"	"file message and reply with copy" },
	{ "s",	1, 1,	scmd,	"s file\t"		"append raw message to file" },
	{ "t",	1, 0,	tcmd,	"t\t"		"text formatter" },
	{ "u",	0, 0,	ucmd,	"u\t"		"remove deletion mark" },
	{ "w",	1, 1,	wcmd,	"w file\t"		"store message contents as file" },
	{ "x",	0, 0,	xcmd,	"x\t"		"exit without flushing deleted messages" },
	{ "y",	0, 0,	ycmd,	"y\t"		"synchronize with mail box" },
	{ "=",	1, 0,	eqcmd,	"=\t"		"print current message number" },
	{ "|",	1, 1,	pipecmd, "|cmd\t"		"pipe message body to a command" },
	{ "||",	1, 1,	rpipecmd, "||cmd\t"	"pipe raw message to a command" },
	{ "!",	1, 0,	bangcmd, "!cmd\t"		"run a command" },
};

struct Cmd {
	Message	*msgs;
	Mfn	*f;
	int	an;
	char	*av[Narg];
	char	cmdline[2*1024];
	int	delete;
};

int		dir2message(Message*, int, Dirstats*);
int		mdir2message(Message*);
char*		extendp(char*, char*);
char*		parsecmd(char*, Cmd*, Message*, Message*);
void		system(char*, char**, int);
int		switchmb(char *, int);
void		closemb(void);
Message*	dosingleton(Message*, char*);
char*		rooted(char*);
int		plumb(Message*, Ctype*);
void		exitfs(char*);
Message*	flushdeleted(Message*);

int	didopen;
int	doflush;
int	interrupted;
int	longestfrom = 12;
int	longestto = 12;
int	hcmdfmt;
Qid	mbqid;
int	mbvers;
char	mbname[Pathlen];
char	mbpath[Pathlen];
int	natural;
Biobuf	out;
int	reverse;
char	root[Pathlen];
int	rootlen;
int	startedfs;
Message	top;
char	*user;
char	homewd[Pathlen];
char	wd[Pathlen];
char	textfmt[Pathlen];

char*
idfmt(char *p, char *e, Message *m)
{
	char buf[32];
	int sz, l;

	for(; (sz = e - p) > 0; ){
		l = snprint(buf, sizeof buf, "%d", m->id);
		if(l + 1 > sz)
			return "*GOK*";
		e -= l;
		memcpy(e, buf, l);
		if((m = m->parent) == &top)
			break;
		e--;
		*e = '.';
	}
	return e;
}

int
eprint(char *fmt, ...)
{
	int n;
	va_list args;

	Bflush(&out);

	va_start(args, fmt);
	n = vfprint(2, fmt, args);
	va_end(args);
	return n;
}

void
dissappeared(void)
{
	char buf[ERRMAX];

	rerrstr(buf, sizeof buf);
	if(strstr(buf, "hungup channel")){
		eprint("\n!she's dead, jim\n");
		exits(buf);
	}
	eprint("!message dissappeared\n");
}

int
Dfmt(Fmt *f)
{
	char *e, buf[128];
	Message *m;

	m = va_arg(f->args, Message*);
	if(m == nil)
		return fmtstrcpy(f, "*GOK*");
	if(m == &top)
		return 0;
	e = buf + sizeof buf - 1;
	*e = 0;
	return fmtstrcpy(f, idfmt(buf, e, m));
}

char*
readline(char *prompt, char *line, int len)
{
	char *p, *e, *q;
	int n, dump;

	e = line + len;
retry:
	dump = 0;
	interrupted = 0;
	eprint("%s", prompt);
	for(p = line;; p += n){
		if(p == e){
			dump = 1;
			p = line;
		}
		n = read(0, p, e - p);
		if(n < 0){
			if(interrupted)
				goto retry;
			return nil;
		}
		if(n == 0)
			return nil;
		if(q = memchr(p, '\n', n)){
			if(dump){
				eprint("!line too long\n");
				goto retry;
			}
			p = q;
			break;
		}
	}
	*p = 0;
	return line;
}

void
usage(void)
{
	fprint(2, "usage: %s [-nrt] [-f mailfile] [-s mailfile]\n", argv0);
	fprint(2, "       %s -c dir\n", argv0);
	exits("usage");
}

void
catchnote(void*, char *note)
{
	if(strstr(note, "interrupt") != nil){
		interrupted = 1;
		noted(NCONT);
	}
	noted(NDFLT);
}

char*
plural(int n)
{
	if (n == 1)
		return "";
	return "s";	
}

void
main(int argc, char **argv)
{
	char cmdline[2*1024], prompt[64], *err, *av[4], *mb;
	int n, cflag, singleton;
	Cmd cmd;
	Ctype *cp;
	Message *cur, *m, *x;

	Binit(&out, 1, OWRITE);

	mb = nil;
	singleton = 0;
	reverse = 1;
	cflag = 0;
	ARGBEGIN {
	case 'c':
		cflag = 1;
		break;
	case 's':
		singleton = 1;
	case 'f':
		mb = EARGF(usage());
		break;
	case 'r':
		reverse = 0;
		break;
	case 'n':
		natural = 1;
		reverse = 0;
		break;
	case 't':
		hcmdfmt = 1;
		break;
	default:
		usage();
		break;
	} ARGEND;

	fmtinstall('D', Dfmt);
	quotefmtinstall();
	doquote = needsrcquote;
	getwd(homewd, sizeof homewd);
	user = getlog();
	if(user == nil || *user == 0)
		sysfatal("can't read user name");

	if(cflag){
		if(argc > 0)
			n = creatembox(user, argv[0]);
		else
			n = creatembox(user, nil);
		exits(n? 0: "fail");
	}

	if(argc)
		usage();

	if(access("/mail/fs/ctl", 0) < 0){
		startedfs = 1;
		av[0] = "fs";
		av[1] = "-p";
		av[2] = 0;
		system("/bin/upas/fs", av, -1);
	}

	switchmb(mb, singleton);
	top.path = strdup(root);
	for(cp = ctype; cp < ctype + nelem(ctype) - 1; cp++)
		cp->next = cp + 1;

	if(singleton){
		cur = dosingleton(&top, mb);
		if(cur == nil){
			eprint("no message\n");
			exitfs(0);
		}
		pcmd(nil, cur);
	} else {
		cur = &top;
		if(icmd(nil, cur) == nil)
			sysfatal("can't read %s", top.path);
	}

	notify(catchnote);
	for(;;){
		snprint(prompt, sizeof prompt, "%D: ", cur);

		/*
		 * leave space at the end of cmd line in case parsecmd needs to
		 * add a space after a '|' or '!'
		 */
		if(readline(prompt, cmdline, sizeof cmdline - 1) == nil)
			break;
		err = parsecmd(cmdline, &cmd, top.child, cur);
		if(err != nil){
			eprint("!%s\n", err);
			continue;
		}
		if(singleton && (cmd.f == icmd || cmd.f == ycmd)){
			eprint("!illegal command\n");
			continue;
		}
		interrupted = 0;
		if(cmd.msgs == nil || cmd.msgs == &top){
			if(x = cmd.f(&cmd, &top))
				cur = x;
		} else for(m = cmd.msgs; m != nil; m = m->cmd){
			x = m;
			if(cmd.delete){
				dcmd(&cmd, x);

				/*
				 * dp acts differently than all other commands
				 * since its an old lesk idiom that people love.
				 * it deletes the current message, moves the current
				 * pointer ahead one and prints.
				 */
				if(cmd.f == pcmd){
					if(x->next == nil){
						eprint("!address\n");
						cur = x;
						break;
					} else
						x = x->next;
				}
			}
			x = cmd.f(&cmd, x);
			if(x != nil)
				cur = x;
			if(interrupted)
				break;
			if(singleton && (cmd.delete || cmd.f == dcmd))
				qcmd(nil, nil);
		}
		if(doflush)
			cur = flushdeleted(cur);
	}
	qcmd(nil, nil);
}

char*
file2string(char *dir, char *file)
{
	int fd, n;
	char *s, *p, *e;

	p = s = malloc(512);
	e = p + 511;

	fd = open(extendp(dir, file), OREAD);
	while((n = read(fd, p, e - p)) > 0){
		p += n;
		if(p == e){
			s = realloc(s, (n = p - s) + 512 + 1);
			if(s == nil)
				sysfatal("malloc: %r");
			p = s + n;
			e = p + 512;
		}
	}
	close(fd);
	*p = 0;
	return s;
}

#define Fields 		18			/* terrible hack; worth 10% */
#define Minfields	17

void
updateinfo(Message *m)
{
	char *s, *f[Fields + 1];
	int i, n, sticky;

	s = file2string(m->path, "info");
	if(s == nil)
		return;
	if((n = getfields(s, f, nelem(f), 0, "\n")) < Minfields){
		for(i = 0; i < n; i++)
			fprint(2, "info: %s\n", f[i]);
		sysfatal("info file invalid %s %D: %d fields", m->path, m, n);
	}
	if((m->nflags & Nnoflags) == 0){
		sticky = m->flags & Fdeleted;
		m->flags = buftoflags(f[17]) | sticky;
	}
	m->nflags &= ~Nmissing;
	free(s);
}

Message*
file2message(Message *parent, char *name)
{
	char *path, *f[Fields + 1];
	int i, n;
	Message *m;

	m = mallocz(sizeof *m, 1);
	if(m == nil)
		return nil;
	m->path = path = strdup(extendp(parent->path, name));
	m->fileno = atoi(name);
	m->info = file2string(path, "info");
	m->parent = parent;
	n = getfields(m->info, f, nelem(f), 0, "\n");
	if(n < Minfields){
		for(i = 0; i < n; i++)
			fprint(2, "info: [%s]\n", f[i]);
		sysfatal("info file invalid %s %D: %d fields", path, m, n);
	}
	m->from = f[0];
	m->to = f[1];
	m->cc = f[2];
	m->replyto = f[3];
	m->date = f[4];
	m->subject = f[5];
	m->type = f[6];
	m->disposition = f[7];
	m->filename = f[8];
	m->len = strtoul(f[16], 0, 0);
	if(n > 17)
		m->flags = buftoflags(f[17]);
	else
		m->nflags |= Nnoflags;

	if(m->type)
	if(strstr(m->type, "multipart") != nil || strcmp(m->type, "message/rfc822") == 0)
		mdir2message(m);
	return m;
}

void
freemessage(Message *m)
{
	Message *nm, *next;

	for(nm = m->child; nm != nil; nm = next){
		next = nm->next;
		freemessage(nm);
	}
	free(m->path);
	free(m->info);
	free(m);
}

/*
 * read a directory into a list of messages.  at the top level, there may be
 * large gaps in message numbers.  so we need to read the whole directory.
 * and pick out the messages we're interested in.  within a message, subparts
 * are contiguous and if we don't read the header/body/rawbody, we can avoid forcing
 * upas/fs to read the whole message.
 */
int
mdir2message(Message *parent)
{
	char buf[Pathlen];
	int i, highest, newmsgs;
	Dir *d;
	Message *first, *last, *m;

	/* count current entries */
	first = parent->child;
	highest = newmsgs = 0;
	for(last = parent->child; last != nil && last->next != nil; last = last->next)
		if(last->fileno > highest)
			highest = last->fileno;
	if(last != nil)
		if(last->fileno > highest)
			highest = last->fileno;
	for(i = highest + 1;; i++){
		snprint(buf, sizeof buf, "%s/%d", parent->path, i);
		if((d = dirstat(buf)) == nil)
			break;
		if((d->qid.type & QTDIR) == 0){
			free(d);
			continue;
		}
		free(d);
		snprint(buf, sizeof buf, "%d", i);
		m = file2message(parent, buf);
		if(m == nil)
			break;
		m->id = m->fileno;
		newmsgs++;
		if(first == nil)
			first = m;
		else
			last->next = m;
		m->prev = last;
		last = m;
	}
	parent->child = first;
	return newmsgs;
}

/*
 * 99.9% of the time, we don't need to sort this list.
 * however, sometimes email is added to a mailbox
 * out of order.  or, sape copies it back in from the
 * dump.  in this case, we've got to sort.
 *
 * BOTCH.  we're not observing any sort of stable
 * order.  if an old message comes in while upas/fs
 * is running, it will appear out of order.  restarting
 * upas/fs will reorder things.
 */
int
dcmp(Dir *a, Dir *b)
{
	return atoi(a->name) - atoi(b->name);
}

void
itsallsapesfault(Dir *d, int n)
{
	int c, i, r, t;

	/* evade qsort suck */
	r = -1;
	for(i = 0; i < n; i++){
		t = atol(d[i].name);
		if(t > r){
			c = d[i].name[0];
			if(c >= '0' && c <= 9)
				break;
		}
		r = t;
	}
	if(i != n)
		qsort(d, n, sizeof *d, (int (*)(void*, void*))dcmp);
}

int
dir2message(Message *parent, int reverse, Dirstats *s)
{
	int i, c, n, fd;
	Dir *d;
	Message *first, *last, *m, **ll;

	memset(s, 0, sizeof *s);
	fd = open(parent->path, OREAD);
	if(fd < 0)
		return -1;
	first = parent->child;
	last = nil;
	if(first)
		for(last = first; last->next; last = last->next)
			;
	n = dirreadall(fd, &d);
	itsallsapesfault(d, n);
	if(reverse)
		ll = &last;
	else
		ll = &parent->child;
	for(i = 0; *ll || i < n; ){
		if(i < n && (d[i].qid.type & QTDIR) == 0){
			i++;
			continue;
		}
		c = -1;
		if(i >= n)
			c = 1;
		else if(*ll)
			c = atoi(d[i].name) - (*ll)->fileno;
		if(c < 0){
			m = file2message(parent, d[i].name);
			if(m == nil)
				break;
			if(reverse){
				m->next = first;
				if(first != nil)
					first->prev = m;
				first = m;
			}else{
				if(first == nil)
					first = m;
				else
					last->next = m;
				m->prev = last;
				last = m;
			}
			*ll = m;
			s->new++;
			s->unread += (m->flags & Fseen) == 0;
			i++;
		}else if(c > 0){
			(*ll)->nflags |= Nmissing;
			s->del++;
		}else{
			updateinfo(*ll);
			s->old++;
			i++;
		}

		if(reverse)
			ll = &(*ll)->prev;
		else
			ll = &(*ll)->next;
	}
	free(d);
	close(fd);
	parent->child = first;

	/* renumber and file longest from */
	i = 1;
	longestfrom = 12;
	longestto = 12;
	for(m = first; m != nil; m = m->next){
		m->id = natural ? m->fileno : i++;
		n = strlen(m->from);
		if(n > longestfrom)
			longestfrom = n;
		n = strlen(m->to);
		if(n > longestto)
			longestto = n;
	}
	return 0;
}

/*
 *   point directly to a message
 */
Message*
dosingleton(Message *parent, char *path)
{
	char *p, *np;
	Message *m;

	/* walk down to message and read it */
	if(strlen(path) < rootlen)
		return nil;
	if(path[rootlen] != '/')
		return nil;
	p = path + rootlen + 1;
	np = strchr(p, '/');
	if(np != nil)
		*np = 0;
	m = file2message(parent, p);
	if(m == nil)
		return nil;
	parent->child = m;
	m->id = 1;

	/* walk down to requested component */
	while(np != nil){
		*np = '/';
		np = strchr(np + 1, '/');
		if(np != nil)
			*np = 0;
		for(m = m->child; m != nil; m = m->next)
			if(strcmp(path, m->path) == 0)
				return m;
		if(m == nil)
			return nil;
	}
	return m;
}

/*
 *   walk the path name an element
 */
char*
extendp(char *dir, char *name)
{
	static char buf[Pathlen];

	if(strcmp(dir, ".") == 0)
		snprint(buf, sizeof buf, "%s", name);
	else
		snprint(buf, sizeof buf, "%s/%s", dir, name);
	return buf;
}

char*
nosecs(char *t)
{
	char *p;

	p = strchr(t, ':');
	if(p == nil)
		return t;
	p = strchr(p + 1, ':');
	if(p != nil)
		*p = 0;
	return t;
}

char *months[12] =
{
	"jan", "feb", "mar", "apr", "may", "jun",
	"jul", "aug", "sep", "oct", "nov", "dec"
};

int
month(char *m)
{
	int i;

	for(i = 0; i < 12; i++)
		if(cistrcmp(m, months[i]) == 0)
			return i + 1;
	return 1;
}

enum
{
	Yearsecs	= 365*24*60*60,
};

void
cracktime(char *d, char *out, int len)
{
	char in[64], *f[6], *dtime;
	int n;
	long now, then;
	Tm tm;

	*out = 0;
	if(d == nil)
		return;
	strncpy(in, d, sizeof in);
	in[sizeof in - 1] = 0;
	n = getfields(in, f, 6, 1, " \t\r\n");
	if(n != 6){
		/* unknown style */
		snprint(out, 16, "%10.10s", d);
		return;
	}
	now = time(0);
	memset(&tm, 0, sizeof tm);
	if(strchr(f[0], ',') != nil && strchr(f[4], ':') != nil){
		/* 822 style */
		tm.year = atoi(f[3])-1900;
		tm.mon = month(f[2]);
		tm.mday = atoi(f[1]);
		dtime = nosecs(f[4]);
		then = tm2sec(&tm);
	} else if(strchr(f[3], ':') != nil){
		/* unix style */
		tm.year = atoi(f[5])-1900;
		tm.mon = month(f[1]);
		tm.mday = atoi(f[2]);
		dtime = nosecs(f[3]);
		then = tm2sec(&tm);
	} else {
		then = now;
		tm = *localtime(now);
		dtime = "";
	}

	if(now - then < Yearsecs/2)
		snprint(out, len, "%2d/%2.2d %s", tm.mon, tm.mday, dtime);
	else
		snprint(out, len, "%2d/%2.2d  %4d", tm.mon, tm.mday, tm.year + 1900);
}

int
matchtype(char *s, Ctype *t)
{
	return strncmp(t->type, s, strlen(t->type)) == 0;
}

Ctype*
findctype(Message *m)
{
	char *p, ftype[256];
	int n, pfd[2];
	Ctype *a, *cp;

	for(cp = ctype; cp; cp = cp->next)
		if(matchtype(m->type, cp))
			if((cp->flag & Rechk) == 0)
				return cp;
			else
				break;

	if(pipe(pfd) < 0)
		return ctype;
	*ftype = 0;
	switch(fork()){
	case -1:
		break;
	case 0:
		close(pfd[1]);
		close(0);
		dup(pfd[0], 0);
		close(1);
		dup(pfd[0], 1);
		execl("/bin/file", "file", "-m", extendp(m->path, "body"), nil);
		exits(0);
	default:
		close(pfd[0]);
		n = read(pfd[1], ftype, sizeof ftype - 1);
		while(n > 0 && isspace(ftype[n - 1]))
			n--;
		ftype[n] = 0;
		close(pfd[1]);
		waitpid();
		break;
	}
	for(cp = ctype; cp; cp = cp->next)
		if(matchtype(ftype, cp))
			return cp;
	if(*ftype == 0 || (p = strchr(ftype, '/')) == nil)
		return ctype;
	*p++ = 0;

	a = mallocz(sizeof *a, 1);
	a->type = strdup(ftype);
	a->ext = strdup(p);
	a->flag = 0;
	a->plumbdest = strdup(ftype);
	for(cp = ctype; cp->next; cp = cp->next)
		;
	cp->next = a;
	a->next = nil;
	return a;
}

/*
 * traditional
 */
void
hds(char *buf, Message *m)
{
	buf[0] = m->child? 'H': ' ';
	buf[1] = m->flags & Fdeleted ? 'd' : ' ';
	buf[2] = m->flags & Fstored? 's': ' ';
	buf[3] = m->flags & Fseen? ' ': '*';
	if(m->flags & Fanswered)
		buf[3] = 'a';
	if(m->flags & Fflagged)
		buf[3] = '\'';
	buf[4] = 0;
}

void
pheader0(char *buf, int len, Message *m)
{
	char *f, *p, *q, frombuf[40], timebuf[32], h[5];
	int max;

	hds(h, m);
	if(hcmdfmt == 0){
		f = m->from;
		max = longestfrom;
	}else{
		snprint(frombuf, sizeof frombuf-5, "%s", m->to);
		p = strchr(frombuf, ' ');
		if(p != nil)
			snprint(p, 5, " ...");
		f = frombuf;
		max = longestto;
		if(max > sizeof frombuf)
			max = sizeof frombuf;
	}

	if(*f == 0)
		snprint(buf, len, "%3D    %s %6d  %s",
			m, m->type, m->len, m->filename);
	else if(*m->subject){
		q = p = strdup(m->subject);
		while(*p == ' ')
			p++;
		if(strlen(p) > 50)
			p[50] = 0;
		cracktime(m->date, timebuf, sizeof timebuf);
		snprint(buf, len, "%3D %s %6d  %11.11s %-*.*s %s",
			m, h, m->len, timebuf, max, max, f, p);
		free(q);
	} else {
		cracktime(m->date, timebuf, sizeof timebuf);
		snprint(buf, len, "%3D %s %6d  %11.11s %s",
			m, h, m->len, timebuf, f);
	}
}

void
pheader(char *buf, int len, int indent, Message *m)
{
	char *p, *e, typeid[80];

	e = buf + len;
	snprint(typeid, sizeof typeid, "%D    %s", m, m->type);
	if(indent < 6)
		p = seprint(buf, e, "%-32s %-6d ", typeid, m->len);
	else
		p = seprint(buf, e, "%-64s %-6d ", typeid, m->len);
	if(m->filename && *m->filename)
		p = seprint(p, e, "(file,%s)", m->filename);
	if(m->from && *m->from)
		p = seprint(p, e, "(from,%s)", m->from);
	if(m->subject && *m->subject)
		seprint(p, e, "(subj,%s)", m->subject);
}

char sstring[256];

/*
 * 	cmd := range cmd ' ' arg-list ;
 * 	range := address
 * 		| address ',' address
 * 		| 'g' search ;
 * 	address := msgno
 * 		| search ;
 * 	msgno := number
 * 		| number '/' msgno ;
 * 	search := '/' string '/'
 * 		| '%' string '%'
 *		| '#' (field '#')? re '#'
 *
 */
static char*
qstrchr(char *s, int c)
{
	for(;; s++){
		if(*s == '\\')
			s++;
		else if(*s == c)
			return s;
		if(*s == 0)
			return nil;
	}
}

Reprog*
parsesearch(char **pp, char *buf, int bufl)
{
	char *p, *np, *e;
	int c, n;

	buf[0] = 0;
	p = *pp;
	c = *p++;
	if(c == '#')
		snprint(buf, bufl, "from");
	np = qstrchr(p, c);
	if(c == '#' && np)
	if(e = qstrchr(np + 1, c)){
		snprint(buf, bufl, "%.*s", (int)(np - p), p);
		p = np + 1;
		np = e;
	}
	if(np != nil){
		*np++ = 0;
		*pp = np;
	} else {
		n = strlen(p);
		*pp = p + n;
	}
	if(*p == 0)
		p = sstring;
	else{
		strncpy(sstring, p, sizeof sstring);
		sstring[sizeof sstring - 1] = 0;
	}
	return regcomp(p);
}

enum{
	Comma = 1,
};

/*
 *   search a message for a regular expression match
 */
int
fsearch(Message *m, Reprog *prog, char *field)
{
	char buf[4096 + 1];
	int i, fd, rv;
	uvlong o;

	rv = 0;
	fd = open(extendp(m->path, field), OREAD);
	/*
	 *  march through raw message 4096 bytes at a time
	 *  with a 128 byte overlap to chain the re search.
	 */
	for(o = 0;; o += i - 128){
		i = pread(fd, buf, sizeof buf - 1, o);
		if(i <= 0)
			break;
		buf[i] = 0;
		if(regexec(prog, buf, nil, 0)){
			rv = 1;
			break;
		}
		if(i < sizeof buf - 1)
			break;
	}
	close(fd);
	return rv;
}

int
rsearch(Message *m, Reprog *prog, char*)
{
	return fsearch(m, prog, "raw");
}

int
hsearch(Message *m, Reprog *prog, char*)
{
	char buf[256];

	pheader0(buf, sizeof buf, m);
	return regexec(prog, buf, nil, 0);
}

/*
 * ack: returns int (*)(Message*, Reprog*, char*)
 */
int (*
chartosearch(int c))(Message*, Reprog*, char*)
{
	switch(c){
	case '%':
		return rsearch;
	case '/':
	case '?':
		return hsearch;
	case '#':
		return fsearch;
	}
	return 0;
}

char*
parseaddr(char **pp, Message *first, Message *cur, Message *unspec, Message **mp, int f)
{
	char *p, buf[256];
	int n, c, sign;
	Message *m, *m0;
	Reprog *prog;
	int (*fn)(Message*, Reprog*, char*);

	*mp = nil;
	p = *pp;

	sign = 0;
	if(*p == '+'){
		sign = 1;
		p++;
		*pp = p;
	} else if(*p == '-'){
		sign = -1;
		p++;
		*pp = p;
	}

	switch(*p){
	default:
		if(sign){
			n = 1;
			goto number;
		}
		*mp = unspec;
		break;
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		n = strtoul(p, pp, 10);
		if(n == 0){
			if(sign)
				*mp = cur;
			else
				*mp = &top;
			break;
		}
	number:
		m0 = m = nil;
		switch(sign){
		case 0:
			for(m = first; m != nil; m0 = m, m = m->next)
				if(m->id == n)
					break;
			break;
		case -1:
			if(cur != &top)
				for(m = cur; m0 = m, m != nil && n > 0; n--)
					m = m->prev;
			break;
		case 1:
			if(cur == &top){
				n--;
				cur = first;
			}
			for(m = cur; m != nil && n > 0; m0 = m, n--)
				m = m->next;
			break;
		}
		if(m == nil && f&Comma)
			m = m0;
		if(m == nil)
			return "address";
		*mp = m;
		break;
	case '?':
		/* legacy behavior.  no longer needed */
		sign = -1;
	case '%':
	case '/':
	case '#':
		c = *p;
		fn= chartosearch(c);
		prog = parsesearch(pp, buf, sizeof buf);
		if(prog == nil)
			return "badly formed regular expression";
		if(sign == -1){
			for(m = cur == &top ? nil : cur->prev; m; m = m->prev)
				if(fn(m, prog, buf))
					break;
		}else{
			for(m = cur == &top ? first : cur->next; m; m = m->next)
				if(fn(m, prog, buf))
					break;
		}
		if(m == nil)
			return "search";
		*mp = m;
		free(prog);
		break;
	case '$':
		for(m = first; m != nil && m->next != nil; m = m->next)
			;
		*mp = m;
		*pp = p + 1;
		break;
	case '.':
		*mp = cur;
		*pp = p + 1;
		break;
	case ',':
		*mp = first;
		*pp = p;
		break;
	}

	if(*mp != nil && **pp == '.'){
		(*pp)++;
		if((m = (*mp)->child) == nil)
			return "no sub parts";
		return parseaddr(pp, m, m, m, mp, 0);
	}
	c = **pp;
	if(c == '+' || c == '-' || c == '/' || c == '%' || c == '#')
		return parseaddr(pp, first, *mp, *mp, mp, 0);

	return nil;
}

char*
parsecmd(char *p, Cmd *cmd, Message *first, Message *cur)
{
	char buf[256], *err;
	int i, c, r;
	Reprog *prog;
	Message *m, *s, *e, **l, *last;
	int (*f)(Message*, Reprog*, char*);
	static char errbuf[ERRMAX];

	cmd->delete = 0;
	l = &cmd->msgs;
	*l = nil;

	while(*p == ' ' || *p == '\t')
		p++;

	/* null command is a special case (advance and print) */
	if(*p == 0){
		if(cur == &top)
			m = first;
		else {
			/* walk to the next message even if we have to go up */
			m = cur->next;
			while(m == nil && cur->parent != nil){
				cur = cur->parent;
				m = cur->next;
			}
		}
		if(m == nil)
			return "address";
		*l = m;
		m->cmd = nil;
		cmd->an = 0;
		cmd->f = pcmd;
		return nil;
	}

	/* global search ? */
	if(*p == 'g'){
		p++;

		/* no search string means all messages */
		if(*p == 'k'){
			for(m = first; m != nil; m = m->next)
			if(m->flags & Fflagged){
				*l = m;
				l = &m->cmd;
				*l = nil;
			}
			p++;
		}else if(*p != '/' && *p != '%' && *p != '#'){
			for(m = first; m != nil; m = m->next){
				*l = m;
				l = &m->cmd;
				*l = nil;
			}
		}else{
			/* mark all messages matching this search string */
			c = *p;
			f = chartosearch(c);
			prog = parsesearch(&p, buf, sizeof buf);
			if(prog == nil)
				return "badly formed regular expression";
			for(m = first; m != nil; m = m->next){
				if(f(m, prog, buf)){
					*l = m;
					l = &m->cmd;
					*l = nil;
				}
			}
			free(prog);
		}
	}else{
		/* parse an address */
		s = e = nil;
		err = parseaddr(&p, first, cur, cur, &s, 0);
		if(err != nil)
			return err;
		if(*p == ','){
			/* this is an address range */
			if(s == &top)
				s = first;
			p++;
			for(last = s; last != nil && last->next != nil; last = last->next)
				;
			err = parseaddr(&p, first, cur, last, &e, Comma);
			if(err != nil)
				return err;
			/* select all messages in the range */
			r = 0;
			if(s != nil && e != nil && s->id > e->id)
				r = 1;
			while(s != nil){
				*l = s;
				l = &s->cmd;
				*l = nil;
				if(s == e)
					break;
				if(r)
					s = s->prev;
				else
					s = s->next;
			}
			if(s == nil)
				return "null address range";
		} else {
			/* single address */
			if(s != &top){
				*l = s;
				s->cmd = nil;
			}
		}
	}

	while(*p == ' ' || *p == '\t')
		p++;
	/* hack to allow all messages to start with 'd' */
	if(*p == 'd' && p[1]){
		cmd->delete = 1;
		p++;
	}
	while(*p == ' ' || *p == '\t')
		p++;
	if(*p == 0)
		p = "p";
	for(i = nelem(cmdtab) - 1; i >= 0; i--)
		if(strncmp(p, cmdtab[i].cmd, strlen(cmdtab[i].cmd)) == 0)
			goto found;
	return "illegal command";
found:
	p += strlen(cmdtab[i].cmd);
	snprint(cmd->cmdline, sizeof cmd->cmdline, "%s", p);
	cmd->av[0] = cmdtab[i].cmd;
	cmd->an = 1 + tokenize(p, cmd->av + 1, nelem(cmd->av) - 2);
	if(cmdtab[i].args == 0 && cmd->an > 1){
		snprint(errbuf, sizeof errbuf, "%s doesn't take an argument", cmdtab[i].cmd);
		return errbuf;
	}
	cmd->f = cmdtab[i].f;

	if(cmdtab[i].addr && (cmd->msgs == nil || cmd->msgs == &top)){
		snprint(errbuf, sizeof errbuf, "%s requires an address", cmdtab[i].cmd);
		return errbuf;
 	}
	return nil;
}

Message*
aichcmd(Message *m, int indent)
{
	char hdr[256];

	pheader(hdr, sizeof hdr, indent, m);
	Bprint(&out, "%s\n", hdr);
	for(m = m->child; m != nil; m = m->next)
		aichcmd(m, indent + 1);
	return m;
}

Message*
Hcmd(Cmd*, Message *m)
{
	if(m == &top)
		return nil;
	return aichcmd(m, 0);
}

Message*
hcmd(Cmd*, Message *m)
{
	char hdr[256];

	if(m == &top)
		return nil;
	pheader0(hdr, sizeof hdr, m);
	Bprint(&out, "%s\n", hdr);
	return m;
}

Message*
bcmd(Cmd*, Message *m)
{
	int i;
	Message *om;

	om = m;
	if(m == &top)
		m = top.child;
	for(i = 0; i < 10 && m != nil; i++){
		hcmd(nil, m);
		om = m;
		m = m->next;
	}

	return m != nil? m: om;
}

Message*
ncmd(Cmd*, Message *m)
{
	if(m == &top)
		return m->child;
	return m->next;
}

int
writepart(char *m, char *part, char *s)
{
	char *e;
	int fd, n;

	fd = open(extendp(m, part), OWRITE);
	if(fd < 0){
		dissappeared();
		return -1;
	}
	for(e = s + strlen(s); e - s > 0; s += n){
		if((n = write(fd, s, e - s)) <= 0){
			eprint("!writepart:%s: %r\n", part);
			break;
		}
		if(interrupted)
			break;
	}
	close(fd);
	return s == e? 0: -1;
}

Message	*xpipecmd(Cmd*, Message*, char*);

Message*
printfmt(Message *m, char *part, char *cmd)
{
	Cmd c;

	c.an = 2;
	snprint(c.cmdline, sizeof c.cmdline, "%s", cmd);
	Bflush(&out);
	return xpipecmd(&c, m, part);
}

int
printpart0(Message *m, char *part)
{
	char buf[4096];
	int n, fd, tot;

	fd = open(extendp(m->path, part), OREAD);
	if(fd < 0){
		dissappeared();
		return 0;
	}
	tot = 0;
	while((n = read(fd, buf, sizeof buf)) > 0){
		if(interrupted)
			break;
		if(Bwrite(&out, buf, n) <= 0)
			break;
		tot += n;
	}
	close(fd);
	return tot;
}

int
printpart(Message *m, char *part, char *cmd)
{
	if(cmd == nil || cmd[0] == 0)
		return printpart0(m, part);
	printfmt(m, part, cmd);
	return 1;
}

int
printhtml(Message *m)
{
	Cmd c;

	memset(&c, 0, sizeof c);
	c.an = 3;
	snprint(c.cmdline, sizeof c.cmdline, "/bin/htmlfmt -l60 -cutf8");
	eprint("!/bin/htmlfmt\n");
	pipecmd(&c, m);
	return 0;
}

Message*
Pcmd(Cmd*, Message *m)
{
	if(m == &top)
		return &top;
	if(m->parent == &top)
		printpart(m, "unixheader", nil);
	printpart(m, "raw", nil);
	return m;
}

void
compress(char *p)
{
	char *np;
	int last;

	last = ' ';
	for(np = p; *p; p++){
		if(*p != ' ' || last != ' '){
			last = *p;
			*np++ = last;
		}
	}
	*np = 0;
}

void
setflags(Message *m, char *f)
{
	uchar f0;

	f0 = m->flags;
	txflags(f, &m->flags);
	if(f0 != m->flags)
		if((m->nflags & Nnoflags) == 0)
			writepart(m->path, "flags", f);
}

Message*
Fcmd(Cmd *c, Message *m)
{
	int i;

	for(i = 1; i < c->an; i++)
		setflags(m, c->av[i]);
	return m;
}

void
seen(Message *m)
{
	setflags(m, "s");
}

/*
 * sleeze
 */
int
magicpart(Message *m, char *s, char *part)
{
	char buf[4096];
	int n, fd, c;

	fd = open(extendp(s, part), OREAD);
	if(fd < 0){
		if(strcmp(part, "id") == 0)
			Bprint(&out, "%D ", m);
		else if(strcmp(part, "fpath") == 0)
			Bprint(&out, "%s ", rooted(m->path));
		else
			Bprint(&out, "%s ", part);
		return 0;
	}

	c = 0;
	while((n = read(fd, buf, sizeof buf)) > 0){
		c = -1;
		if(interrupted)
			break;
		if(Bwrite(&out, buf, n) <= 0)
			break;
		c = buf[n - 1];
	}
	close(fd);
	if(!interrupted && n != -1 && c != -1)
	if(strstr(part, "body") != nil || strcmp(part, "rawunix") == 0)
		seen(m);
	return c;
}

Message*
pcmd0(Cmd *c, Message *m, int mayplumb, char *tfmt)
{
	char *s, buf[128];
	int i, ch;
	Ctype *cp;
	Message *nm;

	if(m == &top)
		return &top;
	if(c && c->an >= 2){
		ch = 0;
		for(i = 1; i < c->an; i++)
			ch = magicpart(m, m->path, c->av[i]);
		if(ch != '\n')
			Bprint(&out, "\n");
		return m;
	}
	if(m->parent == &top){
		seen(m);
		printpart(m, "unixheader", nil);
	}
	if(printpart(m, "header", nil) > 0)
		Bprint(&out, "\n");
	cp = findctype(m);
	if(cp->flag & Display){
		if(strcmp(m->type, "text/html") == 0)
			printhtml(m);
		else
			printpart(m, "body", tfmt);
	}else if(strcmp(m->type, "multipart/alternative") == 0){
		for(nm = m->child; nm != nil; nm = nm->next){
			cp = findctype(nm);
			if(cp->ext != nil && strncmp(cp->ext, "txt", 3) == 0)
				break;
		}
		if(nm == nil)
			for(nm = m->child; nm != nil; nm = nm->next){
				cp = findctype(nm);
				if(cp->flag & Display)
					break;
			}
		if(nm != nil)
			pcmd0(nil, nm, mayplumb, tfmt);
		else
			hcmd(nil, m);
	}else if(strncmp(m->type, "multipart/", 10) == 0){
		nm = m->child;
		if(nm != nil){
			/* always print first part */
			pcmd0(nil, nm, mayplumb, tfmt);

			for(nm = nm->next; nm != nil; nm = nm->next){
				s = rooted(nm->path);
				cp = findctype(nm);
				pheader(buf, sizeof buf, -1, nm);
				compress(buf);
				if(strcmp(nm->disposition, "inline") == 0){
					if(cp->ext != nil)
						Bprint(&out, "\n--- %s %s/body.%s\n\n",
							buf, s, cp->ext);
					else
						Bprint(&out, "\n--- %s %s/body\n\n",
							buf, s);
					pcmd0(nil, nm, 0, tfmt);
				} else {
					if(cp->ext != nil)
						Bprint(&out, "\n!--- %s %s/body.%s\n",
							buf, s, cp->ext);
					else
						Bprint(&out, "\n!--- %s %s/body\n",
							buf, s);
				}
			}
		} else {
			hcmd(nil, m);
		}
	}else if(strcmp(m->type, "message/rfc822") == 0)
		pcmd(nil, m->child);
	else if(!mayplumb){
	}else if(plumb(m, cp) >= 0){
		Bprint(&out, "\n!--- using plumber to type %s", cp->type);
		if(strcmp(cp->type, m->type) != 0)
			Bprint(&out, " (was %s)", m->type);
		Bprint(&out, "\n");
	}else
		Bprint(&out, "\n!--- cannot display %s\n", cp->type);

	return m;
}

Message*
pcmd(Cmd *c, Message *m)
{
	return pcmd0(c, m, 1, textfmt);
}

Message*
tcmd(Cmd *c, Message *m)
{
	switch(c->an){
	case 1:
		if(textfmt[0] != 0)
			textfmt[0] = 0;
		else
			snprint(textfmt, sizeof textfmt, "%s", "upas/tfmt");
		break;
	default:
		snprint(textfmt, sizeof textfmt, "%s", c->cmdline);
		break;
	}
	eprint("!textfmt %s\n", textfmt);
	return m;
}

void
printpartindented(char *s, char *part, char *indent)
{
	char *p;
	Biobuf *b;

	b = Bopen(extendp(s, part), OREAD);
	if(b == nil){
		dissappeared();
		return;
	}
	while((p = Brdline(b, '\n')) != nil){
		if(interrupted)
			break;
		p[Blinelen(b)-1] = 0;
		if(Bprint(&out, "%s%s\n", indent, p) < 0)
			break;
	}
	Bprint(&out, "\n");
	Bterm(b);
}

void
printpartindent2(char *s, char *part, char *indent)
{
	Cmd c;

	memset(&c, 0, sizeof c);
	snprint(c.cmdline, sizeof c.cmdline, "fmt -q '> ' %s | sed 's/^/%s/g' ",
		rooted(extendp(s, part)), indent);
	Bflush(&out);
	bangcmd(&c, nil);
}

Message*
quotecmd0(Cmd *c, Message *m, void (*p)(char*, char*, char*))
{
	Ctype *cp;
	Message *nm;

	if(m == &top)
		return &top;
	Bprint(&out, "\n");
	if(m->from != nil && *m->from)
		Bprint(&out, "On %s, %s wrote:\n", m->date, m->from);
	cp = findctype(m);
	if(cp->flag & Display)
		p(m->path, "body", "> ");
	else if(strcmp(m->type, "multipart/alternative") == 0){
		for(nm = m->child; nm != nil; nm = nm->next){
			cp = findctype(nm);
			if(cp->ext != nil && strncmp(cp->ext, "txt", 3) == 0)
				break;
		}
		if(nm == nil)
			for(nm = m->child; nm != nil; nm = nm->next){
				cp = findctype(nm);
				if(cp->flag & Display)
					break;
			}
		if(nm != nil)
			quotecmd(c, nm);
	}else if(strncmp(m->type, "multipart/", 10) == 0){
		nm = m->child;
		if(nm != nil){
			cp = findctype(nm);
			if(cp->flag & Display || strncmp(m->type, "multipart/", 10) == 0)
				quotecmd(c, nm);
		}
	}
	return m;
}

Message*
quotecmd(Cmd *c, Message *m)
{
	void (*p)(char*, char*, char*);

	p = printpartindented;
	if(strstr(c->av[0], "\"\"") != nil)
		p = printpartindent2;
	return quotecmd0(c, m, p);
}


/* really delete messages */
Message*
flushdeleted(Message *cur)
{
	char buf[1024], *p, *e, *msg;
	int i, deld, n, fd;
	Message *m, **l;

	doflush = 0;
	deld = 0;

	fd = open("/mail/fs/ctl", ORDWR);
	if(fd < 0){
		eprint("!can't delete mail, opening /mail/fs/ctl: %r\n");
		exitfs(0);
	}
	e = buf + sizeof buf;
	p = seprint(buf, e, "delete %s", mbname);
	n = 0;
	for(l = &top.child; *l != nil;){
		m = *l;
		if((m->nflags & Nmissing) == 0)
		if((m->flags & Fdeleted) == 0){
			l = &(*l)->next;
			continue;
		}

		/* don't return a pointer to a deleted message */
		if(m == cur)
			cur = m->next;
		deld++;
		if(m->flags & Fdeleted){
			msg = strrchr(m->path, '/');
			if(msg == nil)
				msg = m->path;
			else
				msg++;
			if(e - p < 10){
				write(fd, buf, p - buf);
				n = 0;
				p = seprint(buf, e, "delete %s", mbname);
			}
			p = seprint(p, e, " %s", msg);
			n++;
		}
		/* unchain and free */
		*l = m->next;
		if(m->next)
			m->next->prev = m->prev;
		freemessage(m);
	}
	if(n)
		write(fd, buf, p - buf);

	close(fd);

	if(deld)
		Bprint(&out, "!%d message%s deleted\n", deld, plural(deld));

	/* renumber */
	i = 1;
	for(m = top.child; m != nil; m = m->next)
		m->id = natural ? m->fileno : i++;

	/*
	 *  if we're out of messages, go back to first
	 *  if no first, return the fake first
	 */
	if(cur == nil){
		if(top.child)
			return top.child;
		else
			return &top;
	}
	return cur;
}

Message*
mbcmd(Cmd *c, Message*)
{
	char *mb, oldmb[Pathlen];
	Message *m, **l;

	switch(c->an){
	case 1:
		mb = "mbox";
		break;
	case 2:
		mb = c->av[1];
		break;
	default:
		eprint("!usage: mbcmd [mbox]\n");
		return nil;	
	}

	/* flushdeleted(nil); ? */
	for(l = &top.child; *l; ){
		m = *l;
		*l = m->next;
		freemessage(m);
	}
	top.child = nil;

	strcpy(oldmb, mbpath);
	if(switchmb(mb, 0) < 0){
		eprint("!no mb\n");
		if(switchmb(oldmb, 0) < 0){
			eprint("!mb disappeared\n");
			exits("fail");
		}
	}
	icmd(nil, nil);
	interrupted = 1;	/* no looping */
	return &top;
}

Message*
qcmd(Cmd*, Message*)
{
	flushdeleted(nil);
	if(didopen)
		closemb();
	Bflush(&out);
	exitfs(0);
	return nil;
}

Message*
ycmd(Cmd *c, Message *m)
{
	doflush = 1;
	return icmd(c, m);
}

Message*
xcmd(Cmd*, Message*)
{
	exitfs(0);
	return nil;
}

Message*
eqcmd(Cmd*, Message *m)
{
	Bprint(&out, "%D\n", m);
	return m;
}

Message*
dcmd(Cmd*, Message *m)
{
	while(m->parent != &top)
		m = m->parent;
	m->flags |= Fdeleted;
	return m;
}

Message*
ucmd(Cmd*, Message *m)
{
	if(m == &top)
		return nil;
	while(m->parent != &top)
		m = m->parent;
	m->flags &= ~Fdeleted;
	return m;
}

int
skipscan(void)
{
	int r;
	Dir *d;
	static int lastvers = -1;

	d = dirstat(top.path);
	r = d && d->qid.path == mbqid.path && d->qid.vers == mbqid.vers;
	r = r && mbvers == lastvers;
	if(d != nil){
		mbqid = d->qid;
		lastvers = mbvers;
	}
	free(d);
	return r;
}

Message*
icmd(Cmd *c, Message *m)
{
	char buf[128], *p, *e;
	Dirstats s;

	if(skipscan())
		return m;
	if(dir2message(&top, reverse, &s) < 0)
		return nil;
	p = buf;
	e = buf + sizeof buf;
	if(s.new > 0 && c == nil){
		p = seprint(p, e, "%d message%s", s.new, plural(s.new));
		if(s.unread > 0)
			p = seprint(p, e, ", %d unread", s.unread);
	}
	else if(s.new > 0)
		Bprint(&out, "%d new message%s", s.new, plural(s.new));
	if(s.new && s.del)
		p = seprint(p, e, "; ");
	if(s.del > 0)
		p = seprint(p, e, "%d deleted message%s", s.del, plural(s.del));
	if(s.new + s.del)
		p = seprint(p, e, "\n");
	if(p > buf){
		Bflush(&out);
		eprint("%s", buf);
	}
	return m;
}

Message*
kcmd0(Cmd *c, Message *m)
{
	char *f, *s;
	int sticky;

	if(c->an > 2){
		eprint("!usage k [flags]\n");
		return nil;
	}
	if(c->f == kcmd)
		f = "f";
	else
		f = "-f";
	if(c->an == 2)
		f = c->av[1];
	setflags(m, f);
	if(c->an == 2 && (m->nflags & Nnoflags) == 0){
		sticky = m->flags & Fdeleted;
		s = file2string(m->path, "flags");
		m->flags = buftoflags(s) | sticky;
		free(s);
	}
	return m;
}

Message*
kcmd(Cmd *c, Message *m)
{
	return kcmd0(c, m);
}

Message*
Kcmd(Cmd *c, Message *m)
{
	return kcmd0(c, m);
}

Message*
helpcmd(Cmd*, Message *m)
{
	int i;

	Bprint(&out, "Commands are of the form [<range>] <command> [args]\n");
	Bprint(&out, "<range> := <addr> | <addr>','<addr>| 'g'<search>\n");
	Bprint(&out, "<addr> := '.' | '$' | '^' | <number> | <search> | <addr>'+'<addr> | <addr>'-'<addr>\n");
	Bprint(&out, "<search> := 'k' | '/'<re>'/' | '?'<re>'?' | '%%'<re>'%%' | '#' <field> '#' <re> '#' \n");
	Bprint(&out, "<command> :=\n");
	for(i = 0; i < nelem(cmdtab); i++)
		Bprint(&out, "%s\n", cmdtab[i].help);
	return m;
}

/* ed thinks this is a good idea */
void
marshal(char **path, char **argv0)
{
	char *s;

	s = getenv("marshal");
	if(s == nil || *s == 0)
		s = "/bin/upas/marshal";
	*path = s;
	*argv0 = strrchr(s, '/') + 1;
	if(*argv0 == (char*)1)
		*argv0 = s;
}

int
tomailer(char **av)
{
	int pid, i;
	char *p, *a;
	Waitmsg *w;

	switch(pid = fork()){
	case -1:
		eprint("can't fork: %r\n");
		return -1;
	case 0:
		marshal(&p, &a);
		Bprint(&out, "!%s", p);
		for(i = 1; av[i]; i++)
			Bprint(&out, " %q", av[i]);
		Bprint(&out, "\n");
		Bflush(&out);
		av[0] = a;
		chdir(wd);
		exec(p, av);
		eprint("couldn't exec %s\n", p);
		exits(0);
	default:
		w = wait();
		if(w == nil){
			if(interrupted)
				postnote(PNPROC, pid, "die");
			waitpid();
			return -1;
		}
		if(w->msg[0]){
			eprint("mailer failed: %s\n", w->msg);
			free(w);
			return -1;
		}
		free(w);
//		Bprint(&out, "!\n");
		break;
	}
	return 0;
}

/*
 *  like tokenize but obey "" quoting
 */
int
tokenize822(char *str, char **args, int max)
{
	int na, intok, inquote;

	if(max <= 0)
		return 0;
	intok = inquote = 0;
	for(na=0; ;str++)
		switch(*str) {
		case ' ':
		case '\t':
			if(inquote)
				goto Default;
			/* fall through */
		case '\n':
			*str = 0;
			if(!intok)
				continue;
			intok = 0;
			if(na < max)
				continue;
			/* fall through */
		case 0:
			return na;
		case '"':
			inquote ^= 1;
			/* fall through */
		Default:
		default:
			if(intok)
				continue;
			args[na++] = str;
			intok = 1;
		}
}

static char *rec[] = {"Re: ", "AW:", };
static char *fwc[] = {"Fwd: ", };

char*
addrecolon(char **tab, int n, char *s)
{
	char *prefix;
	int i;

	prefix = "";
	for(i = 0; i < n; i++)
		if(cistrncmp(s, tab[i], strlen(tab[i]) - 1) == 0)
			break;
	if(i == n)
		prefix = tab[0];
	return smprint("%s%s", prefix, s);
}

Message*
rcmd(Cmd *c, Message *m)
{
	char *from, *path, *subject, *rpath, *addr, *av[128];
	int i, ai;
	Message *nm;

	ai = 1;
	av[ai++] = "-8";
	addr = path = subject = nil;
	for(nm = m; nm != &top; nm = nm->parent)
 		if(*nm->replyto != 0){
			addr = nm->replyto;
			break;
		}
	if(addr == nil){
		eprint("!no reply address\n");
		return nil;
	}

	if(nm == &top){
		print("!noone to reply to\n");
		return nil;
	}

	for(nm = m; nm != &top; nm = nm->parent)
		if(*nm->subject){
			av[ai++] = "-s";
			subject = addrecolon(rec, nelem(rec), nm->subject);
			av[ai++] = subject;
			break;
		}

	av[ai++] = "-R";
	av[ai++] = rpath = strdup(rooted(m->path));

	if(strchr(c->av[0], 'f') != nil){
		fcmd(c, m);
		av[ai++] = "-F";
	}

	if(strchr(c->av[0], 'R') != nil){
		av[ai++] = "-t";
		av[ai++] = "message/rfc822";
		av[ai++] = "-A";
		path = strdup(rooted(extendp(m->path, "raw")));
		av[ai++] = path;
	}

	for(i = 1; i < c->an && ai < nelem(av)-1; i++)
		av[ai++] = c->av[i];
	ai += tokenize822(from = strdup(addr), &av[ai], nelem(av) - ai);
	av[ai] = 0;
	if(tomailer(av) == -1)
		m = nil;
	else
		m->flags |= Fanswered;
	free(path);
	free(rpath);
	free(subject);
	free(from);
	return m;
}

Message*
mcmd(Cmd *c, Message *m)
{
	char *subject, *av[128];
	int i, ai;

	if(c->an < 2){
		eprint("!usage: M list-of addresses\n");
		return nil;
	}

	ai = 1;
	subject = nil;
	if(m->subject){
		av[ai++] = "-s";
		subject = addrecolon(fwc, nelem(fwc), m->subject);
		av[ai++] = subject;
	}

	av[ai++] = "-t";
	if(m->parent == &top)
		av[ai++] = "message/rfc822";
	else
		av[ai++] = "mime";

	av[ai++] = "-A";
	av[ai++] = rooted(extendp(m->path, "raw"));
	if(strchr(c->av[0], 'M') == nil)
		av[ai++] = "-n";
	else
		av[ai++] = "-8";
	for(i = 1; i < c->an && ai < nelem(av)-1; i++)
		av[ai++] = c->av[i];
	av[ai] = 0;

	if(tomailer(av) == -1)
		m = nil;
	else
		m->flags |= Fanswered;
	free(subject);
	return m;
}

Message*
acmd(Cmd *c, Message *m)
{
	char *av[128], *rpath, *subject, *from, *to, *cc;
	int i, ai;

	if(m->from == nil || m->to == nil || m->cc == nil){
		eprint("!bad message\n");
		return nil;
	}

	ai = 1;
	av[ai++] = "-8";
	av[ai++] = "-R";
	av[ai++] = rpath = strdup(rooted(m->path));

	subject = nil;
	if(m->subject && *m->subject){
		av[ai++] = "-s";
		subject = addrecolon(rec, nelem(rec), m->subject);
		av[ai++] = subject;
	}

	if(strchr(c->av[0], 'A') != nil){
		av[ai++] = "-t";
		av[ai++] = "message/rfc822";
		av[ai++] = "-A";
		av[ai++] = rooted(extendp(m->path, "raw"));
	}

	for(i = 1; i < c->an && ai < nelem(av)-1; i++)
		av[ai++] = c->av[i];
	ai += tokenize822(from = strdup(m->from), &av[ai], nelem(av) - ai);
	ai += tokenize822(to = strdup(m->to), &av[ai], nelem(av) - ai);
	ai += tokenize822(cc = strdup(m->cc), &av[ai], nelem(av) - ai);
	av[ai] = 0;
	if(tomailer(av) == -1)
		m = nil;
	else
		m->flags |= Fanswered;
	free(from);
	free(to);
	free(cc);
	free(subject);
	free(rpath);
	return m;
}

int
appendtofile(Message *m, char *part, char *base, int mbox, int f)
{
	char *folder, path[Pathlen];
	int in, rv, rp;

	in = open(extendp(m->path, part), OREAD);
	if(in == -1){
		dissappeared();
		return -1;
	}
	rp = 0;
	if(*base == '/')
		folder = base;
	else if(!mbox){
		snprint(path, sizeof path, "%s/%s", wd, base);
		folder = path;
		rp = 1;
	}else if(f)
		folder = ffoldername(mbpath, user, base);
	else
		folder = foldername(mbpath, user, base);
	if(folder == nil)
		return -1;
	if(mbox)
		rv = fappendfolder(0, 0, folder, in);
	else
		rv = fappendfile(m->from, folder, in);
	close(in);
	if(rv >= 0){
		eprint("!saved in %s\n", rp? base: folder);
		setflags(m, "S");
	}else
		eprint("!error %r\n");
	return rv;
}

Message*
scmd(Cmd *c, Message *m)
{
	char *file;

	switch(c->an){
	case 1:
		file = "stored";
		break;
	case 2:
		file = c->av[1];
		break;
	default:
		eprint("!usage: s filename\n");
		return nil;
	}

	if(appendtofile(m, "rawunix", file, 1, 0) < 0)
		return nil;
	return m;
}

Message*
wcmd(Cmd *c, Message *m)
{
	char *file;

	switch(c->an){
	case 2:
		file = c->av[1];
		break;
	case 1:
		if(*m->filename == 0){
			eprint("!usage: w filename\n");
			return nil;
		}
		file = strrchr(m->filename, '/');
		if(file != nil)
			file++;
		else
			file = m->filename;
		break;
	default:
		eprint("!usage: w filename\n");
		return nil;
	}

	if(appendtofile(m, "body", file, 0, 0) < 0)
		return nil;
	return m;
}

typedef struct Xtab Xtab;
struct Xtab {
	char	*a;
	char	*b;
};
Xtab	*xtab;
int	nxtab;

void
loadxfrom(int fd)
{
	char *f[3], *s, *p;
	int n, a, inc;
	Biobuf b;
	Xtab *x;

	Binit(&b, fd, OREAD);
	a = 0;
	inc = 100;
	for(; s = Brdstr(&b, '\n', 1);){
		if(p = strchr(s, '#'))
			*p = 0;
		n = tokenize(s, f, nelem(f));
		if(n != 2){
			free(s);
			continue;
		}
		if(nxtab == a){
			a += inc;
			xtab = realloc(xtab, a*sizeof *xtab);
			if(xtab == nil)
				sysfatal("realloc: %r");
			inc *= 2;
		}
		for(x = xtab+nxtab; x > xtab && strcmp(x[-1].a, f[0]) > 0; x--)
			x[0] = x[-1];
		x->a = f[0];
		x->b = f[1];
		nxtab++;
	}
}

char*
frombox(char *from)
{
	char *s;
	int n, m, fd;
	Xtab *t, *p;
	static int once;

	if(once == 0){
		once = 1;
		s = foldername(mbpath, user, "fromtab-");
		fd = open(s, OREAD);
		if(fd != -1)
			loadxfrom(fd);
		close(fd);
	}
	t = xtab;
	n = nxtab;
	while(n > 1) {
		m = n/2;
		p = t + m;
		if(strcmp(from, p->a) > 0){
			t = p;
			n = n - m;
		} else
			n = m;
	}
	if(n && strcmp(from, t->a) == 0)
		return t->b;
	return from;
}

Message*
fcmd(Cmd*, Message *m)
{
	char *f;

	f = frombox(m->from);
	if(appendtofile(m, "rawunix", f, 1, 1) < 0)
		return nil;
	return m;
}

Message*
fqcmd(Cmd*, Message *m)
{
	char *f;

	f = frombox(m->from);
	Bprint(&out, "! %s\n", f);
	return m;
}

void
system(char *cmd, char **av, int in)
{
	int pid;

	switch(pid=fork()){
	case -1:
		return;
	case 0:
		if(in >= 0){
			close(0);
			dup(in, 0);
			close(in);
		}
		if(wd[0] != 0)
			chdir(wd);
		exec(cmd, av);
		eprint("!couldn't exec %s\n", cmd);
		exits(0);
	default:
		if(in >= 0)
			close(in);
		while(waitpid() < 0){
			if(!interrupted)
				break;
			postnote(PNPROC, pid, "die");
			continue;
		}
		break;
	}
}

Message*
bangcmd(Cmd *c, Message *m)
{
	char *av[4];

	av[0] = "rc";
	av[1] = "-c";
	av[2] = c->cmdline;
	av[3] = 0;
	system("/bin/rc", av, -1);
//	Bprint(&out, "!\n");
	return m;
}

Message*
xpipecmd(Cmd *c, Message *m, char *part)
{
	char *av[4];
	int fd;

	if(c->an < 2){
		eprint("!usage: | cmd\n");
		return nil;
	}

	fd = open(extendp(m->path, part), OREAD);
	if(fd < 0){
		dissappeared();
		return nil;
	}

	av[0] = "rc";
	av[1] = "-c";
	av[2] = c->cmdline;
	av[3] = 0;
	system("/bin/rc", av, fd);	/* system closes fd */
//	Bprint(&out, "!\n");
	return m;
}

Message*
pipecmd(Cmd *c, Message *m)
{
	return xpipecmd(c, m, "body");
}

Message*
rpipecmd(Cmd *c, Message *m)
{
	return xpipecmd(c, m, "rawunix");
}

void
closemb(void)
{
	int fd;

	fd = open("/mail/fs/ctl", ORDWR);
	if(fd < 0)
		sysfatal("can't open /mail/fs/ctl: %r");

	/* close current mailbox */
	if(*mbname && strcmp(mbname, "mbox") != 0)
	if(fprint(fd, "close %q", mbname) == -1)
		eprint("!close %q: %r", mbname);

	close(fd);
}

static char*
chop(char *s, int c)
{
	char *p;

	p = strrchr(s, c);
	if(p != nil && p > s) {
		*p = 0;
		return p - 1;
	}
	return 0;
}

/* sometimes opens the file (or open mbox) intended. */
int
switchmb(char *mb, int singleton)
{
	char *p, *e, pbuf[Pathlen], buf[Pathlen], file[Pathlen];
	int fd, abs;

	closemb();
	abs = 0;
	if(mb == nil)
		mb = "mbox";
	if(strcmp(mb, ".") == 0)	/* botch */
		mb = homewd;
	if(*mb == '/' || strncmp(mb, "./", 2) == 0 || strncmp(mb, "../", 3) == 0){
		snprint(file, sizeof file, "%s", mb);
		abs = 1;
	}else
		snprint(file, sizeof file, "/mail/fs/%s", mb);
	if(singleton){
		if(chop(file, '/') == nil || (p = strrchr(file, '/')) == nil || p - file < 2){
			eprint("!bad mbox name\n");
			return -1;
		}
		mboxpathbuf(pbuf, sizeof pbuf, user, "mbox");
		snprint(mbname, sizeof mbname, "%s", p + 1);
	}else if(abs || access(file, 0) < 0){
		fd = open("/mail/fs/ctl", ORDWR);
		if(fd < 0)
			sysfatal("can't open /mail/fs/ctl: %r");
		p = pbuf;
		e = pbuf + sizeof pbuf;
		if(abs && *file != '/')
			seprint(p, e, "%s/%s", getwd(buf, sizeof buf), mb);
		else if(abs)
			seprint(p, e, "%s", mb);
		else
			mboxpathbuf(pbuf, sizeof pbuf, user, mb);
		/* make up a handle to use when talking to fs */
		if((p = strrchr(mb, '/')) == nil)
			p = mb - 1;
		snprint(mbname, sizeof mbname, "%s%ld", p + 1, time(0));
		if(fprint(fd, "open %q %q", pbuf, mbname) < 0){
			eprint("!can't open %q %q: %r\n", pbuf, mbname);
			return -1;
		}
		close(fd);
		didopen = 1;
	}else{
		mboxpathbuf(pbuf, sizeof pbuf, user, mb);
		strcpy(mbname, mb);
	}

	snprint(root, sizeof root, "/mail/fs/%s", mbname);
	if(getwd(wd, sizeof wd) == nil)
		wd[0] = 0;
	if(!singleton && chdir(root) >= 0)
		strcpy(root, ".");
	rootlen = strlen(root);
	snprint(mbpath, sizeof mbpath, "%s", pbuf);
	memset(&mbqid, 0, sizeof mbqid);
	mbvers++;

	return 0;
}

char*
rooted(char *s)
{
	static char buf[Pathlen];

	if(strcmp(root, ".") != 0)
		return s;
	snprint(buf, sizeof buf, "/mail/fs/%s/%s", mbname, s);
	return buf;
}

int
plumb(Message *m, Ctype *cp)
{
	char *s;
	Plumbmsg *pm;
	static int fd = -2;

	if(cp->plumbdest == nil)
		return -1;
	if(fd < -1)
		fd = plumbopen("send", OWRITE);
	if(fd < 0)
		return -1;

	pm = mallocz(sizeof *pm, 1);
	pm->src = strdup("mail");
	if(*cp->plumbdest)
		pm->dst = strdup(cp->plumbdest);
	pm->type = strdup("text");
	pm->ndata = -1;
	s = rooted(extendp(m->path, "body"));
	if(cp->ext != nil)
		pm->data  = smprint("%s.%s", s, cp->ext);
	else
		pm->data  = strdup(s);
	plumbsend(fd, pm);
	plumbfree(pm);
	return 0;
}

void
regerror(char*)
{
}

void
exitfs(char *rv)
{
	if(startedfs)
		unmount(nil, "/mail/fs");
	chdir(homewd);			/* prof */
	exits(rv);
}
