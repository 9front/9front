#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>

static void
resample(Memimage *dst, Rectangle r, Memimage *src, Rectangle sr)
{
	Point sp, dp;
	Point _sp, qp;
	Point ssize, dsize;
	uchar *pdst0, *pdst, *psrc0, *psrc;
	ulong s00, s01, s10, s11;
	int tx, ty, bpp, bpl;

	ssize = subpt(subpt(sr.max, sr.min), Pt(1,1));
	dsize = subpt(subpt(r.max, r.min), Pt(1,1));
	pdst0 = byteaddr(dst, r.min);
	bpp = src->depth/8;
	bpl = src->width*sizeof(int);

	qp = Pt(0, 0);
	if(dsize.x > 0)
		qp.x = (ssize.x<<12)/dsize.x;
	if(dsize.y > 0)
		qp.y = (ssize.y<<12)/dsize.y;

	_sp.y = sr.min.y<<12;
	for(dp.y=0; dp.y<=dsize.y; dp.y++){
		sp.y = _sp.y>>12;
		ty = _sp.y&0xFFF;
		pdst = pdst0;
		sp.x = sr.min.x;
		psrc0 = byteaddr(src, sp);
		_sp.x = 0;
		for(dp.x=0; dp.x<=dsize.x; dp.x++){
			sp.x = _sp.x>>12;
			tx = _sp.x&0xFFF;
			psrc = psrc0 + sp.x*bpp;
			s00 = (0x1000-tx)*(0x1000-ty);
			s01 = tx*(0x1000-ty);
			s10 = (0x1000-tx)*ty;
			s11 = tx*ty;
			switch(bpp){
			case 4:
				pdst[3] = (s11*psrc[bpl+bpp+3] + 
					   s10*psrc[bpl+3] + 
					   s01*psrc[bpp+3] +
					   s00*psrc[3]) >>24;
			case 3:
				pdst[2] = (s11*psrc[bpl+bpp+2] + 
					   s10*psrc[bpl+2] + 
					   s01*psrc[bpp+2] +
					   s00*psrc[2]) >>24;
				pdst[1] = (s11*psrc[bpl+bpp+1] + 
					   s10*psrc[bpl+1] + 
					   s01*psrc[bpp+1] +
					   s00*psrc[1]) >>24;
			case 1:
				pdst[0] = (s11*psrc[bpl+bpp] + 
					   s10*psrc[bpl] + 
					   s01*psrc[bpp] +
					   s00*psrc[0]) >>24;
			}
			pdst += bpp;
			_sp.x += qp.x;
		}
		pdst0 += dst->width*sizeof(int);
		_sp.y += qp.y;
	}
}

void
usage(void)
{
	sysfatal("Usage: %s [ -x width ] [ -y height ] [image]\n", argv0);
}

void
main(int argc, char **argv)
{
	int fd, xsize, ysize;
	Memimage *im, *nim;
	ulong ochan, tchan;
	char buf[12];

	xsize = ysize = 0;
	ARGBEGIN{
	case 'x':
		xsize = atoi(EARGF(usage()));
		break;
	case 'y':
		ysize = atoi(EARGF(usage()));
		break;
	default:
		usage();
	}ARGEND
	fd = 0;
	if(*argv){
		fd = open(*argv, OREAD);
		if(fd < 0)
			sysfatal("open: %r");
	}
	memimageinit();
	if((im = readmemimage(fd)) == nil)
		sysfatal("readmemimage: %r");
	if(xsize || ysize){
		if(ysize == 0)
			ysize = (xsize * Dy(im->r)) / Dx(im->r);
		if(xsize == 0)
			xsize = (ysize * Dx(im->r)) / Dy(im->r);
		ochan = im->chan;
		switch(ochan){
		default:
			sysfatal("can't handle channel type %s", chantostr(buf, ochan));
		case GREY8:
		case RGB24:
		case RGBA32:
		case ARGB32:
		case XRGB32:
			tchan = ochan;
			break;
		case CMAP8:
		case RGB16:
		case RGB15:
			tchan = RGB24;
			break;
		case GREY1:
		case GREY2:
		case GREY4:
			tchan = GREY8;
			break;
		}
		if(tchan != ochan){
			if((nim = allocmemimage(im->r, tchan)) == nil)
				sysfatal("allocimage: %r");
			memimagedraw(nim, nim->r, im, im->r.min, nil, ZP, S);
			freememimage(im);
			im = nim;
		}
		if((nim = allocmemimage(Rect(im->r.min.x, im->r.min.y, xsize, ysize), tchan)) == nil)
			sysfatal("addocmemimage: %r");
		resample(nim, nim->r, im, im->r);
		freememimage(im);
		im = nim;
		if(tchan != ochan){
			if((im = allocmemimage(nim->r, ochan)) == nil)
				sysfatal("allocimage: %r");
			memimagedraw(im, im->r, nim, nim->r.min, nil, ZP, S);
			freememimage(nim);
		}
	}
	if(writememimage(1, im) < 0)
		sysfatal("writememimage: %r");
	exits(0);
}
