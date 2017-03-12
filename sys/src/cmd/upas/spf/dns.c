#include "spf.h"

extern char	dflag;
extern char	vflag;
extern char	*netroot;

static int
timeout(void*, char *msg)
{
	if(strstr(msg, "alarm")){
		fprint(2, "deferred: dns timeout");
		exits("deferred: dns timeout");
	}
	return 0;
}

static Ndbtuple*
tdnsquery(char *r, char *s, char *v)
{
	long a;
	Ndbtuple *t;

	atnotify(timeout, 1);
	a = alarm(15*1000);
	t = dnsquery(r, s, v);
	alarm(a);
	atnotify(timeout, 0);
	return t;
}

Ndbtuple*
vdnsquery(char *s, char *v, int recur)
{
	Ndbtuple *n, *t;
	static int nquery;

	/* conflicts with standard: must limit to 10 and -> fail */
	if(recur > 5 || ++nquery == 25){
		fprint(2, "dns query limited %d %d\n", recur, nquery);
		return 0;
	}
	if(dflag)
		fprint(2, "dnsquery(%s, %s, %s) ->\n", netroot, s, v);
	t = tdnsquery(netroot, s, v);
	if(dflag)
		for(n = t; n; n = n->entry)
			fprint(2, "\t%s\t%s\n", n->attr, n->val);
	return t;
}

void
dnreverse(char *s, int l, char *d)
{
	char *p, *e, buf[100], *f[15];
	int i, n;

	n = getfields(d, f, nelem(f), 0, ".");
	p = e = buf;
	if(l < sizeof buf)
		e += l;
	else
		e += sizeof buf;
	for(i = 1; i <= n; i++)
		p = seprint(p, e, "%s.", f[n-i]);
	if(p > buf)
		p = seprint(p-1, e, ".in-addr.arpa");
	memmove(s, buf, p-buf+1);
}

int
dncontains(char *d, char *s)
{
loop:
	if(!strcmp(d, s))
		return 1;
	if(!(s = strchr(s, '.')))
		return 0;
	s++;
	goto loop;	
}

