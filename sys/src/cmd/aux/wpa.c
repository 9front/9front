#include <u.h>
#include <libc.h>
#include <ip.h>
#include <mp.h>
#include <libsec.h>
#include <auth.h>

enum {
	PTKlen = 512/8,
	GTKlen = 256/8,

	MIClen = 16,

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

typedef struct Cipher Cipher;
struct Cipher
{
	char	*name;
	int	keylen;
};

Cipher	tkip = { "tkip", 32 };
Cipher	ccmp = { "ccmp", 16 };

Cipher	*peercipher;
Cipher	*groupcipher;

int	prompt;
int	debug;
int	fd, cfd;
char	*dev;
char	devdir[40];
uchar	ptk[PTKlen];
char	essid[32+1];
uvlong	lastrepc;

uchar rsntkipoui[4] = {0x00, 0x0F, 0xAC, 0x02};
uchar rsnccmpoui[4] = {0x00, 0x0F, 0xAC, 0x04};
uchar rsnapskoui[4] = {0x00, 0x0F, 0xAC, 0x02};

uchar	rsnie[] = {
	0x30,			/* RSN */
	0x14,			/* length */
	0x01, 0x00,		/* version 1 */
	0x00, 0x0F, 0xAC, 0x04,	/* group cipher suite CCMP */
	0x01, 0x00,		/* pairwise cipher suite count 1 */
	0x00, 0x0F, 0xAC, 0x04,	/* pairwise cipher suite CCMP */
	0x01, 0x00,		/* authentication suite count 1 */
	0x00, 0x0F, 0xAC, 0x02,	/* authentication suite PSK */
	0x00, 0x00,		/* capabilities */
};

uchar wpa1oui[4]    = {0x00, 0x50, 0xF2, 0x01};
uchar wpatkipoui[4] = {0x00, 0x50, 0xF2, 0x02};
uchar wpaapskoui[4] = {0x00, 0x50, 0xF2, 0x02};

uchar	wpaie[] = {
	0xdd,			/* vendor specific */
	0x16,			/* length */
	0x00, 0x50, 0xf2, 0x01,	/* WPAIE type 1 */
	0x01, 0x00,		/* version 1 */
	0x00, 0x50, 0xf2, 0x02,	/* group cipher suite TKIP */
	0x01, 0x00,		/* pairwise cipher suite count 1 */
	0x00, 0x50, 0xf2, 0x02,	/* pairwise cipher suite TKIP */
	0x01, 0x00,		/* authentication suite count 1 */
	0x00, 0x50, 0xf2, 0x02,	/* authentication suite PSK */
};

int
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

char*
getifstats(char *key, char *val, int nval)
{
	char buf[8*1024], *f[2], *p, *e;
	int fd, n;

	snprint(buf, sizeof(buf), "%s/ifstats", devdir);
	if((fd = open(buf, OREAD)) < 0)
		return nil;
	n = read(fd, buf, sizeof(buf)-1);
	close(fd);
	if(n <= 0)
		return nil;
	buf[n] = 0;
	for(p = buf; (e = strchr(p, '\n')) != nil; p = e){
		*e++ = 0;
		if(tokenize(p, f, 2) != 2)
			continue;
		if(strcmp(f[0], key) != 0)
			continue;
		strncpy(val, f[1], nval);
		val[nval-1] = 0;
		return val;
	}
	return nil;
}

char*
getessid(void)
{
	return getifstats("essid:", essid, sizeof(essid));
}

int
buildrsne(uchar rsne[258])
{
	char buf[1024];
	uchar brsne[258];
	int brsnelen;
	uchar *p, *w, *e;
	int i, n;

	if(getifstats("brsne:", buf, sizeof(buf)) == nil)
		return 0;	/* not an error, might be old kernel */

	brsnelen = hextob(buf, nil, brsne, sizeof(brsne));
	if(brsnelen <= 4){
trunc:		sysfatal("invalid or truncated RSNE; brsne: %s", buf);
		return 0;
	}

	w = rsne;
	p = brsne;
	e = p + brsnelen;
	if(p[0] == 0x30){
		p += 2;

		/* RSN */
		*w++ = 0x30;
		*w++ = 0;	/* length */
	} else if(p[0] == 0xDD){
		p += 2;
		if((e - p) < 4 || memcmp(p, wpa1oui, 4) != 0){
			sysfatal("unrecognized WPAIE type; brsne: %s", buf);
			return 0;
		}

		/* WPA */
		*w++ = 0xDD;
		*w++ = 0;	/* length */

		memmove(w, wpa1oui, 4);
		w += 4;
		p += 4;
	} else {
		sysfatal("unrecognized RSNE type; brsne: %s", buf);
		return 0;
	}

	if((e - p) < 6)
		goto trunc;

	*w++ = *p++;		/* version */
	*w++ = *p++;

	if(rsne[0] == 0x30){
		if(memcmp(p, rsnccmpoui, 4) == 0)
			groupcipher = &ccmp;
		else if(memcmp(p, rsntkipoui, 4) == 0)
			groupcipher = &tkip;
		else {
			sysfatal("unrecognized RSN group cipher; brsne: %s", buf);
			return 0;
		}
	} else {
		if(memcmp(p, wpatkipoui, 4) != 0){
			sysfatal("unrecognized WPA group cipher; brsne: %s", buf);
			return 0;
		}
		groupcipher = &tkip;
	}

	memmove(w, p, 4);	/* group cipher */
	w += 4;
	p += 4;

	if((e - p) < 6)
		goto trunc;

	*w++ = 0x01;		/* # of peer ciphers */
	*w++ = 0x00;
	n = *p++;
	n |= *p++ << 8;

	if(n <= 0)
		goto trunc;

	peercipher = &tkip;
	for(i=0; i<n; i++){
		if((e - p) < 4)
			goto trunc;

		if(rsne[0] == 0x30 && memcmp(p, rsnccmpoui, 4) == 0 && peercipher == &tkip)
			peercipher = &ccmp;
		p += 4;
	}
	if(peercipher == &ccmp)
		memmove(w, rsnccmpoui, 4);
	else if(rsne[0] == 0x30)
		memmove(w, rsntkipoui, 4);
	else
		memmove(w, wpatkipoui, 4);
	w += 4;

	if((e - p) < 6)
		goto trunc;

	*w++ = 0x01;		/* # of auth suites */
	*w++ = 0x00;
	n = *p++;
	n |= *p++ << 8;

	if(n <= 0)
		goto trunc;

	for(i=0; i<n; i++){
		if((e - p) < 4)
			goto trunc;

		/* look for PSK oui */
		if(rsne[0] == 0x30){
			if(memcmp(p, rsnapskoui, 4) == 0)
				break;
		} else {
			if(memcmp(p, wpaapskoui, 4) == 0)
				break;
		}
		p += 4;
	}
	if(i >= n){
		sysfatal("auth suite is not PSK; brsne: %s", buf);
		return 0;
	}

	memmove(w, p, 4);
	w += 4;

	if(rsne[0] == 0x30){
		/* RSN caps */
		*w++ = 0x00;
		*w++ = 0x00;
	}

	rsne[1] = (w - rsne) - 2;
	return w - rsne;
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
	fprint(2, "type=%.*H vers=%d flags=%.*H ( ",
		sizeof(kd->type), kd->type, kd->flags[1] & 7,
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
	fprint(2, "data[%.4x]=%.*H\n", i, i, kd->data);
}

int
rc4unwrap(uchar key[16], uchar iv[16], uchar *data, int len)
{
	uchar seed[32];
	RC4state rs;

	memmove(seed, iv, 16);
	memmove(seed+16, key, 16);
	setupRC4state(&rs, seed, sizeof(seed));
	rc4skip(&rs, 256);
	rc4(&rs, data, len);
	return len;
}

int
aesunwrap(uchar *key, int nkey, uchar *data, int len)
{
	static uchar IV[8] = { 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, };
	uchar B[16], *R;
	AESstate s;
	uint t;
	int n;

	len -= 8;
	if(len < 16 || (len % 8) != 0)
		return -1;
	n = len/8;
	t = n*6;
	setupAESstate(&s, key, nkey, 0);
	memmove(B, data, 8);
	memmove(data, data+8, n*8);
	do {
		for(R = data + (n - 1)*8; R >= data; t--, R -= 8){
			memmove(B+8, R, 8);
			B[7] ^= (t >> 0);
			B[6] ^= (t >> 8);
			B[5] ^= (t >> 16);
			B[4] ^= (t >> 24);
			aes_decrypt(s.dkey, s.rounds, B, B);
			memmove(R, B+8, 8);
		}
	} while(t > 0);
	if(memcmp(B, IV, 8) != 0)
		return -1;
	return n*8;
}

int
calcmic(Keydescr *kd, uchar *msg, int msglen)
{
	int vers;

	vers = kd->flags[1] & 7;
	memset(kd->mic, 0, MIClen);
	if(vers == 1){
		uchar digest[MD5dlen];

		hmac_md5(msg, msglen, ptk, 16, digest, nil);
		memmove(kd->mic, digest, MIClen);
		return 0;
	}
	if(vers == 2){
		uchar digest[SHA1dlen];

		hmac_sha1(msg, msglen, ptk, 16, digest, nil);
		memmove(kd->mic, digest, MIClen);
		return 0;
	}
	return -1;
}

int
checkmic(Keydescr *kd, uchar *msg, int msglen)
{
	uchar tmp[MIClen];

	memmove(tmp, kd->mic, MIClen);
	if(calcmic(kd, msg, msglen) != 0)
		return -1;
	return memcmp(tmp, kd->mic, MIClen) != 0;
}

void
reply(uchar smac[Eaddrlen], uchar amac[Eaddrlen], int flags, Keydescr *kd, uchar *data, int datalen)
{
	uchar buf[4096], *m, *p = buf;

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

	memset(kd->mic, 0, MIClen);
	if(flags & Fmic)
		calcmic(kd, m, p - m);
	if(debug != 0){
		fprint(2, "\nreply %E -> %E: ", smac, amac);
		dumpkeydescr(kd);
	}
	datalen = p - buf;
	if(write(fd, buf, datalen) != datalen)
		sysfatal("write: %r");
}

void
usage(void)
{
	fprint(2, "%s: [-dp12] [-s essid] dev\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	uchar mac[Eaddrlen], buf[1024];
	char addr[128];
	uchar *rsne;
	int rsnelen;
	int n;

	quotefmtinstall();
	fmtinstall('H', Hfmt);
	fmtinstall('E', eipfmt);

	rsne = nil;
	rsnelen = -1;
	peercipher = nil;
	groupcipher = nil;

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
	case '1':
		rsne = wpaie;
		rsnelen = sizeof(wpaie);
		peercipher = &tkip;
		groupcipher = &tkip;
		break;
	case '2':
		rsne = rsnie;
		rsnelen = sizeof(rsnie);
		peercipher = &ccmp;
		groupcipher = &ccmp;
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

	if(essid[0] != 0){
		if(fprint(cfd, "essid %s", essid) < 0)
			sysfatal("write essid: %r");
	} else {
		getessid();
		if(essid[0] == 0)
			sysfatal("no essid set");
	}

	if(prompt){
		char *s;

		s = smprint("proto=wpapsk essid=%q !password?", essid);
		auth_getkey(s);
		free(s);
	}

	if(rsnelen <= 0){
		static uchar brsne[258];

		rsne = brsne;
		rsnelen = buildrsne(rsne);
	}

	if(rsnelen <= 0){
		/* default is WPA */
		rsne = wpaie;
		rsnelen = sizeof(wpaie);
		peercipher = &tkip;
		groupcipher = &tkip;
	}

	if(debug)
		fprint(2, "rsne: %.*H\n", rsnelen, rsne);

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
		int proto, flags, vers, datalen;
		uvlong repc, rsc, tsc;
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
		if(n < 4 || (p[0] != 0x01 && p[0] != 0x02) || p[1] != 0x03)
			continue;
		n = p[2]<<8 | p[3];
		p += 4;
		if(n < Keydescrlen || p + n > e)
			continue;
		e = p + n;
		kd = (Keydescr*)p;
		if(debug){
			fprint(2, "\nrecv %E <- %E: ", smac, amac);
			dumpkeydescr(kd);
		}

		if(kd->type[0] != 0xFE && kd->type[0] != 0x02)
			continue;

		vers = kd->flags[1] & 7;
		flags = kd->flags[0]<<8 | kd->flags[1];
		datalen = kd->datalen[0]<<8 | kd->datalen[1];
		if(kd->data + datalen > e)
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
			uchar gtk[GTKlen];
			int gtklen, gtkkid;

			if(checkmic(kd, m, e - m) != 0){
				if(debug != 0)
					fprint(2, "bad mic\n");
				continue;
			}

			repc =	(uvlong)kd->repc[7] |
				(uvlong)kd->repc[6]<<8 |
				(uvlong)kd->repc[5]<<16 |
				(uvlong)kd->repc[4]<<24 |
				(uvlong)kd->repc[3]<<32 |
				(uvlong)kd->repc[2]<<40 |
				(uvlong)kd->repc[1]<<48 |
				(uvlong)kd->repc[0]<<56;
			if(repc <= lastrepc){
				if(debug != 0)
					fprint(2, "bad repc: %llux <= %llux\n", repc, lastrepc);
				continue;
			}
			lastrepc = repc;

			rsc =	(uvlong)kd->rsc[0] |
				(uvlong)kd->rsc[1]<<8 |
				(uvlong)kd->rsc[2]<<16 |
				(uvlong)kd->rsc[3]<<24 |
				(uvlong)kd->rsc[4]<<32 |
				(uvlong)kd->rsc[5]<<40;

			if(datalen > 0 && (flags & Fenc) != 0){
				if(vers == 1)
					datalen = rc4unwrap(ptk+16, kd->eapoliv, kd->data, datalen);
				else
					datalen = aesunwrap(ptk+16, 16, kd->data, datalen);
				if(datalen <= 0){
					if(debug != 0)
						fprint(2, "bad keywrap\n");
					continue;
				}
				if(debug != 0)
					fprint(2, "unwraped keydata[%.4x]=%.*H\n", datalen, datalen, kd->data);
			}

			gtklen = 0;
			gtkkid = -1;

			if(kd->type[0] != 0xFE || (flags & (Fptk|Fack)) == (Fptk|Fack)){
				uchar *p, *x, *e;

				p = kd->data;
				e = p + datalen;
				for(; p+2 <= e; p = x){
					if((x = p+2+p[1]) > e)
						break;
					if(debug != 0)
						fprint(2, "ie=%.2x data[%.2x]=%.*H\n", p[0], p[1], p[1], p+2);
					if(p[0] == 0x30){ /* RSN */
					}
					if(p[0] == 0xDD){ /* WPA */
						static uchar oui[] = { 0x00, 0x0f, 0xac, 0x01, };

						if(p+2+sizeof(oui) > x || memcmp(p+2, oui, sizeof(oui)) != 0)
							continue;
						if((flags & Fenc) == 0)
							continue;	/* ignore gorup key if unencrypted */
						gtklen = x - (p + 8);
						if(gtklen <= 0)
							continue;
						if(gtklen > sizeof(gtk))
							gtklen = sizeof(gtk);
						memmove(gtk, p + 8, gtklen);
						gtkkid = p[6] & 3;
					}
				}
			}

			if((flags & (Fptk|Fack)) == (Fptk|Fack)){
				if(vers != 1)	/* in WPA2, RSC is for group key only */
					tsc = 0LL;
				else {
					tsc = rsc;
					rsc = 0LL;
				}
				/* install pairwise receive key */
				if(fprint(cfd, "rxkey %.*H %s:%.*H@%llux", Eaddrlen, amac,
					peercipher->name, peercipher->keylen, ptk+32, tsc) < 0)
					sysfatal("write rxkey: %r");

				/* pick random 16bit tsc value for transmit */
				tsc = 1 + (truerand() & 0x7fff);
				memset(kd->rsc, 0, sizeof(kd->rsc));
				kd->rsc[0] = tsc;
				kd->rsc[1] = tsc>>8;
				memset(kd->eapoliv, 0, sizeof(kd->eapoliv));
				memset(kd->nonce, 0, sizeof(kd->nonce));
				reply(smac, amac, flags & ~(Fack|Fenc|Fsec), kd, nil, 0);
				sleep(100);

				/* install pairwise transmit key */ 
				if(fprint(cfd, "txkey %.*H %s:%.*H@%llux", Eaddrlen, amac,
					peercipher->name, peercipher->keylen, ptk+32, tsc) < 0)
					sysfatal("write txkey: %r");
			} else
			if((flags & (Fptk|Fsec|Fack)) == (Fsec|Fack)){
				if(kd->type[0] == 0xFE){
					/* WPA always RC4 encrypts the GTK, even tho the flag isnt set */
					if((flags & Fenc) == 0)
						datalen = rc4unwrap(ptk+16, kd->eapoliv, kd->data, datalen);
					gtklen = datalen;
					if(gtklen > sizeof(gtk))
						gtklen = sizeof(gtk);
					memmove(gtk, kd->data, gtklen);
					gtkkid = (flags >> 4) & 3;
				}

				memset(kd->rsc, 0, sizeof(kd->rsc));
				memset(kd->eapoliv, 0, sizeof(kd->eapoliv));
				memset(kd->nonce, 0, sizeof(kd->nonce));
				reply(smac, amac, flags & ~(Fenc|Fack), kd, nil, 0);
			} else
				continue;

			if(gtklen >= groupcipher->keylen && gtkkid != -1){
				/* install group key */
				if(fprint(cfd, "rxkey%d %.*H %s:%.*H@%llux",
					gtkkid, Eaddrlen, amac, 
					groupcipher->name, groupcipher->keylen, gtk, rsc) < 0)
					sysfatal("write rxkey%d: %r", gtkkid);
			}
		}
	}
}
