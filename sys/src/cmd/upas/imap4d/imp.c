#include "imap4d.h"

static	char	magic[]	= "imap internal mailbox description\n";

/* another appearance of this nasty hack. */
typedef struct{
	Avl;
	Msg	*m;
}Mtree;

static	Avltree	*mtree;
static	Bin	*mbin;

static int
mtreecmp(Avl *va, Avl *vb)
{
	Mtree *a, *b;

	a = (Mtree*)va;
	b = (Mtree*)vb;
	return strcmp(a->m->info[Idigest], b->m->info[Idigest]);
}

static Namedint	flagcmap[Nflags] =
{
	{"s",	Fseen},
	{"a",	Fanswered},
	{"f",	Fflagged},
	{"D",	Fdeleted},
	{"d",	Fdraft},
	{"r",	Frecent},
};

static int
parseflags(char *flags)
{
	int i, f;

	f = 0;
	for(i = 0; i < Nflags; i++){
		if(flags[i] == '-')
			continue;
		if(flags[i] != flagcmap[i].name[0])
			return 0;
		f |= flagcmap[i].v;
	}
	return f;
}

static int
impflags(Box *box, Msg *m, char *flags)
{
	int f;

	f = parseflags(flags);
	/*
	 * recent flags are set until the first time message's box is selected or examined.
	 * it may be stored in the file as a side effect of a status or subscribe command;
	 * if so, clear it out.
	 */
	if((f & Frecent) && strcmp(box->fs, "imap") == 0)
		box->dirtyimp = 1;
	f |= m->flags & Frecent;

	/*
	 * all old messages with changed flags should be reported to the client
	 */
	if(m->uid && m->flags != f){
		box->sendflags = 1;
		m->sendflags = 1;
	}
	m->flags = f;
	return 1;
}

/*
 * considerations:
 * . messages can be deleted by another agent
 * . we might still have a Msg for an expunged message,
 *	because we haven't told the client yet.
 * . we can have a Msg without a .imp entry.
 * . flag information is added at the end of the .imp by copy & append
 */

static int
rdimp(Biobuf *b, Box *box)
{
	char *s, *f[4];
	uint u;
	Msg *m, m0;
	Mtree t, *p;

	memset(&m0, 0, sizeof m0);
	for(; s = Brdline(b, '\n'); ){
		s[Blinelen(b) - 1] = 0;
		if(tokenize(s, f, nelem(f)) != 3)
			return -1;
		u = strtoul(f[1], 0, 10);

		memset(&t, 0, sizeof t);
		m0.info[Idigest] = f[0];
		t.m = &m0;
		p = (Mtree*)avllookup(mtree, &t, 0);
		if(p){
			m = p->m;
			if(m->uid && m->uid != u){
				ilog("dup? %ud %ud %s", u, m->uid, f[0]);
				continue;
			}
			if(m->uid >= box->uidnext){
				ilog("uid %ud >= %ud\n", m->uid, box->uidnext);
				box->uidnext = m->uid;
			}
			if(m->uid == 0)
				m->flags = 0;
			if(impflags(box, m, f[2]) == -1)
				return -1;
			m->uid = u;
		}else{
			/*
			 * message has been deleted.
			 */
//			ilog("flags, uid dropped on floor [%s, %ud]", m0.info[Idigest], u);
		}
	}
	return 0;
}

enum{
	Rmagic,
	Rrdstr,
	Rtok,
	Rvalidity,
	Ruidnext,
};

static char *rtab[] = {
	"magic",
	"rdstr",
	"tok",
	"val",
	"uidnext"
};

char*
sreason(int r)
{
	if(r >= 0 && r <= nelem(rtab))
		return rtab[r];
	return "*GOK*";
}

static int
verscmp(Biobuf *b, Box *box, int *reason)
{
	char *s, *f[3];
	int n;
	uint u, v;

	n = -1;
	*reason = Rmagic;
	if(s = Brdstr(b, '\n', 0))
		n = strcmp(s, magic);
	free(s);
	if(n == -1)
		return -1;
	n = -1;
	v = box->uidvalidity;
	if((s = Brdstr(b, '\n', 1)) && ++*reason)
	if(tokenize(s, f, nelem(f)) == 2 && ++*reason)
	if((u = strtoul(f[0], 0, 10)) == v || v == 0 && ++*reason)
	if((v = strtoul(f[1], 0, 10)) >= box->uidnext && ++*reason){
		box->uidvalidity = u;
		box->uidnext = v;
		n = 0;
	}
	free(s);
	return n;
}

int
parseimp(Biobuf *b, Box *box)
{
	int r, reason;
	Msg *m;
	Mtree *p;

	if(verscmp(b, box, &reason) == -1)
		return -1;
	mtree = avlcreate(mtreecmp);
	r = 0;
	for(m = box->msgs; m; m = m->next)
		r++;
	p = binalloc(&mbin, r*sizeof *p, 1);
	if(p == nil)
		bye("no memory");
	for(m = box->msgs; m; m = m->next){
		p->m = m;
		avlinsert(mtree, p);
		p++;
	}
	r = rdimp(b, box);
	binfree(&mbin);
	free(mtree);
	return r;
}

static void
wrimpflags(char *buf, int flags, int killrecent)
{
	int i;

	if(killrecent)
		flags &= ~Frecent;
	memset(buf, '-', Nflags);
	for(i = 0; i < Nflags; i++)
		if(flags & flagcmap[i].v)
			buf[i] = flagcmap[i].name[0];
	buf[i] = 0;
}

int
wrimp(Biobuf *b, Box *box)
{
	char buf[16];
	int i;
	Msg *m;

	box->dirtyimp = 0;
	Bprint(b, "%s", magic);
	Bprint(b, "%.*ud %.*ud\n", Nuid, box->uidvalidity, Nuid, box->uidnext);
	i = strcmp(box->fs, "imap") == 0;
	for(m = box->msgs; m != nil; m = m->next){
		if(m->expunged)
			continue;
		wrimpflags(buf, m->flags, i);
		Bprint(b, "%.*s %.*ud %s\n", Ndigest, m->info[Idigest], Nuid, m->uid, buf);
	}
	return 0;
}

static uint
scanferdup(Biobuf *b, char *digest, int *flags, vlong *pos)
{
	char *s, *f[4];
	uint uid;

	uid = 0;
	for(; s = Brdline(b, '\n'); ){
		s[Blinelen(b) - 1] = 0;
		if(tokenize(s, f, nelem(f)) != 3)
			return ~0;
		if(strcmp(f[0], digest) == 0){
			uid = strtoul(f[1], 0, 10);
//			fprint(2, "digest %s matches uid %ud\n", f[0], uid);
			*flags |= parseflags(f[2]);
			break;
		}
		*pos += Blinelen(b);
	}
	return uid;
}

int
appendimp(char *bname, char *digest, int flags, Uidplus *u)
{
	char buf[16], *iname;
	int fd, reason;
	uint dup;
	vlong pos;
	Biobuf b;
	Box box;

	dup = 0;
	pos = 0;
	memset(&box, 0, sizeof box);
	iname = impname(bname);
	fd = cdopen(mboxdir, iname, ORDWR);
	if(fd == -1){
		fd = cdcreate(mboxdir, iname, OWRITE, 0664);
		if(fd == -1)
			return -1;
		box.uidvalidity = time(0);
		box.uidnext = 1;
	}else{
		dup = ~0;
		Binit(&b, fd, OREAD);
		if(verscmp(&b, &box, &reason) == -1)
			ilog("bad verscmp %s", sreason(reason));
		else{
			pos = Bseek(&b, 0, 1);
			dup = scanferdup(&b, digest, &flags, &pos);
		}
		Bterm(&b);
	}
	if(dup == ~0){
		close(fd);
		return -1;
	}
	Binit(&b, fd, OWRITE);
	if(dup == 0){
		Bseek(&b, 0, 0);
		Bprint(&b, "%s", magic);
		Bprint(&b, "%.*ud %.*ud\n", Nuid, box.uidvalidity, Nuid, box.uidnext + 1);
		Bseek(&b, 0, 2);
	}else
 		Bseek(&b, pos, 0);
	wrimpflags(buf, flags, 0);
	Bprint(&b, "%.*s %.*ud %s\n", Ndigest, digest, Nuid, dup? dup: box.uidnext, buf);
	Bterm(&b);
	close(fd);
	u->uidvalidity = box.uidvalidity;
	u->uid = box.uidnext;
	return 0;
}
