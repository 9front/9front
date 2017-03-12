#include "common.h"
#include <auth.h>
#include <libsec.h>
#include <bin.h>
#include "dat.h"

#define idprint(...)	if(1) fprint(2, __VA_ARGS__); else {}
enum{
	Maxver	= 10,
};
static char *magictab[Maxver] = {
[4]	"idx magic v4\n",
[7]	"idx magic v7\n",
};
static int fieldstab[Maxver] = {
[4]	19,
[7]	21,
};

static	char	*magic;
static	int	Idxfields;
static	int	lineno;
static	int	idxver;

int
newid(void)
{
	static int id;

	return ++id;
}

void*
emalloc(ulong n)
{
	void *p;

	p = mallocz(n, 1);
	if(!p)
		sysfatal("malloc %lud: %r", n);
	setmalloctag(p, getcallerpc(&n));
	return p;
}
	
static int
Afmt(Fmt *f)
{
	char buf[SHA1dlen*2 + 1];
	uchar *u, i;

	u = va_arg(f->args, uchar*);
	if(u == 0 && f->flags & FmtSharp)
		return fmtstrcpy(f, "-");
	if(u == 0)
		return fmtstrcpy(f, "<nildigest>");
	for(i = 0; i < SHA1dlen; i++)
		sprint(buf + 2*i, "%2.2ux", u[i]);
	return fmtstrcpy(f, buf);
}

static int
Dfmt(Fmt *f)
{
	char buf[32];
	int seq;
	uvlong v;

	v = va_arg(f->args, uvlong);
	seq = v & 0xff;
	if(seq > 99)
		seq = 99;
	snprint(buf, sizeof buf, "%llud.%.2d", v>>8, seq);
	return fmtstrcpy(f, buf);
}

static Mailbox*
shellmailbox(char *path)
{
	Mailbox *mb;

	mb = malloc(sizeof *mb);
	if(mb == 0)
		sysfatal("malloc");
	memset(mb, 0, sizeof *mb);
	snprint(mb->path, sizeof mb->path, "%s", path);
	mb->id = newid();
	mb->root = newmessage(nil);
	mb->mtree = mkavltree(mtreecmp);
	return mb;
}

void
shellmailboxfree(Mailbox*)
{
}

Message*
newmessage(Message *parent)
{
	static int id;
	Message *m;

//	msgallocd++;

	m = mallocz(sizeof *m, 1);
	if(m == 0)
		sysfatal("malloc");
	m->disposition = Dnone;
//	m->type = newrefs("text/plain");
//	m->charset = newrefs("iso-8859-1");
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

void
unnewmessage(Mailbox *mb, Message *parent, Message *m)
{
	assert(parent->subname > 0);
//	delmessage(mb, m);
	USED(mb, m);
	parent->subname -= 1;
}


static int
validmessage(Mailbox *mb, Message *m, int level)
{
	if(level){
		if(m->digest != 0)
			goto lose;
		if(m->fileid <= 1000000ull<<8)
		if(m->fileid != 0)
			goto lose;
	}else{
		if(m->digest == 0)
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
	fprint(2, "invalid cache[%d] %#A size %ld %D\n", level, m->digest, m->size, m->fileid);
	return 0;
}

static char*
∫(char *x)
{
	if(x && *x)
		return x;
	return nil;
}

static char*
brdstr(Biobuf *b, int c, int eat)
{
	char *s;

	s = Brdstr(b, c, eat);
	if(s)
		lineno++;
	return s;
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
	uchar t[SHA1dlen];
	int i;

	if(strcmp(s, "-") == 0)
		return 0;
	if(strlen(s) != 2*SHA1dlen){
		fprint(2, "bad digest %s\n", s);
		return 0;
	}
	for(i = 0; i < SHA1dlen; i++)
		t[i] = nibble(s[2*i])<<4 | nibble(s[2*i + 1]);
	memmove(s, t, SHA1dlen);
	return (uchar*)s;
}

static Message*
findmessage(Mailbox *, Message *parent, int n)
{
	Message *m;

	for(m = parent->part; m; m = m->next)
		if(!m->digest && n-- == 0)
			return m;
	return 0;
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

static int
rdidx(Biobuf *b, Mailbox *mb, Message *parent, int npart, int level)
{
	char *f[50 + 1], *s;
	uchar *digest;
	int n, nparts, good, bad, redux;
	Message *m, **ll, *l;

	bad = good = redux = 0;
	ll = &parent->part;
	nparts = npart;
	for(; npart != 0 && (s = brdstr(b, '\n', 1)); npart--){
//if(lineno>18&&lineno<25)idprint("%d: %d [%s]\n", lineno, level, s);
		n = tokenize(s, f, nelem(f));
		if(n != Idxfields){
			print("%d: bad line\n", lineno);
			bad++;
			free(s);
			continue;
		}
		digest = hackdigest(f[0]);
		if(level == 0){
			if(digest == 0)
				idprint("%d: no digest\n", lineno);
			m = mtreefind(mb, digest);
		}else{
			m = findmessage(mb, parent, nparts - npart);
			if(m == 0){
			//	idprint("can't find message\n");
			}
		}
		if(m){
			/*
			 * read in mutable information.
			 * currently this is only flags
			 */
			idprint("%d seen before %d... %.2ux", level, m->id, m->cstate);
			redux++;
			m->flags |= strtoul(f[1], 0, 16);
			m->cstate &= ~Cidxstale;
			m->cstate |= Cidx;
			idprint("→%.2ux\n", m->cstate);
			free(s);

			if(m->nparts)
				rdidx(b, mb, m, m->nparts, level + 1);
			ll = &m->next;
			continue;
		}
		m = newmessage(parent);
//if(lineno>18&&lineno<25)idprint("%d: %d %d %A\n", lineno, level, m->id, digest);
//		idprint("%d new %d %#A \n", level, m->id, digest);
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
//		m->type = newrefs(f[14]);
		m->disposition = atoi(f[15]);
		m->size = strtoul(f[16], 0, 0);
		m->rawbsize = strtoul(f[17], 0, 0);
		switch(idxver){
		case 4:
			m->nparts = strtoul(f[18], 0, 0);
		case 7:
			m->ibadchars = strtoul(f[18], 0, 0);
			m->idxaux = ∫(f[19]);
			m->nparts = strtoul(f[20], 0, 0);
		}
		m->cstate &= ~Cidxstale;
		m->cstate |= Cidx;
		m->str = s;
//		free(s);
		if(!validmessage(mb, m, level)){
			/*
			 *  if this was an okay message, and somebody
			 * wrote garbage to the index file, we lose the msg.
			 */
			print("%d: !validmessage\n", lineno);
			bad++;
			unnewmessage(mb, parent, m);
			continue;
		}
		if(level == 0)
			m->inmbox = 1;
//		cachehash(mb, m);		/* hokey */
		l = *ll;
		*ll = m;
		ll = &m->next;
		*ll = l;
		good++;

		if(m->nparts){
//			fprint(2, "%d: %d parts [%s]\n", lineno, m->nparts, f[18]);
			rdidx(b, mb, m, m->nparts, level + 1);
		}
	}
	if(level == 0)
		print("idx: %d %d %d\n", good, bad, redux);
	return 0;
}

static int
verscmp(Biobuf *b)
{
	char *s;
	int i;

	if((s = brdstr(b, '\n', 0)) == 0)
		return -1;
	for(i = 0; i < Maxver; i++)
		if(magictab[i])
		if(strcmp(s, magictab[i]) == 0)
			break;
	free(s);
	if(i == Maxver)
		return -1;
	idxver = i;
	magic = magictab[i];
	Idxfields = fieldstab[i];
	fprint(2, "version %d\n", i);
	return 0;
}

int
mbvers(Biobuf *b)
{
	char *s;

	if(s = brdstr(b, '\n', 1)){
		free(s);
		return 0;
	}
	return -1;
}

int
ckidxfile(Mailbox *mb)
{
	char buf[Pathlen + 4];
	int r;
	Biobuf *b;

	snprint(buf, sizeof buf, "%s", mb->path);
	b = Bopen(buf, OREAD);
	if(b == nil)
		return -1;
	if(verscmp(b) == -1)
		return -1;
	if(idxver >= 7)
		mbvers(b);
	r = rdidx(b, mb, mb->root, -1, 0);
	Bterm(b);
	return r;
}

static char *bargv[] = {"/fd/0", 0};

void
main(int argc, char **argv)
{
	Mailbox *mb;

	fmtinstall('A', Afmt);
	fmtinstall('D', Dfmt);
	ARGBEGIN{
	}ARGEND
	if(*argv == 0)
		argv = bargv;
	for(; *argv; argv++){
		mb = shellmailbox(*argv);
		lineno = 0;
		if(ckidxfile(mb) == -1)
			fprint(2, "%s: %r\n", *argv);
		shellmailboxfree(mb);
	}
	exits("");
}
