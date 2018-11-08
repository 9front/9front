#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>
#include <auth.h>
#include <authsrv.h>

enum {
	MSG_DISCONNECT = 1,
	MSG_IGNORE,
	MSG_UNIMPLEMENTED,
	MSG_DEBUG,
	MSG_SERVICE_REQUEST,
	MSG_SERVICE_ACCEPT,

	MSG_KEXINIT = 20,
	MSG_NEWKEYS,

	MSG_ECDH_INIT = 30,
	MSG_ECDH_REPLY,

	MSG_USERAUTH_REQUEST = 50,
	MSG_USERAUTH_FAILURE,
	MSG_USERAUTH_SUCCESS,
	MSG_USERAUTH_BANNER,

	MSG_USERAUTH_PK_OK = 60,
	MSG_USERAUTH_INFO_REQUEST = 60,
	MSG_USERAUTH_INFO_RESPONSE = 61,

	MSG_GLOBAL_REQUEST = 80,
	MSG_REQUEST_SUCCESS,
	MSG_REQUEST_FAILURE,

	MSG_CHANNEL_OPEN = 90,
	MSG_CHANNEL_OPEN_CONFIRMATION,
	MSG_CHANNEL_OPEN_FAILURE,
	MSG_CHANNEL_WINDOW_ADJUST,
	MSG_CHANNEL_DATA,
	MSG_CHANNEL_EXTENDED_DATA,
	MSG_CHANNEL_EOF,
	MSG_CHANNEL_CLOSE,
	MSG_CHANNEL_REQUEST,
	MSG_CHANNEL_SUCCESS,
	MSG_CHANNEL_FAILURE,
};


enum {
	Overhead = 256,		// enougth for MSG_CHANNEL_DATA header
	MaxPacket = 1<<15,
	WinPackets = 8,		// (1<<15) * 8 = 256K
};

int MaxPwTries = 3; // retry this often for keyboard-interactive

typedef struct
{
	u32int		seq;
	u32int		kex;
	u32int		chan;

	int		win;
	int		pkt;
	int		eof;

	Chachastate	cs1;
	Chachastate	cs2;

	uchar		*r;
	uchar		*w;
	uchar		b[Overhead + MaxPacket];

	char		*v;
	int		pid;
	Rendez;
} Oneway;

int nsid;
uchar sid[256];
char thumb[2*SHA2_256dlen+1], *thumbfile;

int fd, intr, raw, debug;
char *user, *service, *status, *host, *cmd;

Oneway recv, send;
void dispatch(void);

void
shutdown(void)
{
	recv.eof = send.eof = 1;
	if(send.pid > 0)
		postnote(PNPROC, send.pid, "shutdown");
}

void
catch(void*, char *msg)
{
	if(strcmp(msg, "interrupt") == 0){
		intr = 1;
		noted(NCONT);
	}
	noted(NDFLT);
}

int
wasintr(void)
{
	char err[ERRMAX];
	int r;

	memset(err, 0, sizeof(err));
	errstr(err, sizeof(err));
	r = strcmp(err, "interrupted") == 0;
	errstr(err, sizeof(err));
	return r;
}

#define PUT4(p, u) (p)[0] = (u)>>24, (p)[1] = (u)>>16, (p)[2] = (u)>>8, (p)[3] = (u)
#define GET4(p)	(u32int)(p)[3] | (u32int)(p)[2]<<8 | (u32int)(p)[1]<<16 | (u32int)(p)[0]<<24

int
vpack(uchar *p, int n, char *fmt, va_list a)
{
	uchar *p0 = p, *e = p+n;
	u32int u;
	mpint *m;
	void *s;
	int c;

	for(;;){
		switch(c = *fmt++){
		case '\0':
			return p - p0;
		case '_':
			if(++p > e) goto err;
			break;
		case '.':
			*va_arg(a, void**) = p;
			break;
		case 'b':
			if(p >= e) goto err;
			*p++ = va_arg(a, int);
			break;
		case 'm':
			m = va_arg(a, mpint*);
			u = (mpsignif(m)+8)/8;
			if(p+4 > e) goto err;
			PUT4(p, u), p += 4;
			if(u > e-p) goto err;
			mptober(m, p, u), p += u;
			break;
		case '[':
		case 's':
			s = va_arg(a, void*);
			u = va_arg(a, int);
			if(c == 's'){
				if(p+4 > e) goto err;
				PUT4(p, u), p += 4;
			}
			if(u > e-p) goto err;
			memmove(p, s, u);
			p += u;
			break;
		case 'u':
			u = va_arg(a, int);
			if(p+4 > e) goto err;
			PUT4(p, u), p += 4;
			break;
		}
	}
err:
	return -1;
}

int
vunpack(uchar *p, int n, char *fmt, va_list a)
{
	uchar *p0 = p, *e = p+n;
	u32int u;
	mpint *m;
	void *s;

	for(;;){
		switch(*fmt++){
		case '\0':
			return p - p0;
		case '_':
			if(++p > e) goto err;
			break;
		case '.':
			*va_arg(a, void**) = p;
			break;
		case 'b':
			if(p >= e) goto err;
			*va_arg(a, int*) = *p++;
			break;
		case 'm':
			if(p+4 > e) goto err;
			u = GET4(p), p += 4;
			if(u > e-p) goto err;
			m = va_arg(a, mpint*);
			betomp(p, u, m), p += u;
			break;
		case 's':
			if(p+4 > e) goto err;
			u = GET4(p), p += 4;
			if(u > e-p) goto err;
			*va_arg(a, void**) = p;
			*va_arg(a, int*) = u;
			p += u;
			break;
		case '[':
			s = va_arg(a, void*);
			u = va_arg(a, int);
			if(u > e-p) goto err;
			memmove(s, p, u);
			p += u;
			break;
		case 'u':
			if(p+4 > e) goto err;
			u = GET4(p);
			*va_arg(a, int*) = u;
			p += 4;
			break;
		}
	}
err:
	return -1;
}

int
pack(uchar *p, int n, char *fmt, ...)
{
	va_list a;
	va_start(a, fmt);
	n = vpack(p, n, fmt, a);
	va_end(a);
	return n;
}
int
unpack(uchar *p, int n, char *fmt, ...)
{
	va_list a;
	va_start(a, fmt);
	n = vunpack(p, n, fmt, a);
	va_end(a);
	return n;
}

void
setupcs(Oneway *c, uchar otk[32])
{
	uchar iv[8];

	memset(otk, 0, 32);
	pack(iv, sizeof(iv), "uu", 0, c->seq);
	chacha_setiv(&c->cs1, iv);
	chacha_setiv(&c->cs2, iv);
	chacha_setblock(&c->cs1, 0);
	chacha_setblock(&c->cs2, 0);
	chacha_encrypt(otk, 32, &c->cs2);
}

void
sendpkt(char *fmt, ...)
{
	static uchar buf[sizeof(send.b)];
	int n, pad;
	va_list a;

	va_start(a, fmt);
	n = vpack(send.b, sizeof(send.b), fmt, a);
	va_end(a);
	if(n < 0) {
toobig:		sysfatal("sendpkt: message too big");
		return;
	}
	send.r = send.b;
	send.w = send.b+n;

if(debug > 1)
	fprint(2, "sendpkt: (%d) %.*H\n", send.r[0], (int)(send.w-send.r), send.r);

	if(nsid){
		/* undocumented */
		pad = ChachaBsize - ((5+n) % ChachaBsize) + 4;
	} else {
		for(pad=4; (5+n+pad) % 8; pad++)
			;
	}
	prng(send.w, pad);
	n = pack(buf, sizeof(buf)-16, "ub[[", 1+n+pad, pad, send.b, n, send.w, pad);
	if(n < 0) goto toobig;
	if(nsid){
		uchar otk[32];

		setupcs(&send, otk);
		chacha_encrypt(buf, 4, &send.cs1);
		chacha_encrypt(buf+4, n-4, &send.cs2);
		poly1305(buf, n, otk, sizeof(otk), buf+n, nil);
		n += 16;
	}

	if(write(fd, buf, n) != n)
		sysfatal("write: %r");

	send.seq++;
}

int
readall(int fd, uchar *data, int len)
{
	int n, tot;

	for(tot = 0; tot < len; tot += n){
		n = read(fd, data+tot, len-tot);
		if(n <= 0){
			if(n < 0 && wasintr()){
				n = 0;
				continue;
			} else if(n == 0)
				werrstr("eof");
			break;
		}
	}
	return tot;
}

int
recvpkt(void)
{
	uchar otk[32], tag[16];
	DigestState *ds = nil;
	int n;

	if(readall(fd, recv.b, 4) != 4)
		sysfatal("read1: %r");
	if(nsid){
		setupcs(&recv, otk);
		ds = poly1305(recv.b, 4, otk, sizeof(otk), nil, nil);
		chacha_encrypt(recv.b, 4, &recv.cs1);
		unpack(recv.b, 4, "u", &n);
		n += 16;
	} else {
		unpack(recv.b, 4, "u", &n);
	}
	if(n < 8 || n > sizeof(recv.b)){
badlen:		sysfatal("bad length %d", n);
	}
	if(readall(fd, recv.b, n) != n)
		sysfatal("read2: %r");
	if(nsid){
		n -= 16;
		if(n < 0) goto badlen;
		poly1305(recv.b, n, otk, sizeof(otk), tag, ds);
		if(tsmemcmp(tag, recv.b+n, 16) != 0)
			sysfatal("bad tag");
		chacha_encrypt(recv.b, n, &recv.cs2);
	}
	n -= recv.b[0]+1;
	if(n < 1) goto badlen;

	recv.r = recv.b + 1;
	recv.w = recv.r + n;
	recv.seq++;

if(debug > 1)
	fprint(2, "recvpkt: (%d) %.*H\n", recv.r[0], (int)(recv.w-recv.r), recv.r);

	return recv.r[0];
}

static char sshrsa[] = "ssh-rsa";

int
rsapub2ssh(RSApub *rsa, uchar *data, int len)
{
	return pack(data, len, "smm", sshrsa, sizeof(sshrsa)-1, rsa->ek, rsa->n);
}

RSApub*
ssh2rsapub(uchar *data, int len)
{
	RSApub *pub;
	char *s;
	int n;

	pub = rsapuballoc();
	pub->n = mpnew(0);
	pub->ek = mpnew(0);
	if(unpack(data, len, "smm", &s, &n, pub->ek, pub->n) < 0
	|| n != sizeof(sshrsa)-1 || memcmp(s, sshrsa, n) != 0){
		rsapubfree(pub);
		return nil;
	}
	return pub;
}

int
rsasig2ssh(RSApub *pub, mpint *S, uchar *data, int len)
{
	int l = (mpsignif(pub->n)+7)/8;
	if(4+7+4+l > len)
		return -1;
	mptober(S, data+4+7+4, l);
	return pack(data, len, "ss", sshrsa, sizeof(sshrsa)-1, data+4+7+4, l);
}

mpint*
ssh2rsasig(uchar *data, int len)
{
	mpint *m;
	char *s;
	int n;

	m = mpnew(0);
	if(unpack(data, len, "sm", &s, &n, m) < 0
	|| n != sizeof(sshrsa)-1 || memcmp(s, sshrsa, n) != 0){
		mpfree(m);
		return nil;
	}
	return m;
}

mpint*
pkcs1digest(uchar *data, int len, RSApub *pub)
{
	uchar digest[SHA1dlen], buf[256];

	sha1(data, len, digest, nil);
	return pkcs1padbuf(buf, asn1encodedigest(sha1, digest, buf, sizeof(buf)), pub->n, 1);
}

int
pkcs1verify(uchar *data, int len, RSApub *pub, mpint *S)
{
	mpint *V;
	int ret;

	V = pkcs1digest(data, len, pub);
	ret = V != nil;
	if(ret){
		rsaencrypt(pub, S, S);
		ret = mpcmp(V, S) == 0;
		mpfree(V);
	}
	return ret;
}

DigestState*
hashstr(void *data, ulong len, DigestState *ds)
{
	uchar l[4];
	pack(l, 4, "u", len);
	return sha2_256((uchar*)data, len, nil, sha2_256(l, 4, nil, ds));
}

void
kdf(uchar *k, int nk, uchar *h, char x, uchar *out, int len)
{
	uchar digest[SHA2_256dlen], *out0;
	DigestState *ds;
	int n;

	ds = hashstr(k, nk, nil);
	ds = sha2_256(h, sizeof(digest), nil, ds);
	ds = sha2_256((uchar*)&x, 1, nil, ds);
	sha2_256(sid, nsid, digest, ds);
	for(out0=out;;){
		n = len;
		if(n > sizeof(digest))
			n = sizeof(digest);
		memmove(out, digest, n);
		len -= n;
		if(len == 0)
			break;
		out += n;
		ds = hashstr(k, nk, nil);
		ds = sha2_256(h, sizeof(digest), nil, ds);
		sha2_256(out0, out-out0, digest, ds);
	}
}

void
kex(int gotkexinit)
{
	static char kexalgs[] = "curve25519-sha256,curve25519-sha256@libssh.org";
	static char cipheralgs[] = "chacha20-poly1305@openssh.com";
	static char zipalgs[] = "none";
	static char macalgs[] = "";
	static char langs[] = "";

	uchar cookie[16], x[32], yc[32], z[32], k[32+1], h[SHA2_256dlen], *ys, *ks, *sig;
	uchar k12[2*ChachaKeylen];
	int i, nk, nys, nks, nsig;
	DigestState *ds;
	mpint *S, *K;
	RSApub *pub;

	ds = hashstr(send.v, strlen(send.v), nil);	
	ds = hashstr(recv.v, strlen(recv.v), ds);

	genrandom(cookie, sizeof(cookie));
	sendpkt("b[ssssssssssbu", MSG_KEXINIT,
		cookie, sizeof(cookie),
		kexalgs, sizeof(kexalgs)-1,
		sshrsa, sizeof(sshrsa)-1,
		cipheralgs, sizeof(cipheralgs)-1,
		cipheralgs, sizeof(cipheralgs)-1,
		macalgs, sizeof(macalgs)-1,
		macalgs, sizeof(macalgs)-1,
		zipalgs, sizeof(zipalgs)-1,
		zipalgs, sizeof(zipalgs)-1,
		langs, sizeof(langs)-1,
		langs, sizeof(langs)-1,
		0,
		0);
	ds = hashstr(send.r, send.w-send.r, ds);

	if(!gotkexinit){
	Next0:	switch(recvpkt()){
		default:
			dispatch();
			goto Next0;
		case MSG_KEXINIT:
			break;
		}
	}
	ds = hashstr(recv.r, recv.w-recv.r, ds);

	if(debug){
		char *tab[] = {
			"kexalgs", "hostalgs",
			"cipher1", "cipher2",
			"mac1", "mac2",
			"zip1", "zip2",
			"lang1", "lang2",
			nil,
		}, **t, *s;
		uchar *p = recv.r+17;
		int n;
		for(t=tab; *t != nil; t++){
			if(unpack(p, recv.w-p, "s.", &s, &n, &p) < 0)
				break;
			fprint(2, "%s: %.*s\n", *t, n, s);
		}
	}

	curve25519_dh_new(x, yc);
	yc[31] &= ~0x80;

	sendpkt("bs", MSG_ECDH_INIT, yc, sizeof(yc));
Next1:	switch(recvpkt()){
	default:
		dispatch();
		goto Next1;
	case MSG_KEXINIT:
		sysfatal("inception");
	case MSG_ECDH_REPLY:
		if(unpack(recv.r, recv.w-recv.r, "_sss", &ks, &nks, &ys, &nys, &sig, &nsig) < 0)
			sysfatal("bad ECDH_REPLY");
		break;
	}

	if(nys != 32)
		sysfatal("bad server ECDH ephermal public key length");

	ds = hashstr(ks, nks, ds);
	ds = hashstr(yc, 32, ds);
	ds = hashstr(ys, 32, ds);

	if(thumb[0] == 0){
		Thumbprint *ok;

		sha2_256(ks, nks, h, nil);
		i = enc64(thumb, sizeof(thumb), h, sizeof(h));
		while(i > 0 && thumb[i-1] == '=')
			i--;
		thumb[i] = '\0';

		if(debug)
			fprint(2, "host fingerprint: %s\n", thumb);

		ok = initThumbprints(thumbfile, nil, "ssh");
		if(ok == nil || !okThumbprint(h, sizeof(h), ok)){
			if(ok != nil) werrstr("unknown host");
			fprint(2, "%s: %r\n", argv0);
			fprint(2, "verify hostkey: %s %.*[\n", sshrsa, nks, ks);
			fprint(2, "add thumbprint after verification:\n");
			fprint(2, "\techo 'ssh sha256=%s server=%s' >> %q\n", thumb, host, thumbfile);
			sysfatal("checking hostkey failed: %r");
		}
		freeThumbprints(ok);
	}

	if((pub = ssh2rsapub(ks, nks)) == nil)
		sysfatal("bad server public key");
	if((S = ssh2rsasig(sig, nsig)) == nil)
		sysfatal("bad server signature");

	curve25519_dh_finish(x, ys, z);

	K = betomp(z, 32, nil);
	nk = (mpsignif(K)+8)/8;
	mptober(K, k, nk);
	mpfree(K);

	ds = hashstr(k, nk, ds);
	sha2_256(nil, 0, h, ds);
	if(!pkcs1verify(h, sizeof(h), pub, S))
		sysfatal("server verification failed");
	mpfree(S);
	rsapubfree(pub);

	sendpkt("b", MSG_NEWKEYS);
Next2:	switch(recvpkt()){
	default:
		dispatch();
		goto Next2;
	case MSG_KEXINIT:
		sysfatal("inception");
	case MSG_NEWKEYS:
		break;
	}

	/* next key exchange */
	recv.kex = recv.seq + 100000;
	send.kex = send.seq + 100000;

	if(nsid == 0)
		memmove(sid, h, nsid = sizeof(h));

	kdf(k, nk, h, 'C', k12, sizeof(k12));
	setupChachastate(&send.cs1, k12+1*ChachaKeylen, ChachaKeylen, nil, 64/8, 20);
	setupChachastate(&send.cs2, k12+0*ChachaKeylen, ChachaKeylen, nil, 64/8, 20);

	kdf(k, nk, h, 'D', k12, sizeof(k12));
	setupChachastate(&recv.cs1, k12+1*ChachaKeylen, ChachaKeylen, nil, 64/8, 20);
	setupChachastate(&recv.cs2, k12+0*ChachaKeylen, ChachaKeylen, nil, 64/8, 20);
}

static char *authnext;

int
authok(char *meth)
{
	int ok = authnext == nil || strstr(authnext, meth) != nil;
if(debug)
	fprint(2, "userauth %s %s\n", meth, ok ? "ok" : "skipped");
	return ok;
}

int
authfailure(char *meth)
{
	char *s;
	int n, partial;

	if(unpack(recv.r, recv.w-recv.r, "_sb", &s, &n, &partial) < 0)
		sysfatal("bad auth failure response");
	free(authnext);
	authnext = smprint("%.*s", n, s);
if(debug)
	fprint(2, "userauth %s failed: partial=%d, next=%s\n", meth, partial, authnext);
	return partial != 0 || !authok(meth);
}

int
noneauth(void)
{
	static char authmeth[] = "none";

	if(!authok(authmeth))
		return -1;

	sendpkt("bsss", MSG_USERAUTH_REQUEST,
		user, strlen(user),
		service, strlen(service),
		authmeth, sizeof(authmeth)-1);

Next0:	switch(recvpkt()){
	default:
		dispatch();
		goto Next0;
	case MSG_USERAUTH_FAILURE:
		werrstr("authentication needed");
		authfailure(authmeth);
		return -1;
	case MSG_USERAUTH_SUCCESS:
		return 0;
	}
}

int
pubkeyauth(void)
{
	static char authmeth[] = "publickey";

	uchar pk[4096], sig[4096];
	int npk, nsig;

	int afd, n;
	char *s;
	mpint *S;
	AuthRpc *rpc;
	RSApub *pub;

	if(!authok(authmeth))
		return -1;

	if((afd = open("/mnt/factotum/rpc", ORDWR)) < 0)
		return -1;
	if((rpc = auth_allocrpc(afd)) == nil){
		close(afd);
		return -1;
	}

	s = "proto=rsa service=ssh role=client";
	if(auth_rpc(rpc, "start", s, strlen(s)) != ARok){
		auth_freerpc(rpc);
		close(afd);
		return -1;
	}

	pub = rsapuballoc();
	pub->n = mpnew(0);
	pub->ek = mpnew(0);

	while(auth_rpc(rpc, "read", nil, 0) == ARok){
		s = rpc->arg;
		if(strtomp(s, &s, 16, pub->n) == nil)
			break;
		if(*s++ != ' ')
			continue;
		if(strtomp(s, nil, 16, pub->ek) == nil)
			continue;
		npk = rsapub2ssh(pub, pk, sizeof(pk));

		sendpkt("bsssbss", MSG_USERAUTH_REQUEST,
			user, strlen(user),
			service, strlen(service),
			authmeth, sizeof(authmeth)-1,
			0,
			sshrsa, sizeof(sshrsa)-1,
			pk, npk);
Next1:		switch(recvpkt()){
		default:
			dispatch();
			goto Next1;
		case MSG_USERAUTH_FAILURE:
			if(authfailure(authmeth))
				goto Failed;
			continue;
		case MSG_USERAUTH_SUCCESS:
		case MSG_USERAUTH_PK_OK:
			break;
		}

		/* sign sid and the userauth request */
		n = pack(send.b, sizeof(send.b), "sbsssbss",
			sid, nsid,
			MSG_USERAUTH_REQUEST,
			user, strlen(user),
			service, strlen(service),
			authmeth, sizeof(authmeth)-1,
			1,
			sshrsa, sizeof(sshrsa)-1,
			pk, npk);
		S = pkcs1digest(send.b, n, pub);
		n = snprint((char*)send.b, sizeof(send.b), "%B", S);
		mpfree(S);

		if(auth_rpc(rpc, "write", (char*)send.b, n) != ARok)
			break;
		if(auth_rpc(rpc, "read", nil, 0) != ARok)
			break;

		S = strtomp(rpc->arg, nil, 16, nil);
		nsig = rsasig2ssh(pub, S, sig, sizeof(sig));
		mpfree(S);

		/* send final userauth request with the signature */
		sendpkt("bsssbsss", MSG_USERAUTH_REQUEST,
			user, strlen(user),
			service, strlen(service),
			authmeth, sizeof(authmeth)-1,
			1,
			sshrsa, sizeof(sshrsa)-1,
			pk, npk,
			sig, nsig);
Next2:		switch(recvpkt()){
		default:
			dispatch();
			goto Next2;
		case MSG_USERAUTH_FAILURE:
			if(authfailure(authmeth))
				goto Failed;
			continue;
		case MSG_USERAUTH_SUCCESS:
			break;
		}
		rsapubfree(pub);
		auth_freerpc(rpc);
		close(afd);
		return 0;
	}
Failed:
	rsapubfree(pub);
	auth_freerpc(rpc);
	close(afd);
	return -1;	
}

int
passauth(void)
{
	static char authmeth[] = "password";
	UserPasswd *up;

	if(!authok(authmeth))
		return -1;

	up = auth_getuserpasswd(auth_getkey, "proto=pass service=ssh user=%q server=%q thumb=%q",
		user, host, thumb);
	if(up == nil)
		return -1;

	sendpkt("bsssbs", MSG_USERAUTH_REQUEST,
		user, strlen(user),
		service, strlen(service),
		authmeth, sizeof(authmeth)-1,
		0,
		up->passwd, strlen(up->passwd));

	memset(up->passwd, 0, strlen(up->passwd));
	free(up);

Next0:	switch(recvpkt()){
	default:
		dispatch();
		goto Next0;
	case MSG_USERAUTH_FAILURE:
		werrstr("wrong password");
		authfailure(authmeth);
		return -1;
	case MSG_USERAUTH_SUCCESS:
		return 0;
	}
}

int
kbintauth(void)
{
	static char authmeth[] = "keyboard-interactive";
	int tries;

	char *name, *inst, *s, *a;
	int fd, i, n, m;
	int nquest, echo;
	uchar *ans, *answ;
	tries = 0;

	if(!authok(authmeth))
		return -1;

Loop:
	if(++tries > MaxPwTries)
		return -1;
		
	sendpkt("bsssss", MSG_USERAUTH_REQUEST,
		user, strlen(user),
		service, strlen(service),
		authmeth, sizeof(authmeth)-1,
		"", 0,
		"", 0);

Next0:	switch(recvpkt()){
	default:
		dispatch();
		goto Next0;
	case MSG_USERAUTH_FAILURE:
		werrstr("keyboard-interactive failed");
		if(authfailure(authmeth))
			return -1;
		goto Loop;
	case MSG_USERAUTH_SUCCESS:
		return 0;
	case MSG_USERAUTH_INFO_REQUEST:
		break;
	}
Retry:
	if((fd = open("/dev/cons", OWRITE)) < 0)
		return -1;

	if(unpack(recv.r, recv.w-recv.r, "_ss.", &name, &n, &inst, &m, &recv.r) < 0)
		sysfatal("bad info request: name, inst");

	while(n > 0 && strchr("\r\n\t ", name[n-1]) != nil)
		n--;
	while(m > 0 && strchr("\r\n\t ", inst[m-1]) != nil)
		m--;

	if(n > 0)
		fprint(fd, "%.*s\n", n, name);
	if(m > 0)
		fprint(fd, "%.*s\n", m, inst);

	/* lang, nprompt */
	if(unpack(recv.r, recv.w-recv.r, "su.", &s, &n, &nquest, &recv.r) < 0)
		sysfatal("bad info request: lang, #quest");

	ans = answ = nil;
	for(i = 0; i < nquest; i++){
		if(unpack(recv.r, recv.w-recv.r, "sb.", &s, &n, &echo, &recv.r) < 0)
			sysfatal("bad info request: question [%d]", i);

		while(n > 0 && strchr("\r\n\t :", s[n-1]) != nil)
			n--;
		s[n] = '\0';

		if((a = readcons(s, nil, !echo)) == nil)
			sysfatal("readcons: %r");

		n = answ - ans;
		m = strlen(a)+4;
		if((s = realloc(ans, n + m)) == nil)
			sysfatal("realloc: %r");
		ans = (uchar*)s;
		answ = ans+n;
		answ += pack(answ, m, "s", a, m-4);
	}

	sendpkt("bu[", MSG_USERAUTH_INFO_RESPONSE, i, ans, answ - ans);
	free(ans);
	close(fd);

Next1:	switch(recvpkt()){
	default:
		dispatch();
		goto Next1;
	case MSG_USERAUTH_INFO_REQUEST:
		goto Retry;
	case MSG_USERAUTH_FAILURE:
		werrstr("keyboard-interactive failed");
		if(authfailure(authmeth))
			return -1;
		goto Loop;
	case MSG_USERAUTH_SUCCESS:
		return 0;
	}
}

void
dispatch(void)
{
	char *s;
	uchar *p;
	int n, b, c;

	switch(recv.r[0]){
	case MSG_IGNORE:
		return;
	case MSG_GLOBAL_REQUEST:
		if(unpack(recv.r, recv.w-recv.r, "_sb", &s, &n, &b) < 0)
			break;
		if(debug)
			fprint(2, "%s: global request: %.*s\n", argv0, n, s);
		if(b != 0)
			sendpkt("b", MSG_REQUEST_FAILURE);
		return;
	case MSG_DISCONNECT:
		if(unpack(recv.r, recv.w-recv.r, "_us", &c, &s, &n) < 0)
			break;
		sysfatal("disconnect: (%d) %.*s", c, n, s);
		return;
	case MSG_DEBUG:
		if(unpack(recv.r, recv.w-recv.r, "__sb", &s, &n, &c) < 0)
			break;
		if(c != 0 || debug) fprint(2, "%s: %.*s\n", argv0, n, s);
		return;
	case MSG_USERAUTH_BANNER:
		if(unpack(recv.r, recv.w-recv.r, "_s", &s, &n) < 0)
			break;
		if(raw) write(2, s, n);
		return;
	case MSG_CHANNEL_DATA:
		if(unpack(recv.r, recv.w-recv.r, "_us", &c, &s, &n) < 0)
			break;
		if(c != recv.chan)
			break;
		if(write(1, s, n) != n)
			sysfatal("write out: %r");
	Winadjust:
		recv.win -= n;
		if(recv.win < recv.pkt){
			n = WinPackets*recv.pkt;
			recv.win += n;
			sendpkt("buu", MSG_CHANNEL_WINDOW_ADJUST, send.chan, n);
		}
		return;
	case MSG_CHANNEL_EXTENDED_DATA:
		if(unpack(recv.r, recv.w-recv.r, "_uus", &c, &b, &s, &n) < 0)
			break;
		if(c != recv.chan)
			break;
		if(b == 1) write(2, s, n);
		goto Winadjust;
	case MSG_CHANNEL_WINDOW_ADJUST:
		if(unpack(recv.r, recv.w-recv.r, "_uu", &c, &n) < 0)
			break;
		if(c != recv.chan)
			break;
		send.win += n;
		if(send.win >= send.pkt)
			rwakeup(&send);
		return;
	case MSG_CHANNEL_REQUEST:
		if(unpack(recv.r, recv.w-recv.r, "_usb.", &c, &s, &n, &b, &p) < 0)
			break;
		if(c != recv.chan)
			break;
		if(n == 11 && memcmp(s, "exit-signal", n) == 0){
			if(unpack(p, recv.w-p, "s", &s, &n) < 0)
				break;
			if(n != 0 && status == nil)
				status = smprint("%.*s", n, s);
			c = MSG_CHANNEL_SUCCESS;
		} else if(n == 11 && memcmp(s, "exit-status", n) == 0){
			if(unpack(p, recv.w-p, "u", &n) < 0)
				break;
			if(n != 0 && status == nil)
				status = smprint("%d", n);
			c = MSG_CHANNEL_SUCCESS;
		} else {
			if(debug)
				fprint(2, "%s: channel request: %.*s\n", argv0, n, s);
			c = MSG_CHANNEL_FAILURE;
		}
		if(b != 0)
			sendpkt("bu", c, recv.chan);
		return;
	case MSG_CHANNEL_EOF:
		recv.eof = 1;
		if(!raw) write(1, "", 0);
		return;
	case MSG_CHANNEL_CLOSE:
		shutdown();
		return;
	case MSG_KEXINIT:
		kex(1);
		return;
	}
	sysfatal("got: %.*H", (int)(recv.w - recv.r), recv.r);
}

char*
readline(void)
{
	uchar *p;

	for(p = send.b; p < &send.b[sizeof(send.b)-1]; p++){
		*p = '\0';
		if(read(fd, p, 1) != 1 || *p == '\n')
			break;
	}
	while(p >= send.b && (*p == '\n' || *p == '\r'))
		*p-- = '\0';
	return (char*)send.b;
}

static struct {
	char	*term;
	int	xpixels;
	int	ypixels;
	int	lines;
	int	cols;
} tty;

void
getdim(void)
{
	char *s;

	if(s = getenv("XPIXELS")){
		tty.xpixels = atoi(s);
		free(s);
	}
	if(s = getenv("YPIXELS")){
		tty.ypixels = atoi(s);
		free(s);
	}
	if(s = getenv("LINES")){
		tty.lines = atoi(s);
		free(s);
	}
	if(s = getenv("COLS")){
		tty.cols = atoi(s);
		free(s);
	}
}

void
rawon(void)
{
	int ctl;

	close(0);
	if(open("/dev/cons", OREAD) != 0)
		sysfatal("open: %r");
	close(1);
	if(open("/dev/cons", OWRITE) != 1)
		sysfatal("open: %r");
	dup(1, 2);
	if((ctl = open("/dev/consctl", OWRITE)) >= 0){
		write(ctl, "rawon", 5);
		write(ctl, "winchon", 7);	/* vt(1): interrupt note on window change */
	}
	getdim();
}

#pragma	   varargck    type  "k"   char*

kfmt(Fmt *f)
{
	char *s, *p;
	int n;

	s = va_arg(f->args, char*);
	n = fmtstrcpy(f, "'");
	while((p = strchr(s, '\'')) != nil){
		*p = '\0';
		n += fmtstrcpy(f, s);
		*p = '\'';
		n += fmtstrcpy(f, "'\\''");
		s = p+1;
	}
	n += fmtstrcpy(f, s);
	n += fmtstrcpy(f, "'");
	return n;
}

void
usage(void)
{
	fprint(2, "usage: %s [-dR] [-t thumbfile] [-T tries] [-u user] [-h] [user@]host [cmd args...]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	static QLock sl;
	int b, n, c;
	char *s;

	quotefmtinstall();
	fmtinstall('B', mpfmt);
	fmtinstall('H', encodefmt);
	fmtinstall('[', encodefmt);
	fmtinstall('k', kfmt);

	tty.term = getenv("TERM");
	if(tty.term == nil)
		tty.term = "";
	raw = *tty.term != 0;

	ARGBEGIN {
	case 'd':
		debug++;
		break;
	case 'R':
		raw = 0;
		break;
	case 'r':
		raw = 2; /* bloody */
		break;
	case 'u':
		user = EARGF(usage());
		break;
	case 'h':
		host = EARGF(usage());
		break;
	case 't':
		thumbfile = EARGF(usage());
		break;
	case 'T':
		MaxPwTries = strtol(EARGF(usage()), &s, 0);
		if(*s != 0) usage();
		break;
	} ARGEND;

	if(host == nil){
		if(argc == 0)
			usage();
		host = *argv++;
	}

	if(user == nil){
		s = strchr(host, '@');
		if(s != nil){
			*s++ = '\0';
			user = host;
			host = s;
		}
	}

	for(cmd = nil; *argv != nil; argv++){
		if(cmd == nil){
			cmd = strdup(*argv);
			if(raw == 1)
				raw = 0;
		}else{
			s = smprint("%s %k", cmd, *argv);
			free(cmd);
			cmd = s;
		}
	}

	if((fd = dial(netmkaddr(host, nil, "ssh"), nil, nil, nil)) < 0)
		sysfatal("dial: %r");

	send.v = "SSH-2.0-(9)";
	fprint(fd, "%s\r\n", send.v);
	recv.v = readline();
	if(debug)
		fprint(2, "server verison: %s\n", recv.v);
	if(strncmp("SSH-2.0-", recv.v, 8) != 0)
		sysfatal("bad server version: %s", recv.v);
	recv.v = strdup(recv.v);

	send.l = recv.l = &sl;

	if(user == nil)
		user = getuser();
	if(thumbfile == nil)
		thumbfile = smprint("%s/lib/sshthumbs", getenv("home"));

	kex(0);

	sendpkt("bs", MSG_SERVICE_REQUEST, "ssh-userauth", 12);
Next0:	switch(recvpkt()){
	default:
		dispatch();
		goto Next0;
	case MSG_SERVICE_ACCEPT:
		break;
	}

	service = "ssh-connection";
	if(noneauth() < 0 && pubkeyauth() < 0 && passauth() < 0 && kbintauth() < 0)
		sysfatal("auth: %r");

	recv.pkt = MaxPacket;
	recv.win = WinPackets*recv.pkt;
	recv.chan = 0;

	/* open hailing frequencies */
	sendpkt("bsuuu", MSG_CHANNEL_OPEN,
		"session", 7,
		recv.chan,
		recv.win,
		recv.pkt);

Next1:	switch(recvpkt()){
	default:
		dispatch();
		goto Next1;
	case MSG_CHANNEL_OPEN_FAILURE:
		if(unpack(recv.r, recv.w-recv.r, "_uus", &c, &b, &s, &n) < 0)
			n = strlen(s = "???");
		sysfatal("channel open failure: (%d) %.*s", b, n, s);
	case MSG_CHANNEL_OPEN_CONFIRMATION:
		break;
	}

	if(unpack(recv.r, recv.w-recv.r, "_uuuu", &recv.chan, &send.chan, &send.win, &send.pkt) < 0)
		sysfatal("bad channel open confirmation");
	if(send.pkt <= 0 || send.pkt > MaxPacket)
		send.pkt = MaxPacket;

	notify(catch);
	atexit(shutdown);

	recv.pid = getpid();
	n = rfork(RFPROC|RFMEM);
	if(n < 0)
		sysfatal("fork: %r");

	/* parent reads and dispatches packets */
	if(n > 0) {
		send.pid = n;
		while(recv.eof == 0){
			recvpkt();
			qlock(&sl);					
			dispatch();
			if((int)(send.kex - send.seq) <= 0 || (int)(recv.kex - recv.seq) <= 0)
				kex(0);
			qunlock(&sl);
		}
		exits(status);
	}

	/* child reads input and sends packets */
	qlock(&sl);
	if(raw) {
		rawon();
		sendpkt("busbsuuuus", MSG_CHANNEL_REQUEST,
			send.chan,
			"pty-req", 7,
			0,
			tty.term, strlen(tty.term),
			tty.cols,
			tty.lines,
			tty.xpixels,
			tty.ypixels,
			"", 0);
	}
	if(cmd == nil){
		sendpkt("busb", MSG_CHANNEL_REQUEST,
			send.chan,
			"shell", 5,
			0);
	} else if(*cmd == '#') {
		sendpkt("busbs", MSG_CHANNEL_REQUEST,
			send.chan,
			"subsystem", 9,
			0,
			cmd+1, strlen(cmd)-1);
	} else {
		sendpkt("busbs", MSG_CHANNEL_REQUEST,
			send.chan,
			"exec", 4,
			0,
			cmd, strlen(cmd));
	}
	for(;;){
		static uchar buf[MaxPacket];
		qunlock(&sl);
		n = read(0, buf, send.pkt);
		qlock(&sl);
		if(send.eof)
			break;
		if(n < 0 && wasintr())
			intr = 1;
		if(intr){
			if(!raw) break;
			getdim();
			sendpkt("busbuuuu", MSG_CHANNEL_REQUEST,
				send.chan,
				"window-change", 13,
				0,
				tty.cols,
				tty.lines,
				tty.xpixels,
				tty.ypixels);
			sendpkt("busbs", MSG_CHANNEL_REQUEST,
				send.chan,
				"signal", 6,
				0,
				"INT", 3);
			intr = 0;
			continue;
		}
		if(n <= 0)
			break;
		send.win -= n;
		while(send.win < 0)
			rsleep(&send);
		sendpkt("bus", MSG_CHANNEL_DATA,
			send.chan,
			buf, n);
	}
	if(send.eof++ == 0)
		sendpkt("bu", raw ? MSG_CHANNEL_CLOSE : MSG_CHANNEL_EOF, send.chan);
	qunlock(&sl);

	exits(nil);
}
