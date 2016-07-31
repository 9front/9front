/*
 * Test various aspects of the authentication setup.
 */

#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ndb.h>
#include <auth.h>
#include <authsrv.h>

/* private copy with added debugging */
int
authdial(char *netroot, char *dom)
{
	char *p;
	int rv;
	
	if(dom != nil){
		/* look up an auth server in an authentication domain */
		p = csgetvalue(netroot, "authdom", dom, "auth", nil);

		/* if that didn't work, just try the IP domain */
		if(p == nil)
			p = csgetvalue(netroot, "dom", dom, "auth", nil);
		if(p == nil){
			werrstr("no auth server found for %s", dom);
			return -1;
		}
		print("\tdialing auth server %s\n",
			netmkaddr(p, netroot, "ticket"));
		rv = dial(netmkaddr(p, netroot, "ticket"), 0, 0, 0);
		free(p);
		return rv;
	} else
		/* look for one relative to my machine */
		return dial(netmkaddr("$auth", netroot, "ticket"), 0, 0, 0);
}

void
usage(void)
{
	fprint(2, "usage: auth/debug\n");
	exits("usage");
}

void authdialfutz(char*, char*, char*);
void authfutz(char*, char*, char*);

/* scan factotum for p9sk1 keys; check them */
void
debugfactotumkeys(void)
{
	char *s, *dom, *proto, *user;
	int found;
	Attr *a;
	Biobuf *b;

	b = Bopen("/mnt/factotum/ctl", OREAD);
	if(b == nil){
		fprint(2, "debug: cannot open /mnt/factotum/ctl\n");
		return;
	}
	found = 0;
	while((s = Brdstr(b, '\n', 1)) != nil){
		if(strncmp(s, "key ", 4) != 0){
			print("malformed ctl line: %s\n", s);
			free(s);
			continue;
		}
		a = _parseattr(s+4);
		free(s);
		proto = _strfindattr(a, "proto");
		if(proto==nil || (strcmp(proto, "p9sk1")!=0 && strcmp(proto, "dp9ik")!=0))
			continue;
		dom = _strfindattr(a, "dom");
		if(dom == nil){
			print("p9sk1 key with no dom: %A\n", a);
			_freeattr(a);
			continue;
		}
		user = _strfindattr(a, "user");
		if(user == nil){
			print("p9sk1 key with no user: %A\n", a);
			_freeattr(a);
			continue;
		}
		print("key: %A\n", a);
		found = 1;
		authdialfutz(dom, user, proto);
		_freeattr(a);
	}
	if(!found)
		print("no p9sk1/dp9ik keys found in factotum\n");
}

void
authdialfutz(char *dom, char *user, char *proto)
{
	int fd;
	char *server;
	char *addr;

	fd = authdial(nil, dom);
	if(fd >= 0){
		print("\tsuccessfully dialed auth server\n");
		close(fd);
		authfutz(dom, user, proto);
		return;
	}
	print("\tcannot dial auth server: %r\n");
	server = csgetvalue(nil, "authdom", dom, "auth", nil);
	if(server){
		print("\tcsquery authdom=%q auth=%s\n", dom, server);
		free(server);
		return;
	}
	print("\tcsquery authdom=%q auth=* failed\n", dom);
	server = csgetvalue(nil, "dom", dom, "auth", nil);
	if(server){
		print("\tcsquery dom=%q auth=%q\n", dom, server);
		free(server);
		return;
	}
	print("\tcsquery dom=%q auth=*\n", dom);

	fd = dial(addr=netmkaddr("$auth", nil, "ticket"), 0, 0, 0);
	if(fd >= 0){
		print("\tdial %s succeeded\n", addr);
		close(fd);
		return;
	}
	print("\tdial %s failed: %r\n", addr);
}

int
getpakkeys(int fd, Ticketreq *tr, Authkey *akey, Authkey *hkey)
{
	uchar y[PAKYLEN];
	PAKpriv p;
	int ret, type;

	ret = -1;
	type = tr->type;
	tr->type = AuthPAK;
	if(_asrequest(fd, tr) < 0 || _asrdresp(fd, (char*)y, 0) < 0)
		goto out;

	authpak_hash(akey, tr->authid);
	authpak_new(&p, akey, y, 1);
	if(write(fd, y, PAKYLEN) != PAKYLEN
	|| readn(fd, y, PAKYLEN) != PAKYLEN
	|| authpak_finish(&p, akey, y))
		goto out;

	authpak_hash(hkey, tr->hostid);
	authpak_new(&p, hkey, y, 1);
	if(write(fd, y, PAKYLEN) != PAKYLEN
	|| readn(fd, y, PAKYLEN) != PAKYLEN
	|| authpak_finish(&p, hkey, y))
		goto out;

	ret = 0;
out:
	tr->type = type;
	return ret;
}

void
authfutz(char *dom, char *user, char *proto)
{
	int fd, n, m;
	char prompt[128], tbuf[2*MAXTICKETLEN], *pass;
	Authkey key, booteskey;
	Ticket t;
	Ticketreq tr;

	snprint(prompt, sizeof prompt, "\tpassword for %s@%s [hit enter to skip test]", user, dom);
	pass = readcons(prompt, nil, 1);
	if(pass == nil || *pass == 0){
		free(pass);
		return;
	}
	passtokey(&key, pass);
	booteskey = key;
	memset(pass, 0, strlen(pass));
	free(pass);

	fd = authdial(nil, dom);
	if(fd < 0){
		print("\tauthdial failed(!): %r\n");
		return;
	}

	/* try ticket request using just user key */
	memset(&tr, 0, sizeof(tr));
	tr.type = AuthTreq;
	strecpy(tr.authid, tr.authid+sizeof tr.authid, user);
	strecpy(tr.authdom, tr.authdom+sizeof tr.authdom, dom);
	strecpy(tr.hostid, tr.hostid+sizeof tr.hostid, user);
	strecpy(tr.uid, tr.uid+sizeof tr.uid, user);
	memset(tr.chal, 0xAA, sizeof tr.chal);

	if(strcmp(proto, "dp9ik") == 0 && getpakkeys(fd, &tr, &booteskey, &key) < 0){
		print("\tgetpakkeys failed: %r\n");
		close(fd);
		return;
	}

	if((n = _asgetticket(fd, &tr, tbuf, sizeof(tbuf))) < 0){
		print("\t_asgetticket failed: %r\n");
		close(fd);
		return;
	}
	m = convM2T(tbuf, n, &t, &key);
	if(t.num != AuthTc){
		print("\tcannot decrypt ticket1 from auth server (bad t.num=0x%.2ux)\n", t.num);
		print("\tauth server and you do not agree on key for %s@%s\n", user, dom);
		return;
	}
	if(memcmp(t.chal, tr.chal, sizeof tr.chal) != 0){
		print("\tbad challenge1 from auth server got %.*H wanted %.*H\n",
			sizeof t.chal, t.chal, sizeof tr.chal, tr.chal);
		print("\tauth server is rogue\n");
		return;
	}

	convM2T(tbuf+m, n-m, &t, &booteskey);
	if(t.num != AuthTs){
		print("\tcannot decrypt ticket2 from auth server (bad t.num=0x%.2ux)\n", t.num);
		print("\tauth server and you do not agree on key for %s@%s\n", user, dom);
		return;
	}
	if(memcmp(t.chal, tr.chal, sizeof tr.chal) != 0){
		print("\tbad challenge2 from auth server got %.*H wanted %.*H\n",
			sizeof t.chal, t.chal, sizeof tr.chal, tr.chal);
		print("\tauth server is rogue\n");
		return;
	}
	print("\tticket request using %s@%s key succeeded\n", user, dom);

	/* try ticket request using bootes key */
	snprint(prompt, sizeof prompt, "\tcpu server owner for domain %s ", dom);
	user = readcons(prompt, "glenda", 0);
	if(user == nil || *user == '\0'){
		free(user);
		goto Nobootes;
	}
	strecpy(tr.authid, tr.authid+sizeof tr.authid, user);
	free(user);
	snprint(prompt, sizeof prompt, "\tpassword for %s@%s [hit enter to skip test]", tr.authid, dom);
	pass = readcons(prompt, nil, 1);
	if(pass == nil || *pass == '\0'){
		free(pass);
		goto Nobootes;
	}
	passtokey(&booteskey, pass);
	memset(pass, 0, strlen(pass));
	free(pass);

	if(strcmp(proto, "dp9ik") == 0 && getpakkeys(fd, &tr, &booteskey, &key) < 0){
		print("\tgetpakkeys failed: %r\n");
		close(fd);
		return;
	}

	if((n = _asgetticket(fd, &tr, tbuf, sizeof(tbuf))) < 0){
		print("\t_asgetticket failed: %r\n");
		close(fd);
		return;
	}
	m = convM2T(tbuf, n, &t, &key);
	if(t.num != AuthTc){
		print("\tcannot decrypt ticket1 from auth server (bad t.num=0x%.2ux)\n", t.num);
		print("\tauth server and you do not agree on key for %s@%s\n", tr.hostid, dom);
		return;
	}
	if(memcmp(t.chal, tr.chal, sizeof tr.chal) != 0){
		print("\tbad challenge1 from auth server got %.*H wanted %.*H\n",
			sizeof t.chal, t.chal, sizeof tr.chal, tr.chal);
		print("\tauth server is rogue\n");
		return;
	}
	
	convM2T(tbuf+m, n-m, &t, &booteskey);
	if(t.num != AuthTs){
		print("\tcannot decrypt ticket2 from auth server (bad t.num=0x%.2ux)\n", t.num);
		print("\tauth server and you do not agree on key for %s@%s\n", tr.authid, dom);
		return;
	}
	if(memcmp(t.chal, tr.chal, sizeof tr.chal) != 0){
		print("\tbad challenge2 from auth server got %.*H wanted %.*H\n",
			sizeof t.chal, t.chal, sizeof tr.chal, tr.chal);
		print("\tauth server is rogue\n");
		return;
	}
	print("\tticket request using %s@%s key succeeded\n", tr.authid, dom);

Nobootes:;
	/* try p9sk1 exchange with local factotum to test that key is right */


	/*
	 * try p9sk1 exchange with factotum on
	 * auth server (assumes running cpu service)
	 * to test that bootes key is right over there
	 */
}

void
main(int argc, char **argv)
{
	quotefmtinstall();
	fmtinstall('A', _attrfmt);
	fmtinstall('H', encodefmt);

	ARGBEGIN{
	default:
		usage();
	}ARGEND

	if(argc != 0)
		usage();

	debugfactotumkeys();
}
