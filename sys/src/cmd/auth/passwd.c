#include <u.h>
#include <libc.h>
#include <bio.h>
#include <authsrv.h>
#include "authcmdlib.h"

void
main(int argc, char **argv)
{
	int fd, n, try;
	Ticketreq tr;
	Ticket t;
	Passwordreq pr;
	Authkey key;
	char buf[512];
	char *s, *user;

	ARGBEGIN{
	}ARGEND

	argv0 = "passwd";
	user = getuser();
	private();

	s = nil;
	if(argc > 0){
		user = argv[0];
		s = strchr(user, '@');
		if(s != nil)
			*s++ = 0;
		if(*user == 0)
			user = getuser();
	}

	fd = authdial(nil, s);
	if(fd < 0)
		error("authdial: %r");

	memset(&tr, 0, sizeof(tr));
	strncpy(tr.uid, user, sizeof(tr.uid)-1);
	tr.type = AuthPass;

	/*
	 *  get a password from the user and try to decrypt the
	 *  ticket.  If it doesn't work we've got a bad password,
	 *  give up.
	 */
	memset(&pr, 0, sizeof(pr));
	getpass(&key, pr.old, 0, 0);

	/*
	 *  negotiate PAK key. we need to retry in case the AS does
	 *  not support the AuthPAK request or when the user has
	 *  not yet setup a new key and the AS made one up.
	 */
	try = 0;
	authpak_hash(&key, tr.uid);
	if(_asgetpakkey(fd, &tr, &key) < 0){
Retry:
		try++;
		close(fd);
		fd = authdial(nil, s);
		if(fd < 0)
			error("authdial: %r");
	}
	/* send ticket request to AS */
	if(_asrequest(fd, &tr) < 0)
		error("%r");
	if(_asgetresp(fd, &t, nil, &key) < 0)
		error("%r");
	if(t.num != AuthTp || strcmp(t.cuid, tr.uid) != 0){
		if(try == 0)
			goto Retry;
		error("bad password");
	}

	/* loop trying new passwords */
	for(;;){
		memset(pr.new, 0, sizeof(pr.new));
		if(answer("change Plan 9 Password?"))
			getpass(nil, pr.new, 0, 1);
		pr.changesecret = answer("change Inferno/POP secret?");
		if(pr.changesecret){
			if(answer("make it the same as your plan 9 password?")){
				if(*pr.new)
					strcpy(pr.secret, pr.new);
				else
					strcpy(pr.secret, pr.old);
			} else {
				getpass(nil, pr.secret, 0, 1);
			}
		}
		pr.num = AuthPass;
		n = convPR2M(&pr, buf, sizeof(buf), &t);
		if(write(fd, buf, n) != n)
			error("AS protocol botch: %r");
		if(_asrdresp(fd, buf, 0) == 0)
			break;
		fprint(2, "passwd: refused: %r\n");
	}
	close(fd);

	exits(0);
}
