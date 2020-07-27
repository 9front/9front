/*
 * todo:
 * 1.	sync with imap server's flags
 * 2.	better algorithm for avoiding downloading message list.
 * 3.	get sender — eating envelope is lots of work!
 */
#include "common.h"
#include <libsec.h>
#include <auth.h>
#include "dat.h"

#define	idprint(i, ...)	if(i->flags & Fdebug) fprint(2, __VA_ARGS__); else {}
#pragma varargck argpos	imap4cmd	2
#pragma varargck	type	"Z"		char*
#pragma varargck	type	"U"		uvlong
#pragma varargck	type	"U"		vlong

static char	confused[]	= "confused about fetch response";
static char	qsep[]		= " \t\r\n";
static char	Eimap4ctl[]	= "bad imap4 control message";

enum{
	/* cap */
	Cnolog	= 1<<0,
	Ccram	= 1<<1,
	Cntlm	= 1<<2,

	/* flags */
	Fssl	= 1<<0,
	Fdebug	= 1<<1,
	Fgmail	= 1<<2,
};

typedef struct {
	uvlong	uid;
	ulong	sizes;
	ulong	dates;
	uint	flags;
} Fetchi;

typedef struct Imap Imap;
struct Imap {
	char	*mbox;
	/* free this to free the strings below */
	char	*freep;
	char	*host;
	char	*user;

	int	refreshtime;
	uchar	cap;
	uchar	flags;

	ulong	tag;
	ulong	validity;
	ulong	newvalidity;
	int	nmsg;
	int	size;

	/*
	 * These variables are how we keep track
	 * of what's been added or deleted. They
	 * keep a count of the number of uids we
	 * have processed this sync (nuid), and
	 * the number we processed last sync
	 * (muid).
	 *
	 * We keep the latest imap state in fetchi,
	 * and imap4read syncs the information in
	 * it with the messages. That's how we know
	 * something changed on the server.
	 */
	Fetchi	*f;
	int	nuid;
	int	muid;

	/* open network connection */
	Biobuf	bin;
	Biobuf	bout;
	int	binit;
	int	fd;
};

enum
{
	Qok = 0,
	Qquote,
	Qbackslash,
};

static int
needtoquote(Rune r)
{
	if(r >= Runeself)
		return Qquote;
	if(r <= ' ')
		return Qquote;
	if(r == '(' || r == ')' || r == '{' || r == '%' || r == '*' || r == ']')
		return Qquote;
	if(r == '\\' || r == '"')
		return Qbackslash;
	return Qok;
}

static int
Zfmt(Fmt *f)
{
	char *s, *t;
	int w, quotes;
	Rune r;

	s = va_arg(f->args, char*);
	if(s == 0 || *s == 0)
		return fmtstrcpy(f, "\"\"");

	quotes = 0;
	for(t = s; *t; t += w){
		w = chartorune(&r, t);
		quotes |= needtoquote(r);
	}
	if(quotes == 0)
		return fmtstrcpy(f, s);

	fmtrune(f, '"');
	for(t = s; *t; t += w){
		w = chartorune(&r, t);
		if(needtoquote(r) == Qbackslash)
			fmtrune(f, '\\');
		fmtrune(f, r);
	}
	return fmtrune(f, '"');
}

static int
Ufmt(Fmt *f)
{
	char buf[20*2 + 2];
	ulong a, b;
	uvlong u;

	u = va_arg(f->args, uvlong);
	if(u == 1)
		return fmtstrcpy(f, "nil");
	if(u == 0)
		return fmtstrcpy(f, "-");
	a = u>>32;
	b = u;
	snprint(buf, sizeof buf, "%lud:%lud", a, b);
	return fmtstrcpy(f, buf);
}

static void
imap4cmd(Imap *imap, char *fmt, ...)
{
	char buf[256], *p;
	va_list va;

	va_start(va, fmt);
	p = buf + sprint(buf, "9x%lud ", imap->tag);
	vseprint(p, buf + sizeof buf, fmt, va);
	va_end(va);

	p = buf + strlen(buf);
	if(p > buf + sizeof buf - 3)
		sysfatal("imap4 command too long");
	idprint(imap, "-> %s\n", buf);
	strcpy(p, "\r\n");
	Bwrite(&imap->bout, buf, strlen(buf));
	Bflush(&imap->bout);
}

enum {
	Ok,
	No,
	Bad,
	Bye,
	Exists,
	Status,
	Fetch,
	Cap,
	Auth,
	Expunge,

	Unknown,
};

static char *verblist[] = {
	[Ok]		"ok",
	[No]		"no",
	[Bad]		"bad",
	[Bye]		"bye",
	[Exists]	"exists",
	[Status]	"status",
	[Fetch]		"fetch",
	[Cap]		"capability",
	[Auth]		"authenticate",
	[Expunge]	"expunge",
};

static int
verbcode(char *verb)
{
	int i;
	char *q;

	if(q = strchr(verb, ' '))
		*q = '\0';
	for(i = 0; i < nelem(verblist); i++)
		if(strcmp(verblist[i], verb) == 0)
			break;
	if(q)
		*q = ' ';
	return i;
}

static vlong
mkuid(Imap *i, char *id)
{
	vlong v;

	idprint(i, "mkuid: validity: %lud, idstr: '%s', val: %lud\n", i->validity, id, strtoul(id, 0, 10));
	v = (vlong)i->validity<<32;
	return v | strtoul(id, 0, 10);
}

static vlong
xnum(char *s, int a, int b)
{
	vlong v;

	if(*s != a)
		return -1;
	v = strtoull(s + 1, &s, 10);
	if(*s != b)
		return -1;
	return v;
}

static struct{
	char	*flag;
	int	e;
} ftab[] = {
	"\\Answered",	Fanswered,
	"\\Deleted",	Fdeleted,
	"\\Draft",	Fdraft,
	"\\Flagged",	Fflagged,
	"\\Recent",	Frecent,
	"\\Seen",	Fseen,
	"\\Stored",	Fstored,
};

static int
parseflags(char *s)
{
	char *f[10];
	int i, j, n, r;

	r = 0;
	n = tokenize(s, f, nelem(f));
	for(i = 0; i < n; i++)
		for(j = 0; j < nelem(ftab); j++)
			if(cistrcmp(f[i], ftab[j].flag) == 0)
				r |= ftab[j].e;
	return r;
}

/* "17-Jul-1996 02:44:25 -0700" */
long
internaltounix(char *s)
{
	Tm tm;
	if(strlen(s) < 20 || s[2] != '-' || s[6] != '-')
		return -1;
	s[2] = ' ';
	s[6] = ' ';
	if(strtotm(s, &tm) == -1)
		return -1;
	return tm2sec(&tm);
}
	
static char*
qtoken(char *s, char *sep)
{
	int quoting;
	char *t;

	quoting = 0;
	t = s;	/* s is output string, t is input string */
	while(*t != '\0' && (quoting || utfrune(sep, *t) == nil)){
		if(*t != '"' && *t  != '(' && *t != ')'){
			*s++ = *t++;
			continue;
		}
		/* *t is a quote */
		if(!quoting || *t == '('){
			quoting++;
			t++;
			continue;
		}
		/* quoting and we're on a quote */
		if(t[1] != '"'){
			/* end of quoted section; absorb closing quote */
			t++;
			if(quoting > 0)
				quoting--;
			continue;
		}
		/* doubled quote; fold one quote into two */
		t++;
		*s++ = *t++;
	}
	if(*s != '\0'){
		*s = '\0';
		if(t == s)
			t++;
	}
	return t;
}

int
imaptokenize(char *s, char **args, int maxargs)
{
	int nargs;

	for(nargs=0; nargs < maxargs; nargs++){
		while(*s != '\0' && utfrune(qsep, *s) != nil)
			s++;
		if(*s == '\0')
			break;
		args[nargs] = s;
		s = qtoken(s, qsep);
	}

	return nargs;
}

static char*
fetchrsp(Imap *imap, char *p, Mailbox *, Message *m, int idx)
{
	char *f[15], *s, *q;
	int i, n, a;
	ulong o, l;
	uvlong v;
	static char error[256];
	extern void msgrealloc(Message*, ulong);

	if(idx < 0 || idx >= imap->muid){
		snprint(error, sizeof error, "fetchrsp: bad idx %d", idx);
		return error;
	}

redux:
	n = imaptokenize(p, f, nelem(f));
	if(n%2)
		return confused;
	for(i = 0; i < n; i += 2){
		if(strcmp(f[i], "internaldate") == 0){
			l = internaltounix(f[i + 1]);
			if(l < 418319360)
				abort();
			if(idx < imap->muid)
				imap->f[idx].dates = l;
		}else if(strcmp(f[i], "rfc822.size") == 0){
			l = strtoul(f[i + 1], 0, 0);
			if(m)
				m->size = l;
			else if(idx < imap->muid)
				imap->f[idx].sizes = l;
		}else if(strcmp(f[i], "uid") == 0){
			v = mkuid(imap, f[i + 1]);
			if(m)
				m->imapuid = v;
			if(idx < imap->muid)
				imap->f[idx].uid = v;
		}else if(strcmp(f[i], "flags") == 0){
			l = parseflags(f[i + 1]);
			if(m)
				m->flags = l;
			if(idx < imap->muid)
				imap->f[idx].flags = l;
		}else if(strncmp(f[i], "body[]", 6) == 0){
			s = f[i]+6;
			o = 0;
			if(*s == '<')
				o = xnum(s, '<', '>');
			if(o == -1)
				return confused;
			l = xnum(f[i + 1], '{', '}');
			a = o + l - m->ibadchars - m->size;
			if(a > 0){
				assert(imap->flags & Fgmail);
				m->size = o + l;
				msgrealloc(m, m->size);
				m->size -= m->ibadchars;
			}
			if(Bread(&imap->bin, m->start + o, l) != l){
				snprint(error, sizeof error, "read: %r");
				return error;
			}
			if(Bgetc(&imap->bin) == ')'){
				while(Bgetc(&imap->bin) != '\n')
					;
				return 0;
			}
			/* evil */
			if(!(p = Brdline(&imap->bin, '\n')))
				return 0;
			q = p + Blinelen(&imap->bin);
			while(q > p && (q[-1] == '\n' || q[-1] == '\r'))
				q--;
			*q = 0;
			lowercase(p);
			idprint(imap, "<- %s\n", p);

			goto redux;
		}else
			return confused;
	}
	return nil;
}

void
parsecap(Imap *imap, char *s)
{
	char *t[32], *p;
	int n, i;

	s = strdup(s);
	n = getfields(s, t, nelem(t), 0, " ");
	for(i = 0; i < n; i++){
		if(strncmp(t[i], "auth=", 5) == 0){
			p = t[i] + 5;
			if(strcmp(p, "cram-md5") == 0)
				imap->cap |= Ccram;
			if(strcmp(p, "ntlm") == 0)
				imap->cap |= Cntlm;
		}else if(strcmp(t[i], "logindisabled") == 0)
			imap->cap |= Cnolog;
	}
	free(s);
}

/*
 *  get imap4 response line.  there might be various
 *  data or other informational lines mixed in.
 */
static char*
imap4resp0(Imap *imap, Mailbox *mb, Message *m)
{
	char *e, *line, *p, *ep, *op, *q, *verb;
	int n, idx, unexp;
	static char error[256];

	unexp = 0;
	while(p = Brdline(&imap->bin, '\n')){
		ep = p + Blinelen(&imap->bin);
		while(ep > p && (ep[-1] == '\n' || ep[-1] == '\r'))
			*--ep = '\0';
		idprint(imap, "<- %s\n", p);
		if(unexp && p[0] != '9' && p[1] != 'x')
		if(strtoul(p + 2, &p, 10) != imap->tag)
			continue;
		if(p[0] != '+')
			lowercase(p);		/* botch */

		switch(p[0]){
		case '+':				/* cram challenge */
			if(ep - p > 2)
				return p + 2;
			break;
		case '*':
			if(p[1] != ' ')
				continue;
			p += 2;
			line = p;
			n = strtol(p, &p, 10);
			if(*p == ' ')
				p++;
			verb = p;
	
			if(p = strchr(verb, ' '))
				p++;
			else
				p = verb + strlen(verb);

			switch(verbcode(verb)){
			case Bye:
				/* early disconnect */
				snprint(error, sizeof error, "%s", p);
				return error;
			case Ok:
			case No:
			case Bad:
				/* human readable text at p; */
				break;
			case Exists:
				imap->nmsg = n;
				break;
			case Cap:
				parsecap(imap, p);
				break;
			case Status:
				/* * status inbox (messages 2 uidvalidity 960164964) */
				if(q = strstr(p, "messages"))
					imap->nmsg = strtoul(q + 8, 0, 10);
				if(q = strstr(p, "uidvalidity"))
					imap->newvalidity = strtoul(q + 11, 0, 10);
				break;
			case Fetch:
				if(*p == '('){
					p++;
					if(ep[-1] == ')')
						*--ep = 0;
				}
				if(e = fetchrsp(imap, p, mb, m, n - 1))
					eprint("imap: fetchrsp: %s\n", e);
				if(n > 0 && n <= imap->muid && n > imap->nuid)
					imap->nuid = n;
				break;
			case Expunge:
				if(n < 1 || n > imap->muid){
					snprint(error, sizeof(error), "bad expunge %d (nmsg %d)", n, imap->nuid);
					return error;
				}
				idx = n - 1;
				memmove(&imap->f[idx], &imap->f[idx + 1], (imap->nmsg - idx - 1)*sizeof(imap->f[0]));
				imap->nmsg--;
				imap->nuid--;
				break;
			case Auth:
				break;
			}
			if(imap->tag == 0)
				return line;
			break;
		case '9':		/* response to our message */
			op = p;
			if(p[1] == 'x' && strtoul(p + 2, &p, 10) == imap->tag){
				while(*p == ' ')
					p++;
				imap->tag++;
				return p;
			}
			eprint("imap: expected %lud; got %s\n", imap->tag, op);
			break;
		default:
			if(imap->flags&Fdebug || *p){
				eprint("imap: unexpected line: %s\n", p);
				unexp = 1;
			}
		}
	}
	snprint(error, sizeof error, "i/o error: %r\n");
	return error;
}

static char*
imap4resp(Imap *i)
{
	return imap4resp0(i, 0, 0);
}

static int
isokay(char *resp)
{
	return cistrncmp(resp, "OK", 2) == 0;
}

static char*
findflag(int idx)
{
	int i;

	for(i = 0; i < nelem(ftab); i++)
		if(ftab[i].e == 1<<idx)
			return ftab[i].flag;
	return nil;
}

static void
imap4modflags(Mailbox *mb, Message *m, int flags)
{
	char buf[128], *p, *e, *fs;
	int i, f;
	Imap *imap;

	imap = mb->aux;
	e = buf + sizeof buf;
	p = buf;
	f = flags & ~Frecent;
	for(i = 0; i < Nflags; i++)
		if(f & 1<<i && (fs = findflag(i)))
			p = seprint(p, e, "%s ", fs);
	if(p > buf){
		p[-1] = 0;
		imap4cmd(imap, "uid store %lud flags (%s)", (ulong)m->imapuid, buf);
		imap4resp0(imap, mb, m);
	}
}

static char*
imap4cram(Imap *imap)
{
	char *s, *p, ch[128], usr[64], rbuf[128], ubuf[128], ebuf[192];
	int i, n, l;

	fmtinstall('[', encodefmt);

	imap4cmd(imap, "authenticate cram-md5");
	p = imap4resp(imap);
	if(p == nil)
		return "no challenge";
	l = dec64((uchar*)ch, sizeof ch, p, strlen(p));
	if(l == -1)
		return "bad base64";
	ch[l] = 0;
	idprint(imap, "challenge [%s]\n", ch);

	if(imap->user == nil)
		imap->user = getlog();
	n = auth_respond(ch, l, usr, sizeof usr, rbuf, sizeof rbuf, auth_getkey,
		"proto=cram role=client server=%q user=%s", imap->host, imap->user);
	if(n == -1)
		return "cannot find IMAP password";
	for(i = 0; i < n; i++)
		rbuf[i] = tolower(rbuf[i]);
	l = snprint(ubuf, sizeof ubuf, "%s %.*s", usr, utfnlen(rbuf, n), rbuf);
	idprint(imap, "raw cram [%s]\n", ubuf);
	snprint(ebuf, sizeof ebuf, "%.*[", l, ubuf);

	imap->tag = 1;
	idprint(imap, "-> %s\n", ebuf);
	Bprint(&imap->bout, "%s\r\n", ebuf);
	Bflush(&imap->bout);

	if(!isokay(s = imap4resp(imap)))
		return s;
	return nil;
}

/*
 *  authenticate to IMAP4 server using NTLM (untested)
 * 
 *  http://davenport.sourceforge.net/ntlm.html#ntlmImapAuthentication
 *  http://msdn.microsoft.com/en-us/library/cc236621%28PROT.13%29.aspx
 */
static uchar*
psecb(uchar *p, uint o, int n)
{
	p[0] = n;
	p[1] = n>>8;
	p[2] = n;
	p[3] = n>>8;
	p[4] = o;
	p[5] = o>>8;
	p[6] = o>>16;
	p[7] = o>>24;
	return p+8;
}

static uchar*
psecq(uchar *q, char *s, int n)
{
	memcpy(q, s, n);
	return q+n;
}

static char*
imap4ntlm(Imap *imap)
{
	char *s, ruser[64], enc[256];
	uchar buf[128], *p, *ep, *q, *eq, *chal;
	int n;
	MSchapreply mcr;

	imap4cmd(imap, "authenticate ntlm");
	imap4resp(imap);

	/* simple NtLmNegotiate blob with NTLM+OEM flags */
	imap4cmd(imap, "TlRMTVNTUAABAAAAAgIAAA==");
	s = imap4resp(imap);
	n = dec64(buf, sizeof buf, s, strlen(s));
	if(n < 32 || memcmp(buf, "NTLMSSP", 8) != 0)
		return "bad NtLmChallenge";
	chal = buf+24;

	if(auth_respond(chal, 8, ruser, sizeof ruser,
			&mcr, sizeof mcr, auth_getkey,
			"proto=mschap role=client service=imap server=%q user?",
			imap->host) < 0)
		return "auth_respond failed";

	/* prepare NtLmAuthenticate blob */

	memset(buf, sizeof buf, 0);
	p = buf;
	ep = p + 8 + 6*8 + 2*4;
	q = ep;
	eq = buf + sizeof buf;


	memcpy(p, "NTLMSSP", 8);	/* magic */
	p += 8;

	*p++ = 3;
	*p++ = 0;
	*p++ = 0;
	*p++ = 0;

	p = psecb(p, q-buf, 24);		/* LMresp */
	q = psecq(q, mcr.LMresp, 24);

	p = psecb(p, q-buf, 24);		/* NTresp */
	q = psecq(q, mcr.NTresp, 24);

	p = psecb(p, q-buf, 0);		/* realm */

	n = strlen(ruser);
	p = psecb(p, q-buf, n);		/* user name */
	q = psecq(q, ruser, n);

	p = psecb(p, q-buf, 0);		/* workstation name */
	p = psecb(p, q-buf, 0);		/* session key */

	*p++ = 0x02;			/* flags: oem(2)|ntlm(0x200) */
	*p++ = 0x02;
	*p++ = 0;
	*p++ = 0;

	if(p > ep || q > eq)
		return "error creating NtLmAuthenticate";
	enc64(enc, sizeof enc, buf, q-buf);

	imap4cmd(imap, enc);
	if(!isokay(s = imap4resp(imap)))
		return s;
	return nil;
}

static char*
imap4passwd(Imap *imap)
{
	char *s;
	UserPasswd *up;

	if(imap->user != nil)
		up = auth_getuserpasswd(auth_getkey, "proto=pass service=imap server=%q user=%q", imap->host, imap->user);
	else
		up = auth_getuserpasswd(auth_getkey, "proto=pass service=imap server=%q", imap->host);
	if(up == nil)
		return "cannot find IMAP password";

	imap->tag = 1;
	imap4cmd(imap, "login %Z %Z", up->user, up->passwd);
	free(up);
	if(!isokay(s = imap4resp(imap)))
		return s;
	return nil;
}

static char*
imap4login(Imap *imap)
{
	char *e;

	if(imap->cap & Ccram)
		e = imap4cram(imap);
	else if(imap->cap & Cntlm)
		e = imap4ntlm(imap);
	else
		e = imap4passwd(imap);
	if(e)
		return e;
	imap4cmd(imap, "select %Z", imap->mbox);
	if(!isokay(e = imap4resp(imap)))
		return e;
	return nil;
}

static char*
imaperrstr(char *host, char *port)
{
	char err[ERRMAX];
	static char buf[256];

	err[0] = 0;
	errstr(err, sizeof err);
	snprint(buf, sizeof buf, "%s/%s:%s", host, port, err);
	return buf;
}

static void
imap4disconnect(Imap *imap)
{
	if(imap->binit){
		Bterm(&imap->bin);
		Bterm(&imap->bout);
		imap->binit = 0;
	}
	if(imap->fd >= 0){
		close(imap->fd);
		imap->fd = -1;
	}
}

char*
capabilties(Imap *imap)
{
	char * err;

	imap4cmd(imap, "capability");
	imap4resp(imap);
	err = imap4resp(imap);
	if(isokay(err))
		err = 0;
	return err;
}

static char*
imap4dial(Imap *imap)
{
	char *err, *port;

	if(imap->fd >= 0){
		imap4cmd(imap, "noop");
		if(isokay(imap4resp(imap)))
			return nil;
		imap4disconnect(imap);
	}
	if(imap->flags & Fssl)
		port = "imaps";
	else
		port = "imap";
	if((imap->fd = dial(netmkaddr(imap->host, "net", port), 0, 0, 0)) < 0)
		return imaperrstr(imap->host, port);
	if(imap->flags & Fssl && (imap->fd = wraptls(imap->fd, imap->host)) < 0){
		err = imaperrstr(imap->host, port);
		imap4disconnect(imap);
		return err;
	}
	assert(imap->binit == 0);
	Binit(&imap->bin, imap->fd, OREAD);
	Binit(&imap->bout, imap->fd, OWRITE);
	imap->binit = 1;

	imap->tag = 0;
	err = imap4resp(imap);
	if(!isokay(err))
		return "error in initial IMAP handshake";

	if((err = capabilties(imap)) || (err = imap4login(imap))){
		eprint("imap: err is %s\n", err);
		imap4disconnect(imap);
		return err;
	}
	return nil;
}

static void
imap4hangup(Imap *imap)
{
	imap4cmd(imap, "logout");
	imap4resp(imap);
	imap4disconnect(imap);
}

/* gmail lies about message sizes */
static ulong
gmaildiscount(Message *m, uvlong o, ulong l)
{
	if((m->cstate&Cidx) == 0)
	if(o + l == m->size)
		return l + 100 + (o + l)/5;
	return l;
}

static int
imap4fetch(Mailbox *mb, Message *m, uvlong o, ulong l)
{
	Imap *imap;

	imap = mb->aux;
	if(imap->flags & Fgmail)
		l = gmaildiscount(m, o, l);
	idprint(imap, "uid fetch %lud (flags body.peek[]<%llud.%lud>)\n", (ulong)m->imapuid, o, l);
	imap4cmd(imap, "uid fetch %lud (flags body.peek[]<%llud.%lud>)", (ulong)m->imapuid, o, l);
	if(!isokay(imap4resp0(imap, mb, m))){
		eprint("imap: imap fetch failed\n");
		return -1;
	}
	return 0;
}

static uvlong
datesec(Imap *imap, int i)
{
	int j;
	uvlong v;
	Fetchi *f;

	f = imap->f;
	v = (uvlong)f[i].dates << 8;

	/* shifty; these sequences should be stable. */
	for(j = i; j-- > 0; )
		if(f[i].dates != f[j].dates)
			break;
	v |= i - (j + 1);
	return v;
}

static int
vcmp(vlong a, vlong b)
{
	a -= b;
	if(a > 0)
		return 1;
	if(a < 0)
		return -1;
	return 0;
}

static int
fetchicmp(Fetchi *f1, Fetchi *f2)
{
	return vcmp(f1->uid, f2->uid);
}

static char*
imap4read(Imap *imap, Mailbox *mb)
{
	char *s;
	int i, n, c;
	Fetchi *f;
	Message *m, **ll;

again:
	imap4cmd(imap, "status %Z (messages uidvalidity)", imap->mbox);
	if(!isokay(s = imap4resp(imap)))
		return s;
	/* the world shifted: start over */
	if(imap->validity != imap->newvalidity){
		imap->validity = imap->newvalidity;
		imap->nuid = 0;
		imap->muid = 0;
		imap->nmsg = 0;
		goto again;
	}

	imap->f = erealloc(imap->f, imap->nmsg*sizeof imap->f[0]);
	if(imap->nmsg > imap->muid)
		memset(&imap->f[imap->muid], 0, (imap->nmsg - imap->muid)*sizeof(imap->f[0]));
	imap->muid = imap->nmsg;
	if(imap->nmsg > 0){
		n = imap->nuid;
		if(n == 0)
			n = 1;
		if(n > imap->nmsg)
			n = imap->nmsg;
		imap4cmd(imap, "fetch %d:%d (uid flags rfc822.size internaldate)", n, imap->nmsg);
		if(!isokay(s = imap4resp(imap)))
			return s;
	}

	f = imap->f;
	n = imap->nuid;
	if(n > imap->muid){
		idprint(imap, "partial sync %d > %d\n", n, imap->muid);
		n = imap->nuid = imap->muid;
	} else if(n < imap->nmsg)
		idprint(imap, "partial sync %d < %d\n", n, imap->nmsg);
	qsort(f, n, sizeof f[0], (int(*)(void*, void*))fetchicmp);
	ll = &mb->root->part;
	for(i = 0; (m = *ll) != nil || i < n; ){
		c = -1;
		if(i >= n)
			c = 1;
		else if(m){
			if(m->imapuid == 0)
				m->imapuid = strtoull(m->idxaux, 0, 0);
			c = vcmp(f[i].uid, m->imapuid);
		}
		if(c < 0){
			/* new message */
			idprint(imap, "new: %U (%U)\n", f[i].uid, m? m->imapuid: 0);
			if(f[i].sizes == 0 || f[i].sizes > Maxmsg){
				idprint(imap, "skipping bad size: %lud\n", f[i].sizes);
				i++;
				continue;
			}
			m = newmessage(mb->root);
			m->inmbox = 1;
			m->idxaux = smprint("%llud", f[i].uid);
			m->imapuid = f[i].uid;
			m->fileid = datesec(imap, i);
			m->size = f[i].sizes;
			m->flags = f[i].flags;
			m->next = *ll;
			*ll = m;
			ll = &m->next;
			i++;
		}else if(c > 0){
			/* deleted message; */
			idprint(imap, "deleted: %U (%U)\n", i<n? f[i].uid: 0, m? m->imapuid: 0);
			m->inmbox = 0;
			m->deleted = Disappear;
			ll = &m->next;
		}else{
			if((m->flags & ~Frecent) != (f[i].flags & ~Frecent)){
				idprint(imap, "modified: %d != %d\n", m->flags, f[i].flags);
				m->cstate |= Cmod;
			}
			m->flags = f[i].flags;
			ll = &m->next;
			i++;
		}
	}
	return nil;
}

static void
imap4delete(Mailbox *mb, Message *m)
{
	Imap *imap;

	imap = mb->aux;
	if((ulong)(m->imapuid>>32) == imap->validity){
		imap4cmd(imap, "uid store %lud +flags (\\Deleted)", (ulong)m->imapuid);
		imap4resp(imap);
		imap4cmd(imap, "expunge");
		imap4resp(imap);
//		if(!isokay(imap4resp(imap))
//			return -1;
	}
	m->inmbox = 0;
}

static char*
imap4sync(Mailbox *mb)
{
	char *err;
	Imap *imap;

	imap = mb->aux;
	if(err = imap4dial(imap))
		goto out;
	err = imap4read(imap, mb);
out:
	mb->waketime = (ulong)time(0) + imap->refreshtime;
	return err;
}

static char*
imap4ctl(Mailbox *mb, int argc, char **argv)
{
	Imap *imap;

	imap = mb->aux;
	if(argc < 1)
		return Eimap4ctl;

	if(argc == 1 && strcmp(argv[0], "debug") == 0){
		imap->flags ^= Fdebug;
		return nil;
	}
	if(argc == 2 && strcmp(argv[0], "uid") == 0){
		uvlong l;
		Message *m;

		for(m = mb->root->part; m; m = m->next)
			if(strcmp(argv[1], m->name) == 0){
				l = strtoull(m->idxaux, 0, 0);
				fprint(2, "uid %s %lud %lud %lud %lud\n", m->name, (ulong)(l>>32), (ulong)l,
					(ulong)(m->imapuid>>32), (ulong)m->imapuid);
			}
		return nil;
	}
	if(strcmp(argv[0], "refresh") == 0)
		switch(argc){
		case 1:
			imap->refreshtime = 60;
			return nil;
		case 2:
			imap->refreshtime = atoi(argv[1]);
			return nil;
		}

	return Eimap4ctl;
}

static void
imap4close(Mailbox *mb)
{
	Imap *imap;

	imap = mb->aux;
	imap4disconnect(imap);
	free(imap->f);
	free(imap);
}

static char*
mkmbox(Imap *imap, char *p, char *e)
{
	p = seprint(p, e, "%s/box/%s/imap.%s", MAILROOT, getlog(), imap->host);
	if(imap->user && strcmp(imap->user, getlog()))
		p = seprint(p, e, ".%s", imap->user);
	if(cistrcmp(imap->mbox, "inbox"))
		p = seprint(p, e, ".%s", imap->mbox);
	return p;
}

static char*
findmbox(char *p)
{
	char *f[10], path[Pathlen];
	int nf;

	snprint(path, sizeof path, "%s", p);
	nf = getfields(path, f, 5, 0, "/");
	if(nf < 3)
		return nil;
	return f[nf - 1];
}

static char*
imap4rename(Mailbox *mb, char *p2, int)
{
	char *r, *new;
	Imap *imap;

	imap = mb->aux;
	new = findmbox(p2);
	idprint(imap, "rename %s %s\n", imap->mbox, new);
	imap4cmd(imap, "rename %s %s", imap->mbox, new);
	r = imap4resp(imap);
	if(!isokay(r))
		return r;
	free(imap->mbox);
	imap->mbox = smprint("%s", new);
	mkmbox(imap, mb->path, mb->path + sizeof mb->path);
	return 0;
}

char*
imap4mbox(Mailbox *mb, char *path)
{
	char *f[10];
	uchar flags;
	int nf;
	Imap *imap;

	fmtinstall('Z', Zfmt);
	fmtinstall('U', Ufmt);
	if(strncmp(path, "/imap/", 6) == 0)
		flags = 0;
	else if(strncmp(path, "/imaps/", 7) == 0)
		flags = Fssl;
	else
		return Enotme;

	path = strdup(path);
	if(path == nil)
		return "out of memory";

	nf = getfields(path, f, 5, 0, "/");
	if(nf < 3){
		free(path);
		return "bad imap path syntax /imap[s]/system[/user[/mailbox]]";
	}

	imap = emalloc(sizeof *imap);
	imap->fd = -1;
	imap->freep = path;
	imap->flags = flags;
	imap->host = f[2];
	if(strstr(imap->host, "gmail.com"))
		imap->flags |= Fgmail;
	imap->refreshtime = 60;
	if(nf < 4)
		imap->user = nil;
	else
		imap->user = f[3];
	if(nf < 5)
		imap->mbox = strdup("inbox");
	else
		imap->mbox = strdup(f[4]);
	mkmbox(imap, mb->path, mb->path + sizeof mb->path);
	mb->aux = imap;
	mb->sync = imap4sync;
	mb->close = imap4close;
	mb->ctl = imap4ctl;
	mb->fetch = imap4fetch;
	mb->delete = imap4delete;
	mb->rename = imap4rename;
	mb->modflags = imap4modflags;
	mb->addfrom = 1;
	return nil;
}
