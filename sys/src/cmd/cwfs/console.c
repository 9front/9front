#include	"all.h"

/* 9p2 */
int	version(Chan*, Fcall*, Fcall*);
int	attach(Chan*, Fcall*, Fcall*);
int	walk(Chan*, Fcall*, Fcall*);
int	fs_open(Chan*, Fcall*, Fcall*);
int	fs_create(Chan*, Fcall*, Fcall*);
int	fs_read(Chan*, Fcall*, Fcall*, uchar*);
int	fs_write(Chan*, Fcall*, Fcall*);
int	fs_remove(Chan*, Fcall*, Fcall*);

int
con_session(void)
{
	Fcall in, ou;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&ou, 0, sizeof(ou));

	in.type = Tversion;
	in.version = VERSION9P;
	in.msize = MAXDAT + IOHDRSZ;

	rlock(&mainlock);
	err = version(cons.chan, &in, &ou);
	runlock(&mainlock);

	return err;
}

int
con_attach(int fid, char *uid, char *arg)
{
	Fcall in, ou;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&ou, 0, sizeof(ou));

	in.type = Tattach;
	in.fid = fid;
	in.uname = uid;
	in.aname = arg;

	rlock(&mainlock);
	err = attach(cons.chan, &in, &ou);
	runlock(&mainlock);

	return err;
}

int
con_clone(int fid1, int fid2)
{
	Fcall in, ou;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&ou, 0, sizeof(ou));

	in.type = Twalk;
	in.fid = fid1;
	in.newfid = fid2;
	in.nwname = 0;

	rlock(&mainlock);
	err = walk(cons.chan, &in, &ou);
	runlock(&mainlock);

	return err;
}

int
con_walk(int fid, char *name)
{
	Fcall in, ou;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&ou, 0, sizeof(ou));

	in.type = Twalk;
	in.fid = fid;
	in.newfid = fid;
	in.wname[0] = name;
	in.nwname = 1;

	rlock(&mainlock);
	err = walk(cons.chan, &in, &ou);
	runlock(&mainlock);

	return err;
}

int
con_open(int fid, int mode)
{
	Fcall in, ou;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&ou, 0, sizeof(ou));

	in.type = Topen;
	in.fid = fid;
	in.mode = mode;

	rlock(&mainlock);
	err = fs_open(cons.chan, &in, &ou);
	runlock(&mainlock);

	return err;
}

int
con_create(int fid, char *name, int uid, int gid, long perm, int mode)
{
	Fcall in, ou;
	int err;

	cons.uid = uid;			/* beyond ugly */
	cons.gid = gid;

	memset(&in, 0, sizeof(in));
	memset(&ou, 0, sizeof(ou));

	in.type = Tcreate;
	in.fid = fid;
	in.mode = mode;
	in.perm = perm;
	in.name = name;

	rlock(&mainlock);
	err = fs_create(cons.chan, &in, &ou);
	runlock(&mainlock);

	return err;
}

int
con_read(int fid, char *data, Off offset, int count)
{
	Fcall in, ou;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&ou, 0, sizeof(ou));

	in.type = Tread;
	in.fid = fid;
	in.offset = offset;
	in.count = count;

	rlock(&mainlock);
	err = fs_read(cons.chan, &in, &ou, (uchar*)data);
	runlock(&mainlock);

	return err ? 0 : ou.count;
}

int
con_write(int fid, char *data, Off offset, int count)
{
	Fcall in, ou;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&ou, 0, sizeof(ou));

	in.type = Twrite;
	in.fid = fid;
	in.offset = offset;
	in.count = count;
	in.data = data;

	rlock(&mainlock);
	err = fs_write(cons.chan, &in, &ou);
	runlock(&mainlock);

	return err ? 0 : ou.count;
}

int
con_remove(int fid)
{
	Fcall in, ou;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&ou, 0, sizeof(ou));

	in.type = Tremove;
	in.fid = fid;

	rlock(&mainlock);
	err = fs_remove(cons.chan, &in, &ou);
	runlock(&mainlock);

	return err;
}


int
doclri(File *f)
{
	Iobuf *p, *p1;
	Dentry *d, *d1;
	int err;

	err = 0;
	p = 0;
	p1 = 0;
	if(f->fs->dev->type == Devro) {
		err = Eronly;
		goto out;
	}
	/*
	 * check on parent directory of file to be deleted
	 */
	if(f->wpath == 0 || f->wpath->addr == f->addr) {
		err = Ephase;
		goto out;
	}
	p1 = getbuf(f->fs->dev, f->wpath->addr, Brd);
	d1 = getdir(p1, f->wpath->slot);
	if(!d1 || checktag(p1, Tdir, QPNONE) || !(d1->mode & DALLOC)) {
		err = Ephase;
		goto out;
	}

	accessdir(p1, d1, FWRITE, 0);
	putbuf(p1);
	p1 = 0;

	/*
	 * check on file to be deleted
	 */
	p = getbuf(f->fs->dev, f->addr, Brd);
	d = getdir(p, f->slot);

	/*
	 * do it
	 */
	memset(d, 0, sizeof(Dentry));
	settag(p, Tdir, QPNONE);
	freewp(f->wpath);
	freefp(f);

out:
	if(p1)
		putbuf(p1);
	if(p)
		putbuf(p);
	return err;
}

static int
f_fstat(Chan *cp, Fcall *in, Fcall *ou)
{
	File *f;
	Iobuf *p;
	Dentry *d;
	int i, err;

	err = 0;
	if(CHAT(cp)) {
		fprint(2, "c_fstat %d\n", cp->chan);
		fprint(2, "\tfid = %d\n", in->fid);
	}

	p = 0;
	f = filep(cp, in->fid, 0);
	if(!f) {
		err = Efid;
		goto out;
	}
	p = getbuf(f->fs->dev, f->addr, Brd);
	d = getdir(p, f->slot);
	if(d == 0)
		goto out;

	print("name = %.*s\n", NAMELEN, d->name);
	print("uid = %d; gid = %d; muid = %d\n", d->uid, d->gid, d->muid);
	print("size = %lld; qid = %llux/%lux\n", (Wideoff)d->size,
		(Wideoff)d->qid.path, d->qid.version);
	print("atime = %ld; mtime = %ld\n", d->atime, d->mtime);
	print("dblock =");
	for(i=0; i<NDBLOCK; i++)
		print(" %lld", (Wideoff)d->dblock[i]);
	for (i = 0; i < NIBLOCK; i++)
		print("; iblocks[%d] = %lld", i, (Wideoff)d->iblocks[i]);
	print("\n\n");

out:
	if(p)
		putbuf(p);
	ou->fid = in->fid;
	if(f)
		qunlock(f);
	return err;
}

int
con_fstat(int fid)
{
	Fcall in, ou;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&ou, 0, sizeof(ou));

	in.type = Tstat;
	in.fid = fid;

	rlock(&mainlock);
	err = f_fstat(cons.chan, &in, &ou);
	runlock(&mainlock);

	return err;
}

static int
f_clri(Chan *cp, Fcall *in, Fcall *ou)
{
	File *f;
	int err;

	if(CHAT(cp)) {
		fprint(2, "c_clri %d\n", cp->chan);
		fprint(2, "\tfid = %d\n", in->fid);
	}

	f = filep(cp, in->fid, 0);
	if(!f) {
		err = Efid;
		goto out;
	}
	err = doclri(f);

out:
	ou->fid = in->fid;
	if(f)
		qunlock(f);
	return err;
}

int
con_clri(int fid)
{
	Fcall in, ou;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&ou, 0, sizeof(ou));

	in.type = Tremove;
	in.fid = fid;

	rlock(&mainlock);
	err = f_clri(cons.chan, &in, &ou);
	runlock(&mainlock);

	return err;
}
