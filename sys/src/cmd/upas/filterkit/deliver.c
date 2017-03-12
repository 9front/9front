/*
 * deliver recipient fromfile mbox - append stdin to mbox with locking & logging
 */
#include "dat.h"
#include "common.h"

void
usage(void)
{
	fprint(2, "usage: deliver recipient fromaddr-file mbox\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *to, *s;
	int r;
	long l;
	Addr *a;

	ARGBEGIN{
	}ARGEND;
	if(argc != 3)
		usage();
	if(to = strrchr(argv[0], '!'))
		to++;
	else
		to = argv[0];
	a = readaddrs(argv[1], nil);
	if(a == nil)
		sysfatal("missing from address");
	s = ctime(l = time(0));
	werrstr("");
	r = fappendfolder(a->val, l, argv[2], 0);
	syslog(0, "mail", "delivered %s From %s %.28s (%s) %d %r", to, a->val, s, argv[0], r);
	exits("");
}
