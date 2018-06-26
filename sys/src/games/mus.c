#include <u.h>
#include <libc.h>

typedef struct Trk Trk;
struct Trk{
	u32int len;
	uchar *dat;
	uchar *p;
	uchar *end;
	uchar v[16];
	int done;
};
Trk t;
uchar *mcmd, *mp, *me;
int fd;

#define	PBIT16(p,v)	(p)[0]=(v);(p)[1]=(v)>>8
#define BBIT32(p,v)	(p)[3]=(v);(p)[2]=(v)>>8;(p)[1]=(v)>>16;(p)[0]=(v)>>24

void
eread(int fd, void *u, long n)
{
	if(readn(fd, u, n) != n)
		sysfatal("readn: %r");
}

uchar
r8(void)
{
	return *t.p++;
}

void
delay(void)
{
	uchar v;

	do{
		v = r8();
		*mp++ = v;
	}while(v & 0x80);
}

void
putcmd(uchar *cmd, int n)
{
	if(mp + n >= me){
		me += 8192;
		mcmd = realloc(mcmd, me - mcmd);
		if(mcmd == nil)
			sysfatal("realloc: %r");
	}
	memcpy(mp, cmd, n);
	mp += n;
}

void
ev(void)
{
	uchar e, v, cmd[3], *p;

	e = r8();
	p = cmd;
	switch(e >> 4 & 7){
	case 0:
		v = r8() & 0x7f;
		*p++ = e | 0x80;
		*p++ = v;
		*p++ = 0x40;
		break;
	case 1:
		v = r8();
		*p++ = e | 0x80;
		*p++ = v & 0x7f;
		if(v & 0x80)
			t.v[e & 15] = r8() & 0x7f;
		*p++ = t.v[e & 15];
		break;
	case 2:
		v = r8();
		*p++ = e | 0xc0;
		PBIT16(p, v << 7 & 0x7f7f);
		p += 2;
		break;
	case 3:
		v = r8();
		*p++ = 0xb | e & 15;
		switch(v){
		case 10: *p++ = 0x78; break;
		case 11: *p++ = 0x7b; break;
		case 12: *p++ = 0x7e; break;
		case 13: *p++ = 0x7f; break;
		case 14: *p++ = 0x79; break;
		default: sysfatal("unknown system event %ux\n", v);
		}
		*p++ = 0;
		break;
	case 4:
		v = r8();
		if(v > 9)
			sysfatal("unknown controller %ux\n", v);
		*p++ = 0xb0 | e & 15;
		switch(v){
		case 1: *p++ = 0x00; break;
		case 2: *p++ = 0x01; break;
		case 3: *p++ = 0x07; break;
		case 4: *p++ = 0x0a; break;
		case 5: *p++ = 0x0b; break;
		case 6: *p++ = 0x5b; break;
		case 7: *p++ = 0x5d; break;
		case 8: *p++ = 0x40; break;
		case 9: *p++ = 0x43; break;
		}
		*p++ = r8() & 0x7f;
		if(v == 0)
			cmd[0] += 0x10;
		break;
	case 6:
		*p++ = 0xff;
		*p++ = 0x2f;
		e = 0;
		t.done++;
		break;
	default:
		sysfatal("unknown event %ux\n", e >> 4 & 7);
	}
	if((e & 15) == 9)
		cmd[0] |= 6;
	if((e & 15) == 15)
		cmd[0] &= ~6;
	putcmd(cmd, p-cmd);
	if(e & 0x80)
		delay();
	else
		*mp++ = 0;
}

void
reset(void)
{
	memset(t.v, 0x7f, sizeof t.v);
	mcmd = mallocz(t.len * 2, 1);
	if(mcmd == nil)
		sysfatal("mallocz: %r");
	mp = mcmd;
	me = mcmd + t.len * 2;
}

void
barf(void)
{
	static uchar hdr[] = {
		'M', 'T', 'h', 'd',
		0x00, 0x00, 0x00, 0x06,
		0x00, 0x00,
		0x00, 0x01,
		0x01, 0x01,
		'M', 'T', 'r', 'k',
		0x00, 0x00, 0x00, 0x00,
		0x00, 0xb0, 0x07, 0x7f,
		0x00, 0xb1, 0x07, 0x7f,
		0x00, 0xb2, 0x07, 0x7f,
		0x00, 0xb3, 0x07, 0x7f,
		0x00, 0xb4, 0x07, 0x7f,
		0x00, 0xb5, 0x07, 0x7f,
		0x00, 0xb6, 0x07, 0x7f,
		0x00, 0xb7, 0x07, 0x7f,
		0x00, 0xb8, 0x07, 0x7f,
		0x00, 0xb9, 0x07, 0x7f,
		0x00, 0xba, 0x07, 0x7f,
		0x00, 0xbb, 0x07, 0x7f,
		0x00, 0xbc, 0x07, 0x7f,
		0x00, 0xbd, 0x07, 0x7f,
		0x00, 0xbe, 0x07, 0x7f,
		0x00, 0xbf, 0x07, 0x7f,
		0x00, 0xff, 0x51, 0x03, 0x1b, 0x8a, 0x06,
		0x00
	};
	int n;

	n = sizeof(hdr) - 22 + mp - mcmd;
	BBIT32(hdr + 18, n);
	write(1, hdr, sizeof hdr);
	write(1, mcmd, mp - mcmd);
}

void
main(int argc, char *argv[])
{
	int n, ofs;
	uchar s[8], b[1024];

	if(argc > 1){
		fd = open(argv[1], OREAD);
		if(fd < 0)
			sysfatal("open: %r");
	}
	eread(fd, s, sizeof s);
	if(memcmp(s, "MUS\x1a", 4) != 0)
		sysfatal("invalid mus file: %r");
	t.len = s[5] << 8 | s[4];
	ofs = (s[7] << 8 | s[6]) - 8;
	while(ofs > 0){
		n = ofs > sizeof b ? sizeof b : ofs;
		eread(fd, b, n);
		ofs -= n;
	}
	t.dat = malloc(t.len);
	if(t.dat == nil)
		sysfatal("malloc: %r");
	t.p = t.dat;
	t.end = t.dat + t.len;
	eread(fd, t.dat, t.len);
	reset();
	while(!t.done && t.p < t.end)
		ev();
	barf();
	exits(nil);
}
