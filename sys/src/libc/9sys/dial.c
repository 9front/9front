#include <u.h>
#include <libc.h>

typedef struct DS DS;

static int	call(char*, char*, char*, DS*);
static int	csdial(DS*);
static void	_dial_string_parse(char*, DS*);

enum
{
	Maxstring	= 128,
	Maxpath		= 256,
};

struct DS {
	/* dist string */
	char	buf[Maxstring];
	char	*netdir;
	char	*proto;
	char	*rem;

	/* other args */
	char	*local;
	char	*dir;
	int	*cfdp;
};


/*
 *  the dialstring is of the form '[/net/]proto!dest'
 */
int
dial(char *dest, char *local, char *dir, int *cfdp)
{
	DS ds;
	int rv;
	char err[ERRMAX];

	ds.local = local;
	ds.dir = dir;
	ds.cfdp = cfdp;

	_dial_string_parse(dest, &ds);
	if(ds.netdir)
		return csdial(&ds);

	ds.netdir = "/net";
	rv = csdial(&ds);
	if(rv >= 0)
		return rv;
	*err = 0;
	errstr(err, sizeof err);
	if(strcmp(err, "interrupted") == 0 || strstr(err, "refused") != nil){
		errstr(err, sizeof err);
		return rv;
	}

	ds.netdir = "/net.alt";
	rv = csdial(&ds);
	if(rv >= 0)
		return rv;
	errstr(err, sizeof err);
	if(strstr(err, "translate") == nil && strstr(err, "does not exist") == nil)
		errstr(err, sizeof err);
	return rv;
}

static int
csdial(DS *ds)
{
	int n, fd, rv;
	char *rem, *loc, buf[Maxstring], clone[Maxpath], err[ERRMAX];

	/*
	 *  open connection server
	 */
	snprint(buf, sizeof(buf), "%s/cs", ds->netdir);
	fd = open(buf, ORDWR);
	if(fd < 0){
		/* no connection server, don't translate */
		snprint(clone, sizeof(clone), "%s/%s/clone", ds->netdir, ds->proto);
		return call(clone, ds->rem, ds->local, ds);
	}

	/*
	 *  ask connection server to translate
	 */
	snprint(buf, sizeof(buf), "%s!%s", ds->proto, ds->rem);
	if(write(fd, buf, strlen(buf)) < 0){
		close(fd);
		return -1;
	}

	/*
	 *  loop through each address from the connection server till
	 *  we get one that works.
	 */
	rv = -1;
	*err = 0;
	seek(fd, 0, 0);
	while((n = read(fd, buf, sizeof(buf) - 1)) > 0){
		buf[n] = 0;
		rem = strchr(buf, ' ');
		if(rem == nil)
			continue;
		*rem++ = 0;
		loc = strchr(rem, ' ');
		if(loc != nil)
			*loc++ = 0;
		rv = call(buf, rem, ds->local!=nil? ds->local: loc, ds);
		if(rv >= 0)
			break;
		errstr(err, sizeof err);
		if(strcmp(err, "interrupted") == 0)
			break;
		if(strstr(err, "does not exist") != nil)
			errstr(err, sizeof err);	/* get previous error back */
	}
	close(fd);

	/* restore errstr if any */
	if(rv < 0 && *err)
		errstr(err, sizeof err);

	return rv;
}

static int
call(char *clone, char *remote, char *local, DS *ds)
{
	int fd, cfd, n;
	char cname[Maxpath], name[Maxpath], data[Maxpath], *p;

	/* because cs is in a different name space, replace the mount point */
	if(*clone == '/' || *clone == '#'){
		p = strchr(clone+1, '/');
		if(p == nil)
			p = clone;
		else
			p++;
	} else
		p = clone;
	snprint(cname, sizeof cname, "%s/%s", ds->netdir, p);

	cfd = open(cname, ORDWR);
	if(cfd < 0)
		return -1;

	/* get directory name */
	n = read(cfd, name, sizeof(name)-1);
	if(n < 0){
		close(cfd);
		return -1;
	}
	name[n] = 0;
	for(p = name; *p == ' '; p++)
		;
	snprint(name, sizeof(name), "%ld", strtoul(p, 0, 0));
	p = strrchr(cname, '/');
	*p = 0;
	if(ds->dir)
		snprint(ds->dir, NETPATHLEN, "%s/%s", cname, name);
	snprint(data, sizeof(data), "%s/%s/data", cname, name);

	/* connect */
	if(local != nil)
		snprint(name, sizeof(name), "connect %s %s", remote, local);
	else
		snprint(name, sizeof(name), "connect %s", remote);
	if(write(cfd, name, strlen(name)) < 0){
		close(cfd);
		return -1;
	}

	/* open data connection */
	fd = open(data, ORDWR);
	if(fd < 0){
		close(cfd);
		return -1;
	}
	if(ds->cfdp)
		*ds->cfdp = cfd;
	else
		close(cfd);
	return fd;
}

/*
 *  parse a dial string
 */
static void
_dial_string_parse(char *str, DS *ds)
{
	char *p, *p2;

	strncpy(ds->buf, str, Maxstring);
	ds->buf[Maxstring-1] = 0;

	p = strchr(ds->buf, '!');
	if(p == 0) {
		ds->netdir = 0;
		ds->proto = "net";
		ds->rem = ds->buf;
	} else {
		p2 = ds->buf;
		if(*p2 == '#'){
			p2 = strchr(p2, '/');
			if(p2 == nil || p2 > p)
				p2 = ds->buf;
		}
		if(*p2 != '/'){
			ds->netdir = 0;
			ds->proto = ds->buf;
		} else {
			for(p2 = p; *p2 != '/'; p2--)
				;
			*p2++ = 0;
			ds->netdir = ds->buf;
			ds->proto = p2;
		}
		*p = 0;
		ds->rem = p + 1;
	}
}
