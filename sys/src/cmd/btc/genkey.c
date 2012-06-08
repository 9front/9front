#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>

ECdomain dom;

void readln(char *prompt, char *line, int len, int raw);

void
address(ECpub *p, char *buf)
{
	uchar buf1[65], buf2[25], buf3[25];
	
	buf1[0] = 4;
	buf3[0] = 0;
	mptobe(p->x, buf1 + 1, 32, nil);
	mptobe(p->y, buf1 + 33, 32, nil);
	sha2_256(buf1, 65, buf2, nil);
	ripemd160(buf2, 32, buf3 + 1, nil);
	sha2_256(buf3, 21, buf2, nil);
	sha2_256(buf2, 32, buf2, nil);
	memcpy(buf3 + 21, buf2, 4);
	memset(buf, 0, 100);
	base58enc(buf3, buf, 25);
}

void
pubkey(ECpub *b, char *buf)
{
	uchar buf1[65];
	
	buf1[0] = 4;
	mptobe(b->x, buf1 + 1, 32, nil);
	mptobe(b->y, buf1 + 33, 32, nil);
	memset(buf, 0, 100);
	base58enc(buf1, buf, 65);
}

void
privkey(ECpriv *p, char *buf, char *pw)
{
	uchar buf1[53], buf2[32];
	AESstate st;
	
	buf1[0] = 0x80;
	mptobe(p->d, buf1 + 1, 32, nil);
	sha2_256(buf1, 33, buf2, nil);
	sha2_256(buf2, 32, buf2, nil);
	memcpy(buf1 + 33, buf2, 4);
	sha2_256((uchar *) pw, strlen(pw), buf2, nil);
	sha2_256(buf2, 32, buf2, nil);
	genrandom(buf1 + 37, 16);
	setupAESstate(&st, buf2, 32, buf1+37);
	aesCBCencrypt(buf1, 37, &st);
	memset(buf, 0, 100);
	base58enc(buf1, buf, 53);
}

void
main()
{
	ECpriv *p;
	char addr[100], pub[100], priv[100], pw[256], pw2[256];

	dom.p = strtomp("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F", nil, 16, nil);
	dom.a = uitomp(0, nil);
	dom.b = uitomp(7, nil);
	dom.n = strtomp("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141", nil, 16, nil);
	dom.h = uitomp(1, nil);
	dom.G = strtoec(&dom, "0279BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798", nil, nil);
	p = ecgen(&dom, nil);
	readln("password: ", pw, sizeof pw, 1);
	readln("repeat: ", pw2, sizeof pw2, 1);
	if(strcmp(pw, pw2) != 0)
		sysfatal("passwords don't match");
	address(p, addr);
	pubkey(p, pub);
	privkey(p, priv, pw);
	print("%s %s %s\n", addr, pub, priv);
}

void
readln(char *prompt, char *line, int len, int raw)
{
	char *p;
	int fdin, fdout, ctl, n, nr;

	fdin = open("/dev/cons", OREAD);
	fdout = open("/dev/cons", OWRITE);
	fprint(fdout, "%s", prompt);
	if(raw){
		ctl = open("/dev/consctl", OWRITE);
		if(ctl < 0)
			sysfatal("couldn't set raw mode");
		write(ctl, "rawon", 5);
	} else
		ctl = -1;
	nr = 0;
	p = line;
	for(;;){
		n = read(fdin, p, 1);
		if(n < 0){
			close(ctl);
			sysfatal("can't read cons\n");
		}
		if(*p == 0x7f)
			exits(0);
		if(n == 0 || *p == '\n' || *p == '\r'){
			*p = '\0';
			if(raw){
				write(ctl, "rawoff", 6);
				write(fdout, "\n", 1);
			}
			close(ctl);
			return;
		}
		if(*p == '\b'){
			if(nr > 0){
				nr--;
				p--;
			}
		}else{
			nr++;
			p++;
		}
		if(nr == len){
			fprint(fdout, "line too long; try again\n");
			nr = 0;
			p = line;
		}
	}
}
