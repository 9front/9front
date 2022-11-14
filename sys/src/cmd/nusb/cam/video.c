#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <bio.h>
#include "usb.h"
#include "uvc.h"
#include "dat.h"
#include "fns.h"

enum {
	NFrames = 20, /* frames to buffer */
};

VFrame *
grabframe(Cam *c)
{
	VFrame *v;
	VFrame **l;

	qlock(&c->qulock);
	if(c->freel != nil)
		l = &c->freel;
	else{
		assert(c->actl != nil);
		if(c->actl->p == 0)
			l = &c->actl;
		else
			l = &c->actl->next;
	}
	assert(*l != nil);
	v = *l;
	*l = v->next;
	qunlock(&c->qulock);
	v->next = nil;
	v->n = 60;
	v->p = 0;
	return v;
}

void
pushframe(Cam *c, VFrame *v)
{
	VFrame **l;
	Req *r;
	
	v->next = nil;
	qlock(&c->qulock);
	for(l = &c->actl; *l != nil; l = &(*l)->next)
		;
	*l = v;
	while(c->delreq != nil && c->actl != nil){
		r = c->delreq;
		c->delreq = (Req*)r->qu.next;
		videoread(r, c, 0);
	}
	qunlock(&c->qulock);
}

void
yuy2convert(Format *, VSUncompressedFrame *g, uchar *in, VFrame *out)
{
	int y, x, w, h;
	int R₀, G₀, B₀, R₁, G₁, B₁;
	int Cb, Cr, z, Y₀, Y₁;
	uchar *op;

	w = GET2(g->wWidth);
	h = GET2(g->wHeight);
	op = out->d + out->n;
	for(y = 0; y < h; y++)
		for(x = 0; x < w; x += 2, in += 4){
			Y₀ = in[0] * 0x10101;
			Cb = in[1] - 0x80;
			Y₁ = in[2] * 0x10101;
			Cr = in[3] - 0x80;
			z = 22554*Cb + 46802*Cr;
			Cb *= 116130;
			Cr *= 91881;
			R₀ = Y₀ + Cr;
			G₀ = Y₀ - z;
			B₀ = Y₀ + Cb;
			R₁ = Y₁ + Cr;
			G₁ = Y₁ - z;
			B₁ = Y₁ + Cb;
			*op++ = (R₀>>24) ? ~(R₀>>31) : (R₀>>16);
			*op++ = (G₀>>24) ? ~(G₀>>31) : (G₀>>16);
			*op++ = (B₀>>24) ? ~(B₀>>31) : (B₀>>16);
			*op++ = (R₁>>24) ? ~(R₁>>31) : (R₁>>16);
			*op++ = (G₁>>24) ? ~(G₁>>31) : (G₁>>16);
			*op++ = (B₁>>24) ? ~(B₁>>31) : (B₁>>16);
		}
	out->n = op - out->d;
}

struct Converter {
	uchar guid[16];
	void (*fn)(Format *, VSUncompressedFrame *, uchar *, VFrame *);
} converters[] = {
	{{0x59,0x55,0x59,0x32, 0x00,0x00,0x10,0x00, 0x80,0x00,0x00,0xAA, 0x00,0x38,0x9B,0x71}, yuy2convert},
};

struct Converter *
getconverter(Format *f)
{
	struct Converter *c;
	uchar *guid;
	
	guid = f->desc->guidFormat;
	for(c = converters; c < converters + nelem(converters); c++)
		if(memcmp(guid, c->guid, 16) == 0)
			return c;
	werrstr("unknown format %.2X%.2X%.2X%.2X %.2X%.2X%.2X%.2X %.2X%.2X%.2X%.2X %.2X%.2X%.2X%.2X",
		guid[0], guid[1], guid[2], guid[3],
		guid[4], guid[5], guid[6], guid[7],
		guid[8], guid[9], guid[10], guid[11],
		guid[12], guid[13], guid[14], guid[15]);
	return nil;
}

static void
freeframes(VFrame **fp)
{
	VFrame *f, *g;
	
	for(f = *fp; f != nil; f = g){
		g = f->next;
		free(f);
	}
	*fp = nil;
}

void
cvtproc(void *v)
{
	int frsz;
	Cam *c;
	Format *f;
	VSUncompressedFrame *g;
	uchar *fbuf;
	int n;
	int rc;
	uchar buf[3*1024];
	struct Converter *cvt;
	int bufn;
	int ob;
	VFrame *of;
	
	c = v;
	assert(getframedesc(c, c->pc.bFormatIndex, c->pc.bFrameIndex, &f, &g) >= 0);
	cvt = getconverter(f);
	assert(cvt != nil);
	frsz = GET2(g->wWidth) * GET2(g->wHeight) * f->desc->bBitsPerPixel / 8;
	fbuf = emallocz(frsz, 1);
	bufn = 0;
	ob = 0;
	for(;;){
		if(c->abort) break;
		rc = read(c->ep->dfd, buf, sizeof(buf));
		if(c->abort || rc < 0) break;
		if(rc == 0) continue;
		if((ob ^ buf[1]) & 1){
			if(bufn == frsz){
				of = grabframe(c);
				cvt->fn(f, g, fbuf, of);
				pushframe(c, of);
			}
			bufn = 0;
		}
		ob = buf[1];
		n = rc - buf[0];
		if(n > frsz - bufn) n = frsz - bufn;
		if(n > 0){
			memcpy(fbuf + bufn, buf + buf[0], n);
			bufn += n;
		}
			
	}
	qlock(&c->qulock);
	freeframes(&c->actl);
	freeframes(&c->freel);
	c->abort = 1;
	free(fbuf);
	closedev(c->ep);
	setalt(c->dev, c->iface);
	c->ep = nil;
	c->active = 0;
	c->abort = 0;
	qunlock(&c->qulock);
}

static Ep*
selbw(Cam *c, ProbeControl *pc, Ep *ep)
{
	uvlong bw, bw1, minbw;
	Format *fo;
	VSUncompressedFrame *f;
	Ep *mink;

	if(getframedesc(c, pc->bFormatIndex, pc->bFrameIndex, &fo, &f) < 0){
		werrstr("selaltc: PROBE_CONTROL returned invalid bFormatIndex,bFrameIndex=%d,%d",
			pc->bFormatIndex, pc->bFrameIndex);
		return nil;
	}
	mink = nil;
	minbw = ~0ULL;
	bw = (uvlong)GET2(f->wWidth) * GET2(f->wHeight) * fo->desc->bBitsPerPixel * 10e6 / GET4(c->pc.dwFrameInterval);
	for(;ep != nil; ep = ep->next){
		if(ep->iface->id != c->iface->id)
			continue;
		bw1 = ep->maxpkt * ep->ntds * 8 * 1000 * 8;
		if(bw1 >= bw && GET4(c->pc.dwMaxPayloadTransferSize) <= ep->maxpkt*ep->ntds) {
			if(mink == nil || bw1 < minbw){
				mink = ep;
				minbw = bw1;
			}
		}
	}
	if(mink == nil){
		werrstr("device does not have enough bandwidth (need %lld bit/s)", bw);
		return nil;
	}
	return mink;
}

static void
mkframes(Cam *c)
{
	int i;
	VSUncompressedFrame *f;
	int frsz;
	VFrame *v;

	assert(getframedesc(c, c->pc.bFormatIndex, c->pc.bFrameIndex, nil, &f) >= 0);
	frsz = GET2(f->wWidth) * GET2(f->wHeight) * 3;
	for(i = 0; i < NFrames; i++){
		v = emallocz(sizeof(VFrame) + 60 + frsz, 1);
		sprint((char*)&v[1], "%11s %11d %11d %11d %11d ", "b8g8r8", 0, 0, GET2(f->wWidth), GET2(f->wHeight));
		v->d = (uchar*)&v[1];
		v->sz = frsz;
		v->n = 60;
		v->next = c->freel;
		c->freel = v;
	}
}

int
videoopen(Cam *c, int fr)
{
	Dev *d;
	Ep *e;
	Format *f;

	qlock(&c->qulock);
	if(c->active){
		qunlock(&c->qulock);
		werrstr("already in use");
		return -1;
	}
	if(getframedesc(c, c->pc.bFormatIndex, c->pc.bFrameIndex, &f, nil) < 0){
err:
		qunlock(&c->qulock);
		return -1;
	}
	if(getconverter(f) == nil) goto err;
	d = c->dev;
	if(setalt(d, c->iface) < 0) goto err;
	if(usbcmd(d, 0x21, SET_CUR, VS_PROBE_CONTROL << 8, c->iface->id, (uchar *) &c->pc, sizeof(ProbeControl)) < sizeof(ProbeControl)) goto err;
	if(usbcmd(d, 0xA1, GET_CUR, VS_PROBE_CONTROL << 8, c->iface->id, (uchar *) &c->pc, sizeof(ProbeControl)) < 0) goto err;
	if(usbcmd(d, 0x21, SET_CUR, VS_COMMIT_CONTROL << 8, c->iface->id, (uchar *) &c->pc, sizeof(ProbeControl)) < sizeof(ProbeControl)) goto err;
	e = selbw(c, &c->pc, d->usb->ep[c->hdr->bEndpointAddress & Epmax]);
	if(e == nil || setalt(c->dev, e->iface) < 0)
		return -1;
	c->ep = openep(d, e);
	if(c->ep == nil){
		setalt(d, c->iface);
		goto err;
	}
	devctl(c->ep, "uframes 1");
	if(opendevdata(c->ep, OREAD) < 0){
		closedev(c->ep);
		setalt(d, c->iface);
		goto err;
	}
	mkframes(c);
	c->active = 1;
	c->framemode = fr;
	qunlock(&c->qulock);
	c->cvtid = proccreate(cvtproc, c, 16384);
	return 0;
}

void
videoclose(Cam *c)
{
	if(c->active == 0 || c->abort)
		return;
	c->abort = -1;
	threadint(c->cvtid);
}

void
videoread(Req *r, Cam *c, int lock)
{
	VFrame *v;
	int n;
	Req **rp;

	if(lock) qlock(&c->qulock);
	if(c->active == 0 || c->abort){
		if(lock) qunlock(&c->qulock);
		respond(r, "the front fell off");
		return;
	}
	if(c->framemode == 2){
		c->framemode = 1;
		if(lock) qunlock(&c->qulock);
		r->ofcall.count = 0;
		respond(r, nil);
		return;
	}
	if(c->actl == nil){
		for(rp = &c->delreq; *rp != nil; rp = (Req**)&(*rp)->qu.next)
			;
		r->qu.next = nil;
		*rp = r;
		if(lock) qunlock(&c->qulock);
		return;
	}
	v = c->actl;
	n = v->n - v->p;
	if(n > r->ifcall.count) n = r->ifcall.count;
	memcpy(r->ofcall.data, v->d + v->p, n);
	v->p += n;
	if(v->p == v->n){
		if(c->framemode)
			c->framemode = 2;
		c->actl = v->next;
		v->next = c->freel;
		c->freel = v;
	}
	if(lock) qunlock(&c->qulock);
	r->ofcall.count = n;
	respond(r, nil);
}

void
videoflush(Req *r, Cam *c)
{
	Req **rp;

	qlock(&c->qulock);
	for(rp = &c->delreq; *rp != nil; rp = (Req**)&(*rp)->qu.next)
		if(*rp == r){
			*rp = (Req *) r->qu.next;
			respond(r, "interrupted");
			break;
		}
	qunlock(&c->qulock);
}
