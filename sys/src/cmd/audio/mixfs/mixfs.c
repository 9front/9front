#include <u.h>
#include <libc.h>
#include <tos.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <pcm.h>
#pragma varargck type "!" Pcmdesc

enum {
	NBUF = 8*1024,
	NDELAY = 1764,	/* 40.0ms */
	NQUANTA = 64,	/* ~1.45ms */
	NCHAN = 2,
	ABUF = NBUF*NCHAN*2,
	Nsec = 1000000000ULL,
};

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

typedef struct Stream Stream;
struct Stream
{
	int	used;
	int	mode;
	int	flush;
	int	run;
	ulong	rp;
	ulong	wp;
	QLock;
	Rendez;
};

ulong	mixrp;
Lock	rplock;
int	lbbuf[NBUF][NCHAN];

int	mixbuf[NBUF][NCHAN];
Lock	mixlock;

Stream	streams[64];

char	*devaudio;
QLock	devlock;
int	audiofd = -1;
int	volfd = -1;

int	volume[2] = {100, 100};
int	vol64k[2] = {65536, 65536};

Pcmdesc	fmt;
Lock	fmtlock;
int	fmtchanged;
int	isnulldev;
int delay;

int
s16(uchar *p)
{
	int v;

	v = p[0]<<(sizeof(int)-2)*8 | p[1]<<(sizeof(int)-1)*8;
	v >>= (sizeof(int)-2)*8;
	return v;
}

int
clip16(int v)
{
	if(v > 0x7fff)
		return 0x7fff;
	if(v < -0x8000)
		return -0x8000;
	return v;
}

void
closeaudiodev(void)
{
	qlock(&devlock);
	if(audiofd >= 0){
		close(audiofd);
		audiofd = -1;
	}
	qunlock(&devlock);
}

void
updfmt(void)
{
	static char svol[4*1024];
	char *s, *e;
	int n, ok;

	ok = 0;
	if(volfd >= 0 && (n = pread(volfd, svol+1, sizeof(svol)-2, 0)) > 8){
		svol[0] = '\n';
		svol[1+n] = 0;
		if((s = strstr(svol, "\nfmtout ")) != nil && (e = strchr(s+12, '\n')) != nil){
			*e = 0;
			lock(&fmtlock);
			ok = fmtchanged = mkpcmdesc(s+8, &fmt) == 0;
			unlock(&fmtlock);
		}
		if(!ok && (s = strstr(svol, "\nspeed ")) != nil && (n = strtol(s+6, &s, 10)) > 0){
			lock(&fmtlock);
			fmt = pcmdescdef;
			fmt.rate = n;
			ok = fmtchanged = fmt.rate > 0;
			unlock(&fmtlock);
		}
	}
	if(!ok){
		lock(&fmtlock);
		fmt = pcmdescdef;
		fmtchanged = 1;
		unlock(&fmtlock);
	}
}

int
reopendevs(char *name)
{
	static char dir[] = "/dev/";
	int i, n, dfd, afd, len;
	char *p;
	Dir *d;

	if(name != nil){
		if(name != devaudio){
			/* hack: restrict to known audio device names */
			if((strncmp(name, "/dev/audio", 10) != 0 || strchr(name+10, '/') != nil)
			&& (strncmp(name, "#u/audio", 8) != 0 || strchr(name+8, '/') != nil)
			&& (strncmp(name, "#A/audio", 8) != 0 || strchr(name+8, '/') != nil)
			&& (strncmp(name, "/dev/null", 9) != 0 || strchr(name+9, '/') != nil)){
				werrstr("name doesnt look like an audio device");
				return -1;
			}
		}
		if((afd = open(name, OWRITE)) >= 0){
			name = strdup(name);
			goto found;
		}
		if(name != devaudio)
			return -1;
	}
	if((dfd = open(dir, OREAD)) >= 0){
		while((n = dirread(dfd, &d)) > 0){
			for(i = 0; i < n; i++){
				len = strlen(d[i].name);
				if((d[i].mode & DMDIR) != 0
				|| len < 5
				|| strncmp(d[i].name, "audio", 5) != 0
				|| strcmp(d[i].name+len-3, "ctl") == 0
				|| strcmp(d[i].name+len-4, "stat") == 0)
					continue;
				name = smprint("%s%s", dir, d[i].name);
				if((afd = open(name, OWRITE)) >= 0){
					close(dfd);
					free(d);
					goto found;
				}
				free(name);
			}
			free(d);
		}
		close(dfd);
		werrstr("no devices found");
	}
	return -1;
found:
	qlock(&devlock);
	free(devaudio);
	devaudio = name;
	isnulldev = 0;
	audiofd = dup(afd, audiofd);
	qunlock(&devlock);

	close(afd);
	if(volfd >= 0){
		close(volfd);
		volfd = -1;
	}
	if((p = utfrrune(name, '/')) != nil)
		p++;
	else
		p = name;
	if(strncmp(p, "audio", 5) == 0){
		name = smprint("%.*svolume%s", (int)(p - name), name, p+5);
		volfd = open(name, ORDWR);
		free(name);
		if(volfd >= 0)
			fprint(volfd, "delay %d\n", delay);
		updfmt();
	}else if(strncmp(p, "null", 4) == 0){
		isnulldev = 1;
		updfmt();
	}
	return 0;
}

void
fsopen(Req *r)
{
	Stream *s;

	if(strcmp(r->fid->file->name, "audio") != 0){
		respond(r, nil);
		return;
	}
	for(s = streams; s < streams+nelem(streams); s++){
		qlock(s);
		if(s->used == 0 && s->run == 0){
			s->used = 1;
			s->mode = r->ifcall.mode;
			s->flush = 0;
			qunlock(s);

			r->fid->aux = s;
			respond(r, nil);
			return;
		}
		qunlock(s);
	}
	respond(r, "all streams in use");
}

void
fsflush(Req *r)
{
	Fid *f = r->oldreq->fid;
	Stream *s;

	if(f->file != nil && strcmp(f->file->name, "audio") == 0 && (s = f->aux) != nil){
		qlock(s);
		if(s->used && s->run){
			s->flush = 1;
			rwakeup(s);
		}
		qunlock(s);
	}
	respond(r, nil);
}

void
fsclunk(Fid *f)
{
	Stream *s;

	if(f->file != nil && strcmp(f->file->name, "audio") == 0 && (s = f->aux) != nil)
		s->used = 0;
	f->aux = nil;
}

int
eqfmt(Pcmdesc *a, Pcmdesc *b)
{
	return
		a->rate == b->rate
		&& a->bits == b->bits
		&& a->channels == b->channels
		&& a->framesz == b->framesz
		&& a->fmt == b->fmt;
}

uvlong
nanosec(void)
{
	static uvlong fasthz, xstart;
	uvlong x;

	if(fasthz == ~0ULL)
		return uptime() - xstart;

	if(fasthz == 0){
		if(_tos->cyclefreq){
			fasthz = _tos->cyclefreq;
			cycles(&xstart);
		} else {
			fasthz = ~0ULL;
			xstart = uptime();
		}
		return 0;
	}
	cycles(&x);
	x -= xstart;

	uvlong q = x / fasthz;
	uvlong r = x % fasthz;

	return q*Nsec + r*Nsec/fasthz;
}

void
audioproc(void *)
{
	static uchar buf[ABUF];
	int sweep, i, j, n, m, v, rate;
	uvlong t0, dt, ns;
	ulong rp;
	Stream *s;
	uchar *p, *bufconv;
	Pcmconv	*conv;

	threadsetname("audioproc");
	conv = nil;
	bufconv = nil;
	sweep = 0;
	rate = fmt.rate;
	delay = NDELAY;
	ns = Nsec/pcmdescdef.rate;

	t0 = nanosec();
	for(;;){
		m = NBUF;
		for(s = streams; s < streams+nelem(streams); s++){
			qlock(s);
			if(s->run){
				if(s->mode & OWRITE){
					n = (long)(s->wp - mixrp);
					if(n <= 0 && (s->used == 0 || sweep))
						s->run = 0;
					else if(n < m)
						m = n;
					if(n < delay)
						rwakeup(s);
				} else {
					n = (long)(mixrp - s->rp);
					if(n > NBUF && (s->used == 0 || sweep))
						s->run = 0;
					if(n > 0)
						rwakeup(s);
				}
			}
			qunlock(s);
		}
		m %= NBUF;

		if(m == 0){
			int ms;

			ms = 100;
			if(audiofd >= 0){
				if(sweep){
					closeaudiodev();
				} else {
					/* attempt to sleep just shortly before buffer underrun */
					ms = seek(audiofd, 0, 2);
					ms *= 800;
					ms /= rate*NCHAN*2;
				}
				sweep = 1;
			}
			sleep(ms);
			continue;
		}
		sweep = 0;
		if(audiofd < 0 && reopendevs(devaudio) < 0){
			fprint(2, "%s: reopendevs: %r\n", argv0);
			sleep(1000);
			continue;
		}

		if(isnulldev){
			dt = nanosec() - t0;
			m = (dt+ns-1)/ns;
			m = MAX(NQUANTA, m);
			m = MIN(m, NBUF);
			t0 += dt;
		}else
			m = MIN(m, NQUANTA);

		p = buf;
		rp = mixrp;
		for(i=0; i<m; i++){
			for(j=0; j<NCHAN; j++){
				v = clip16(mixbuf[rp % NBUF][j])*vol64k[j] / 65536;
				lbbuf[rp % NBUF][j] = v;
				mixbuf[rp % NBUF][j] = 0;
				*p++ = v & 0xFF;
				*p++ = v >> 8;
			}
			rp++;
		}

		/* barrier */
		lock(&rplock);
		mixrp = rp;
		unlock(&rplock);

		if(isnulldev){
			sleep(5);
			continue;
		}

		lock(&fmtlock);
		if(fmtchanged){
			fmtchanged = 0;
			free(bufconv);
			bufconv = nil;
			freepcmconv(conv);
			conv = nil;
			if(!eqfmt(&fmt, &pcmdescdef))
			if((conv = allocpcmconv(&pcmdescdef, &fmt)) == nil ||
			   (n = pcmratio(conv, ABUF)) < 0 ||
			   (bufconv = malloc(n)) == nil){
				fprint(2, "%s: %r\n", devaudio);
				freepcmconv(conv);
				conv = nil;
			}
			rate = fmt.rate;
		}
		unlock(&fmtlock);

		n = p - buf;
		p = buf;
		if(conv != nil){
			n = pcmconv(conv, buf, bufconv, n);
			p = bufconv;
		}
		if(write(audiofd, p, n) != n)
			closeaudiodev();
	}
}

void
fsread(Req *r)
{
	Srv *srv;
	int i, j, n, m, v;
	char *f, *e;
	Stream *s;
	uchar *p;

	if(r->fid->file->aux == &volfd){
		static char svol[4096];
		if(r->ifcall.offset == 0){
			m = snprint(svol, sizeof(svol), "dev %s\nmix %d %d\nfmtout %!\nspeed %d\n",
				devaudio?devaudio:"",
				volume[0], volume[1],
				pcmdescdef,
				pcmdescdef.rate);
			if(volfd >= 0 && (n = pread(volfd, svol+m, sizeof(svol)-m-1, 0)) > 0){
				svol[m+n] = 0;
				/* device's fmtout and speed are removed */
				if((f = strstr(svol+m-1, "\nfmtout ")) != nil && (e = strchr(f+8, '\n')) != nil){
					n -= e-f;
					memmove(f, e, n+1);
				}
				if((f = strstr(svol+m-1, "\nspeed ")) != nil && (e = strchr(f+7, '\n')) != nil){
					n -= e-f;
					memmove(f, e, n+1);
				}
			}
		}
		readstr(r, svol);
		respond(r, nil);
		return;
	}

	p = (uchar*)r->ofcall.data;
	n = r->ifcall.count;
	n &= ~(NCHAN*2 - 1);
	r->ofcall.count = n;
	n /= (NCHAN*2);

	srv = r->srv;
	srvrelease(srv);
	s = r->fid->aux;
	qlock(s);
	while(n > 0){
		if(s->run == 0){
			s->rp = mixrp;
			s->run = 1;
		}
		m = (long)(mixrp - s->rp);
		if(m <= 0){
			if(s->flush)
				break;
			s->run = 1;
			rsleep(s);
			continue;
		}
		if(m > NBUF){
			m = NBUF;
			s->rp = mixrp - m;
		}
		if(m > n)
			m = n;

		for(i=0; i<m; i++){
			for(j=0; j<NCHAN; j++){
				v = lbbuf[s->rp % NBUF][j];
				*p++ = v & 0xFF;
				*p++ = v >> 8;
			}
			s->rp++;
		}

		n -= m;
	}
	s->flush = 0;
	qunlock(s);
	respond(r, nil);
	srvacquire(srv);
}

void
fswrite(Req *r)
{
	Srv *srv;
	int i, j, n, m;
	Stream *s;
	uchar *p;

	if(r->fid->file->aux == &volfd){
		char msg[64], *f[5];
		int x[2], nf;

		r->ofcall.count = r->ifcall.count;
		snprint(msg, sizeof(msg), "%.*s",
			utfnlen((char*)r->ifcall.data, r->ifcall.count), (char*)r->ifcall.data);
		nf = tokenize(msg, f, nelem(f));
		if(nf > 1 && strcmp(f[0], "dev") == 0){
			if(reopendevs(f[1]) < 0){
				responderror(r);
				return;
			}
		}else if(nf > 1 && strcmp(f[0], "mix") == 0){
			x[0] = atoi(f[1]);
			x[1] = nf < 3 ? x[0] : atoi(f[2]);
			if(f[1][0] == '+' || f[1][0] == '-'){
				x[0] += volume[0];
				x[1] += volume[1];
			}
			volume[0] = MAX(0, x[0]);
			volume[0] = MIN(volume[0], 100);
			volume[1] = MAX(0, x[1]);
			volume[1] = MIN(volume[1], 100);
			/* ≈60dB dynamic range; [0-100] → [0-65536] */
			vol64k[0] = 65.536 * (exp(volume[0] * 0.0690876) - 1.0);
			vol64k[1] = 65.536 * (exp(volume[1] * 0.0690876) - 1.0);
		}else if(volfd >= 0){
			if(nf > 1 && strcmp(f[0], "delay") == 0){
				n = atoi(f[1]);
				if(n < NQUANTA || n > NBUF){
					responderror(r);
					return;
				}
				delay = n;
			}
			if(write(volfd, r->ifcall.data, r->ifcall.count) < 0){
				responderror(r);
				return;
			}
		}else{
			respond(r, "bad msg");
			return;
		}
		respond(r, nil);
		return;
	}

	p = (uchar*)r->ifcall.data;
	n = r->ifcall.count;
	r->ofcall.count = n;
	n /= (NCHAN*2);

	srv = r->srv;
	srvrelease(srv);
	s = r->fid->aux;
	qlock(s);
	while(n > 0){
		if(s->run == 0){
			s->wp = mixrp;
			s->run = 1;
		}
		m = NBUF-1 - (long)(s->wp - mixrp);

		if(m <= 0){
			if(s->flush)
				break;
			s->run = 1;
			rsleep(s);
			continue;
		}
		if(m > n)
			m = n;

		lock(&mixlock);
		for(i=0; i<m; i++){
			for(j=0; j<NCHAN; j++){
				mixbuf[s->wp % NBUF][j] += s16(p);
				p += 2;
			}
			s->wp++;
		}
		unlock(&mixlock);

		n -= m;
	}
	if((long)(s->wp - mixrp) >= delay && !s->flush){
		s->run = 1;
		rsleep(s);
	}
	s->flush = 0;
	qunlock(s);
	respond(r, nil);
	srvacquire(srv);
}

void
fsstat(Req *r)
{
	Stream *s;

	r->d.length = 0;
	if(r->fid->file != nil && strcmp(r->fid->file->name, "audio") == 0 && (s = r->fid->aux) != nil){
		qlock(s);
		if(s->run){
			r->d.length = (long)(s->wp - mixrp);
			r->d.length *= NCHAN*2;
		}
		qunlock(s);
	}
	respond(r, nil);
}

void
fsstart(Srv *)
{
	Stream *s;

	for(s=streams; s < streams+nelem(streams); s++){
		s->used = s->run = 0;
		s->Rendez.l = &s->QLock;
	}
	proccreate(audioproc, nil, 16*1024);
}

void
fsend(Srv *)
{
	threadexitsall(nil);
}

Srv fs = {
	.open=		fsopen,
	.read=		fsread,
	.write=		fswrite,
	.stat=		fsstat,
	.destroyfid=	fsclunk,
	.flush=		fsflush,
	.start=		fsstart,
	.end=		fsend,
};

void
usage(void)
{
	fprint(2, "usage: %s [-D] [-v] [-s srvname] [-m mtpt]\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char **argv)
{
	char *srv = nil;
	char *m, *mtpt = "/mnt/mix";

	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	case 's':
		srv = EARGF(usage());
		break;
	case 'm':
		mtpt = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND;

	if(argc > 1)
		usage();

	fmtinstall('!', pcmdescfmt);
	reopendevs(argv[0]);
	closeaudiodev();

	fs.tree = alloctree(nil, nil, DMDIR|0777, nil);
	createfile(fs.tree->root, "audio", nil, 0666, nil);
	createfile(fs.tree->root, "volume", nil, 0666, &volfd);
	threadpostmountsrv(&fs, srv, mtpt, MREPL);

	bind(mtpt, "/dev", MAFTER);
	m = smprint("%s/audio", mtpt);
	bind(m, "/dev/audio", MREPL);
	m = smprint("%s/volume", mtpt);
	bind(m, "/dev/volume", MREPL);

	threadexits(0);
}
