#include <u.h>
#include <libc.h>
#include <libsec.h>
#include "dat.h"

void
usage(void)
{
	fprint(2, "usage: token key [token]\n");
	exits("usage");
}

static char*
mktoken(char *key, long t)
{
	char *now, token[64];
	uchar digest[SHA1dlen];

	now = ctime(t);
	memset(now+11, ':', 8);
	hmac_sha1((uchar*)now, strlen(now), (uchar*)key, strlen(key), digest, nil);
	enc64(token, sizeof token, digest, sizeof digest);
	return smprint("%.5s", token);
}

static char*
check_token(char *key, char *file)
{
	char *s, buf[1024];
	int i, fd, m;
	long now;

	fd = open(file, OREAD);
	if(fd < 0)
		return "no match";
	i = read(fd, buf, sizeof buf-1);
	close(fd);
	if(i < 0)
		return "no match";
	buf[i] = 0;
	now = time(0);
	for(i = 0; i < 14; i++){
		s = mktoken(key, now-24*60*60*i);
		m = s != nil && strstr(buf, s) != nil;
		free(s);
		if(m)
			return nil;
	}
	return "no match";
}

static char*
create_token(char *key)
{
	print("%s", mktoken(key, time(0)));
	return nil;
}

void
main(int argc, char **argv)
{
	ARGBEGIN {
	} ARGEND;

	switch(argc){
	case 2:
		exits(check_token(argv[0], argv[1]));
	case 1:
		exits(create_token(argv[0]));
	default:
		usage();
	}
	exits(0);
}
