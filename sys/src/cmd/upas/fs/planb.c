/*
 * Plan B (mail2fs) mail box format.
 *
 * BUG: this does not reconstruct the
 * raw text for attachments.  So imap and others
 * will be unable to access any attachment using upas/fs.
 * As an aid, we add the path to the message directory
 * to the message body, so the user could build the path
 * for any attachment and open it.
 */

#include "common.h"
#include <ctype.h>
#include <plumb.h>
#include <libsec.h>
#include "dat.h"

static char*
parseunix(Message *m)
{
	char *s, *p, *q;
	int l;
	Tm tm;

	l = m->header - m->start;
	m->unixheader = smprint("%.*s", l, m->start);
	s = m->start + 5;
	if((p = strchr(s, ' ')) == nil)
		return s;
	*p = 0;
	m->unixfrom = strdup(s);
	*p++ = ' ';
	if(q = strchr(p, '\n'))
		*q = 0;
	if(strtotm(p, &tm) < 0)
		return p;
	if(q)
		*q = '\n';
	m->fileid = (uvlong)tm2sec(&tm) << 8;
	return 0;
}

static int
readmessage(Message *m, char *msg)
{
	int fd, n;
	char *buf, *name, *p;
	char hdr[128];
	Dir *d;

	buf = nil;
	d = nil;
	name = smprint("%s/raw", msg);
	if(name == nil)
		return -1;
	if(m->filename != nil)
		free(m->filename);
	m->filename = strdup(name);
	if(m->filename == nil)
		sysfatal("malloc: %r");
	fd = open(name, OREAD);
	if(fd < 0)
		goto Fail;
	n = read(fd, hdr, sizeof(hdr)-1);
	if(n <= 0)
		goto Fail;
	hdr[n] = 0;
	close(fd);
	fd = -1;
	p = strchr(hdr, '\n');
	if(p != nil)
		*++p = 0;
	if(strncmp(hdr, "From ", 5) != 0)
		goto Fail;
	free(name);
	name = smprint("%s/text", msg);
	if(name == nil)
		goto Fail;
	fd = open(name, OREAD);
	if(fd < 0)
		goto Fail;
	d = dirfstat(fd);
	if(d == nil)
		goto Fail;
	buf = malloc(strlen(hdr) + d->length + strlen(msg) + 10); /* few extra chars */
	if(buf == nil)
		goto Fail;
	strcpy(buf, hdr);
	p = buf+strlen(hdr);
	n = readn(fd, p, d->length);
	if(n < 0)
		goto Fail;
	sprint(p+n, "\n[%s]\n", msg);
	n += 2 + strlen(msg) + 2;
	close(fd);
	free(name);
	free(d);
	free(m->start);
	m->start = buf;
	m->lim = m->end = p+n;
	if(*(m->end-1) == '\n')
		m->end--;
	*m->end = 0;
	m->bend = m->rbend = m->end;

	return 0;
Fail:
	if(fd >= 0)
		close(fd);
	free(name);
	free(buf);
	free(d);
	return -1;
}

/*
 * Deleted messages are kept as spam instead.
 */
static void
archive(Message *m)
{
	char *dir, *p, *nname;
	Dir d;

	dir = strdup(m->filename);
	nname = nil;
	if(dir == nil)
		return;
	p = strrchr(dir, '/');
	if(p == nil)
		goto Fail;
	*p = 0;
	p = strrchr(dir, '/');
	if(p == nil)
		goto Fail;
	p++;
	if(*p < '0' || *p > '9')
		goto Fail;
	nname = smprint("s.%s", p);
	if(nname == nil)
		goto Fail;
	nulldir(&d);
	d.name = nname;
	dirwstat(dir, &d);
Fail:
	free(dir);
	free(nname);
}

int
purgembox(Mailbox *mb, int virtual)
{
	Message *m, *next;
	int newdels;

	/* forget about what's no longer in the mailbox */
	newdels = 0;
	for(m = mb->root->part; m != nil; m = next){
		next = m->next;
		if(m->deleted > 0 && m->refs == 0){
			if(m->inmbox){
				newdels++;
				/*
				 * virtual folders are virtual,
				 * we do not archive
				 */
				if(virtual == 0)
					archive(m);
			}
			delmessage(mb, m);
		}
	}
	return newdels;
}

static int
mustshow(char* name)
{
	if(isdigit(name[0]))
		return 1;
	if(0 && name[0] == 'a' && name[1] == '.')
		return 1;
	if(0 && name[0] == 's' && name[1] == '.')
		return 1;
	return 0;
}

static int
readpbmessage(Mailbox *mb, char *msg, int doplumb, int *nnew)
{
	Message *m, **l;
	char *x, *p;

	m = newmessage(mb->root);
	m->mallocd = 1;
	m->inmbox = 1;
	if(readmessage(m, msg) < 0){
		unnewmessage(mb, mb->root, m);
		return -1;
	}
	for(l = &mb->root->part; *l != nil; l = &(*l)->next)
		if(strcmp((*l)->filename, m->filename) == 0 &&
		    *l != m){
			if((*l)->deleted < 0)
				(*l)->deleted = 0;
			delmessage(mb, m);
			mb->root->subname--;
			return -1;
		}
	m->header = m->end;
	if(x = strchr(m->start, '\n'))
		m->header = x + 1;
	if(p = parseunix(m))
		sysfatal("%s:%s naked From in body? [%s]", mb->path, (*l)->filename, p);
	m->mheader = m->mhend = m->header;
	parse(mb, m, 0, 0);
	if(m != *l && m->deleted != Dup){
		logmsg(m, "new");
		newcachehash(mb, m, doplumb);
		putcache(mb, m);
		nnew[0]++;
	}

	/* chain in */
	*l = m;
	if(doplumb)
		mailplumb(mb, m, 0);
	return 0;
}

static int
dcmp(Dir *a, Dir *b)
{
	char *an, *bn;

	an = a->name;
	bn = b->name;
	if(an[0] != 0 && an[1] == '.')
		an += 2;
	if(bn[0] != 0 && bn[1] == '.')
		bn += 2;
	return strcmp(an, bn);
}

static char*
readpbmbox(Mailbox *mb, int doplumb, int *new)
{
	char *month, *msg;
	int fd, i, j, nd, nmd;
	Dir *d, *md;
	static char err[ERRMAX];

	fd = open(mb->path, OREAD);
	if(fd < 0){
		errstr(err, sizeof err);
		return err;
	}
	nd = dirreadall(fd, &d);
	close(fd);
	if(nd > 0)
		qsort(d, nd, sizeof d[0], (int (*)(void*, void*))dcmp);
	for(i = 0; i < nd; i++){
		month = smprint("%s/%s", mb->path, d[i].name);
		if(month == nil)
			break;
		fd = open(month, OREAD);
		if(fd < 0){
			fprint(2, "%s: %s: %r\n", argv0, month);
			free(month);
			continue;
		}
		md = dirfstat(fd);
		if(md != nil && (md->qid.type & QTDIR) != 0){
			free(md);
			md = nil;
			nmd = dirreadall(fd, &md);
			for(j = 0; j < nmd; j++)
				if(mustshow(md[j].name)){
					msg = smprint("%s/%s", month, md[j].name);
					readpbmessage(mb, msg, doplumb, new);
					free(msg);
				}
		}
		close(fd);
		free(month);
		free(md);
		md = nil;
	}
	free(d);
	return nil;
}

static char*
readpbvmbox(Mailbox *mb, int doplumb, int *new)
{
	char *data, *ln, *p, *nln, *msg;
	int fd, nr;
	long sz;
	Dir *d;
	static char err[ERRMAX];

	fd = open(mb->path, OREAD);
	if(fd < 0){
		errstr(err, sizeof err);
		return err;
	}
	d = dirfstat(fd);
	if(d == nil){
		errstr(err, sizeof err);
		return err;
	}
	sz = d->length;
	free(d);
	if(sz > 2 * 1024 * 1024){
		sz = 2 * 1024 * 1024;
		fprint(2, "upas/fs: %s: bug: folder too big\n", mb->path);
	}
	data = malloc(sz+1);
	if(data == nil){
		errstr(err, sizeof err);
		return err;
	}
	nr = readn(fd, data, sz);
	close(fd);
	if(nr < 0){
		errstr(err, sizeof err);
		free(data);
		return err;
	}
	data[nr] = 0;

	for(ln = data; *ln != 0; ln = nln){
		nln = strchr(ln, '\n');
		if(nln != nil)
			*nln++ = 0;
		else
			nln = ln + strlen(ln);
		p = strchr(ln , ' ');
		if(p != nil)
			*p = 0;
		p = strchr(ln, '\t');
		if(p != nil)
			*p = 0;
		p = strstr(ln, "/text");
		if(p != nil)
			*p = 0;
		msg = smprint("/mail/box/%s/msgs/%s", user, ln);
		if(msg == nil){
			fprint(2, "upas/fs: malloc: %r\n");
			continue;
		}
		readpbmessage(mb, msg, doplumb, new);
		free(msg);
	}
	free(data);
	return nil;
}

static char*
readmbox(Mailbox *mb, int doplumb, int virt, int *new)
{
	char *mberr;
	int fd;
	Dir *d;
	Message *m;
	static char err[128];

	if(debug)
		fprint(2, "read mbox %s\n", mb->path);
	fd = open(mb->path, OREAD);
	if(fd < 0){
		errstr(err, sizeof(err));
		return err;
	}

	d = dirfstat(fd);
	if(d == nil){
		close(fd);
		errstr(err, sizeof(err));
		return err;
	}
	if(mb->d != nil){
		if(d->qid.path == mb->d->qid.path &&
		   d->qid.vers == mb->d->qid.vers){
			close(fd);
			free(d);
			return nil;
		}
		free(mb->d);
	}
	close(fd);
	mb->d = d;
	mb->vers++;
	henter(PATH(0, Qtop), mb->name,
		(Qid){PATH(mb->id, Qmbox), mb->vers, QTDIR}, nil, mb);
	snprint(err, sizeof err, "reading '%s'", mb->path);
	logmsg(nil, err, nil);

	for(m = mb->root->part; m != nil; m = m->next)
		if(m->deleted == 0)
			m->deleted = -1;
	if(virt == 0)
		mberr = readpbmbox(mb, doplumb, new);
	else
		mberr = readpbvmbox(mb, doplumb, new);

	/*
	 * messages removed from the mbox; flag them to go.
	 */
	for(m = mb->root->part; m != nil; m = m->next)
		if(m->deleted < 0 && doplumb){
			delmessage(mb, m);
			if(doplumb)
				mailplumb(mb, m, 1);
		}
	logmsg(nil, "mbox read");
	return mberr;
}

static char*
mbsync(Mailbox *mb, int doplumb, int *new)
{
	char *rv;

	rv = readmbox(mb, doplumb, 0, new);
	purgembox(mb, 0);
	return rv;
}

static char*
mbvsync(Mailbox *mb, int doplumb, int *new)
{
	char *rv;

	rv = readmbox(mb, doplumb, 1, new);
	purgembox(mb, 1);
	return rv;
}

char*
planbmbox(Mailbox *mb, char *path)
{
	char *list;
	static char err[64];

	if(access(path, AEXIST) < 0)
		return Enotme;
	list = smprint("%s/list", path);
	if(access(list, AEXIST) < 0){
		free(list);
		return Enotme;
	}
	free(list);
	mb->sync = mbsync;
	if(debug)
		fprint(2, "planb mbox %s\n", path);
	return nil;
}

char*
planbvmbox(Mailbox *mb, char *path)
{
	int fd, nr, i;
	char buf[64];
	static char err[64];

	fd = open(path, OREAD);
	if(fd < 0)
		return Enotme;
	nr = read(fd, buf, sizeof(buf)-1);
	close(fd);
	if(nr < 7)
		return Enotme;
	buf[nr] = 0;
	for(i = 0; i < 6; i++)
		if(buf[i] < '0' || buf[i] > '9')
			return Enotme;
	if(buf[6] != '/')
		return Enotme;
	mb->sync = mbvsync;
	if(debug)
		fprint(2, "planb virtual mbox %s\n", path);
	return nil;
}
