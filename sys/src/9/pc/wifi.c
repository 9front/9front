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

#include <libsec.h>

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
	WIFIHDRSIZE = 2+2+3*6+2,
	SNAPHDRSIZE = 8,
};

static char Snone[] = "new";
static char Sconn[] = "connecting";
static char Sauth[] = "authenticated";
static char Sunauth[] = "unauthenticated";
static char Sassoc[] = "associated";
static char Sunassoc[] = "unassociated";
static char Sblocked[] = "blocked";	/* no keys negotiated. only pass EAPOL frames */

static Block* wifidecrypt(Wifi *, Wnode *, Block *);
static Block* wifiencrypt(Wifi *, Wnode *, Block *);

static uchar*
srcaddr(Wifipkt *w)
{
	if((w->fc[1] & 3) == 0x02)
		return w->a3;
	else
		return w->a2;
}
static uchar*
dstaddr(Wifipkt *w)
{
	if((w->fc[1] & 3) == 0x01)
		return w->a3;
	else
		return w->a1;
}

static int
wifihdrlen(Wifipkt *w)
{
	int n;

	n = WIFIHDRSIZE;
	if((w->fc[0] & 0x0c) == 0x08)
		if((w->fc[0] & 0xf0) == 0x80){	/* QOS */
			n += 2;
			if(w->fc[1] & 0x80)
				n += 4;
		}
	return n;
}

void
wifiiq(Wifi *wifi, Block *b)
{
	SNAP s;
	Wifipkt h, *w;
	Etherpkt *e;

	if(BLEN(b) < WIFIHDRSIZE)
		goto drop;
	w = (Wifipkt*)b->rp;
	if(w->fc[1] & 0x40){
		/* encrypted */
		qpass(wifi->iq, b);
		return;
	}
	switch(w->fc[0] & 0x0c){
	case 0x00:	/* management */
		if((w->fc[1] & 3) != 0x00)	/* STA->STA */
			break;
		qpass(wifi->iq, b);
		return;
	case 0x04:	/* control */
		break;
	case 0x08:	/* data */
		if((w->fc[1] & 3) == 0x03)	/* AP->AP */
			break;
		b->rp += wifihdrlen(w);
		switch(w->fc[0] & 0xf0){
		default:
			goto drop;
		case 0x80:	/* QOS */
		case 0x00:
			break;
		}
		if(BLEN(b) < SNAPHDRSIZE)
			break;
		memmove(&s, b->rp, SNAPHDRSIZE);
		if(s.dsap != 0xAA || s.ssap != 0xAA || s.control != 3)
			break;
		if(s.orgcode[0] != 0 || s.orgcode[1] != 0 || s.orgcode[2] != 0)
			break;
		b->rp += SNAPHDRSIZE-ETHERHDRSIZE;
		h = *w;
		e = (Etherpkt*)b->rp;
		memmove(e->d, dstaddr(&h), Eaddrlen);
		memmove(e->s, srcaddr(&h), Eaddrlen);
		memmove(e->type, s.type, 2);
		etheriq(wifi->ether, b, 1);
		return;
	}
drop:
	freeb(b);
}

static void
wifitx(Wifi *wifi, Wnode *wn, Block *b)
{
	Wifipkt *w;
	uint seq;

	seq = incref(&wifi->txseq);
	seq <<= 4;

	w = (Wifipkt*)b->rp;
	w->dur[0] = 0;
	w->dur[1] = 0;
	w->seq[0] = seq;
	w->seq[1] = seq>>8;

	if((w->fc[0] & 0x0c) != 0x00)
		b = wifiencrypt(wifi, wn, b);

	if(b != nil)
		(*wifi->transmit)(wifi, wn, b);
}

static Wnode*
nodelookup(Wifi *wifi, uchar *bssid, int new)
{
	Wnode *wn, *nn;

	if(memcmp(bssid, wifi->ether->bcast, Eaddrlen) == 0)
		return nil;
	if((wn = wifi->bss) != nil){
		if(memcmp(wn->bssid, bssid, Eaddrlen) == 0){
			wn->lastseen = MACHP(0)->ticks;
			return wn;
		}
	}
	if((nn = wifi->node) == wn)
		nn++;
	for(wn = wifi->node; wn != &wifi->node[nelem(wifi->node)]; wn++){
		if(wn == wifi->bss)
			continue;
		if(memcmp(wn->bssid, bssid, Eaddrlen) == 0){
			wn->lastseen = MACHP(0)->ticks;
			return wn;
		}
		if(wn->lastseen < nn->lastseen)
			nn = wn;
	}
	if(!new)
		return nil;
	memset(nn, 0, sizeof(Wnode));
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
	wifitx(wifi, bss, b);
}

static void
sendassoc(Wifi *wifi, Wnode *bss)
{
	Wifipkt *w;
	Block *b;
	uchar *p;
	int n;

	b = allocb(WIFIHDRSIZE + 512);
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

	n = bss->rsnelen;
	if(n > 0){
		memmove(p, bss->rsne, n);
		p += n;
	}

	b->wp = p;
	wifitx(wifi, bss, b);
}

static void
setstatus(Wifi *wifi, char *new)
{
	char *old;

	old = wifi->status;
	wifi->status = new;
	if(wifi->debug && new != old)
		print("#l%d: status: %s -> %s (from pc=%#p)\n",
			wifi->ether->ctlrno, old, new, getcallerpc(&wifi));
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
		if(wn->rsnelen > 0)
			setstatus(wifi, Sblocked);
		else
			setstatus(wifi, Sassoc);
		break;
	default:
		wn->aid = 0;
		setstatus(wifi, Sunassoc);
		return;
	}
}

static void
recvbeacon(Wifi *, Wnode *wn, uchar *d, int len)
{
	static uchar wpa1oui[4] = { 0x00, 0x50, 0xf2, 0x01 };
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
		case 0x00:	/* SSID */
			len = 0;
			while(len < Essidlen && d+len < x && d[len] != 0)
				len++;
			if(len == 0)
				continue;
			if(len != strlen(wn->ssid) || strncmp(wn->ssid, (char*)d, len) != 0){
				strncpy(wn->ssid, (char*)d, len);
				wn->ssid[len] = 0;
			}
			break;
		case 0x03:	/* DSPARAMS */
			if(d != x)
				wn->channel = d[0];
			break;
		case 0xdd:	/* vendor specific */
			len = x - d;
			if(len < sizeof(wpa1oui) || memcmp(d, wpa1oui, sizeof(wpa1oui)) != 0)
				break;
			/* no break */
		case 0x30:	/* RSN information */
			len = x - &d[-2];
			memmove(wn->brsne, &d[-2], len);
			wn->brsnelen = len;
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
		if(w->fc[1] & 0x40){
			/* encrypted */
			if((wn = nodelookup(wifi, w->a2, 1)) == nil)
				continue;
			if((b = wifidecrypt(wifi, wn, b)) != nil){
				w = (Wifipkt*)b->rp;
				if(w->fc[1] & 0x40)
					continue;
				wifiiq(wifi, b);
				b = nil;
			}
			continue;
		}
		/* management */
		if((w->fc[0] & 0x0c) != 0x00)
			continue;
		switch(w->fc[0] & 0xf0){
		case 0x50:	/* probe response */
		case 0x80:	/* beacon */
			if((wn = nodelookup(wifi, w->a3, 1)) == nil)
				continue;
			b->rp += wifihdrlen(w);
			recvbeacon(wifi, wn, b->rp, BLEN(b));
			if(wifi->bss == nil && wifi->essid[0] != 0 && strcmp(wifi->essid, wn->ssid) == 0){
				wifi->bss = wn;
				setstatus(wifi, Sconn);
				sendauth(wifi, wn);
			}
			continue;
		}
		if(memcmp(w->a1, wifi->ether->ea, Eaddrlen))
			continue;
		if((wn = nodelookup(wifi, w->a3, 0)) == nil)
			continue;
		if(wn != wifi->bss)
			continue;
		switch(w->fc[0] & 0xf0){
		case 0x10:	/* assoc response */
		case 0x30:	/* reassoc response */
			b->rp += wifihdrlen(w);
			recvassoc(wifi, wn, b->rp, BLEN(b));
			break;
		case 0xb0:	/* auth */
			setstatus(wifi, Sauth);
			sendassoc(wifi, wn);
			break;
		case 0xc0:	/* deauth */
			setstatus(wifi, Sunauth);
			memset(wn->rxkey, 0, sizeof(wn->rxkey));
			memset(wn->txkey, 0, sizeof(wn->txkey));
			wn->aid = 0;
			sendauth(wifi, wn);
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
	Wnode *wn;
	SNAP *s;

	if(BLEN(b) < ETHERHDRSIZE)
		goto drop;
	if((wn = wifi->bss) == nil)
		goto drop;

	memmove(&e, b->rp, ETHERHDRSIZE);
	b->rp += ETHERHDRSIZE;

	if(wifi->status == Sblocked){
		/* only pass EAPOL frames when port is blocked */
		if((e.type[0]<<8 | e.type[1]) != 0x888e)
			goto drop;
	} else if(wifi->status != Sassoc)
		goto drop;

	b = padblock(b, WIFIHDRSIZE + SNAPHDRSIZE);

	w = (Wifipkt*)b->rp;
	w->fc[0] = 0x08;	/* data */
	w->fc[1] = 0x01;	/* STA->AP */
	memmove(w->a1, wn->bssid, Eaddrlen);
	memmove(w->a2, e.s, Eaddrlen);
	memmove(w->a3, e.d, Eaddrlen);

	s = (SNAP*)(b->rp + WIFIHDRSIZE);
	s->dsap = s->ssap = 0xAA;
	s->control = 0x03;
	s->orgcode[0] = 0;
	s->orgcode[1] = 0;
	s->orgcode[2] = 0;
	memmove(s->type, e.type, 2);

	wifitx(wifi, wn, b);
	return;
drop:
	freeb(b);
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
	char name[32];
	Wifi *wifi;

	wifi = malloc(sizeof(Wifi));
	if(wifi == nil)
		error(Enomem);
	wifi->iq = qopen(ether->limit, 0, 0, 0);
	if(wifi->iq == nil){
		free(wifi);
		error(Enomem);
	}
	wifi->ether = ether;
	wifi->transmit = transmit;
	wifi->status = Snone;

	snprint(name, sizeof(name), "#l%dwifi", ether->ctlrno);
	kproc(name, wifiproc, wifi);
	snprint(name, sizeof(name), "#l%dwifo", ether->ctlrno);
	kproc(name, wifoproc, wifi);

	return wifi;
}

static int
hextob(char *s, char **sp, uchar *b, int n)
{
	int r;

	n <<= 1;
	for(r = 0; r < n && *s; s++){
		*b <<= 4;
		if(*s >= '0' && *s <= '9')
			*b |= (*s - '0');
		else if(*s >= 'a' && *s <= 'f')
			*b |= 10+(*s - 'a');
		else if(*s >= 'A' && *s <= 'F')
			*b |= 10+(*s - 'A');
		else break;
		if((++r & 1) == 0)
			b++;
	}
	if(sp != nil)
		*sp = s;
	return r >> 1;
}

static char *ciphers[] = {
	[0]	"clear",
	[TKIP]	"tkip",
	[CCMP]	"ccmp",
};

static int
parsekey(Wkey *k, char *s)
{
	char buf[256], *p;
	int i;

	strncpy(buf, s, sizeof(buf)-1);
	buf[sizeof(buf)-1] = 0;
	if((p = strchr(buf, ':')) != nil)
		*p++ = 0;
	else
		p = buf;
	for(i=0; i<nelem(ciphers); i++){
		if(ciphers[i] == nil)
			continue;
		if(strcmp(ciphers[i], buf) == 0)
			break;
	}
	if(i >= nelem(ciphers))
		return -1;
	memset(k, 0, sizeof(Wkey));
	k->len = hextob(p, &p, k->key, sizeof(k->key));
	if(*p == '@')
		k->tsc = strtoull(++p, nil, 16);
	k->cipher = i;
	return 0;
}

enum {
	CMdebug,
	CMessid,
	CMauth,
	CMrxkey0,
	CMrxkey1,
	CMrxkey2,
	CMrxkey3,
	CMrxkey4,
	CMtxkey0,
};

static Cmdtab wifictlmsg[] =
{
	CMdebug,	"debug",	0,
	CMessid,	"essid",	0,
	CMauth,		"auth",		0,

	CMrxkey0,	"rxkey0",	0,	/* group keys */
	CMrxkey1,	"rxkey1",	0,
	CMrxkey2,	"rxkey2",	0,
	CMrxkey3,	"rxkey3",	0,

	CMrxkey4,	"rxkey",	0,	/* peerwise keys */
	CMtxkey0,	"txkey",	0,

	CMtxkey0,	"txkey0",	0,
};

long
wifictl(Wifi *wifi, void *buf, long n)
{
	Cmdbuf *cb;
	Cmdtab *ct;
	Wnode *wn;
	Wkey *k;

	cb = nil;
	if(waserror()){
		free(cb);
		nexterror();
	}
	if(wifi->debug)
		print("#l%d: wifictl: %.*s\n", wifi->ether->ctlrno, (int)n, buf);
	wn = wifi->bss;
	cb = parsecmd(buf, n);
	ct = lookupcmd(cb, wifictlmsg, nelem(wifictlmsg));
	if(ct->index >= CMauth){
		if(ct->index >= CMrxkey0 && cb->nf > 1){
			uchar addr[Eaddrlen];

			if(parseether(addr, cb->f[1]) == 0){
				cb->f++;
				cb->nf--;
				wn = nodelookup(wifi, addr, 0);
			}
		}
		if(wn == nil)
			error("missing node");
	}
	switch(ct->index){
	case CMdebug:
		if(cb->f[1] != nil)
			wifi->debug = atoi(cb->f[1]);
		else
			wifi->debug ^= 1;
		print("#l%d: debug: %d\n", wifi->ether->ctlrno, wifi->debug);
		break;
	case CMessid:
		if(cb->f[1] == nil){
			wifi->essid[0] = 0;
			wifi->bss = nil;
			setstatus(wifi, Snone);
		} else {
			strncpy(wifi->essid, cb->f[1], Essidlen);
			for(wn = wifi->node; wn != &wifi->node[nelem(wifi->node)]; wn++)
				if(strcmp(wifi->essid, wn->ssid) == 0){
					wifi->bss = wn;
					setstatus(wifi, Sconn);
					sendauth(wifi, wn);
					break;
				}
		}
		break;
	case CMauth:
		setstatus(wifi, Sauth);
		memset(wn->rxkey, 0, sizeof(wn->rxkey));
		memset(wn->txkey, 0, sizeof(wn->txkey));
		if(cb->f[1] == nil)
			wn->rsnelen = 0;
		else
			wn->rsnelen = hextob(cb->f[1], nil, wn->rsne, sizeof(wn->rsne));
		sendassoc(wifi, wn);
		break;
	case CMrxkey0: case CMrxkey1: case CMrxkey2: case CMrxkey3: case CMrxkey4:
	case CMtxkey0:
		if(ct->index < CMtxkey0)
			k = &wn->rxkey[ct->index - CMrxkey0];
		else
			k = &wn->txkey[ct->index - CMtxkey0];
		if(cb->f[1] == nil || parsekey(k, cb->f[1]) != 0)
			error("bad key");
		if(ct->index >= CMtxkey0 && wifi->status == Sblocked && wifi->bss == wn)
			setstatus(wifi, Sassoc);
		break;
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
	int i;

	p = s = smalloc(4096);
	e = s + 4096;

	p = seprint(p, e, "status: %s\n", wifi->status);
	p = seprint(p, e, "essid: %s\n", wifi->essid);

	wn = wifi->bss;
	if(wn != nil){
		p = seprint(p, e, "bssid: %E\n", wn->bssid);

		/* only print key ciphers and key length */
		for(i = 0; i<nelem(wn->rxkey); i++)
			p = seprint(p, e, "rxkey%d: %s:[%d]\n", i,
				ciphers[wn->rxkey[i].cipher], wn->rxkey[i].len);
		for(i = 0; i<nelem(wn->txkey); i++)
			p = seprint(p, e, "txkey%d: %s:[%d]\n", i,
				ciphers[wn->txkey[i].cipher], wn->txkey[i].len);

		if(wn->brsnelen > 0){
			p = seprint(p, e, "brsne: ");
			for(i=0; i<wn->brsnelen; i++)
				p = seprint(p, e, "%.2X", wn->brsne[i]);
			p = seprint(p, e, "\n");
		}
		p = seprint(p, e, "channel: %.2d\n", wn->channel);
	}

	now = MACHP(0)->ticks;
	for(wn = wifi->node; wn != &wifi->node[nelem(wifi->node)]; wn++){
		if(wn->lastseen == 0)
			continue;
		p = seprint(p, e, "node: %E %.4x %-11ld %.2d %s\n",
			wn->bssid, wn->cap, TK2MS(now - wn->lastseen), wn->channel, wn->ssid);
	}
	n = readstr(off, buf, n, s);
	free(s);
	return n;
}

static void tkipencrypt(Wkey *k, Wifipkt *w, Block *b, uvlong tsc);
static int tkipdecrypt(Wkey *k, Wifipkt *w, Block *b, uvlong tsc);
static void ccmpencrypt(Wkey *k, Wifipkt *w, Block *b, uvlong tsc);
static int ccmpdecrypt(Wkey *k, Wifipkt *w, Block *b, uvlong tsc);

static Block*
wifiencrypt(Wifi *, Wnode *wn, Block *b)
{
	uvlong tsc;
	int n, kid;
	Wifipkt *w;
	Wkey *k;

	kid = 0;
	k = &wn->txkey[kid];
	if(k->cipher == 0)
		return b;

	n = wifihdrlen((Wifipkt*)b->rp);

	b = padblock(b, 8);
	b = padblock(b, -(8+4));

	w = (Wifipkt*)b->rp;
	memmove(w, b->rp+8, n);
	b->rp += n;

	tsc = ++k->tsc;

	switch(k->cipher){
	case TKIP:
		b->rp[0] = tsc>>8;
		b->rp[1] = (b->rp[0] | 0x20) & 0x7f;
		b->rp[2] = tsc;
		b->rp[3] = kid<<6 | 0x20;
		b->rp[4] = tsc>>16;
		b->rp[5] = tsc>>24;
		b->rp[6] = tsc>>32;
		b->rp[7] = tsc>>40;
		b->rp += 8;
		if(k->len != 32)
			goto drop;
		tkipencrypt(k, w, b, tsc);
		break;
	case CCMP:
		b->rp[0] = tsc;
		b->rp[1] = tsc>>8;
		b->rp[2] = 0;
		b->rp[3] = kid<<6 | 0x20;
		b->rp[4] = tsc>>16;
		b->rp[5] = tsc>>24;
		b->rp[6] = tsc>>32;
		b->rp[7] = tsc>>40;
		b->rp += 8;
		if(k->len != 16)
			goto drop;
		ccmpencrypt(k, w, b, tsc);
		break;
	default:
	drop:
		free(b);
		return nil;
	}

	b->rp = (uchar*)w;
	w->fc[1] |= 0x40;
	return b;
}

static Block*
wifidecrypt(Wifi *, Wnode *wn, Block *b)
{
	uvlong tsc;
	int n, kid;
	Wifipkt *w;
	Wkey *k;

	w = (Wifipkt*)b->rp;
	if(BLEN(b) < WIFIHDRSIZE)
		goto drop;

	n = wifihdrlen(w);
	b->rp += n;
	if(BLEN(b) < 8+8)
		goto drop;

	kid = b->rp[3]>>6;
	if((b->rp[3] & 0x20) == 0)
		goto drop;
	if((dstaddr(w)[0] & 1) == 0)
		kid = 4;	/* use peerwise key for non-unicast */

	k = &wn->rxkey[kid];
	switch(k->cipher){
	case TKIP:
		tsc =	(uvlong)b->rp[7]<<40 |
			(uvlong)b->rp[6]<<32 |
			(uvlong)b->rp[5]<<24 |
			(uvlong)b->rp[4]<<16 |
			(uvlong)b->rp[0]<<8 |
			(uvlong)b->rp[2];
		b->rp += 8;
		if(tsc <= k->tsc || k->len != 32)
			goto drop;
		if(tkipdecrypt(k, w, b, tsc) != 0)
			goto drop;
		break;
	case CCMP:
		tsc =	(uvlong)b->rp[7]<<40 |
			(uvlong)b->rp[6]<<32 |
			(uvlong)b->rp[5]<<24 |
			(uvlong)b->rp[4]<<16 |
			(uvlong)b->rp[1]<<8 |
			(uvlong)b->rp[0];
		b->rp += 8;
		if(tsc <= k->tsc || k->len != 16)
			goto drop;
		if(ccmpdecrypt(k, w, b, tsc) != 0)
			goto drop;
		break;
	default:
	drop:
		freeb(b);
		return nil;
	}

	k->tsc = tsc;
	b->rp -= n;
	memmove(b->rp, w, n);
	w = (Wifipkt*)b->rp;
	w->fc[1] &= ~0x40;
	return b;
}

static u16int Sbox[256] = {
	0xC6A5, 0xF884, 0xEE99, 0xF68D, 0xFF0D, 0xD6BD, 0xDEB1, 0x9154,
	0x6050, 0x0203, 0xCEA9, 0x567D, 0xE719, 0xB562, 0x4DE6, 0xEC9A,
	0x8F45, 0x1F9D, 0x8940, 0xFA87, 0xEF15, 0xB2EB, 0x8EC9, 0xFB0B,
	0x41EC, 0xB367, 0x5FFD, 0x45EA, 0x23BF, 0x53F7, 0xE496, 0x9B5B,
	0x75C2, 0xE11C, 0x3DAE, 0x4C6A, 0x6C5A, 0x7E41, 0xF502, 0x834F,
	0x685C, 0x51F4, 0xD134, 0xF908, 0xE293, 0xAB73, 0x6253, 0x2A3F,
	0x080C, 0x9552, 0x4665, 0x9D5E, 0x3028, 0x37A1, 0x0A0F, 0x2FB5,
	0x0E09, 0x2436, 0x1B9B, 0xDF3D, 0xCD26, 0x4E69, 0x7FCD, 0xEA9F,
	0x121B, 0x1D9E, 0x5874, 0x342E, 0x362D, 0xDCB2, 0xB4EE, 0x5BFB,
	0xA4F6, 0x764D, 0xB761, 0x7DCE, 0x527B, 0xDD3E, 0x5E71, 0x1397,
	0xA6F5, 0xB968, 0x0000, 0xC12C, 0x4060, 0xE31F, 0x79C8, 0xB6ED,
	0xD4BE, 0x8D46, 0x67D9, 0x724B, 0x94DE, 0x98D4, 0xB0E8, 0x854A,
	0xBB6B, 0xC52A, 0x4FE5, 0xED16, 0x86C5, 0x9AD7, 0x6655, 0x1194,
	0x8ACF, 0xE910, 0x0406, 0xFE81, 0xA0F0, 0x7844, 0x25BA, 0x4BE3,
	0xA2F3, 0x5DFE, 0x80C0, 0x058A, 0x3FAD, 0x21BC, 0x7048, 0xF104,
	0x63DF, 0x77C1, 0xAF75, 0x4263, 0x2030, 0xE51A, 0xFD0E, 0xBF6D,
	0x814C, 0x1814, 0x2635, 0xC32F, 0xBEE1, 0x35A2, 0x88CC, 0x2E39,
	0x9357, 0x55F2, 0xFC82, 0x7A47, 0xC8AC, 0xBAE7, 0x322B, 0xE695,
	0xC0A0, 0x1998, 0x9ED1, 0xA37F, 0x4466, 0x547E, 0x3BAB, 0x0B83,
	0x8CCA, 0xC729, 0x6BD3, 0x283C, 0xA779, 0xBCE2, 0x161D, 0xAD76,
	0xDB3B, 0x6456, 0x744E, 0x141E, 0x92DB, 0x0C0A, 0x486C, 0xB8E4,
	0x9F5D, 0xBD6E, 0x43EF, 0xC4A6, 0x39A8, 0x31A4, 0xD337, 0xF28B,
	0xD532, 0x8B43, 0x6E59, 0xDAB7, 0x018C, 0xB164, 0x9CD2, 0x49E0,
	0xD8B4, 0xACFA, 0xF307, 0xCF25, 0xCAAF, 0xF48E, 0x47E9, 0x1018,
	0x6FD5, 0xF088, 0x4A6F, 0x5C72, 0x3824, 0x57F1, 0x73C7, 0x9751,
	0xCB23, 0xA17C, 0xE89C, 0x3E21, 0x96DD, 0x61DC, 0x0D86, 0x0F85,
	0xE090, 0x7C42, 0x71C4, 0xCCAA, 0x90D8, 0x0605, 0xF701, 0x1C12,
	0xC2A3, 0x6A5F, 0xAEF9, 0x69D0, 0x1791, 0x9958, 0x3A27, 0x27B9,
	0xD938, 0xEB13, 0x2BB3, 0x2233, 0xD2BB, 0xA970, 0x0789, 0x33A7,
	0x2DB6, 0x3C22, 0x1592, 0xC920, 0x8749, 0xAAFF, 0x5078, 0xA57A,
	0x038F, 0x59F8, 0x0980, 0x1A17, 0x65DA, 0xD731, 0x84C6, 0xD0B8,
	0x82C3, 0x29B0, 0x5A77, 0x1E11, 0x7BCB, 0xA8FC, 0x6DD6, 0x2C3A
};

static void
tkipk2tk(uchar key[16], u16int tk[8])
{
	tk[0] = (u16int)key[1]<<8 | key[0];
	tk[1] = (u16int)key[3]<<8 | key[2];
	tk[2] = (u16int)key[5]<<8 | key[4];
	tk[3] = (u16int)key[7]<<8 | key[6];
	tk[4] = (u16int)key[9]<<8 | key[8];
	tk[5] = (u16int)key[11]<<8 | key[10];
	tk[6] = (u16int)key[13]<<8 | key[12];
	tk[7] = (u16int)key[15]<<8 | key[14];
}

static void
tkipphase1(u32int tscu, uchar ta[Eaddrlen], u16int tk[8], u16int p1k[5])
{
	u16int *k, i, x0, x1, x2;

	p1k[0] = tscu;
	p1k[1] = tscu>>16;
	p1k[2] = (u16int)ta[1]<<8 | ta[0];
	p1k[3] = (u16int)ta[3]<<8 | ta[2];
	p1k[4] = (u16int)ta[5]<<8 | ta[4];

	for(i=0; i<8; i++){
		k = &tk[i & 1];

		x0 = p1k[4] ^ k[0];
		x1 = Sbox[x0 >> 8];
		x2 = Sbox[x0 & 0xFF] ^ ((x1>>8) | (x1<<8));
		p1k[0] += x2;
		x0 = p1k[0] ^ k[2];
		x1 = Sbox[x0 >> 8];
		x2 = Sbox[x0 & 0xFF] ^ ((x1>>8) | (x1<<8));
		p1k[1] += x2;
		x0 = p1k[1] ^ k[4];
		x1 = Sbox[x0 >> 8];
		x2 = Sbox[x0 & 0xFF] ^ ((x1>>8) | (x1<<8));
		p1k[2] += x2;
		x0 = p1k[2] ^ k[6];
		x1 = Sbox[x0 >> 8];
		x2 = Sbox[x0 & 0xFF] ^ ((x1>>8) | (x1<<8));
		p1k[3] += x2;
		x0 = p1k[3] ^ k[0];
		x1 = Sbox[x0 >> 8];
		x2 = Sbox[x0 & 0xFF] ^ ((x1>>8) | (x1<<8));
		p1k[4] += x2;

		p1k[4] += i;
	}
}

static void
tkipphase2(u16int tscl, u16int p1k[5], u16int tk[8], uchar rc4key[16])
{
	u16int ppk[6], x0, x1, x2;

	ppk[0] = p1k[0];
	ppk[1] = p1k[1];
	ppk[2] = p1k[2];
	ppk[3] = p1k[3];
	ppk[4] = p1k[4];
	ppk[5] = p1k[4] + tscl;

	x0 = ppk[5] ^ tk[0];
	x1 = Sbox[x0 >> 8];
	x2 = Sbox[x0 & 0xFF] ^ ((x1>>8) | (x1<<8));
	ppk[0] += x2;
	x0 = ppk[0] ^ tk[1];
	x1 = Sbox[x0 >> 8];
	x2 = Sbox[x0 & 0xFF] ^ ((x1>>8) | (x1<<8));
	ppk[1] += x2;
	x0 = ppk[1] ^ tk[2];
	x1 = Sbox[x0 >> 8];
	x2 = Sbox[x0 & 0xFF] ^ ((x1>>8) | (x1<<8));
	ppk[2] += x2;
	x0 = ppk[2] ^ tk[3];
	x1 = Sbox[x0 >> 8];
	x2 = Sbox[x0 & 0xFF] ^ ((x1>>8) | (x1<<8));
	ppk[3] += x2;
	x0 = ppk[3] ^ tk[4];
	x1 = Sbox[x0 >> 8];
	x2 = Sbox[x0 & 0xFF] ^ ((x1>>8) | (x1<<8));
	ppk[4] += x2;
	x0 = ppk[4] ^ tk[5];
	x1 = Sbox[x0 >> 8];
	x2 = Sbox[x0 & 0xFF] ^ ((x1>>8) | (x1<<8));
	ppk[5] += x2;

	x2 = ppk[5] ^ tk[6];
	ppk[0] += (x2 >> 1) | (x2 << 15);
	x2 = ppk[0] ^ tk[7];
	ppk[1] += (x2 >> 1) | (x2 << 15);

	x2 = ppk[1];
	ppk[2] += (x2 >> 1) | (x2 << 15);
	x2 = ppk[2];
	ppk[3] += (x2 >> 1) | (x2 << 15);
	x2 = ppk[3];
	ppk[4] += (x2 >> 1) | (x2 << 15);
	x2 = ppk[4];
	ppk[5] += (x2 >> 1) | (x2 << 15);

	rc4key[0] = tscl >> 8;
	rc4key[1] = (rc4key[0] | 0x20) & 0x7F;
	rc4key[2] = tscl;
	rc4key[3] = (ppk[5] ^ tk[0]) >> 1;
	rc4key[4]  = ppk[0];
	rc4key[5]  = ppk[0] >> 8;
	rc4key[6]  = ppk[1];
	rc4key[7]  = ppk[1] >> 8;
	rc4key[8]  = ppk[2];
	rc4key[9]  = ppk[2] >> 8;
	rc4key[10] = ppk[3];
	rc4key[11] = ppk[3] >> 8;
	rc4key[12] = ppk[4];
	rc4key[13] = ppk[4] >> 8;
	rc4key[14] = ppk[5];
	rc4key[15] = ppk[5] >> 8;
}

typedef struct MICstate MICstate;
struct MICstate
{
	u32int	l;
	u32int	r;
	u32int	m;
	u32int	n;
};

static void
micsetup(MICstate *s, uchar key[8])
{
	s->l =	(u32int)key[0] |
		(u32int)key[1]<<8 |
		(u32int)key[2]<<16 |
		(u32int)key[3]<<24;
	s->r =	(u32int)key[4] |
		(u32int)key[5]<<8 |
		(u32int)key[6]<<16 |
		(u32int)key[7]<<24;
	s->m = 0;
	s->n = 0;
}

static void
micupdate(MICstate *s, uchar *data, ulong len)
{
	u32int l, r, m, n, e;

	l = s->l;
	r = s->r;
	m = s->m;
	n = s->n;
	e = n + len;
	while(n != e){
		m >>= 8;
		m |= (u32int)*data++ << 24;
		if(++n & 3)
			continue;
		l ^= m;
		r ^= (l << 17) | (l >> 15);
		l += r;
		r ^= ((l & 0x00FF00FFUL)<<8) | ((l & 0xFF00FF00UL)>>8);
		l += r;
		r ^= (l << 3) | (l >> 29);
		l += r;
		r ^= (l >> 2) | (l << 30);
		l += r;
	}
	s->l = l;
	s->r = r;
	s->m = m;
	s->n = n;
}

static void
micfinish(MICstate *s, uchar mic[8])
{
	static uchar pad[8] = { 0x5a, 0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00, };

	micupdate(s, pad, sizeof(pad));

	mic[0] = s->l;
	mic[1] = s->l>>8;
	mic[2] = s->l>>16;
	mic[3] = s->l>>24;
	mic[4] = s->r;
	mic[5] = s->r>>8;
	mic[6] = s->r>>16;
	mic[7] = s->r>>24;
}

static uchar pad4[4] = { 0x00, 0x00, 0x00, 0x00, };

static void
tkipencrypt(Wkey *k, Wifipkt *w, Block *b, uvlong tsc)
{
	u16int tk[8], p1k[5];
	uchar seed[16];
	RC4state rs;
	MICstate ms;
	ulong crc;

	micsetup(&ms, k->key+24);
	micupdate(&ms, dstaddr(w), Eaddrlen);
	micupdate(&ms, srcaddr(w), Eaddrlen);
	micupdate(&ms, pad4, 4);
	micupdate(&ms, b->rp, BLEN(b));
	micfinish(&ms, b->wp);
	b->wp += 8;

	crc = ethercrc(b->rp, BLEN(b));
	crc = ~crc;
	b->wp[0] = crc;
	b->wp[1] = crc>>8;
	b->wp[2] = crc>>16;
	b->wp[3] = crc>>24;
	b->wp += 4;

	tkipk2tk(k->key, tk);
	tkipphase1(tsc >> 16, w->a2, tk, p1k);
	tkipphase2(tsc & 0xFFFF, p1k, tk, seed);
	setupRC4state(&rs, seed, sizeof(seed));
	rc4(&rs, b->rp, BLEN(b));
}

static int
tkipdecrypt(Wkey *k, Wifipkt *w, Block *b, uvlong tsc)
{
	uchar seed[16], mic[8];
	u16int tk[8], p1k[5];
	RC4state rs;
	MICstate ms;
	ulong crc;

	if(BLEN(b) < 8+4)
		return -1;

	tkipk2tk(k->key, tk);
	tkipphase1(tsc >> 16, w->a2, tk, p1k);
	tkipphase2(tsc & 0xFFFF, p1k, tk, seed);
	setupRC4state(&rs, seed, sizeof(seed));
	rc4(&rs, b->rp, BLEN(b));

	b->wp -= 4;
	crc =	(ulong)b->wp[0] |
		(ulong)b->wp[1]<<8 |
		(ulong)b->wp[2]<<16 |
		(ulong)b->wp[3]<<24;
	crc = ~crc;
	if(ethercrc(b->rp, BLEN(b)) != crc)
		return -1;

	b->wp -= 8;
	micsetup(&ms, k->key+16);
	micupdate(&ms, dstaddr(w), Eaddrlen);
	micupdate(&ms, srcaddr(w), Eaddrlen);
	micupdate(&ms, pad4, 4);
	micupdate(&ms, b->rp, BLEN(b));
	micfinish(&ms, mic);

	return memcmp(b->wp, mic, 8) != 0;
}

static uchar*
putbe(uchar *p, int L, uint v)
{
	while(--L >= 0)
		*p++ = (v >> L*8) & 0xFF;
	return p;
}

static void
xblock(int L, int M, uchar *N, uchar *a, int la, int lm, uchar t[16], AESstate *s)
{
	uchar l[8], *p, *x, *e;

	assert(M >= 4 && M <= 16);
	assert(L >= 2 && L <= 4);

	t[0] = ((la > 0)<<6) | ((M-2)/2)<<3 | (L-1);	/* flags */
	memmove(&t[1], N, 15-L);
	putbe(&t[16-L], L, lm);
	aes_encrypt(s->ekey, s->rounds, t, t);
	
	if(la > 0){
		assert(la < 0xFF00);
		for(p = l, e = putbe(l, 2, la), x = t; p < e; x++, p++)
			*x ^= *p;
		for(e = a + la; a < e; x = t){
			for(; a < e && x < &t[16]; x++, a++)
				*x ^= *a;
			aes_encrypt(s->ekey, s->rounds, t, t);
		}
	}
}

static uchar*
sblock(int L, uchar *N, uint i, uchar b[16], AESstate *s)
{
	b[0] = L-1;	/* flags */
	memmove(&b[1], N, 15-L);
	putbe(&b[16-L], L, i);
	aes_encrypt(s->ekey, s->rounds, b, b);
	return b;
};

static void
aesCCMencrypt(int L, int M, uchar *N /* N[15-L] */,
	uchar *a /* a[la] */, int la,
	uchar *m /* m[lm+M] */, int lm,
	AESstate *s)
{
	uchar t[16], b[16], *p, *x;
	uint i;

	xblock(L, M, N, a, la, lm, t, s);

	for(i = 1; lm >= 16; i++, m += 16, lm -= 16){
		sblock(L, N, i, b, s);

		*((u32int*)&t[0]) ^= *((u32int*)&m[0]);
		*((u32int*)&m[0]) ^= *((u32int*)&b[0]);
		*((u32int*)&t[4]) ^= *((u32int*)&m[4]);
		*((u32int*)&m[4]) ^= *((u32int*)&b[4]);
		*((u32int*)&t[8]) ^= *((u32int*)&m[8]);
		*((u32int*)&m[8]) ^= *((u32int*)&b[8]);
		*((u32int*)&t[12]) ^= *((u32int*)&m[12]);
		*((u32int*)&m[12]) ^= *((u32int*)&b[12]);

		aes_encrypt(s->ekey, s->rounds, t, t);
	}
	if(lm > 0){
		for(p = sblock(L, N, i, b, s), x = t; p < &b[lm]; x++, m++, p++){
			*x ^= *m;
			*m ^= *p;
		}
		aes_encrypt(s->ekey, s->rounds, t, t);
	}

	for(p = sblock(L, N, 0, b, s), x = t; p < &b[M]; x++, p++)
		*x ^= *p;

	memmove(m, t, M);
}

static int
aesCCMdecrypt(int L, int M, uchar *N /* N[15-L] */,
	uchar *a /* a[la] */, int la,
	uchar *m /* m[lm+M] */, int lm,
	AESstate *s)
{
	uchar t[16], b[16], *p, *x;
	uint i;

	xblock(L, M, N, a, la, lm, t, s);

	for(i = 1; lm >= 16; i++, m += 16, lm -= 16){
		sblock(L, N, i, b, s);

		*((u32int*)&m[0]) ^= *((u32int*)&b[0]);
		*((u32int*)&t[0]) ^= *((u32int*)&m[0]);
		*((u32int*)&m[4]) ^= *((u32int*)&b[4]);
		*((u32int*)&t[4]) ^= *((u32int*)&m[4]);
		*((u32int*)&m[8]) ^= *((u32int*)&b[8]);
		*((u32int*)&t[8]) ^= *((u32int*)&m[8]);
		*((u32int*)&m[12]) ^= *((u32int*)&b[12]);
		*((u32int*)&t[12]) ^= *((u32int*)&m[12]);

		aes_encrypt(s->ekey, s->rounds, t, t);
	}
	if(lm > 0){
		for(p = sblock(L, N, i, b, s), x = t; p < &b[lm]; x++, m++, p++){
			*m ^= *p;
			*x ^= *m;
		}
		aes_encrypt(s->ekey, s->rounds, t, t);
	}

	for(p = sblock(L, N, 0, b, s), x = t; p < &b[M]; x++, p++)
		*x ^= *p;

	return memcmp(m, t, M) != 0;
}

static int
setupCCMP(Wkey *k, Wifipkt *w, uvlong tsc, uchar nonce[13], uchar auth[32], AESstate *as)
{
	uchar *p;

	setupAESstate(as, k->key, k->len, nil);

	nonce[0] = ((w->fc[0] & 0x0c) == 0x00) << 4;
	memmove(&nonce[1], w->a2, Eaddrlen);
	nonce[7]  = tsc >> 40;
	nonce[8]  = tsc >> 32;
	nonce[9]  = tsc >> 24;
	nonce[10] = tsc >> 16;
	nonce[11] = tsc >> 8;
	nonce[12] = tsc;

	p = auth;
	*p++ = (w->fc[0] & (((w->fc[0] & 0x0c) == 0x08) ? 0x0f : 0xff));
	*p++ = (w->fc[1] & ~0x38) | 0x40;
	memmove(p, w->a1, Eaddrlen); p += Eaddrlen;
	memmove(p, w->a2, Eaddrlen); p += Eaddrlen;
	memmove(p, w->a3, Eaddrlen); p += Eaddrlen;
	*p++ = w->seq[0] & 0x0f;
	*p++ = 0;

	return p - auth;
}

static void
ccmpencrypt(Wkey *k, Wifipkt *w, Block *b, uvlong tsc)
{
	uchar auth[32], nonce[13];
	AESstate as;

	aesCCMencrypt(2, 8, nonce, auth,
		setupCCMP(k, w, tsc, nonce, auth, &as),
		b->rp, BLEN(b), &as);
	b->wp += 8;
}

static int
ccmpdecrypt(Wkey *k, Wifipkt *w, Block *b, uvlong tsc)
{
	uchar auth[32], nonce[13];
	AESstate as;

	if(BLEN(b) < 8)
		return -1;

	b->wp -= 8;
	return aesCCMdecrypt(2, 8, nonce, auth,
		setupCCMP(k, w, tsc, nonce, auth, &as),
		b->rp, BLEN(b), &as);
}
