/*
 * generic CDC
 */

#include <u.h>
#include <libc.h>
#include <thread.h>
#include <bio.h>
#include <auth.h>
#include <fcall.h>
#include <9p.h>

#include "usb.h"
#include "dat.h"

static int
str2mac(uchar *m, char *s)
{
	int i;

	if(strlen(s) != 12)
		return -1;

	for(i=0; i<12; i++){
		uchar v;

		if(s[i] >= 'A' && s[i] <= 'F'){
			v = 10 + s[i] - 'A';
		} else if(s[i] >= 'a' && s[i] <= 'f'){
			v = 10 + s[i] - 'a';
		} else if(s[i] >= '0' && s[i] <= '9'){
			v = s[i] - '0';
		} else {
			v = 0;
		}
		if(i&1){
			m[i/2] |= v;
		} else {
			m[i/2] = v<<4;
		}
	}
	return 0;
}

static int
cdcread(Dev *ep, uchar *p, int n)
{
	return read(ep->dfd, p, n);
}

static void
cdcwrite(Dev *ep, uchar *p, int n)
{
	if(write(ep->dfd, p, n) < 0){
		fprint(2, "cdcwrite: %r\n");
	} else {
		/*
		 * this may not work with all CDC devices. the
		 * linux driver sends one more random byte
		 * instead of a zero byte transaction. maybe we
		 * should do the same?
		 */
		if(n % ep->maxpkt == 0)
			write(ep->dfd, "", 0);
	}
}
int
cdcinit(Dev *d)
{
	int i;
	Usbdev *ud;
	uchar *b;
	Desc *dd;
	char *mac;

	ud = d->usb;
	for(i = 0; i < nelem(ud->ddesc); i++)
		if((dd = ud->ddesc[i]) != nil){
			b = (uchar*)&dd->data;
			if(b[1] == Dfunction && b[2] == Fnether){
				mac = loaddevstr(d, b[3]);
				if(mac != nil && strlen(mac) != 12){
					free(mac);
					mac = nil;
				}
				if(mac != nil){
					str2mac(macaddr, mac);
					free(mac);

					epread = cdcread;
					epwrite = cdcwrite;

					return 0;
				}
			}
		}
	return -1;
}
