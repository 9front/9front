/*
 * as user cmd [arg...] - run cmd with args as user on this cpu server.
 *	must be hostowner for this to work.
 *	needs #¤/caphash and #¤/capuse.
 */
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <libsec.h>
#include <auth.h>
#include <authsrv.h>
#include "authcmdlib.h"

extern int newnsdebug;

char	*defargv[] = { "/bin/rc", "-i", nil };
char	*namespace = nil;

int	becomeuser(char*);

void
usage(void)
{
	fprint(2, "usage: %s [-d] [-n namespace] user [cmd [args...]]\n", argv0);
	exits("usage");
}

void
run(char **a)
{
	exec(a[0], a);

	if(a[0][0] != '/' && a[0][0] != '#' &&
	  (a[0][0] != '.' || (a[0][1] != '/' &&
		             (a[0][1] != '.' ||  a[0][2] != '/'))))
		exec(smprint("/bin/%s", a[0]), a);

	sysfatal("exec: %s: %r", a[0]);
}

void
main(int argc, char *argv[])
{
	ARGBEGIN{
	case 'd':
		newnsdebug = 1;
		break;
	case 'n':
		namespace = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	if(argc == 0)
		usage();

	if(becomeuser(argv[0]) < 0)
		sysfatal("can't change uid for %s: %r", argv[0]);
	if(newns(argv[0], namespace) < 0)
		sysfatal("can't build namespace: %r");

	argv++;
	if(--argc == 0)
		argv = defargv;

	run(argv);
}

/*
 *  create a change uid capability 
 */
char*
mkcap(char *from, char *to)
{
	uchar rand[20];
	char *cap;
	char *key;
	int nfrom, nto;
	uchar hash[SHA1dlen];
	int fd;

	fd = open("#¤/caphash", OCEXEC|OWRITE);
	if(fd < 0)
		return nil;

	/* create the capability */
	nto = strlen(to);
	nfrom = strlen(from);
	cap = malloc(nfrom+1+nto+1+sizeof(rand)*3+1);
	if(cap == nil)
		sysfatal("malloc: %r");
	sprint(cap, "%s@%s", from, to);
	genrandom(rand, sizeof(rand));
	key = cap+nfrom+1+nto+1;
	enc64(key, sizeof(rand)*3, rand, sizeof(rand));

	/* hash the capability */
	hmac_sha1((uchar*)cap, strlen(cap), (uchar*)key, strlen(key), hash, nil);

	/* give the kernel the hash */
	key[-1] = '@';
	if(write(fd, hash, SHA1dlen) < 0){
		close(fd);
		free(cap);
		return nil;
	}
	close(fd);

	return cap;
}

int
usecap(char *cap)
{
	int fd, rv;

	fd = open("#¤/capuse", OWRITE);
	if(fd < 0)
		return -1;
	rv = write(fd, cap, strlen(cap));
	close(fd);
	return rv;
}

int
becomeuser(char *new)
{
	char *cap;
	int rv;

	cap = mkcap(getuser(), new);
	if(cap == nil)
		return -1;
	rv = usecap(cap);
	free(cap);
	return rv;
}
