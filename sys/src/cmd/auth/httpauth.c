#include <u.h>
#include <libc.h>
#include <authsrv.h>

int
httpauth(char *name, char *password)
{
	int afd;
	Ticketreq tr;
	Ticket	t;
	char key[DESKEYLEN];
	char buf[512];

	afd = authdial(nil, nil);
	if(afd < 0)
		return -1;

	/* send ticket request to AS */
	memset(&tr, 0, sizeof(tr));
	strcpy(tr.uid, name);
	tr.type = AuthHttp;
	convTR2M(&tr, buf);
	if(write(afd, buf, TICKREQLEN) != TICKREQLEN){
		close(afd);
		return -1;
	}
	if(_asrdresp(afd, buf, TICKETLEN) < 0){
		close(afd);
		return -1;
	}
	close(afd);

	/*
	 *  use password and try to decrypt the
	 *  ticket.  If it doesn't work we've got a bad password,
	 *  give up.
	 */
	passtokey(key, password);
	convM2T(buf, &t, key);
	if(t.num != AuthHr || strcmp(t.cuid, tr.uid))
		return -1;

	return 0;
}

void
usage(void)
{
	fprint(2, "Usage:\n\t%s user pass\n\t%s authorization\n", argv0, argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	char *a, *s;
	int n;

	ARGBEGIN {
	} ARGEND

	switch(argc){
	default:
		usage();
		return;
	case 2:
		s = argv[0];
		a = argv[1];
		break;
	case 1:
		a = argv[0];
		if(cistrncmp(a, "Basic ", 6) == 0)
			a += 6;
		n = strlen(a);
		if((s = malloc(n+1)) == nil)
			sysfatal("out of memory");
		if((n = dec64((uchar*)s, n, a, n)) <= 0)
			sysfatal("bad base64");
		s[n] = '\0';
		if((a = strchr(s, ':')) == nil)
			sysfatal("bad format");
		*a++ = '\0';
		break;
	}
	if(*s == '\0')
		sysfatal("empty username");
	if(httpauth(s, a))
		sysfatal("bad password");
	print("%s\n", s);
	exits(nil);
}
