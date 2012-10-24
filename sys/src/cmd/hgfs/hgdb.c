/* hg debug stuff, just dumps dirstate database right now */

#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

char dothg[MAXPATH];

void
main(int argc, char *argv[])
{
	char buf[MAXPATH];
	uchar hdr[1+4+4+4+4];
	int n, fd;

	ARGBEGIN {
	} ARGEND;

	if(getdothg(dothg, *argv) < 0)
		sysfatal("can't find .hg: %r");

	snprint(buf, sizeof(buf), "%s/dirstate", dothg);
	if((fd = open(buf, OREAD)) < 0)
		sysfatal("can't open dirstate: %r");

	if(seek(fd, 0x28LL, 0) != 0x28LL)
		sysfatal("can't seek dirstate: %r");

	for(;;){
		char state;
		int mode, len;
		vlong size;
		long mtime;
		
		if((n = read(fd, hdr, sizeof(hdr))) == 0)
			break;
		if(n < 0)
			sysfatal("read error: %r");
		if(n < sizeof(hdr))
			sysfatal("dirstate truncated");

		state = hdr[0];
		mode = hdr[4] | hdr[3]<<8 | hdr[2]<<16 | hdr[1]<<24;
		size = hdr[8] | hdr[7]<<8 | hdr[6]<<16 | hdr[5]<<24;
		mtime = hdr[12] | hdr[11]<<8 | hdr[10]<<16 | hdr[9]<<24;
		len = hdr[16] | hdr[15]<<8 | hdr[14]<<16 | hdr[13]<<24;
		USED(mtime);

		if(len >= sizeof(buf))
			sysfatal("invalid name length %d", len);

		n = read(fd, buf, len);
		if(n < 0)
			sysfatal("read error: %r");
		if(n < len)
			sysfatal("dirstate name truncated");
		buf[n] = 0;


		print("%c\t%o\t%lld\t%s\n", state, mode, size, buf);
	}

	exits(0);
}
