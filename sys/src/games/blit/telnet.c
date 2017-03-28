#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

Channel *telnetrxch, *telnettxch;
extern Channel *uartrxch, *uarttxch;
int teldebug;

enum {
	SE = 240,
	NOP = 241,
	BRK = 243,
	IP = 244,
	AO = 245,
	AYT = 246,
	EC = 247,
	EL = 248,
	GA = 249,
	SB = 250,
	WILL = 251,
	WONT = 252,
	DO = 253,
	DONT = 254,
	IAC = 255,
	
	XMITBIN = 0,
	ECHO = 1,
	SUPRGA = 3,
	LINEEDIT = 34,
	
};

int telfd;

static void
netrproc(void *)
{
	static uchar buf[512];
	int n, c;
	uchar *p;
	
	for(;;){
		n = read(telfd, buf, sizeof(buf));
		if(n < 0) sysfatal("read: %r");
		for(p = buf; p < buf + n; p++){
			c = *p;
			send(telnetrxch, &c);
		}
	}
}

static void
netwproc(void *)
{
	static uchar buf[512];
	int c;
	uchar *p;
	
	for(;;){
		recv(telnettxch, &c);
		p = buf;
		do
			*p++ = c;
		while(nbrecv(telnettxch, &c) > 0);
		if(write(telfd, buf, p - buf) < p - buf)
			sysfatal("write: %r");
	}
}

static void
telnetrthread(void *)
{
	int c;

	for(;;){
		recv(telnetrxch, &c);
		if(c != IAC){
			send(uartrxch, &c);
			continue;
		}
		recv(telnetrxch, &c);
		switch(c){
		case NOP: break;
		case WILL:
			recv(telnetrxch, &c);
			if(teldebug) fprint(2, "WILL %d\n", c);
			break;
		case WONT:
			recv(telnetrxch, &c);
			if(teldebug) fprint(2, "WONT %d\n", c);
			break;
		case DO:
			recv(telnetrxch, &c);
			if(teldebug) fprint(2, "DO %d\n", c);
			break;
		case DONT:
			recv(telnetrxch, &c);
			if(teldebug) fprint(2, "DONT %d\n", c);
			break;
		case IAC:
			send(uartrxch, &c);
			break;
		default:	
			fprint(2, "unknown telnet command %d\n", c);
		}
	}
}

static void
cmd(int a, int b)
{
	send(telnettxch, &a);
	if(b >= 0) send(telnettxch, &b);
}

static void
telnetwthread(void *)
{
	int c;

	for(;;){
		recv(uarttxch, &c);
		send(telnettxch, &c);
		if(c == 0xff)
			send(telnettxch, &c);
	}
}

void
telnetinit(char *dialstr)
{
	telfd = dial(netmkaddr(dialstr, nil, "telnet"), nil, nil, nil);
	if(telfd < 0) sysfatal("dial: %r");
	telnetrxch = chancreate(sizeof(int), 128);
	telnettxch = chancreate(sizeof(int), 128);
	cmd(WILL, XMITBIN);
	cmd(DO, XMITBIN);
	cmd(DONT, ECHO);
	cmd(DO, SUPRGA);
	cmd(WILL, SUPRGA);
	cmd(WONT, LINEEDIT);
	cmd(DONT, LINEEDIT);
	proccreate(netrproc, nil, mainstacksize);
	proccreate(netwproc, nil, mainstacksize);
	threadcreate(telnetrthread, nil, mainstacksize);
	threadcreate(telnetwthread, nil, mainstacksize);
}
