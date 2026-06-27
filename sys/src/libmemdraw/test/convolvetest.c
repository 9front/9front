#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <memdraw.h>

uvlong t0;

void
profbegin(void)
{
	char buf[128];
	int fd;

	snprint(buf, sizeof(buf), "/proc/%d/ctl", getpid());
	fd = open(buf, OWRITE);
	if(fd < 0)
		sysfatal("open: %r");
	fprint(fd, "profile\n");
	close(fd);
	t0 = nsec();
}

void
profend(void)
{
	char buf[128];
	double dt;

	dt = (nsec() - t0)/1e6;
	fprint(2, "dt: %fms\n", dt);

	snprint(buf, sizeof(buf), "%d", getpid());
	switch(fork()){
	case -1:
		abort();
	case 0:
		dup(2, 1);
		print("tprof %s\n", buf);
		execl("/bin/tprof", "tprof", buf, nil);
		abort();
	default:
		free(wait());
		break;
	}
}

void
initworkrects(Rectangle *wr, int nwr, Rectangle *fbr)
{
	int i, Δy;

	wr[0] = *fbr;
	Δy = Dy(wr[0])/nwr;
	wr[0].max.y = wr[0].min.y + Δy;
	for(i = 1; i < nwr; i++)
		wr[i] = rectaddpt(wr[i-1], Pt(0,Δy));
	if(wr[nwr-1].max.y < fbr->max.y)
		wr[nwr-1].max.y = fbr->max.y;
}

void *
erealloc(void *v, ulong n)
{
	void *nv;

	nv = realloc(v, n);
	if(nv == nil)
		sysfatal("realloc: %r");
	if(v == nil)
		setmalloctag(nv, getcallerpc(&v));
	else
		setrealloctag(nv, getcallerpc(&v));
	return nv;
}

void
reversem(double *k, int len)
{
	double *e, t;

	e = k + len;
	while(k < e){
		t = *k;
		*k++ = *--e;
		*e = t;
	}
}

#define isspace(c)	((c) == ' ' || (c) == '\t')
Memimage *
readimagekernel(int fd, double denom, int reverse)
{
	Memimage *ik;
	Biobuf *bin;
	double *k, d;
	int nk, dx0, dx, dy;
	char *line;

	k = nil;
	nk = 0;
	dx0 = dx = dy = 0;

	bin = Bfdopen(fd, OREAD);
	if(bin == nil)
		sysfatal("Bfdopen: %r");

	while((line = Brdstr(bin, '\n', 1)) != nil){
		while(*line){
			d = strtod(line, &line);
			if(*line && !isspace(*line)){
				free(k);
				Bterm(bin);
				werrstr("bad image kernel file");
				return nil;
			}
			k = erealloc(k, (nk + 1)*sizeof(double));
			k[nk++] = d;
			dx++;
		}
		if(dy > 0 && dx != dx0){
			free(k);
			Bterm(bin);
			werrstr("image kernel file has inconsistent dx");
			return nil;
		}
		if(dy++ == 0)
			dx0 = dx;
		dx = 0;
	}
	Bterm(bin);

	if(reverse)
		reversem(k, dx0*dy);

	ik = allocmemimagekernel(k, dx0, dy, denom);
	free(k);
	if(ik == nil){
		werrstr("allocmemimagekernel: %r");
		return nil;
	}
	return ik;
}

void
usage(void)
{
	fprint(2, "usage: %s [-cRp] kernel [denom]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	Memimage *dst, *src;
	Memimage *ikern;
	Rectangle dr, *wr;
	int convolve, dorepl, parallel, fd, nproc, i;
	char *nprocs, *kernf;
	double denom;

	kernf = nil;
	denom = 0;
	convolve = 1;
	dorepl = 0;
	parallel = 0;
	ARGBEGIN{
	case 'c':
		convolve = 0;	/* correlate */
		break;
	case 'R':
		dorepl++;
		break;
	case 'p':
		parallel++;
		break;
	default:
		usage();
	}ARGEND;
	switch(argc){
	case 2:
		denom = strtod(argv[1], nil);
		/* fallthrough */
	case 1:
		kernf = argv[0];
		break;
	default:
		usage();
	}

	if(memimageinit() != 0)
		sysfatal("memimageinit: %r");

	fd = open(kernf, OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	ikern = readimagekernel(fd, denom, convolve);
	if(ikern == nil)
		sysfatal("readimagekernel: %r");
	close(fd);

	src = readmemimage(0);
	if(dorepl)
		src->flags |= Frepl;

	dr = src->r;
	dst = allocmemimage(dr, src->chan);
	memfillcolor(dst, DTransparent);

	profbegin();
	if(parallel){
		nprocs = getenv("NPROC");
		if(nprocs == nil || (nproc = strtoul(nprocs, nil, 10)) < 2)
			nproc = 1;
		free(nprocs);

		wr = malloc(nproc*sizeof(Rectangle));
		initworkrects(wr, nproc, &dr);

		for(i = 0; i < nproc; i++){
			switch(rfork(RFPROC|RFMEM)){
			case -1:
				sysfatal("rfork: %r");
			case 0:
				if(memimagecorrelate(dst, wr[i], src, src->r.min, ikern) < 0)
					fprint(2, "[%d] memimagecorrelate: %r\n", getpid());
				exits(nil);
			}
		}
		while(waitpid() != -1)
			;

		free(wr);
	}else{
		if(memimagecorrelate(dst, dr, src, src->r.min, ikern) < 0)
			sysfatal("memimagecorrelate: %r");

//		dr = rectaddpt(Rect(0,0,Dx(dst->r)/2,Dy(dst->r)/2), dst->r.min);
//		dst->clipr = dr;
//		if(memimagecorrelate(dst, dr, src, src->r.min, ikern) < 0)
//			sysfatal("memimagecorrelate: %r");
//		dr = rectaddpt(Rect(Dx(dst->r)/2+1,0,Dx(dst->r),Dy(dst->r)/2), dst->r.min);
//		dst->clipr = dst->r;
//		if(memimagecorrelate(dst, dr, src, src->r.min, ikern) < 0)
//			sysfatal("memimagecorrelate: %r");
//		dr = rectaddpt(Rect(0,Dy(dst->r)/2+1,Dx(dst->r)/2,Dy(dst->r)), dst->r.min);
//		if(memimagecorrelate(dst, dr, src, src->r.min, ikern) < 0)
//			sysfatal("memimagecorrelate: %r");
//		dr = rectaddpt(Rect(Dx(dst->r)/2+1,Dy(dst->r)/2+1,Dx(dst->r),Dy(dst->r)), dst->r.min);
//		dst->clipr = dr;
//		if(memimagecorrelate(dst, dr, src, src->r.min, ikern) < 0)
//			sysfatal("memimagecorrelate: %r");
	}
	profend();
	writememimage(1, dst);

	exits(nil);
}
