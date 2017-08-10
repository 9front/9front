#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <bio.h>

enum{
	Nsig = 4,
	Nhdr = Nsig+4+4,
	Ndict = 4+4+8,
	Nname = 8,
	Nbuf = 8192,
	Maxsz = 0x7fffffff - Nhdr
};

enum{
	LTnil,
	LTreg,
	LTmap,
	LTmrk,
	LTend
};
typedef struct Lump Lump;
struct Lump{
	char name[Nname+1];
	u32int ofs;
	uchar *buf;
	ulong nbuf;
	int type;
	File *f;
	Lump *l;
	Lump *lp;
};
Lump l1 = {.l = &l1, .lp = &l1}, *lumps = &l1;

Biobuf *wad;
u32int nlmp;
File *ldir, *fsig, *fwad;
int rdonly, dirty;

Srv fs;

char *mapn[] = {
	"things", "linedefs", "sidedefs", "vertexes", "segs",
	"ssectors", "nodes", "sectors", "reject", "blockmap"
};

void
strupr(char *s, char *p)
{
	char c;

	do{
		c = *p++;
		*s++ = toupper(c);
	}while(c != 0);
}

void
strlwr(char *s, char *p)
{
	char c;

	do{
		c = *p++;
		*s++ = tolower(c);
	}while(c != 0);
}

void
link(Lump *l, Lump *lp, int len)
{
	l->lp = lp;
	l->l = lp->l;
	lp->l->lp = l;
	lp->l = l;
	nlmp++;
	fwad->length += Ndict + len;
}

void
unlink(Lump *l)
{
	if(l->l == nil)
		return;
	l->lp->l = l->l;
	l->l->lp = l->lp;
	l->l = nil;
	nlmp--;
	fwad->length -= Ndict + (l->f != nil ? l->f->length : 0);
}

void
freelump(Lump *l)
{
	unlink(l);
	free(l->buf);
	free(l);
}

void
readlump(Lump *l, uchar *p, long n)
{
	if(n <= 0)
		return;
	Bseek(wad, l->ofs, 0);
	if(Bread(wad, p, n) != n)
		fprint(2, "readlump: short read: %r\n");
}

void
loadlump(File *f, ulong n)
{
	Lump *l;

	l = f->aux;
	if(f->length > n)
		n = f->length;
	l->buf = emalloc9p(n);
	l->nbuf = n;
	l->ofs = 0;
	readlump(l, l->buf, f->length);
}

Lump *
lastlump(Lump *lp)
{
	File *f, *dir;

	for(dir=lp->f, f=lp->l->f; lp->l!=lumps; lp=lp->l, f=lp->l->f)
		if(f->parent != dir && f->parent->parent != dir)
			break;
	if(lp->type == LTend && lp->f->parent == dir)
		lp = lp->lp;
	return lp;
}

int
nextmaplump(char *s)
{
	char **p;

	for(p=mapn; p<mapn+nelem(mapn); p++)
		if(strcmp(s, *p) == 0)
			return p-mapn;
	return -1;
}

Lump *
sortmap(Lump *lp, Lump *l)
{
	int ip, i;

	i = nextmaplump(l->f->name);
	for(; lp->l != lumps; lp=lp->l){
		ip = nextmaplump(lp->l->f->name);
		if(ip < 0 || ip > i)
			break;
	}
	return lp;
}

int
ismaplump(char *s)
{
	return nextmaplump(s) >= 0;
}

int
ismapname(char *s)
{
	if(strncmp(s, "map", 3) == 0)
		return isdigit(s[3]) && isdigit(s[4]);
	return s[0] == 'e' && isdigit(s[1])
		&& s[2] == 'm' && isdigit(s[3]);
}

int
ismarkname(char *s, char *m)
{
	char *p;

	p = strstr(s, m);
	if(p == nil || p[strlen(m)] != 0)
		return 0;
	if(p - s > 2)
		return 0;
	return 1;
}

int
validname(char *s, File *dir, int *type, int isnew, int isdir)
{
	int n;
	char c, *p;
	Lump *lp;

	*type = LTnil;
	n = strlen(s);
	if(n < 1 || n > sizeof(lp->name)-1){
		werrstr("invalid lump name");
		return 0;
	}
	for(p=s+n-1; c=*p, p-->=s;)
		if(!isprint(c) || isupper(c) || c == '/'){
			werrstr("invalid char %c in filename", c);
			return 0;
		}
	if(isnew && !ismaplump(s))
		for(lp=lumps->l; lp!=lumps; lp=lp->l)
			if(cistrcmp(s, lp->name) == 0){
				werrstr("duplicate non map lump");
				return 0;
			}
	*type = LTreg;
	lp = dir->aux;
	if(ismapname(s)){
		*type = LTmap;
		if(isnew && !isdir){
			werrstr("map marker not a directory");
			return 0;
		}else if(dir != fs.tree->root){
			werrstr("nested map directory");
			return 0;
		}
		return 1;
	}else if(ismarkname(s, "_end")){
		*type = LTend;
		if(dir == fs.tree->root || lp == nil || lp->type == LTmap){
			werrstr("orphaned end marker");
			return 0;
		}
		return 1;
	}else if(ismarkname(s, "_start")){
		*type = LTmrk;
		if(isnew){
			werrstr("not allowed");
			return 0;
		}
		goto mrkchk;
	}else if(isnew && isdir){
		*type = LTmrk;
		if(n > 2){
			werrstr("marker name too long");
			return 0;
		}
mrkchk:
		if(dir->parent != fs.tree->root){
			werrstr("start marker nested too deep");
			return 0;
		}else if(lp != nil && lp->type == LTmap){
			werrstr("start marker within map directory");
			return 0;
		}
		return 1;
	}else if(ismaplump(s) ^ (lp != nil && lp->type == LTmap)){
		werrstr("map lump outside of map directory");
		return 0;
	}
	return 1;
}

int
endldir(Lump *lp, Lump *le)
{
	char *s, name[sizeof lp->name];
	Lump *l;
	File *f;

	l = emalloc9p(sizeof *l);
	strcpy(l->name, lp->name);
	s = strrchr(l->name, '_');
	strcpy(s, "_END");
	strlwr(name, l->name);
	fprint(2, "adding end marker %s\n", l->name);
	if(!validname(name, lp->f, &l->type, 1, 0) || l->type != LTend)
		goto err;
	f = createfile(lp->f, name, nil, lp->f->mode & 0666, l);
	if(f == nil)
		goto err;
	closefile(f);
	l->f = f;
	link(l, le, 0);
	return 0;
err:
	free(l);
	return -1;
}

void
accessfile(File *f, int mode)
{
	f->atime = time(nil);
	if(mode & AWRITE){
		f->mtime = f->atime;
		f->qid.vers++;
		dirty = 1;
	}
}

void
fswstat(Req *r)
{
	int type;
	char *e;
	File *f, *fp;
	Lump *lp;

	e = "permission denied";
	if(rdonly)
		goto err;
	if(r->d.mode != ~0 || r->d.gid[0] != 0)
		goto err;
	f = r->fid->file;
	lp = f->aux;
	if(r->d.length != ~0 && r->d.length != f->length){
		if(f == fsig || f->mode & DMDIR)
			goto err;
		if(!hasperm(f, r->fid->uid, AWRITE))
			goto err;
		if(r->d.length < 0){
			e = "invalid file length";
			goto err;
		}
		if(fwad->length - f->length + r->d.length >= Maxsz){
			e = "lump size exceeds wad limit";
			goto err;
		}
	}
	if(r->d.name[0] != 0 && strcmp(r->d.name, f->name) != 0){
		fp = f->parent;
		if(fp == nil){
			e = "orphaned file";
			goto err;
		}
		if(!hasperm(fp, r->fid->uid, AWRITE))
			goto err;
		if(!validname(r->d.name, fp, &type, 1, f->mode & DMDIR)){
			responderror(r);
			return;
		}
		if(lp->type != type){
			e = "incompatible lump type";
			goto err;
		}
		incref(fp);
		fp = walkfile(fp, r->d.name);
		if(fp != nil){
			e = "file already exists";
			goto err;
		}
	}

	if(r->d.length != ~0 && r->d.length != f->length){
		if(lp->buf == nil)
			loadlump(f, r->d.length);
		fwad->length += r->d.length - f->length;
		f->length = r->d.length;
	}
	if(r->d.name[0] != 0 && strcmp(r->d.name, f->name) != 0){
		free(f->name);
		f->name = estrdup9p(r->d.name);
		strupr(lp->name, f->name);
		if(lp->type == LTmrk)
			strcat(lp->name, "_START");
	}
	accessfile(f, AWRITE);
	if(r->d.mtime != ~0)
		f->mtime = r->d.mtime;
	respond(r, nil);
	return;
err:
	respond(r, e);
}

void
fsremove(Req *r)
{
	File *f;
	Lump *lp;

	f = r->fid->file;
	lp = f->aux;
	if(f == fsig || f == fwad){
		respond(r, "not allowed");
		return;
	}else if(lp->l->f != nil && lp->l->f->parent == f){
		respond(r, "has children");
		return;
	}
	unlink(f->aux);
	dirty = 1;
	respond(r, nil);
}

char *
writesig(uchar *buf, char *s, vlong n)
{
	if(n > Nsig+1 || strncmp(s, "IWAD", Nsig) != 0 && strncmp(s, "PWAD", Nsig) != 0)
		return "invalid wad signature";
	memcpy(buf, s, Nsig);
	dirty = 1;
	return nil;
}

void
fswrite(Req *r)
{
	vlong n, m, ofs, end;
	File *f;
	Lump *l;

	f = r->fid->file;
	n = r->ifcall.count;
	ofs = r->ifcall.offset;
	if(f->mode & DMAPPEND)
		ofs = f->length;
	end = ofs + n;
	l = f->aux;
	if(f == fsig){
		respond(r, writesig(l->buf, r->ifcall.data, n));
		return;
	}
	if(l->buf == nil)
		loadlump(f, end + Nbuf);
	if(end > l->nbuf){
		m = l->nbuf + Nbuf > end ? l->nbuf + Nbuf : end;
		if(fwad->length - l->nbuf + m >= Maxsz){
			respond(r, "lump size exceeds wad limit");
			return;
		}
		l->buf = erealloc9p(l->buf, m);
		l->nbuf = m;
	}
	memcpy(l->buf + ofs, r->ifcall.data, n);
	m = end - f->length;
	if(m > 0){
		f->length += m;
		fwad->length += m;
	}
	accessfile(f, AWRITE);
	r->ofcall.count = n;
	respond(r, nil);
}

void
makewad(void)
{
	vlong n;
	uchar *p;
	u32int ofs;
	Lump *l, *lp;

	l = fwad->aux;
	free(l->buf);
	l->buf = emalloc9p(fwad->length);
	p = l->buf;
	lp = fsig->aux;
	memcpy(p, lp->buf, 4), p += 4;
	PBIT32(p, nlmp), p += 8;
	for(lp=lumps->l; lp!=lumps; p+=n, lp=lp->l){
		n = lp->f->length;
		if(lp->buf != nil)
			memcpy(p, lp->buf, n);
		else
			readlump(lp, p, n);
	}
	PBIT32(l->buf + 8, p - l->buf);
	ofs = Nhdr;
	for(lp=lumps->l; lp!=lumps; ofs+=n, lp=lp->l){
		n = lp->f->length;
		PBIT32(p, ofs), p += 4;
		PBIT32(p, n), p += 4;
		memcpy(p, lp->name, 8), p += 8;
	}
	dirty = 0;
}

void
fsread(Req *r)
{
	vlong n, ofs, end;
	File *f;
	Lump *l;

	f = r->fid->file;
	l = f->aux;
	ofs = r->ifcall.offset + l->ofs;
	end = l->ofs + f->length;
	n = r->ifcall.count;
	if(ofs + n >= end)
		n = end - ofs;
	if(n <= 0){
		r->ofcall.count = 0;
		respond(r, nil);
		return;
	}
	if(f == fwad && dirty)
		makewad();
	if(l->buf != nil)
		memcpy(r->ofcall.data, l->buf+ofs, n);
	else{
		Bseek(wad, ofs, 0);
		n = Bread(wad, r->ofcall.data, n);
		if(n < 0){
			responderror(r);
			return;
		}
	}
	accessfile(f, AREAD);
	r->ofcall.count = n;
	respond(r, nil);
}

int
addlump(Lump *l, File *dir)
{
	Lump *lp;

	lp = lumps->lp;
	if(dir != fs.tree->root){
		lp = dir->aux;
		lp = lp->type == LTmap ? sortmap(lp, l) : lastlump(lp);
	}
	if(l->type == LTend && lp->l->type == LTend && lp->l->f->parent == dir){
		werrstr("an end marker already exists");
		return -1;
	}
	link(l, lp, 0);
	if(l->type == LTmrk){
		strcat(l->name, "_START");
		if(endldir(l, l) < 0)
			return -1;
	}else if(l->type == LTreg){
		l->buf = emalloc9p(Nbuf);
		l->nbuf = Nbuf;
	}
	dirty = 1;
	return 0;
}

Lump *
createlump(char *s, File *dir, int ismark)
{
	int type;
	Lump *l;

	if(!validname(s, dir, &type, 1, ismark))
		return nil;
	l = emalloc9p(sizeof *l);
	l->type = type;
	strupr(l->name, s);
	return l;
}

void
fscreate(Req *r)
{
	int p;
	File *f;
	Lump *l;

	f = r->fid->file;
	p = r->ifcall.perm;
	if(p & DMDIR)
		p = p & ~0777 | p & f->mode & 0777;
	else
		p = p & ~0666 | p & f->mode & 0666;
	l = createlump(r->ifcall.name, f, p & DMDIR);
	if(l == nil)
		goto err;
	f = createfile(f, r->ifcall.name, r->fid->uid, p, l);
	if(f == nil){
		free(l);
		goto err;
	}
	l->f = f;
	if(addlump(l, r->fid->file) < 0){
		removefile(f);
		goto err;
	}
	r->fid->file = f;
	r->ofcall.qid = f->qid;
	respond(r, nil);
	return;
err:
	responderror(r);
}

void
fsopen(Req *r)
{
	File *f;

	f = r->fid->file;
	if((f->mode & DMAPPEND) == 0 && (r->ifcall.mode & OTRUNC) != 0
	&& f != fsig){
		fwad->length -= f->length;
		f->length = 0;
		dirty = 1;
	}
	respond(r, nil);
}

void
fsdestroyfile(File *f)
{
	freelump(f->aux);
}

Srv fs = {
	.open = fsopen,
	.create = fscreate,
	.read = fsread,
	.write = fswrite,
	.remove = fsremove,
	.wstat = fswstat
};

int
get32(Biobuf *bf, u32int *v)
{
	int n;
	uchar u[4];

	n = Bread(bf, u, sizeof u);
	if(n != sizeof u)
		return -1;
	*v = GBIT32(u);
	return 0;
}

File *
replacefile(File *dir, char *fname, int mode, Lump *l)
{
	File *f;

	incref(dir);
	f = walkfile(dir, fname);
	if(f == nil)
		return nil;
	if(removefile(f) < 0)
		return nil;
	f = createfile(dir, fname, nil, mode, l);
	return f;
}

void
addsigfile(char *sig)
{
	int n;
	Lump *l;
	File *f;

	n = strlen(sig) + 1;
	l = emalloc9p(sizeof *l);
	l->buf = (uchar *)estrdup9p(sig);
	l->buf[n-1] = '\n';
	f = createfile(fs.tree->root, "SIG", nil, rdonly ? 0444 : 0666, l);
	if(f == nil)
		sysfatal("addsigfile: %r");
	else{
		fsig = f;
		f->length = n;
	}
}

void
addwadfile(void)
{
	Lump *l;
	File *f;

	l = emalloc9p(sizeof *l);
	f = createfile(fs.tree->root, "WAD", nil, 0444, l);
	if(f == nil)
		sysfatal("addwadfile: %r");
	else{
		fwad = f;
		f->length = Nhdr;
	}
	dirty++;

}

void
checkends(void)
{
	Lump *lp;

	if(ldir == fs.tree->root)
		return;
	lp = ldir->aux;
	if(lp->type != LTmap && endldir(lp, lastlump(lp)) < 0)
		fprint(2, "checkends: %r\n");
	ldir = ldir->parent;
	checkends();
}

int
addfile(Lump *l, u32int *len, int mode)
{
	int err;
	char fname[sizeof l->name], *s;
	Lump *lp;
	File *f;

	*len = 0;
	if(get32(wad, &l->ofs) < 0 || get32(wad, len) < 0)
		return -1;
	if(Bread(wad, l->name, sizeof(l->name)-1) != sizeof(l->name)-1)
		return -1;
	strlwr(fname, l->name);

	lp = ldir->aux;
	err = !validname(fname, ldir, &l->type, 0, 0);
	switch(l->type){
	case LTmap:
		closefile(ldir);
		ldir = fs.tree->root;
		if(err && lp != nil && lp->type != LTmap){
			fprint(2, "addfile %s ofs=%#ux len=%#ux: %r\n", l->name, l->ofs, *len);
			if(endldir(lp, lastlump(lp)) < 0)
				fprint(2, "endldir: %r\n");
		}
		mode |= DMDIR|0111;
		*len = 0;
		break;
	case LTmrk:
		if(err){
			if(lp != nil && lp->type == LTmap){
				closefile(ldir);
				ldir = fs.tree->root;
			}else{
				fprint(2, "addfile %s ofs=%#ux len=%#ux: %r\n", l->name, l->ofs, *len);
				if(endldir(lp, lastlump(lp)) < 0)
					return -1;
				ldir = ldir->parent;
			}
		}
		s = strrchr(fname, '_');
		*s = 0;
		mode |= DMDIR|0111;
		*len = 0;
		break;
	case LTend:
		if(err){
			ldir = ldir->parent;
			return -1;
		}
		*len = 0;
		break;
	case LTreg:
		if(err){
			if(ismaplump(fname))
				fprint(2, "addfile %s ofs=%#ux len=%#ux: %r\n", l->name, l->ofs, *len);
			else
				ldir = fs.tree->root;
		}
		break;
	default:
		return -1;
	}

	f = createfile(ldir, fname, nil, mode, l);
	if(f == nil){
		fprint(2, "createfile %s: %r\n", l->name);
		if(mode & DMDIR)
			return -1;
		f = replacefile(ldir, fname, mode, l);
		if(f == nil)
			return -1;
	}
	if(mode & DMDIR)
		ldir = f;
	else if(l->type == LTend)
		ldir = ldir->parent;
	else
		closefile(f);
	f->length = *len;
	l->f = f;
	return 0;
}

void
parsewad(void)
{
	int n, ne, mode;
	u32int len;
	Lump *l;

	mode = rdonly ? 0444 : 0666;
	ldir = fs.tree->root;
	for(n=0, ne=nlmp, nlmp=0; n<ne; n++){
		l = emalloc9p(sizeof *l);
		if(addfile(l, &len, mode) < 0){
			fprint(2, "addfile %s ofs=%#ux len=%#ux: %r\n", l->name, l->ofs, len);
			free(l);
			continue;
		}
		link(l, lumps->lp, len);
	}
	checkends();
}

void
wadinfo(char *sig)
{
	int n;
	u32int dictofs;

	n = Bread(wad, sig, Nsig);
	if(n != Nsig)
		sysfatal("readwad: short read: %r");
	sig[4] = 0;
	if(strcmp(sig, "IWAD") != 0 && strcmp(sig, "PWAD") != 0)
		sysfatal("invalid wad signature");
	if(get32(wad, &nlmp) < 0 || get32(wad, &dictofs) < 0)
		sysfatal("wadinfo: %r");
	Bseek(wad, dictofs, 0);
}

void
usage(void)
{
	fprint(2, "usage: %s [-Dr] [-m mtpt] [-S srvname] [wad]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	int fl, p;
	char *mtpt, *srvname, sig[Nsig+1] = "PWAD";

	mtpt = "/mnt/wad";
	srvname = nil;
	fl = MREPL|MCREATE;
	p = DMDIR|0777;
	ARGBEGIN{
	case 'D': chatty9p++; break;
	case 'S': srvname = EARGF(usage()); break;
	case 'm': mtpt = EARGF(usage()); break;
	case 'r': rdonly++; p &= ~0222; fl &= ~MCREATE; break;
	default: usage();
	}ARGEND
	if(*argv != nil){
		wad = Bopen(*argv, OREAD);
		if(wad == nil)
			sysfatal("Bopen: %r");
		wadinfo(sig);
	}
	fs.tree = alloctree(nil, nil, p, fsdestroyfile);
	addsigfile(sig);
	addwadfile();
	parsewad();
	postmountsrv(&fs, srvname, mtpt, fl);
	exits(nil);
}
