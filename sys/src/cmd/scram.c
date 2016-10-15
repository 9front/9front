#include <u.h>
#include </386/include/ureg.h>
#include <libc.h>
#include <aml.h>

int fd, iofd;
struct Ureg u;
ulong PM1a_CNT_BLK, PM1b_CNT_BLK, SLP_TYPa, SLP_TYPb;
ulong GPE0_BLK, GPE1_BLK, GPE0_BLK_LEN, GPE1_BLK_LEN;
enum {
	SLP_EN = 0x2000,
	SLP_TM = 0x1c00,
};

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

enum {
	Tblsz	= 4+4+1+1+6+8+4+4+4,
};

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
	ulong l;
	Tbl *t;
	int n;

	amlinit();
	for(;;){
		t = malloc(sizeof(*t));
		if((n = readn(fd, t, Tblsz)) <= 0)
			break;
		if(n != Tblsz)
			return -1;
		l = get32(t->len);
		if(l < Tblsz)
			return -1;
		l -= Tblsz;
		t = realloc(t, sizeof(*t) + l);
		if(readn(fd, t->data, l) != l)
			return -1;
		if(memcmp("DSDT", t->sig, 4) == 0){
			amlintmask = (~0ULL) >> (t->rev <= 1)*32;
			amlload(t->data, l);
		}
		else if(memcmp("SSDT", t->sig, 4) == 0)
			amlload(t->data, l);
		else if(memcmp("FACP", t->sig, 4) == 0){
			PM1a_CNT_BLK = get32(((uchar*)t) + 64);
			PM1b_CNT_BLK = get32(((uchar*)t) + 68);
			GPE0_BLK = get32(((uchar*)t) + 80);
			GPE1_BLK = get32(((uchar*)t) + 84);
			GPE0_BLK_LEN = *(((uchar*)t) + 92);
			GPE1_BLK_LEN = *(((uchar*)t) + 93);
		}
	}
	if(amleval(amlwalk(amlroot, "_S5"), "", &r) < 0)
		return -1;
	if(amltag(r) != 'p' || amllen(r) < 2)
		return -1;
	rr = amlval(r);
	SLP_TYPa = amlint(rr[0]);
	SLP_TYPb = amlint(rr[1]);
	return 0;
}

void
outw(long addr, ushort val)
{
	uchar buf[2];

	if(addr == 0)
		return;
	buf[0] = val;
	buf[1] = val >> 8;
	pwrite(iofd, buf, 2, addr);
}

void
wirecpu0(void)
{
	char buf[128];
	int ctl;

	snprint(buf, sizeof(buf), "/proc/%d/ctl", getpid());
	if((ctl = open(buf, OWRITE)) < 0){
		snprint(buf, sizeof(buf), "#p/%d/ctl", getpid());
		if((ctl = open(buf, OWRITE)) < 0)
			return;
	}
	write(ctl, "wired 0", 7);
	close(ctl);
}

void
main()
{
	int n;

	wirecpu0();

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

	/* disable GPEs */
	for(n = 0; GPE0_BLK > 0 && n < GPE0_BLK_LEN/2; n += 2){
		outw(GPE0_BLK + GPE0_BLK_LEN/2 + n, 0); /* EN */
		outw(GPE0_BLK + n, 0xffff); /* STS */
	}
	for(n = 0; GPE1_BLK > 0 && n < GPE1_BLK_LEN/2; n += 2){
		outw(GPE1_BLK + GPE1_BLK_LEN/2 + n, 0); /* EN */
		outw(GPE1_BLK + n, 0xffff); /* STS */
	}

	outw(PM1a_CNT_BLK, ((SLP_TYPa << 10) & SLP_TM) | SLP_EN);
	outw(PM1b_CNT_BLK, ((SLP_TYPb << 10) & SLP_TM) | SLP_EN);
	sleep(100);

	/*
	 * The SetSystemSleeping() example from the ACPI spec 
	 * writes the same value in both registers. But Linux/BSD
	 * write distinct values from the _Sx package (like the
	 * code above). The _S5 package on a HP DC5700 is
	 * Package(0x2){0x0, 0x7} and writing SLP_TYPa of 0 to
	 * PM1a_CNT_BLK seems to have no effect but 0x7 seems
	 * to work fine. So trying the following as a last effort.
	 */
	SLP_TYPa |= SLP_TYPb;
	outw(PM1a_CNT_BLK, ((SLP_TYPa << 10) & SLP_TM) | SLP_EN);
	outw(PM1b_CNT_BLK, ((SLP_TYPa << 10) & SLP_TM) | SLP_EN);
	sleep(100);

fail:
	exits("scram");
}
