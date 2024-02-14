#include "ext4_config.h"
#include "ext4.h"
#include <thread.h>
#include "ext4_mkfs.h"
#include "group.h"
#include "common.h"

#define TRACE(fmt, ...) //fprint(2, fmt, __VA_ARGS__)

#define BDEV2PART(bdev) ((bdev)->bdif->p_user)

static struct {
	QLock;
	Part *ps;
	u32int id;
}sv;

static long
preadn(int f, void *av, long n, vlong offset)
{
	char *a;
	long m, t;

	assert(offset >= 0);

	a = av;
	t = 0;
	while(t < n){
		m = pread(f, a+t, n-t, offset);
		if(m <= 0){
			if(t == 0)
				return m;
			break;
		}
		t += m;
		offset += m;
	}
	return t;
}

static int
bdopen(struct ext4_blockdev *bdev)
{
	Part *p;

	p = BDEV2PART(bdev);
	TRACE("bdopen %p\n", p);
	USED(p);

	return 0;
}

static int
bdread(struct ext4_blockdev *bdev, void *buf, u64int blkid, u32int blkcnt)
{
	Part *p;

	p = BDEV2PART(bdev);
	TRACE("bdread %p %p %llud %ud\n", p, buf, blkid, blkcnt);
	if(preadn(p->f, buf, blkcnt*p->bdif.ph_bsize, blkid*p->bdif.ph_bsize) != blkcnt*p->bdif.ph_bsize)
		return -1;

	return 0;
}

static int
bdwrite(struct ext4_blockdev *bdev, const void *buf, u64int blkid, u32int blkcnt)
{
	Part *p;

	p = BDEV2PART(bdev);
	TRACE("bdwrite %p %p %llud %ud\n", p, buf, blkid, blkcnt);
	if(pwrite(p->f, buf, blkcnt*p->bdif.ph_bsize, blkid*p->bdif.ph_bsize) != blkcnt*p->bdif.ph_bsize)
		return -1;

	return 0;
}

static int
bdclose(struct ext4_blockdev *bdev)
{
	Part *p;

	p = BDEV2PART(bdev);
	TRACE("bdclose %p\n", p);
	USED(p);

	return 0;
}

static int
getblksz(char *dev, u32int *blksz)
{
	char *s, *e, *g, *a[5];
	vlong x;
	int f, n, r;

	/* default blksz if couldn't find out the real one */
	*blksz = 512;

	f = -1;
	g = nil;
	if((s = smprint("%s_ctl", dev)) == nil)
		goto error;
	cleanname(s);
	if((e = strrchr(s, '/')) == nil)
		e = s;
	strcpy(e, "/ctl");
	f = open(s, OREAD);
	free(s);
	if(f >= 0){
		if((g = malloc(4096)) == nil)
			goto error;
		for(n = 0; (r = read(f, g+n, 4096-n-1)) > 0; n += r);
		g[n] = 0;
		close(f);
		f = -1;

		for(s = g; (e = strchr(s, '\n')) != nil; s = e+1){
			*e = 0;
			if(tokenize(s, a, nelem(a)) >= 3 && strcmp(a[0], "geometry") == 0){
				x = strtoll(a[2], &e, 0);
				if(x > 0 && *e == 0)
					*blksz = x;
				if(*blksz != x){
					werrstr("invalid block size: %s", a[2]);
					goto error;
				}
				break;
			}
		}
	}

	close(f);
	free(g);
	return 0;
error:
	close(f);
	free(g);
	return -1;
}

static int
fmtpart(Fmt *f)
{
	Part *p;

	p = va_arg(f->args, Part*);

	return fmtprint(f, f->r == 'M' ? "/%#llux" : "dev%#llux", p->qid.path);
}

static void *
readfile(Part *p, char *path, usize *sz)
{
	usize n, got;
	char *s, *d;
	ext4_file f;
	int r;

	d = nil;
	while(*path == '/')
		path++;
	s = smprint("/%s", path);
	r = ext4_fopen2(&p->mp, &f, s, O_RDONLY);
	free(s);

	if(r == 0){
		*sz = ext4_fsize(&f);
		if((d = malloc(*sz+1)) == nil){
			ext4_fclose(&f);
			goto error;
		}

		for(n = 0; n < *sz; n += got){
			if(ext4_fread(&f, d+n, *sz-n, &got) < 0){
				werrstr("readfile: %r");
				ext4_fclose(&f);
				goto error;
			}
			if(got == 0)
				break;
		}

		*sz = n;
		ext4_fclose(&f);
	}else{
error:
		free(d);
		d = nil;
		*sz = 0;
	}

	return d;
}

static int
mountpart(Part *p, Opts *opts)
{
	struct ext4_mountpoint *mp;
	usize sz;
	char *gr;
	int r;

	mp = &p->mp;
	if(ext4_mount(mp, &p->bdev, opts->rdonly) < 0){
		werrstr("mount: %r");
		goto error;
	}
	if(ext4_mount_setup_locks(mp, &p->oslocks) < 0){
		werrstr("locks: %r");
		goto error;
	}
	if(ext4_recover(mp) < 0){
		werrstr("recover: %r");
		goto error;
	}
	if(ext4_journal_start(mp) < 0){
		werrstr("journal: %r");
		goto error;
	}
	if(opts->cachewb)
		ext4_cache_write_back(mp, 1);

	if(ext4_get_sblock(mp, &p->sb) < 0){
		werrstr("sblock: %r");
		goto error;
	}

	r = 0;
	if(opts->group != nil){
		r = loadgroups(&p->groups, opts->group);
	}else if((gr = readfile(p, "/etc/group", &sz)) != nil){
		gr[sz] = 0;
		r = loadgroups(&p->groups, gr);
		free(gr);
	}
	if(r != 0)
		goto error;

	return 0;
error:
	werrstr("mountpart: %r");
	return -1;
}

static void
plock(void *aux)
{
	Part *p;

	p = aux;
	qlock(p);
}

static void
punlock(void *aux)
{
	Part *p;

	p = aux;
	qunlock(p);
}

Part *
openpart(char *dev, Opts *opts)
{
	struct ext4_mkfs_info info;
	struct ext4_fs fs;
	u32int blksz;
	Part *p;
	char *s;
	Dir *d;
	int f;

	d = nil;
	p = nil;
	s = nil;
	qlock(&sv);

	f = open(dev, ORDWR);
	if(f < 0 || (d = dirfstat(f)) == nil)
		goto error;
	/* see if it's already opened */
	for(p = sv.ps; p != nil && p->qid.path != d->qid.path; p = p->next);
	if(p == nil){ /* no? then make one */
		if(getblksz(dev, &blksz) != 0 || (p = calloc(1, sizeof(*p)+blksz+strlen(dev)+1)) == nil)
			goto error;

		p->f = f;
		p->qid = d->qid;
		p->bdev.bdif = &p->bdif;
		p->bdev.part_size = d->length;
		p->bdif.open = bdopen;
		p->bdif.bread = bdread;
		p->bdif.bwrite = bdwrite;
		p->bdif.close = bdclose;
		p->bdif.ph_bsize = blksz;
		p->bdif.ph_bcnt = d->length/blksz;
		p->bdif.ph_bbuf = p->blkbuf;
		p->oslocks.lock = plock;
		p->oslocks.unlock = punlock;
		p->oslocks.p_user = p;
		p->bdif.p_user = p;

		p->partdev = (char*)(p+1) + blksz;
		strcpy(p->partdev, dev);

		if(opts->fstype > 1){
			memset(&fs, 0, sizeof(fs));
			memset(&info, 0, sizeof(info));
			info.block_size = opts->blksz;
			snprint(info.label, sizeof(info.label), opts->label);
			info.inode_size = opts->inodesz;
			info.inodes = opts->ninode;
			info.journal = true;
			if(ext4_mkfs(&fs, &p->bdev, &info, opts->fstype) < 0){
				werrstr("mkfs: %r");
				goto error;
			}
		}

		if(mountpart(p, opts) != 0)
			goto error;

		p->next = sv.ps;
		if(sv.ps != nil)
			sv.ps->prev = p;
		sv.ps = p;
		p->qidmask.path = ((uvlong)sv.id++) << 32;
		p->qidmask.type = QTDIR;
	}else{
		close(f);
	}

	free(d);
	free(s);
	qunlock(&sv);

	return p;

error:
	werrstr("openpart: %r");
	if(f >= 0)
		close(f);
	free(d);
	free(p);
	free(s);
	qunlock(&sv);

	return nil;
}

static void
_closepart(Part *p)
{
	struct ext4_mountpoint *mp;

	mp = &p->mp;
	ext4_cache_write_back(mp, 0);
	if(ext4_journal_stop(mp) < 0)
		fprint(2, "closepart: journal: %s: %r\n", p->partdev);
	if(ext4_umount(mp) < 0)
		fprint(2, "closepart: umount %s: %r\n", p->partdev);
	close(p->f);
	if(p->prev != nil)
		p->prev = p->next;
	if(p->next != nil)
		p->next->prev = p->prev;
	if(p == sv.ps)
		sv.ps = p->next;
	freegroups(&p->groups);
	free(p);
}

void
closepart(Part *p)
{
	qlock(&sv);
	_closepart(p);
	qunlock(&sv);
}

void
closeallparts(void)
{
	qlock(&sv);
	while(sv.ps != nil)
		_closepart(sv.ps);
	qunlock(&sv);
}

void
statallparts(void)
{
	struct ext4_mount_stats s;
	uvlong div;
	Part *p;

	qlock(&sv);
	for(p = sv.ps; p != nil; p = p->next){
		if(ext4_mount_point_stats(&p->mp, &s) < 0){
			fprint(2, "%s: %r\n", p->partdev);
		}else{
			print(
				"%s (inodes) free %ud, used %ud, total %ud\n",
				p->partdev,
				s.free_inodes_count,
				s.inodes_count-s.free_inodes_count,
				s.inodes_count
			);
			print(
				"%s (blocks) free %llud, used %llud, total %llud, each %ud\n",
				p->partdev,
				s.free_blocks_count,
				s.blocks_count-s.free_blocks_count,
				s.blocks_count, s.block_size
			);
			div = 1024/(s.block_size/1024);
			print(
				"%s (MB) free %llud, used %llud, total %llud\n",
				p->partdev,
				s.free_blocks_count/div,
				(s.blocks_count-s.free_blocks_count)/div,
				s.blocks_count/div
			);
		}
	}
	qunlock(&sv);
}

void
syncallparts(void)
{
	Part *p;
	qlock(&sv);
	for(p = sv.ps; p != nil; p = p->next){
		if(ext4_cache_flush(&p->mp) < 0)
			fprint(2, "%s: %r\n", p->partdev);
	}
	qunlock(&sv);
}
