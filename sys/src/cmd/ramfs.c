#include <u.h>
#include <libc.h>
#include <auth.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <pool.h>

char Ebadoff[] = "bad file offset or count";
char Eexist[] = "file already exists";
char Enomem[] = "no memory";
char Eperm[] = "permission denied";
char Enotowner[] = "not owner";
char Elocked[] = "file is locked";

enum {
	Tdat	= 0xbabababa,	
	Tind	= 0xdadadada,

	ESIZE	= 64*1024,
};

#define MAXFSIZE ((0x7fffffffll/sizeof(Ram*))*ESIZE)

typedef struct Ram Ram;
struct Ram
{
	int	type;
	int 	size;
	Ram	**link;
	Ram	*ent[];
};

int private;

void*
ramalloc(ulong size)
{
	void *v;

	v = sbrk(size);
	if(v == (void*)-1)
		return nil;
	return v;
}

void
rammoved(void*, void *to)
{
	Ram **link, **elink, *x = to;

	*x->link = x;
	if(x->type != Tind)
		return;

	link = x->ent;
	for(elink = link + (x->size / sizeof(Ram*)); link < elink; link++)
		if((x = *link) != nil)
			x->link = link;
}

void
ramnolock(Pool*)
{
}

Pool rampool = {
        .name=          "ram",
        .maxsize=       800*1024*1024,
        .minarena=      4*1024,
        .quantum=       32,
        .alloc=         ramalloc,
	.move=		rammoved,
	.lock=		ramnolock,
	.unlock=	ramnolock,
        .flags=         0,
};

void
accessfile(File *f, int a)
{
	f->atime = time(0);
	if(a & AWRITE){
		f->mtime = f->atime;
		f->qid.vers++;
	}
}

void
fsread(Req *r)
{
	int o, n, i, count;
	vlong top, off;
	File *f;
	Ram *x;
	char *p;

	f = r->fid->file;
	off = r->ifcall.offset;
	count = r->ifcall.count;

	if(count == 0 || off >= f->length || f->aux == nil){
		r->ofcall.count = 0;
		respond(r, nil);
		return;
	}

	top = off + count;
	if(top > MAXFSIZE){
		respond(r, Ebadoff);
		return;
	}
		
	if(top > f->length){
		top = f->length;
		count = top - off;
	}
	p = (char*)r->ofcall.data;
	while(count > 0){
		i = off / ESIZE;
		o = off % ESIZE;

		x = (Ram*)f->aux;
		if(i < (x->size / sizeof(Ram*)))
			x = x->ent[i];
		else
			x = nil;
		if(x != nil && o < x->size){
			n = x->size - o;
			if(n > count)
				n = count;
			memmove(p, (char*)&x[1] + o, n);
		} else {
			n = ESIZE - o;
			if(n > count)
				n = count;
			memset(p, 0, n);
		}
		p += n;
		off += n;
		count -= n;
	}
	accessfile(f, AREAD);

	r->ofcall.count = p - (char*)r->ofcall.data;
	respond(r, nil);
}

void
fswrite(Req *r)
{
	int o, n, i, count;
	Ram *x, **link;
	vlong top, off;
	File *f;
	char *p;

	f = r->fid->file;
	off = r->ifcall.offset;
	count = r->ifcall.count;

	if(f->mode & DMAPPEND)
		off = f->length;

	if(count == 0){
		r->ofcall.count = 0;
		respond(r, nil);
		return;
	}

	top = off + count;
	if(top > MAXFSIZE){
		respond(r, Ebadoff);
		return;
	}

	n = ((top + ESIZE-1)/ESIZE) * sizeof(Ram*);
	x = (Ram*)f->aux;
	if(x == nil || x->size < n){
		x = poolrealloc(&rampool, x, sizeof(Ram) + n);		
		if(x == nil){
			respond(r, Enomem);
			return;
		}
		link = (Ram**)&f->aux;
		if(*link == nil){
			memset(x, 0, sizeof(Ram));
			x->type = Tind;
			x->link = link;
			*link = x;
		} else if(x != *link)
			rammoved(*link, x);
		memset((char*)&x[1] + x->size, 0, n - x->size);
		x->size = n;
	}

	p = (char*)r->ifcall.data;
	while(count > 0){
		i = off / ESIZE;
		o = off % ESIZE;

		n = ESIZE - o;
		if(n > count)
			n = count;

		x = ((Ram*)f->aux)->ent[i];
		if(x == nil || x->size < o+n){
			x = poolrealloc(&rampool, x, sizeof(Ram) + o+n);
			if(x == nil){
				respond(r, Enomem);
				return;
			}
			link = &((Ram*)f->aux)->ent[i];
			if(*link == nil){
				memset(x, 0, sizeof(Ram));
				x->type = Tdat;
			}
			if(o > x->size)
				memset((char*)&x[1] + x->size, 0, o - x->size);
			x->size = o + n;
			x->link = link;
			*link = x;
		}

		memmove((char*)&x[1] + o, p, n);
		p += n;
		off += n;
		count -= n;
	}

	if(top > f->length)
		f->length = top;
	accessfile(f, AWRITE);

	r->ofcall.count = p - (char*)r->ifcall.data;
	respond(r, nil);
}

void
truncfile(File *f, vlong l)
{
	int i, o, n;
	Ram *x;

	x = (Ram*)f->aux;
	if(x != nil){
		n = x->size / sizeof(Ram*);
		i = l / ESIZE;
		if(i < n){
			o = l % ESIZE;
			if(o != 0 && x->ent[i] != nil){
				x->ent[i]->size = o * sizeof(Ram*);
				i++;
			}
			while(i < n){
				if(x->ent[i] != nil){
					poolfree(&rampool, x->ent[i]);
					x->ent[i] = nil;
				}
				i++;
			}
		}
		if(l == 0){
			poolfree(&rampool, (Ram*)f->aux);
			f->aux = nil;
		}
	}
	f->length = l;
}

void
fswstat(Req *r)
{
	File *f, *w;
	char *u;

	f = r->fid->file;
	u = r->fid->uid;

	/*
	 * To change length, must have write permission on file.
	 */
	if(r->d.length != ~0 && r->d.length != f->length){
		if(r->d.length > MAXFSIZE){
			respond(r, Ebadoff);
			return;
		}
	 	if(!hasperm(f, u, AWRITE) || (f->mode & DMDIR) != 0)
			goto Perm;
	}

	/*
	 * To change name, must have write permission in parent.
	 */
	if(r->d.name[0] != '\0' && strcmp(r->d.name, f->name) != 0){
		if((w = f->parent) == nil)
			goto Perm;
		incref(w);
	 	if(!hasperm(w, u, AWRITE)){
			closefile(w);
			goto Perm;
		}
		if((w = walkfile(w, r->d.name)) != nil){
			closefile(w);
			respond(r, Eexist);
			return;
		}
	}

	/*
	 * To change mode, must be owner or group leader.
	 * Because of lack of users file, leader=>group itself.
	 */
	if(r->d.mode != ~0 && f->mode != r->d.mode){
		if(strcmp(u, f->uid) != 0)
		if(strcmp(u, f->gid) != 0){
			respond(r, Enotowner);
			return;
		}
	}

	/*
	 * To change group, must be owner and member of new group,
	 * or leader of current group and leader of new group.
	 * Second case cannot happen, but we check anyway.
	 */
	while(r->d.gid[0] != '\0' && strcmp(f->gid, r->d.gid) != 0){
		if(strcmp(u, f->uid) == 0)
			break;
		if(strcmp(u, f->gid) == 0)
		if(strcmp(u, r->d.gid) == 0)
			break;
		respond(r, Enotowner);
		return;
	}

	if(r->d.mode != ~0){
		f->mode = r->d.mode;
		f->qid.type = f->mode >> 24;
	}
	if(r->d.name[0] != '\0'){
		free(f->name);
		f->name = estrdup9p(r->d.name);
	}
	if(r->d.length != ~0 && r->d.length != f->length)
		truncfile(f, r->d.length);

	accessfile(f, AWRITE);
	if(r->d.mtime != ~0){
		f->mtime = r->d.mtime;
	}

	respond(r, nil);
	return;

Perm:
	respond(r, Eperm);
}

void
fscreate(Req *r)
{
	File *f;
	int p;

	f = r->fid->file;
	p = r->ifcall.perm;
	if((p & DMDIR) != 0)
		p = (p & ~0777) | ((p & f->mode) & 0777);
	else
		p = (p & ~0666) | ((p & f->mode) & 0666);
	if((f = createfile(f, r->ifcall.name, r->fid->uid, p, nil)) == nil){
		responderror(r);
		return;
	}
	f->atime = f->mtime = time(0);
	f->aux = nil;
	r->fid->file = f;
	r->ofcall.qid = f->qid;
	respond(r, nil);
}

void
fsopen(Req *r)
{
	File *f;

	f = r->fid->file;
	if((f->mode & DMEXCL) != 0){
		if(f->ref > 2 && (long)((ulong)time(0)-(ulong)f->atime) < 300){
			respond(r, Elocked);
			return;
		}
	}
	if((f->mode & DMAPPEND) == 0 && (r->ifcall.mode & OTRUNC) != 0){
		truncfile(f, 0);
		accessfile(f, AWRITE);
	}
	respond(r, nil);
}

void
fsdestroyfid(Fid *fid)
{
	File *f;

	f = fid->file;
	if(fid->omode != -1 && (fid->omode & ORCLOSE) != 0 && f != nil && f->parent != nil)
		removefile(f);
}

void
fsdestroyfile(File *f)
{
	truncfile(f, 0);
}

void
fsstart(Srv *)
{
	char buf[40];
	int ctl;

	if(private){
		snprint(buf, sizeof buf, "/proc/%d/ctl", getpid());
		if((ctl = open(buf, OWRITE)) < 0)
			sysfatal("can't protect memory: %r");
		fprint(ctl, "noswap\n");
		fprint(ctl, "private\n");
		close(ctl);
	}
}

Srv fs = {
	.open=		fsopen,
	.read=		fsread,
	.write=		fswrite,
	.wstat=		fswstat,
	.create=	fscreate,
	.destroyfid=	fsdestroyfid,

	.start=		fsstart,
};

void
usage(void)
{
	fprint(2, "usage: %s [-Dipsubac] [-m mountpoint] [-S srvname]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *srvname = nil;
	char *mtpt = "/tmp";
	int mountflags, stdio;

	fs.tree = alloctree(nil, nil, DMDIR|0777, fsdestroyfile);

	mountflags = stdio = 0;
	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	case 's':
		srvname = "ramfs";
		mtpt = nil;
		break;
	case 'S':
		srvname = EARGF(usage());
		mtpt = nil;
		break;
	case 'm':
		mtpt = EARGF(usage());
		break;
	case 'i':
		stdio = 1;
		break;
	case 'p':
		private = 1;
		break;
	case 'u':
		rampool.maxsize = (uintptr)~0;
		break;
	case 'b':
		mountflags |= MBEFORE;
		break;
	case 'c':
		mountflags |= MCREATE;
		break;
	case 'a':
		mountflags |= MAFTER;
		break;
	default:
		usage();
	}ARGEND;
	if(argc > 0)
		usage();

	if(stdio){
		fs.infd = 0;
		fs.outfd = 1;
		srv(&fs);
		exits(0);
	}

	if(srvname == nil && mtpt == nil)
		sysfatal("must specify -S, or -m option");

	if(mountflags == 0)
		mountflags = MREPL | MCREATE;
	postmountsrv(&fs, srvname, mtpt, mountflags);
	exits(0);
}
