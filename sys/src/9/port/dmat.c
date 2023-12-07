/*
 * Dynamic Mac Address Translation (DMAT)
 *
 * Wifi does not allow spoofing of the source mac which breaks
 * bridging. To solve this we proxy mac addresses, maintaining
 * a translation table from ip address to destination mac address.
 * Upstream ARP and NDP packets get ther source mac address changed
 * to proxy and a translation entry is added with the original mac
 * for downstream translation. The proxy does not appear in the
 * table.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/netif.h"
#include "../port/etherif.h"
#include "../ip/ip.h"
#include "../ip/ipv6.h"

void
dmatproxy(Block *bp, int upstream, uchar proxy[Eaddrlen], DMAT *t)
{
	static uchar arp4[] = {
		0x00, 0x01,
		0x08, 0x00,
		0x06, 0x04,
		0x00,
	};
	uchar ip[IPaddrlen], mac[Eaddrlen], *targ, *end, *a, *o;
	ulong csum, c, h;
	Etherpkt *pkt;
	int proto, i;
	DMTE *te;

	end = bp->wp;
	pkt = (Etherpkt*)bp->rp;
	a = pkt->data;
	if(a >= end)
		return;

	if(upstream)
		memmove(pkt->s, proxy, Eaddrlen);
	else if(t->map == 0 || (pkt->d[0]&1) != 0 || memcmp(pkt->d, proxy, Eaddrlen) != 0)
		return;

	targ = nil;
	switch(pkt->type[0]<<8 | pkt->type[1]){
	default:
		return;
	case ETIP4:
	case ETIP6:
		switch(a[0]&0xF0){
		default:
			return;
		case IP_VER4:
			if(a+IP4HDR > end || (a[0]&15) < IP_HLEN4)
				return;
			v4tov6(ip, a+12+4*(upstream==0));
			proto = a[9];
			a += (a[0]&15)*4;
			break;
		case IP_VER6:
			if(a+IP6HDR > end)
				return;
			memmove(ip, a+8+16*(upstream==0), 16);
			proto = a[6];
			a += IP6HDR;
			break;
		}
		if(!upstream)
			break;
		switch(proto){
		case ICMPv6:
			if(a+8 > end)
				return;
			switch(a[0]){
			default:
				return;
			case 133:	/* Router Solicitation */
				o = a+8;
				break;
			case 134:	/* Router Advertisement */
				o = a+8+8;
				break;
			case 136:	/* Neighbor Advertisement */
				targ = a+8;
				/* wet floor */
			case 135:	/* Neighbor Solicitation */
				o = a+8+16;
				break;
			case 137:	/* Redirect */
				o = a+8+16+16;
				break;
			}
			memset(mac, 0xFF, Eaddrlen);
			csum = (a[2]<<8 | a[3])^0xFFFF;
			while(o+8 <= end && o[1] != 0){
				switch(o[0]){
				case SRC_LLADDR:
				case TARGET_LLADDR:
					for(i=0; i<Eaddrlen; i += 2)
						csum += (o[2+i]<<8 | o[3+i])^0xFFFF;
					memmove(mac, o+2, Eaddrlen);
					memmove(o+2, proxy, Eaddrlen);
					for(i=0; i<Eaddrlen; i += 2)
						csum += (o[2+i]<<8 | o[3+i]);
					break;
				}
				o += o[1]*8;
			}
			while((c = csum >> 16) != 0)
				csum = (csum & 0xFFFF) + c;
			csum ^= 0xFFFF;
			a[2] = csum>>8;
			a[3] = csum;
			break;
		case UDP:	/* for BOOTP */
			if(a+42 > end
			|| (a[0]<<8 | a[1]) != 68
			|| (a[2]<<8 | a[3]) != 67
			|| a[8] != 1
			|| a[9] != 1
			|| a[10] != Eaddrlen
			|| (a[18]&0x80) != 0
			|| memcmp(a+36, proxy, Eaddrlen) == 0)
				return;

			csum = (a[6]<<8 | a[7])^0xFFFF;

			/* set the broadcast flag so response reaches us */
			csum += (a[18]<<8)^0xFFFF;
			a[18] |= 0x80;
			csum += (a[18]<<8);

			while((c = csum >> 16) != 0)
				csum = (csum & 0xFFFF) + c;
			csum ^= 0xFFFF;

			a[6] = csum>>8;
			a[7] = csum;
		default:
			return;
		}
		break;
	case ETARP:
		if(a+26 > end || memcmp(a, arp4, sizeof(arp4)) != 0 || (a[7] != 1 && a[7] != 2))
			return;
		v4tov6(ip, a+14+10*(upstream==0));
		if(upstream){
			memmove(mac, a+8, Eaddrlen);
			memmove(a+8, proxy, Eaddrlen);
		}
		break;
	}

Again:
	h = (	(ip[IPaddrlen-1] ^ proxy[2])<<24 |
		(ip[IPaddrlen-2] ^ proxy[3])<<16 |
		(ip[IPaddrlen-3] ^ proxy[4])<<8  |
		(ip[IPaddrlen-4] ^ proxy[5]) ) % nelem(t->tab);
	te = &t->tab[h];
	h &= 63;

	if(upstream){
		if((mac[0]&1) != 0 || memcmp(mac, proxy, Eaddrlen) == 0)
			return;
		for(i=0; te->valid && i<nelem(t->tab); i++){
			if(memcmp(te->ip, ip, IPaddrlen) == 0)
				break;
			if(++te >= &t->tab[nelem(t->tab)])
				te = t->tab;
		}
		memmove(te->mac, mac, Eaddrlen);
		memmove(te->ip, ip, IPaddrlen);
		te->valid = 1;
		t->map |= 1ULL<<h;
		if(targ != nil){
			memmove(ip, targ, IPaddrlen);
			targ = nil;
			goto Again;
		}
	} else {
		if((t->map>>h & 1) == 0)
			return;
		for(i=0; te->valid && i<nelem(t->tab); i++){
			if(memcmp(te->ip, ip, IPaddrlen) == 0){
				memmove(pkt->d, te->mac, Eaddrlen);
				return;
			}
			if(++te >= &t->tab[nelem(t->tab)])
				te = t->tab;
		}
	}
}
