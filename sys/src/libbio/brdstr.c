#include	<u.h>
#include	<libc.h>
#include	<bio.h>

static char*
badd(char *oldp, int *np, char *data, int ndata, int delim, int nulldelim)
{
	int n;
	char *p;

	n = *np;
	p = realloc(oldp, n+ndata+1);
	if(p){
		memmove(p+n, data, ndata);
		n += ndata;
		if(n>0 && nulldelim && p[n-1]==delim)
			p[--n] = '\0';
		else
			p[n] = '\0';
		*np = n;
	}else
		free(oldp);
	return p;
}

char*
Brdstr(Biobufhdr *bp, int delim, int nulldelim)
{
	char *ip, *ep, *p;
	int i, j;

	i = -bp->icount;
	bp->rdline = 0;
	if(i == 0) {
		/*
		 * eof or other error
		 */
		if(bp->state != Bractive) {
			if(bp->state == Bracteof)
				bp->state = Bractive;
			bp->gbuf = bp->ebuf;
			return nil;
		}
	}

	/*
	 * first try in remainder of buffer (gbuf doesn't change)
	 */
	ip = (char*)bp->ebuf - i;
	ep = memchr(ip, delim, i);
	if(ep) {
		j = (ep - ip) + 1;
		bp->icount += j;
		p = badd(nil, &bp->rdline, ip, j, delim, nulldelim);
		goto out;
	}

	/*
	 * copy data to beginning of buffer
	 */
	if(i < bp->bsize)
		memmove(bp->bbuf, ip, i);
	bp->gbuf = bp->bbuf;

	/*
	 * append to buffer looking for the delim
	 */
	p = nil;
	for(;;){
		ip = (char*)bp->bbuf + i;
		while(i < bp->bsize) {
			j = bp->iof(bp, ip, bp->bsize-i);
			if(j < 0)
				Berror(bp, "read error: %r");
			if(j <= 0 && i == 0)
				goto out;
			if(j <= 0 && i > 0){
				/*
				 * end of file but no delim. pretend we got a delim
				 * by making the delim \0 and smashing it with nulldelim.
				 */
				j = 1;
				ep = ip;
				delim = '\0';
				nulldelim = 1;
				*ep = delim;	/* there will be room for this */
			}else{
				bp->offset += j;
				ep = memchr(ip, delim, j);
			}
			i += j;
			if(ep) {
				/*
				 * found in new piece
				 * copy back up and reset everything
				 */
				ip = (char*)bp->ebuf - i;
				if(i < bp->bsize){
					memmove(ip, bp->bbuf, i);
					bp->gbuf = (uchar*)ip;
				}
				j = (ep - (char*)bp->bbuf) + 1;
				bp->icount = j - i;
				p = badd(p, &bp->rdline, ip, j, delim, nulldelim);
				goto out;
			}
			ip += j;
		}
	
		/*
		 * full buffer without finding; add to user string and continue
		 */
		p = badd(p, &bp->rdline, (char*)bp->bbuf, bp->bsize, 0, 0);
		i = 0;
		bp->icount = 0;
		bp->gbuf = bp->ebuf;
	}
out:
	setmalloctag(p, getcallerpc(&bp));
	return p;
}
