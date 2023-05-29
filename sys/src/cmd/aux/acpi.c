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
typedef struct FACP FACP;

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

struct FACP {
	int ok;

	uvlong pm1a;
	int pm1aspace;
	int pm1awid;

	uvlong pm1b;
	int pm1bspace;
	int pm1bwid;

	uvlong gpe0;
	ulong gpe0len;
	int gpe0space;
	int gpe0wid;

	uvlong gpe1;
	ulong gpe1len;
	int gpe1space;
	int gpe1wid;

	ulong slpa;
	ulong slpb;
};

enum {
	Tblsz = 4+4+1+1+6+8+4+4+4,

	Temp = 1,
	Battery,
	Pmctl,

	SLP_EN = 0x2000,
	SLP_TM = 0x1c00,
};

static int ec, mem, iofd[5], nbats, ntherms;
static char *uid = "pm", *units[] = {"mW", "mA"};
static Therm therms[16];
static Bat bats[4];
static FACP facp;

static int
enumec(void *dot, void *)
{
	void *p;
	char *id;

	p = amlval(amlwalk(dot, "^_HID"));
	id = amleisaid(p);
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
				if(st.rate > 0 && (st.state & 2) != 0)
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
	char buf[32];
	int ctl;

	snprint(buf, sizeof(buf), "/proc/%d/ctl", getpid());
	if((ctl = open(buf, OWRITE)) < 0)
		return;
	write(ctl, "wired 0", 7);
	close(ctl);
}

static ulong
get32(uchar *p)
{
	return p[3]<<24 | p[2]<<16 | p[1]<<8 | p[0];
}

static uvlong
get64(uchar *p)
{
	return ((uvlong)p[7]<<56) | ((uvlong)p[6]<<48)
			| ((uvlong)p[5]<<40) | ((uvlong)p[4]<<32)
			| ((uvlong)p[3]<<24) | ((uvlong)p[2]<<16)
			| ((uvlong)p[1]<<8) | ((uvlong)p[0]);
}

static void
amlwrite(Amlio *io, int addr, int wid, uvlong v)
{
	uchar b[8];

	b[0] = v; b[1] = v >> 8;
	b[2] = v >> 16; b[3] = v >> 24;
	b[4] = v >> 32; b[5] = v >> 40;
	b[6] = v >> 48; b[7] = v >> 56;

	(*io->write)(io, b, 1<<(wid-1), addr);
}

static int
poweroff(void)
{
	int n;
	void *tts, *pts;
	Amlio ioa, iob;

	if(facp.ok == 0){
		werrstr("no FACP");
		return -1;
	}

	wirecpu0();

	/* The ACPI spec requires we call _TTS and _PTS to prepare
	 * the system to go to _S5 state. If they fail, too bad,
	 * try to go to _S5 state anyway. */
	pts = amlwalk(amlroot, "_PTS");
	if(pts)
		amleval(pts, "i", 5, nil);
	tts = amlwalk(amlroot, "_TTS");
	if(tts)
		amleval(tts, "i", 5, nil);

	/* disable GPEs */
	ioa.space = facp.gpe0space;
	iob.space = facp.gpe1space;
	ioa.off = facp.gpe0;
	iob.off = facp.gpe1;
	amlmapio(&ioa);
	amlmapio(&iob);

	for(n = 0; facp.gpe0 > 0 && n < facp.gpe0len/2; n += facp.gpe0wid){
		amlwrite(&ioa, facp.gpe0len/2 + n, facp.gpe0wid, 0);
		amlwrite(&ioa, n, facp.gpe0wid, ~0);
	}

	for(n = 0; facp.gpe1 > 0 && n < facp.gpe1len/2; n += facp.gpe1wid){
		amlwrite(&iob, facp.gpe1len/2 + n, facp.gpe1wid, 0);
		amlwrite(&iob, n, facp.gpe1wid, ~0);
	}

	ioa.space = facp.pm1aspace;
	iob.space = facp.pm1bspace;
	ioa.off = facp.pm1a;
	iob.off = facp.pm1b;
	amlmapio(&ioa);
	amlmapio(&iob);

	amlwrite(&ioa, 0, facp.pm1awid, ((facp.slpa << 10) & SLP_TM) | SLP_EN);
	amlwrite(&iob, 0, facp.pm1bwid, ((facp.slpb << 10) & SLP_TM) | SLP_EN);
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

	facp.slpa |= facp.slpb;
	amlwrite(&ioa, 0, facp.pm1awid, ((facp.slpa << 10) & SLP_TM) | SLP_EN);
	amlwrite(&iob, 0, facp.pm1bwid, ((facp.slpa << 10) & SLP_TM) | SLP_EN);
	sleep(100);

	werrstr("acpi failed");
	return -1;
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
	fprint(2, "usage: aux/acpi [-DHp] [-m mountpoint] [-s service]\n");
	exits("usage");
}

static Srv fs = {
	.read = fsread,
	.write = fswrite,
};



void
threadmain(int argc, char **argv)
{
	char *mtpt, *srv;
	void *r, **rr;
	int fd, n, l, halt;
	Tbl *t;

	mtpt = "/dev";
	srv = nil;
	halt = 0;
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
	case 'H':
		halt = 1;
		break;
	default:
		usage();
	}ARGEND

	if((ec = open("/dev/ec", ORDWR)) < 0)
		ec = open("#P/ec", ORDWR);
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
			facp.ok = 1;
			if(t->rev >= 3) {
				/* try the ACPI 2.0 method */
				facp.pm1aspace = *(((uchar*)t) + 172);
				facp.pm1awid = *(((uchar*)t) + 175);
				facp.pm1a = get64(((uchar*)t) + 176);

				facp.pm1bspace = *(((uchar*)t) + 184);
				facp.pm1bwid = *(((uchar*)t) + 187);
				facp.pm1b = get64(((uchar*)t) + 188);

				facp.gpe0space = *(((uchar*)t) + 220);
				facp.gpe0wid = *(((uchar*)t) + 223);
				facp.gpe0 = get64(((uchar*)t) + 224);

				facp.gpe1space = *(((uchar*)t) + 232);
				facp.gpe1wid = *(((uchar*)t) + 235);
				facp.gpe1 = get64(((uchar*)t) + 236);
			}

			/* fall back to the ACPI 1.0 io port method */
			if(facp.pm1a == 0 || facp.pm1awid == 0) {
				facp.pm1aspace = IoSpace;
				facp.pm1awid = 2;
				facp.pm1a = get32(((uchar*)t) + 64);
			}

			if(facp.pm1b == 0 || facp.pm1bwid == 0) {
				facp.pm1bspace = IoSpace;
				facp.pm1bwid = 2;
				facp.pm1b = get32(((uchar*)t) + 68);
			}

			if(facp.gpe0 == 0 || facp.gpe0wid == 0) {
				facp.gpe0space = IoSpace;
				facp.gpe0wid = 2;
				facp.gpe0 = get32(((uchar*)t) + 80);
				facp.gpe0len = *(((uchar*)t) + 92);
			}

			if(facp.gpe1 == 0 || facp.gpe1wid == 0) {
				facp.gpe1space = IoSpace;
				facp.gpe1wid = 2;
				facp.gpe1 = get32(((uchar*)t) + 84);
				facp.gpe1len = *(((uchar*)t) + 93);
			}
		}
	}
	if(amleval(amlwalk(amlroot, "_S5"), "", &r) >= 0 && amltag(r) == 'p' && amllen(r) >= 2){
		rr = amlval(r);
		facp.slpa = amlint(rr[0]);
		facp.slpb = amlint(rr[1]);
	}
	close(fd);

	if(halt && poweroff() < 0)
		sysfatal("%r");

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
		io->read = ec >= 0 ? readec : dummy;
		io->write = ec >= 0 ? writeec : dummy;
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
