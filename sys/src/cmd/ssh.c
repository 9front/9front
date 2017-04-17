#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>
#include <auth.h>

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

typedef struct
{
	u32int		seq;
	u32int		kex;
	Chachastate	cs1;
	Chachastate	cs2;
	char		*v;
	char		eof;

	uchar		*r;
	uchar		*w;
	uchar		b[1<<15];
} Oneway;

int nsid;
uchar sid[256];

int fd, pid1, pid2, intr, raw, debug;
char *user, *status, *host, *cmd;

Oneway recv, send;

void
shutdown(void)
{
	int pid = getpid();
	if(pid1 && pid1 != pid)
		postnote(PNPROC, pid1, "shutdown");
	if(pid2 && pid2 != pid)
		postnote(PNPROC, pid2, "shutdown");
}

void
catch(void*, char *msg)
{
	if(strstr(msg, "interrupt") != nil){
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

	if(intr)
		return 1;
	memset(err, 0, sizeof(err));
	errstr(err, sizeof(err));
	r = strstr(err, "interrupt") != nil;
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
			}
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

void
unexpected(char *info)
{
	char *s;
	int n, c;

	switch(recv.r[0]){
	case MSG_DISCONNECT:
		if(unpack(recv.r, recv.w-recv.r, "_us", &c, &s, &n) < 0)
			break;
		sysfatal("disconnect: (%d) %.*s", c, n, s);
		break;
	case MSG_IGNORE:
	case MSG_GLOBAL_REQUEST:
		return;
	case MSG_DEBUG:
		if(unpack(recv.r, recv.w-recv.r, "__sb", &s, &n, &c) < 0)
			break;
		if(c != 0) fprint(2, "%s: %.*s\n", argv0, n, s);
		return;
	case MSG_USERAUTH_BANNER:
		if(unpack(recv.r, recv.w-recv.r, "_s", &s, &n) < 0)
			break;
		if(raw) write(2, s, n);
		return;
	}
	sysfatal("%s got: %.*H", info, (int)(recv.w - recv.r), recv.r);
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

/* libsec */
extern mpint* pkcs1padbuf(uchar *buf, int len, mpint *modulus, int blocktype);
extern int asn1encodedigest(DigestState* (*fun)(uchar*, ulong, uchar*, DigestState*),
	uchar *digest, uchar *buf, int len);

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
	static char hostkeyalgs[] = "ssh-rsa";
	static char cipheralgs[] = "chacha20-poly1305@openssh.com";
	static char zipalgs[] = "none";
	static char macalgs[] = "";
	static char langs[] = "";

	uchar cookie[16], x[32], yc[32], z[32], k[32+1], h[SHA2_256dlen], *ys, *ks, *sig;
	uchar k12[2*ChachaKeylen];
	int nk, nys, nks, nsig;
	DigestState *ds;
	mpint *S, *K;
	RSApub *pub;

	ds = hashstr(send.v, strlen(send.v), nil);	
	ds = hashstr(recv.v, strlen(recv.v), ds);

	genrandom(cookie, sizeof(cookie));
	sendpkt("b[ssssssssssbu", MSG_KEXINIT,
		cookie, sizeof(cookie),
		kexalgs, sizeof(kexalgs)-1,
		hostkeyalgs, sizeof(hostkeyalgs)-1,
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
			unexpected("KEXINIT");
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
		unexpected("ECDH_INIT");
		goto Next1;
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
		unexpected("NEWKEYS");
		goto Next2;
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

int
auth(char *username, char *servicename)
{
	static char sshuserauth[] = "ssh-userauth";
	static char publickey[] = "publickey";

	uchar pk[4096], sig[4096];
	int npk, nsig;

	int afd, n;
	char *s;
	mpint *S;
	AuthRpc *rpc;
	RSApub *pub;

	sendpkt("bs", MSG_SERVICE_REQUEST, sshuserauth, sizeof(sshuserauth)-1);
Next0:	switch(recvpkt()){
	default:
		unexpected("SERVICE_REQUEST");
		goto Next0;
	case MSG_SERVICE_ACCEPT:
		break;
	}

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
			username, strlen(username),
			servicename, strlen(servicename),
			publickey, sizeof(publickey)-1,
			0,
			sshrsa, sizeof(sshrsa)-1,
			pk, npk);
Next1:		switch(recvpkt()){
		default:
			unexpected("USERAUTH_REQUEST");
			goto Next1;
		case MSG_USERAUTH_FAILURE:
			continue;
		case MSG_USERAUTH_SUCCESS:
		case MSG_USERAUTH_PK_OK:
			break;
		}

		/* sign sid and the userauth request */
		n = pack(send.b, sizeof(send.b), "sbsssbss",
			sid, nsid,
			MSG_USERAUTH_REQUEST,
			username, strlen(username),
			servicename, strlen(servicename),
			publickey, sizeof(publickey)-1,
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
			username, strlen(username),
			servicename, strlen(servicename),
			publickey, sizeof(publickey)-1,
			1,
			sshrsa, sizeof(sshrsa)-1,
			pk, npk,
			sig, nsig);
Next2:		switch(recvpkt()){
		default:
			unexpected("USERAUTH_REQUEST");
			goto Next2;
		case MSG_USERAUTH_FAILURE:
			continue;
		case MSG_USERAUTH_SUCCESS:
			break;
		}
		rsapubfree(pub);
		auth_freerpc(rpc);
		close(afd);
		return 0;
	}
	rsapubfree(pub);
	auth_freerpc(rpc);
	close(afd);
	return -1;	
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
} tty = {
	"dumb",
	0,
	0,
	0,
	0,
};

void
rawon(void)
{
	int ctl;
	char *s;

	close(0);
	if(open("/dev/cons", OREAD) != 0)
		sysfatal("open: %r");
	close(1);
	if(open("/dev/cons", OWRITE) != 1)
		sysfatal("open: %r");
	dup(1, 2);
	if((ctl = open("/dev/consctl", OWRITE)) >= 0)
		write(ctl, "rawon", 5);
	if(s = getenv("TERM")){
		tty.term = s;
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
}

void
usage(void)
{
	fprint(2, "usage: %s [-dR] [-u user] [user@]host [cmd]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	static char buf[8*1024];
	static QLock sl;
	int b, n, c;
	char *s;
	uchar *p;

	quotefmtinstall();
	fmtinstall('B', mpfmt);
	fmtinstall('H', encodefmt);

	s = getenv("TERM");
	raw = s != nil && strcmp(s, "dumb") != 0;
	free(s);

	ARGBEGIN {
	case 'd':
		debug++;
		break;
	case 'R':
		raw = 0;
		break;
	case 'u':
		user = EARGF(usage());
		break;
	} ARGEND;

	if(argc == 0)
		usage();

	host = *argv++;
	if(user == nil){
		s = strchr(host, '@');
		if(s != nil){
			*s++ = '\0';
			user = host;
			host = s;
		}
	}
	for(cmd = nil; *argv != nil; argv++){
		if(cmd == nil)
			cmd = strdup(*argv);
		else {
			s = smprint("%s %q", cmd, *argv);
			free(cmd);
			cmd = s;
		}
	}
	if(cmd != nil)
		raw = 0;

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

	kex(0);

	if(user == nil)
		user = getuser();
	if(auth(user, "ssh-connection") < 0)
		sysfatal("auth: %r");

	/* open hailing frequencies */
	sendpkt("bsuuu", MSG_CHANNEL_OPEN,
		"session", 7,
		0,
		sizeof(buf),
		sizeof(buf));

	while((send.eof | recv.eof) == 0){
		if((int)(send.kex - send.seq) <= 0 || (int)(recv.kex - recv.seq) <= 0){
			qlock(&sl);
			kex(0);
			qunlock(&sl);
		}
		switch(recvpkt()){
		default:
			unexpected("CHANNEL");
			continue;
		case MSG_KEXINIT:
			qlock(&sl);
			kex(1);
			qunlock(&sl);
			continue;
		case MSG_CHANNEL_WINDOW_ADJUST:
			continue;
		case MSG_CHANNEL_EXTENDED_DATA:
			if(unpack(recv.r, recv.w-recv.r, "_uus", &c, &b, &s, &n) < 0)
				unexpected("CHANNEL_EXTENDED_DATA");
			if(b == 1) write(2, s, n);
			sendpkt("buu", MSG_CHANNEL_WINDOW_ADJUST, c, n);
			continue;
		case MSG_CHANNEL_DATA:
			if(unpack(recv.r, recv.w-recv.r, "_us", &c, &s, &n) < 0)
				unexpected("CHANNEL_DATA");
			write(1, s, n);
			sendpkt("buu", MSG_CHANNEL_WINDOW_ADJUST, c, n);
			continue;
		case MSG_CHANNEL_EOF:
			recv.eof = 1;
			if(!raw) write(1, "", 0);
			continue;
		case MSG_CHANNEL_OPEN_FAILURE:
			if(unpack(recv.r, recv.w-recv.r, "_uus", &c, &b, &s, &n) < 0)
				unexpected("CHANNEL_OPEN_FAILURE");
			sysfatal("channel open failure: (%d) %.*s", b, n, s);
			break;
		case MSG_CHANNEL_OPEN_CONFIRMATION:
			if(raw) {
				rawon();
				sendpkt("busbsuuuus", MSG_CHANNEL_REQUEST,
					0,
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
					0,
					"shell", 5,
					0);
			} else {
				sendpkt("busbs", MSG_CHANNEL_REQUEST,
					0,
					"exec", 4,
					0,
					cmd, strlen(cmd));
			}
			if(pid2)
				continue;
			pid1 = getpid();
			notify(catch);
			atexit(shutdown);
			n = rfork(RFPROC|RFMEM);
			if(n){
				pid2 = n;
				continue;
			}
			qlock(&sl);
			for(;;){
				qunlock(&sl);
				n = read(0, buf, sizeof(buf));
				qlock(&sl);
				if(n < 0 && wasintr()){
					sendpkt("busbs", MSG_CHANNEL_REQUEST,
						0,
						"signal", 6,
						0,
						"INT", 3);
					intr = 0;
					continue;
				}
				if(n <= 0)
					break;
				sendpkt("bus", MSG_CHANNEL_DATA,
					0,
					buf, n);
			}
			send.eof = 1;
			sendpkt("bu", MSG_CHANNEL_EOF, 0);
			qunlock(&sl);
			break;
		case MSG_CHANNEL_REQUEST:
			if(unpack(recv.r, recv.w-recv.r, "_usb.", &c, &s, &n, &b, &p) < 0)
				unexpected("CHANNEL_REQUEST");
			if(n == 11 && memcmp(s, "exit-signal", n) == 0){
				if(unpack(p, recv.w-p, "s", &s, &n) < 0)
					continue;
				if(n != 0 && status == nil)
					status = smprint("%.*s", n, s);
			} else if(n == 11 && memcmp(s, "exit-status", n) == 0){
				if(unpack(p, recv.w-p, "u", &n) < 0)
					continue;
				if(n != 0 && status == nil)
					status = smprint("%d", n);
			} else {
				fprint(2, "%s: channel request: %.*s\n", argv0, n, s);
			}
			continue;
		case MSG_CHANNEL_CLOSE:
			break;
		}
		break;
	}
	exits(status);
}
