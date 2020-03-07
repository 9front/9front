#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <aml.h>

typedef struct Batstat Batstat;
typedef struct Battery Battery;
typedef struct Dfile Dfile;
typedef struct Tbl Tbl;
typedef struct Thermal Thermal;

struct Batstat {
	int rate;
	int capacity;
	int state;
	int voltage;
};

struct Battery {
	char *unit;
	void *bst;
	int fullcharge;
	int capacity;
	int capacitywarn;
	int capacitylow;
	int voltage;
};

struct Dfile {
	Qid qid;
	char *name;
	ulong mode;
	void (*read)(Req*);
	void (*write)(Req*);
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

struct Thermal {
	uint cpus;
	void *tmp;
};

enum {
	Stacksz = 16384,
	Tblsz = 4+4+1+1+6+8+4+4+4,

	Qroot = 0,
	Qbattery,
	Qcputemp,
	Qctl,
};

static void rootread(Req*);
static void ctlread(Req*);
static void ctlwrite(Req*);
static void batteryread(Req*);
static void tmpread(Req*);

int ec, mem, iofd[5], nbats, ntherms, rp, wp;
char *units[] = {"mW", "mA"};
Battery bats[4];
Thermal therms[4];
Channel *creq, *cevent;
Req *rlist, **tailp;

Dfile dfile[] = {
	{{Qroot,0,QTDIR},	"/",		DMDIR|0555,	rootread,		nil},
	{{Qbattery},		"battery",	0444,		batteryread,	nil},
	{{Qcputemp},		"cputemp",	0444,		tmpread,		nil},
	{{Qctl},			"ctl",		0666,		ctlread,		ctlwrite},
};

static int
enumbat(void *dot, void *)
{
	void *p, *r, **rr;
	Battery *b;
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
	if((int)b->capacity < 0) /* even though _STA tells it's there */
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
	int i, n;
	uint cpus;

	cpus = 0;
	if(ntherms < nelem(therms) && amleval(dot, "", &r) >= 0 && amllen(r) > 0 && (rr = amlval(r)) != nil){
		for(i = 0; i < amllen(r); i++){
			snprint(s, sizeof(s), "%N", amlval(rr[i]));
			if((n = strlen(s)) > 0){
				for(n--; n > 3; n--){
					if(s[n-2] == 'C' && s[n-1] == 'P' && s[n] == 'U' && s[n+1] >= '0' && s[n+1] <= '9'){
						cpus |= 1<<atoi(&s[n+1]);
						break;
					}
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
batstat(Battery *b, Batstat *s)
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
batteryread(Req *r)
{
	char buf[nelem(bats)*120], *ep, *p, *state;
	Battery *b;
	Batstat st;
	int n, x, h, m, s;

	p = buf;
	*p = 0;
	ep = buf + sizeof(buf);
	for(n = 0; n < nbats; n++){
		b = &bats[n];

		st.rate = -1;
		st.capacity = -1;
		st.state = 0;
		st.voltage = -1;
		batstat(b, &st);

		h = m = s = 0;
		if(st.state & 4)
			state = "critical";
		else if(st.state & 1)
			state = "discharging";
		else if(st.state & 2)
			state = "charging";
		else
			state = "unknown";
		if(st.rate > 0){
			s = ((st.state & 2) ? bats[n].fullcharge - st.capacity : st.capacity) * 3600 / st.rate;
			h = s/3600;
			s -= 3600*(s/3600);
			m = s/60;
			s -= 60*(s/60);
		}
		x = bats[n].fullcharge > 0 ? st.capacity * 100 / bats[n].fullcharge : -1;
		p += snprint(p, ep-p, "%d %s %d %d %d %d %d %s %d %d %02d:%02d:%02d %s\n",
			x,
			bats[n].unit, st.capacity, b->fullcharge, b->capacity, b->capacitywarn, b->capacitylow,
			"mV", st.voltage, b->voltage,
			h, m, s,
			state
		);
	}
	
	readstr(r, buf);
	respond(r, nil);
}

static void
tmpread(Req *r)
{
	char buf[32], *ep, *p;
	void *er;
	int n, t;

	p = buf;
	ep = buf + sizeof(buf);

	for(n = 0; n < ntherms; n++){
		t = 0;
		if(amleval(therms[n].tmp, "", &er) >= 0)
			t = amlint(er);
			p += snprint(p, ep-p, "%d\n", (t - 2732)/10);
	}

	readstr(r, buf);
	respond(r, nil);
}

static void
ctlread(Req *r)
{
	respond(r, "no.");
}

static void
ctlwrite(Req *r)
{
	respond(r, "no.");
}

void*
emalloc(ulong n)
{
	void *v;

	v = malloc(n);
	if(v == nil)
		sysfatal("out of memory allocating %lud", n);
	memset(v, 0, n);
	setmalloctag(v, getcallerpc(&n));
	return v;
}

char*
estrdup(char *s)
{
	int l;
	char *t;

	if (s == nil)
		return nil;
	l = strlen(s)+1;
	t = emalloc(l);
	memcpy(t, s, l);
	setmalloctag(t, getcallerpc(&s));
	return t;
}

static int
fillstat(uvlong path, Dir *d, int doalloc)
{
	int i;

	for(i=0; i<nelem(dfile); i++)
		if(path == dfile[i].qid.path)
			break;
	if(i == nelem(dfile))
		return -1;

	memset(d, 0, sizeof *d);
	d->uid = doalloc ? estrdup("acpi") : "acpi";
	d->gid = doalloc ? estrdup("acpi") : "acpi";
	d->length = 0;
	d->name = doalloc ? estrdup(dfile[i].name) : dfile[i].name;
	d->mode = dfile[i].mode;
	d->atime = d->mtime = time(0);
	d->qid = dfile[i].qid;
	return 0;
}

static char*
fswalk1(Fid *fid, char *name, Qid *qid)
{
	int i;

	if(strcmp(name, "..") == 0){
		*qid = dfile[0].qid;
		fid->qid = *qid;
		return nil;
	}

	for(i = 1; i < nelem(dfile); i++){	/* i=1: 0 is root dir */
		if(strcmp(dfile[i].name, name) == 0){
			*qid = dfile[i].qid;
			fid->qid = *qid;
			return nil;
		}
	}
	return "file does not exist";
}

static void
fsopen(Req *r)
{
	switch((ulong)r->fid->qid.path){
	case Qroot:
		r->fid->aux = (void*)0;
		respond(r, nil);
		return;

	case Qbattery:
	case Qcputemp:
		if(r->ifcall.mode == OREAD){
			respond(r, nil);
			return;
		}
		break;

	case Qctl:
		if((r->ifcall.mode & ~(OTRUNC|OREAD|OWRITE|ORDWR)) == 0){
			respond(r, nil);
			return;
		}
		break;
	}
	respond(r, "permission denied");
	return;
}

static void
fsstat(Req *r)
{
	fillstat(r->fid->qid.path, &r->d, 1);
	respond(r, nil);
}

static void
fsread(Req *r)
{
	dfile[r->fid->qid.path].read(r);
}

static void
fswrite(Req *r)
{
	dfile[r->fid->qid.path].write(r);
}

static void
rootread(Req *r)
{
	int n;
	uvlong offset;
	char *p, *ep;
	Dir d;

	offset = r->ifcall.offset == 0 ? 0 : (uvlong)r->fid->aux;
	p = r->ofcall.data;
	ep = r->ofcall.data + r->ifcall.count;

	if(offset == 0) /* skip root */
		offset = 1;
	for(; p+2 < ep; p += n){
		if(fillstat(offset, &d, 0) < 0)
			break;
		n = convD2M(&d, (uchar*)p, ep-p);
		if(n <= BIT16SZ)
			break;
		offset++;
	}
	r->fid->aux = (void*)offset;
	r->ofcall.count = p - r->ofcall.data;
	respond(r, nil);
}

static void
fsattach(Req *r)
{
	if(r->ifcall.aname && r->ifcall.aname[0]){
		respond(r, "invalid attach specifier");
		return;
	}
	r->fid->qid = dfile[0].qid;
	r->ofcall.qid = dfile[0].qid;
	respond(r, nil);
}

static void
usage(void)
{
	fprint(2, "usage: aux/acpi [-D] [-d /dev] [-m /mnt/acpi] [-s service]\n");
	exits("usage");
}

static ulong
get32(uchar *p){
	return p[3]<<24 | p[2]<<16 | p[1]<<8 | p[0];
}

Srv fs = {
	.attach = fsattach,
	.walk1 = fswalk1,
	.open = fsopen,
	.read = fsread,
	.write = fswrite,
	.stat = fsstat,
};

void
threadmain(int argc, char **argv)
{
	char *mtpt, *srv;
	Tbl *t;
	int fd, n, l;

	mtpt = "/mnt/acpi";
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
		}else if(memcmp("SSDT", t->sig, 4) == 0)
			amlload(t->data, l);
	}
	close(fd);

	amlenum(amlroot, "_BIF", enumbat, nil);
	amlenum(amlroot, "_PSL", enumtmp, nil);

	threadpostmountsrv(&fs, srv, mtpt, MREPL);
	return;

fail:
	fprint(2, "%r\n");
	amlexit();
	threadexitsall("acpi");
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
