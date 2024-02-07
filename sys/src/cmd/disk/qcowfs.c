/* Adapted from OpenBSD's src/usr.sbin/vmd/vioqcow2.c */
#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

typedef struct Header Header;
typedef struct Disk Disk;

struct Header {
	char magic[4];
	u32int version;
	u64int backingoff;
	u32int backingsz;
	u32int clustershift;
	u64int disksz;
	u32int cryptmethod;
	u32int l1sz;
	u64int l1off;
	u64int refoff;
	u32int refsz;
	u32int snapcount;
	u64int snapsz;
	/* v3 additions */
	u64int incompatfeatures;
	u64int compatfeatures;
	u64int autoclearfeatures;
	u32int reforder;	/* Bits = 1 << reforder */
	u32int headersz;
};

#define QCOW2_COMPRESSED	0x4000000000000000ull
#define QCOW2_INPLACE		0x8000000000000000ull
char *MAGIC_QCOW		= "QFI\xfb";
enum{
	QCOW2_DIRTY		= 1 << 0,
	QCOW2_CORRUPT		= 1 << 1,

	ICFEATURE_DIRTY		= 1 << 0,
	ICFEATURE_CORRUPT	= 1 << 1,

	ACFEATURE_BITEXT	= 1 << 0,

	HDRSZ = 4 + 4 + 8 + 4 + 4 + 8 + 4 + 4 + 8 + 8 + 4 + 4 + 8 + 8 + 8 + 8 + 4 + 4,
};

struct Disk {
	RWLock lock;
	Disk *base;
	Header h;

	int       fd;
	u64int *l1;
	s64int     end;
	s64int	  clustersz;
	s64int	  disksz; /* In bytes */
	u32int  cryptmethod;

	u32int l1sz;
	s64int	 l1off;

	s64int	 refoff;
	s64int	 refsz;

	u32int nsnap;
	s64int	 snapoff;

	/* v3 features */
	u64int incompatfeatures;
	u64int autoclearfeatures;
	u32int refssz;
	u32int headersz;
};

#define PUT2(p, u) (p)[0] = (u)>>8, (p)[1] = (u)
#define GET2(p) (u16int)(p)[1] | (u16int)(p)[0]<<8
#define PUT4(p, u) (p)[0] = (u)>>24, (p)[1] = (u)>>16, (p)[2] = (u)>>8, (p)[3] = (u)
#define GET4(p)	(u32int)(p)[3] | (u32int)(p)[2]<<8 | (u32int)(p)[1]<<16 | (u32int)(p)[0]<<24

#define PUT8(p, u) (p)[0] = (u)>>56, (p)[1] = (u)>>48, (p)[2] = (u)>>40, (p)[3] = (u)>>32, \
	(p)[4] = (u)>>24, (p)[5] = (u)>>16, (p)[6] = (u)>>8, (p)[7] = (u)

#define GET8(p)	(u64int)(p)[7] | (u64int)(p)[6]<<8 | (u64int)(p)[5]<<16 | (u64int)(p)[4]<<24 | \
	(u64int)(p)[3]<<32 | (u64int)(p)[2]<<40 | (u64int)(p)[1]<<48 | (u64int)(p)[0]<<56

int
ftruncate(int fd, s64int length)
{
	Dir d;

	if(length < 0)
		return -1;
	nulldir(&d);
	d.length = length;
	if(dirfwstat(fd, &d) < 0)
		return -1;
	return 0;
}

static void
writehdr(Header *src, int fd)
{
	uchar store[HDRSZ];
	uchar *buf = store;

	memcpy(buf, src->magic, strlen(src->magic)); buf += 4;
	PUT4(buf, src->version); buf += 4;

	PUT8(buf, src->backingoff); buf += 8;
	PUT4(buf, src->backingsz); buf += 4;
	PUT4(buf, src->clustershift); buf += 4;
	PUT8(buf, src->disksz); buf += 8;
	PUT4(buf, src->cryptmethod); buf += 4;
	PUT4(buf, src->l1sz); buf += 4;
	PUT8(buf, src->l1off); buf += 8;
	PUT8(buf, src->refoff); buf += 8;
	PUT4(buf, src->refsz); buf += 4;
	PUT4(buf, src->snapcount); buf += 4;
	PUT8(buf, src->snapsz); buf += 8;
	PUT8(buf, src->incompatfeatures); buf += 8;
	PUT8(buf, src->compatfeatures); buf += 8;
	PUT8(buf, src->autoclearfeatures); buf += 8;
	PUT4(buf, src->reforder); buf += 4;
	PUT4(buf, src->headersz);

	if(write(fd, store, sizeof store) != sizeof store)
		sysfatal("writehdr: %r");
}

static void
readhdr(Header *dst, int fd)
{
	uchar store[HDRSZ];
	uchar *buf = store;

	if(readn(fd, store, sizeof store) != sizeof store)
		sysfatal("short read on header: %r");
	if(memcmp(MAGIC_QCOW, buf, strlen(MAGIC_QCOW)) != 0)
		sysfatal("invalid magic");
	buf += 4;

	dst->version = GET4(buf);
	if(dst->version != 2 && dst->version != 3)
		sysfatal("unsupported version: %d", dst->version);
	buf += 4;

	dst->backingoff = GET8(buf); buf += 8;
	dst->backingsz = GET4(buf); buf += 4;
	dst->clustershift = GET4(buf); buf += 4;
	dst->disksz = GET8(buf); buf += 8;
	dst->cryptmethod = GET4(buf); buf += 4;
	dst->l1sz = GET4(buf); buf += 4;
	dst->l1off = GET8(buf); buf += 8;
	dst->refoff = GET8(buf); buf += 8;
	dst->refsz = GET4(buf); buf += 4;
	dst->snapcount = GET4(buf); buf += 4;
	dst->snapsz = GET8(buf); buf += 8;
	dst->incompatfeatures = GET8(buf); buf += 8;
	dst->compatfeatures = GET8(buf); buf += 8;
	dst->autoclearfeatures = GET8(buf); buf += 8;
	dst->reforder = GET4(buf); buf += 4;
	dst->headersz = GET4(buf);
}

#define ALIGNSZ(sz, align)	((sz + align - 1) & ~(align - 1))

static void
qc2create(int fd, u64int disksz)
{
	Header hdr;
	s64int base_len;
	u64int l1sz, refsz, initsz, clustersz;
	u64int l1off, refoff, i, l1entrysz, refentrysz;
	uchar v[8], v2[2];

	clustersz = 1<<16;
	l1off = ALIGNSZ(HDRSZ, clustersz);

	l1entrysz = clustersz * clustersz / 8;
	l1sz = (disksz + l1entrysz - 1) / l1entrysz;

	refoff = ALIGNSZ(l1off + 8*l1sz, clustersz);
	refentrysz = clustersz * clustersz * clustersz / 2;
	refsz = (disksz + refentrysz - 1) / refentrysz;

	initsz = ALIGNSZ(refoff + refsz*clustersz, clustersz);
	base_len = 0;

	memcpy(hdr.magic, MAGIC_QCOW, strlen(MAGIC_QCOW));
	hdr.version		= 3;
	hdr.backingoff		= 0;
	hdr.backingsz		= base_len;
	hdr.clustershift	= 16;
	hdr.disksz		= disksz;
	hdr.cryptmethod		= 0;
	hdr.l1sz		= l1sz;
	hdr.l1off		= l1off;
	hdr.refoff		= refoff;
	hdr.refsz		= refsz;
	hdr.snapcount		= 0;
	hdr.snapsz		= 0;
	hdr.incompatfeatures	= 0;
	hdr.compatfeatures	= 0;
	hdr.autoclearfeatures	= 0;
	hdr.reforder		= 4;
	hdr.headersz		= HDRSZ;

	writehdr(&hdr, fd);
	if(ftruncate(fd, (s64int)initsz + clustersz) == -1)
		sysfatal("ftruncate: %r");

	assert(initsz/clustersz < clustersz/2);

	PUT8(v, initsz);
	if(pwrite(fd, v, sizeof v, refoff) != sizeof v)
		sysfatal("q2create: pwrite: %r");

	for(i=0; i < initsz/clustersz + 1; i++){
		PUT2(v2, 1);
		if(pwrite(fd, v2, sizeof v2, initsz + 2*i) != sizeof v2)
			sysfatal("q2create: pwrite: %r");
	}
}

static void
qc2open(Disk *disk, int fd)
{
	int i;
	Dir *d;
	uchar buf[8];

	disk->fd = fd;
	disk->base = nil;
	disk->l1 = nil;
	readhdr(&disk->h, disk->fd);

	disk->clustersz = 1ull << disk->h.clustershift;
	disk->disksz = disk->h.disksz;
	disk->cryptmethod = disk->h.cryptmethod;
	disk->l1sz = disk->h.l1sz;
	disk->l1off = disk->h.l1off;
	disk->refsz = disk->h.refsz;
	disk->refoff = disk->h.refoff;
	disk->nsnap = disk->h.snapcount;
	disk->snapoff = disk->h.snapsz;

	disk->incompatfeatures = disk->h.incompatfeatures;
	disk->autoclearfeatures = disk->h.autoclearfeatures;
	disk->refssz = disk->h.refsz;
	disk->headersz = disk->h.headersz;

	if(disk->h.reforder != 4)
		sysfatal("unsupoprted refcount size %d", disk->h.reforder);

	disk->l1 = mallocz(disk->l1sz * 8, 1);
	pread(disk->fd, disk->l1, disk->l1sz * 8, disk->l1off);
	for(i = 0; i < disk->l1sz; i++){
		memcpy(buf, disk->l1 + i, sizeof buf);
		disk->l1[i] = GET8(buf);
	}

	d = dirfstat(fd);
	if(d == nil)
		sysfatal("dirfstat: %r");
	disk->end = d->length;
	free(d);
}

static u64int
xlate(Disk *disk, s64int off, int *inplace)
{
	s64int l2sz, l1off, l2tab, l2off, cluster, clusteroff;
	uchar buf[8];

	/*
	 * Clear out inplace flag -- xlate misses should not
	 * be flagged as updatable in place. We will still
	 * return 0 from them, but this leaves less surprises
	 * in the API.
	 */
	if (inplace)
		*inplace = 0;
	rlock(&disk->lock);
	if (off < 0)
		goto err;

	l2sz = disk->clustersz / 8;
	l1off = (off / disk->clustersz) / l2sz;
	if (l1off >= disk->l1sz)
		goto err;

	l2tab = disk->l1[l1off];
	l2tab &= ~QCOW2_INPLACE;
	if (l2tab == 0) {
		runlock(&disk->lock);
		return 0;
	}
	l2off = (off / disk->clustersz) % l2sz;
	pread(disk->fd, buf, sizeof(buf), l2tab + l2off * 8);
	cluster = GET8(buf);
	/*
	 * cluster may be 0, but all future operations don't affect
	 * the return value.
	 */
	if (inplace)
		*inplace = !!(cluster & QCOW2_INPLACE);
	if (cluster & QCOW2_COMPRESSED)
		sysfatal("xlate: compressed clusters unsupported");
	runlock(&disk->lock);
	clusteroff = 0;
	cluster &= ~QCOW2_INPLACE;
	if (cluster)
		clusteroff = off % disk->clustersz;
	return cluster + clusteroff;
err:
	runlock(&disk->lock);
	return -1;
}

static void
inc_refs(Disk *disk, s64int off, int newcluster)
{
	s64int l1off, l1idx, l2idx, l2cluster;
	u64int nper;
	u16int refs;
	uchar buf[8], buf2[2];

	off &= ~QCOW2_INPLACE;
	nper = disk->clustersz / 2;
	l1idx = (off / disk->clustersz) / nper;
	l2idx = (off / disk->clustersz) % nper;
	l1off = disk->refoff + 8 * l1idx;
	if (pread(disk->fd, buf, sizeof(buf), l1off) != 8)
		sysfatal("could not read refs");

	l2cluster = GET8(buf);
	if (l2cluster == 0) {
		l2cluster = disk->end;
		disk->end += disk->clustersz;
		if (ftruncate(disk->fd, disk->end) < 0)
			sysfatal("inc_refs: failed to allocate ref block");
		PUT8(buf, l2cluster);
		if (pwrite(disk->fd, buf, sizeof(buf), l1off) != 8)
			sysfatal("inc_refs: failed to write ref block");
	}

	refs = 1;
	if (!newcluster) {
		if (pread(disk->fd, buf2, sizeof buf2,
		    l2cluster + 2 * l2idx) != 2)
			sysfatal("could not read ref cluster");
		refs = GET2(buf2) + 1;
	}
	PUT2(buf2, refs);
	if (pwrite(disk->fd, buf2, sizeof buf2, l2cluster + 2 * l2idx) != 2)
		sysfatal("inc_refs: could not write ref block");
}

static void
copy_cluster(Disk *disk, Disk *base, u64int dst, u64int src)
{
	char *scratch;

	scratch = malloc(disk->clustersz);
	if(!scratch)
		sysfatal("out of memory");
	src &= ~(disk->clustersz - 1);
	dst &= ~(disk->clustersz - 1);
	if(pread(base->fd, scratch, disk->clustersz, src) == -1)
		sysfatal("copy_cluster: could not read cluster");
	if(pwrite(disk->fd, scratch, disk->clustersz, dst) == -1)
		sysfatal("copy_cluster: could not write cluster");
	free(scratch);
}

/*
 * Allocates a new cluster on disk, creating a new L2 table
 * if needed. The cluster starts off with a refs of one,
 * and the writable bit set.
 *
 * Returns -1 on error, and the physical address within the
 * cluster of the write offset if it exists.
 */
static s64int
mkcluster(Disk *disk, Disk *base, s64int off, s64int src_phys)
{
	s64int l2sz, l1off, l2tab, l2off, cluster, clusteroff, orig;
	uchar buf[8];

	wlock(&disk->lock);

	/* L1 entries always exist */
	l2sz = disk->clustersz / 8;
	l1off = off / (disk->clustersz * l2sz);
	if (l1off >= disk->l1sz)
		sysfatal("l1 offset outside disk");

	disk->end = (disk->end + disk->clustersz - 1) & ~(disk->clustersz - 1);

	l2tab = disk->l1[l1off];
	l2off = (off / disk->clustersz) % l2sz;
	/* We may need to create or clone an L2 entry to map the block */
	if (l2tab == 0 || (l2tab & QCOW2_INPLACE) == 0) {
		orig = l2tab & ~QCOW2_INPLACE;
		l2tab = disk->end;
		disk->end += disk->clustersz;
		if (ftruncate(disk->fd, disk->end) == -1)
			sysfatal("mkcluster: ftruncate failed");

		/*
		 * If we translated, found a L2 entry, but it needed to
		 * be copied, copy it.
		 */
		if (orig != 0)
			copy_cluster(disk, disk, l2tab, orig);
		/* Update l1 -- we flush it later */
		disk->l1[l1off] = l2tab | QCOW2_INPLACE;
		inc_refs(disk, l2tab, 1);
	}
	l2tab &= ~QCOW2_INPLACE;

	/* Grow the disk */
	if (ftruncate(disk->fd, disk->end + disk->clustersz) < 0)
		sysfatal("mkcluster: could not grow disk");
	if (src_phys > 0)
		copy_cluster(disk, base, disk->end, src_phys);
	cluster = disk->end;
	disk->end += disk->clustersz;
	PUT8(buf, cluster | QCOW2_INPLACE);
	if (pwrite(disk->fd, buf, sizeof(buf), l2tab + l2off * 8) != 8)
		sysfatal("mkcluster: could not write cluster");

	PUT8(buf, disk->l1[l1off]);
	if (pwrite(disk->fd, buf, sizeof(buf), disk->l1off + 8 * l1off) != 8)
		sysfatal("mkcluster: could not write l1");
	inc_refs(disk, cluster, 1);

	wunlock(&disk->lock);
	clusteroff = off % disk->clustersz;
	if (cluster + clusteroff < disk->clustersz)
		sysfatal("write would clobber header");
	return cluster + clusteroff;
}

static void
fsread(Req *r)
{
	char *buf;
	Disk *disk, *d;
	s64int off, phys_off, end, cluster_off;
	u64int len, sz, rem;

	off = r->ifcall.offset;
	buf = r->ofcall.data;
	len = r->ifcall.count;
	disk = d = r->fid->file->aux;

	end = off + len;
	if(end > d->disksz)
		len -= end - d->disksz;

	rem = len;
	while(rem != 0){
		phys_off = xlate(d, off, nil);
		if(phys_off == -1){
			responderror(r);
			return;
		}
		cluster_off = off % disk->clustersz;
		sz = disk->clustersz - cluster_off;
		if(sz > rem)
			sz = rem;
		if(phys_off == 0)
			memset(buf, 0, sz);
		else
			sz = pread(d->fd, buf, sz, phys_off);
		off += sz;
		buf += sz;
		rem -= sz;
	}
	r->ofcall.count = len;
	respond(r, nil);
}

static void
fswrite(Req *r)
{
	char *buf;
	Disk *d;
	s64int off, phys_off, end, cluster_off;
	u64int len, sz, rem;
	int inplace;

	off = r->ifcall.offset;
	buf = r->ifcall.data;
	len = r->ifcall.count;
	d = r->fid->file->aux;
	inplace = 1;

	end = off + len;
	if(end > d->disksz){
		respond(r, "end of device");
		return;
	}

	rem = len;
	while(off != end){
		cluster_off = off % d->clustersz;
		sz = d->clustersz - cluster_off;
		if(sz > rem)
			sz = rem;
		phys_off = xlate(d, off, nil);
		if(phys_off == -1){
			respond(r, "xlate error");
			return;
		}

		if(!inplace || phys_off == 0)
			phys_off = mkcluster(d, d, off, phys_off);
		if(phys_off == -1){
			respond(r, "mkcluster error");
			return;
		}
		if(phys_off < d->clustersz)
			sysfatal("fswrite: writing reserved cluster");
		if(pwrite(d->fd, buf, sz, phys_off) != sz){
			respond(r, "phase error");
			return;
		}
		off += sz;
		buf += sz;
		rem -= sz;
	}

	r->ofcall.count = len;
	respond(r, nil);
}

Srv fs = {
.read = fsread,
.write = fswrite,
};

static void
usage(void)
{
	fprint(2, "usage: %s [-s srv] [-m mntpt ] [-n size] file\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	int fd;
	char *uid;
	File *f;
	Disk *d;
	uvlong size;
	int nflag;
	char *mntpt = "/mnt/qcow";
	char *srvname = nil;

	size = 0;
	nflag = 0;
	ARGBEGIN{
	case 'm':
		mntpt = EARGF(usage());
		break;
	case 'n':
		size = strtoull(EARGF(usage()), nil, 0);
		nflag++;
		break;
	case 's':
		srvname = EARGF(usage());
		break;
	default:
		usage();
		break;
	}ARGEND
	if(argc < 1)
		usage();

	if(nflag){
		if((fd = create(argv[0], ORDWR, 0666)) < 0)
			sysfatal("create: %r");
		qc2create(fd, size);
		seek(fd, 0, 0);
	} else if((fd = open(argv[0], ORDWR)) < 0)
			sysfatal("open: %r");

	uid = getuser();
	fs.tree = alloctree(uid, uid, 0755, nil);
	if(fs.tree == nil)
		sysfatal("alloctree: %r");

	f = createfile(fs.tree->root, "data", uid, 0666, nil);
	d = mallocz(sizeof(Disk), 1);
	qc2open(d, fd);
	f->aux = d;
	f->length = d->disksz;
	postmountsrv(&fs, srvname, mntpt, MREPL);
	exits(nil);
}
