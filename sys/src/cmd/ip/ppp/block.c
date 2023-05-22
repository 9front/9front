#include <u.h>
#include <libc.h>
#include <ip.h>
#include <auth.h>
#include "ppp.h"

enum
{
	PAD	= 128,
};

Block*
resetb(Block *bp)
{
	bp->rptr = bp->base+PAD;
	bp->wptr = bp->rptr;
	return bp;
}

Block*
allocb(int len)
{
	Block *bp;

	len += PAD;
	bp = mallocz(sizeof(Block)+len, 0);
	bp->base = (uchar*)bp+sizeof(Block);
	bp->lim = bp->base+len;
	return resetb(bp);
}

void
freeb(Block *bp)
{
	if(bp == nil)
		return;
	bp->rptr = (void*)0xdeadbabe;
	bp->wptr = (void*)0xdeadbabe;
	if(bp->base == (uchar*)bp + sizeof(Block))
		free(bp);
}
