#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <plumb.h>
#include <ctype.h>
#include <regexp.h>

#include "mail.h"

typedef struct Fn	Fn;

struct Fn {
	char *name;
	void (*fn)(char **, int);
};

enum {
	Cevent,
	Cseemail,
	Cshowmail,
	Csendmail,
	Nchan,
};


char	*maildir	= "/mail/fs";
char	*mailbox	= "mbox";
char	*savebox	= "outgoing";
char	*listfmt	= "%>48s\t<%f>";
Mesg	dead = {.messageid="", .hash=42};

Reprog	*mesgpat;

int	threadsort = 1;
int	sender;

int	plumbsendfd;
int	plumbseemailfd;
int	plumbshowmailfd;
int	plumbsendmailfd;
Channel *cwait;

Mbox	mbox;

static void	showmesg(Biobuf*, Mesg*, int, int);

static void
plumbloop(Channel *ch, int fd)
{
	Plumbmsg *m;

	while(1){
		if((m = plumbrecv(fd)) == nil)
			threadexitsall("plumber gone");
		sendp(ch, m);
	}
}

static void
plumbshowmail(void*)
{
	threadsetname("plumbshow %s", mbox.path);
	plumbloop(mbox.show, plumbshowmailfd);
}

static void
plumbseemail(void*)
{
	threadsetname("plumbsee %s", mbox.path);
	plumbloop(mbox.see, plumbseemailfd);
}

static void
plumbsendmail(void*)
{
	threadsetname("plumbsend %s", mbox.path);
	plumbloop(mbox.send, plumbsendmailfd);
}

static void
eventread(void*)
{
	Event *ev;

	threadsetname("mbevent %s", mbox.path);
	while(1){
		ev = emalloc(sizeof(Event));
		if(winevent(&mbox, ev) == -1)
			break;
		sendp(mbox.event, ev);
	}
	sendp(mbox.event, nil);
	threadexits(nil);
}

static int
ideq(char *a, char *b)
{
	if(a == nil || b == nil)
		return 0;
	return strcmp(a, b) == 0;
}

static int
cmpmesg(void *pa, void *pb)
{
	Mesg *a, *b;

	a = *(Mesg**)pa;
	b = *(Mesg**)pb;

	return b->time - a->time;
}

static int
rcmpmesg(void *pa, void *pb)
{
	Mesg *a, *b;

	a = *(Mesg**)pa;
	b = *(Mesg**)pb;

	return a->time - b->time;
}

static int
mesglineno(Mesg *msg, int *depth)
{
	Mesg *p, *m;
	int i, o, n, d;

	o = 0;
	d = 0;
	n = 1;

	/* Walk up to root, counting depth in the thread */
	p = msg;
	while(p->parent != nil){
		m = p;
		p = p->parent;
		for(i = 0; i < p->nchild; i++){
			if(p->child[i] == m)
				break;
			o += p->child[i]->nsub + 1;
		}
		if(!(p->state & Sdummy)){
			o++;
			d++;
		}
	}

	/* Find the thread in the thread list */
	for(i = 0; i < mbox.nmesg; i++){
		m = mbox.mesg[i];
		if(m == p)
			break;
		if(m->state & Stoplev){
			n += mbox.mesg[i]->nsub;
			if(!(m->state & Sdummy))
				n++;
		}

	}
	if(depth != nil)
		*depth = d;
	assert(n + o <= mbox.nmesg);
	return n + o;
}

static int
addchild(Mesg *p, Mesg *m, int d)
{
	Mesg *q;

	assert(m->parent == nil);
	for(q = p; q != nil; q = q->parent){
		if(ideq(m->messageid, q->messageid)){
			fprint(2, "wonky message replies to self\n");
			return 0;
		}
		if(m->time > q->time)
			q->time = m->time;
	}
	for(q = p; q != nil; q = q->parent)
		q->nsub += d;
	p->child = erealloc(p->child, ++p->nchild*sizeof(Mesg*));
	p->child[p->nchild - 1] = m;
	qsort(p->child, p->nchild, sizeof(Mesg*), rcmpmesg);
	m->parent = p;
	return 1;
}

static int
slotfor(Mesg *m)
{
	int i;

	for(i = 0; i < mbox.nmesg; i++)
		if(cmpmesg(&mbox.mesg[i], &m) >= 0)
			break;
	return i;
}

static void
removeid(Mesg *m)
{
	Mesg *e;
	int i;

	/* Dummies don't go in the table */
	if(m->state & Sdummy)
		return;
	i = m->hash % mbox.hashsz;
	while(1){
		e = mbox.hash[i];
		if(e == nil)
			return;
		if(e != &dead && e->hash == m->hash && strcmp(e->messageid, m->messageid) == 0){
			mbox.hash[i] = &dead;
			mbox.ndead++;
		}
		i = (i + 1) % mbox.hashsz;
	}
}

Mesg*
lookupid(char *msgid)
{
	u32int h, i;
	Mesg *e;

	if(msgid == nil || strlen(msgid) == 0)
		return nil;
	h = strhash(msgid);
	i = h % mbox.hashsz;
	while(1){
		e = mbox.hash[i];
		if(e == nil)
			return nil;
		if(e != &dead && e->hash == h && strcmp(e->messageid, msgid) == 0)
			return e;
		i = (i + 1) % mbox.hashsz;
	}
}

static void
addmesg(Mesg *m, int ins)
{
	Mesg *o, *e, **oldh;
	int i, oldsz, idx;

	/* 
	 * on initial load, it's faster to append everything then sort,
	 * but on subsequent messages it's better to just put it in the
	 * right place; we don't want to shuffle the already-sorted
	 * messages.
	 */
	if(mbox.nmesg == mbox.mesgsz){
		mbox.mesgsz *= 2;
		mbox.mesg = erealloc(mbox.mesg, mbox.mesgsz*sizeof(Mesg*));
	}
	if(ins)
		idx = slotfor(m);
	else
		idx = mbox.nmesg;
	memmove(&mbox.mesg[idx + 1], &mbox.mesg[idx], (mbox.nmesg - idx)*sizeof(Mesg*));
	mbox.mesg[idx] = m;
	mbox.nmesg++;
	if(m->messageid == nil)
		return;

	/* grow hash table, or squeeze out deadwood */
	if(mbox.hashsz <= 2*(mbox.nmesg + mbox.ndead)){
		oldsz = mbox.hashsz;
		oldh = mbox.hash;
		if(mbox.hashsz <= 2*mbox.nmesg)
			mbox.hashsz *= 2;
		mbox.ndead = 0;
		mbox.hash = emalloc(mbox.hashsz*sizeof(Mesg*));
		for(i = 0; i < oldsz; i++){
			if((o = oldh[i]) == nil)
				continue;
			mbox.hash[o->hash % mbox.hashsz] = o;
		}
		free(oldh);
	}
	i = m->hash % mbox.hashsz;
	while(1){
		e = mbox.hash[i % mbox.hashsz];
		if(e == nil || e == &dead)
			break;
		i = (i + 1) % mbox.hashsz;
	}
	mbox.hash[i] = m;
}

static Mesg *
placeholder(char *msgid, vlong time, int ins)
{
	Mesg *m;

	m = emalloc(sizeof(Mesg));
	m->state |= Sdummy|Stoplev;
	m->messageid = estrdup(msgid);
	m->hash = strhash(msgid);
	m->time = time;
	addmesg(m, ins);
	return m;
}

static Mesg*
change(char *name, char *digest)
{
	Mesg *m;
	char *f;

	if((m = mesglookup(name, digest)) == nil)
		return nil;
	if((f = rslurp(m, "flags", nil)) == nil)
		return nil;
	free(m->mflags);
	m->mflags = f;
	m->flags = 0;
	if(strchr(m->mflags, 'd')) m->flags |= Fdel;
	if(strchr(m->mflags, 's')) m->flags |= Fseen;
	if(strchr(m->mflags, 'a')) m->flags |= Fresp;
	return m;
}

static Mesg*
delete(char *name, char *digest)
{
	Mesg *m;

	if((m = mesglookup(name, digest)) == nil)
		return nil;
	m->flags |= Fdel;
	return m;
}

static Mesg*
load(char *name, char *digest, int ins)
{
	Mesg *m, *p;
	int d;

	if(strncmp(name, mbox.path, strlen(mbox.path)) == 0)
		name += strlen(mbox.path);
	if((m = mesgload(name)) == nil)
		goto error;

	if(digest != nil && strcmp(digest, m->digest) != 0)
		goto error;
	/* if we already have a dummy, populate it */
	d = 1;
	p = lookupid(m->messageid);
	if(p != nil && (p->state & Sdummy)){
		d = p->nsub + 1;
		m->child = p->child;
		m->nchild = p->nchild;
		m->nsub = p->nsub;
		mesgclear(p);
		memcpy(p, m, sizeof(*p));
		free(m);
		m = p;

	}else{
		/*
		 * if we raced a notify and a mailbox load, we
		 * can get duplicate load requests for the same
		 * name in the mailbox.
		 */
		if(p != nil && strcmp(p->name, m->name) == 0)
			goto error;
		addmesg(m, ins);
	}

	if(!threadsort || m->inreplyto == nil || ideq(m->messageid, m->inreplyto)){
		m->state |= Stoplev;
		return m;
	}
	p = lookupid(m->inreplyto);
	if(p == nil)
		p = placeholder(m->inreplyto, m->time, ins);
	if(!addchild(p, m, d))
		m->state |= Stoplev;
	return m;
error:
	mesgfree(m);
	return nil;
}

void
mbredraw(Mesg *m, int add, int rec)
{
	Biobuf *bfd;
	int ln, depth;

	ln = mesglineno(m, &depth);
	fprint(mbox.addr, "%d%s", ln, add?"-#0":"");
	bfd = bwindata(&mbox, OWRITE);
	showmesg(bfd, m, depth, rec);
	Bterm(bfd);

	/* highlight the redrawn message */
	fprint(mbox.addr, "%d%s", ln, add ? "-#0" : "");
	fprint(mbox.ctl, "dot=addr\n");
}

static void
mbload(void)
{
	int i, n, fd;
	Dir *d;

	mbox.mesgsz = 128;
	mbox.hashsz = 128;
	mbox.mesg = emalloc(mbox.mesgsz*sizeof(Mesg*));
	mbox.hash = emalloc(mbox.hashsz*sizeof(Mesg*));
	mbox.path = esmprint("%s/%s/", maildir, mailbox);
	cleanname(mbox.path);
	n = strlen(mbox.path);
	if(mbox.path[n - 1] != '/')
		mbox.path[n] = '/';
	if((fd = open(mbox.path, OREAD)) == -1)
		sysfatal("%s: open: %r", mbox.path);
	while(1){
		n = dirread(fd, &d);
		if(n == -1)
			sysfatal("%s read: %r", mbox.path);
		if(n == 0)
			break;
		for(i = 0; i < n; i++)
			if(strcmp(d[i].name, "ctl") != 0)
				load(d[i].name, nil, 0);
		free(d);
	}
	qsort(mbox.mesg, mbox.nmesg, sizeof(Mesg*), cmpmesg);	
}

static char*
getflag(Mesg *m)
{
	char* flag;

	flag = "★";
	if(m->flags & Fseen)	flag = " ";
	if(m->flags & Fresp)	flag = "←";
	if(m->flags & Fdel)	flag = "∉";
	if(m->flags & Ftodel)	flag = "∉";
	return flag;
}

static void
printw(Biobuf *bp, char *s, int width)
{
	char *dots;

	if(width <= 0)
		Bprint(bp, "%s", s);
	else{
		dots = "";
		if(utflen(s) > width){
			width -= 3;
			dots = "...";
		}
		Bprint(bp, "%*.*s%s", -width, width, s, dots);
	}
}

/*
 * Message format string:
 * ======================
  * %s: subject
 * %f: from address
 * %F: name + from address
 * %t: to address
 * %c: CC address
 * %r: replyto address
 * %[...]: string to display for child messages
 * %{...}: date format string
 */
static void
fmtmesg(Biobuf *bp, char *fmt, Mesg *m, int depth)
{
	char *p, *e, buf[64];
	int width, i, indent;
	Tm tm;

	Bprint(bp, "%-6s\t%s ", m->name, getflag(m));
	for(p = fmt; *p; p++){
		if(*p != '%'){
			Bputc(bp, *p);
			continue;
		}
		p++;
		width = 0;
		indent = 0;
		while(*p == '>'){
			p++;
			indent++;
		}
		while('0'<=*p && *p<='9')
			width = width * 10 + *p++ - '0';
		for(i = 0; indent && i < depth; i++){
			Bputc(bp, '\t');
			width -= 4;
			if(indent == 1)
				break;
		}
		switch(*p){
		case '%':
			Bprint(bp, "%%");
			break;
		case 'i':
			if(depth > 0)
				depth = 1;
		case 'I':
			for(i = 0; i < depth; i++){
				if(width>0)
					Bprint(bp, "%*s", width, "");
				else
					Bprint(bp, "\t");
			}
			break;
		case 's':
			printw(bp, m->subject, width);
			break;
		case 'f':
			printw(bp, m->from, width);
			break;
		case 'F':
			printw(bp, m->fromcolon, width);
			break;
		case 't':
			printw(bp, m->to, width);
			break;
		case 'c':
			printw(bp, m->cc, width);
			break;
		case 'r':
			printw(bp, m->replyto, width);
			break;
		case '[':
			p++;
			if((e = strchr(p, ']')) == nil)
				sysfatal("missing closing '}' in %%{");
			if(e - p >= sizeof(buf) - 1)
				sysfatal("%%{} contents too long");
			snprint(buf, sizeof(buf), "%.*s", (int)(e - p), p);
			if(depth > 0)
				Bprint(bp, "%s", buf);
			p = e;
			break;
		case '{':
			p++;
			if((e = strchr(p, '}')) == nil)
				sysfatal("missing closing '}' in %%{");
			if(e - p >= sizeof(buf) - 1)
				sysfatal("%%{} contents too long");
			snprint(buf, sizeof(buf), "%.*s", (int)(e - p), p);
			tmtime(&tm, m->time, nil);
			Bprint(bp, "%τ", tmfmt(&tm, buf));
			p = e;
			break;
		default:
			sysfatal("invalid directive '%%%c' in format string", *p);
			break;
		}
	}
	Bputc(bp, '\n');
}


static void
showmesg(Biobuf *bfd, Mesg *m, int depth, int recurse)
{
	int i;

	if(!(m->state & Sdummy)){
		fmtmesg(bfd, listfmt, m, depth);
		depth++;
	}
	if(recurse && mbox.view != Vflat)
		for(i = 0; i < m->nchild; i++)
			showmesg(bfd, m->child[i], depth, recurse);
}

static void
mark(char **f, int nf, char *fstr, int flags, int add)
{
	char *sel, *p, *q, *e, *path;
	int i, q0, q1, fd;
	Mesg *m;

	if(flags == 0)
		return;
	wingetsel(&mbox, &q0, &q1);
	if(nf == 0){
		sel = winreadsel(&mbox);
		for(p = sel; p != nil; p = e){
			if((e = strchr(p, '\n')) != nil)
				*e++ = 0;
			if(!matchmesg(&mbox, p))
				continue;
			if((q = strchr(p, '/')) != nil)
				q[1] = 0;
			if((m = mesglookup(p, nil)) != nil){
				if(add)
					m->flags |= flags;
				else
					m->flags &= ~flags;
				if(fstr != nil && strlen(fstr) != 0){
					path = estrjoin(mbox.path, "/", m->name, "/flags", nil);
					if((fd = open(path, OWRITE)) != -1){
						fprint(fd, fstr);
						close(fd);
					}
					free(path);
				}
				mbredraw(m, 0, 0);
			}
		}
		free(sel);
	}else for(i = 0; i < nf; i++){
		if((m = mesglookup(f[i], nil)) != nil){
			m->flags |= flags;
			mbredraw(m, 0, 0);
		}
	}
	winsetsel(&mbox, q0, q1);
}

static void
mbmark(char **f, int nf)
{
	int add, flg;

	if(nf == 0){
		fprint(2, "missing fstr");
		return;
	}
	if((flg = mesgflagparse(f[0], &add)) == -1){
		fprint(2, "Mark: invalid flags %s\n", f[0]);
		return;
	}
	mark(f+1, nf-1, f[0], flg, add);
}

static void
relinkmsg(Mesg *p, Mesg *m)
{
	Mesg *c, *pp;
	int i, j;

	/* remove child, preserving order */
	j = 0;
	for(i = 0; p && i < p->nchild; i++){
		if(p->child[i] != m)
			p->child[j++] = p->child[i];
	}
	p->nchild = j;
	for(pp = p; pp != nil; pp = pp->parent)
		pp->nsub -= m->nsub + 1;

	/* reparent children */
	for(i = 0; i < m->nchild; i++){
		c = m->child[i];
		c->parent = nil;
		addchild(p, c, c->nsub + 1);
	}
}

static void
mbflush(char **, int)
{
	int i, j, ln, fd;
	char *path;
	Mesg *m, *p;

	i = 0;
	path = estrjoin(maildir, "/ctl", nil);
	fd = open(path, OWRITE);
	free(path);
	if(fd == -1)
		sysfatal("open mbox: %r");
	while(i < mbox.nmesg){
		m = mbox.mesg[i];
		if((m->state & Sopen) || !(m->flags & (Fdel|Ftodel))){
			i++;
			continue;
		}
		ln = mesglineno(m, nil);
		fprint(mbox.addr, "%d,%d", ln, ln+m->nsub);
		write(mbox.data, "", 0);
		if(m->flags & Ftodel)
			fprint(fd, "delete %s %d", mailbox, atoi(m->name));

		p = m->parent;
		removeid(m);
		if(p == nil && m->nsub != 0){
			p = placeholder(m->messageid, m->time, 1);
			p->nsub = m->nsub + 1;
			mbox.mesg[i] = p;
		}
		if(p != nil)
			relinkmsg(p, m);
		for(j = 0; j < m->nchild; j++)
			mbredraw(m->child[j], 1, 1);
		memmove(&mbox.mesg[i], &mbox.mesg[i+1], (mbox.nmesg - i)*sizeof(Mesg*));
		mbox.nmesg--;
 	}
	close(fd);
	fprint(mbox.ctl, "clean\n");
}

static void
mbcompose(char **, int)
{
	compose("", nil, 0);
}

static void
delmesg(char **f, int nf)
{
	mark(f, nf, nil, Ftodel, 1);
}

static void
undelmesg(char **f, int nf)
{
	mark(f, nf, nil, Ftodel, 0);
}

static void
showlist(void)
{
	Biobuf *bfd;
	Mesg *m;
	int i;

	bfd = bwinopen(&mbox, "data", OWRITE);
	for(i = 0; i < mbox.nmesg; i++){
		m = mbox.mesg[i];
		if(mbox.view == Vflat || m->state & (Sdummy|Stoplev))
			showmesg(bfd, m, 0, 1);
	}
	Bterm(bfd);
	fprint(mbox.addr, "0");
	fprint(mbox.ctl, "dot=addr\n");
	fprint(mbox.ctl, "show\n");
}

static void
quitall(char **, int)
{
	Mesg *m;
	Comp *c;

	if(mbox.nopen > 0 && !mbox.canquit){
		fprint(2, "Del: %d open messages\n", mbox.nopen);
		mbox.canquit = 1;
		return;
	}
	for(m = mbox.openmesg; m != nil; m = m->qnext)
		fprint(m->ctl, "del\n");
	for(c = mbox.opencomp; c != nil; c = c->qnext)
		fprint(c->ctl, "del\n");
	fprint(mbox.ctl, "del\n");
	threadexitsall(nil);
}

/*
 * shuffle a message to the right location
 * in the list without doing a full sort.
 */
static void
reinsert(Mesg *m)
{
	int i, idx;

	idx = slotfor(m);
	for(i = idx; i < mbox.nmesg; i++)
		if(mbox.mesg[i] == m)
			break;
	memmove(&mbox.mesg[idx + 1], &mbox.mesg[idx], (i - idx)*sizeof(Mesg*));
	mbox.mesg[idx] = m;
}

static void
changemesg(Plumbmsg *pm)
{
	char *digest, *action;
	Mesg *m, *r;
	int ln, nr;

	digest = plumblookup(pm->attr, "digest");
	action = plumblookup(pm->attr, "mailtype");
//	fprint(2, "changing message %s, %s %s\n", action, pm->data, digest);
	if(strcmp(action, "new") == 0){
		if((m = load(pm->data, digest, 1)) == nil)
			return;
		for(r = m; r->parent != nil; r = r->parent)
			/* nothing */;
		/* Bump whole thread up in list */
		if(r->nsub > 0){
			ln = mesglineno(r, nil);
			nr = r->nsub-1;
			if(!(r->state & Sdummy))
				nr++;
			/*
			 * We can end up with an empty container
			 * in an edge case.
			 *
			 * Imagine we have a dummy message with
			 * a child, and that child gets deleted,
			 * then a new message comes in replying
			 * to that dummy.
			 *
			 * In this case, r->nsub == 1 due to the
			 * newly added message, so nr=0.
			 * in that case, skip the redraw, and
			 * reinsert the dummy in the right place.
			 */
			if(nr > 0){
				fprint(mbox.addr, "%d,%d", ln, ln+nr-1);
				write(mbox.data, "", 0);
			}
			reinsert(r);
		}
		mbredraw(r, 1, 1);
	}else if(strcmp(action, "delete") == 0){
		if((m = delete(pm->data, digest)) != nil)
			mbredraw(m, 0, 0);
	}else if(strcmp(action, "modify") == 0){
		if((m = change(pm->data, digest)) != nil)
			mbredraw(m, 0, 0);
	}
}

static void
viewmesg(Plumbmsg *pm)
{
	Mesg *m;
	m = mesgopen(pm->data, plumblookup(pm->attr, "digest"));
	if(m != nil){
		fprint(mbox.addr, "%d", mesglineno(m, nil));
		fprint(mbox.ctl, "dot=addr\n");
		fprint(mbox.ctl, "show\n");
	}
}

static void
redraw(char **, int)
{
	fprint(mbox.addr, ",");
	showlist();
}

static void
nextunread(char **, int)
{
	fprint(mbox.ctl, "addr=dot\n");
	fprint(mbox.addr, "/^[0-9]+\\/ *\t★.*");
	fprint(mbox.ctl, "dot=addr\n");
	fprint(mbox.ctl, "show\n");
}

Fn mboxfn[] = {
	{"Put",	mbflush},
	{"Mail", mbcompose},
	{"Delmesg", delmesg},
	{"Undelmesg", undelmesg},
	{"Del", quitall},
	{"Redraw", redraw},
	{"Next", nextunread},
	{"Mark", mbmark},
#ifdef NOTYET
	{"Filter", filter},
	{"Get", mbrefresh},
#endif
	{nil}
};

static void
doevent(Event *ev)
{
	char *f[32];
	int nf;
	Fn *p;

	if(ev->action != 'M')
		return;
	switch(ev->type){
	case 'l':
	case 'L':
		if(matchmesg(&mbox, ev->text))
			if(mesgopen(ev->text, nil) != nil)
				break;
		winreturn(&mbox, ev);
		break;
	case 'x':
	case 'X':
		if((nf = tokenize(ev->text, f, nelem(f))) == 0)
			return;
		for(p = mboxfn; p->fn != nil; p++)
			if(strcmp(p->name, f[0]) == 0 && p->fn != nil){
				p->fn(&f[1], nf - 1);
				break;
			}
		if(p->fn == nil)
			winreturn(&mbox, ev);
		else if(p->fn != quitall)
			mbox.canquit = 0;
		break;
	}
}

static void
execlog(void*)
{
	Waitmsg *w;

	while(1){
		w = recvp(cwait);
		if(w->msg[0] != 0)
			fprint(2, "%d: %s\n", w->pid, w->msg);
		free(w);
	}
} 

static void
mbmain(void *cmd)
{
	Event *ev;
	Plumbmsg *psee, *pshow, *psend;

	Alt a[] = {
	[Cevent]	= {mbox.event, &ev, CHANRCV},
	[Cseemail]	= {mbox.see, &psee, CHANRCV},
	[Cshowmail]	= {mbox.show, &pshow, CHANRCV},
	[Csendmail]	= {mbox.send, &psend, CHANRCV},
	[Nchan]		= {nil,	nil, CHANEND},
	};

	threadsetname("mbox %s", mbox.path);
	wininit(&mbox, mbox.path);
	wintagwrite(&mbox, "Put Mail Delmesg Undelmesg Next ");
	showlist();
	fprint(mbox.ctl, "dump %s\n", cmd);
	fprint(mbox.ctl, "clean\n");
	procrfork(eventread, nil, Stack, RFNOTEG);
	while(1){
		switch(alt(a)){
		case Cevent:
			doevent(ev);
			free(ev);
			break;
		case Cseemail:
			changemesg(psee);
			plumbfree(psee);
			break;
		case Cshowmail:
			viewmesg(pshow);
			plumbfree(pshow);
			break;
		case Csendmail:
			compose(psend->data, nil, 0);
			plumbfree(psend);
			break;
		}
	}
}

static void
usage(void)
{
	fprint(2, "usage: %s [-T] [-m mailfs] [-s] [-f format] [mbox]\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char **argv)
{
	Fmt fmt;
	char *cmd;
	int i;

	mbox.view = Vgroup;
	doquote = needsrcquote;
	quotefmtinstall();
	tmfmtinstall();

	fmtstrinit(&fmt);
	for(i = 0; i < argc; i++)
		fmtprint(&fmt, "%q ", argv[i]);
	cmd = fmtstrflush(&fmt);
	if(cmd == nil)
		sysfatal("out of memory");

	ARGBEGIN{
	case 'm':
		maildir = EARGF(usage());
		break;
	case 'T':
		mbox.view = Vflat;
		break;
	case 's':
		sender++;
		break;
	case 'f':
		listfmt = EARGF(usage());
		break;
	case 'O':
		savebox = nil;
		break;
	case 'o':
		savebox = EARGF(usage());
		break;
	default:
		usage();
		break;
	}ARGEND;

	if(argc > 1)
		usage();
	if(argc == 1)
		mailbox = argv[0];

	mesgpat = regcomp("([0-9]+)(/.*)?");
	cwait = threadwaitchan();

	/* open these early so we won't miss messages while loading */
	mbox.event = chancreate(sizeof(Event*), 0);
	mbox.see = chancreate(sizeof(Plumbmsg*), 0);
	mbox.show = chancreate(sizeof(Plumbmsg*), 0);
	mbox.send = chancreate(sizeof(Plumbmsg*), 0);

	plumbsendfd = plumbopen("send", OWRITE|OCEXEC);
	plumbseemailfd = plumbopen("seemail", OREAD|OCEXEC);
	plumbshowmailfd = plumbopen("showmail", OREAD|OCEXEC);

	mbload();
	procrfork(plumbseemail, nil, Stack, RFNOTEG);
	procrfork(plumbshowmail, nil, Stack, RFNOTEG);

	/* avoid duplicate sends when multiple mailboxes are open */
	if(sender || strcmp(mailbox, "mbox") == 0){
		plumbsendmailfd = plumbopen("sendmail", OREAD|OCEXEC);
		procrfork(plumbsendmail, nil, Stack, RFNOTEG);
	}
	threadcreate(execlog, nil, Stack);
	threadcreate(mbmain, cmd, Stack);
	threadexits(nil);
}
