#include <u.h>
#include <libc.h>
#include <bio.h>
#include <libsec.h>
#include <auth.h>
#include <authsrv.h>
#include <pool.h>

char *signhdr[] = {
	"from:",
	"to:",
	"subject:",
	"date:",
	"message-id:",
	nil
};

char *keyspec;
char *domain;
char *selector = "dkim";

int
trim(char *p)
{
	char *e;

	for(e = p; *e != 0; e++)
		if(*e == '\r' || *e == '\n')
			break;
	*e = 0;
	return e - p;
}		

int
usehdr(char *ln, char **hs)
{
	char **p;

	for(p = signhdr; *p; p++)
		if(cistrncmp(ln, *p, strlen(*p)) == 0){
			if((*hs = realloc(*hs, strlen(*hs) + strlen(*p) + 1)) == nil)
				sysfatal("realloc: %r");
			strcat(*hs, *p);
			return 1;
		}
	return 0;
}

void
append(char **m, int *nm, int *sz, char *ln, int n)
{
	while(*nm + n + 2 >= *sz){
		*sz += *sz/2;
		*m = realloc(*m, *sz);
	}
	memcpy(*m + *nm, ln, n);
	memcpy(*m + *nm + n, "\r\n", 2);
	*nm += n+2;
}


int
sign(uchar *hash, int nhash, char **sig, int *nsig)
{
	AuthRpc *rpc;
	int afd;

	if((afd = open("/mnt/factotum/rpc", ORDWR|OCEXEC)) < 0)
		return -1;
	if((rpc = auth_allocrpc(afd)) == nil){
		close(afd);
		return -1;
	}
	if(auth_rpc(rpc, "start", keyspec, strlen(keyspec)) != ARok){
		auth_freerpc(rpc);
		close(afd);
		return -1;
	}

	if(auth_rpc(rpc, "write", hash, nhash) != ARok)
		sysfatal("sign: write hash: %r");
	if(auth_rpc(rpc, "read", nil, 0) != ARok)
		sysfatal("sign: read sig: %r");
	if((*sig = malloc(rpc->narg)) == nil)
		sysfatal("malloc: %r");
	*nsig = rpc->narg;
	memcpy(*sig, rpc->arg, *nsig);
	auth_freerpc(rpc);
	close(afd);
	return 0;
}

void
usage(void)
{
	fprint(2, "usage: %s [-s sel] -d dom\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	int i, n, nhdr, nmsg, nsig, ntail, hdrsz, msgsz, use;
	uchar hdrhash[SHA2_256dlen], msghash[SHA2_256dlen];
	char *hdr, *msg, *sig, *ln, *hdrset, *dhdr;
	Biobuf *rd, *wr;
	DigestState *sh, *sb;

	ARGBEGIN{
	case 'd':
		domain = EARGF(usage());
		break;
	case 's':
		selector = EARGF(usage());
		break;
	default:
		usage();
		break;
	}ARGEND;

	if(domain == nil)
		usage();
	fmtinstall('H', encodefmt);
	fmtinstall('[', encodefmt);
	keyspec = smprint("proto=rsa service=dkim role=sign hash=sha256 domain=%s", domain);

	rd = Bfdopen(0, OREAD);
	wr = Bfdopen(1, OWRITE);

	nhdr = 0;
	hdrsz = 32;
	if((hdr = malloc(hdrsz)) == nil)
		sysfatal("malloc: %r");
	nmsg = 0;
	msgsz = 32;
	if((msg = malloc(msgsz)) == nil)
		sysfatal("malloc: %r");

	use = 0;
	sh = nil;
	hdrset = strdup("");
	while((ln = Brdstr(rd, '\n', 1)) != nil){
		n = trim(ln);
		if(n == 0
		|| (n == 1 && ln[0] == '\r' || ln[0] == '\n')
		|| (n == 2 && strcmp(ln, "\r\n") == 0))
			break;
		/*
		 * strip out existing DKIM signatures,
		 * for the sake of mailing lists and such.
		 */
		if(cistrcmp(ln, "DKIM-Signature:") == 0)
			continue;
		if(ln[0] != ' ' && ln[0] != '\t')
			use = usehdr(ln, &hdrset);
		if(use){
			sh = sha2_256((uchar*)ln, n, nil, sh);
			sh = sha2_256((uchar*)"\r\n", 2, nil, sh);
		}
		append(&hdr, &nhdr, &hdrsz, ln, n);
	}

	sb = nil;
	ntail = 0;
	while((ln = Brdstr(rd, '\n', 0)) != nil){
		n = trim(ln);
		if(n == 0){
			ntail++;
			continue;
		}
		for(i = 0; i < ntail; i++){
			sb = sha2_256((uchar*)"\r\n", 2, nil, sb);
			append(&msg, &nmsg, &msgsz, "", 0);
			ntail = 0;
		}
		sb = sha2_256((uchar*)ln, n, nil, sb);
		sb = sha2_256((uchar*)"\r\n", 2, nil, sb);
		append(&msg, &nmsg, &msgsz, ln, n);
	}
	if(nmsg == 0 || ntail > 1)
		sb = sha2_256((uchar*)"\r\n", 2, nil, sb);
	Bterm(rd);

	sha2_256(nil, 0, msghash, sb);
	dhdr = smprint(
		"DKIM-Signature: v=1; a=rsa-sha256; c=simple/simple; d=%s;\r\n"
		" h=%s; s=%s;\r\n"
		" bh=%.*[; \r\n"
		" b=",
		domain, hdrset, selector,
		(int)sizeof(msghash), msghash);
	if(dhdr == nil)
		sysfatal("smprint: %r");
	sh = sha2_256((uchar*)dhdr, strlen(dhdr), nil, sh);
	sha2_256(nil, 0, hdrhash, sh);
	if(sign(hdrhash, sizeof(hdrhash), &sig, &nsig) == -1)
		sysfatal("sign: %r");

	Bwrite(wr, dhdr, strlen(dhdr));
	Bprint(wr, "%.*[\r\n", nsig, sig);
	Bwrite(wr, hdr, nhdr);
	Bprint(wr, "\n");
	Bwrite(wr, msg, nmsg);
	Bterm(wr);

	free(hdr);
	free(msg);
	free(sig);
	exits(nil);	
}
