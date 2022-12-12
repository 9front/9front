/*
 * IMPORTANT!  DO NOT ADD LIBRARY CALLS TO THIS FILE.
 * The entire text image must fit on one page
 * (and there's no data segment, so any read/write data must be on the stack).
 */

#include <u.h>
#include <libc.h>

char cons[] = "/dev/cons";
char boot[] = "/boot/boot";
char dev[] = "/dev";
char c[] = "#c";
char d[] = "#d";
char e[] = "#e";
char ec[] = "#ec";
char p[] = "#p";
char s[] = "#s";
char σ[] = "#σ";
char env[] = "/env";
char fd[] = "/fd";
char proc[] = "/proc";
char srv[] = "/srv";
char shr[] = "/shr";

void
startboot(char*, char **argv)
{
	char buf[200];	/* keep this fairly large to capture error details */

	bind(c, dev, MAFTER);
	bind(d, fd, MREPL);
	bind(ec, env, MAFTER);
	bind(e, env, MCREATE|MAFTER);
	bind(p, proc, MREPL);
	bind(s, srv, MREPL|MCREATE);
	bind(σ, shr, MREPL);

	open(cons, OREAD);
	open(cons, OWRITE);
	open(cons, OWRITE);

	exec(boot, argv);

	rerrstr(buf, sizeof buf);
	buf[sizeof buf - 1] = '\0';
	_exits(buf);
}
