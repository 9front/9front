#include "vnc.h"
#include <libsec.h>
#include <auth.h>

int
vncsrvhandshake(Vnc *v)
{
	char msg[VerLen+1];

	vncwrbytes(v, "RFB 003.003\n", VerLen);
	vncflush(v);

	vncrdbytes(v, msg, VerLen);
	if(verbose)
		fprint(2, "client version: %s\n", msg);
	return 0;
}

int
vnchandshake(Vnc *v)
{
	char msg[VerLen + 1];

	msg[VerLen] = 0;
	vncrdbytes(v, msg, VerLen);

	if(verbose)
		fprint(2, "server version: %s\n", msg);

	if(strncmp(msg, "RFB 003.003\n", VerLen) == 0)
		v->vers = 33;
	else if(strncmp(msg, "RFB 003.007\n", VerLen) == 0)
		v->vers = 37;
	else if(strncmp(msg, "RFB 003.008\n", VerLen) == 0)
		v->vers = 38;
	else if(strncmp(msg, "RFB 003.889\n", VerLen) == 0)
		v->vers = 38;  /* Darwin */
	else if(strncmp(msg, "RFB 004.000\n", VerLen) == 0)
		v->vers = 38;
	else /* RFC6143: Any other should be treated as 3.3. */
		v->vers = 33;

	strcpy(msg, "RFB 003.008\n");
	vncwrbytes(v, msg, VerLen);
	vncflush(v);
	return 0;
}

int
vncauth(Vnc *v, char *keypattern)
{
	uchar chal[VncChalLen];
	ulong auth, type;
	int i, ntypes;
	char *err;

	if(keypattern == nil)
		keypattern = "";

	auth = AFailed;
	if(v->vers == 33)
		auth = vncrdlong(v);
	else{
		ntypes = vncrdchar(v);
		for(i = 0; i < ntypes; i++){
			type = vncrdchar(v);
			if(verbose)
				fprint(2, "auth type %uld\n", type);
			if(type > auth && type <= AVncAuth)
				auth = type;
		}
		if(auth == AFailed){
			werrstr("no supported auth types");
			return -1;
		}
	}

	switch(auth){
	default:
		werrstr("unknown auth type 0x%lux", auth);
		if(verbose)
			fprint(2, "unknown auth type 0x%lux\n", auth);
		return -1;

	case AFailed:
		err = vncrdstring(v);
		werrstr("%s", err);
		if(verbose)
			fprint(2, "auth failed: %s\n", err);
		return -1;

	case ANoAuth:
		if(v->vers == 38){
			vncwrchar(v, auth);
			vncflush(v);
		}
		if(verbose)
			fprint(2, "no auth needed\n");
		break;

	case AVncAuth:
		if(v->vers == 38){
			vncwrchar(v, auth);
			vncflush(v);
		}

		vncrdbytes(v, chal, VncChalLen);
		if(auth_respond(chal, VncChalLen, nil, 0, chal, VncChalLen, auth_getkey,
			"proto=vnc role=client server=%s %s", v->srvaddr, keypattern) != VncChalLen){
			return -1;
		}
		vncwrbytes(v, chal, VncChalLen);
		vncflush(v);
		break;
	}

	/* in version 3.8 the auth status is always sent, in 3.3 and 3.7, only in AVncAuth */
	if(v->vers == 38 || auth == AVncAuth){
		auth = vncrdlong(v); /* auth status */
		switch(auth){
		default:
			werrstr("unknown server response 0x%lux", auth);
			return -1;
		case VncAuthFailed:
			err = (v->vers == 38) ? vncrdstring(v) : "rejected";
			werrstr("%s", err);
			if(verbose)
				fprint(2, "auth failed: %s\n", err);
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

