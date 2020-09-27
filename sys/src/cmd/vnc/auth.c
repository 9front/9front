#include "vnc.h"
#include <libsec.h>
#include <auth.h>

char *serveraddr;

enum
{
	VerLen	= 12
};

static char version33[VerLen+1] = "RFB 003.003\n";
static char version38[VerLen+1] = "RFB 003.008\n";
static int srvversion;

int
vncsrvhandshake(Vnc *v)
{
	char msg[VerLen+1];

	strecpy(msg, msg+sizeof msg, version33);
	if(verbose)
		fprint(2, "server version: %s\n", msg);
	vncwrbytes(v, msg, VerLen);
	vncflush(v);

	vncrdbytes(v, msg, VerLen);
	if(verbose)
		fprint(2, "client version: %s\n", msg);
	return 0;
}

int
vnchandshake(Vnc *v)
{
	char msg[VerLen+1];

	msg[VerLen] = 0;
	vncrdbytes(v, msg, VerLen);
	if(strncmp(msg, "RFB 003.", 8) != 0 ||
	   strncmp(msg, "RFB 003.007\n", VerLen) == 0){
		werrstr("bad rfb version \"%s\"", msg);
		return -1;
	}
	if(strncmp(msg, "RFB 003.008\n", VerLen) == 0)
		srvversion = 38;
	else
		srvversion = 33;

	if(verbose)
		fprint(2, "server version: %s\n", msg);
	strcpy(msg, version38);
	vncwrbytes(v, msg, VerLen);
	vncflush(v);
	return 0;
}

ulong
sectype38(Vnc *v)
{
	ulong auth, type;
	int i, ntypes;

	ntypes = vncrdchar(v);
	if(ntypes == 0){
		werrstr("no security types from server");
		return AFailed;
	}

	/* choose the "most secure" security type */
	auth = AFailed;
	for(i = 0; i < ntypes; i++){
		type = vncrdchar(v);
		if(verbose){
			fprint(2, "auth type %s\n",
				type == AFailed ? "Invalid" :
				type == ANoAuth ? "None" :
				type == AVncAuth ? "VNC" : "Unknown");
		}
		if(type > auth)
			auth = type;
	}
	return auth;
}

int
vncauth(Vnc *v, char *keypattern)
{
	char *reason;
	uchar chal[VncChalLen];
	ulong auth;

	if(keypattern == nil)
		keypattern = "";

	auth = srvversion == 38 ? sectype38(v) : vncrdlong(v);

	switch(auth){
	default:
		werrstr("unknown auth type 0x%lux", auth);
		if(verbose)
			fprint(2, "unknown auth type 0x%lux\n", auth);
		return -1;

	case AFailed:
	failed:
		reason = vncrdstring(v);
		werrstr("%s", reason);
		if(verbose)
			fprint(2, "auth failed: %s\n", reason);
		return -1;

	case ANoAuth:
		if(srvversion == 38){
			vncwrchar(v, auth);
			vncflush(v);
		}
		if(verbose)
			fprint(2, "no auth needed\n");
		break;

	case AVncAuth:
		if(srvversion == 38){
			vncwrchar(v, auth);
			vncflush(v);
		}

		vncrdbytes(v, chal, VncChalLen);
		if(auth_respond(chal, VncChalLen, nil, 0, chal, VncChalLen, auth_getkey,
			"proto=vnc role=client server=%s %s", serveraddr, keypattern) != VncChalLen){
			return -1;
		}
		vncwrbytes(v, chal, VncChalLen);
		vncflush(v);
		break;
	}

	/* in version 3.8 the auth status is always sent, in 3.3 only in AVncAuth */
	if(srvversion == 38 || auth == AVncAuth){
		auth = vncrdlong(v); /* auth status */
		switch(auth){
		default:
			werrstr("unknown server response 0x%lux", auth);
			return -1;
		case VncAuthFailed:
			if (srvversion == 38)
				goto failed;

			werrstr("server says authentication failed");
			return -1;
		case VncAuthTooMany:
			werrstr("server says too many tries");
			return -1;
		case VncAuthOK:
			break;
		}
	}
	return 0;
}

int
vncsrvauth(Vnc *v)
{
	Chalstate *c;
	AuthInfo *ai;

	if((c = auth_challenge("proto=vnc role=server user=%q", getuser()))==nil)
		sysfatal("vncchal: %r");
	if(c->nchal != VncChalLen)
		sysfatal("vncchal got %d bytes wanted %d", c->nchal, VncChalLen);
	vncwrlong(v, AVncAuth);
	vncwrbytes(v, c->chal, VncChalLen);
	vncflush(v);

	vncrdbytes(v, c->chal, VncChalLen);
	c->resp = c->chal;
	c->nresp = VncChalLen;
	ai = auth_response(c);
	auth_freechal(c);
	if(ai == nil){
		fprint(2, "vnc auth failed: server factotum: %r\n");
		vncwrlong(v, VncAuthFailed);
		vncflush(v);
		return -1;
	}
	auth_freeAI(ai);
	vncwrlong(v, VncAuthOK);
	vncflush(v);

	return 0;
}

