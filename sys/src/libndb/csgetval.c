#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ndb.h>
#include <ndbhf.h>

/*
 *  search for a tuple that has the given 'attr=val' and also 'rattr=x'.
 *  copy 'x' into 'buf' and return the whole tuple.
 *
 *  return 0 if not found.
 */
char*
csgetvalue(char *netroot, char *attr, char *val, char *rattr, Ndbtuple **pp)
{
	Ndbtuple *t, *first, *last;
	char line[1024];
	int fd, n;
	char *rv;

	if(pp != nil)
		*pp = nil;

	if(netroot)
		snprint(line, sizeof(line), "%s/cs", netroot);
	else
		strcpy(line, "/net/cs");
	fd = open(line, ORDWR);
	if(fd < 0)
		return nil;
	seek(fd, 0, 0);
	snprint(line, sizeof(line), "!%s=%s %s=*", attr, val, rattr);
	if(write(fd, line, strlen(line)) < 0){
		close(fd);
		return nil;
	}
	seek(fd, 0, 0);

	rv = nil;
	first = last = nil;
	for(;;){
		n = read(fd, line, sizeof(line)-2);
		if(n <= 0)
			break;
		line[n] = '\n';
		line[n+1] = 0;

		t = _ndbparseline(line);
		if(t == nil)
			continue;
		if(first != nil)
			last->entry = t;
		else
			first = t;
		do {
			last = t;
			if(rv == nil && strcmp(rattr, t->attr) == 0)
				rv = strdup(t->val);
			t = t->entry;
		} while(t != nil);
	}
	close(fd);

	if(pp != nil){
		setmalloctag(first, getcallerpc(&netroot));
		*pp = first;
	} else
		ndbfree(first);

	return rv;
}
