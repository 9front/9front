#include <u.h>
#include <libc.h>
#include <thread.h>
#include "usb.h"

int audiofreq = 44100;
int audiochan = 2;
int audiores = 16;

char validaltc[] = "";

void
parsedescr(Desc *dd)
{
	uchar *b;
	int i;

	if(dd == nil || dd->iface == nil || dd->altc == nil)
		return;
	b = (uchar*)&dd->data;
	if(Subclass(dd->iface->csp) != 2 || b[1] != 0x24 || b[2] != 0x02)
		return;
	if(b[4] != audiochan)
		return;
	if(b[6] != audiores)
		return;
	if(b[7] == 0){
		int minfreq, maxfreq;

		minfreq = b[8] | b[9]<<8 | b[10]<<16;
		maxfreq = b[11] | b[12]<<8 | b[13]<<16;
		if(minfreq > audiofreq || maxfreq < audiofreq)
			return;
	} else {
		int freq;

		for(i=0; i<b[7]; i++){
			freq = b[8+3*i] | b[9+3*i]<<8 | b[10+3*i]<<16;
			if(freq == audiofreq)
				break;
		}
		if(i == b[7])
			return;
	}
	dd->altc->aux = validaltc;
}

void
usage(void)
{
	fprint(2, "%s devid\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	Dev *d, *ed;
	Usbdev *ud;
	uchar b[4];
	Altc *a;
	Ep *e;
	int i;

	ARGBEGIN {
	} ARGEND;

	if(argc == 0)
		usage();

	if((d = getdev(atoi(*argv))) == nil)
		sysfatal("getdev: %r");
	if(configdev(d) < 0)
		sysfatal("configdev: %r");

	ud = d->usb;

	/* parse descriptors, mark valid altc */
	for(i = 0; i < nelem(ud->ddesc); i++)
		parsedescr(ud->ddesc[i]);

	for(i = 0; i < nelem(ud->ep); i++){
		e = ud->ep[i];
		if(e && e->iface && e->iface->csp == CSP(Claudio, 2, 0) && e->dir == Eout)
			goto Foundendp;
	}
	sysfatal("no endpoints found");
	return;

Foundendp:
	for(i = 0; i < nelem(e->iface->altc); i++){
		a = e->iface->altc[i];
		if(a && a->aux == validaltc)
			goto Foundaltc;
	}
	sysfatal("no altc found");

Foundaltc:
	if(usbcmd(d, Rh2d|Rstd|Riface, Rsetiface, i, e->iface->id, nil, 0) < 0)
		sysfatal("usbcmd: set altc: %r");

	b[0] = audiofreq;
	b[1] = audiofreq >> 8;
	b[2] = audiofreq >> 16;
	if(usbcmd(d, Rh2d|Rclass|Rep, Rsetcur, 0x100, e->addr, b, 3) < 0)
		fprint(2, "warning: set freq: %r");

	if((ed = openep(d, e->id)) == nil)
		sysfatal("openep: %r");
	devctl(ed, "pollival %d", 1);
	devctl(ed, "samplesz %d", audiochan*audiores/8);
	devctl(ed, "hz %d", audiofreq);

	/* rename endpoint to #u/audio */
	devctl(ed, "name audio");

	exits(0);
}
