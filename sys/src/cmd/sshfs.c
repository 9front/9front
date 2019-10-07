#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <libsec.h>

int readonly;
int debug;
char *root = ".";
#define dprint(...) if(debug) fprint(2, __VA_ARGS__)
#pragma	varargck	type	"Σ"	int

enum {
	MAXPACK = 34000,
	MAXWRITE = 32768,
	MAXATTRIB = 64,
	VERSION = 3,
	MAXREQID = 32,
	HASH = 64
};

enum {
	SSH_FXP_INIT = 1,
	SSH_FXP_VERSION = 2,
	SSH_FXP_OPEN = 3,
	SSH_FXP_CLOSE = 4,
	SSH_FXP_READ = 5,
	SSH_FXP_WRITE = 6,
	SSH_FXP_LSTAT = 7,
	SSH_FXP_FSTAT = 8,
	SSH_FXP_SETSTAT = 9,
	SSH_FXP_FSETSTAT = 10,
	SSH_FXP_OPENDIR = 11,
	SSH_FXP_READDIR = 12,
	SSH_FXP_REMOVE = 13,
	SSH_FXP_MKDIR = 14,
	SSH_FXP_RMDIR = 15,
	SSH_FXP_REALPATH = 16,
	SSH_FXP_STAT = 17,
	SSH_FXP_RENAME = 18,
	SSH_FXP_READLINK = 19,
	SSH_FXP_SYMLINK = 20,
	SSH_FXP_STATUS = 101,
	SSH_FXP_HANDLE = 102,
	SSH_FXP_DATA = 103,
	SSH_FXP_NAME = 104,
	SSH_FXP_ATTRS = 105,
	SSH_FXP_EXTENDED = 200,
	SSH_FXP_EXTENDED_REPLY = 201,
	
	SSH_FXF_READ = 0x00000001,
	SSH_FXF_WRITE = 0x00000002,
	SSH_FXF_APPEND = 0x00000004,
	SSH_FXF_CREAT = 0x00000008,
	SSH_FXF_TRUNC = 0x00000010,
	SSH_FXF_EXCL = 0x00000020,
	SSH_FILEXFER_ATTR_SIZE = 0x00000001,
	SSH_FILEXFER_ATTR_UIDGID = 0x00000002,
	SSH_FILEXFER_ATTR_PERMISSIONS = 0x00000004,
	SSH_FILEXFER_ATTR_ACMODTIME = 0x00000008,
	SSH_FILEXFER_ATTR_EXTENDED = 0x80000000,

	SSH_FX_OK = 0,
	SSH_FX_EOF = 1,
	SSH_FX_NO_SUCH_FILE = 2,
	SSH_FX_PERMISSION_DENIED = 3,
	SSH_FX_FAILURE = 4,
	SSH_FX_BAD_MESSAGE = 5,
	SSH_FX_NO_CONNECTION = 6,
	SSH_FX_CONNECTION_LOST = 7,
	SSH_FX_OP_UNSUPPORTED = 8,
};

char *errors[] = {
	[SSH_FX_OK] "success",
	[SSH_FX_EOF] "end of file",
	[SSH_FX_NO_SUCH_FILE] "file does not exist",
	[SSH_FX_PERMISSION_DENIED] "permission denied",
	[SSH_FX_FAILURE] "failure",
	[SSH_FX_BAD_MESSAGE] "bad message",
	[SSH_FX_NO_CONNECTION] "no connection",
	[SSH_FX_CONNECTION_LOST] "connection lost",
	[SSH_FX_OP_UNSUPPORTED] "unsupported operation",
};

typedef struct SFid SFid;
typedef struct SReq SReq;
typedef struct IDEnt IDEnt;

struct SFid {
	RWLock;
	char *fn;
	uchar *hand;
	int handn;
	Qid qid;
	int dirreads;
	Dir *dirent;
	int ndirent, dirpos;
	uchar direof;
};

struct SReq {
	Req *req;
	SFid *closefid;
	SReq *next;
};

struct IDEnt {
	char *name;
	int id;
	IDEnt *next;
};
IDEnt *uidtab[HASH], *gidtab[HASH];

int rdfd, wrfd;
SReq *sreqrd[MAXREQID];
QLock sreqidlock;
Rendez sreqidrend = {.l = &sreqidlock};

SReq *sreqwr, **sreqlast = &sreqwr;
QLock sreqwrlock;
Rendez writerend = {.l = &sreqwrlock};

#define PUT4(p, u) (p)[0] = (u)>>24, (p)[1] = (u)>>16, (p)[2] = (u)>>8, (p)[3] = (u)
#define GET4(p)	((u32int)(p)[3] | (u32int)(p)[2]<<8 | (u32int)(p)[1]<<16 | (u32int)(p)[0]<<24)

int
fxpfmt(Fmt *f)
{
	int n;
	
	n = va_arg(f->args, int);
	switch(n){
	case SSH_FXP_INIT: fmtstrcpy(f, "SSH_FXP_INIT"); break;
	case SSH_FXP_VERSION: fmtstrcpy(f, "SSH_FXP_VERSION"); break;
	case SSH_FXP_OPEN: fmtstrcpy(f, "SSH_FXP_OPEN"); break;
	case SSH_FXP_CLOSE: fmtstrcpy(f, "SSH_FXP_CLOSE"); break;
	case SSH_FXP_READ: fmtstrcpy(f, "SSH_FXP_READ"); break;
	case SSH_FXP_WRITE: fmtstrcpy(f, "SSH_FXP_WRITE"); break;
	case SSH_FXP_LSTAT: fmtstrcpy(f, "SSH_FXP_LSTAT"); break;
	case SSH_FXP_FSTAT: fmtstrcpy(f, "SSH_FXP_FSTAT"); break;
	case SSH_FXP_SETSTAT: fmtstrcpy(f, "SSH_FXP_SETSTAT"); break;
	case SSH_FXP_FSETSTAT: fmtstrcpy(f, "SSH_FXP_FSETSTAT"); break;
	case SSH_FXP_OPENDIR: fmtstrcpy(f, "SSH_FXP_OPENDIR"); break;
	case SSH_FXP_READDIR: fmtstrcpy(f, "SSH_FXP_READDIR"); break;
	case SSH_FXP_REMOVE: fmtstrcpy(f, "SSH_FXP_REMOVE"); break;
	case SSH_FXP_MKDIR: fmtstrcpy(f, "SSH_FXP_MKDIR"); break;
	case SSH_FXP_RMDIR: fmtstrcpy(f, "SSH_FXP_RMDIR"); break;
	case SSH_FXP_REALPATH: fmtstrcpy(f, "SSH_FXP_REALPATH"); break;
	case SSH_FXP_STAT: fmtstrcpy(f, "SSH_FXP_STAT"); break;
	case SSH_FXP_RENAME: fmtstrcpy(f, "SSH_FXP_RENAME"); break;
	case SSH_FXP_READLINK: fmtstrcpy(f, "SSH_FXP_READLINK"); break;
	case SSH_FXP_SYMLINK: fmtstrcpy(f, "SSH_FXP_SYMLINK"); break;
	case SSH_FXP_STATUS: fmtstrcpy(f, "SSH_FXP_STATUS"); break;
	case SSH_FXP_HANDLE: fmtstrcpy(f, "SSH_FXP_HANDLE"); break;
	case SSH_FXP_DATA: fmtstrcpy(f, "SSH_FXP_DATA"); break;
	case SSH_FXP_NAME: fmtstrcpy(f, "SSH_FXP_NAME"); break;
	case SSH_FXP_ATTRS: fmtstrcpy(f, "SSH_FXP_ATTRS"); break;
	case SSH_FXP_EXTENDED: fmtstrcpy(f, "SSH_FXP_EXTENDED"); break;
	case SSH_FXP_EXTENDED_REPLY: fmtstrcpy(f, "SSH_FXP_EXTENDED_REPLY");
	default: fmtprint(f, "%d", n);
	}
	return 0;
}

char *
idlookup(IDEnt **tab, int id)
{
	IDEnt *p;
	
	for(p = tab[(ulong)id % HASH]; p != nil; p = p->next)
		if(p->id == id)
			return estrdup9p(p->name);
	return smprint("%d", id);
}

int
namelookup(IDEnt **tab, char *name)
{
	IDEnt *p;
	int i;
	char *q;
	
	for(i = 0; i < HASH; i++)
		for(p = tab[i]; p != nil; p = p->next)
			if(strcmp(p->name, name) == 0)
				return p->id;
	i = strtol(name, &q, 10);
	if(*q == 0) return i;
	werrstr("unknown %s '%s'", tab == uidtab ? "user" : "group", name);
	return -1;
}

int
allocsreqid(SReq *r)
{
	int i;

	qlock(&sreqidlock);
	for(;;){
		for(i = 0; i < MAXREQID; i++)
			if(sreqrd[i] == nil){
				sreqrd[i] = r;
				goto out;
			}
		rsleep(&sreqidrend);
	}
out:
	qunlock(&sreqidlock);
	return i;
}

int
vpack(uchar *p, int n, char *fmt, va_list a)
{
	uchar *p0 = p, *e = p+n;
	SReq *sr = nil;
	u32int u;
	u64int v;
	void *s;
	int c;

	for(;;){
		switch(c = *fmt++){
		case '\0':
			if(sr != nil){
				u = allocsreqid(sr);
				PUT4(p0+1, u);
			}
			return p - p0;
		case '_':
			if(++p > e) goto err;
			break;
		case '.':
			*va_arg(a, void**) = p;
			break;
		case 'b':
			if(p >= e) goto err;
			*p++ = va_arg(a, int);
			break;
		case '[':
		case 's':
			s = va_arg(a, void*);
			u = va_arg(a, int);
			if(c == 's'){
				if(p+4 > e) goto err;
				PUT4(p, u), p += 4;
			}
			if(u > e-p) goto err;
			memmove(p, s, u);
			p += u;
			break;
		case 'q':
			p += 4;
			if(p != p0+5 || p > e) goto err;
			sr = va_arg(a, SReq*);
			break;
		case 'u':
			u = va_arg(a, int);
			if(p+4 > e) goto err;
			PUT4(p, u), p += 4;
			break;
		case 'v':
			v = va_arg(a, vlong);
			if(p+8 > e) goto err;
			u = v>>32; PUT4(p, u), p += 4;
			u = v; PUT4(p, u), p += 4;
			break;
		}
	}
err:
	return -1;
}

int
vunpack(uchar *p, int n, char *fmt, va_list a)
{
	uchar *p0 = p, *e = p+n;
	u32int u;
	u64int v;
	void *s;

	for(;;){
		switch(*fmt++){
		case '\0':
			return p - p0;
		case '_':
			if(++p > e) goto err;
			break;
		case '.':
			*va_arg(a, void**) = p;
			break;
		case 'b':
			if(p >= e) goto err;
			*va_arg(a, int*) = *p++;
			break;
		case 's':
			if(p+4 > e) goto err;
			u = GET4(p), p += 4;
			if(u > e-p) goto err;
			*va_arg(a, void**) = p;
			*va_arg(a, int*) = u;
			p += u;
			break;
		case '[':
			s = va_arg(a, void*);
			u = va_arg(a, int);
			if(u > e-p) goto err;
			memmove(s, p, u);
			p += u;
			break;
		case 'u':
			if(p+4 > e) goto err;
			u = GET4(p);
			*va_arg(a, int*) = u;
			p += 4;
			break;
		case 'v':
			if(p+8 > e) goto err;
			v = (u64int)GET4(p) << 32;
			v |= (u32int)GET4(p+4);
			*va_arg(a, vlong*) = v;
			p += 8;
			break;
		}
	}
err:
	return -1;
}

int
pack(uchar *p, int n, char *fmt, ...)
{
	va_list a;
	va_start(a, fmt);
	n = vpack(p, n, fmt, a);
	va_end(a);
	return n;
}
int
unpack(uchar *p, int n, char *fmt, ...)
{
	va_list a;
	va_start(a, fmt);
	n = vunpack(p, n, fmt, a);
	va_end(a);
	return n;
}

void
sendpkt(char *fmt, ...)
{
	static uchar buf[MAXPACK];
	int n;
	va_list a;

	va_start(a, fmt);
	n = vpack(buf+4, sizeof(buf)-4, fmt, a);
	va_end(a);
	if(n < 0) {
		sysfatal("sendpkt: message too big");
		return;
	}
	PUT4(buf, n);
	n += 4;

	dprint("SFTP --> %Σ\n", (int)buf[4]);
	if(write(wrfd, buf, n) != n)
		sysfatal("write: %r");
}

static uchar rxpkt[MAXPACK];
static int rxlen;

int
recvpkt(void)
{
	static uchar rxbuf[MAXPACK];
	static int rxfill;
	int rc;
	
	while(rxfill < 4 || rxfill < (rxlen = GET4(rxbuf) + 4) && rxlen <= MAXPACK){
		rc = read(rdfd, rxbuf + rxfill, MAXPACK - rxfill);
		if(rc < 0) sysfatal("read: %r");
		if(rc == 0) sysfatal("read: eof");
		rxfill += rc;
	}
	if(rxlen > MAXPACK) sysfatal("received garbage");
	memmove(rxpkt, rxbuf + 4, rxlen - 4);
	memmove(rxbuf, rxbuf + rxlen, rxfill - rxlen);
	rxfill -= rxlen;
	rxlen -= 4;
	dprint("SFTP <-- %Σ\n", (int)rxpkt[0]);
	return rxpkt[0];
}

void
freedir1(Dir *d)
{
	free(d->name);
	free(d->uid);
	free(d->gid);
	free(d->muid);
}

void
freedir(SFid *s)
{
	int i;

	for(i = 0; i < s->ndirent; i++)
		freedir1(&s->dirent[i]);
	free(s->dirent);
	s->dirent = nil;
	s->ndirent = 0;
	s->dirpos = 0;
}

void
putsfid(SFid *s)
{
	if(s == nil) return;

	wlock(s);
	wunlock(s);

	free(s->fn);
	free(s->hand);
	freedir(s);
	free(s);
}

void
putsreq(SReq *s)
{
	free(s);
}

void
submitsreq(SReq *s)
{
	qlock(&sreqwrlock);
	*sreqlast = s;
	sreqlast = &s->next;
	rwakeup(&writerend);
	qunlock(&sreqwrlock);
}

void
submitreq(Req *r)
{
	SReq *s;
	
	s = emalloc9p(sizeof(SReq));
	s->req = r;
	submitsreq(s);
}

char *
pathcat(char *p, char *c)
{
	return cleanname(smprint("%s/%s", p, c));
}

char *
parentdir(char *p)
{
	return pathcat(p, "..");
}

char *
finalelem(char *p)
{
	char *q;
	
	q = strrchr(p, '/');
	if(q == nil) return estrdup9p(p);
	return estrdup9p(q+1);
}

u64int
qidcalc(char *c)
{
	uchar dig[SHA1dlen];

	sha1((uchar *) c, strlen(c), dig, nil);
	return dig[0] | dig[1] << 8 | dig[2] << 16 | dig[3] << 24 | (uvlong)dig[4] << 32 | (uvlong)dig[5] << 40 | (uvlong)dig[6] << 48 | (uvlong)dig[7] << 56;
}

void
walkprocess(Req *r, char *e)
{
	char *p;
	SFid *sf;
	
	sf = r->newfid->aux;
	if(e != nil){
		r->ofcall.nwqid--;
		if(r->ofcall.nwqid == 0){
			respond(r, e);
			return;
		}
		p = r->aux;
		r->aux = parentdir(p);
		free(p);
		submitreq(r);
	}else{
		assert(r->ofcall.nwqid > 0);
		wlock(sf);
		free(sf->fn);
		sf->fn = r->aux;
		r->aux = nil;
		sf->qid = r->ofcall.wqid[r->ofcall.nwqid - 1];
		wunlock(sf);
		respond(r, nil);
	}
}

int
attrib2dir(uchar *p0, uchar *ep, Dir *d)
{
	uchar *p;
	int i, rc, extn, extvn;
	u32int flags, uid, gid, perm, next;
	uchar *exts,  *extvs;
	
	p = p0;
	if(p + 4 > ep) return -1;
	flags = GET4(p), p += 4;
	if((flags & SSH_FILEXFER_ATTR_SIZE) != 0){
		rc = unpack(p, ep - p, "v", &d->length); if(rc < 0) return -1; p += rc;
	}
	if((flags & SSH_FILEXFER_ATTR_UIDGID) != 0){
		rc = unpack(p, ep - p, "uu", &uid, &gid); if(rc < 0) return -1; p += rc;
		d->uid = idlookup(uidtab, uid);
		d->gid = idlookup(gidtab, gid);
	}else{
		d->uid = estrdup9p("sshfs");
		d->gid = estrdup9p("sshfs");
	}
	d->muid = estrdup9p(d->uid);
	if((flags & SSH_FILEXFER_ATTR_PERMISSIONS) != 0){
		rc = unpack(p, ep - p, "u", &perm); if(rc < 0) return -1; p += rc;
		d->mode = perm & 0777;
		if((perm & 0170000) == 0040000) d->mode |= DMDIR;
	}
	d->qid.type = d->mode >> 24;
	if((flags & SSH_FILEXFER_ATTR_ACMODTIME) != 0){
		rc = unpack(p, ep - p, "uu", &d->atime, &d->mtime); if(rc < 0) return -1; p += rc;
		d->qid.vers = d->mtime;
	}
	if((flags & SSH_FILEXFER_ATTR_EXTENDED) != 0){
		rc = unpack(p, ep - p, "u", &next); if(rc < 0) return -1; p += rc;
		for(i = 0; i < next; i++){
			rc = unpack(p, ep - p, "ss", &exts, &extn, &extvs, &extvn); if(rc < 0) return -1; p += rc;
			exts[extn] = extvs[extvn] = 0;
		}
	}
	return p - p0;
}

int
dir2attrib(Dir *d, uchar **rp)
{
	int rc;
	uchar *r, *p, *e;
	u32int fl;
	int uid, gid;

	werrstr("phase error");
	*rp = r = emalloc9p(MAXATTRIB);
	e = r + MAXATTRIB;
	fl = 0;
	p = r + 4;
	if(d->length != (uvlong)-1){
		fl |= SSH_FILEXFER_ATTR_SIZE;
		rc = pack(p, e - p, "v", d->length); if(rc < 0) return -1; p += rc;
	}
	if(d->uid != nil && *d->uid != 0 || d->gid != nil && *d->gid != 0){
		/* FIXME: sending -1 for "don't change" works with openssh, but violates the spec */
		if(d->uid != nil && *d->uid != 0){
			uid = namelookup(uidtab, d->uid);
			if(uid == -1)
				return -1;
		}else
			uid = -1;
		if(d->gid != nil && *d->gid != 0){
			gid = namelookup(gidtab, d->gid);
			if(gid == -1)
				return -1;
		}else
			gid = -1;
		fl |= SSH_FILEXFER_ATTR_UIDGID;
		rc = pack(p, e - p, "uu", uid, gid); if(rc < 0) return -1; p += rc;
	}
	if(d->mode != (ulong)-1){
		fl |= SSH_FILEXFER_ATTR_PERMISSIONS;
		rc = pack(p, e - p, "u", d->mode); if(rc < 0) return -1; p += rc;
	}
	if(d->atime != (ulong)-1 || d->mtime != (ulong)-1){
		/* FIXME: see above */
		fl |= SSH_FILEXFER_ATTR_ACMODTIME;
		rc = pack(p, e - p, "uu", d->atime, d->mtime); if(rc < 0) return -1; p += rc;
	}
	PUT4(r, fl);
	return p - r;
}

int
attrfixupqid(Qid *qid)
{
	u32int flags;
	uchar *p;

	if(unpack(rxpkt, rxlen, "_____u", &flags) < 0) return -1;
	p = rxpkt + 9;
	if(flags & SSH_FILEXFER_ATTR_SIZE) p += 8;
	if(flags & SSH_FILEXFER_ATTR_UIDGID) p += 8;
	if(flags & SSH_FILEXFER_ATTR_PERMISSIONS){
		if(p + 4 > rxpkt + rxlen) return -1;
		if((GET4(p) & 0170000) != 0040000) qid->type = 0;
		else qid->type = QTDIR;
		p += 4;
	}
	if(flags & SSH_FILEXFER_ATTR_ACMODTIME){
		if(p + 8 > rxpkt + rxlen) return -1;
		p += 4;
		qid->vers = GET4(p);	/* mtime for qid.vers */
	}
	return 0;
}

int
parsedir(SFid *sf)
{
	int i, rc;
	Dir *d;
	u32int c;
	uchar *p, *ep;
	char *fn, *ln;
	int fns, lns;
	char *s;

	if(unpack(rxpkt, rxlen, "_____u", &c) < 0) return -1;
	wlock(sf);
	freedir(sf);
	sf->dirent = emalloc9p(c * sizeof(Dir));
	d = sf->dirent;
	p = rxpkt + 9;
	ep = rxpkt + rxlen;
	for(i = 0; i < c; i++){
		memset(d, 0, sizeof(Dir));
		rc = unpack(p, ep - p, "ss", &fn, &fns, &ln, &lns); if(rc < 0) goto err; p += rc;
		rc = attrib2dir(p, ep, d); if(rc < 0) goto err; p += rc;
		if(fn[0] == '.' && (fns == 1 || fns == 2 && fn[1] == '.')){
			freedir1(d);
			continue;
		}
		d->name = emalloc9p(fns + 1);
		memcpy(d->name, fn, fns);
		d->name[fns] = 0;
		s = pathcat(sf->fn, d->name);
		d->qid.path = qidcalc(s);
		free(s);
		sf->ndirent++;
		d++;
	}
	wunlock(sf);
	return 0;
err:
	freedir1(d);
	wunlock(sf);
	return -1;
}


void
readprocess(Req *r)
{
	int i;
	uchar *p, *ep;
	uint rv;
	SFid *sf;
	
	sf = r->fid->aux;
	wlock(sf);
	if(sf->direof){
		wunlock(sf);
		respond(r, nil);
		return;
	}
	i = sf->dirpos;
	p = (uchar*)r->ofcall.data + r->ofcall.count;
	ep = (uchar*)r->ofcall.data + r->ifcall.count;
	rv = ep - p;
	while(p < ep){
		if(i >= sf->ndirent)
			break;
		rv = convD2M(&sf->dirent[i], p, ep-p);
		if(rv <= BIT16SZ)
			break;
		p += rv;
		i++;
	}
	sf->dirpos = i;
	if(i >= sf->ndirent)
		freedir(sf);
	wunlock(sf);
	r->ofcall.count = p - (uchar*)r->ofcall.data;
	if(rv <= BIT16SZ)
		respond(r, nil);
	else
		submitreq(r);
}

void
sshfsread(Req *r)
{
	if((r->fid->qid.type & QTDIR) == 0){
		submitreq(r);
		return;
	}
	if(r->ifcall.offset == 0){
		SFid *sf = r->fid->aux;
		wlock(sf);
		freedir(sf);
		if(sf->dirreads > 0){
			wunlock(sf);
			r->aux = (void*)-1;
			submitreq(r);
			return;
		}
		wunlock(sf);
	}
	readprocess(r);
}

void
sshfsattach(Req *r)
{
	SFid *sf;

	if(r->aux == nil){
		sf = emalloc9p(sizeof(SFid));
		if(r->ifcall.aname != nil)
			switch(*r->ifcall.aname){
			case '~':
				switch(r->ifcall.aname[1]){
				case 0: sf->fn = estrdup9p("."); break;
				case '/': sf->fn = estrdup9p(r->ifcall.aname + 2); break;
				default:
					free(sf);
					respond(r, "invalid attach name");
					return;
				}
				break;
			case '/':
				sf->fn = estrdup9p(r->ifcall.aname);
				break;
			case 0:
				sf->fn = estrdup9p(root);
				break;
			default:
				sf->fn = pathcat(root, r->ifcall.aname);
			}
		else
			sf->fn = estrdup9p(root);
		r->fid->aux = sf;
		submitreq(r);
	}else{
		sf = r->fid->aux;
		sf->qid = (Qid){qidcalc(sf->fn), 0, QTDIR};
		r->ofcall.qid = sf->qid;
		r->fid->qid = sf->qid;
		respond(r, nil);
	}
}

void
sendproc(void *)
{
	SReq *r;
	SFid *sf;
	int x, y;
	char *s, *t;

	threadsetname("send");

	for(;;){
		qlock(&sreqwrlock);
		while(sreqwr == nil)
			rsleep(&writerend);
		r = sreqwr;
		sreqwr = r->next;
		if(sreqwr == nil) sreqlast = &sreqwr;
		qunlock(&sreqwrlock);

		sf = r->closefid;
		if(sf != nil){
			rlock(sf);
			sendpkt("bqs", SSH_FXP_CLOSE, r, sf->hand, sf->handn);
			runlock(sf);
			continue;
		}
		if(r->req == nil)
			sysfatal("nil request in queue");

		sf = r->req->fid->aux;
		switch(r->req->ifcall.type){
		case Tattach:
			rlock(sf);
			sendpkt("bqs", SSH_FXP_STAT, r, sf->fn, strlen(sf->fn));
			runlock(sf);
			break;
		case Twalk:
			sendpkt("bqs", SSH_FXP_STAT, r, r->req->aux, strlen(r->req->aux));
			break;
		case Topen:
			if((r->req->ofcall.qid.type & QTDIR) != 0){
				rlock(sf);
				sendpkt("bqs", SSH_FXP_OPENDIR, r, sf->fn, strlen(sf->fn));
				runlock(sf);
			}else{
				x = r->req->ifcall.mode;
				y = 0;
				switch(x & 3){
				case OREAD: y = SSH_FXF_READ; break;
				case OWRITE: y = SSH_FXF_WRITE; break;
				case ORDWR: y = SSH_FXF_READ | SSH_FXF_WRITE; break;
				}
				if(readonly && (y & SSH_FXF_WRITE) != 0){
					respond(r->req, "mounted read-only");
					putsreq(r);
					break;
				}
				if((x & OTRUNC) != 0)
					y |= SSH_FXF_TRUNC;
				rlock(sf);
				sendpkt("bqsuu", SSH_FXP_OPEN, r, sf->fn, strlen(sf->fn), y, 0);
				runlock(sf);
			}
			break;
		case Tcreate:
			rlock(sf);
			s = pathcat(sf->fn, r->req->ifcall.name);
			runlock(sf);
			if((r->req->ifcall.perm & DMDIR) != 0){
				if(r->req->aux == nil){
					r->req->aux = (void*)-1;
					sendpkt("bqsuu", SSH_FXP_MKDIR, r, s, strlen(s),
						SSH_FILEXFER_ATTR_PERMISSIONS, r->req->ifcall.perm & 0777);
				}else{
					r->req->aux = (void*)-2;
					sendpkt("bqs", SSH_FXP_OPENDIR, r, s, strlen(s));
				}
				free(s);
				break;
			}
			x = r->req->ifcall.mode;
			y = SSH_FXF_CREAT | SSH_FXF_EXCL;
			switch(x & 3){
			case OREAD: y |= SSH_FXF_READ; break;
			case OWRITE: y |= SSH_FXF_WRITE; break;
			case ORDWR: y |= SSH_FXF_READ | SSH_FXF_WRITE; break;
			}
			sendpkt("bqsuuu", SSH_FXP_OPEN, r, s, strlen(s), y,
				SSH_FILEXFER_ATTR_PERMISSIONS, r->req->ifcall.perm & 0777);
			free(s);
			break;
		case Tread:
			if((r->req->fid->qid.type & QTDIR) != 0){
				wlock(sf);
				if(r->req->aux == (void*)-1){
					sendpkt("bqs", SSH_FXP_CLOSE, r, sf->hand, sf->handn);
					free(sf->hand);
					sf->hand = nil;
					sf->handn = 0;
					sf->direof = 0;
					sf->dirreads = 0;
				}else if(r->req->aux == (void*)-2){
					sendpkt("bqs", SSH_FXP_OPENDIR, r, sf->fn, strlen(sf->fn));
				}else{
					sf->dirreads++;
					sendpkt("bqs", SSH_FXP_READDIR, r, sf->hand, sf->handn);
				}
				wunlock(sf);
			}else{
				rlock(sf);
				sendpkt("bqsvuu", SSH_FXP_READ, r, sf->hand, sf->handn,
					r->req->ifcall.offset, r->req->ifcall.count);
				runlock(sf);
			}
			break;
		case Twrite:
			x = r->req->ifcall.count - r->req->ofcall.count;
			if(x >= MAXWRITE) x = MAXWRITE;
			r->req->ofcall.offset = x;
			rlock(sf);
			sendpkt("bqsvs", SSH_FXP_WRITE, r, sf->hand, sf->handn,
				r->req->ifcall.offset + r->req->ofcall.count,
				r->req->ifcall.data + r->req->ofcall.count,
				x);
			runlock(sf);
			break;
		case Tstat:
			rlock(sf);
			r->req->d.name = finalelem(sf->fn);
			r->req->d.qid = sf->qid;
			if(sf->handn > 0 && (sf->qid.type & QTDIR) == 0)
				sendpkt("bqs", SSH_FXP_FSTAT, r, sf->hand, sf->handn);
			else
				sendpkt("bqs", SSH_FXP_STAT, r, sf->fn, strlen(sf->fn));
			runlock(sf);
			break;
		case Twstat:
			if(r->req->aux == (void*)-1){
				rlock(sf);
				s = parentdir(sf->fn);
				t = pathcat(s, r->req->d.name);
				r->req->aux = t;
				sendpkt("bqss", SSH_FXP_RENAME, r, sf->fn, strlen(sf->fn), t, strlen(t));
				runlock(sf);
				free(s);
				break;
			}
			x = dir2attrib(&r->req->d, (uchar **) &s);
			if(x < 0){
				responderror(r->req);
				putsreq(r);
				free(s);
				break;
			}
			rlock(sf);
			if(sf->handn > 0)
				sendpkt("bqs[", SSH_FXP_FSETSTAT, r, sf->hand, sf->handn, s, x);
			else
				sendpkt("bqs[", SSH_FXP_SETSTAT, r, sf->fn, strlen(sf->fn), s, x);
			runlock(sf);
			free(s);
			break;
		case Tremove:
			rlock(sf);
			if((sf->qid.type & QTDIR) != 0)
				sendpkt("bqs", SSH_FXP_RMDIR, r, sf->fn, strlen(sf->fn));
			else
				sendpkt("bqs", SSH_FXP_REMOVE, r, sf->fn, strlen(sf->fn));
			runlock(sf);
			break;
		default:
			fprint(2, "sendproc: unimplemented 9p request %F in queue\n", &r->req->ifcall);
			respond(r->req, "phase error");
			putsreq(r);
		}
	}
}

void
recvproc(void *)
{
	static char ebuf[256];

	SReq *r;
	SFid *sf;
	int t, id;
	u32int code;
	char *msg, *lang, *hand, *s;
	int msgn, langn, handn;
	int okresp;
	char *e;
	
	threadsetname("recv");
	
	for(;;){
		e = "phase error";
		switch(t = recvpkt()){
		case SSH_FXP_STATUS:
		case SSH_FXP_HANDLE:
		case SSH_FXP_DATA:
		case SSH_FXP_NAME:
		case SSH_FXP_ATTRS:
			break;
		default:
			fprint(2, "sshfs: received unexpected packet of type %Σ\n", t);
			continue;
		}
		id = GET4(rxpkt + 1);
		if(id >= MAXREQID){
			fprint(2, "sshfs: received %Σ response with id out of range, %d > %d\n", t, id, MAXREQID);
			continue;
		}
		qlock(&sreqidlock);
		r = sreqrd[id];
		if(r != nil){
			sreqrd[id] = nil;
			rwakeup(&sreqidrend);
		}
		qunlock(&sreqidlock);
		if(r == nil){
			fprint(2, "sshfs: received %Σ response to non-existent request (req id = %d)\n", t, id);
			continue;
		}
		if(r->closefid != nil){
			putsfid(r->closefid);
			putsreq(r);
			continue;
		}
		if(r->req == nil)
			sysfatal("recvproc: r->req == nil");

		sf = r->req->fid->aux;
		okresp = rxlen >= 9 && t == SSH_FXP_STATUS && GET4(rxpkt+5) == SSH_FX_OK;
		switch(r->req->ifcall.type){
		case Tattach:
			if(t != SSH_FXP_ATTRS) goto common;
			if(attrfixupqid(&r->req->ofcall.qid) < 0)
				goto garbage;
			r->req->aux = (void*)-1;
			if((r->req->ofcall.qid.type & QTDIR) == 0)
				respond(r->req, "not a directory");
			else
				sshfsattach(r->req);
			break;
		case Twalk:
			if(t != SSH_FXP_ATTRS) goto common;
			if(r->req->ofcall.nwqid <= 0
			|| attrfixupqid(&r->req->ofcall.wqid[r->req->ofcall.nwqid - 1]) < 0)
				goto garbage;
			walkprocess(r->req, nil);
			break;
		case Tcreate:
			if(okresp && r->req->aux == (void*)-1){
				submitreq(r->req);
				break;
			}
			/* wet floor */
		case Topen: opendir:
			if(t != SSH_FXP_HANDLE) goto common;
			if(unpack(rxpkt, rxlen, "_____s", &hand, &handn) < 0) goto garbage;
			wlock(sf);
			sf->handn = handn;
			sf->hand = emalloc9p(sf->handn);
			memcpy(sf->hand, hand, sf->handn);
			if(r->req->ifcall.type == Tcreate){
				s = sf->fn;
				sf->fn = pathcat(s, r->req->ifcall.name);
				free(s);
				sf->qid = (Qid){qidcalc(sf->fn), 0, (r->req->ifcall.perm & DMDIR) != 0 ? QTDIR : 0};
				r->req->ofcall.qid = sf->qid;
				r->req->fid->qid = sf->qid;
			}
			wunlock(sf);
			if(r->req->ifcall.type == Tread){
				r->req->aux = nil;
				readprocess(r->req);
			}else
				respond(r->req, nil);
			break;
		case Tread:
			if((r->req->fid->qid.type & QTDIR) != 0){
				if(r->req->aux == (void*)-1){
					if(t != SSH_FXP_STATUS) goto common;
					/* reopen even if close failed */
					r->req->aux = (void*)-2;
					submitreq(r->req);
				}else if(r->req->aux == (void*)-2)
					goto opendir;
				else{
					if(t != SSH_FXP_NAME) goto common;
					if(parsedir(sf) < 0) goto garbage;
					readprocess(r->req);
				}
				break;
			}
			if(t != SSH_FXP_DATA) goto common;
			if(unpack(rxpkt, rxlen, "_____s", &msg, &msgn) < 0) goto garbage;
			if(msgn > r->req->ifcall.count) msgn = r->req->ifcall.count;
			r->req->ofcall.count = msgn;
			memcpy(r->req->ofcall.data, msg, msgn);
			respond(r->req, nil);
			break;
		case Twrite:
			if(t != SSH_FXP_STATUS) goto common;
			if(okresp){
				r->req->ofcall.count += r->req->ofcall.offset;
				if(r->req->ofcall.count == r->req->ifcall.count)
					respond(r->req, nil);
				else
					submitreq(r->req);
				break;
			}
			if(r->req->ofcall.count == 0) goto common;
			respond(r->req, nil);
			break;
		case Tstat:
			if(t != SSH_FXP_ATTRS) goto common;
			if(attrib2dir(rxpkt + 5, rxpkt + rxlen, &r->req->d) < 0) goto garbage;
			respond(r->req, nil);
			break;
		case Twstat:
			if(!okresp) goto common;
			if(!r->req->d.name[0]){
				respond(r->req, nil);
				break;
			}
			if(r->req->aux == nil){
				r->req->aux = (void*)-1;
				submitreq(r->req);
			}else{
				wlock(sf);
				free(sf->fn);
				sf->fn = r->req->aux;
				wunlock(sf);
				respond(r->req, nil);
			}
			break;
		case Tremove:
			goto common;
		default:
			fprint(2, "sendproc: unimplemented 9p request %F in queue\n", &r->req->ifcall);
			respond(r->req, "phase error");
		}
		putsreq(r);
		continue;
		
	common:
		switch(t){
		case SSH_FXP_STATUS:
			if(unpack(rxpkt, rxlen, "_____uss", &code, &msg, &msgn, &lang, &langn) < 0){
	garbage:
				fprint(2, "sshfs: garbled packet in response to 9p request %F\n", &r->req->ifcall);
				break;
			}
			if(code == SSH_FX_OK)
				e = nil;
			else if(code == SSH_FX_EOF && r->req->ifcall.type == Tread){
				if((r->req->fid->qid.type & QTDIR) != 0){
					wlock(sf);
					sf->direof = 1;
					wunlock(sf);
					readprocess(r->req);
					putsreq(r);
					continue;
				}
				r->req->ofcall.count = 0;
				e = nil;
			}else if(msgn > 0){
				e = msg;
				e[msgn] = 0;
			}else if(code < nelem(errors))
				e = errors[code];
			else{
				snprint(ebuf, sizeof(ebuf), "error code %d", code);
				e = ebuf;
			}
			break;
		default:
			fprint(2, "sshfs: received unexpected packet %Σ for 9p request %F\n", t, &r->req->ifcall);
		}
		if(r->req->ifcall.type == Twalk)
			walkprocess(r->req, e);
		else
			respond(r->req, e);
		putsreq(r);
		continue;
	}
}

void
sshfswalk(Req *r)
{
	SFid *s, *t;
	char *p, *q;
	int i;

	if(r->fid != r->newfid){
		r->newfid->qid = r->fid->qid;
		s = r->fid->aux;
		t = emalloc9p(sizeof(SFid));
		t->fn = estrdup9p(s->fn);
		t->qid = s->qid;
		r->newfid->aux = t;
	}else
		t = r->fid->aux;
	if(r->ifcall.nwname == 0){
		respond(r, nil);
		return;
	}
	p = estrdup9p(t->fn);
	for(i = 0; i < r->ifcall.nwname; i++){
		q = pathcat(p, r->ifcall.wname[i]);
		free(p);
		p = q;
		r->ofcall.wqid[i] = (Qid){qidcalc(p), 0, QTDIR};
	}
	r->ofcall.nwqid = r->ifcall.nwname;
	r->aux = p;
	submitreq(r);
}

void
sshfsdestroyfid(Fid *f)
{
	SFid *sf;
	SReq *sr;

	sf = f->aux;
	if(sf == nil)
		return;
	if(sf->hand != nil){
		sr = emalloc9p(sizeof(SReq));
		sr->closefid = sf;
		submitsreq(sr);
	}else
		putsfid(sf);
}

void
sshfsdestroyreq(Req *r)
{
	if(r->ifcall.type == Twalk)
		free(r->aux);
}

void
sshfsstart(Srv *)
{
	proccreate(sendproc, nil, mainstacksize);
	proccreate(recvproc, nil, mainstacksize);
}

void
sshfsend(Srv *)
{
	dprint("sshfs: ending\n");
	threadexitsall(nil);
}

Srv sshfssrv = {
	.start sshfsstart,
	.attach sshfsattach,
	.walk sshfswalk,
	.open submitreq,
	.create submitreq,
	.read sshfsread,
	.write submitreq,
	.stat submitreq,
	.wstat submitreq,
	.remove submitreq,
	.destroyfid sshfsdestroyfid,
	.destroyreq sshfsdestroyreq,
	.end sshfsend,
};

char *
readfile(char *fn)
{
	char *hand, *dat;
	int handn, datn;
	u32int code;
	char *p;
	int off;
	
	if(fn == nil) return nil;
	sendpkt("busuu", SSH_FXP_OPEN, 0, fn, strlen(fn), SSH_FXF_READ, 0);
	if(recvpkt() != SSH_FXP_HANDLE) return nil;
	if(unpack(rxpkt, rxlen, "_____s", &dat, &handn) < 0) return nil;
	hand = emalloc9p(handn);
	memcpy(hand, dat, handn);
	off = 0;
	p = nil;
	for(;;){
		sendpkt("busvu", SSH_FXP_READ, 0, hand, handn, (uvlong)off, MAXWRITE);
		switch(recvpkt()){
		case SSH_FXP_STATUS:
			if(unpack(rxpkt, rxlen, "_____u", &code) < 0) goto err;
			if(code == SSH_FX_EOF) goto out;
		default:
			goto err;
		case SSH_FXP_DATA:
			if(unpack(rxpkt, rxlen, "_____s", &dat, &datn) < 0) goto err;
			break;
		}
		p = erealloc9p(p, off + datn + 1);
		memcpy(p + off, dat, datn);
		off += datn;
		p[off] = 0;
	}
err:
	p = nil;
out:
	sendpkt("bus", SSH_FXP_CLOSE, 0, hand, handn);
	free(hand);
	recvpkt();
	return p;
}

void
passwdparse(IDEnt **tab, char *s)
{
	IDEnt *e, **b;
	char *p, *n;
	int id;

	if(s == nil)
		return;
	for(p = s;;){
		n = p;
		p = strpbrk(p, ":\n"); if(p == nil) break; if(*p != ':'){ p++; continue; }
		*p = 0;
		p = strpbrk(p+1, ":\n");
		p = strpbrk(p, ":\n"); if(p == nil) break; if(*p != ':'){ p++; continue; }
		id = strtol(p+1, &p, 10);
		p = strchr(p, '\n');
		if(p == nil) break;
		p++;
		e = emalloc9p(sizeof(IDEnt));
		e->name = estrdup9p(n);
		e->id = id;
		b = &tab[((ulong)e->id) % HASH];
		e->next = *b;
		*b = e;
	}
	free(s);
}

int pfd[2];
int sshargc;
char **sshargv;

void
startssh(void *)
{
	char *f;

	close(pfd[0]);
	dup(pfd[1], 0);
	dup(pfd[1], 1);
	close(pfd[1]);
	if(strncmp(sshargv[0], "./", 2) != 0)
		f = smprint("/bin/%s", sshargv[0]);
	else
		f = sshargv[0];
	procexec(nil, f, sshargv);
	sysfatal("exec: %r");
}

void
usage(void)
{
	static char *common = "[-abdRUG] [-s service] [-m mtpt] [-u uidfile] [-g gidfile]";
	fprint(2, "usage: %s %s [-- ssh-options] [user@]host\n", argv0, common);
	fprint(2, "       %s %s -c cmdline\n", argv0, common);
	fprint(2, "       %s %s -p\n", argv0, common);
	threadexits("usage");
}

void
threadmain(int argc, char **argv)
{
	u32int x;
	static int pflag, cflag;
	static char *svc, *mtpt;
	static int mflag;
	static char *uidfile, *gidfile;
	
	fmtinstall(L'Σ', fxpfmt);
	
	mtpt = "/n/ssh";
	uidfile = "/etc/passwd";
	gidfile = "/etc/group";
	ARGBEGIN{
	case 'R': readonly++; break;
	case 'd': debug++; chatty9p++; break;
	case 'p': pflag++; break;
	case 'c': cflag++; break;
	case 's': svc = EARGF(usage()); break;
	case 'a': mflag |= MAFTER; break;
	case 'b': mflag |= MBEFORE; break;
	case 'm': mtpt = EARGF(usage()); break;
	case 'M': mtpt = nil; break;
	case 'u': uidfile = EARGF(usage()); break;
	case 'U': uidfile = nil; break;
	case 'g': gidfile = EARGF(usage()); break;
	case 'G': gidfile = nil; break;
	case 'r': root = EARGF(usage()); break;
	default: usage();
	}ARGEND;
	
	if(readonly){
		sshfssrv.create = nil;
		sshfssrv.write = nil;
		sshfssrv.wstat = nil;
		sshfssrv.remove = nil;
	}
	
	if(pflag){
		rdfd = 0;
		wrfd = 1;
	}else{
		if(argc == 0) usage();
		if(cflag){
			sshargc = argc;
			sshargv = argv;
		}else{
			sshargc = argc + 2;
			sshargv = emalloc9p(sizeof(char *) * (sshargc + 1));
			sshargv[0] = "ssh";
			memcpy(sshargv + 1, argv, argc * sizeof(char *));
			sshargv[sshargc - 1] = "#sftp";
		}
		pipe(pfd);
		rdfd = wrfd = pfd[0];
		procrfork(startssh, nil, mainstacksize, RFFDG|RFNOTEG|RFNAMEG);
		close(pfd[1]);
	}

	sendpkt("bu", SSH_FXP_INIT, VERSION);
	if(recvpkt() != SSH_FXP_VERSION || unpack(rxpkt, rxlen, "_u", &x) < 0) sysfatal("received garbage");
	if(x != VERSION) sysfatal("server replied with incompatible version %d", x);
	
	passwdparse(uidtab, readfile(uidfile));
	passwdparse(gidtab, readfile(gidfile));
	
	threadpostmountsrv(&sshfssrv, svc, mtpt, MCREATE | mflag);

	threadexits(nil);
}
