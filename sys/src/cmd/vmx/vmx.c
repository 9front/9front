#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

char *segname;
int segrclose;
Region *mmap;
int ctlfd, regsfd, mapfd, waitfd;
Channel *waitch, *sleepch, *notifch;
enum { MSEC = 1000*1000, MinSleep = MSEC, SleeperPoll = 2000*MSEC } ;
int getexit, state;
typedef struct VmxNotif VmxNotif;
struct VmxNotif {
	void (*f)(void *);
	void *arg;
};

int mainstacksize = 65536;

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

void
vmerror(char *fmt, ...)
{
	Fmt f;
	char buf[256];
	va_list arg;
	
	fmtfdinit(&f, 2, buf, sizeof buf);
	va_start(arg, fmt);
	fmtvprint(&f, fmt, arg);
	va_end(arg);
	fmtprint(&f, "\n");
	fmtfdflush(&f);
}

int
ctl(char *fmt, ...)
{
	va_list va;
	int rc;
	
	va_start(va, fmt);
	rc = vfprint(ctlfd, fmt, va);
	va_end(va);
	return rc;
}

void
modregion(Region *r)
{
	if(r->segname == nil){
		if(fprint(mapfd, "--- wb %#ullx %#ullx\n", (uvlong)r->start, (uvlong)r->end) < 0)
			vmerror("updating memory map: %r");
	}else
		if(fprint(mapfd, "%c%c%c wb %#ullx %#ullx %s %#ullx\n",
			(r->type & REGR) != 0 ? 'r' : '-',
			(r->type & REGW) != 0 ? 'w' : '-',
			(r->type & REGX) != 0 ? 'x' : '-',
			(uvlong)r->start, (uvlong)r->end, r->segname, (uvlong)r->segoff) < 0)
			vmerror("updating memory map: %r");
}

static void
vmxsetup(void)
{
	static char buf[128];
	static char name[128];
	int rc;
	
	ctlfd = open("#X/clone", ORDWR|ORCLOSE);
	if(ctlfd < 0) sysfatal("open: %r");
	rc = read(ctlfd, name, sizeof(name) - 1);
	if(rc < 0) sysfatal("read: %r");
	name[rc] = 0;
	if(segname == nil){
		segname = smprint("vm.%s", name);
		segrclose = ORCLOSE;
	}
	
	snprint(buf, sizeof(buf), "#X/%s/regs", name);
	regsfd = open(buf, ORDWR);
	if(regsfd < 0) sysfatal("open: %r");
	
	snprint(buf, sizeof(buf), "#X/%s/map", name);
	mapfd = open(buf, OWRITE|OTRUNC);
	if(mapfd < 0) sysfatal("open: %r");
	
	snprint(buf, sizeof(buf), "#X/%s/wait", name);
	waitfd = open(buf, OREAD);
	if(waitfd < 0) sysfatal("open: %r");
}

enum { RCENT = 256 };
char *rcname[RCENT];
uvlong rcval[RCENT];
uvlong rcvalid[(RCENT+63)/64], rcdirty[(RCENT+63)/64];

static int
rclookup(char *n)
{
	int i;
	
	for(i = 0; i < RCENT; i++)
		if(rcname[i] != nil && strcmp(n, rcname[i]) == 0)
			return i;
	return -1;
}

char *
rcflush(int togo)
{
	int i, j;
	static char buf[4096];
	char *p, *e;
	uvlong v;
	
	p = buf;
	e = buf + sizeof(buf);
	*p = 0;
	for(i = 0; i < (RCENT+63)/64; i++){
		if(v = rcdirty[i], v != 0){
			for(j = 0; j < 64; j++)
				if((v>>j & 1) != 0)
					p = seprint(p, e, "%s%c%#ullx%c", rcname[i*64+j], togo?'=':' ', rcval[i*64+j], togo?';':'\n');
			rcdirty[i] = 0;
		}
		rcvalid[i] = 0;
	}
	if(!togo && p != buf && write(regsfd, buf, p - buf) < p - buf)
		sysfatal("rcflush: write: %r");
	return p != buf ? buf : nil;
}

static void
rcload(void)
{
	char buf[4096];
	char *p, *q, *f[2];
	int nf;
	int i, rc;

	rcflush(0);
	rc = pread(regsfd, buf, sizeof(buf) - 1, 0);
	if(rc < 0) sysfatal("rcload: pread: %r");
	buf[rc] = 0;
	p = buf;
	for(i = 0; i < nelem(rcname); i++){
		q = strchr(p, '\n');
		if(q == nil) break;
		*q = 0;
		nf = tokenize(p, f, nelem(f));
		p = q + 1;
		if(nf < 2) break;
		free(rcname[i]);
		rcname[i] = strdup(f[0]);
		rcval[i] = strtoull(f[1], nil, 0);
		rcvalid[i>>6] |= 1ULL<<(i&63);
	}
	for(; i < nelem(rcname); i++){
		free(rcname[i]);
		rcname[i] = 0;
		rcvalid[i>>6] &= ~(1ULL<<(i&63));
	}
}

uvlong
rget(char *reg)
{
	int i;

	i = rclookup(reg);
	if(i < 0 || (rcvalid[i>>6]>>i&1) == 0){
		rcload();
		i = rclookup(reg);
		if(i < 0) sysfatal("unknown register %s", reg);
	}
	return rcval[i];
}

void
rpoke(char *reg, uvlong val, int clean)
{
	int i;

	i = rclookup(reg);
	if(i >= 0){
		if((rcvalid[i>>6]>>(i&63)&1) != 0 && rcval[i] == val) return;
		goto goti;
	}
	for(i = 0; i < nelem(rcname); i++)
		if(rcname[i] == nil){
			rcname[i] = strdup(reg);
			break;
		}
	assert(i < nelem(rcname));
goti:
	rcval[i] = val;
	rcvalid[i>>6] |= 1ULL<<(i&63);
	if(!clean)
		rcdirty[i>>6] |= 1ULL<<(i&63);
}

uvlong
rgetsz(char *reg, int sz)
{
	switch(sz){
	case 1: return (u8int)rget(reg);
	case 2: return (u16int)rget(reg);
	case 4: return (u32int)rget(reg);
	case 8: return rget(reg);
	default:
		vmerror("invalid size operand for rgetsz");
		assert(0);
		return 0;
	}
}

void
rsetsz(char *reg, uvlong val, int sz)
{
	switch(sz){
	case 1: rset(reg, (u8int)val | rget(reg) & ~0xffULL); break;
	case 2: rset(reg, (u16int)val | rget(reg) & ~0xffffULL); break;
	case 4: rset(reg, (u32int)val); break;
	case 8: rset(reg, val); break;
	default:
		vmerror("invalid size operand for rsetsz");
		assert(0);
	}
}

Region *
mkregion(u64int pa, u64int end, int type)
{
	Region *r, *s, **rp;

	r = emalloc(sizeof(Region));
	if(end < pa) sysfatal("end of region %p before start of region %#p", (void*)end, (void*)pa);
	if((pa & BY2PG-1) != 0 || (end & BY2PG-1) != 0) sysfatal("address %#p not page aligned", (void*)pa);
	r->start = pa;
	r->end = end;
	r->type = type;
	for(s = mmap; s != nil; s = s->next)
		if(!(pa < s->start && end < s->end || pa >= s->start && pa >= s->end))
			sysfatal("region %#p-%#p overlaps region %#p-%#p", (void*)pa, (void*)end, (void*)s->start, (void*)s->end);
	for(rp = &mmap; (*rp) != nil && (*rp)->start < end; rp = &(*rp)->next)
		;
	r->next = *rp;
	*rp = r;
	return r;
}

Region *
regptr(u64int addr)
{
	Region *r;

	for(r = mmap; r != nil; r = r->next)
		if(addr >= r->start && addr < r->end)
			return r;
	return nil;
}

void *
gptr(u64int addr, u64int len)
{
	Region *r;

	if(addr + len < addr)
		return nil;
	for(r = mmap; r != nil; r = r->next)
		if(addr >= r->start && addr < r->end){
			if(addr + len > r->end)
				return nil;
			return (uchar *) r->v + (addr - r->start);
		}
	return nil;
}

uintptr
gpa(void *v)
{
	Region *r;

	for(r = mmap; r != nil; r = r->next)
		if(v >= r->v && v < r->ve)
			return (uchar *) v - (uchar *) r->v + r->start;
	return -1;
}

uintptr
gavail(void *v)
{
	Region *r;
	
	for(r = mmap; r != nil; r = r->next)
		if(v >= r->v && v < r->ve)
			return (uchar *) r->ve - (uchar *) v;
	return 0;
}

void *
gend(void *v)
{
	return (u8int *) v + gavail(v);
}

void *tmp, *vgamem;
uvlong tmpoff, vgamemoff;

static void
mksegment(char *sn)
{
	uintptr sz;
	int fd;
	Region *r;
	char buf[256];
	u8int *gmem, *p;

	sz = BY2PG; /* temporary page */
	sz += 256*1024; /* vga */
	for(r = mmap; r != nil; r = r->next){
		if((r->type & REGALLOC) == 0)
			continue;
		r->segname = sn;
		if(sz + (r->end - r->start) < sz)
			sysfatal("out of address space");
		sz += r->end - r->start;
	}
	gmem = segattach(0, sn, nil, sz);
	if(gmem == (void*)-1){
		snprint(buf, sizeof(buf), "#g/%s", sn);
		fd = create(buf, OREAD|segrclose, DMDIR | 0777);
		if(fd < 0) sysfatal("create: %r");
		snprint(buf, sizeof(buf), "#g/%s/ctl", sn);
		fd = open(buf, OWRITE|OTRUNC);
		if(fd < 0) sysfatal("open: %r");
		snprint(buf, sizeof(buf), "va %#ullx %#ullx sticky", 0x10000000ULL, (uvlong)sz);
		if(write(fd, buf, strlen(buf)) < 0) sysfatal("write: %r");
		close(fd);
		gmem = segattach(0, sn, nil, sz);
		if(gmem == (void*)-1) sysfatal("segattach: %r");
	}
	memset(gmem, 0, sz > 1<<24 ? 1<<24 : sz);
	p = gmem;
	for(r = mmap; r != nil; r = r->next){
		if(r->segname == nil) continue;
		r->segoff = p - gmem;
		r->v = p;
		p += r->end - r->start;
		r->ve = p;
	}
	vgamem = p;
	vgamemoff = p - gmem;
	regptr(0xa0000)->segoff = vgamemoff;
	regptr(0xa0000)->v = vgamem;
	p += 256*1024;
	regptr(0xa0000)->ve = p;
	tmp = p;
	tmpoff = p - gmem;

	for(r = mmap; r != nil; r = r->next)
		modregion(r);

}

void
postexc(char *name, vlong code)
{
	if(code >= 0){
		if(ctl("exc %s,%#ux", name, (u32int)code) < 0)
			sysfatal("ctl(postexc): %r");
	}else
		if(ctl("exc %s", name) < 0)
			sysfatal("ctl(postexc): %r");
}

void
launch(void)
{
	char *s;

	s = rcflush(1);
	if(ctl("go %s", s == nil ? "" : s) < 0)
		sysfatal("go %s: %r", s == nil ? "" : s);
	getexit++;
}

static void
waitproc(void *)
{
	static char buf[512];
	char *p;
	int rc;

	threadsetname("waitexit");
	for(;;){
		rc = read(waitfd, buf, sizeof(buf) - 1);
		if(rc < 0)
			sysfatal("read: %r");
		buf[rc] = 0;
		p = strchr(buf, '\n');
		if(p != nil) *p = 0;
		sendp(waitch, strdup(buf));
	}
}

vlong timerevent = -1;
Lock timerlock;
int timerid;

static void
sleeperproc(void *)
{
	vlong then, now;

	timerid = threadid();
	timerevent = nsec() + SleeperPoll;
	unlock(&timerlock);
	threadsetname("sleeper");
	for(;;){
		lock(&timerlock);
		then = timerevent;
		now = nsec();
		if(then <= now) timerevent = now + SleeperPoll;
		unlock(&timerlock);
		if(then - now >= MinSleep){
			sleep((then - now) / MSEC);
			continue;
		}
		while(nsec() < then)
			;
		sendul(sleepch, 0);
	}
}

static void
runloop(void)
{
	char *waitmsg;
	ulong ul;
	VmxNotif notif;

	lock(&timerlock);
	proccreate(waitproc, nil, 4096);
	proccreate(sleeperproc, nil, 4096);
	launch();
	for(;;){
		enum {
			WAIT,
			SLEEP,
			NOTIF,
		};
		Alt a[] = {
			[WAIT] {waitch, &waitmsg, CHANRCV},
			[SLEEP] {sleepch, &ul, CHANRCV},
			[NOTIF] {notifch, &notif, CHANRCV},
			{nil, nil, CHANEND}
		};
		switch(alt(a)){
		case WAIT:
			getexit--;
			processexit(waitmsg);
			free(waitmsg);
			break;
		case SLEEP:
			pitadvance();
			rtcadvance();
			break;
		case NOTIF:
			notif.f(notif.arg);
			break;
		}
		if(getexit == 0 && state == VMRUNNING)
			launch();
	}
}

static int mainid;

void
sendnotif(void (*f)(void *), void *arg)
{
	VmxNotif notif = {f, arg};
	
	if(threadid() == mainid)
		f(arg);
	else
		send(notifch, &notif);
}

extern void vgainit(void);
extern void pciinit(void);
extern void pcibusmap(void);
extern void cpuidinit(void);
extern void vgafbparse(char *);
extern void init9p(char *);

int cmdlinen;
char **cmdlinev;
int bootmodn;
char **bootmod;

static uvlong
siparse(char *s)
{
	uvlong l;
	char *p;
	
	l = strtoull(s, &p, 0);
	switch(*p){
	case 'k': case 'K': p++; l *= 1<<10; break;
	case 'm': case 'M': p++; l *= 1<<20; break;
	case 'g': case 'G': p++; l *= 1<<30; break;
	}
	if(*p != 0) sysfatal("invalid argument: %s", s);
	return l;
}

static void
setiodebug(char *s)
{
	char *p;
	int n, m, neg;
	extern u32int iodebug[32];
	
	do{
		if(neg = *s == '!')
			s++;
		n = strtoul(s, &p, 0);
		if(s == p)
no:			sysfatal("invalid iodebug argument (error at %#q)", s);
		if(n >= sizeof(iodebug)*8)
range:			sysfatal("out of iodebug range (0-%#ux)", sizeof(iodebug)*8-1);
		s = p + 1;
		if(*p == '-'){
			m = strtoul(s, &p, 0);
			if(m >= sizeof(iodebug)*8)
				goto range;
			if(s == p || m < n) goto no;
			s = p + 1;
		}else
			m = n;
		for(; n <= m; n++)
			if(neg)
				iodebug[n>>5] &= ~(1<<(n&31));
			else
				iodebug[n>>5] |= 1<<(n&31);
	}while(*p == ',');
	if(*p != 0) goto no;
}

static void
usage(void)
{
	char *blanks, *p;
	
	blanks = strdup(argv0);
	for(p = blanks; *p != 0; p++)
		*p = ' ';
	fprint(2, "usage: %s [ -M mem ] [ -c com1rd[,com1wr] ] [ -C com2rd[,com2r] ] [ -n nic ]\n", argv0);
	fprint(2, "       %s [ -d blockfile ] [ -m module ] [ -v vga ] [ -9 srv ] kernel [ args ... ]\n", blanks);
	threadexitsall("usage");
}

void (*kconfig)(void);

void
threadmain(int argc, char **argv)
{
	static int (*edev[16])(char *);
	static char *edevt[nelem(edev)];
	static char *edevaux[nelem(edev)];
	static int edevn;
	static uvlong gmemsz = 64*1024*1024;
	static char *srvname;
	extern uintptr fbsz, fbaddr;
	int i;

	quotefmtinstall();
	mainid = threadid();
	cpuidinit();
	waitch = chancreate(sizeof(char *), 32);
	sleepch = chancreate(sizeof(ulong), 32);
	notifch = chancreate(sizeof(VmxNotif), 16);
	
	ARGBEGIN {
	case 'm':
		bootmod = realloc(bootmod, (bootmodn + 1) * sizeof(char *));
		bootmod[bootmodn++] = strdup(EARGF(usage()));
		break;
	case 's':
		segname = strdup(EARGF(usage()));
		segrclose = 0;
		break;
	case 'c':
		uartinit(0, EARGF(usage()));
		break;
	case 'C':
		uartinit(1, EARGF(usage()));
		break;
	case 'n':
		assert(edevn < nelem(edev));
		edev[edevn] = mkvionet;
		edevt[edevn] = "virtio network";
		edevaux[edevn++] = strdup(EARGF(usage()));
		break;
	case 'd':
		assert(edevn < nelem(edev));
		edevaux[edevn] = strdup(EARGF(usage()));
		if(strncmp(edevaux[edevn], "ide:", 4) == 0){
			edevaux[edevn] += 4;
			edev[edevn] = mkideblk;
			edevt[edevn] = "ide block";
		}else{
			edev[edevn] = mkvioblk;
			edevt[edevn] = "virtio block";
		}
		edevn++;
		break;
	case 'M':
		gmemsz = siparse(EARGF(usage()));
		if(gmemsz != (uintptr) gmemsz) sysfatal("too much memory for address space");
		break;
	case 'v':
		vgafbparse(EARGF(usage()));
		break;
	case '9':
		if(srvname != nil) usage();
		srvname = EARGF(usage());
		break;
	case L'ι':
		setiodebug(EARGF(usage()));
		break;
	default:
		usage();
	} ARGEND;
	if(argc < 1) usage();
	cmdlinen = argc - 1;
	cmdlinev = argv + 1;
	
	if(gmemsz < 1<<20) sysfatal("640 KB of RAM is not enough for everyone");
	mkregion(0, 0xa0000, REGALLOC|REGFREE|REGRWX);
	mkregion(0xa0000, 0xc0000, REGALLOC|REGRWX);
	mkregion(0xc0000, 0x100000, REGALLOC|REGRES|REGRWX);
	if(fbsz != 0 && fbaddr < gmemsz){
		mkregion(0x100000, fbaddr, REGALLOC|REGFREE|REGRWX);
		mkregion(fbaddr + fbsz, gmemsz, REGALLOC|REGFREE|REGRWX);
	}else
		mkregion(0x100000, gmemsz, REGALLOC|REGFREE|REGRWX);
	if(fbsz != 0){
		if(fbaddr < 1<<20) sysfatal("framebuffer must not be within first 1 MB");
		if(fbaddr != (u32int) fbaddr || (u32int)(fbaddr+fbsz) < fbaddr) sysfatal("framebuffer must be within first 4 GB");
		mkregion(fbaddr, fbaddr+fbsz, REGALLOC|REGRWX);
	}
	vmxsetup();
	mksegment(segname);
	loadkernel(argv[0]);
	pciinit();

	vgainit();
	for(i = 0; i < edevn; i++)
		if(edev[i](edevaux[i]) < 0)
			sysfatal("%s: %r", edevt[i]);

	pcibusmap();
	
	if(srvname != nil) init9p(srvname);
	if(kconfig != nil) kconfig();
	runloop();
	exits(nil);
}
