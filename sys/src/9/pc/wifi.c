#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "pool.h"
#include "ureg.h"
#include "../port/error.h"
#include "../port/netif.h"

#include "etherif.h"
#include "wifi.h"

typedef struct SNAP SNAP;
struct SNAP
{
	uchar	dsap;
	uchar	ssap;
	uchar	control;
	uchar	orgcode[3];
	uchar	type[2];
};

enum {
	SNAPHDRSIZE = 8,
};

static char Snone[] = "new";
static char Sconn[] = "connecting";
static char Sauth[] = "authenticated";
static char Sunauth[] = "unauthentictaed";
static char Sassoc[] = "associated";
static char Sunassoc[] = "unassociated";

void
wifiiq(Wifi *wifi, Block *b)
{
	SNAP s;
	Wifipkt w;
	Etherpkt *e;

	if(BLEN(b) < WIFIHDRSIZE)
		goto drop;
	memmove(&w, b->rp, WIFIHDRSIZE);
	switch(w.fc[0] & 0x0c){
	case 0x00:	/* management */
		if((w.fc[1] & 3) != 0x00)	/* STA->STA */
			break;
		qpass(wifi->iq, b);
		return;
	case 0x04:	/* control */
		break;
	case 0x08:	/* data */
		b->rp += WIFIHDRSIZE;
		switch(w.fc[0] & 0xf0){
		case 0x80:
			b->rp += 2;
			if(w.fc[1] & 0x80)
				b->rp += 4;
		case 0x00:
			break;
		default:
			goto drop;
		}
		if(BLEN(b) < SNAPHDRSIZE || b->rp[0] != 0xAA || b->rp[1] != 0xAA || b->rp[2] != 0x03)
			break;
		memmove(&s, b->rp, SNAPHDRSIZE);
		b->rp += SNAPHDRSIZE-ETHERHDRSIZE;
		e = (Etherpkt*)b->rp;
		switch(w.fc[1] & 0x03){
		case 0x00:	/* STA->STA */
			memmove(e->d, w.a1, Eaddrlen);
			memmove(e->s, w.a2, Eaddrlen);
			break;
		case 0x01:	/* STA->AP */
			memmove(e->d, w.a3, Eaddrlen);
			memmove(e->s, w.a2, Eaddrlen);
			break;
		case 0x02:	/* AP->STA */
			memmove(e->d, w.a1, Eaddrlen);
			memmove(e->s, w.a3, Eaddrlen);
			break;
		case 0x03:	/* AP->AP */
			goto drop;
		}
		memmove(e->type, s.type, 2);
		etheriq(wifi->ether, b, 1);
		return;
	}
drop:
	freeb(b);
}

static void
wifitx(Wifi *wifi, Block *b)
{
	Wifipkt *w;
	uint seq;

	seq = wifi->txseq++;
	seq <<= 4;

	w = (Wifipkt*)b->rp;
	w->dur[0] = 0;
	w->dur[1] = 0;
	w->seq[0] = seq;
	w->seq[1] = seq>>8;

	(*wifi->transmit)(wifi, wifi->bss, b);
}


static Wnode*
nodelookup(Wifi *wifi, uchar *bssid, int new)
{
	Wnode *wn, *nn;

	if(memcmp(bssid, wifi->ether->bcast, Eaddrlen) == 0)
		return nil;
	for(wn = nn = wifi->node; wn != &wifi->node[nelem(wifi->node)]; wn++){
		if(memcmp(wn->bssid, bssid, Eaddrlen) == 0){
			wn->lastseen = MACHP(0)->ticks;
			return wn;
		}
		if(wn != wifi->bss && wn->lastseen < nn->lastseen)
			nn = wn;
	}
	if(!new)
		return nil;
	memmove(nn->bssid, bssid, Eaddrlen);
	nn->lastseen = MACHP(0)->ticks;
	return nn;
}

static void
sendauth(Wifi *wifi, Wnode *bss)
{
	Wifipkt *w;
	Block *b;
	uchar *p;

	b = allocb(WIFIHDRSIZE + 3*2);
	w = (Wifipkt*)b->wp;
	w->fc[0] = 0xB0;	/* auth request */
	w->fc[1] = 0x00;	/* STA->STA */
	memmove(w->a1, bss->bssid, Eaddrlen);	/* ??? */
	memmove(w->a2, wifi->ether->ea, Eaddrlen);
	memmove(w->a3, bss->bssid, Eaddrlen);
	b->wp += WIFIHDRSIZE;
	p = b->wp;
	*p++ = 0;	/* alg */
	*p++ = 0;
	*p++ = 1;	/* seq */
	*p++ = 0;
	*p++ = 0;	/* status */
	*p++ = 0;
	b->wp = p;
	wifitx(wifi, b);
}

static void
sendassoc(Wifi *wifi, Wnode *bss)
{
	Wifipkt *w;
	Block *b;
	uchar *p;

	b = allocb(WIFIHDRSIZE + 128);
	w = (Wifipkt*)b->wp;
	w->fc[0] = 0x00;	/* assoc request */
	w->fc[1] = 0x00;	/* STA->STA */
	memmove(w->a1, bss->bssid, Eaddrlen);	/* ??? */
	memmove(w->a2, wifi->ether->ea, Eaddrlen);
	memmove(w->a3, bss->bssid, Eaddrlen);
	b->wp += WIFIHDRSIZE;
	p = b->wp;
	*p++ = 1;	/* capinfo */
	*p++ = 0;
	*p++ = 16;	/* interval */
	*p++ = 16>>8;
	*p++ = 0;	/* SSID */
	*p = strlen(bss->ssid);
	memmove(p+1, bss->ssid, *p);
	p += 1+*p;
	*p++ = 1;	/* RATES (BUG: these are all lies!) */
	*p++ = 4;
	*p++ = 0x82;
	*p++ = 0x84;
	*p++ = 0x8b;
	*p++ = 0x96;
	b->wp = p;
	wifitx(wifi, b);
}

static void
recvassoc(Wifi *wifi, Wnode *wn, uchar *d, int len)
{
	uint s;

	if(len < 2+2+2)
		return;

	d += 2;	/* caps */
	s = d[0] | d[1]<<8;
	d += 2;
	switch(s){
	case 0x00:
		wn->aid = d[0] | d[1]<<8;
		wifi->status = Sassoc;
		break;
	default:
		wifi->status = Sunassoc;
		return;
	}
}

static void
recvbeacon(Wifi *wifi, Wnode *wn, uchar *d, int len)
{
	uchar *e, *x;
	uchar t, m[256/8];

	if(len < 8+2+2)
		return;

	d += 8;	/* timestamp */
	wn->ival = d[0] | d[1]<<8;
	d += 2;
	wn->cap = d[0] | d[1]<<8;
	d += 2;

	memset(m, 0, sizeof(m));
	for(e = d + len; d+2 <= e; d = x){
		d += 2;
		x = d + d[-1];
		t = d[-2];

		/* skip double entries */
		if(m[t/8] & 1<<(t%8))
			continue;
		m[t/8] |= 1<<(t%8);

		switch(t){
		case 0:	/* SSID */
			len = 0;
			while(len < 32 && d+len < x && d[len] != 0)
				len++;
			if(len == 0)
				continue;
			if(len != strlen(wn->ssid) || strncmp(wn->ssid, (char*)d, len) != 0){
				strncpy(wn->ssid, (char*)d, len);
				wn->ssid[len] = 0;
				if(wifi->bss == nil && strcmp(wifi->essid, wn->ssid) == 0){
					wifi->bss = wn;
					wifi->status = Sconn;
					sendauth(wifi, wn);
				}
			}
			break;
		case 3:	/* DSPARAMS */
			if(d != x)
				wn->channel = d[0];
			break;
		}
	}
}

static void
wifiproc(void *arg)
{
	Wifi *wifi;
	Wifipkt *w;
	Wnode *wn;
	Block *b;

	b = nil;
	wifi = arg;
	for(;;){
		if(b != nil)
			freeb(b);
		if((b = qbread(wifi->iq, 100000)) == nil)
			break;
		w = (Wifipkt*)b->rp;
		switch(w->fc[0] & 0xf0){
		case 0x50:	/* probe response */
		case 0x80:	/* beacon */
			if((wn = nodelookup(wifi, w->a3, 1)) == nil)
				continue;
			b->rp += WIFIHDRSIZE;
			recvbeacon(wifi, wn, b->rp, BLEN(b));
			continue;
		}
		if((wn = nodelookup(wifi, w->a3, 0)) == nil)
			continue;
		if(wn != wifi->bss)
			continue;
		switch(w->fc[0] & 0xf0){
		case 0x10:	/* assoc response */
		case 0x30:	/* reassoc response */
			b->rp += WIFIHDRSIZE;
			recvassoc(wifi, wn, b->rp, BLEN(b));
			break;
		case 0xb0:	/* auth */
			wifi->status = Sauth;
			sendassoc(wifi, wn);
			break;
		case 0xc0:	/* deauth */
			wifi->status = Sunauth;
			break;
		}
	}
	pexit("wifi in queue closed", 0);
}

static void
wifietheroq(Wifi *wifi, Block *b)
{
	Etherpkt e;
	Wifipkt *w;
	SNAP *s;

	if(BLEN(b) < ETHERHDRSIZE){
		freeb(b);
		return;
	}
	memmove(&e, b->rp, ETHERHDRSIZE);

	b->rp += ETHERHDRSIZE;
	b = padblock(b, WIFIHDRSIZE + SNAPHDRSIZE);

	w = (Wifipkt*)b->rp;
	w->fc[0] = 0x08;	/* data */
	w->fc[1] = 0x01;	/* STA->AP */
	memmove(w->a1, wifi->bss ? wifi->bss->bssid : wifi->ether->bcast, Eaddrlen);
	memmove(w->a2, e.s, Eaddrlen);
	memmove(w->a3, e.d, Eaddrlen);

	s = (SNAP*)(b->rp + WIFIHDRSIZE);
	s->dsap = s->ssap = 0xAA;
	s->control = 0x03;
	s->orgcode[0] = 0;
	s->orgcode[1] = 0;
	s->orgcode[2] = 0;
	memmove(s->type, e.type, 2);

	wifitx(wifi, b);
}

static void
wifoproc(void *arg)
{
	Ether *ether;
	Wifi *wifi;
	Block *b;

	wifi = arg;
	ether = wifi->ether;
	while((b = qbread(ether->oq, 1000000)) != nil)
		wifietheroq(wifi, b);
	pexit("ether out queue closed", 0);
}

Wifi*
wifiattach(Ether *ether, void (*transmit)(Wifi*, Wnode*, Block*))
{
	Wifi *wifi;

	wifi = malloc(sizeof(Wifi));
	wifi->ether = ether;
	wifi->iq = qopen(8*1024, 0, 0, 0);
	wifi->transmit = transmit;
	wifi->status = Snone;

	kproc("wifi", wifiproc, wifi);
	kproc("wifo", wifoproc, wifi);

	return wifi;
}

long
wifictl(Wifi *wifi, void *buf, long n)
{
	Cmdbuf *cb;
	Wnode *wn;

	cb = nil;
	if(waserror()){
		free(cb);
		nexterror();
	}
	cb = parsecmd(buf, n);
	if(cb->f[0] && strcmp(cb->f[0], "essid") == 0){
		if(cb->f[1] == nil){
			/* TODO senddeauth(wifi); */
			wifi->essid[0] = 0;
			wifi->bss = nil;
		} else {
			strncpy(wifi->essid, cb->f[1], 32);
			wifi->essid[32] = 0;
			for(wn=wifi->node; wn != &wifi->node[nelem(wifi->node)]; wn++)
				if(strcmp(wifi->essid, wn->ssid) == 0){
					wifi->bss = wn;
					wifi->status = Sconn;
					sendauth(wifi, wn);
					break;
				}
		}
	}
	poperror();
	free(cb);
	return n;
}

long
wifistat(Wifi *wifi, void *buf, long n, ulong off)
{
	static uchar zeros[Eaddrlen];
	char *s, *p, *e;
	Wnode *wn;
	long now;

	p = s = smalloc(4096);
	e = s + 4096;

	p = seprint(p, e, "status: %s\n", wifi->status);
	p = seprint(p, e, "essid: %s\n", wifi->essid);
	p = seprint(p, e, "bssid: %E\n", wifi->bss ? wifi->bss->bssid : zeros);

	now = MACHP(0)->ticks;
	for(wn=wifi->node; wn != &wifi->node[nelem(wifi->node)]; wn++){
		if(wn->lastseen == 0)
			continue;
		p = seprint(p, e, "node: %E %.4x %d %ld %d %s\n",
			wn->bssid, wn->cap, wn->ival, TK2MS(now - wn->lastseen), wn->channel, wn->ssid);
	}
	n = readstr(off, buf, n, s);
	free(s);
	return n;
}
