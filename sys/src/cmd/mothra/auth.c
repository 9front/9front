#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <panel.h>
#include <bio.h>
#include "mothra.h"

static int
basicauth(char *arg, char *str, int n)
{
	int i;
	char *p;
	char buf[1024];
	Biobuf *b;

	if(strncmp(arg, "realm=", 6) == 0)
		arg += 6;
	if(*arg == '"'){
		arg++;
		for(p = arg; *p && *p != '"'; p++);
		*p = 0;
	} else {
		for(p = arg; *p && *p != ' ' && *p != '\t'; p++);
		*p = 0;
	}

	p = getenv("home");
	if(p == 0){
		werrstr("$home not set");
		return -1;
	}
	snprint(buf, sizeof(buf), "%s/lib/mothra/insecurity", p);
	b = Bopen(buf, OREAD);
	if(b == 0){
		werrstr("www password file %s: %r", buf);
		return -1;
	}

	i = strlen(arg);
	while(p = Brdline(b, '\n'))
		if(strncmp(arg, p, i) == 0 && p[i] == '\t')
			break;
	if(p == 0){
		Bterm(b);
		werrstr("no basic password for domain `%s'", arg);
		return -1;
	}

	p[Blinelen(b)-1] = 0;
	for(p += i; *p == '\t'; p++);
	if (enc64(buf, sizeof buf, (uchar*)p, strlen(p)) < 0) {
		Bterm(b);
		werrstr("password too long: %s", p);
		return -1;
	}
	snprint(str, n, "Authorization: Basic %s\r\n", buf);
	return 0;
}

int
auth(Url *url, char *str, int n)
{
	if(cistrcmp(url->authtype, "basic") == 0)
		return basicauth(url->autharg, str, n);
	werrstr("unknown auth method %s", url->authtype);
	return -1;
}
