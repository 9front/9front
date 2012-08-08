#include <u.h>
#include <libc.h>
#include <thread.h>
#include <fcall.h>
#include "dat.h"
#include "fns.h"

Chan *
chanattach(Fs *fs, int flags)
{	
	Chan *ch;

	ch = emalloc(sizeof(*ch));
	ch->fs = fs;
	ch->flags = flags;
	ch->loc = cloneloc(fs, (flags & CHFDUMP) != 0 ? fs->dumprootloc : fs->rootloc);
	return ch;
}

Chan *
chanclone(Chan *ch)
{
	Chan *d;

	chbegin(ch);
	d = emalloc(sizeof(*d));
	d->fs = ch->fs;
	d->flags = ch->flags;
	d->uid = ch->uid;
	d->loc = cloneloc(ch->fs, ch->loc);
	chend(ch);
	return d;
}

int
chanwalk(Chan *ch, char *name)
{
	Buf *b;
	Dentry *d;
	Loc *l;
	FLoc f;
	
	if(name == nil || name[0] == 0 || name[0] == '.' && name[1] == 0)
		return 1;
	chbegin(ch);
	b = getbuf(ch->fs->d, ch->loc->blk, TDENTRY, 0);
	if(b == nil){
		chend(ch);
		return -1;
	}
	d = &b->de[ch->loc->deind];
	if((d->type & QTDIR) == 0){
		werrstr(Enotadir);
		goto error;
	}
	if(!permcheck(ch->fs, d, ch->uid, OEXEC)){
		werrstr(Eperm);
		goto error;
	}
	if(strcmp(name, "..") == 0){
		l = ch->loc->next;
		if(l == nil)
			goto done;
		putloc(ch->fs, ch->loc, 0);
		ch->loc = l;
		goto done;
	}
	if(findentry(ch->fs, ch->loc, b, name, &f, ch->flags & CHFDUMP) <= 0)
		goto error;
	ch->loc = getloc(ch->fs, f, ch->loc);
done:
	putbuf(b);
	chend(ch);
	return 1;
error:
	putbuf(b);
	chend(ch);
	return -1;
}

int
namevalid(char *name)
{
	char *p;
	
	if(name == nil || name[0] == 0)
		return 0;
	if(name[0] == '.' && (name[1] == 0 || name[1] == '.' && name[2] == 0))
		return 0;
	for(p = name; *p; p++)
		if((uchar) *p < ' ' || *p == '/')
			return 0;
	return p - name < NAMELEN;
}


int
chancreat(Chan *ch, char *name, int perm, int mode)
{
	Buf *b, *c;
	Dentry *d;
	int isdir;
	FLoc f;
	
	if((ch->flags & CHFRO) != 0){
		werrstr(Einval);
		return -1;
	}
	chbegin(ch);
	if(willmodify(ch->fs, ch->loc, ch->flags & CHFNOLOCK) < 0){
		chend(ch);
		return -1;
	}
	if(!namevalid(name) || ch->open != 0){
		werrstr(Einval);
		chend(ch);
		return -1;
	}
	if(isdir = ((perm & DMDIR) != 0))
		if((mode & (OWRITE | OEXEC | ORCLOSE | OTRUNC)) != 0){
			werrstr(Einval);
			chend(ch);
			return -1;
		}
	b = getbuf(ch->fs->d, ch->loc->blk, TDENTRY, 0);
	if(b == nil){
		chend(ch);
		return -1;
	}
	d = &b->de[ch->loc->deind];
	if((d->type & QTDIR) == 0){
		werrstr(Enotadir);
		goto error;
	}
	if(!permcheck(ch->fs, d, ch->uid, OWRITE)){
		werrstr(Eperm);
		goto error;
	}
	if(newentry(ch->fs, ch->loc, b, name, &f) <= 0)
		goto error;
	c = getbuf(ch->fs->d, f.blk, TDENTRY, 0);
	if(c == nil)
		goto error;
	modified(ch, d);
	if(isdir)
		perm &= ~0777 | d->mode & 0777;
	else
		perm &= ~0666 | d->mode & 0666;
	d = &c->de[f.deind];
	memset(d, 0, sizeof(Dentry));
	if(newqid(ch->fs, &d->path) < 0)
		goto error;
	d->type = perm >> 24;
	strcpy(d->name, name);
	d->mtime = time(0);
	d->atime = d->mtime;
	d->gid = d->uid = d->muid = ch->uid;
	d->mode = DALLOC | perm & 0777;
	f.Qid = d->Qid;
	ch->loc = getloc(ch->fs, f, ch->loc);
	if((d->type & QTEXCL) != 0){
		qlock(&ch->loc->ex);
		ch->loc->exlock = ch;
		ch->loc->lwrite = d->atime;
		qunlock(&ch->loc->ex);
	}
	c->op |= BDELWRI;
	b->op |= BDELWRI;
	putbuf(c);
	putbuf(b);
	switch(mode & OEXEC){
	case ORDWR:
		ch->open |= CHREAD;
	case OWRITE:
		ch->open |= CHWRITE;
		break;
	case OEXEC:
	case OREAD:
		ch->open |= CHREAD;
		break;
	}
	chend(ch);
	return 1;

error:
	putbuf(b);
	chend(ch);
	return -1;
}

int
chanopen(Chan *ch, int mode)
{
	Buf *b;
	Dentry *d;
	int isdir;

	chbegin(ch);
	if(ch->open != 0){
		werrstr(Einval);
		chend(ch);
		return -1;
	}
	b = getbuf(ch->fs->d, ch->loc->blk, TDENTRY, 0);
	if(b == nil){
		chend(ch);
		return -1;
	}
	d = &b->de[ch->loc->deind];
	if(!permcheck(ch->fs, d, ch->uid, mode & OEXEC)){
	permerr:
		werrstr(Eperm);
	err:
		putbuf(b);
		chend(ch);
		return -1;
	}
	if((d->type & QTAPPEND) != 0)
		mode &= ~OTRUNC;
	isdir = (d->type & QTDIR) != 0;
	if(isdir && (mode & (ORCLOSE | OTRUNC | OWRITE | ORDWR)) != 0){
		werrstr(Einval);
		goto err;
	}
	if((mode & OTRUNC) != 0 && !permcheck(ch->fs, d, ch->uid, OWRITE))
		goto permerr;
	if((ch->flags & CHFRO) != 0 && (mode & (ORCLOSE | OTRUNC | OWRITE | ORDWR)) != 0){
		werrstr(Einval);
		goto err;
	}
	if((ch->loc->type & QTEXCL) != 0){
		qlock(&ch->loc->ex);
		if(ch->loc->exlock == nil || ch->loc->lwrite < time(0) - EXCLDUR){
			ch->loc->exlock = ch;
			ch->loc->lwrite = time(0);
			qunlock(&ch->loc->ex);
		}else{
			qunlock(&ch->loc->ex);
			werrstr(Elocked);
			goto err;
		}
	}
	switch(mode & OEXEC){
	case ORDWR:
		ch->open |= CHREAD;
	case OWRITE:
		ch->open |= CHWRITE;
		break;
	case OEXEC:
	case OREAD:
		ch->open |= CHREAD;
		break;
	}
	if((mode & OTRUNC) != 0){
		trunc(ch->fs, ch->loc, b, 0);
		modified(ch, d);
	}
	if((mode & ORCLOSE) != 0)
		ch->open |= CHRCLOSE;
	putbuf(b);
	chend(ch);
	return 1;
}

static int
checklock(Chan *ch)
{
	int rc;

	qlock(&ch->loc->ex);
	rc = 1;
	if(ch->loc->exlock == ch){
		if(ch->loc->lwrite < time(0) - EXCLDUR){
			ch->loc->exlock = nil;
			werrstr("lock broken");
			rc = -1;
		}else
			ch->loc->lwrite = time(0);
	}else{
		werrstr(Elocked);
		rc = -1;
	}
	qunlock(&ch->loc->ex);
	return rc;
}

int
chanwrite(Chan *ch, void *buf, ulong n, uvlong off)
{
	uvlong i, e, bl;
	int r, rn, rc;
	Buf *b, *c;
	Dentry *d;
	uchar *p;

	if(n == 0)
		return 0;
	if((ch->flags & CHFRO) != 0){
		werrstr(Einval);
		return -1;
	}
	if((ch->open & CHWRITE) == 0){
		werrstr(Einval);
		return -1;
	}
	chbegin(ch);
	if((ch->loc->type & QTEXCL) != 0 && checklock(ch) < 0){
		chend(ch);
		return -1;
	}
	if(willmodify(ch->fs, ch->loc, ch->flags & CHFNOLOCK) < 0){
		chend(ch);
		return -1;
	}
	b = getbuf(ch->fs->d, ch->loc->blk, TDENTRY, 0);
	if(b == nil){
		chend(ch);
		return -1;
	}
	d = &b->de[ch->loc->deind];
	if((d->type & QTAPPEND) != 0)
		off = d->size;
	e = off + n;
	i = off;
	p = buf;
	while(i < e){
		bl = i / RBLOCK;
		r = i % RBLOCK;
		rn = RBLOCK - r;
		if(i + rn > e)
			rn = e - i;
		rc = getblk(ch->fs, ch->loc, b, bl, &bl, rn == RBLOCK ? GBOVERWR : GBCREATE);
		if(rc < 0)
			goto done;
		c = getbuf(ch->fs->d, bl, TRAW, rc == 0 || rn == RBLOCK);
		if(c == nil)
			goto done;
		if(rc == 0 && rn != RBLOCK)
			memset(c->data, 0, sizeof(c->data));
		memcpy(c->data + r, p, rn);
		i += rn;
		c->op |= i != e ? BWRIM : BDELWRI;
		putbuf(c);
		p += rn;
	}
done:
	modified(ch, d);
	e = off + (p - (uchar *) buf);
	if(e > d->size)
		d->size = e;
	b->op |= BDELWRI;
	putbuf(b);
	chend(ch);
	if(p == buf)
		return -1;
	return p - (uchar *) buf;
}

static int chandirread(Chan *, void *, ulong, uvlong);

int
chanread(Chan *ch, void *buf, ulong n, uvlong off)
{
	uvlong i, e, bl;
	int r, rn, rc;
	uchar *p;
	Buf *b, *c;
	Dentry *d;

	chbegin(ch);
	if((ch->loc->type & QTEXCL) != 0 && checklock(ch) < 0){
		chend(ch);
		return -1;
	}
	if((ch->open & CHREAD) == 0){
		werrstr(Einval);
		chend(ch);
		return -1;
	}
	if((ch->loc->Qid.type & QTDIR) != 0)
		return chandirread(ch, buf, n, off);
	b = getbuf(ch->fs->d, ch->loc->blk, TDENTRY, 0);
	if(b == nil){
		chend(ch);
		return -1;
	}
	d = &b->de[ch->loc->deind];
	if(off >= d->size)
		n = 0;
	else if(off + n > d->size)
		n = d->size - off;
	if(n == 0){
		putbuf(b);
		chend(ch);
		return 0;
	}
	e = off + n;
	i = off;
	p = buf;
	while(i < e){
		bl = i / RBLOCK;
		r = i % RBLOCK;
		rn = RBLOCK - r;
		if(i + rn > e)
			rn = e - i;
		rc = getblk(ch->fs, ch->loc, b, bl, &bl, GBREAD);
		if(rc < 0)
			goto error;
		if(rc == 0)
			memset(p, 0, rn);
		else{
			c = getbuf(ch->fs->d, bl, TRAW, 0);
			if(c == nil)
				goto error;
			memcpy(p, c->data + r, rn);
			putbuf(c);
		}
		i += rn;
		p += rn;
	}
	putbuf(b);
	chend(ch);
	return n;
	
error:
	putbuf(b);
	chend(ch);
	return -1;
}

static void
statbuf(Fs *fs, Dentry *d, Dir *di)
{
	di->qid = d->Qid;
	di->mode = (d->mode & 0777) | (d->Qid.type << 24);
	di->mtime = d->mtime;
	di->atime = d->atime;
	di->length = d->size;
	if(d->type & QTDIR)
		di->length = 0;
	di->name = strdup(d->name);
	di->uid = uid2name(fs, d->uid);
	di->gid = uid2name(fs, d->gid);
	di->muid = uid2name(fs, d->muid);
}

int
chanstat(Chan *ch, Dir *di)
{
	Buf *b;
	
	chbegin(ch);
	b = getbuf(ch->fs->d, ch->loc->blk, TDENTRY, 0);
	if(b == nil){
		chend(ch);
		return -1;
	}
	statbuf(ch->fs, &b->de[ch->loc->deind], di);
	putbuf(b);
	chend(ch);
	return 0;
}

static int
chandirread(Chan *ch, void *buf, ulong n, uvlong off)
{
	Buf *b, *c;
	Dentry *d;
	uvlong i, blk;
	int j;
	int rc;
	ulong wr;
	Dir di;

	if(off == 0){
		ch->dwloff = 0;
		ch->dwblk = 0;
		ch->dwind = 0;
	}else if(ch->dwloff != off){
		werrstr(Einval);
		chend(ch);
		return -1;
	}
	b = getbuf(ch->fs->d, ch->loc->blk, TDENTRY, 0);
	if(b == nil){
		chend(ch);
		return -1;
	}
	d = &b->de[ch->loc->deind];
	if(ch->dwblk >= d->size){
		putbuf(b);
		chend(ch);
		return 0;
	}
	c = nil;
	wr = 0;
	i = ch->dwblk;
	j = ch->dwind;
	for(;;){
		if(c == nil){
			rc = getblk(ch->fs, ch->loc, b, i, &blk, GBREAD);
			if(rc < 0)
				goto error;
			if(rc == 0){
				j = 0;
				if(++i >= d->size)
					break;
				continue;
			}
			c = getbuf(ch->fs->d, blk, TDENTRY, 0);
			if(c == nil)
				goto error;
		}
		if((c->de[j].mode & DALLOC) == 0)
			goto next;
		if((ch->flags & CHFDUMP) != 0 && (c->de[j].type & QTTMP) != 0)
			goto next;
		statbuf(ch->fs, &c->de[j], &di);
		rc = convD2M(&di, (uchar *) buf + wr, n - wr);
		free(di.uid);
		free(di.gid);
		free(di.muid);
		free(di.name);
		if(rc <= BIT16SZ)
			break;
		wr += rc;
	next:
		if(++j >= DEPERBLK){
			j = 0;
			if(c != nil)
				putbuf(c);
			c = nil;
			if(++i >= d->size)
				break;
		}
	}
	ch->dwblk = i;
	ch->dwind = j;
	ch->dwloff += wr;
	if(c != nil)
		putbuf(c);
	putbuf(b);
	chend(ch);
	return wr;
error:
	putbuf(b);
	chend(ch);
	return -1;
}

int
chanclunk(Chan *ch)
{
	Buf *b, *p;
	int rc;
	Dentry *d;

	chbegin(ch);
	rc = 1;
	if(ch->open & CHRCLOSE){
		if((ch->flags & CHFRO) != 0)
			goto inval;
		if(willmodify(ch->fs, ch->loc, ch->flags & CHFNOLOCK) < 0)
			goto err;
		if(ch->loc->next == nil){
		inval:
			werrstr(Einval);
		err:
			rc = -1;
			goto done;
		}
		p = getbuf(ch->fs->d, ch->loc->next->blk, TDENTRY, 0);
		if(p == nil)
			goto err;
		b = getbuf(ch->fs->d, ch->loc->blk, TDENTRY, 0);
		if(b == nil){
			putbuf(p);
			goto err;
		}
		if(!permcheck(ch->fs, &p->de[ch->loc->next->deind], ch->uid, OWRITE)){
			werrstr(Eperm);
			putbuf(b);
			putbuf(p);
			goto err;
		}
		d = &b->de[ch->loc->deind];
		if((d->type & QTDIR) != 0 && findentry(ch->fs, ch->loc, b, nil, nil, ch->flags & CHFDUMP) != 0){
			putbuf(b);
			putbuf(p);
			goto inval;
		}
		if((d->mode & DGONE) != 0){
			putbuf(b);
			putbuf(p);
			goto done;
		}
		qlock(&ch->fs->loctree);
		if(ch->loc->ref > 1){
			d->mode &= ~DALLOC;
			d->mode |= DGONE; /* aaaaand it's gone */
			ch->loc->flags |= LGONE;
			qunlock(&ch->fs->loctree);
		}else{
			ch->loc->flags &= ~LGONE;
			qunlock(&ch->fs->loctree);
			rc = delete(ch->fs, ch->loc, b);
		}
		b->op |= BDELWRI;
		putbuf(b);
		putbuf(p);
	}
done:
	if((ch->loc->type & QTEXCL) != 0){
		qlock(&ch->loc->ex);
		if(ch->loc->exlock == ch)
			ch->loc->exlock = nil;
		qunlock(&ch->loc->ex);
	}
	putloc(ch->fs, ch->loc, 1);
	chend(ch);
	free(ch);
	return rc;
}

int
chanwstat(Chan *ch, Dir *di)
{
	Buf *b, *pb;
	Dentry *d;
	int isdir, owner, rc;
	short nuid, ngid;

	if((ch->flags & CHFRO) != 0){
		werrstr(Einval);
		return -1;
	}
	chbegin(ch);
	if(willmodify(ch->fs, ch->loc, ch->flags & CHFNOLOCK) < 0){
		chend(ch);
		return -1;
	}
	pb = nil;
	if(*di->name){
		if(!namevalid(di->name) || ch->loc->next == nil){
			werrstr(Einval);
			chend(ch);
			return -1;
		}
		pb = getbuf(ch->fs->d, ch->loc->next->blk, TDENTRY, 0);
		if(pb == nil){
			chend(ch);
			return -1;
		}
		if(!permcheck(ch->fs, &pb->de[ch->loc->next->deind], ch->uid, OWRITE)){
			werrstr(Eperm);
			putbuf(pb);
			chend(ch);
			return -1;
		}
		rc = findentry(ch->fs, ch->loc->next, pb, di->name, nil, ch->flags & CHFDUMP);
		if(rc > 0)
			werrstr(Eexists);
		if(rc != 0){
			putbuf(pb);
			chend(ch);
			return -1;
		}
	}
	b = getbuf(ch->fs->d, ch->loc->blk, TDENTRY, 0);
	if(b == nil){
		chend(ch);
		return -1;
	}
	d = &b->de[ch->loc->deind];
	isdir = (d->type & QTDIR) != 0;
	owner = ch->uid == d->uid || ingroup(ch->fs, ch->uid, d->gid, 1) || (ch->fs->flags & FSNOPERM) != 0;
	if((uvlong)~di->length){
		if(isdir && di->length != 0)
			goto inval;
		if(di->length != d->size && !permcheck(ch->fs, d, ch->uid, OWRITE))
			goto perm;
	}
	if((ulong)~di->atime)
		goto inval;
	if((ulong)~di->mtime && !owner)
		goto perm;
	if((ulong)~di->mode && !owner)
		goto perm;
	nuid = NOUID;
	ngid = NOUID;
	if(*di->uid != 0 && name2uid(ch->fs, di->uid, &nuid) < 0)
		goto inval;
	if(*di->gid != 0 && name2uid(ch->fs, di->gid, &ngid) < 0)
		goto inval;
	if(nuid != NOUID && (ch->fs->flags & FSCHOWN) == 0)
		goto inval;
	if((nuid != NOUID || ngid != NOUID) && !owner)
		goto perm;

	if((uvlong)~di->length && !isdir){
		trunc(ch->fs, ch->loc, b, di->length);
		modified(ch, d);
	}
	if((ulong)~di->mtime)
		d->mtime = di->mtime;
	if((ulong)~di->mode){
		d->mode = d->mode & ~0777 | di->mode & 0777;
		ch->loc->type = d->type = di->mode >> 24;
	}
	if(*di->name){
		memset(d->name, 0, NAMELEN);
		strcpy(d->name, di->name);
	}
	if(nuid != NOUID)
		d->uid = nuid;
	if(ngid != NOUID)
		d->gid = ngid;
	b->op |= BDELWRI;
	if(pb != nil)
		putbuf(pb);
	putbuf(b);
	chend(ch);
	return 1;

inval:
	werrstr(Einval);
	goto error;
perm:
	werrstr(Eperm);
error:
	if(pb != nil)
		putbuf(pb);
	putbuf(b);
	chend(ch);
	return -1;
}

int
chanremove(Chan *ch)
{
	ch->open |= CHRCLOSE;
	return chanclunk(ch);
}
