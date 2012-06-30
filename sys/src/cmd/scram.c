#include <u.h>
#include </386/include/ureg.h>
#include <libc.h>
#include <aml.h>

int fd, iofd;
struct Ureg u;
ulong PM1A_CNT_BLK, PM1B_CNT_BLK, SLP_TYPa, SLP_TYPb;

typedef struct Tbl Tbl;
struct Tbl {
	uchar	sig[4];
	uchar	len[4];
	uchar	rev;
	uchar	csum;
	uchar	oemid[6];
	uchar	oemtid[8];
	uchar	oemrev[4];
	uchar	cid[4];
	uchar	crev[4];
	uchar	data[];
};

void*
amlalloc(int n){
	return mallocz(n, 1);
}

void
amlfree(void *p){
	free(p);
}

static ulong
get32(uchar *p){
	return p[3]<<24 | p[2]<<16 | p[1]<<8 | p[0];
}

void
apm(void)
{
	seek(fd, 0, 0);
	if(write(fd, &u, sizeof u) < 0)
		sysfatal("write: %r");
	seek(fd, 0, 0);
	if(read(fd, &u, sizeof u) < 0)
		sysfatal("read: %r");
	if(u.flags & 1)
		sysfatal("apm: %lux", (u.ax>>8) & 0xFF);
}

int
loadacpi(void)
{
	void *r, **rr;
	Tbl *t;
	int n;
	ulong l;

	amlinit();
	for(;;){
		t = malloc(sizeof(*t));
		if((n = readn(fd, t, sizeof(*t))) <= 0)
			break;
		if(n != sizeof(*t))
			return -1;
		l = get32(t->len);
		if(l < sizeof(*t))
			return -1;
		t = realloc(t, l);
		l -= sizeof(*t);
		if(readn(fd, t->data, l) != l)
			return -1;
		if(memcmp("DSDT", t->sig, 4) == 0)
			amlload(t->data, l);
		else if(memcmp("SSDT", t->sig, 4) == 0)
			amlload(t->data, l);
		else if(memcmp("FACP", t->sig, 4) == 0){
			PM1A_CNT_BLK = get32(((uchar*)t) + 64);
			PM1B_CNT_BLK = get32(((uchar*)t) + 68);
		}
	}
	if(PM1A_CNT_BLK == 0)
		return -1;
	if(amleval(amlwalk(amlroot, "_S5"), "", &r) < 0)
		return -1;
	if(amltag(r) != 'p' || amllen(r) < 2)
		return -1;
	rr = amlval(r);
	SLP_TYPa = (amlint(rr[1]) & 0xFF) << 10;
	SLP_TYPb = ((amlint(rr[1]) >> 8) & 0xFF) << 10;
	return 0;
}

void
outw(long addr, short val)
{
	uchar buf[2];
	
	buf[0] = val;
	buf[1] = val >> 8;
	pwrite(iofd, buf, 2, addr);
}

void
main()
{
	if((fd = open("/dev/apm", ORDWR)) < 0)
		if((fd = open("#P/apm", ORDWR)) < 0)
			goto tryacpi;

	u.ax = 0x530E;
	u.bx = 0x0000;
	u.cx = 0x0102;
	apm();
	u.ax = 0x5307;
	u.bx = 0x0001;
	u.cx = 0x0003;
	apm();
	
tryacpi:
	if((fd = open("/dev/acpitbls", OREAD)) < 0)
		if((fd = open("#P/acpitbls", OREAD)) < 0)
			goto fail;
	if((iofd = open("/dev/iow", OWRITE)) < 0)
		if((iofd = open("#P/iow", OWRITE)) < 0)
			goto fail;
	if(loadacpi() < 0)
		goto fail;
	outw(PM1A_CNT_BLK, SLP_TYPa | 0x2000);
	if(PM1B_CNT_BLK != 0)
		outw(PM1B_CNT_BLK, SLP_TYPb | 0x2000);
fail:
	;
}
