#include <u.h>
#include <libc.h>

const char datac[] = {0,'b','w',0,'l'};

void
main(int argc, char** argv) {
	ulong size, segsize, op, port;
	char* segment;
	ulong data;
	uchar *seg;
	
	data = 0;
	size = 1;
	op = -1;
	ARGBEGIN {
		case 'W': size = 2; break;
		case 'L': size = 4; break;
		case 'r': op = OREAD; break;
		case 'w': op = OWRITE; break;
		default: sysfatal("bad flag %c", ARGC());
	} ARGEND;
	if(op == -1) sysfatal("no operation selected");
	if(argc < 3) sysfatal("no address, no segsize or no segment selected");
	if(op == OWRITE && argc < 4) sysfatal("no data selected");
	segment = argv[0];
	segsize = strtoul(argv[1], 0, 0);
	port = strtoul(argv[2], 0, 0);
	if(op == OWRITE) data = strtoul(argv[3], 0, 0);
	
	seg = segattach(0, segment, 0, segsize);
	if(seg == (uchar*)-1) sysfatal("segattach: %r");
	
	if(op == OWRITE) {
		switch(size) {
			case 1: *(uchar*)(seg+port) = data; break;
			case 2: *(ushort*)(seg+port) = data; break;
			case 4: *(ulong*)(seg+port) = data; break;
		}
	}
	else {
		data = seg[port];
		if(size >= 2) data |= seg[port+1] << 8;
		if(size >= 4) data |= (seg[port+2] << 16) | (seg[port+3] << 24);
		print("0x%ulx\n", data);
	}
	exits(nil);
}
