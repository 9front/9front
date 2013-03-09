#include <u.h>
#include <libc.h>
#include <ip.h>
#include <mp.h>
#include <libsec.h>
#include <auth.h>

enum {
	PTKlen = 512/8,
	GTKlen = 256/8,
	Noncelen = 32,
	Eaddrlen = 6,
};

enum {
	Fptk	= 1<<3,
	Fins	= 1<<6,
	Fack	= 1<<7,
	Fmic	= 1<<8,
	Fsec	= 1<<9,
	Ferr	= 1<<10,
	Freq	= 1<<11,
	Fenc	= 1<<12,

	Keydescrlen = 1+2+2+8+32+16+8+8+16+2,
};

typedef struct Keydescr Keydescr;
struct Keydescr
{
	uchar	type[1];
	uchar	flags[2];
	uchar	keylen[2];
	uchar	repc[8];
	uchar	nonce[32];
	uchar	eapoliv[16];
	uchar	rsc[8];
	uchar	id[8];
	uchar	mic[16];
	uchar	datalen[2];
	uchar	data[];
};

int	prompt;
int	debug;
int	fd, cfd;
char	*dev;
char	devdir[40];
uchar	ptk[PTKlen];
char	essid[32+1];
uvlong	lastrepc;

uchar	rsnie[] = {
	0x30,			/* RSN */
	0x14,			/* length */
	0x01, 0x00,		/* version 1 */
	0x00, 0x0F, 0xAC, 0x02,	/* group cipher suite TKIP */
	0x01, 0x00,		/* peerwise cipher suite count 1 */
	0x00, 0x0F, 0xAC, 0x02,	/* peerwise cipher suite TKIP */
	0x01, 0x00,		/* authentication suite count 1 */
	0x00, 0x0F, 0xAC, 0x02,	/* authentication suite PSK */
	0x00, 0x00,		/* capabilities */
};

uchar	wpaie[] = {
	0xdd,			/* vendor specific */
	0x16,			/* length */
	0x00, 0x50, 0xf2, 0x01,	/* WPAIE type 1 */
	0x01, 0x00,		/* version 1 */
	0x00, 0x50, 0xf2, 0x02,	/* group cipher suite TKIP */
	0x01, 0x00,		/* peerwise cipher suite count 1 */
	0x00, 0x50, 0xf2, 0x02,	/* peerwise cipher suite TKIP */
	0x01, 0x00,		/* authentication suite count 1 */
	0x00, 0x50, 0xf2, 0x02,	/* authentication suite PSK */
};

/* only WPA for now */
uchar	*rsne = wpaie;
int	rsnelen = sizeof(wpaie);

char*
getessid(void)
{
	char buf[8*1024], *f[2], *p, *e;
	int fd, n;

	snprint(buf, sizeof(buf), "%s/ifstats", devdir);
	if((fd = open(buf, OREAD)) < 0)
		return nil;
	n = read(fd, buf, sizeof(buf)-1);
	close(fd);
	if(n > 0){
		buf[n] = 0;
		for(p = buf; (e = strchr(p, '\n')) != nil; p = e){
			*e++ = 0;
			if(tokenize(p, f, 2) != 2)
				continue;
			if(strcmp(f[0], "essid:") != 0)
				continue;
			strncpy(essid, f[1], 32);
			return essid;
		}
	}
	return nil;
}

int
getptk(	uchar smac[Eaddrlen], uchar amac[Eaddrlen], 
	uchar snonce[Noncelen],  uchar anonce[Noncelen], 
	uchar ptk[PTKlen])
{
	uchar buf[2*Eaddrlen + 2*Noncelen], *p;
	AuthRpc *rpc;
	int afd, ret;
	char *s;

	ret = -1;
	s = nil;
	rpc = nil;
	if((afd = open("/mnt/factotum/rpc", ORDWR)) < 0)
		goto out;
	if((rpc = auth_allocrpc(afd)) == nil)
		goto out;
	if((s = getessid()) == nil)
		goto out;
	if((s = smprint("proto=wpapsk role=client essid=%q", s)) == nil)
		goto out;
	if((ret = auth_rpc(rpc, "start", s, strlen(s))) != ARok)
		goto out;
	p = buf;
	memmove(p, smac, Eaddrlen); p += Eaddrlen;
	memmove(p, amac, Eaddrlen); p += Eaddrlen;
	memmove(p, snonce, Noncelen); p += Noncelen;
	memmove(p, anonce, Noncelen); p += Noncelen;
	if((ret = auth_rpc(rpc, "write", buf, p - buf)) != ARok)
		goto out;
	if((ret = auth_rpc(rpc, "read", nil, 0)) != ARok)
		goto out;
	if(rpc->narg != PTKlen)
		goto out;
	memmove(ptk, rpc->arg, PTKlen);
	ret = 0;
out:
	free(s);
	if(afd >= 0) close(afd);
	if(rpc != nil) auth_freerpc(rpc);
	return ret;
}

int
Hfmt(Fmt *f)
{
	uchar *p, *e;

	p = va_arg(f->args, uchar*);
	e = p;
	if(f->prec >= 0)
		e += f->prec;
	for(; p != e; p++)
		if(fmtprint(f, "%.2x", *p) < 0)
			return -1;
	return 0;
}

void
dumpkeydescr(Keydescr *kd)
{
	static struct {
		int	flag;
		char	*name;
	} flags[] = {
		Fptk,	"ptk",
		Fins,	"ins",
		Fack,	"ack",
		Fmic,	"mic",
		Fsec,	"sec",
		Ferr,	"err",
		Freq,	"req",
		Fenc,	"enc",
	};
	int i, f;

	f = kd->flags[0]<<8 | kd->flags[1];
	fprint(2, "type=%.*H flags=%.*H ( ",
		sizeof(kd->type), kd->type,
		sizeof(kd->flags), kd->flags);
	for(i=0; i<nelem(flags); i++)
		if(flags[i].flag & f)
			fprint(2, "%s ", flags[i].name);
	fprint(2, ") len=%.*H\nrepc=%.*H nonce=%.*H\neapoliv=%.*H rsc=%.*H id=%.*H mic=%.*H\n",
		sizeof(kd->keylen), kd->keylen,
		sizeof(kd->repc), kd->repc,
		sizeof(kd->nonce), kd->nonce,
		sizeof(kd->eapoliv), kd->eapoliv,
		sizeof(kd->rsc), kd->rsc,
		sizeof(kd->id), kd->id,
		sizeof(kd->mic), kd->mic);
	i = kd->datalen[0]<<8 | kd->datalen[1];
	fprint(2, "data[%.4x]=%.*H\n\n", i, i, kd->data);
}

void
reply(uchar smac[Eaddrlen], uchar amac[Eaddrlen], int flags, Keydescr *kd, uchar *data, int datalen)
{
	uchar buf[4096], mic[MD5dlen], *m, *p = buf;

	memmove(p, amac, Eaddrlen); p += Eaddrlen;
	memmove(p, smac, Eaddrlen); p += Eaddrlen;
	*p++ = 0x88;
	*p++ = 0x8e;

	m = p;
	*p++ = 0x01;
	*p++ = 0x03;
	datalen += Keydescrlen;
	*p++ = datalen >> 8;
	*p++ = datalen;
	datalen -= Keydescrlen;

	memmove(p, kd, Keydescrlen);
	kd = (Keydescr*)p;
	kd->flags[0] = flags >> 8;
	kd->flags[1] = flags;
	kd->datalen[0] = datalen >> 8;
	kd->datalen[1] = datalen;
	p = kd->data;
	memmove(p, data, datalen);
	p += datalen;

	memset(kd->mic, 0, sizeof(kd->mic));
	if(flags & Fmic){
		hmac_md5(m, p - m, ptk, 16, mic, nil);
		memmove(kd->mic, mic, sizeof(kd->mic));
	}
	if(debug != 0){
		fprint(2, "reply %E -> %E: ", smac, amac);
		dumpkeydescr(kd);
	}
	datalen = p - buf;
	if(write(fd, buf, datalen) != datalen)
		sysfatal("write: %r");
}

void
usage(void)
{
	fprint(2, "%s: [-dp] [-s essid] dev\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	uchar mac[Eaddrlen], buf[1024];
	char addr[128];
	int n;

	quotefmtinstall();
	fmtinstall('H', Hfmt);
	fmtinstall('E', eipfmt);

	ARGBEGIN {
	case 'd':
		debug = 1;
		break;
	case 'p':
		prompt = 1;
		break;
	case 's':
		strncpy(essid, EARGF(usage()), 32);
		break;
	default:
		usage();
	} ARGEND;

	if(*argv != nil)
		dev = *argv++;

	if(*argv != nil || dev == nil)
		usage();

	if(myetheraddr(mac, dev) < 0)
		sysfatal("can't get mac address: %r");

	snprint(addr, sizeof(addr), "%s!0x888e", dev);
	if((fd = dial(addr, nil, devdir, &cfd)) < 0)
		sysfatal("dial: %r");

	if(essid[0] != 0)
		if(fprint(cfd, "essid %s", essid) < 0)
			sysfatal("write essid: %r");

	if(prompt){
		char *s;

		if(essid[0] == 0)
			getessid();
		if(essid[0] != 0)
			s = smprint("proto=wpapsk essid=%q !password?", essid);
		else
			s = smprint("proto=wpapsk essid? !password?");
		auth_getkey(s);
		free(s);
	}

	/*
	 * we use write() instead of fprint so message gets  written
	 * at once and not chunked up on fprint buffer.
	 */
	n = sprint((char*)buf, "auth %.*H", rsnelen, rsne);
	if(write(cfd, buf, n) != n)
		sysfatal("write auth: %r");

	if(!debug){
		switch(rfork(RFFDG|RFREND|RFPROC|RFNOWAIT)){
		default:
			exits(nil);
		case -1:
			sysfatal("fork: %r");
			return;
		case 0:
			break;
		}
	}
	
	for(;;){
		uchar smac[Eaddrlen], amac[Eaddrlen], snonce[Noncelen], anonce[Noncelen], *p, *e, *m;
		int proto, flags, kid;
		uvlong repc, rsc;
		Keydescr *kd;

		if((n = read(fd, buf, sizeof(buf))) < 0)
			sysfatal("read: %r");
		p = buf;
		e = buf+n;
		if(n < 2*Eaddrlen + 2)
			continue;
		memmove(smac, p, Eaddrlen); p += Eaddrlen;
		memmove(amac, p, Eaddrlen); p += Eaddrlen;
		proto = p[0]<<8 | p[1]; p += 2;
		if(proto != 0x888e || memcmp(smac, mac, Eaddrlen) != 0)
			continue;

		m = p;
		n = e - p;
		if(n < 4 || p[0] != 0x01 || p[1] != 0x03)
			continue;
		n = p[2]<<8 | p[3];
		p += 4;
		if(n < Keydescrlen || p + n > e)
			continue;
		kd = (Keydescr*)p;
		if(debug){
			fprint(2, "recv %E <- %E: ", smac, amac);
			dumpkeydescr(kd);
		}

		/* only WPA, RSN descriptor format not suppoted yet */
		if(kd->type[0] != 0xFE)
			continue;

		flags = kd->flags[0]<<8 | kd->flags[1];

		rsc =	(uvlong)kd->rsc[0] |
			(uvlong)kd->rsc[1]<<8 |
			(uvlong)kd->rsc[2]<<16 |
			(uvlong)kd->rsc[3]<<24 |
			(uvlong)kd->rsc[4]<<32 |
			(uvlong)kd->rsc[5]<<40;

		repc =	(uvlong)kd->repc[7] |
			(uvlong)kd->repc[6]<<8 |
			(uvlong)kd->repc[5]<<16 |
			(uvlong)kd->repc[4]<<24 |
			(uvlong)kd->repc[3]<<32 |
			(uvlong)kd->repc[2]<<40 |
			(uvlong)kd->repc[1]<<48 |
			(uvlong)kd->repc[0]<<56;

		if(repc <= lastrepc)
			continue;

		if((flags & Fmic) == 0){
			if((flags & (Fptk|Fack)) != (Fptk|Fack))
				continue;

			memmove(anonce, kd->nonce, sizeof(anonce));
			genrandom(snonce, sizeof(snonce));
			if(getptk(smac, amac, snonce, anonce, ptk) < 0)
				continue;

			/* ack key exchange with mic */
			memset(kd->rsc, 0, sizeof(kd->rsc));
			memset(kd->eapoliv, 0, sizeof(kd->eapoliv));
			memmove(kd->nonce, snonce, sizeof(kd->nonce));
			reply(smac, amac, (flags & ~(Fack|Fins)) | Fmic, kd, rsne, rsnelen);
		} else {
			uchar tmp[MD5dlen], mic[MD5dlen];

			/* check mic */
			memmove(tmp, kd->mic, sizeof(mic));
			memset(kd->mic, 0, sizeof(kd->mic));
			hmac_md5(m, e - m, ptk, 16, mic, nil);
			if(memcmp(tmp, mic, sizeof(mic)) != 0)
				continue;

			lastrepc = repc;

			if((flags & (Fptk|Fsec|Fack)) == (Fptk|Fack)){
				/* install peerwise receive key */
				if(fprint(cfd, "rxkey %.*H tkip:%.*H@%llux", Eaddrlen, amac, 32, ptk+32, rsc) < 0)
					sysfatal("write rxkey: %r");

				/* pick random 16bit tsc value for transmit */
				rsc = 1 + (truerand() & 0x7fff);
				memset(kd->rsc, 0, sizeof(kd->rsc));
				kd->rsc[0] = rsc;
				kd->rsc[1] = rsc>>8;
				memset(kd->eapoliv, 0, sizeof(kd->eapoliv));
				memset(kd->nonce, 0, sizeof(kd->nonce));
				reply(smac, amac, flags & ~Fack, kd, nil, 0);
				sleep(100);

				/* install peerwise transmit key */ 
				if(fprint(cfd, "txkey %.*H tkip:%.*H@%llux", Eaddrlen, amac, 32, ptk+32, rsc) < 0)
					sysfatal("write txkey: %r");
			} else
			if((flags & (Fptk|Fsec|Fack)) == (Fsec|Fack)){
				uchar seed[32], gtk[GTKlen];
				RC4state rs;
				int len;

				len = kd->datalen[1]<<8 | kd->datalen[0];
				if(len > sizeof(gtk))
					len = sizeof(gtk);
				memmove(gtk, kd->data, len);
				memmove(seed, kd->eapoliv, 16);
				memmove(seed+16, ptk+16, 16);
				setupRC4state(&rs, seed, sizeof(seed));
				rc4skip(&rs, 256);
				rc4(&rs, gtk, len);
	
				/* install group key */
				kid = (flags >> 4) & 3;
				if(fprint(cfd, "rxkey%d %.*H tkip:%.*H@%llux", kid, Eaddrlen, amac, len, gtk, rsc) < 0)
					sysfatal("write rxkey%d: %r", kid);

				memset(kd->rsc, 0, sizeof(kd->rsc));
				memset(kd->eapoliv, 0, sizeof(kd->eapoliv));
				memset(kd->nonce, 0, sizeof(kd->nonce));
				reply(smac, amac, flags & ~Fack, kd, nil, 0);
			}
		}
	}
}
