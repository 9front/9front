#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include "usb.h"

enum {
	Paudio1 = 0x00,
	Paudio2 = 0x20,

	Csamfreq = 0x01,

	/* audio 1 */
	Rsetcur	= 0x01,

	/* audio 2 */
	Rcur = 0x01,
	Rrange = 0x02,
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
	int	terminal;
	Range	*freq;
	int	nfreq;

	/* audio 1 */
	int	controls;

	/* audio 2 */
	int	clock;
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
findiad(Usbdev *u, int id, int csp)
{
	int i;
	Desc *dd;
	uchar *b;

	for(i = 0; i < nelem(u->ddesc); i++){
		dd = u->ddesc[i];
		if(dd == nil || dd->data.bDescriptorType != 11 || dd->data.bLength != 8)
			continue;
		b = dd->data.bbytes;
		if(b[0] == id && b[0]+b[1] <= Niface && csp == CSP(b[2], b[3], b[4]))
			return dd;
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
		switch(Proto(ac->csp)){
		case Paudio1:
			if(dd->data.bLength == 8+b[5])
				return dd;
			break;
		case Paudio2:
			if(dd->data.bLength == 9)
				return dd;
			break;
		}
	}
	return nil;
}

Desc*
findterminal(Usbdev *u, Iface *ac, int id)
{
	Desc *dd;
	uchar *b;
	int i;

	for(i = 0; i < nelem(u->ddesc); i++){
		dd = u->ddesc[i];
		if(dd == nil || dd->iface != ac)
			continue;
		if(dd->data.bDescriptorType != 0x24 || dd->data.bLength < 4)
			continue;
		b = dd->data.bbytes;
		if(b[1] != id)
			continue;
		/* check descriptor length according to type and proto */
		switch(b[0]<<16 | dd->data.bLength<<8 | Proto(ac->csp)){
		case 0x020C00|Paudio1:
		case 0x030900|Paudio1:
		case 0x021100|Paudio2:
		case 0x030c00|Paudio2:
			return dd;
		}
	}
	return nil;
}

Desc*
findclocksource(Usbdev *u, Iface *ac, int id)
{
	Desc *dd;
	uchar *b;
	int i;

	for(i = 0; i < nelem(u->ddesc); i++){
		dd = u->ddesc[i];
		if(dd == nil || dd->iface != ac)
			continue;
		if(dd->data.bDescriptorType != 0x24 || dd->data.bLength != 8)
			continue;
		b = dd->data.bbytes;
		if(b[0] == 0x0A && b[1] == id)
			return dd;
	}
	return nil;
}

void
parseasdesc1(Desc *dd, Aconf *c)
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
		c->terminal = b[1];
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
parseasdesc2(Desc *dd, Aconf *c)
{
	uchar *b;

	b = dd->data.bbytes;
	switch(dd->data.bDescriptorType<<8 | b[0]){
	case 0x2401:	/* CS_INTERFACE, AS_GENERAL */
		if(dd->data.bLength != 16 || b[3] != 1)
			return;
		c->terminal = b[1];
		c->format = GET4(&b[4]);
		c->channels = b[8];
		break;

	case 0x2402:	/* CS_INTERFACE, FORMAT_TYPE */
		if(dd->data.bLength != 6 || b[1] != 1)
			return;
		c->bps = b[2];
		c->bits = b[3];
		break;
	}
}

int
setclock(Dev *d, Iface *ac, Aconf *c, int speed)
{
	uchar b[4];
	int index;

	switch(Proto(ac->csp)){
	case Paudio1:
		if((c->controls & 1) == 0)
			break;
		b[0] = speed;
		b[1] = speed >> 8;
		b[2] = speed >> 16;
		index = c->ep->id & Epmax;
		if(c->ep->dir == Ein)
			index |= 0x80;
		return usbcmd(d, Rh2d|Rclass|Rep, Rsetcur, Csamfreq<<8, index, b, 3);
	case Paudio2:
		PUT4(b, speed);
		index = c->clock<<8 | ac->id;
		return usbcmd(d, Rh2d|Rclass|Riface, Rcur, Csamfreq<<8, index, b, 4);
	}
	return 0;
}

int
getclockrange(Dev *d, Iface *ac, Aconf *c)
{
	uchar b[2 + 32*12];
	int i, n, rc;

	rc = usbcmd(d, Rd2h|Rclass|Riface, Rrange, Csamfreq<<8, c->clock<<8 | ac->id, b, sizeof(b));
	if(rc < 0)
		return -1;
	if(rc < 2 || rc < 2 + (n = GET2(b))*12){
		werrstr("invalid response");
		return -1;
	}
	c->freq = emallocz(n, sizeof(Range));
	c->nfreq = n;
	for(i = 0; i < n; i++)
		c->freq[i] = (Range){GET4(&b[2 + i*12]), GET4(&b[6 + i*12])};
	return 0;
}

void
parsestream(Dev *d, Iface *ac, int id)
{
	Iface *as;
	Desc *dd;
	Ep *e;
	Aconf *c;
	uchar *b;
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
		Skip:
			free(c);
			as->aux = nil;
			continue;
		}

		/* parse AS descriptors */
		for(i = 0; i < nelem(d->usb->ddesc); i++){
			dd = d->usb->ddesc[i];
			if(dd == nil || dd->iface != as)
				continue;
			switch(Proto(ac->csp)){
			case Paudio1:
				parseasdesc1(dd, c);
				break;
			case Paudio2:
				parseasdesc2(dd, c);
				break;
			}
		}

		if(Proto(ac->csp) == Paudio1)
			continue;

		dd = findterminal(d->usb, ac, c->terminal);
		if(dd == nil)
			goto Skip;
		b = dd->data.bbytes;
		switch(b[0]){
		case 0x02:	/* INPUT_TERMINAL */
			c->clock = b[5];
			break;
		case 0x03:	/* OUTPUT_TERMINAL */
			c->clock = b[6];
			break;
		}

		dd = findclocksource(d->usb, ac, c->clock);
		if(dd == nil)
			goto Skip;
		b = dd->data.bbytes;
		/* check that clock has rw frequency control */
		if((b[3] & 3) != 3)
			goto Skip;
		if(getclockrange(d, ac, c) != 0){
			fprint(2, "getclockrange %d: %r", c->clock);
			goto Skip;
		}
	}
}

Dev*
setupep(Dev *d, Iface *ac, Ep *e, int *speed, int force)
{
	int dir = e->dir;
	Aconf *c, *bestc;
	Range *f;
	Ep *beste;
	int closest, sp;

	bestc = nil;
	beste = nil;
	closest = 1<<30;
	sp = *speed;

	for(;e != nil; e = e->next){
		c = e->iface->aux;
		if(c == nil || e != c->ep || e->dir != dir)
			continue;
		if(c->format != 1 || c->bits != audiores || 8*c->bps != audiores || c->channels != audiochan)
			continue;
		for(f = c->freq; f != c->freq+c->nfreq; f++){
			if(sp >= f->min && sp <= f->max)
				goto Foundaltc;
			if(force)
				continue;
			if(f->min >= sp && closest-sp >= f->min-sp){
				closest = f->min;
				bestc = c;
				beste = e;
			}else if(bestc == nil || (f->max < sp && closest < sp && sp-closest > sp-f->max)){
				closest = f->max;
				bestc = c;
				beste = e;
			}
		}
	}
	if(bestc == nil){
		werrstr("no altc found");
		return nil;
	}
	e = beste;
	c = bestc;
	sp = closest;

Foundaltc:
	if(setalt(d, e->iface) < 0){
		fprint(2, "setalt: %r\n");
		return nil;
	}
	if(setclock(d, ac, c, sp) < 0){
		werrstr("setclock: %r");
		return nil;
	}
	if((d = openep(d, e)) == nil){
		werrstr("openep: %r");
		return nil;
	}
	devctl(d, "samplesz %d", audiochan*audiores/8);
	devctl(d, "sampledelay %d", audiodelay);
	devctl(d, "hz %d", sp);
	devctl(d, "name audio%sU%s", e->dir==Ein ? "in" : "", audiodev->hname);
	*speed = sp;
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
		if((d = setupep(audiodev, audiocontrol, audioepout, &speed, 1)) == nil){
			responderror(r);
			return;
		}
		audiofreq = speed;
		closedev(d);
		if(audioepin != nil)
			if(d = setupep(audiodev, audiocontrol, audioepin, &speed, 1))
				closedev(d);
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
	uchar *b;
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

	switch(Proto(ac->csp)){
	case Paudio1:
		dd = findacheader(d->usb, ac);
		if(dd == nil)
			sysfatal("no audio control header");
		b = dd->data.bbytes;
		for(i = 6; i < dd->data.bLength-2; i++)
			parsestream(d, ac, b[i]);
		break;
	case Paudio2:
		dd = findiad(d->usb, ac->id, CSP(Claudio, 0, Paudio2));
		if(dd == nil)
			sysfatal("no audio function");
		b = dd->data.bbytes;
		for(i = b[0]+1; i < b[0]+b[1]; i++)
			parsestream(d, ac, i);
		break;
	}

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
		if((ed = setupep(d, ac, e, &audiofreq, 0)) == nil){
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
	snprint(buf, sizeof buf, "volumeU%s", audiodev->hname);
	createfile(fs.tree->root, buf, user, 0666, nil);

	snprint(buf, sizeof buf, "%d.audio", audiodev->id);
	postsharesrv(&fs, nil, "usb", buf);

	exits(0);
}
