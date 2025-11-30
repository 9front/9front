#include <u.h>
#include <libc.h>
#include <auth.h>

int noprompt;

void
usage(void)
{
	fprint(2, "usage: auth/userpasswd [-n] fmt\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	UserPasswd *up;

	ARGBEGIN{
	case 'n':
		noprompt++;
		break;
	default:
		usage();
	}ARGEND

	if(argc != 1)
		usage();

	quotefmtinstall();
	up = auth_getuserpasswd(noprompt?nil:auth_getkey, "proto=pass %s", argv[0]);
	if(up == nil)
		sysfatal("getuserpasswd: %r");

	print("%s\n%s\n", up->user, up->passwd);
	exits(nil);
}
