#include <u.h>
#include <libc.h>
#include <bio.h>
#include <mp.h>
#include <ctype.h>
#include <libsec.h>
#include <auth.h>

typedef struct TxIn TxIn;
typedef struct TxOut TxOut;
typedef struct Sig Sig;
typedef struct Word Word;

int afd;
AuthRpc *rpc;

struct Word {
	char *name;
	int val;
} words[];

struct Sig {
	char *priv;
	int loc;
	Sig *n;
};

struct TxIn {
	uchar prev[36];
	int scoldlen, sclen;
	uchar scold[10000];
	uchar sc[10000];
	Sig *sig;
};

struct TxOut {
	uvlong val;
	int sclen;
	uchar sc[10000];
};

Biobuf *bp;

int nin, nout;
TxIn *in[0xFD];
TxOut *out[0xFD];
uchar buf[65536];

void
varenc(uint i, uchar **b)
{
	if(i < 0xfd)
		*(*b)++ = i;
	else if(i <= 0xffff){
		*(*b)++ = 0xfd;
		*(*b)++ = i;
		*(*b)++ = i >> 8;
	}else{
		*(*b)++ = 0xfe;
		*(*b)++ = i;
		*(*b)++ = i >> 8;
		*(*b)++ = i >> 16;
		*(*b)++ = i >> 24;
	}
}


int
hexdec(char *c, uchar *u, int n)
{
	int i, v;
	char *b;
	static char *hexdig = "0123456789abcdef";
	
	memset(u, 0, n);
	for(i = 0; i < 2 * n; i++){
		b = strchr(hexdig, c[i]);
		if(b == nil)
			return -1;
		v = b - hexdig;
		if(i & 1)
			u[i>>1] |= v;
		else
			u[i>>1] |= v << 4;
	}
	return 0;
}

void
pushdat(uchar **scr, uchar *d, int n)
{
	if(n <= 0x4b)
		*(*scr)++ = n;
	else if(n <= 0xff){
		*(*scr)++ = 0x4c;
		*(*scr)++ = n;
	}else if(n <= 0xffff){
		*(*scr)++ = 0x4d;
		*(*scr)++ = n;
		*(*scr)++ = n >> 8;
	}else{
		*(*scr)++ = 0x4e;
		*(*scr)++ = n;
		*(*scr)++ = n >> 8;
		*(*scr)++ = n >> 16;
		*(*scr)++ = n >> 24;
	}
	memcpy(*scr, d, n);
	*scr += n;
}

void
doscript(char **args, int n, uchar *script, int *len, TxIn *ti)
{
	int i, k;
	Word *w;
	uchar *scr;
	uchar *b;
	char *s;
	Sig *si;
	
	scr = script;
	for(i = 0; i < n; i++){
		for(w = words; w->name; w++)
			if(strcmp(w->name, args[i]) == 0){
				*scr++ = w->val;
				goto next;
			}
		if(strncmp(args[i], "sig(", 4) == 0){
			if(in == nil)
				sysfatal("sig in out script");
			si = malloc(sizeof(*si));
			args[i][strlen(args[i])-1] = 0;
			si->priv = strdup(args[i] + 4);
			si->loc = scr - script;
			si->n = ti->sig;
			ti->sig = si;
			continue;
		}
		if(strncmp(args[i], "h160(", 5) == 0){
			b = mallocz(25, 1);
			args[i][strlen(args[i])-1] = 0;
			base58dec(args[i] + 5, b, 25);
			pushdat(&scr, b+1, 20);
			free(b);
			continue;
		}
		if(args[i][0] == '('){
			k = strtol(args[i] + 1, &s, 0);
			b = mallocz(k, 1);
			base58dec(s+1, b, k);
			pushdat(&scr, b, k);
			free(b);
			continue;
		}
		sysfatal("invalid word %s", args[i]);
next:	;
	}
	*len = scr - script;
}

int
serialize(uchar *buf, int sig)
{
	uchar *s;
	TxIn *ti;
	TxOut *to;
	int i;
	
	s = buf;
	*s++ = 1;
	*s++ = 0;
	*s++ = 0;
	*s++ = 0;
	*s++ = nin;
	for(i = 0; i < nin; i++){
		ti = in[i];
		memcpy(s, ti->prev, 36);
		s += 36;
		if(sig == -1){
			varenc(ti->sclen, &s);
			memcpy(s, ti->sc, ti->sclen);
			s += ti->sclen;
		}
		if(sig == i){
			memcpy(s, ti->scold, ti->scoldlen);
			s += ti->scoldlen;
		}
		*s++ = 0xff;
		*s++ = 0xff;
		*s++ = 0xff;
		*s++ = 0xff;
	}
	*s++ = nout;
	for(i = 0; i < nout; i++){
		to = out[i];
		*s++ = to->val;
		*s++ = to->val >> 8;
		*s++ = to->val >> 16;
		*s++ = to->val >> 24;
		*s++ = to->val >> 32;
		*s++ = to->val >> 40;
		*s++ = to->val >> 48;
		*s++ = to->val >> 56;
		varenc(to->sclen, &s);
		memcpy(s, to->sc, to->sclen);
		s += to->sclen;
	}
	*s++ = 0;
	*s++ = 0;
	*s++ = 0;
	*s++ = 0;
	if(sig != -1){
		*s++ = 0;
		*s++ = 0;
		*s++ = 0;
		*s++ = 0;
	}
	return s - buf;
}

void
sign(uchar *hash, char *priv, uchar *tar, uint *n)
{
	char buf[512];
	int rc;

again:
	sprint(buf, "proto=ecdsa role=client key=%s", priv);
	rc = auth_rpc(rpc, "start", buf, strlen(buf));
	if(rc == ARneedkey || rc == ARbadkey){
		rerrstr(buf, sizeof buf);
		if(auth_getkey(buf + 8) < 0)
			sysfatal("auth_getkey: %r");
		goto again;
	}
	if(rc != ARok)
		sysfatal("factotum start: %r");
	if(auth_rpc(rpc, "write", hash, 32) != ARok)
		sysfatal("factotum write: %r");
	if(auth_rpc(rpc, "read", "", 0) != ARok)
		sysfatal("factotum read: %r");
	memcpy(tar, rpc->arg, *n = rpc->narg);
}

void
main()
{
	char *line;
	int linenum;
	uint i, n;
	char *args[256];
	TxOut *to;
	TxIn *ti;
	Sig *si;
	uchar hash[32];
	uchar sig[100];

	afd = open("/mnt/factotum/rpc", ORDWR);
	if(afd < 0)
		sysfatal("open: %r");
	rpc = auth_allocrpc(afd);

	bp = malloc(sizeof(*bp));
	Binit(bp, 0, OREAD);
	linenum = 0;
	for(;;){
		line = Brdstr(bp, '\n', 1);
		linenum++;
		if(strcmp(line, "-") == 0)
			break;
		if(++nin >= 0xFD)
			sysfatal("too many inputs");
		ti = malloc(sizeof(*ti));
		in[nin-1] = ti;
		if(tokenize(line, args, nelem(args)) != 2)
			sysfatal("line %d: invalid data", linenum);
		hexdec(args[0], ti->prev, 32);
		i = atoi(args[1]);
		ti->prev[32] = i;
		ti->prev[33] = i >> 8;
		ti->prev[34] = i >> 16;
		ti->prev[35] = i >> 24;
		line = Brdstr(bp, '\n', 1);
		linenum++;
		i = tokenize(line, args, nelem(args));
		doscript(args, i, ti->scold, &ti->scoldlen, nil);
		line = Brdstr(bp, '\n', 1);
		linenum++;
		i = tokenize(line, args, nelem(args));
		doscript(args, i, ti->sc, &ti->sclen, ti);
	}
	for(;;){
		line = Brdstr(bp, '\n', 1);
		if(line == nil)
			break;
		linenum++;
		if(++nout >= 0xFD)
			sysfatal("too many outputs");
		to = malloc(sizeof(*to));
		out[nout-1] = to;
		to->val = atoll(line);
		line = Brdstr(bp, '\n', 1);
		linenum++;
		i = tokenize(line, args, nelem(args));
		doscript(args, i, to->sc, &to->sclen, nil);
	}
	for(i = 0; i < nin; i++){
		ti = in[i];
		if(ti->sig == nil)
			continue;
		n = serialize(buf, i);
		sha2_256(buf, n, hash, nil);
		sha2_256(hash, 32, hash, nil);
		for(si = ti->sig; si != nil; si = si->n){
			sign(hash, ti->sig->priv, sig + 1, &n);
			print("%d\n", n);
			sig[0] = n++;
			memmove(ti->sc + si->loc + n, ti->sc + si->loc, ti->sclen - si->loc);
			memmove(ti->sc + si->loc, sig, n);
			ti->sclen += n;
		}
	}
	n = serialize(buf, -1);
	for(i = 0; i < n; i++){
		print("%.2x", buf[i]);
		if((i%4)==3)
			print(" ");
		if((i%32)==31)
			print("\n");
	}
	if((i%16)!=0)
		print("\n");
}

Word words[] = {
	{"nop", 97},
	{"if", 99},
	{"notif", 100},
	{"else", 103},
	{"endif", 104},
	{"verify", 105},
	{"return", 106},
	{"toaltstack", 107},
	{"fromaltstack", 108},
	{"2drop", 109},
	{"2dup", 110},
	{"3dup", 111},
	{"2over", 112},
	{"2rot", 113},
	{"2swap", 114},
	{"ifdup", 115},
	{"depth", 116},
	{"drop", 117},
	{"dup", 118},
	{"nip", 119},
	{"over", 120},
	{"pick", 121},
	{"roll", 122},
	{"rot", 123},
	{"swap", 124},
	{"tuck", 125},
	{"cat", 126},
	{"substr", 127},
	{"left", 128},
	{"right", 129},
	{"size", 130},
	{"invert", 131},
	{"and", 132},
	{"or", 133},
	{"xor", 134},
	{"equal", 135},
	{"equalverify", 136},
	{"1add", 139},
	{"1sub", 140},
	{"2mul", 141},
	{"2div", 142},
	{"negate", 143},
	{"abs", 144},
	{"not", 145},
	{"0notequal", 146},
	{"add", 147},
	{"sub", 148},
	{"mul", 149},
	{"div", 150},
	{"mod", 151},
	{"lshift", 152},
	{"rshift", 153},
	{"booland", 154},
	{"boolor", 155},
	{"numequal", 156},
	{"numequalverify", 157},
	{"numnotequal", 158},
	{"lessthan", 159},
	{"greaterthan", 160},
	{"lessthanorequal", 161},
	{"greaterthanorequal", 162},
	{"min", 163},
	{"max", 164},
	{"within", 165},
	{"ripemd160", 166},
	{"sha1", 167},
	{"sha256", 168},
	{"hash160", 169},
	{"hash256", 170},
	{"codeseparator", 171},
	{"checksig", 172},
	{"checksigverify", 173},
	{"checkmultisig", 174},
	{"checkmultisigverify", 175},
	{nil, 0},
};
