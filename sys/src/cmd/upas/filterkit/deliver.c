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
	char *to;
	Tmfmt tf;
	Tm tm;
	int r;
	Addr *a;

	ARGBEGIN{
	}ARGEND;
	tmfmtinstall();
	if(argc != 3)
		usage();
	if(to = strrchr(argv[0], '!'))
		to++;
	else
		to = argv[0];
	a = readaddrs(argv[1], nil);
	if(a == nil)
		sysfatal("missing from address");
	tf = thedate(&tm);
	werrstr("");
	r = fappendfolder(a->val, tmnorm(&tm), argv[2], 0);
	syslog(0, "mail", "delivered %s From %s %τ (%s) %d %r", to, a->val, tf, argv[0], r);
	exits("");
}
