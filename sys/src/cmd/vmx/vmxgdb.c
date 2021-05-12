#include <u.h>
#include <libc.h>
#include <bio.h>

char *vmxroot = "/n/vmx";

Biobuf *bin, *bout;
int regsfd, memfd;
int noack;

void *
emalloc(ulong sz)
{
	void *v;
	
	v = malloc(sz);
	if(v == nil)
		sysfatal("malloc: %r");
	memset(v, 0, sz);
	setmalloctag(v, getcallerpc(&sz));
	return v;
}

int
eBgetc(Biobuf *bp)
{
	int c;
	
	c = Bgetc(bp);
	if(c < 0) sysfatal("Bgetc: %r");
	return c;
}

char *
rpack(void)
{
	int c;
	char *pkt;
	ulong npkt;
	u8int csum, csum2;
	char buf[3], *p;

	while(eBgetc(bin) != '$')
		;
	if(0){
repeat:
		free(pkt);
	}
	pkt = nil;
	npkt = 0;
	csum = 0;
	while(c = eBgetc(bin)){
		if(c == '#') break;
		if(c == '$') goto repeat;
		csum += c;
		if(c == '}'){
			c = eBgetc(bin);
			if(c == '#') break;
			if(c == '$') goto repeat;
			csum += c;
			c ^= 0x20;
		}
		if(npkt % 64 == 0)
			pkt = realloc(pkt, npkt + 64);
		pkt[npkt++] = c;
	}
	if(npkt % 64 == 0)
		pkt = realloc(pkt, npkt + 1);
	pkt[npkt] = 0;
	buf[0] = eBgetc(bin);
	if(buf[0] == '$') goto repeat;
	buf[1] = eBgetc(bin);
	if(buf[1] == '$') goto repeat;
	if(noack) return pkt;
	buf[2] = 0;
	csum2 = strtol(buf, &p, 16);
	if(p != &buf[2] || csum != csum2){
		Bputc(bout, '-');
		goto repeat;
	}
	Bputc(bout, '+');
	return pkt;
}

int
bflush(Biobufhdr *, void *v, long n)
{
	Bflush(bout);
	return read(bin->fid, v, n);
}

void
wpack(char *p0)
{
	u8int csum;
	char *p;

	fprint(2, "-> %s\n", p0);
again:
	p = p0;
	csum = 0;
	Bputc(bout, '$');
	for(; *p != 0; p++)
		switch(*p){
		case '$': case '#': case '{': case '*':
			Bputc(bout, '{');
			Bputc(bout, *p ^ 0x20);
			csum += '{' + (*p ^ 0x20);
			break;
		default:
			Bputc(bout, *p);
			csum += *p;
		}
	Bprint(bout, "#%.2uX", csum);
	if(noack) return;
	for(;;)
		switch(eBgetc(bin)){
		case '+': return;
		case '-': goto again;
		case '$': Bungetc(bin); return;
		}
}

static char *regname[] = {
	"ax", "cx", "dx", "bx",
	"sp", "bp", "si", "di",
	"pc", "flags", "cs", "ss",
	"ds", "es", "fs", "gs",
};

char *
regpacket(void)
{
	char *buf;
	char rbuf[4096];
	int rc;
	char *p, *q, *f[2];
	int pos, i, l;
	uvlong v;
	char tbuf[3];
	
	l = 4 * nelem(regname);
	buf = emalloc(2 * l + 1);
	memset(buf, 'x', 2 * l);
	rc = pread(regsfd, rbuf, sizeof(rbuf)-1, 0);
	if(rc < 0){
		free(buf);
		return strdup("");
	}
	rbuf[rc] = 0;
	for(p = rbuf; (q = strchr(p, '\n')) != nil; p = q + 1){
		*q = 0;
		if(tokenize(p, f, nelem(f)) < 2) continue;
		v = strtoull(f[1], nil, 0);
		pos = 0;
		for(i = 0; i < nelem(regname); i++){
			if(strcmp(f[0], regname[i]) == 0)
				break;
			pos += 4;
		}
		if(i == nelem(regname)) continue;
		if(f[0][1] == 's' && f[0][2] == 0) v = 0;
		l = 4;
		while(l--){
			sprint(tbuf, "%.2ux", (u8int)v);
			((u16int*)buf)[pos++] = *(u16int*)tbuf;
			v >>= 8;
		}
	}
	return buf;
}

char *
memread(char *p)
{
	char *q;
	uvlong addr, count;
	char *buf;
	int rc, i;
	char tbuf[3];
	
	addr = strtoull(p, &q, 16);

	/* avoid negative file offset */
	addr <<= 1;
	addr >>= 1;

	if(p == q || *q != ',') return strdup("E99");
	count = strtoull(q + 1, &p, 16);
	if(q+1 == p || *p != 0) return strdup("E99");
	if(count > 65536) count = 65536;
	buf = emalloc(2*count+4);
	rc = pread(memfd, buf, count, addr);
	if(rc <= 0) return strcpy(buf, "E01");
	for(i = rc; --i >= 0; ){
		sprint(tbuf, "%.2ux", (uchar)buf[i]);
		((u16int*)buf)[i] = *(u16int*)tbuf;
	}
	return buf;
}

void
main(int, char **)
{
	char *p, *msg;

	bin = Bfdopen(0, OREAD);
	if(bin == nil) sysfatal("Bfdopen: %r");
	bout = Bfdopen(1, OWRITE);
	if(bout == nil) sysfatal("Bfdpen: %r");
	Biofn(bin, bflush);
	
	p = smprint("%s/mem", vmxroot);
	memfd = open(p, OREAD);
	free(p);
	if(memfd < 0) sysfatal("open: %r");

	p = smprint("%s/xregs", vmxroot);
	regsfd = open(p, OREAD);
	free(p);
	if(regsfd < 0) sysfatal("open: %r");
	
	for(;;){
		msg = rpack();
		fprint(2, "<- %s\n", msg);
	reinterpret:
		switch(*msg){
		case 'g':
			p = regpacket();
			wpack(p);
			free(p);
			break;
		case '?':
			wpack("S00");
			break;
		case 'm':
			p = memread(msg+1);
			wpack(p);
			free(p);
			break;
		case 'q':
			if(strncmp(msg, "qSupported", 10) == 0 && (msg[10] == ':' || msg[10] == 0)){
				wpack("PacketSize=4096;QStartNoAckMode+");
			}else
				goto no;
			break;
		case 'Q':
			if(strcmp(msg, "QStartNoAckMode") == 0){
				wpack("OK");
				noack = 1;
			}
			break;
		case 'H':
			msg[0] = msg[1];
			msg[1] = 0;
			goto reinterpret;
		default: no: wpack(""); break;
		}
		free(msg);
	}
	
}
