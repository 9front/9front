#include "imap4d.h"

static	int	fsctl		= -1;
static	char	Ecanttalk[]	= "can't talk to mail server";

static void
fsinit(void)
{
	if(fsctl != -1)
		return;
	fsctl = open("/mail/fs/ctl", ORDWR);
	if(fsctl == -1)
		bye(Ecanttalk);
}

static void
boxflags(Box *box)
{
	Msg *m;

	box->recent = 0;
	for(m = box->msgs; m != nil; m = m->next){
		if(m->uid == 0){
	//		fprint(2, "unassigned uid %s\n", m->info[Idigest]);
			box->dirtyimp = 1;
			m->uid = box->uidnext++;
		}
		if(m->flags & Frecent)
			box->recent++;
	}
}

/*
 * try to match permissions with mbox
 */
static int
createimp(Box *box, Qid *qid)
{
	int fd;
	long mode;
	Dir *d;

	fd = cdcreate(mboxdir, box->imp, OREAD, 0664);
	if(fd < 0)
		return -1;
	d = cddirstat(mboxdir, box->name);
	if(d != nil){
		mode = d->mode & 0777;
		nulldir(d);
		d->mode = mode;
		dirfwstat(fd, d);
		free(d);
	}
	if(fqid(fd, qid) < 0){
		close(fd);
		return -1;
	}

	return fd;
}

/*
 * read in the .imp file, or make one if it doesn't exist.
 * make sure all flags and uids are consistent.
 * return the mailbox lock.
 */
static Mblock*
openimp(Box *box, int new)
{
	char buf[ERRMAX];
	int fd;
	Biobuf b;
	Mblock *ml;
	Qid qid;

	ml = mblock();
	if(ml == nil)
		return nil;
	fd = cdopen(mboxdir, box->imp, OREAD);
	if(fd < 0 || fqid(fd, &qid) < 0){
		if(fd < 0){
			errstr(buf, sizeof buf);
			if(cistrstr(buf, "does not exist") == nil)
				ilog("imp: %s: %s", box->imp, buf);
			else
				debuglog("imp: %s: %s .. creating", box->imp, buf);
		}else{
			close(fd);
			ilog("%s: bogus imp: bad qid: recreating", box->imp);
		}
		fd = createimp(box, &qid);
		if(fd < 0){
			ilog("createimp fails: %r");
			mbunlock(ml);
			return nil;
		}
		box->dirtyimp = 1;
		if(box->uidvalidity == 0){
			ilog("set uidvalidity %lud [new]\n", box->uidvalidity);
			box->uidvalidity = box->mtime;
		}
		box->impqid = qid;
		new = 1;
	}else if(qid.path != box->impqid.path || qid.vers != box->impqid.vers){
		Binit(&b, fd, OREAD);
		if(parseimp(&b, box) == -1){
			ilog("%s: bogus imp: parse failure", box->imp);
			box->dirtyimp = 1;
			if(box->uidvalidity == 0){
				ilog("set uidvalidity %lud [parseerr]\n", box->uidvalidity);
				box->uidvalidity = box->mtime;
			}
		}
		Bterm(&b);
		box->impqid = qid;
		new = 1;
	}
	if(new)
		boxflags(box);
	close(fd);
	return ml;
}

/*
 * mailbox is unreachable, so mark all messages expunged
 * clean up .imp files as well.
 */
static void
mboxgone(Box *box)
{
	char buf[ERRMAX];
	Msg *m;

	rerrstr(buf, ERRMAX);
	if(strstr(buf, "hungup channel"))
		bye(Ecanttalk);
//	too smart.
//	if(cdexists(mboxdir, box->name) < 0)
//		cdremove(mboxdir, box->imp);
	for(m = box->msgs; m != nil; m = m->next)
		m->expunged = 1;
	ilog("mboxgone");
	box->writable = 0;
}

/*
 * read messages in the mailbox
 * mark message that no longer exist as expunged
 * returns -1 for failure, 0 if no new messages, 1 if new messages.
 */
enum {
	Gone	= 2,		/* don't unexpunge messages */
};

static int
readbox(Box *box)
{
	char buf[ERRMAX];
	int i, n, fd, new, id;
	Dir *d;
	Msg *m, *last;

	fd = cdopen(box->fsdir, ".", OREAD);
	if(fd == -1){
goinggoinggone:
		rerrstr(buf, ERRMAX);
		ilog("upas/fs stat of %s/%s aka %s failed: %r",
			username, box->name, box->fsdir);
		mboxgone(box);
		return -1;
	}

	if((d = dirfstat(fd)) == nil){
		close(fd);
		goto goinggoinggone;
	}
	box->mtime = d->mtime;
	box->qid = d->qid;
	last = nil;
	for(m = box->msgs; m != nil; m = m->next){
		last = m;
		m->expunged |= Gone;
	}
	new = 0;
	free(d);

	for(;;){
		n = dirread(fd, &d);
		if(n <= 0){
			close(fd);
			if(n == -1)
				goto goinggoinggone;
			break;
		}
		for(i = 0; i < n; i++){
			if((d[i].qid.type & QTDIR) == 0)
				continue;
			id = atoi(d[i].name);
			if(m = fstreefind(box, id)){
				m->expunged &= ~Gone;
				continue;
			}
			new = 1;
			m = MKZ(Msg);
			m->id = id;
			m->fsdir = box->fsdir;
			m->fs = emalloc(2 * (Filelen + 1));
			m->efs = seprint(m->fs, m->fs + (Filelen + 1), "%ud/", id);
			m->size = ~0UL;
			m->lines = ~0UL;
			m->flags = Frecent;
			if(!msginfo(m) || m->info[Idigest] == 0)
				freemsg(0, m);
			else{
				fstreeadd(box, m);
				if(last == nil)
					box->msgs = m;
				else
					last->next = m;
				last = m;
			}
		}
		free(d);
	}

	/* box->max is invalid here */
	return new;
}

int
uidcmp(void *va, void *vb)
{
	Msg **a, **b;

	a = va;
	b = vb;
	return (*a)->uid - (*b)->uid;
}

static void
sequence(Box *box)
{
	Msg **a, *m;
	int n, i;

	n = 0;
	for(m = box->msgs; m; m = m->next)
		n++;
	a = ezmalloc(n * sizeof *a);
	i = 0;
	for(m = box->msgs; m; m = m->next)
		a[i++] = m;
	qsort(a, n, sizeof *a, uidcmp);
	for(i = 0; i < n - 1; i++)
		a[i]->next = a[i + 1];
	for(i = 0; i < n; i++)
		if(a[i]->seq && a[i]->seq != i + 1)
			bye("internal error assigning message numbers");
		else
			a[i]->seq = i + 1;
	box->msgs = nil;
	if(n > 0){
		a[n - 1]->next = nil;
		box->msgs = a[0];
	}
	box->max = n;
	memset(a, 0, n*sizeof *a);
	free(a);
}

/*
 * strategy:
 * every mailbox file has an associated .imp file
 * which maps upas/fs message digests to uids & message flags.
 *
 * the .imp files are locked by /mail/fs/usename/L.mbox.
 * whenever the flags can be modified, the lock file
 * should be opened, thereby locking the uid & flag state.
 * for example, whenever new uids are assigned to messages,
 * and whenever flags are changed internally, the lock file
 * should be open and locked.  this means the file must be
 * opened during store command, and when changing the \seen
 * flag for the fetch command.
 *
 * if no .imp file exists, a null one must be created before
 * assigning uids.
 *
 * the .imp file has the following format
 * imp		: "imap internal mailbox description\n"
 * 			uidvalidity " " uidnext "\n"
 *			messagelines
 *
 * messagelines	:
 *		| messagelines digest " " uid " " flags "\n"
 *
 * uid, uidnext, and uidvalidity are 32 bit decimal numbers
 * printed right justified in a field Nuid characters long.
 * the 0 uid implies that no uid has been assigned to the message,
 * but the flags are valid. note that message lines are in mailbox
 * order, except possibly for 0 uid messages.
 *
 * digest is an ascii hex string Ndigest characters long.
 *
 * flags has a character for each of NFlag flag fields.
 * if the flag is clear, it is represented by a "-".
 * set flags are represented as a unique single ascii character.
 * the currently assigned flags are, in order:
 *	Fseen		s
 *	Fanswered	a
 *	Fflagged	f
 *	Fdeleted	D
 *	Fdraft		d
 */

Box*
openbox(char *name, char *fsname, int writable)
{
	char err[ERRMAX];
	int new;
	Box *box;
	Mblock *ml;

	fsinit();
if(!strcmp(name, "mbox"))ilog("open %F %q", name, fsname);
	if(fprint(fsctl, "open %F %q", name, fsname) < 0){
		rerrstr(err, sizeof err);
		if(strstr(err, "file does not exist") == nil)
			ilog("fs open %F as %s: %s", name, fsname, err);
		if(strstr(err, "hungup channel"))
			bye(Ecanttalk);
		fprint(fsctl, "close %s", fsname);
		return nil;
	}

	/*
	 * read box to find all messages
	 * each one has a directory, and is in numerical order
	 */
	box = MKZ(Box);
	box->writable = writable;
	box->name = smprint("%s", name);
	box->imp = smprint("%s.imp", name);
	box->fs = smprint("%s", fsname);
	box->fsdir = smprint("/mail/fs/%s", fsname);
	box->uidnext = 1;
	box->fstree = avlcreate(fstreecmp);
	new = readbox(box);
	if(new >= 0 && (ml = openimp(box, new))){
		closeimp(box, ml);
		sequence(box);
		return box;
	}
	closebox(box, 0);
	return nil;
}

/*
 * careful: called by idle polling proc
 */
Mblock*
checkbox(Box *box, int imped)
{
	int new;
	Dir *d;
	Mblock *ml;

	if(box == nil)
		return nil;

	/*
	 * if stat fails, mailbox must be gone
	 */
	d = cddirstat(box->fsdir, ".");
	if(d == nil){
		mboxgone(box);
		return nil;
	}
	new = 0;
	if(box->qid.path != d->qid.path || box->qid.vers != d->qid.vers
	|| box->mtime != d->mtime){
		new = readbox(box);
		if(new < 0){
			free(d);
			return nil;
		}
	}
	free(d);
	ml = openimp(box, new);
	if(ml == nil){
		ilog("openimp fails; box->writable = 0: %r");
		box->writable = 0;
	}else if(!imped){
		closeimp(box, ml);
		ml = nil;
	}
	if(new || box->dirtyimp)
		sequence(box);
	return ml;
}

/*
 * close the .imp file, after writing out any changes
 */
void
closeimp(Box *box, Mblock *ml)
{
	int fd;
	Biobuf b;
	Qid qid;

	if(ml == nil)
		return;
	if(!box->dirtyimp){
		mbunlock(ml);
		return;
	}
	fd = cdcreate(mboxdir, box->imp, OWRITE, 0664);
	if(fd < 0){
		mbunlock(ml);
		return;
	}
	Binit(&b, fd, OWRITE);
	box->dirtyimp = 0;
	wrimp(&b, box);
	Bterm(&b);

	if(fqid(fd, &qid) == 0)
		box->impqid = qid;
	close(fd);
	mbunlock(ml);
}

void
closebox(Box *box, int opened)
{
	Msg *m, *next;

	/*
	 * make sure to leave the mailbox directory so upas/fs can close the mailbox
	 */
	mychdir(mboxdir);

	if(box->writable){
		deletemsg(box, 0);
		if(expungemsgs(box, 0))
			closeimp(box, checkbox(box, 1));
	}

	if(fprint(fsctl, "close %s", box->fs) < 0 && opened)
		bye(Ecanttalk);
	for(m = box->msgs; m != nil; m = next){
		next = m->next;
		freemsg(box, m);
	}
	free(box->name);
	free(box->fs);
	free(box->fsdir);
	free(box->imp);
	free(box->fstree);
	free(box);
}

int
deletemsg(Box *box, Msgset *ms)
{
	char buf[Bufsize], *p, *start;
	int ok;
	Msg *m;

	if(!box->writable)
		return 0;

	/*
	 * first pass: delete messages; gang the writes together for speed.
	 */
	ok = 1;
	start = seprint(buf, buf + sizeof buf, "delete %s", box->fs);
	p = start;
	for(m = box->msgs; m != nil; m = m->next)
		if(ms == 0 || ms && inmsgset(ms, m->uid))
		if((m->flags & Fdeleted) && !m->expunged){
			m->expunged = 1;
			p = seprint(p, buf + sizeof buf, " %ud", m->id);
			if(p + 32 >= buf + sizeof buf){
				if(write(fsctl, buf, p - buf) == -1)
					bye(Ecanttalk);
				p = start;
			}
		}
	if(p != start && write(fsctl, buf, p - buf) == -1)
		bye(Ecanttalk);
	return ok;
}

/*
 * second pass: remove the message structure,
 * and renumber message sequence numbers.
 * update messages counts in mailbox.
 * returns true if anything changed.
 */
int
expungemsgs(Box *box, int send)
{
	uint n;
	Msg *m, *next, *last;

	n = 0;
	last = nil;
	for(m = box->msgs; m != nil; m = next){
		m->seq -= n;
		next = m->next;
		if(m->expunged){
			if(send)
				Bprint(&bout, "* %ud expunge\r\n", m->seq);
			if(m->flags & Frecent)
				box->recent--;
			n++;
			if(last == nil)
				box->msgs = next;
			else
				last->next = next;
			freemsg(box, m);
		}else
			last = m;
	}
	if(n){
		box->max -= n;
		box->dirtyimp = 1;
	}
	return n;
}

static char *stoplist[] =
{
	".",
	"dead.letter",
	"forward",
	"headers",
	"imap.subscribed",
	"mbox",
	"names",
	"pipefrom",
	"pipeto",
	0
};

/*
 * reject bad mailboxes based on mailbox name
 */
int
okmbox(char *path)
{
	char *name;
	int i, c;

	name = strrchr(path, '/');
	if(name == nil)
		name = path;
	else
		name++;
	if(strlen(name) + STRLEN(".imp") >= Pathlen)
		return 0;
	for(i = 0; stoplist[i]; i++)
		if(strcmp(name, stoplist[i]) == 0)
			return 0;
	c = name[0];
	if(c == 0 || c == '-' || c == '/'
	|| isdotdot(name)
	|| isprefix("L.", name)
	|| isprefix("imap-tmp.", name)
	|| issuffix("-", name)
	|| issuffix(".00", name)
	|| issuffix(".imp", name)
	|| issuffix(".idx", name))
		return 0;

	return 1;
}

int
creatembox(char *mbox)
{
	fsinit();
	if(fprint(fsctl, "create %q", mbox) > 0){
		fprint(fsctl, "close %s", mbox);
		return 0;
	}
	return -1;
}

/*
 * rename mailbox.  truncaes or removes the source.
 * bug? is the lock required
 * upas/fs helpfully moves our .imp file.
 */
int
renamebox(char *from, char *to, int doremove)
{
	char *p;
	int r;
	Mblock *ml;

	fsinit();
	ml = mblock();
	if(ml == nil)
		return 0;
	if(doremove)
		r = fprint(fsctl, "rename %F %F", from, to);
	else
		r = fprint(fsctl, "rename -t %F %F", from, to);
	if(r > 0){
		if(p = strrchr(to, '/'))
			p++;
		else
			p = to;
		fprint(fsctl, "close %s", p);
	}
	mbunlock(ml);
	return r > 0;
}

/*
 * upas/fs likes us; he removes the .imp file
 */
int
removembox(char *path)
{
	fsinit();
	return fprint(fsctl, "remove %s", path) > 0;
}
