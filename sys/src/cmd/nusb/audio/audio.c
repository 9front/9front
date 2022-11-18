#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include "usb.h"

enum {
	Rgetcur	= 0x81,
	Rgetmin	= 0x82,
	Rgetmax	= 0x83,
	Rgetres	= 0x84,
	Rsetcur	= 0x01,
	Rsetmin	= 0x02,
	Rsetmax	= 0x03,
	Rsetres	= 0x04,
};

typedef struct Range Range;
struct Range
{
	uint	min;
	uint	max;
};

typedef struct Aconf Aconf;
struct Aconf
{
	Ep	*ep;
	int	bits;
	int	bps;	/* subslot size (bytes per sample) */
	int	format;
	int	channels;
	int	controls;
	Range	*freq;
	int	nfreq;
};

int audiodelay = 1764;	/* 40 ms */
int audiofreq = 44100;
int audiochan = 2;
int audiores = 16;

char user[] = "audio";

Dev *audiodev = nil;
Iface *audiocontrol = nil;
Ep *audioepin = nil;
Ep *audioepout = nil;

Iface*
findiface(Conf *conf, int class, int subclass, int id)
{
	int i;
	Iface *iface;

	for(i = 0; i < nelem(conf->iface); i++){
		iface = conf->iface[i];
		if(iface == nil || Class(iface->csp) != class || Subclass(iface->csp) != subclass)
			continue;
		if(id == -1 || iface->id == id)
			return iface;
	}
	return nil;
}

Desc*
findacheader(Usbdev *u, Iface *ac)
{
	Desc *dd;
	uchar *b;
	int i;

	for(i = 0; i < nelem(u->ddesc); i++){
		dd = u->ddesc[i];
		if(dd == nil || dd->iface != ac || dd->data.bDescriptorType != 0x24)
			continue;
		if(dd->data.bLength < 8 || dd->data.bbytes[0] != 1)
			continue;
		b = dd->data.bbytes;
		if(dd->data.bLength == 8+b[5])
			return dd;
	}
	return nil;
}

void
parseasdesc(Desc *dd, Aconf *c)
{
	uchar *b;
	Range *f;

	b = dd->data.bbytes;
	switch(dd->data.bDescriptorType<<8 | b[0]){
	case 0x2501:	/* CS_ENDPOINT, EP_GENERAL */
		if(dd->data.bLength != 7)
			return;
		c->controls = b[1];
		break;

	case 0x2401:	/* CS_INTERFACE, AS_GENERAL */
		if(dd->data.bLength != 7)
			return;
		c->format = GET2(&b[3]);
		break;

	case 0x2402:	/* CS_INTERFACE, FORMAT_TYPE */
		if(dd->data.bLength < 8 || b[1] != 1)
			return;
		c->channels = b[2];
		c->bps = b[3];
		c->bits = b[4];
		if(b[5] == 0){	/* continuous frequency range */
			c->nfreq = 1;
			c->freq = emallocz(sizeof(*f), 1);
			c->freq->min = b[6] | b[7]<<8 | b[8]<<16;
			c->freq->max = b[9] | b[10]<<8 | b[11]<<16;
		}else{		/* discrete sampling frequencies */
			c->nfreq = b[5];
			c->freq = emallocz(c->nfreq * sizeof(*f), 1);
			b += 6;
			for(f = c->freq; f < c->freq+c->nfreq; f++, b += 3){
				f->min = b[0] | b[1]<<8 | b[2]<<16;
				f->max = f->min;
			}
		}
		break;
	}
}

void
parsestream(Dev *d, int id)
{
	Iface *as;
	Desc *dd;
	Ep *e;
	Aconf *c;
	int i;

	/* find AS interface */
	as = findiface(d->usb->conf[0], Claudio, 2, id);

	/* enumerate through alt. settings */
	for(; as != nil; as = as->next){
		c = emallocz(sizeof(*c), 1);
		as->aux = c;

		/* find AS endpoint */
		for(i = 0; i < nelem(as->ep); i++){
			e = as->ep[i];
			if(e != nil && e->type == Eiso && (e->attrib>>4 & 3) == Edata){
				c->ep = e;
				break;
			}
		}
		if(c->ep == nil){
			free(c);
			as->aux = nil;
			continue;
		}

		/* parse AS descriptors */
		for(i = 0; i < nelem(d->usb->ddesc); i++){
			dd = d->usb->ddesc[i];
			if(dd == nil || dd->iface != as)
				continue;
			parseasdesc(dd, c);
		}
	}
}

Dev*
setupep(Dev *d, Ep *e, int speed)
{
	int dir = e->dir;
	Aconf *c;
	Range *f;

	for(;e != nil; e = e->next){
		c = e->iface->aux;
		if(c == nil || e != c->ep || e->dir != dir)
			continue;
		if(c->format != 1 || c->bits != audiores || 8*c->bps != audiores || c->channels != audiochan)
			continue;
		for(f = c->freq; f != c->freq+c->nfreq; f++)
			if(speed >= f->min && speed <= f->max)
				goto Foundaltc;
	}
	werrstr("no altc found");
	return nil;

Foundaltc:
	if(setalt(d, e->iface) < 0)
		return nil;

	if(c->controls & 1){
		uchar b[4];

		b[0] = speed;
		b[1] = speed >> 8;
		b[2] = speed >> 16;
		if(usbcmd(d, Rh2d|Rclass|Rep, Rsetcur, 0x100, (e->dir==Ein?0x80:0)|(e->id&Epmax), b, 3) < 0)
			fprint(2, "warning: set freq: %r\n");
	}

	if((d = openep(d, e)) == nil){
		werrstr("openep: %r");
		return nil;
	}
	devctl(d, "samplesz %d", audiochan*audiores/8);
	devctl(d, "sampledelay %d", audiodelay);
	devctl(d, "hz %d", speed);
	if(e->dir==Ein)
		devctl(d, "name audioin");
	else
		devctl(d, "name audio");
	return d;
}

void
fsread(Req *r)
{
	char *msg;

	msg = smprint("master 100 100\nspeed %d\ndelay %d\n", audiofreq, audiodelay);
	readstr(r, msg);
	respond(r, nil);
	free(msg);
}

void
fswrite(Req *r)
{
	char msg[256], *f[4];
	int nf, speed;

	snprint(msg, sizeof(msg), "%.*s",
		utfnlen((char*)r->ifcall.data, r->ifcall.count), (char*)r->ifcall.data);
	nf = tokenize(msg, f, nelem(f));
	if(nf < 2){
		respond(r, "invalid ctl message");
		return;
	}
	if(strcmp(f[0], "speed") == 0){
		Dev *d;

		speed = atoi(f[1]);
Setup:
		if((d = setupep(audiodev, audioepout, speed)) == nil){
			responderror(r);
			return;
		}
		closedev(d);
		if(audioepin != nil)
			if(d = setupep(audiodev, audioepin, speed))
				closedev(d);
		audiofreq = speed;
	} else if(strcmp(f[0], "delay") == 0){
		audiodelay = atoi(f[1]);
		speed = audiofreq;
		goto Setup;
	}
	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
}

Srv fs = {
	.read = fsread,
	.write = fswrite,
};

void
usage(void)
{
	fprint(2, "%s devid\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	char buf[32];
	Dev *d, *ed;
	Desc *dd;
	Conf *conf;
	Iface *ac;
	Aconf *c;
	Ep *e;
	int i;

	ARGBEGIN {
	case 'D':
		chatty9p++;
		break;
	case 'd':
		usbdebug++;
		break;
	} ARGEND;

	if(argc == 0)
		usage();

	if((d = getdev(*argv)) == nil)
		sysfatal("getdev: %r");
	audiodev = d;

	conf = d->usb->conf[0];
	ac = findiface(conf, Claudio, 1, -1);
	if(ac == nil)
		sysfatal("no audio control interface");
	audiocontrol = ac;

	dd = findacheader(d->usb, ac);
	if(dd == nil)
		sysfatal("no audio control header");
	for(i = 6; i < dd->data.bLength-2; i++)
		parsestream(d, dd->data.bbytes[i]);

	for(i = 0; i < nelem(d->usb->ep); i++){
		for(e = d->usb->ep[i]; e != nil; e = e->next){
			c = e->iface->aux;
			if(c != nil && c->ep == e)
				break;
		}
		if(e == nil)
			continue;
		switch(e->dir){
		case Ein:
			if(audioepin != nil)
				continue;
			audioepin = e;
			break;
		case Eout:
			if(audioepout != nil)
				continue;
			audioepout = e;
			break;
		}
		if((ed = setupep(audiodev, e, audiofreq)) == nil){
			fprint(2, "setupep: %r\n");

			if(e == audioepin)
				audioepin = nil;
			if(e == audioepout)
				audioepout = nil;
			continue;
		}
		closedev(ed);
	}
	if(audioepout == nil)
		sysfatal("no output stream found");

	fs.tree = alloctree(user, "usb", DMDIR|0555, nil);
	createfile(fs.tree->root, "volume", user, 0666, nil);

	snprint(buf, sizeof buf, "%d.audio", audiodev->id);
	postsharesrv(&fs, nil, "usb", buf);

	exits(0);
}
