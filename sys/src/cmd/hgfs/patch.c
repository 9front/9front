#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

int
fcopy(int dfd, int sfd, vlong off, vlong len)
{
	uchar buf[BUFSZ];
	int n;

	while(len != 0){
		n = BUFSZ;
		if(len > 0 && n > len)
			n = len;
		if((n = pread(sfd, buf, n, off)) < 0)
			return -1;
		if(n == 0)
			return len > 0 ? -1 : 0;
		if(write(dfd, buf, n) != n)
			return -1;
		if(off >= 0)
			off += n;
		if(len > 0)
			len -= n;
	}
	return 0;
}

static uchar patchmark[12] = {
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
};

int
fpatchmark(int pfd, char *)
{
	if(write(pfd, patchmark, 12) != 12)
		return -1;
	return 0;
}

typedef struct Frag Frag;
struct Frag
{
	Frag	*next;
	int	fd;
	vlong	len;
	vlong	off;
};

int
fpatch(int ofd, int bfd, int pfd)
{
	vlong off, fstart, fend, start, end, len;
	int err, front, back;
	Frag *h, *f, *p;
	uchar buf[12];

	h = nil;
	err = -1;

	if(bfd >= 0){
		h = malloc(sizeof(Frag));
		if(h == nil)
			goto errout;
		h->next = nil;
		h->off = 0;
		h->fd = bfd;
		h->len = seek(h->fd, 0, 2);
		if(h->len < 0)
			goto errout;
	}

	off = 0;
	while(pfd >= 0){
		if(readn(pfd, buf, 12) != 12)
			break;

		if(memcmp(buf, patchmark, 12) == 0){
			off = 0;
			continue;
		}

		start = buf[0]<<24 | buf[1]<<16 | buf[2]<<8 | buf[3];
		end = buf[4]<<24 | buf[5]<<16 | buf[6]<<8 | buf[7];
		len = buf[8]<<24 | buf[9]<<16 | buf[10]<<8 | buf[11];

		if(start > end){
			werrstr("bad patch: start > end");
			goto errout;
		}

		start += off;
		end += off;
		off += start + len - end;

		fstart = 0;
		for(f = h; f; f = f->next, fstart = fend){
			fend = fstart + f->len;
			if(fend <= start)
				continue;
			if(fstart >= end)
				break;

			front = start > fstart;
			back = end < fend;
			if(front && back){
				p = malloc(sizeof(Frag));
				if(p == nil)
					goto errout;
				*p = *f;
				f->next = p;
				f->len = start - fstart;
				p->off += end - fstart;
				p->len -= end - fstart;
				break;
			} else if(back){
				f->off += end - fstart;
				f->len -= end - fstart;
				break;
			} else if(front){
				f->len = start - fstart;
			} else {
				f->len = 0;
			}
		}

		fstart = 0;
		for(p = nil, f = h; f && fstart < start; p = f, f = f->next)
			fstart += f->len;

		f = malloc(sizeof(Frag));
		if(f == nil)
			goto errout;
		f->fd = pfd;
		f->len = len;
		f->off = seek(f->fd, 0, 1);

		if(p){
			f->next = p->next;
			p->next = f;
		} else {
			f->next = h;
			h = f;
		}

		if(f->off < 0)
			goto errout;
		if(seek(pfd, f->len, 1) < 0)
			goto errout;
	}

	for(f = h; f; f = f->next)
		if(fcopy(ofd, f->fd, f->off, f->len) < 0)
			goto errout;
	err = 0;

errout:
	while(f = h){
		h = f->next;
		free(f);
	}

	return err;
}
