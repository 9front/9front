#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <aml.h>

typedef struct Batstat Batstat;
typedef struct Bat Bat;
typedef struct Tbl Tbl;
typedef struct Therm Therm;

struct Batstat {
	int rate;
	int capacity;
	int state;
	int voltage;
};

struct Bat {
	char *unit;
	void *bst;
	int fullcharge;
	int capacity;
	int capacitywarn;
	int capacitylow;
	int voltage;
};

struct Tbl {
	uchar sig[4];
	uchar len[4];
	uchar rev;
	uchar csum;
	uchar oemid[6];
	uchar oemtid[8];
	uchar oemrev[4];
	uchar cid[4];
	uchar crev[4];
	uchar data[];
};

struct Therm {
	uint cpus;
	void *tmp;
};

enum {
	Tblsz = 4+4+1+1+6+8+4+4+4,

	Temp = 1,
	Battery,
	Pmctl,

	SLP_EN = 0x2000,
	SLP_TM = 0x1c00,
};

static ulong PM1a_CNT_BLK, PM1b_CNT_BLK, SLP_TYPa, SLP_TYPb;
static ulong GPE0_BLK, GPE1_BLK, GPE0_BLK_LEN, GPE1_BLK_LEN;
static int ec, mem, iofd[5], nbats, ntherms, facp;
static char *uid = "pm", *units[] = {"mW", "mA"};
static Therm therms[16];
static Bat bats[4];

static char*
eisaid(void *v)
{
	static char id[8];
	ulong b, l;
	int i;

	if(amltag(v) == 's')
		return v;
	b = amlint(v);
	for(l = 0, i = 24; i >= 0; i -= 8, b >>= 8)
		l |= (b & 0xFF) << i;
	id[7] = 0;
	for(i = 6; i >= 3; i--, l >>= 4)
		id[i] = "0123456789ABCDEF"[l & 0xF];
	for(i = 2; i >= 0; i--, l >>= 5)
		id[i] = '@' + (l & 0x1F);
	return id;
}

static int
enumec(void *dot, void *)
{
	void *p;
	char *id;
	id = eisaid(amlval(amlwalk(dot, "^_HID")));
	if(id == nil || strcmp(id, "PNP0C09") != 0)
		return 1;
	p = amlwalk(dot, "^_REG");
	if(p != nil)
		amleval(p, "ii", 0x3, 1, nil);
	return 1;
}

static int
enumbat(void *dot, void *)
{
	void *p, *r, **rr;
	Bat *b;
	int n;

	if(nbats >= nelem(bats))
		return 1;

	if((p = amlwalk(dot, "^_STA")) == nil)
		return 1;
	if(amleval(p, "", &r) < 0 || (amlint(r)&3) != 3)
		return 1;
	if(amleval(dot, "", &r) < 0) /* _BIF */
		return 1;
	if(r == nil || amltag(r) != 'p' || amllen(r) < 7)
		return 1;

	rr = amlval(r);
	b = &bats[nbats];
	if((n = amlint(rr[0])) >= nelem(units) || n < 0)
		b->unit = "??";
	else
		b->unit = units[n];
	b->capacity = amlint(rr[1]);
	if(b->capacity < 0) /* even though _STA tells it's there */
		return 1;
	b->fullcharge = amlint(rr[2]);
	b->voltage = amlint(rr[4]);
	b->capacitywarn = amlint(rr[5]);
	b->capacitylow = amlint(rr[6]);
	b->bst = amlwalk(dot, "^_BST");
	if(b->bst != nil){
		amltake(b->bst);
		nbats++;
	}

	return 1;
}

static int
enumtmp(void *dot, void *)
{
	void *r, **rr;
	char s[64];
	uint cpus;
	int i, n;

	cpus = 0;
	if(ntherms < nelem(therms) && amleval(dot, "", &r) >= 0 && amllen(r) > 0 && (rr = amlval(r)) != nil){
		for(i = 0; i < amllen(r); i++){
			snprint(s, sizeof(s), "%N", amlval(rr[i]));
			for(n = strlen(s)-1; n > 3; n--){
				if(s[n-2] == 'C' && s[n-1] == 'P' && s[n] == 'U' && s[n+1] >= '0' && s[n+1] <= '9'){
					cpus |= 1 << atoi(&s[n+1]);
					break;
				}
			}
		}
	}

	if(cpus != 0 && (dot = amlwalk(dot, "^_TMP")) != nil){
		therms[ntherms].cpus = cpus;
		therms[ntherms].tmp = dot;
		ntherms++;
	}

	return 1;
}

static int
batstat(Bat *b, Batstat *s)
{
	void *r, **rr;

	if(amleval(b->bst, "", &r) < 0)
		return -1;
	if(r == nil || amltag(r) != 'p' || amllen(r) < 4)
		return -1;
	rr = amlval(r);
	s->state = amlint(rr[0]);
	s->rate = amlint(rr[1]);
	s->capacity = amlint(rr[2]);
	s->voltage = amlint(rr[3]);
	return 0;
}

static void
batteryread(char *s, char *e)
{
	int n, x, hh, mm, ss;
	char *state;
	Batstat st;
	Bat *b;

	for(n = 0; n < nbats; n++){
		b = &bats[n];

		state = "unknown";
		ss = x = 0;

		if(batstat(b, &st) == 0){
			if(st.state & 4)
				state = "critical";
			else if(st.state & 1)
				state = "discharging";
			else if(st.state & 2)
				state = "charging";
			if(st.rate > 0 && (st.state & 2) == 0)
				ss = st.capacity * 3600 / st.rate;
			if(bats[n].fullcharge > 0){
				x = st.capacity * 100 / bats[n].fullcharge;
				if(st.state & 2)
					ss = (bats[n].fullcharge - st.capacity) * 3600 / st.rate;
			}
		}else{
			memset(&st, 0, sizeof(st));
		}

		hh = ss / 3600;
		ss -= 3600 * (ss / 3600);
		mm = ss / 60;
		ss -= 60 * (ss / 60);
		s = seprint(s, e, "%d %s %d %d %d %d %d %s %d %d %02d:%02d:%02d %s\n",
			x,
			bats[n].unit, st.capacity, b->fullcharge, b->capacity, b->capacitywarn, b->capacitylow,
			"mV", st.voltage, b->voltage,
			hh, mm, ss,
			state
		);
	}
}

static void
tmpread(char *s, char *e)
{
	void *er;
	int n, t;

	for(n = 0; n < ntherms; n++){
		t = 2732;
		if(amleval(therms[n].tmp, "", &er) >= 0)
			t = amlint(er);
		s = seprint(s, e, "%d.0\n", (t - 2732)/10);
	}
}

static void
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

static void
outw(long addr, ushort val)
{
	uchar buf[2];

	if(addr == 0)
		return;
	buf[0] = val;
	buf[1] = val >> 8;
	pwrite(iofd[2], buf, 2, addr);
}

static void
poweroff(void)
{
	int n;

	if(facp == 0){
		werrstr("no FACP");
		return;
	}

	wirecpu0();

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

	werrstr("acpi failed");
}

static void
pmctlread(char *s, char *e)
{
	USED(s, e);
}

static void
fsread(Req *r)
{
	char msg[512], *s, *e;
	void *aux;

	s = msg;
	e = s + sizeof(msg);
	*s = 0;
	aux = r->fid->file->aux;
	if(r->ifcall.offset == 0){
		if(aux == (void*)Temp)
			tmpread(s, e);
		else if(aux == (void*)Battery)
			batteryread(s, e);
		else if(aux == (void*)Pmctl)
			pmctlread(s, e);
	}

	readstr(r, msg);
	respond(r, nil);
}

static void
fswrite(Req *r)
{
	char msg[256], *f[4];
	void *aux;
	int nf;

	snprint(msg, sizeof(msg), "%.*s",
		utfnlen((char*)r->ifcall.data, r->ifcall.count), (char*)r->ifcall.data);
	nf = tokenize(msg, f, nelem(f));
	aux = r->fid->file->aux;
	if(aux == (void*)Pmctl){
		if(nf == 2 && strcmp(f[0], "power") == 0 && strcmp(f[1], "off") == 0)
			poweroff(); /* should not go any further here */
		else
			werrstr("invalid ctl message");
		responderror(r);
		return;
	}

	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
}

static void
usage(void)
{
	fprint(2, "usage: aux/acpi [-Dp] [-m mountpoint] [-s service]\n");
	exits("usage");
}

static Srv fs = {
	.read = fsread,
	.write = fswrite,
};

static ulong
get32(uchar *p)
{
	return p[3]<<24 | p[2]<<16 | p[1]<<8 | p[0];
}

void
threadmain(int argc, char **argv)
{
	char *mtpt, *srv;
	void *r, **rr;
	int fd, n, l;
	Tbl *t;

	mtpt = "/dev";
	srv = nil;
	ARGBEGIN{
	case 'D':
		chatty9p = 1;
		break;
	case 'm':
		mtpt = EARGF(usage());
		break;
	case 's':
		srv = EARGF(usage());
		break;
	case 'p':
		amldebug++;
		break;
	default:
		usage();
	}ARGEND

	if((ec = open("/dev/ec", ORDWR)) < 0)
		if((ec = open("#P/ec", ORDWR)) < 0)
			goto fail;
	if((mem = open("/dev/acpimem", ORDWR)) < 0)
		mem = open("#P/acpimem", ORDWR);
	if((iofd[1] = open("/dev/iob", ORDWR)) < 0)
		if((iofd[1] = open("#P/iob", ORDWR)) < 0)
			goto fail;
	if((iofd[2] = open("/dev/iow", ORDWR)) < 0)
		if((iofd[2] = open("#P/iow", ORDWR)) < 0)
			goto fail;
	if((iofd[4] = open("/dev/iol", ORDWR)) < 0)
		if((iofd[4] = open("#P/iol", ORDWR)) < 0)
			goto fail;
	if((fd = open("/dev/acpitbls", OREAD)) < 0)
		if((fd = open("#P/acpitbls", OREAD)) < 0)
			goto fail;

	amlinit();
	for(;;){
		t = malloc(sizeof(*t));
		if((n = readn(fd, t, Tblsz)) <= 0)
			break;
		if(n != Tblsz)
			goto fail;
		l = get32(t->len);
		if(l < Tblsz)
			goto fail;
		l -= Tblsz;
		t = realloc(t, sizeof(*t) + l);
		if(readn(fd, t->data, l) != l)
			goto fail;
		if(memcmp("DSDT", t->sig, 4) == 0){
			amlintmask = (~0ULL) >> (t->rev <= 1)*32;
			amlload(t->data, l);
		}else if(memcmp("SSDT", t->sig, 4) == 0){
			amlload(t->data, l);
		}else if(memcmp("FACP", t->sig, 4) == 0){
			facp = 1;
			PM1a_CNT_BLK = get32(((uchar*)t) + 64);
			PM1b_CNT_BLK = get32(((uchar*)t) + 68);
			GPE0_BLK = get32(((uchar*)t) + 80);
			GPE1_BLK = get32(((uchar*)t) + 84);
			GPE0_BLK_LEN = *(((uchar*)t) + 92);
			GPE1_BLK_LEN = *(((uchar*)t) + 93);
		}
	}
	if(amleval(amlwalk(amlroot, "_S5"), "", &r) >= 0 && amltag(r) == 'p' && amllen(r) >= 2){
		rr = amlval(r);
		SLP_TYPa = amlint(rr[0]);
		SLP_TYPb = amlint(rr[1]);
	}
	close(fd);

	amlenum(amlroot, "_HID", enumec, nil);
	amlenum(amlroot, "_BIF", enumbat, nil);
	amlenum(amlroot, "_PSL", enumtmp, nil);

	fs.tree = alloctree(uid, uid, DMDIR|0555, nil);
	if(nbats > 0)
		createfile(fs.tree->root, "battery", uid, 0444, (void*)Battery);
	if(ntherms > 0)
		createfile(fs.tree->root, "cputemp", uid, 0444, (void*)Temp);
	createfile(fs.tree->root, "pmctl", uid, 0666, (void*)Pmctl);

	threadpostmountsrv(&fs, srv, mtpt, MAFTER);
	threadexits(nil);

fail:
	sysfatal("%r");
}

static int
readec(Amlio *, void *data, int len, int off)
{
	return pread(ec, data, len, off);
}

static int
writeec(Amlio *, void *data, int len, int off)
{
	return pwrite(ec, data, len, off);
}

static int
readio(Amlio *io, void *data, int len, int port)
{
	assert(len == 1 || len == 2 || len == 4);
	return pread(iofd[len], data, len, io->off+port);
}

static int
writeio(Amlio *io, void *data, int len, int port)
{
	assert(len == 1 || len == 2 || len == 4);
	return pwrite(iofd[len], data, len, io->off+port);
}

static int
memread(Amlio *io, void *data, int len, int addr)
{
	return pread(mem, data, len, io->off+addr);
}

static int
memwrite(Amlio *io, void *data, int len, int addr)
{
	return pwrite(mem, data, len, io->off+addr);
}

static int
dummy(Amlio *, void *, int len, int)
{
	return len;
}

int
amlmapio(Amlio *io)
{
	switch(io->space){
	case EbctlSpace:
		io->read = readec;
		io->write = writeec;
		break;
	case IoSpace:
		io->read = readio;
		io->write = writeio;
		break;
	case MemSpace:
		io->read = mem >= 0 ? memread : dummy;
		io->write = mem >= 0 ? memwrite : dummy;
		break;
	default:
		io->read = dummy;
		io->write = dummy;
		break;
	}
	return 0;
}

void
amlunmapio(Amlio *)
{
}
