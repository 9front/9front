/*
 * Beware the LM hash is easy to crack (google for l0phtCrack)
 * and though NTLM is more secure it is still breakable.
 * Ntlmv2 is better and seen as good enough by the windows community.
 * For real security use kerberos.
 */
#include <u.h>
#include <libc.h>
#include <mp.h>
#include <auth.h>
#include <libsec.h>
#include <ctype.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include "cifs.h"

#define DEF_AUTH 	"ntlmv2"

static enum {
	MACkeylen	= 40,	/* MAC key len */
	MAClen		= 8,	/* signature length */
	MACoff		= 14,	/* sign. offset from start of SMB (not netbios) pkt */
};

#ifdef DEBUG_MAC
static void
dmp(char *s, int seq, void *buf, int n)
{
	int i;
	char *p = buf;

	print("%s %3d      ", s, seq);
	while(n > 0){
		for(i = 0; i < 16 && n > 0; i++, n--)
			print("%02x ", *p++ & 0xff);
		if(n > 0)
			print("\n");
	}
	print("\n");
}
#endif

static Auth *
auth_plain(char *windom, char *keyp, uchar *chal, int len)
{
	UserPasswd *up;
	static Auth *ap;

	USED(chal, len);

	up = auth_getuserpasswd(auth_getkey, "windom=%s proto=pass service=cifs %s",
		windom, keyp);
	if(! up)
		sysfatal("cannot get key - %r");

	ap = emalloc9p(sizeof(Auth));
	memset(ap, 0, sizeof(ap));
	ap->user = estrdup9p(up->user);
	ap->windom = estrdup9p(windom);

	ap->resp[0] = estrdup9p(up->passwd);
	ap->len[0] = strlen(up->passwd);
	memset(up->passwd, 0, strlen(up->passwd));
	free(up);

	return ap;
}

static Auth *
auth_proto(char *proto, char *windom, char *keyp, uchar *chal, int len)
{
	MSchapreply *mcr;
	uchar resp[4096];
	int nresp;
	char user[64];
	Auth *ap;

	mcr = (MSchapreply*)resp;
	nresp = sizeof(resp);
	nresp = auth_respond(chal, len, user, sizeof user, resp, nresp,
		auth_getkey, "proto=%s role=client service=cifs windom=%s %s",
		proto, windom, keyp);
	if(nresp < 0)
		sysfatal("cannot get response - %r");
	if(nresp < sizeof(*mcr))
		sysfatal("bad response size");

	ap = emalloc9p(sizeof(Auth));
	memset(ap, 0, sizeof(ap));
	ap->user = estrdup9p(user);
	ap->windom = estrdup9p(windom);

	/* LM response */
	ap->len[0] = sizeof(mcr->LMresp);
	ap->resp[0] = emalloc9p(ap->len[0]);
	memcpy(ap->resp[0], mcr->LMresp, ap->len[0]);

	/* NT response */
	ap->len[1] = nresp+sizeof(mcr->NTresp)-sizeof(*mcr);
	ap->resp[1] = emalloc9p(ap->len[1]);
	memcpy(ap->resp[1], mcr->NTresp, ap->len[1]);

	return ap;
}

static Auth *
auth_lm_and_ntlm(char *windom, char *keyp, uchar *chal, int len)
{
	return auth_proto("ntlm", windom, keyp, chal, len);
}

/*
 * NTLM response only, the LM response is a just
 * copy of the NTLM one. we do this because the lm
 * response is easily reversed - Google for l0pht
 * for more info.
 */
static Auth *
auth_ntlm(char *windom, char *keyp, uchar *chal, int len)
{
	Auth *ap;

	if((ap = auth_lm_and_ntlm(windom, keyp, chal, len)) == nil)
		return nil;

	free(ap->resp[0]);
	ap->len[0] = ap->len[1];
	ap->resp[0] = emalloc9p(ap->len[0]);
	memcpy(ap->resp[0], ap->resp[1], ap->len[0]);
	return ap;
}

static Auth *
auth_ntlmv2(char *windom, char *keyp, uchar *chal, int len)
{
	return auth_proto("ntlmv2", windom, keyp, chal, len);
}

struct {
	char	*name;
	Auth	*(*func)(char *, char *, uchar *, int);
} methods[] = {
	{ "plain",	auth_plain },
	{ "lm+ntlm",	auth_lm_and_ntlm },
	{ "ntlm",	auth_ntlm },
	{ "ntlmv2",	auth_ntlmv2 },
//	{ "kerberos",	auth_kerberos },
};

void
autherr(void)
{
	int i;

	fprint(2, "supported auth methods:\t");
	for(i = 0; i < nelem(methods); i++)
		fprint(2, "%s ", methods[i].name);
	fprint(2, "\n");
	exits("usage");
}

Auth *
getauth(char *name, char *windom, char *keyp, int secmode, uchar *chal, int len)
{
	int i;
	Auth *ap;

	if(name == nil){
		name = DEF_AUTH;
		if((secmode & SECMODE_PW_ENCRYPT) == 0)
			sysfatal("plaintext authentication required, use '-a plain'");
	}

	ap = nil;
	for(i = 0; i < nelem(methods); i++)
		if(strcmp(methods[i].name, name) == 0){
			ap = methods[i].func(windom, keyp, chal, len);
			break;
		}

	if(! ap){
		fprint(2, "%s: %s - unknown auth method\n", argv0, name);
		autherr();	/* never returns */
	}
	return ap;
}

static int
genmac(uchar *buf, int len, int seq, uchar key[MACkeylen], uchar ours[MAClen])
{
	DigestState *ds;
	uchar *sig, digest[MD5dlen], theirs[MAClen];

	sig = buf+MACoff;
	memcpy(theirs, sig, MAClen);

	memset(sig, 0, MAClen);
	sig[0] = seq;
	sig[1] = seq >> 8;
	sig[2] = seq >> 16;
	sig[3] = seq >> 24;

	ds = md5(key, MACkeylen, nil, nil);
	md5(buf, len, digest, ds);
	memcpy(ours, digest, MAClen);

	return memcmp(theirs, ours, MAClen);
}

int
macsign(Pkt *p, int seq)
{
	int rc, len;
	uchar *sig, *buf, mac[MAClen];

	sig = p->buf + NBHDRLEN + MACoff;
	buf = p->buf + NBHDRLEN;
	len = (p->pos - p->buf) - NBHDRLEN;

#ifdef DEBUG_MAC
	if(seq & 1)
		dmp("rx", seq, sig, MAClen);
#endif
	rc = 0;
	if(! p->s->seqrun)
		memcpy(mac, "BSRSPYL ", 8);	/* no idea, ask MS */
	else
		rc = genmac(buf, len, seq, p->s->auth->mackey[0], mac);
#ifdef DEBUG_MAC
	if(!(seq & 1))
		dmp("tx", seq, mac, MAClen);
#endif
	memcpy(sig, mac, MAClen);
	return rc;
}
