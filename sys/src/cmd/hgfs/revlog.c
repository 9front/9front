#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

int
fmktemp(void)
{
	static ulong id = 1;
	char path[MAXPATH];
	snprint(path, sizeof(path), "/tmp/hg%.12d%.8lux", getpid(), id++);
	return create(path, OEXCL|ORDWR|ORCLOSE, 0600);
}

void
revlogupdate(Revlog *r)
{
	uchar buf[64];
	Revmap *m;
	vlong noff;
	int rev;

	if(seek(r->ifd, r->ioff, 0) < 0)
		return;
	for(rev=r->nmap;;rev++){
		if(readn(r->ifd, buf, sizeof(buf)) != sizeof(buf))
			break;
		if((rev % 16) == 0)
			r->map = realloc(r->map, sizeof(r->map[0])*(rev+16));
		m = &r->map[rev];
		memset(m, 0, sizeof(*m));
		m->hoff = (vlong)buf[0]<<40 | 
			(vlong)buf[1]<<32 |
			(vlong)buf[2]<<24 | 
			(vlong)buf[3]<<16 | 
			(vlong)buf[4]<<8 | 
			(vlong)buf[5];
		if(rev == 0)
			m->hoff &= 0xFFFF;
		m->rev = rev;
		m->flags = buf[6]<<8 | buf[7];
		m->hlen = buf[8]<<24 | buf[9]<<16 | buf[10]<<8 | buf[11];
		m->flen = buf[12]<<24 | buf[13]<<16 | buf[14]<<8 | buf[15];
		m->baserev = buf[16]<<24 | buf[17]<<16 | buf[18]<<8 | buf[19];
		m->linkrev = buf[20]<<24 | buf[21]<<16 | buf[22]<<8 | buf[23];
		m->p1rev = buf[24]<<24 | buf[25]<<16 | buf[26]<<8 | buf[27];
		m->p2rev = buf[28]<<24 | buf[29]<<16 | buf[30]<<8 | buf[31];
		memmove(m->hash, buf+32, HASHSZ);

		noff = r->ioff + sizeof(buf);
		if(r->dfd < 0){
			m->hoff = noff;
			noff = seek(r->ifd, m->hoff + m->hlen, 0);
			if(noff < 0)
				break;
		}
		r->ioff = noff;
	}
	r->nmap = rev;
}

int
revlogopen(Revlog *r, char *path, int mode)
{
	r->ifd = -1;
	r->dfd = -1;
	r->tfd = -1;
	r->tid = -1;
	path = smprint("%s.i", path);
	if((r->ifd = open(path, mode)) < 0){
		free(path);
		return -1;
	}
	path[strlen(path)-1] = 'd';
	r->dfd = open(path, mode);

	path[strlen(path)-2] = 0;
	r->path = path;

	r->ioff = 0;
	r->nmap = 0;
	r->map = nil;
	revlogupdate(r);
	return 0;
}

void
revlogclose(Revlog *r)
{
	if(r->ifd >= 0){
		close(r->ifd);
		r->ifd = -1;
	}
	if(r->dfd >= 0){
		close(r->dfd);
		r->dfd = -1;
	}
	if(r->tfd >= 0){
		close(r->tfd);
		r->tfd = -1;
	}
	r->tid = -1;
	free(r->map);
	r->map = nil;
	r->nmap = 0;
	free(r->path);
	r->path = nil;
}

uchar*
revhash(Revlog *r, int rev)
{
	if(rev < 0 || rev >= r->nmap)
		return nullid;
	return r->map[rev].hash;
}

int
hashrev(Revlog *r, uchar hash[])
{
	int rev;

	for(rev=0; rev<r->nmap; rev++)
		if(memcmp(r->map[rev].hash, hash, HASHSZ) == 0)
			return rev;
	return -1;
}

static int
prevlogrev(Revlog *r, int rev)
{
	if(r->map[rev].baserev == rev)
		return -1;
	return rev-1;
}

static int
prevbundlerev(Revlog *r, int rev)
{
	if(r->map[rev].baserev == rev)
		return -1;
	return r->map[rev].p1rev;
}

static Revmap**
getchain1(Revlog *r, int rev, int *count, int (*next)(Revlog *, int))
{
	Revmap **chain;

	if(rev < 0 || rev >= r->nmap){
		if(*count <= 0)
			return nil;
		chain = malloc(sizeof(chain[0]) * ((*count)+1));
		chain[*count] = nil;
		*count = 0;
	}else{
		(*count)++;
		if(chain = getchain1(r, (*next)(r, rev), count, next))
			chain[(*count)++] = &r->map[rev];
	}
	return chain;
}

static Revmap**
getchain(Revlog *r, int rev, int (*next)(Revlog *, int))
{
	int count = 0;
	return getchain1(r, rev, &count, next);
}

int
revlogextract(Revlog *r, int rev, int ofd)
{
	int err, hfd, pfd, bfd;
	uchar hash[HASHSZ];
	Revmap **chain, *m;
	char buf[32];
	vlong off;
	int i;

	err = -1;
	bfd = -1;
	pfd = -1;

	if((chain = getchain(r, rev, prevlogrev)) == nil){
		werrstr("bad patch chain");
		goto errout;
	}

	off = seek(ofd, 0, 1);
	if(off < 0){
		werrstr("seek outfile: %r");
		goto errout;
	}
	hfd = r->dfd < 0 ? r->ifd : r->dfd;
	for(i=0; m = chain[i]; i++){
		if(seek(hfd, m->hoff, 0) < 0){
			werrstr("seek index: %r");
			goto errout;
		}
		if(m == chain[0] && m->baserev == m->rev){
			if(chain[1]){
				if(bfd >= 0 && bfd != ofd)
					close(bfd);
				if((bfd = fmktemp()) < 0){
					werrstr("create basefile: %r");
					goto errout;
				}
			} else
				bfd = ofd;
			if(funzip(bfd, hfd, m->hlen) < 0){
				werrstr("unzip basefile: %r");
				goto errout;
			}
		} else {
			if(pfd < 0){
				if((pfd = fmktemp()) < 0){
					werrstr("create patchfile: %r");
					goto errout;
				}
			}

			/* put a mark before the patch data */
			snprint(buf, sizeof(buf), "%H", m->hash);
			if(fpatchmark(pfd, buf) < 0){
				werrstr("patchmark: %r");
				goto errout;
			}

			if(funzip(pfd, hfd, m->hlen) < 0){
				werrstr("unzip patchfile: %r");
				goto errout;
			}
		}
	}
	m = chain[i-1];

	if(pfd >= 0 && bfd >= 0 && bfd != ofd){
		if(seek(pfd, 0, 0) < 0){
			werrstr("seek patchfile: %r");
			goto errout;
		}
		if(fpatch(ofd, bfd, pfd) < 0){
			werrstr("patch: %r");
			goto errout;
		}
	}

	if(seek(ofd, off, 0) < 0){
		werrstr("seek outfile: %r");
		goto errout;
	}
	if(fhash(ofd, revhash(r, m->p1rev), revhash(r, m->p2rev), hash) < 0){
		werrstr("hash outfile: %r");
		goto errout;
	}
	if(memcmp(m->hash, hash, HASHSZ)){
		werrstr("got bad hash");
		goto errout;
	}
	err = 0;

errout:
	if(pfd >= 0)
		close(pfd);
	if(bfd >= 0 && bfd != ofd)
		close(bfd);
	free(chain);

	return err;
}

int
revlogopentemp(Revlog *r, int rev)
{
	int fd;

	if(r->tfd < 0 || rev != r->tid){
		if((fd = fmktemp()) < 0)
			return -1;
		if(revlogextract(r, rev, fd) < 0){
			close(fd);
			return -1;
		}
		if(r->tfd >= 0)
			close(r->tfd);
		r->tfd = fd;
		r->tid = rev;
	}
	fd = dup(r->tfd, -1);
	if(seek(fd, 0, 0) < 0){
		close(fd);
		return -1;
	}
	return fd;
}

int
fmetaheader(int fd)
{
	static char magic[2] = { 0x01, 0x0A, };
	char buf[4096], *s, *p;
	int o, n;

	o = 0;
	while(o < sizeof(buf)){
		if((n = pread(fd, buf+o, sizeof(buf)-o, o)) <= 0)
			break;
		o += n;
		if(o < sizeof(magic))
			continue;
		if(memcmp(buf, magic, sizeof(magic)) != 0)
			break;
		s = buf + sizeof(magic);
		while((s - buf) <= (o - sizeof(magic))){
			if((p = memchr(s, magic[0], o - (s - buf))) == nil)
				break;
			if(memcmp(p, magic, sizeof(magic)) == 0)
				return (p - buf) + sizeof(magic);
			s = p+1;
		}
	}
	return 0;
}
