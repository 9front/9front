#include <u.h>
#include <libc.h>
#include <ip.h>
#include <auth.h>
#include "ppp.h"

ushort
ptclcsum(Block *bp, int offset, int len)
{
	uchar *addr;
	int blen;

	if(bp == nil || bp->rptr + offset >= bp->wptr)
		return 0;
	addr = bp->rptr + offset;
	blen = BLEN(bp) - offset;
	if(blen < len)
		len = blen;
	return ~ptclbsum(addr, len) & 0xffff;
}

ushort
ipcsum(uchar *addr)
{
	int len;
	ulong sum;

	sum = 0;
	len = (addr[0]&0xf)<<2;

	while(len > 0) {
		sum += (addr[0]<<8) | addr[1] ;
		len -= 2;
		addr += 2;
	}

	sum = (sum & 0xffff) + (sum >> 16);
	sum = (sum & 0xffff) + (sum >> 16);

	return (sum^0xffff);
}
