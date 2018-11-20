#include "common.h"
#include <libsec.h>
#include "dat.h"

#define idprint(...)	if(iflag > 1) fprint(2, __VA_ARGS__); else {}
#define iprint(...)		if(iflag) fprint(2, __VA_ARGS__); else {}

static char *magic	= "idx magic v8\n";
static char *mbmagic	= "genericv1";
enum {
	Idxfields		= 22,

	Idxto		= 30000,		/* index timeout in ms */
	Idxstep		= 300,		/* sleep between tries */
};

typedef struct Intern Intern;
struct Intern
{
	Intern	*next;
	char	str[];
};

static Intern *itab[64];

char*
intern(char *s)
{
	Intern *i, **h;
	int n;

	h = &itab[hash(s) % nelem(itab)];
	for(i = *h; i != nil; i = i->next)
		if(strcmp(s, i->str) == 0)
			return i->str;
	n = strlen(s)+1;
	i = emalloc(sizeof(*i) + n);
	memmove(i->str, s, n);
	i->next = *h;
	*h = i;
	return i->str;
}

void
idxfree(Idx *i)
{
	if(i->str)
		free(i->str);
	else{
		free(i->digest);
		free(i->ffrom);
		free(i->from);
		free(i->to);
		free(i->cc);
		free(i->bcc);
		free(i->replyto);
		free(i->messageid);
		free(i->subject);
		free(i->sender);
		free(i->inreplyto);
		free(i->idxaux);
		free(i->filename);
	}
	memset(i, 0, sizeof *i);
}

static char*
∂(char *x)
{
	if(x)
		return x;
	return "";
}

static int
pridxmsg(Biobuf *b, Idx *x)
{
	Bprint(b, "%#A %ux %D %lud ", x->digest, x->flags&~Frecent, x->fileid, x->lines);
	Bprint(b, "%q %q %q %q %q ", ∂(x->ffrom), ∂(x->from), ∂(x->to), ∂(x->cc), ∂(x->bcc));
	Bprint(b, "%q %q %q %q %q ", ∂(x->replyto), ∂(x->messageid), ∂(x->subject), ∂(x->sender), ∂(x->inreplyto));
	Bprint(b, "%s %d %q %lud %lud ", x->type, x->disposition, ∂(x->filename), x->size, x->rawbsize);
	Bprint(b, "%lud %q %d\n", x->ibadchars, ∂(x->idxaux), x->nparts);
	return 0;
}

static int
pridx0(Biobuf *b, Mailbox *mb, Message *m, int l)
{
	for(; m; m = m->next){
		if(l == 0)
		if(insurecache(mb, m) == -1)
			continue;
		if(pridxmsg(b, m))
			return -1;
		if(m->part)
			pridx0(b, mb, m->part, l + 1);
		m->cstate &= ~Cidxstale;
		m->cstate |= Cidx;
		if(l == 0)
			msgdecref(mb, m);
	}
	return 0;
}

void
genericidxwrite(Biobuf *b, Mailbox*)
{
	Bprint(b, "%s\n", mbmagic);
}

static int
pridx(Biobuf *b, Mailbox *mb)
{
	int i;

	Bprint(b, magic);
	mb->idxwrite(b, mb);
	i = pridx0(b, mb, mb->root->part, 0);
	return i;
}

static char *eopen[] = {
	"not found",
	"does not exist",
	"file is locked",
	"file locked",
	"exclusive lock",
	0,
};

static char *ecreate[] = {
	"already exists",
	"file is locked",
	"file locked",
	"exclusive lock",
	0,
};

static int
bad(char **t)
{
	char buf[ERRMAX];
	int i;

	rerrstr(buf, sizeof buf);
	for(i = 0; t[i]; i++)
		if(strstr(buf, t[i]))
			return 0;
	return 1;
}

static int
forceexcl(int fd)
{
	int r;
	Dir *d;

	d = dirfstat(fd);
	if(d == nil)
		return 0;			/* ignore: assume file removed */
	if(d->mode & DMEXCL){
		free(d);
		return 0;
	}
	d->mode |= DMEXCL;
	d->qid.type |= QTEXCL;
	r = dirfwstat(fd, d);
	free(d);
	if(r == -1)
		return 0;			/* ignore unwritable (e.g dump) */
	close(fd);
	return -1;
}

static int
exopen(char *s)
{
	int i, fd;

	for(i = 0; i < Idxto/Idxstep; i++){
		if((fd = open(s, OWRITE|OTRUNC)) >= 0 || bad(eopen)){
			if(fd != -1 && forceexcl(fd) == -1)
				continue;
			return fd;
		}
		if((fd = create(s, OWRITE|OEXCL, DMTMP|DMEXCL|0600)) >= 0  || bad(ecreate))
			return fd;
		sleep(Idxstep);
	}
	werrstr("lock timeout");
	return -1;
}

static Message*
findmessage(Mailbox *, Message *parent, int n)
{
	Message *m;

	for(m = parent->part; m; m = m->next)
		if(m->digest == nil && n-- == 0)
			return m;
	return 0;
}

static int
validmessage(Mailbox *mb, Message *m, int level)
{
	if(level){
		if(m->digest != nil)
			goto lose;
		if(m->fileid <= 1000000ull<<8)
		if(m->fileid != 0)
			goto lose;
	}else{
		if(m->digest == nil)
			goto lose;
		if(m->size == 0)
			goto lose;
		if(m->fileid <= 1000000ull<<8)
			goto lose;
		if(mtreefind(mb, m->digest))
			goto lose;
	}
	return 1;
lose:
	eprint("invalid cache[%d] %#A size %ld %D\n", level, m->digest, m->size, m->fileid);
	return 0;
}

/*
 * n.b.: we don't insure this is the index version we last read.
 *
 * we may overwrite changes.  dualing deletes should sync eventually.
 * mboxsync should complain about missing messages but
 * mutable information (which is not in the email itself)
 * may be lost.
 */
int
wridxfile(Mailbox *mb)
{
	char buf[Pathlen + 4];
	int r, fd;
	Biobuf b;
	Dir *d;

	snprint(buf, sizeof buf, "%s.idx", mb->path);
	iprint("wridxfile %s\n", buf);
	if((fd = exopen(buf)) == -1){
		rerrstr(buf, sizeof buf);
		if(strcmp(buf, "no creates") != 0)
		if(strstr(buf, "file system read only") == 0)
			eprint("wridxfile: %r\n");
		return -1;
	}
	seek(fd, 0, 0);
	Binit(&b, fd, OWRITE);
	r = pridx(&b, mb);
	Bterm(&b);
	d = dirfstat(fd);
	if(d == 0)
		sysfatal("dirfstat: %r");
	mb->qid = d->qid;
	free(d);
	close(fd);
	return r;
}

static int
nibble(int c)
{
	if(c >= '0' && c <= '9')
		return c - '0';
	if(c < 0x20)
		c += 0x20;
	if(c >= 'a' && c <= 'f')
		return c - 'a'+10;
	return 0xff;
}

static uchar*
hackdigest(char *s)
{
	int i;

	if(strcmp(s, "-") == 0)
		return nil;
	if(strlen(s) != 2*SHA1dlen){
		eprint("bad digest %s\n", s);
		return nil;
	}
	for(i = 0; i < SHA1dlen; i++)
		((uchar*)s)[i] = nibble(s[2*i])<<4 | nibble(s[2*i + 1]);
	s[i] = 0;
	return (uchar*)s;
}

static uvlong
rdfileid(char *s, int level)
{
	char *p;
	uvlong uv;

	uv = strtoul(s, &p, 0);
	if((level == 0 && uv < 1000000) || *p != '.')
		return 0;
	return uv<<8 | strtoul(p + 1, 0, 10);
}

static char*
∫(char *x)
{
	if(x && *x)
		return x;
	return nil;
}

/*
 * strategy:  use top-level avl tree to merge index with
 * our ideas about the mailbox.  new or old messages
 * with corrupt index entries are marked Dead.  they
 * will be cleared out of the mailbox and are kept out
 * of the index.  when messages are marked Dead, a
 * reread of the mailbox is forced.
 *
 * side note.  if we get a new message while we are
 * running it is added to the list in order but m->id
 * looks out-of-order.  this is because m->id must
 * increase monotonicly.  a new instance of the fs
 * will result in a different ordering.
 */

static int
rdidx(Biobuf *b, Mailbox *mb, Message *parent, int npart, int level)
{
	char *f[Idxfields + 1], *s;
	uchar *digest;
	int n, flags, nparts, good, bad, redux;
	Message *m, **ll, *l;

	bad = good = redux = 0;
	ll = &parent->part;
	nparts = npart;
	for(; npart != 0 && (s = Brdstr(b, '\n', 1)); npart--){
		m = nil;
		digest = nil;
		n = tokenize(s, f, nelem(f));
		if(n != Idxfields){
dead:
			eprint("bad index %#A %d %d n=%d\n", digest, level, npart, n);
			bad++;
			free(s);
			if(level)
				return -1;
			if(m)
				m->deleted = Dead;
			continue;
		}
		digest = hackdigest(f[0]);
		if(level == 0){
			if(digest == nil)
				goto dead;
			m = mtreefind(mb, digest);
		} else
			m = findmessage(mb, parent, nparts - npart);
		if(m){
			/*
			 * read in mutable information.
			 * currently this is only flags
			 * and nparts.
			 */
			redux++;
			if(level == 0)
				m->deleted &= ~Dmark;
			n = m->nparts;
			m->nparts = strtoul(f[21], 0, 0);
			if(rdidx(b, mb, m, m->nparts, level + 1) == -1)
				goto dead;
			ll = &m->next;
			idprint("%d seen before %lud... %.2ux", level, m->id, m->cstate);
			flags = m->flags;
			m->flags |= strtoul(f[1], 0, 16);
			if(flags != m->flags || n != m->nparts)
				m->cstate |= Cidxstale;
			m->cstate |= Cidx;
			idprint("→%.2ux\n", m->cstate);
			free(s);
			continue;
		}
		m = newmessage(parent);
		idprint("%d new %lud %#A\n", level, m->id, digest);
		m->digest = digest;
		m->flags = strtoul(f[1], 0, 16);
		m->fileid = rdfileid(f[2], level);
		m->lines = atoi(f[3]);
		m->ffrom = ∫(f[4]);
		m->from = ∫(f[5]);
		m->to = ∫(f[6]);
		m->cc = ∫(f[7]);
		m->bcc = ∫(f[8]);
		m->replyto = ∫(f[9]);
		m->messageid = ∫(f[10]);
		m->subject = ∫(f[11]);
		m->sender = ∫(f[12]);
		m->inreplyto = ∫(f[13]);
		m->type = intern(f[14]);
		m->disposition = atoi(f[15]);
		m->filename = ∫(f[16]);
		m->size = strtoul(f[17], 0, 0);
		m->rawbsize = strtoul(f[18], 0, 0);
		m->ibadchars = strtoul(f[19], 0, 0);
		m->idxaux = ∫(f[20]);
		m->nparts = strtoul(f[21], 0, 0);

		m->cstate &= ~Cidxstale;
		m->cstate |= Cidx|Cnew;
		m->str = s;
		s = 0;

		if(!validmessage(mb, m, level))
			goto dead;
		if(level == 0){
			mtreeadd(mb, m);
			m->inmbox = 1;
		}
		cachehash(mb, m);		/* hokey */
		l = *ll;
		*ll = m;
		ll = &m->next;
		*ll = l;
		good++;

		if(m->nparts)
		if(rdidx(b, mb, m, m->nparts, level + 1) == -1)
			goto dead;
	}
	if(level == 0 && bad + redux > 0)
		iprint("idx: %d %d %d\n", good, bad, redux);
	if(bad)
		return -1;
	return 0;
}

/* bug: should check time. */
static int
qidcmp(int fd, Qid *q)
{
	int r;
	Dir *d;
	Qid q0;

	d = dirfstat(fd);
	if(!d)
		sysfatal("dirfstat: %r");
	r = 1;
	if(d->qid.path == q->path)
	if(d->qid.vers == q->vers)
		r = 0;
	q0 = *q;
	*q = d->qid;
	free(d);
	if(q0.path != 0 && r)
		iprint("qidcmp ... index changed [%ld .. %ld]\n", q0.vers, q->vers);
	return r;
}

static int
verscmp(Biobuf *b, Mailbox *mb)
{
	char *s;
	int n;

	n = -1;
	if(s = Brdstr(b, '\n', 0))
		n = strcmp(s, magic);
	free(s);
	if(n)
		return -1;
	n = -1;
	if(s = Brdstr(b, '\n', 0))
		n = mb->idxread(s, mb);
	free(s);
	return n;
}

int
genericidxread(char *s, Mailbox*)
{
	return strcmp(s, mbmagic);
}

void
genericidxinvalid(Mailbox *mb)
{
	if(mb->d)
		memset(&mb->d->qid, 0, sizeof mb->d->qid);
	mb->waketime = time(0);
}

void
mark(Mailbox *mb)
{
	Message *m;

	for(m = mb->root->part; m != nil; m = m->next)
		m->deleted |= Dmark;
}

int
unmark(Mailbox *mb)
{
	int i;
	Message *m;

	i = 0;
	for(m = mb->root->part; m != nil; m = m->next)
		if(m->deleted & Dmark){
			i++;
			m->deleted &= ~Dmark;	/* let mailbox scan figure this out.  BOTCH?? */
		}
	return i;
}

int
rdidxfile0(Mailbox *mb)
{
	char buf[Pathlen + 4];
	int r, v;
	Biobuf *b;

	snprint(buf, sizeof buf, "%s.idx", mb->path);
	b = Bopen(buf, OREAD);
	if(b == nil)
		return -2;
	if(qidcmp(Bfildes(b), &mb->qid) == 0)
		r = 0;
	else if(verscmp(b, mb) == -1)
		r = -1;
	else{
		mark(mb);
		r = rdidx(b, mb, mb->root, -1, 0);
		v = unmark(mb);
		if(r == 0 && v > 0)
			r = -1;
	}
	Bterm(b);
	return r;
}

int
rdidxfile(Mailbox *mb)
{
	int r;

	r = rdidxfile0(mb);
	if(r == -1 && mb->idxinvalid)
		mb->idxinvalid(mb);
	return r;
}
