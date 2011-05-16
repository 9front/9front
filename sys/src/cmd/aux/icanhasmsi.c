#include <u.h>
#include <libc.h>

int
pcicfg16r(int fd, ushort *s, vlong offset)
{
	char buf[2];
	
	if(pread(fd, buf, 2, offset) < 2) return -1;
	*s = buf[0] | (buf[1] << 8);
	return 0;
}

void
main()
{
	int fd;
	long n;
	Dir *dir;
	char *p, *s;
	uchar cap, c;
	ushort sh;
	
	fd = open("/dev/pci", OREAD);
	if(fd < 0) sysfatal("open /dev/pci: %r");
	n = dirreadall(fd, &dir);
	if(n < 0) sysfatal("dirreadall /dev/pci: %r");
	close(fd);
	for(; n--; dir++) {
		p = dir->name + strlen(dir->name) - 3;
		if(strcmp(p, "raw") != 0)
			continue;
		s = smprint("/dev/pci/%s", dir->name);
		fd = open(s, OREAD);
		if(fd < 0) {
			fprint(2, "open %s: %r", s);
			free(s);
			continue;
		}
		if(pcicfg16r(fd, &sh, 0) < 0) goto err;
		if(sh == 0xFFFF) goto end;
		if(pcicfg16r(fd, &sh, 0x06) < 0) goto err;
		if((sh & (1<<4)) == 0) goto end;
		cap = 0x33;
		for(;;) {
			if(pread(fd, &cap, 1, cap+1) < 0) goto err;
			if(cap == 0) goto end;
			if(pread(fd, &c, 1, cap) < 0) goto err;
			if(c == 0x05) break;
		}
		s[strlen(s) - 3] = 0;
		print("%s\n", s+9);
		goto end;
	err:
		fprint(2, "read %s: %r", s);
	end:
		free(s);
		close(fd);
	}
}
