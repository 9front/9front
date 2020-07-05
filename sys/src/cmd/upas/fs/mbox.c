#include "common.h"
#include <ctype.h>
#include <plumb.h>
#include <libsec.h>
#include "dat.h"

typedef struct Header Header;
struct Header {
	char	*type;
	uintptr	offset;
	char	*(*f)(Message*, Header*, char*, char*);
	int	len;
	int	str;
};

/* headers */
static	char	*ctype(Message*, Header*, char*, char*);
static	char	*cencoding(Message*, Header*, char*, char*);
static	char	*cdisposition(Message*, Header*, char*, char*);
static	char	*from822(Message*, Header*, char*, char*);
static	char	*replace822(Message*, Header*, char*, char*);
static	char	*concat822(Message*, Header*, char*, char*);
static	char	*copy822(Message*, Header*, char*, char*);
static	char	*ref822(Message*, Header*, char*, char*);

enum
{
	Mhead	= 11,	/* offset of first mime header */
};

#define O(x)	offsetof(Message, x)
static Header head[] =
{
	"date:",		O(date822),	copy822,		0,	0,
	"from:", 		O(from),		from822,	0,	1,
	"to:", 		O(to),		concat822,	0,	1,
	"sender:",	O(sender),	replace822,	0,	1,
	"reply-to:",	O(replyto),	replace822,	0,	1,
	"subject:",	O(subject),	copy822,		0,	1,
	"cc:", 		O(cc),		concat822,	0,	1,
	"bcc:",		O(bcc),		concat822,	0,	1,
	"in-reply-to:",	O(inreplyto),	replace822,	0,	1,
	"message-id:",	O(messageid),	replace822,	0,	1,
	"references:",	~0,		ref822,		0,	0,

[Mhead]	"content-type:", 	~0,		ctype,		0, 	0,
	"content-transfer-encoding:", ~0,	cencoding,	0,	0,
	"content-disposition:", ~0,		cdisposition,	0,	0,
};

static Mailboxinit *boxinit[] = {
	imap4mbox,
	pop3mbox,
	mdirmbox,
	plan9mbox,
};

static void delmessage(Mailbox*, Message*);
static void mailplumb(Mailbox*, Message*);

char*
syncmbox(Mailbox *mb, int doplumb)
{
	char *s;
	int n, d, y, a;
	Message *m, *next;

	if(mb->syncing)
		return nil;

	mb->syncing = 1;

	a = mb->root->subname;
	if(rdidxfile(mb) == -2)
		wridxfile(mb);
	if(s = mb->sync(mb)){
		mb->syncing = 0;
		return s;
	}
	n = 0;
	d = 0;
	y = 0;
	for(m = mb->root->part; m; m = next){
		next = m->next;
		if(m->deleted == 0){
			if((m->cstate & Cidx) == 0){
				cachehash(mb, m);
				m->cstate |= Cnew;
				n++;
			}
			if((doplumb && m->cstate & (Cnew|Cmod)) && ensurecache(mb, m) == 0){
				mailplumb(mb, m);
				msgdecref(mb, m);
			}
			m->cstate &= ~(Cnew|Cmod);
		}
		if(m->cstate & Cidxstale)
			y++;
		if(m->deleted == 0 || m->refs > 0)
			continue;
		if(mb->delete && m->inmbox && m->deleted & Deleted)
			mb->delete(mb, m);
		if(!m->inmbox){
			if(doplumb)
				mailplumb(mb, m);
			delmessage(mb, m);
			d++;
		}
	}
	a = mb->root->subname - a;
	assert(a >= 0);
	if(n + d + y + a){
		Hash *h;

		iprint("deleted: %d; new %d; stale %d\n", d, n, y);
		logmsg(nil, "deleted: %d; new %d; stale %d", d, n, y);
		wridxfile(mb);

		mb->vers++;
		if(mb->refs > 0 && (h = hlook(PATH(0, Qtop), mb->name)) != nil && h->mb == mb)
			h->qid.vers = mb->vers;
	}

	mb->syncing = 0;

	return nil;
}

/*
 * not entirely clear where the locking should take place, if
 * it is required.
 */
char*
mboxrename(char *a, char *b, int flags)
{
	char f0[Pathlen + 4], f1[Pathlen + 4], *err, *p0, *p1;
	Mailbox *mb;

	snprint(f0, sizeof f0, "%s", a);
	snprint(f1, sizeof f1, "%s", b);
	err = newmbox(f0, nil, 0, &mb);
	dprint("mboxrename %s %s -> %s\n", f0, f1, err);
	if(err == nil && mb->rename == nil)
		err = "rename not supported";
	if(err)
		goto done;
	err = mb->rename(mb, f1, flags);
	if(err)
		goto done;
	if(flags & Rtrunc)
		/* we're comitted, so forget bailing */
		err = newmbox(f0, nil, DMcreate, 0);
	p0 = f0 + strlen(f0);
	p1 = f1 + strlen(f1);

	strcat(f0, ".idx");
	strcat(f1, ".idx");
	rename(f0, f1, 0);

	*p0 = *p1 = 0;
	strcat(f0, ".imp");
	strcat(f1, ".imp");
	rename(f0, f1, 0);

	hfree(PATH(0, Qtop), mb->name);
	snprint(mb->path, sizeof mb->path, "%s", b);
	p0 = strrchr(mb->path, '/') + 1;
	if(p0 == (char*)1)
		p0 = mb->path;
	snprint(mb->name, sizeof mb->name, "%s", p0);
	henter(PATH(0, Qtop), mb->name,
		(Qid){PATH(mb->id, Qmbox), mb->vers, QTDIR}, nil, mb);
done:
	return err;
}

static void
initheaders(void)
{
	int i;
	static int already;

	if(already)
		return;
	already = 1;
	for(i = 0; i < nelem(head); i++)
		head[i].len = strlen(head[i].type);
}

static ulong
newid(void)
{
	ulong rv;
	static ulong id;
	static Lock idlock;

	lock(&idlock);
	rv = ++id;
	unlock(&idlock);

	return rv;
}

char*
newmbox(char *path, char *name, int flags, Mailbox **r)
{
	char *p, *rv;
	int i;
	Mailbox *mb, **l;

	if(r)
		*r = nil;
	initheaders();
	mb = emalloc(sizeof *mb);
	mb->flags = flags;
	strncpy(mb->path, path, sizeof mb->path - 1);
	p = name;
	if(p == nil){
		p = strrchr(path, '/');
		if(p == nil)
			p = path;
		else
			p++;
		if(*p == 0){
			free(mb);
			return "bad mbox name";
		}
	}
	strncpy(mb->name, p, sizeof mb->name - 1);
	mb->idxread = genericidxread;
	mb->idxwrite = genericidxwrite;
	mb->idxinvalid = genericidxinvalid;

	/* check for a mailbox type */
	rv = Enotme;	/* can't happen; shut compiler up */
	for(i = 0; i < nelem(boxinit); i++)
		if((rv = boxinit[i](mb, path)) != Enotme)
			break;
	if(rv){
		free(mb);
		return rv;
	}

	/* make sure name isn't taken */
	for(l = &mbl; *l != nil; l = &(*l)->next)
		if(strcmp((*l)->name, mb->name) == 0){
			if(strcmp(path, (*l)->path) == 0)
				rv = nil;
			else
				rv = "mbox name in use";
			if(mb->close)
				mb->close(mb);
			free(mb);
			return rv;
		}

	/* always try locking */
	mb->dolock = 1;
	mb->refs = 1;
	mb->next = nil;
	mb->id = newid();
	mb->root = newmessage(nil);
	mtreeinit(mb);

	*l = mb;

	henter(PATH(0, Qtop), mb->name,
		(Qid){PATH(mb->id, Qmbox), mb->vers, QTDIR}, nil, mb);
	if(mb->ctl)
		henter(PATH(mb->id, Qmbox), "ctl",
			(Qid){PATH(mb->id, Qmboxctl), 0, QTFILE}, nil, mb);
	if(r)
		*r = mb;

	return syncmbox(mb, 0);
}

/* close the named mailbox */
void
freembox(char *name)
{
	Mailbox **l, *mb;

	for(l = &mbl; (mb = *l) != nil; l = &mb->next)
		if(strcmp(name, mb->name) == 0){
			*l = mb->next;
			mb->next = nil;
			hfree(PATH(0, Qtop), mb->name);
			if(mb->ctl)
				hfree(PATH(mb->id, Qmbox), "ctl");
			mboxdecref(mb);
			break;
		}
}

char*
removembox(char *name, int flags)
{
	Mailbox *mb;

	for(mb = mbl; mb != nil; mb = mb->next)
		if(strcmp(name, mb->path) == 0){
			mb->flags |= ORCLOSE;
			mb->rmflags = flags;
			freembox(mb->name);
			return 0;
		}
	return "maibox not found";
}

void
syncallmboxes(void)
{
	Mailbox *mb;
	char *err;

	for(mb = mbl; mb != nil; mb = mb->next)
		if(err = syncmbox(mb, 0))
			eprint("syncmbox: %s\n", err);
}

/*
 *  look for the date in the first Received: line.
 *  it's likely to be the right time zone (it's
 *  the local system) and in a convenient format.
 */
static int
rxtotm(Message *m, Tm *tm)
{
	char *p, *q;
	int r;

	if(cistrncmp(m->header, "received:", 9))
		return -1;
	q = strchr(m->header, ';');
	if(!q)
		return -1;
	p = q;
	while((p = strchr(p, '\n')) != nil){
		if(p[1] != ' ' && p[1] != '\t' && p[1] != '\n')
			break;
		p++;
	}
	if(!p)
		return -1;
	*p = '\0';
	r = strtotm(q + 1, tm);
	*p = '\n';
	return r;
}

static Message*
gettopmsg(Mailbox *mb, Message *m)
{
	while(!Topmsg(mb, m))
		m = m->whole;
	return m;
}

static void
datesec(Mailbox *mb, Message *m)
{
	vlong v;
	Tm tm;

	if(m->fileid > 1000000ull<<8)
		return;
	if(m->unixfrom && strtotm(m->unixfrom, &tm) >= 0)
		v = tm2sec(&tm);
	else if(m->date822 && strtotm(m->date822, &tm) >= 0)
		v = tm2sec(&tm);
	else if(rxtotm(m, &tm) >= 0)
		v = tm2sec(&tm);
	else{
		logmsg(gettopmsg(mb, m), "%s:%s: datasec %s %s\n", mb->path,
			m->whole? m->whole->name: "?",
			m->name, m->type);
		if(Topmsg(mb, m) || strcmp(m->type, "message/rfc822") == 0)
			abort();
		v = 0;
	}
	m->fileid = v<<8;
}

/*
 *  parse a message
 */
extern void sanemsg(Message*);
extern void sanembmsg(Mailbox*, Message*);

static Message*
haschild(Message *m, int i)
{
	for(m = m->part; m && i; i--)
		m = m->next;
	return m;
}

static void
parseattachments(Message *m, Mailbox *mb)
{
	char *p, *x;
	int i;
	Message *nm, **l;

	/* if there's a boundary, recurse... */
	dprint("parseattachments %p %ld bonudary %s\n", m->start, (ulong)(m->end - m->start), m->boundary);
	if(m->boundary != nil){
		p = m->body;
		nm = nil;
		l = &m->part;
		for(i = 0;;){
			x = strstr(p, m->boundary);
			/* two sequential boundaries; ignore nil message */
			if(nm && x == p){
				p = strchr(x, '\n');
				if(p == nil){
					nm->rbend = nm->bend = nm->end = x;
					sanemsg(nm);
					break;
				}
				p = p + 1;
				continue;
			}
			/* no boundary, we're done */
			if(x == nil){
				if(nm != nil)
					nm->rbend = nm->bend = nm->end = m->bend;
				break;
			}
			/* boundary must be at the start of a line */
			if(x != m->body && x[-1] != '\n'){
				p = x + 1;
				continue;
			}

			if(nm != nil)
				nm->rbend = nm->bend = nm->end = x;
			x += strlen(m->boundary);

			/* is this the last part? ignore anything after it */
			if(strncmp(x, "--", 2) == 0)
				break;
			p = strchr(x, '\n');
			if(p == nil)
				break;
			if((nm = haschild(m, i++)) == nil){
				nm = newmessage(m);
				*l = nm;
				l = &nm->next;
			}
			nm->start = ++p;
			assert(nm->ballocd == 0);
			nm->mheader = nm->header = nm->body = nm->rbody = nm->start;
		}
		for(nm = m->part; nm != nil; nm = nm->next){
			nm->size = nm->end - nm->start;
			parse(mb, nm, 0, 1);
			cachehash(mb, nm);		/* botchy place for this */
		}
		return;
	}

	/* if we've got an rfc822 message, recurse... */
	if(strcmp(m->type, "message/rfc822") == 0){
		if((nm = haschild(m, 0)) == nil){
			nm = newmessage(m);
			m->part = nm;
		}
		assert(nm->ballocd == 0);
		nm->start = nm->header = nm->body = nm->rbody = m->body;
		nm->end = nm->bend = nm->rbend = m->bend;
		nm->size = nm->end - nm->start;
		parse(mb, nm, 0, 0);
		cachehash(mb, nm);			/* botchy place for this */
	}
}

static void
parseunix(Message *m)
{
	char *s, *p;

	m->unixheader = smprint("%.*s", utfnlen(m->start, m->header - m->start), m->start);
	s = m->start + 5;
	if((p = strchr(s, ' ')) == nil)
		return;
	*p = 0;
	free(m->unixfrom);
	m->unixfrom = strdup(s);
	*p = ' ';
}

void
parseheaders(Mailbox *mb, Message *m, int addfrom, int justmime)
{
	char *p, *e, *o, *t, *s;
	int i, i0, n;
	uintptr a;

	if(m->header == nil)
		m->header = m->start;

	/* parse unix header */
	if(!justmime && !addfrom && m->unixheader == nil){
		if(strncmp(m->start, "From ", 5) == 0)
		if((e = memchr(m->start, '\n', m->end - m->start)) != nil){
			m->header = e + 1;
			parseunix(m);
		}
	}

	/* parse mime headers */
	p = m->mheader = m->mhend = m->header;
	i0 = 0;
	if(justmime)
		i0 = Mhead;
	s = emalloc(2048);
	e = s + 2048 - 1;
	while((n = hdrlen(p, m->end)) > 0){
		if(n > e - s){
			s = erealloc(s, n);
			e = s + n - 1;
		}
		rfc2047(s, e, p, n, 1);
		p += n;

		for(i = i0; i < nelem(head); i++)
			if(!cistrncmp(s, head[i].type, head[i].len)){
				a = head[i].offset;
				if(a != ~0){
					if(o = *(char**)((char*)m + a))
						continue;
					t = head[i].f(m, head + i, o, s);
					*(char**)((char*)m + a) = t;
				}else
					head[i].f(m, head + i, 0, s);
				break;
			}
	}
	free(s);
	/* the blank line isn't really part of the body or header */
	if(justmime){
		m->mhend = p;
		m->hend = m->header;
	} else{
		m->hend = p;
		m->mhend = m->header;
	}
	if(*p == '\n')
		p++;
	m->rbody = m->body = p;

	if(!justmime)
		datesec(mb, m);

	/*
	 *  only fake header for top-level messages for pop3 and imap4
	 *  clients (those protocols don't include the unix header).
	 *  adding the unix header all the time screws up mime-attached
	 *  rfc822 messages.
	 */
	if(!addfrom && m->unixfrom == nil) {
		free(m->unixheader);
		m->unixheader = nil;
	} else if(m->unixheader == nil){
		if(m->unixfrom != nil && strcmp(m->unixfrom, "???") != 0)
			p = m->unixfrom;
		else if(m->from != nil)
			p = m->from;
		else
			p = "???";
		m->unixheader = smprint("From %s %Δ\n", p, m->fileid);
	}
	m->cstate |= Cheader;
sanembmsg(mb, m);
}

char*
promote(char *s)
{
	return s? strdup(s): nil;
}

void
parsebody(Message *m, Mailbox *mb)
{
	Message *nm;

	/* recurse */
	if(strncmp(m->type, "multipart/", 10) == 0)
		parseattachments(m, mb);
	else if(strcmp(m->type, "message/rfc822") == 0){
		decode(m);
		parseattachments(m, mb);
		nm = m->part;

		/* promote headers */
		if(m->replyto == nil && m->from == nil && m->sender == nil){
			m->from = promote(nm->from);
			m->to = promote(nm->to);
			m->date822 = promote(nm->date822);
			m->sender = promote(nm->sender);
			m->replyto = promote(nm->replyto);
			m->subject = promote(nm->subject);
		}
	}else if(strncmp(m->type, "text/", 5) == 0)
		sanemsg(m);

	free(m->boundary);
	m->boundary = nil;

	if(m->replyto == nil){
		if(m->from != nil)
			m->replyto = strdup(m->from);
		else if(m->sender != nil)
			m->replyto = strdup(m->sender);
		else if(m->unixfrom != nil)
			m->replyto = strdup(m->unixfrom);
	}
	if(m->from == nil && m->unixfrom != nil)
		m->from = strdup(m->unixfrom);

	free(m->unixfrom);
	m->unixfrom = nil;

	m->rawbsize = m->rbend - m->rbody;
	m->cstate |= Cbody;
}

void
parse(Mailbox *mb, Message *m, int addfrom, int justmime)
{
	sanemsg(m);
	if((m->cstate & Cheader) == 0)
		parseheaders(mb, m, addfrom, justmime);
	parsebody(m, mb);
	sanemsg(m);
}

static char*
skipwhite(char *p)
{
	while(isascii(*p) && isspace(*p))
		p++;
	return p;
}

static char*
skiptosemi(char *p)
{
	while(*p && *p != ';')
		p++;
	while(*p == ';' || (isascii(*p) && isspace(*p)))
		p++;
	return p;
}

static char*
getstring(char *p, char *s, char *e, int dolower)
{
	int c;

	p = skipwhite(p);
	if(*p == '"'){
		for(p++; (c = *p) != '"'; p++){
			if(c == '\\')
				c = *++p;
			/*
			 * 821 says <x> after \ can be anything at all.
			 * we just don't care.
			 */
			if(c == 0)
				break;
			if(c < ' ')
				continue;
			if(dolower && c >= 'A' && c <= 'Z')
				c += 0x20;
			s = sputc(s, e, c);
		}
		if(*p == '"')
			p++;
	}else{
		for(; (c = *p) && !isspace(c) && c != ';'; p++){
			if(c == '\\')
				c = *++p;
			/*
			 * 821 says <x> after \ can be anything at all.
			 * we just don't care.
			 */
			if(c == 0)
				break;
			if(c < ' ')
				continue;
			if(dolower && c >= 'A' && c <= 'Z')
				c += 0x20;
			s = sputc(s, e, c);
		}
	}
	*s = 0;
	return p;
}

static void
setfilename(Message *m, char *p)
{
	char buf[Pathlen];

	dprint("setfilename %p %s -> %s\n", m, m->filename, p);
	if(m->filename != nil)
		return;
	getstring(p, buf, buf + sizeof buf - 1, 0);
	m->filename = smprint("%s", buf);
	for(p = m->filename; *p; p++)
		if(*p == ' ' || *p == '\t' || *p == ';')
			*p = '_';
}

static char*
rtrim(char *p)
{
	char *e;

	if(p == 0)
		return p;
	e = p + strlen(p) - 1;
	while(e > p && isascii(*e) && isspace(*e))
		*e-- = 0;
	return p;
}

static char*
addr822(char *p, char **ac)
{
	int n, c, space, incomment, addrdone, inanticomment, quoted;
	char s[128+1], *ps, *e, *x, *list;

	list = 0;
	s[0] = 0;
	ps = s;
	e = s + sizeof s;
	space = quoted = incomment = addrdone = inanticomment = 0;
	n = 0;
	for(; c = *p; p++){
		if(!inanticomment && !quoted && !space && ps != s && c == ' '){
			ps = sputc(ps, e, c);
			space = 1;
			continue;
		}
		space = 0;
		if(!quoted && isspace(c) || c == '\r')
			continue;
		/* strings are always treated as atoms */
		if(!quoted && c == '"'){
			if(!addrdone && !incomment && !ac)
				ps = sputc(ps, e, c);
			for(p++; c = *p; p++){
				if(ac && c == '"')
					break;
				if(!addrdone && !incomment)
					ps = sputc(ps, e, c);
				if(!quoted && *p == '"')
					break;
				if(*p == '\\')
					quoted = 1;
				else
					quoted = 0;
			}
			if(c == 0)
				break;
			quoted = 0;
			continue;
		}

		/* ignore everything in an expicit comment */
		if(!quoted && c == '('){
			incomment = 1;
			continue;
		}
		if(incomment){
			if(!quoted && c == ')')
				incomment = 0;
			quoted = 0;
			continue;
		}

		/* anticomments makes everything outside of them comments */
		if(!quoted && c == '<' && !inanticomment){
			if(ac){
				*ps-- = 0;
				if(ps > s && *ps == ' ')
					*ps = 0;
				if(*ac){
					*ac = smprint("%s, %s", x=*ac, s);
					free(x);
				}else
					*ac = smprint("%s", s);
			}

			inanticomment = 1;
			ps = s;
			continue;
		}
		if(!quoted && c == '>' && inanticomment){
			addrdone = 1;
			inanticomment = 0;
			continue;
		}

		/* commas separate addresses */
		if(!quoted && c == ',' && !inanticomment){
			*ps = 0;
			addrdone = 0;
			if(n++ != 0){
				list = smprint("%s %s", x=list, s);
				free(x);
			}else
				list = smprint("%s", s);
			ps = s;
			continue;
		}

		/* what's left is part of the address */
		ps = sputc(ps, e, c);

		/* quoted characters are recognized only as characters */
		if(c == '\\')
			quoted = 1;
		else
			quoted = 0;

	}

	if(ps > s){
		*ps = 0;
		if(n != 0){
			list = smprint("%s %s", x=list, s);
			free(x);
		}else
			list = smprint("%s", s);
	}
	return rtrim(list);
}

/*
 * per rfc2822 §4.5.3, permit multiple to, cc and bcc headers by
 * concatenating their values.
 */

static char*
concat822(Message*, Header *h, char *o, char *p)
{
	char *s, *n;

	p += strlen(h->type);
	s = addr822(p, 0);
	if(o){
		n = smprint("%s %s", o, s);
		free(s);
	}else
		n = s;
	return n;
}

static char*
from822(Message *m, Header *h, char*, char *p)
{
	if(m->ffrom)
		free(m->ffrom);
	m->from = 0;
	return addr822(p + h->len, &m->ffrom);
}

static char*
replace822(Message *, Header *h, char*, char *p)
{
	return addr822(p + h->len, 0);
}

static char*
copy822(Message*, Header *h, char*, char *p)
{
	return rtrim(strdup(skipwhite(p + h->len)));
}

static char*
ref822(Message *m, Header *h, char*, char *p)
{
	char **a, *s, *f[Nref + 1];
	int i, j, n;

	s = strdup(skipwhite(p + h->len));
	n = getfields(s, f, nelem(f), 1, "<> \n\t\r,");
	if(n > Nref)
		n = Nref;
	/*
	 * if there are too many references, drop from the beginning
	 * of the list. If someone else has a duplicate, we keep the
	 * old duplicate.
	 */
	a = m->references;
	for(i = 0; i < n; i++){
		for(j = 0; j < Nref; j++)
			if(a[j] == nil || strcmp(a[j], f[i]) == 0)
				break;
		if(j == Nref){
			free(a[0]);
			memmove(&a[0], &a[1], (Nref - 1) * sizeof(a[0]));
			j--;
		} else if(a[j] != nil)
			continue;
		a[j] = strdup(f[i]);
	}
	free(s);
	return (char*)~0;
}

static int
isattribute(char **pp, char *attr)
{
	char *p;
	int n;

	n = strlen(attr);
	p = *pp;
	if(cistrncmp(p, attr, n) != 0)
		return 0;
	p += n;
	while(*p == ' ')
		p++;
	if(*p++ != '=')
		return 0;
	while(*p == ' ')
		p++;
	*pp = p;
	return 1;
}

static char*
ctype(Message *m, Header *h, char*, char *p)
{
	char buf[128], *e;

	e = buf + sizeof buf - 1;
	p = getstring(skipwhite(p + h->len), buf, e, 1);
	m->type = intern(buf);

	for(; *p; p = skiptosemi(p))
		if(isattribute(&p, "boundary")){
			p = getstring(p, buf, e, 0);
			free(m->boundary);
			m->boundary = smprint("--%s", buf);
		} else if(cistrncmp(p, "multipart", 9) == 0){
			/*
			 *  the first unbounded part of a multipart message,
			 *  the preamble, is not displayed or saved
			 */
		} else if(isattribute(&p, "name")){
			setfilename(m, p);
		} else if(isattribute(&p, "charset")){
			p = getstring(p, buf, e, 1);
			m->charset = intern(buf);
		}
	return (char*)~0;
}

static char*
cencoding(Message *m, Header *h, char*, char *p)
{
	p = skipwhite(p + h->len);
	if(cistrncmp(p, "base64", 6) == 0)
		m->encoding = Ebase64;
	else if(cistrncmp(p, "quoted-printable", 16) == 0)
		m->encoding = Equoted;
	return (char*)~0;
}

static char*
cdisposition(Message *m, Header *h, char*, char *p)
{
	for(p = skipwhite(p + h->len); *p; p = skiptosemi(p))
		if(cistrncmp(p, "inline", 6) == 0)
			m->disposition = Dinline;
		else if(cistrncmp(p, "attachment", 10) == 0)
			m->disposition = Dfile;
		else if(cistrncmp(p, "filename=", 9) == 0){
			p += 9;
			setfilename(m, p);
		}
	return (char*)~0;
}

ulong	msgallocd;
ulong	msgfreed;

Message*
newmessage(Message *parent)
{
	static int id;
	Message *m;

	msgallocd++;

	m = emalloc(sizeof *m);
	dprint("newmessage %ld	%p	%p\n", msgallocd, parent, m);
	m->type = intern("text/plain");
	m->charset = intern("iso-8859-1");
	m->cstate = Cidxstale;
	m->flags = Frecent;
	m->id = newid();
	if(parent)
		snprint(m->name, sizeof m->name, "%d", ++(parent->subname));
	if(parent == nil)
		parent = m;
	m->whole = parent;
	m->hlen = -1;
	return m;
}

/* delete a message from a mailbox */
static void
delmessage(Mailbox *mb, Message *m)
{
	Message **l;

	assert(m->refs == 0);
	while(m->part)
		delmessage(mb, m->part);

	mb->vers++;
	msgfreed++;

	if(m != m->whole){
		/* unchain from parent */
		for(l = &m->whole->part; *l && *l != m; l = &(*l)->next)
			;
		if(*l != nil)
			*l = m->next;
		m->next = nil;
		/* clear out of name lookup hash table */
		if(m->whole->whole == m->whole)
			hfree(PATH(mb->id, Qmbox), m->name);
		else
			hfree(PATH(m->whole->id, Qdir), m->name);
		hfree(PATH(m->id, Qdir), "xxx");		/* sleezy speedup */

		if(Topmsg(mb, m))
			mtreedelete(mb, m);
		cachefree(mb, m);
		idxfree(m);
	}
	free(m);
}

void
unnewmessage(Mailbox *mb, Message *parent, Message *m)
{
	assert(parent->subname > 0);
	m->deleted = Dup;
	delmessage(mb, m);
	parent->subname -= 1;
}

/* mark messages (identified by path) for deletion */
char*
delmessages(int ac, char **av)
{
	int i, needwrite;
	Mailbox *mb;
	Message *m;

	for(mb = mbl; mb != nil; mb = mb->next)
		if(strcmp(av[0], mb->name) == 0)
			break;
	if(mb == nil)
		return "no such mailbox";

	needwrite = 0;
	for(i = 1; i < ac; i++)
		for(m = mb->root->part; m != nil; m = m->next)
			if(strcmp(m->name, av[i]) == 0){
				if(!m->deleted){
					needwrite = 1;
					m->deleted = Deleted;
					logmsg(m, "deleting");
				}
				break;
			}
	if(needwrite)
		syncmbox(mb, 1);
	return 0;
}

char*
flagmessages(int argc, char **argv)
{
	char *err, *rerr;
	int i, needwrite;
	Mailbox *mb;
	Message *m;

	if(argc%2)
		return "bad flags";
	for(mb = mbl; mb != nil; mb = mb->next)
		if(strcmp(*argv, mb->name) == 0)
			break;
	if(mb == nil)
		return "no such mailbox";
	needwrite = 0;
	rerr = 0;
	for(i = 1; i < argc; i += 2)
		for(m = mb->root->part; m; m = m->next)
			if(strcmp(m->name, argv[i]) == 0){
				if(err = modflags(mb, m, argv[i + 1]))
					rerr = err;
				else
					needwrite = 1;
			}
	if(needwrite)
		syncmbox(mb, 1);
	return rerr;
}

void
msgincref(Mailbox *mb, Message *m)
{
	assert(mb->refs >= 0);
	for(;; m = m->whole){
		assert(m->refs >= 0);
		m->refs++;
		if(Topmsg(mb, m))
			break;
	}
}

void
msgdecref(Mailbox *mb, Message *m)
{
	assert(mb->refs >= 0);
	for(;; m = m->whole){
		assert(m->refs > 0);
		m->refs--;
		if(Topmsg(mb, m)){
			if(m->refs == 0){
				if(m->deleted)
					syncmbox(mb, 1);
				else
					putcache(mb, m);
			}
			break;
		}
	}
}

void
mboxincref(Mailbox *mb)
{
	assert(mb->refs > 0);
	mb->refs++;
}

static void
mbrmidx(char *path, int flags)
{
	char buf[Pathlen];

	snprint(buf, sizeof buf, "%s.idx", path);
	vremove(buf);
	if((flags & Rtrunc) == 0){
		snprint(buf, sizeof buf, "%s.imp", path);
		vremove(buf);
	}
}

void
mboxdecref(Mailbox *mb)
{
	assert(mb->refs > 0);
	if(--mb->refs)
		return;
	syncmbox(mb, 1);
	delmessage(mb, mb->root);
	if(mb->close)
		mb->close(mb);
	if(mb->flags & ORCLOSE && mb->remove)
	if(mb->remove(mb, mb->rmflags))
		rmidx(mb->path, mb->rmflags);
	mtreefree(mb);
	free(mb->d);
	free(mb);
}


/* just space over \r.  sleezy but necessary for ms email. */
int
deccr(char *x, int len)
{
	char *e;

	e = x + len;
	for(;;){
		x = memchr(x, '\r', e - x);
		if(x == nil)
			break;
		*x = ' ';
	}
	return len;
}

/*
 *  undecode message body
 */
void
decode(Message *m)
{
	int i, len;
	char *x;

	if(m->decoded)
		return;
	dprint("decode %d %p\n", m->encoding, m);
	switch(m->encoding){
	case Ebase64:
		len = m->bend - m->body;
		i = (len*3)/4 + 1;	/* room for max chars + null */
		x = emalloc(i);
		len = dec64((uchar*)x, i, m->body, len);
		if(len == -1){
			free(x);
			break;
		}
		if(strncmp(m->type, "text/", 5) == 0)
			len = deccr(x, len);
		if(m->ballocd)
			free(m->body);
		m->body = x;
		m->bend = x + len;
		m->ballocd = 1;
		break;
	case Equoted:
		len = m->bend - m->body;
		x = emalloc(len + 2);	/* room for null and possible extra nl */
		len = decquoted(x, m->body, m->bend, 0);
		if(m->ballocd)
			free(m->body);
		m->body = x;
		m->bend = x + len;
		m->ballocd = 1;
		break;
	default:
		break;
	}
	m->decoded = 1;
}

/* convert x to utf8 */
void
convert(Message *m)
{
	int len;
	char *x;

	/* don't convert if we're not a leaf, not text, or already converted */
	if(m->converted)
		return;
	dprint("convert type=%q charset=%q %p\n", m->type, m->charset, m);
	m->converted = 1;
	if(m->part != nil || strncmp(m->type, "text", 4) != 0 || *m->charset == 0)
		return;
	len = xtoutf(m->charset, &x, m->body, m->bend);
	if(len > 0){
		if(m->ballocd)
			free(m->body);
		m->body = x;
		m->bend = x + len;
		m->ballocd = 1;
	}
}

static int
hex2int(int x)
{
	if(x >= '0' && x <= '9')
		return x - '0';
	if(x >= 'A' && x <= 'F')
		return x - 'A' + 10;
	if(x >= 'a' && x <= 'f')
		return x - 'a' + 10;
	return -1;
}

/*
 *  underscores are translated in 2047 headers (uscores=1)
 *  but not in the body (uscores=0)
 */
static char*
decquotedline(char *out, char *in, char *e, int uscores)
{
	int c, soft;

	/* dump trailing white space */
	while(e >= in && (*e == ' ' || *e == '\t' || *e == '\r' || *e == '\n'))
		e--;

	/* trailing '=' means no newline */
	if(*e == '='){
		soft = 1;
		e--;
	} else
		soft = 0;

	while(in <= e){
		c = (*in++) & 0xff;
		switch(c){
		case '_':
			if(uscores){
				*out++ = ' ';
				break;
			}
		default:
			*out++ = c;
			break;
		case '=':
			c = hex2int(*in++)<<4;
			c |= hex2int(*in++);
			if(c != -1)
				*out++ = c;
			else{
				*out++ = '=';
				in -= 2;
			}
			break;
		}
	}
	if(!soft)
		*out++ = '\n';
	*out = 0;

	return out;
}

int
decquoted(char *out, char *in, char *e, int uscores)
{
	char *p, *nl;

	p = out;
	while((nl = strchr(in, '\n')) != nil && nl < e){
		p = decquotedline(p, in, nl, uscores);
		in = nl + 1;
	}
	if(in < e)
		p = decquotedline(p, in, e - 1, uscores);

	/* make sure we end with a new line */
	if(*(p - 1) != '\n'){
		*p++ = '\n';
		*p = 0;
	}

	return p - out;
}

char*
lowercase(char *p)
{
	char *op;
	int c;

	for(op = p; c = *p; p++)
		if(isupper(c))
			*p = tolower(c);
	return op;
}

/* translate latin1 directly since it fits neatly in utf */
static int
latin1toutf(char **out, char *in, char *e)
{
	int n;
	char *p;
	Rune r;

	n = 0;
	for(p = in; p < e; p++)
		if(*p & 0x80)
			n++;
	if(n == 0)
		return 0;

	n += e - in;
	*out = p = malloc(n + 1);
	if(p == nil)
		return 0;

	for(; in < e; in++){
		r = (uchar)*in;
		p += runetochar(p, &r);
	}
	*p = 0;
	return p - *out;
}

/* translate any thing using the tcs program */
int
xtoutf(char *charset, char **out, char *in, char *e)
{
	char *av[4], *p;
	int totcs[2], fromtcs[2], n, len, sofar;

	/* might not need to convert */
	if(strcmp(charset, "us-ascii") == 0 || strcmp(charset, "utf-8") == 0)
		return 0;
	if(strcmp(charset, "iso-8859-1") == 0)
		return latin1toutf(out, in, e);

	len = e - in + 1;
	sofar = 0;
	*out = p = malloc(len + 1);
	if(p == nil)
		return 0;

	av[0] = charset;
	av[1] = "-f";
	av[2] = charset;
	av[3] = 0;
	if(pipe(totcs) < 0)
		goto error;
	if(pipe(fromtcs) < 0){
		close(totcs[0]); close(totcs[1]);
		goto error;
	}
	switch(rfork(RFPROC|RFFDG|RFNOWAIT)){
	case -1:
		close(fromtcs[0]); close(fromtcs[1]);
		close(totcs[0]); close(totcs[1]);
		goto error;
	case 0:
		close(fromtcs[0]); close(totcs[1]);
		dup(fromtcs[1], 1);
		dup(totcs[0], 0);
		close(fromtcs[1]); close(totcs[0]);
		dup(open("/dev/null", OWRITE), 2);
		exec("/bin/tcs", av);
		_exits("");
	default:
		close(fromtcs[1]); close(totcs[0]);
		switch(rfork(RFPROC|RFFDG|RFNOWAIT)){
		case -1:
			close(fromtcs[0]); close(totcs[1]);
			goto error;
		case 0:
			close(fromtcs[0]);
			while(in < e){
				n = write(totcs[1], in, e - in);
				if(n <= 0)
					break;
				in += n;
			}
			close(totcs[1]);
			_exits("");
		default:
			close(totcs[1]);
			for(;;){
				n = read(fromtcs[0], &p[sofar], len - sofar);
				if(n <= 0)
					break;
				sofar += n;
				p[sofar] = 0;
				if(sofar == len){
					len += 1024;
					p = realloc(p, len + 1);
					if(p == nil)
						goto error;
					*out = p;
				}
			}
			close(fromtcs[0]);
			break;
		}
		break;
	}
	if(sofar == 0)
		goto error;
	return sofar;

error:
	free(*out);
	*out = nil;
	return 0;
}

void *
emalloc(ulong n)
{
	void *p;

	p = mallocz(n, 1);
	if(!p)
		sysfatal("malloc %lud: %r", n);
	setmalloctag(p, getcallerpc(&n));
	return p;
}

void *
erealloc(void *p, ulong n)
{
	if(n == 0)
		n = 1;
	p = realloc(p, n);
	if(!p)
		sysfatal("realloc %lud: %r", n);
	setrealloctag(p, getcallerpc(&p));
	return p;
}

int
myplumbsend(int fd, Plumbmsg *m)
{
	char *buf;
	int n;

	buf = plumbpack(m, &n);
	if(buf == nil)
		return -1;
	n = write(fd, buf, n);
	free(buf);
	return n;
}

static void
mailplumb(Mailbox *mb, Message *m)
{
	char buf[256], dbuf[SHA1dlen*2 + 1], len[10], date[30], *from, *subject;
	int ai;
	Plumbmsg p;
	Plumbattr a[7];
	static int fd = -1;

	subject = m->subject;
	if(subject == nil)
		subject = "";

	from = m->from;
	if(from == nil)
		from = "";

	sprint(len, "%lud", m->size);
	if(biffing && m->inmbox)
		fprint(2, "[ %s / %s / %s ]\n", from, subject, len);
	if(!plumbing)
		return;

	if(fd < 0)
		fd = plumbopen("send", OWRITE);
	if(fd < 0)
		return;

	p.src = "mailfs";
	p.dst = "seemail";
	p.wdir = "/mail/fs";
	p.type = "text";

	ai = 0;
	a[ai].name = "filetype";
	a[ai].value = "mail";

	a[++ai].name = "sender";
	a[ai].value = from;
	a[ai-1].next = &a[ai];

	a[++ai].name = "length";
	a[ai].value = len;
	a[ai-1].next = &a[ai];

	a[++ai].name = "mailtype";
	if(!m->inmbox)
		a[ai].value = "delete";
	else if(m->cstate & Cmod)
		a[ai].value = "modify";
	else
		a[ai].value = "new";
	a[ai-1].next = &a[ai];

	snprint(date, sizeof date, "%Δ", m->fileid);
	a[++ai].name = "date";
	a[ai].value = date;
	a[ai-1].next = &a[ai];

	if(m->digest){
		snprint(dbuf, sizeof dbuf, "%A", m->digest);
		a[++ai].name = "digest";
		a[ai].value = dbuf;
		a[ai-1].next = &a[ai];
	}
	a[ai].next = nil;
	p.attr = a;
	snprint(buf, sizeof buf, "%s/%s/%s",
		mntpt, mb->name, m->name);
	p.ndata = strlen(buf);
	p.data = buf;

	myplumbsend(fd, &p);
}

/*
 *  count the number of lines in the body (for imap4)
 */
ulong
countlines(Message *m)
{
	char *p;
	ulong i;

	i = 0;
	for(p = strchr(m->rbody, '\n'); p != nil && p < m->rbend; p = strchr(p + 1, '\n'))
		i++;
	return i;
}

static char *logf = "fs";

void
logmsg(Message *m, char *fmt, ...)
{
	char buf[256], *p, *e;
	va_list args;

	if(!lflag)
		return;
	e = buf + sizeof buf;
	p = seprint(buf, e, "%s.%d: ", user, getpid());
	if(m)
		p = seprint(p, e, "from %s digest %A ",
			m->from, m->digest);
	va_start(args, fmt);
	vseprint(p, e, fmt, args);
	va_end(args);

	if(Sflag)
		fprint(2, "%s\n", buf);
	syslog(Sflag, logf, "%s", buf);
}

void
iprint(char *fmt, ...)
{
	char buf[256], *p, *e;
	va_list args;

	if(!iflag)
		return;
	e = buf + sizeof buf;
	p = seprint(buf, e, "%s.%d: ", user, getpid());
	va_start(args, fmt);
	vseprint(p, e, fmt, args);
	vfprint(2, fmt, args);
	va_end(args);
	syslog(Sflag, logf, "%s", buf);
}

void
eprint(char *fmt, ...)
{
	char buf[256], buf2[256], *p, *e;
	va_list args;

	e = buf + sizeof buf;
	p = seprint(buf, e, "%s.%d: ", user, getpid());
	va_start(args, fmt);
	vseprint(p, e, fmt, args);
	e = buf2 + sizeof buf2;
	p = seprint(buf2, e, "upas/fs: ");
	vseprint(p, e, fmt, args);
	va_end(args);
	syslog(Sflag, logf, "%s", buf);
	fprint(2, "%s", buf2);
}

/*
 *  convert an RFC822 date into a Unix style date
 *  for when the Unix From line isn't there (e.g. POP3).
 *  enough client programs depend on having a Unix date
 *  that it's easiest to write this conversion code once, right here.
 *
 *  people don't follow RFC822 particularly closely,
 *  so we use strtotm, which is a bunch of heuristics.
 */

char*
date822tounix(Message *, char *s)
{
	char *p, *q;
	Tm tm;

	if(strtotm(s, &tm) < 0)
		return nil;

	p = asctime(&tm);
	if(q = strchr(p, '\n'))
		*q = '\0';
	return strdup(p);
}
