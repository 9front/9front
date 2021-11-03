#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ndb.h>
#include <ndbhf.h>
#include <ip.h>

static Ndbtuple *doquery(int, char *dn, char *type);

/*
 *  search for a tuple that has the given 'attr=val' and also 'rattr=x'.
 *  copy 'x' into 'buf' and return the whole tuple.
 *
 *  return nil if not found.
 */
Ndbtuple*
dnsquery(char *net, char *val, char *type)
{
	char buf[128];
	Ndbtuple *t;
	int fd;

	/* if the address is V4 or V6 null address, give up early */
	if(strcmp(val, "::") == 0 || strcmp(val, "0.0.0.0") == 0)
		return nil;

	if(net == nil)
		net = "/net";

	snprint(buf, sizeof(buf), "%s/dns", net);
	if((fd = open(buf, ORDWR|OCEXEC)) < 0)
		return nil;

	/* zero out the error string */
	werrstr("");

	/* if this is a reverse lookup, first lookup the domain name */
	if(strcmp(type, "ptr") == 0){
		mkptrname(val, buf, sizeof buf);
		t = doquery(fd, buf, "ptr");
	} else
		t = doquery(fd, val, type);

	/*
	 * TODO: make fd static and keep it open to reduce 9P traffic
	 * walking to /net*^/dns.
	 */
	close(fd);
	ndbsetmalloctag(t, getcallerpc(&net));
	return t;
}

static Ndbtuple*
doquery(int fd, char *dn, char *type)
{
	char buf[1024];
	int n;
	Ndbtuple *t, *first, *last;

	snprint(buf, sizeof(buf), "!%s %s", dn, type);
	if(write(fd, buf, strlen(buf)) < 0)
		return nil;
		
	seek(fd, 0, 0);

	first = last = nil;
	
	for(;;){
		n = read(fd, buf, sizeof(buf)-2);
		if(n <= 0)
			break;
		if(buf[n-1] != '\n')
			buf[n++] = '\n';	/* ndbparsline needs a trailing new line */
		buf[n] = 0;

		/* check for the error condition */
		if(buf[0] == '!'){
			werrstr("%s", buf+1);
			return nil;
		}

		t = _ndbparseline(buf);
		if(t != nil){
			if(first != nil)
				last->entry = t;
			else
				first = t;
			last = t;
			while(last->entry != nil)
				last = last->entry;
		}
	}
	return first;
}
