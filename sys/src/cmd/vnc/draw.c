#include "vnc.h"
#include "vncv.h"

static struct {
	char	*name;
	int	num;
} enctab[] = {
	"copyrect",	EncCopyRect,
	"corre",	EncCorre,
	"hextile",	EncHextile,
	"raw",		EncRaw,
	"rre",		EncRre,
	"mousewarp",	EncMouseWarp,
	"desktopsize",	EncDesktopSize,
	"xdesktopsize",	EncXDesktopSize,
};

static	uchar	*pixbuf;
static	uchar	*linebuf;
static	int	vpixb;
static	int	pixb;
static	void	(*pixcp)(uchar*, uchar*);
static double scalex;
static double scaley;

static void
vncsetscale(Vnc *v)
{
	scalex = (v->dim.max.x - v->dim.min.x) / (double)(screen->r.max.x - screen->r.min.x);
	scaley = (v->dim.max.y - v->dim.min.y) / (double)(screen->r.max.y - screen->r.min.y);
	if(verbose > 1) fprint(2, "scaling %fx%f\n", scalex, scaley);
}

static void
vncsetdim(Vnc *v, Rectangle dim)
{
	v->dim = rectsubpt(dim, dim.min);
	linebuf = realloc(linebuf, v->dim.max.x * vpixb);
	pixbuf = realloc(pixbuf, v->dim.max.x * pixb * v->dim.max.y);
	if(linebuf == nil || pixbuf == nil)
		sysfatal("can't allocate pix decompression storage");
	lockdisplay(display);
	adjustwin(v, 0);
	unlockdisplay(display);
}

static void
vncrdcolor(Vnc *v, uchar *color)
{
	vncrdbytes(v, color, vpixb);

	if(cvtpixels)
		(*cvtpixels)(color, color, 1);
}

void
sendencodings(Vnc *v)
{
	char *f[16];
	int enc[16], nenc, i, j, nf;

	nf = tokenize(encodings, f, nelem(f));
	nenc = 0;
	for(i=0; i<nf; i++){
		for(j=0; j<nelem(enctab); j++)
			if(strcmp(f[i], enctab[j].name) == 0)
				break;
		if(j == nelem(enctab)){
			print("warning: unknown encoding %s\n", f[i]);
			continue;
		}
		enc[nenc++] = enctab[j].num;
	}

	vnclock(v);
	vncwrchar(v, MSetEnc);
	vncwrchar(v, 0);
	vncwrshort(v, nenc);
	for(i=0; i<nenc; i++)
		vncwrlong(v, enc[i]);
	vncflush(v);
	vncunlock(v);
}

void
requestupdate(Vnc *v, int incremental)
{
	Rectangle r;

	lockdisplay(display);
	flushimage(display, 1);
	r = autoscale ? v->dim : rectsubpt(screen->r, screen->r.min);
	unlockdisplay(display);
	vnclock(v);
	if(incremental == 0 && (v->canresize&2)!=0 && !eqrect(r, v->dim)){
		vncwrchar(v, MSetDesktopSize);
		vncwrchar(v, 0);
		vncwrpoint(v, r.max);
		vncwrchar(v, 1);
		vncwrchar(v, 0);
		vncwrlong(v, v->screen[0].id);
		vncwrrect(v, r);
		vncwrlong(v, v->screen[0].flags);
	} else 
		rectclip(&r, v->dim);
	vncwrchar(v, MFrameReq);
	vncwrchar(v, autoscale ? 0 : incremental);
	vncwrrect(v, r);
	vncflush(v);
	vncunlock(v);
}

static Rectangle
clippixbuf(Rectangle r, int maxx, int maxy)
{
	int y, h, stride1, stride2;

	if(r.min.x > maxx || r.min.y > maxy){
		r.max.x = 0;
		return r;
	}
	if(r.max.y > maxy)
		r.max.y = maxy;
	if(r.max.x <= maxx)
		return r;

	stride2 = Dx(r) * pixb;
	r.max.x = maxx;
	stride1 = Dx(r) * pixb;
	h = Dy(r);
	for(y = 0; y < h; y++)
		memmove(&pixbuf[y * stride1], &pixbuf[y * stride2], stride1);

	return r;
}

/* must be called with display locked */
static void
updatescreen(Rectangle r)
{
	int b, bb;

	lockdisplay(display);
	if(r.max.x > Dx(screen->r) || r.max.y > Dy(screen->r)){
		r = clippixbuf(r, Dx(screen->r), Dy(screen->r));
		if(r.max.x == 0){
			unlockdisplay(display);
			return;
		}
	}

	/*
	 * assume load image fails only because of resize
	 */
	b = Dx(r) * pixb * Dy(r);
	bb = loadimage(screen, rectaddpt(r, screen->r.min), pixbuf, b);
	if(bb != b && verbose)
		fprint(2, "loadimage %d on %R for %R returned %d: %r\n", b, rectaddpt(r, screen->r.min), screen->r, bb);
	unlockdisplay(display);
}

static void
fillrect(Rectangle r, int stride, uchar *color)
{
	int x, xe, y, off;

	y = r.min.y;
	off = y * stride;
	for(; y < r.max.y; y++){
		xe = off + r.max.x * pixb;
		for(x = off + r.min.x * pixb; x < xe; x += pixb)
			(*pixcp)(&pixbuf[x], color);
		off += stride;
	}
}

static void
loadbuf(Vnc *v, Rectangle r, int stride)
{
	int off;
	double x, y, endy;

	if(cvtpixels){
		y = r.min.y;
		off = y * stride + r.min.x * pixb;
		for(; y < r.max.y; y++){
			vncrdbytes(v, linebuf, Dx(r) * vpixb);
			(*cvtpixels)(&pixbuf[off], linebuf, Dx(r));
			off += stride;
		}
	}else{
		y = r.min.y;
		off = y * stride + r.min.x * pixb;
		for(; y < r.max.y; y++){
			vncrdbytes(v, &pixbuf[off], Dx(r) * pixb);
			off += stride;
		}
	}
	if(autoscale){
		endy = off/(double)stride;
		for(y = 0; y < endy; y += scaley)
			for(x = 0; x < stride; x+=scalex)
				memmove(&pixbuf[(int)(y/scaley)*stride+(int)(x/scalex)/pixb*pixb], &pixbuf[(int)(y)*stride+(int)(x/pixb)*pixb], pixb);
	}
}

static Rectangle
hexrect(ushort u)
{
	int x, y, w, h;

	x = u>>12;
	y = (u>>8)&15;
	w = ((u>>4)&15)+1;
	h = (u&15)+1;

	return Rect(x, y, x+w, y+h);
}


static void
dohextile(Vnc *v, Rectangle r, int stride)
{
	ulong bg, fg, c;
	int enc, nsub, sx, sy, w, h, th, tw;
	Rectangle sr, ssr;

	fg = bg = 0;
	h = Dy(r);
	w = Dx(r);
	for(sy = 0; sy < h; sy += HextileDim){
		th = h - sy;
		if(th > HextileDim)
			th = HextileDim;
		for(sx = 0; sx < w; sx += HextileDim){
			tw = w - sx;
			if(tw > HextileDim)
				tw = HextileDim;

			sr = Rect(sx, sy, sx + tw, sy + th);
			enc = vncrdchar(v);
			if(enc & HextileRaw){
				loadbuf(v, sr, stride);
				continue;
			}

			if(enc & HextileBack)
				vncrdcolor(v, (uchar*)&bg);
			fillrect(sr, stride, (uchar*)&bg);

			if(enc & HextileFore)
				vncrdcolor(v, (uchar*)&fg);

			if(enc & HextileRects){
				nsub = vncrdchar(v);
				(*pixcp)((uchar*)&c, (uchar*)&fg);
				while(nsub-- > 0){
					if(enc & HextileCols)
						vncrdcolor(v, (uchar*)&c);
					ssr = rectaddpt(hexrect(vncrdshort(v)), sr.min);
					fillrect(ssr, stride, (uchar*)&c);
				}
			}
		}
	}
}

static void
dorectangle(Vnc *v)
{
	ulong type;
	long n, stride;
	ulong color;
	Point p;
	Rectangle r, subr, maxr;

	r = vncrdrect(v);
	type = vncrdlong(v);
	switch(type){
	case EncMouseWarp:
		mousewarp(r.min);
		return;
	case EncDesktopSize:
		v->canresize |= 1;
		vncsetdim(v, r);
		return;
	case EncXDesktopSize:
		v->canresize |= 2;
		n = vncrdlong(v)>>24;
		if(n <= 0)
			break;
		v->screen[0].id = vncrdlong(v);
		v->screen[0].rect = vncrdrect(v);
		v->screen[0].flags = vncrdlong(v);
		while(--n > 0){
			vncrdlong(v);
			vncrdrect(v);
			vncrdlong(v);
		}
		vncsetdim(v, v->screen[0].rect);
		return;
	}

	if(!rectinrect(r, v->dim))
		sysfatal("bad rectangle from server: %R not in %R", r, v->dim);
	maxr = autoscale ? rectsubpt(v->dim, v->dim.min) : rectsubpt( r, r.min );
	maxr.max.x = Dx(maxr); maxr.min.x = 0;
	maxr.max.y = Dy(maxr); maxr.min.y = 0;
	stride = maxr.max.x * pixb;
	if(verbose > 2) fprint(2, "maxr.max.x %d; maxr.max.y %d; maxr.min.x %d; maxr.min.y %d, pixb: %d, stride: %ld, type: %lx\n", maxr.max.x, maxr.max.y, maxr.min.x, maxr.min.y, pixb, stride, type);

	switch(type){
	default:
		sysfatal("bad rectangle encoding from server: %lx", type);
		break;
	case EncRaw:
		loadbuf(v, maxr, stride);
		updatescreen(r);
		break;

	case EncCopyRect:
		p = vncrdpoint(v);
		lockdisplay(display);
		p = addpt(p, screen->r.min);
		r = rectaddpt(r, screen->r.min);
		draw(screen, r, screen, nil, p);
		unlockdisplay(display);
		break;

	case EncRre:
	case EncCorre:
		n = vncrdlong(v);
		vncrdcolor(v, (uchar*)&color);
		fillrect(maxr, stride, (uchar*)&color);
		while(n-- > 0){
			vncrdcolor(v, (uchar*)&color);
			if(type == EncRre)
				subr = vncrdrect(v);
			else
				subr = vncrdcorect(v);
			if(!rectinrect(subr, maxr))
				sysfatal("bad encoding from server");
			fillrect(subr, stride, (uchar*)&color);
		}
		updatescreen(r);
		break;

	case EncHextile:
		dohextile(v, r, stride);
		updatescreen(r);
		break;
	}
}

static void
pixcp8(uchar *dst, uchar *src)
{
	*dst = *src;
}

static void
pixcp16(uchar *dst, uchar *src)
{
	*(ushort*)dst = *(ushort*)src;
}

static void
pixcp32(uchar *dst, uchar *src)
{
	*(ulong*)dst = *(ulong*)src;
}

static void
pixcp24(uchar *dst, uchar *src)
{
	dst[0] = src[0];
	dst[1] = src[1];
	dst[2] = src[2];
}

static int
calcpixb(int bpp)
{
	if(bpp / 8 * 8 != bpp)
		sysfatal("can't handle your screen");
	return bpp / 8;
}

void
readfromserver(Vnc *v)
{
	uchar type;
	uchar junk[100];
	long n;

	vpixb = calcpixb(v->bpp);
	pixb = calcpixb(screen->depth);
	switch(pixb){
	case 1:
		pixcp = pixcp8;
		break;
	case 2:
		pixcp = pixcp16;
		break;
	case 3:
		pixcp = pixcp24;
		break;
	case 4:
		pixcp = pixcp32;
		break;
	default:
		sysfatal("can't handle your screen: bad depth %d", pixb);
	}
	vncsetdim(v, v->dim);
	for(;;){
		type = vncrdchar(v);
		switch(type){
		default:
			sysfatal("bad message from server: %x", type);
			break;
		case MFrameUpdate:
			if(autoscale)
				vncsetscale(v);
			vncrdchar(v);
			n = vncrdshort(v);
			while(n-- > 0)
				dorectangle(v);
			requestupdate(v, 1);
			break;

		case MSetCmap:
			vncrdbytes(v, junk, 3);
			n = vncrdshort(v);
			vncgobble(v, n*3*2);
			break;

		case MBell:
			break;

		case MSAck:
			break;

		case MSCut:
			vncrdbytes(v, junk, 3);
			n = vncrdlong(v);
			writesnarf(v, n);
			break;
		}
	}
}
