#include <u.h>
#include <libc.h>

char *datac[] = {0,"#P/iob","#P/iow",0,"#P/iol",0,0,0,"#P/msr"};

void
main(int argc, char** argv) {
	int fd, size, op;
	ulong port;
	uvlong data;
	uchar datab[8];
	
	data = 0;
	size = 1;
	op = -1;
	ARGBEGIN {
		case 'W': size = 2; break;
		case 'L': size = 4; break;
		case 'M': size = 8; break;
		case 'E': datac[1] = datac[2] = datac[4] = datac[8] = "#P/ec"; break;
		case 'r': op = OREAD; break;
		case 'w': op = OWRITE; break;
		default: sysfatal("bad flag %c", ARGC());
	} ARGEND;
	if(op == -1) sysfatal("no operation selected");
	if(argc < 1) sysfatal("no port selected");
	if(op == OWRITE && argc < 2) sysfatal("no data selected");
	port = strtoul(argv[0], 0, 0);
	if(op == OWRITE) data = strtoull(argv[1], 0, 0);
	
	fd = open(datac[size], op);
	if(fd == -1) sysfatal("open: %r");
	
	if(op == OWRITE) {
		datab[0] = data;
		datab[1] = data >> 8;
		datab[2] = data >> 16;
		datab[3] = data >> 24;
		datab[4] = data >> 32;
		datab[5] = data >> 40;
		datab[6] = data >> 48;
		datab[7] = data >> 56;
		if(pwrite(fd, datab, size, port) != size)
			sysfatal("pwrite: %r");
	}
	else {
		memset(datab, 0, 8);
		if(pread(fd, datab, size, port) != size)
			sysfatal("pread: %r");
		data = datab[0] | (datab[1] << 8) | (datab[2] << 16) |
			(datab[3] << 24) | ((vlong)datab[4] << 32) |
			((vlong)datab[5] << 40) | ((vlong)datab[6] << 48) | 
			((vlong)datab[7] << 56);
		print("0x%ullx\n", data);
	}
	exits(nil);
}
