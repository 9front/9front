#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	<authsrv.h>

char	*eve;
char	hostdomain[DOMLEN];

/*
 *  return true if current user is eve
 */
int
iseve(void)
{
	return strcmp(eve, up->user) == 0;
}

uintptr
sysfversion(va_list list)
{
	int msize, arglen, fd;
	char *vers;
	Chan *c;

	fd = va_arg(list, int);
	msize = va_arg(list, int);
	vers = va_arg(list, char*);
	arglen = va_arg(list, int);
	validaddr((uintptr)vers, arglen, 1);
	/* check there's a NUL in the version string */
	if(arglen <= 0 || memchr(vers, 0, arglen) == nil)
		error(Ebadarg);
	c = fdtochan(fd, ORDWR, 0, 1);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	msize = mntversion(c, vers, msize, arglen);
	cclose(c);
	poperror();
	return msize;
}

uintptr
sys_fsession(va_list list)
{
	int fd;
	char *str;
	uint len;

	/* deprecated; backwards compatibility only */
	fd = va_arg(list, int);
	str = va_arg(list, char*);
	len = va_arg(list, uint);
	if(len == 0)
		error(Ebadarg);
	validaddr((uintptr)str, len, 1);
	*str = '\0';
	USED(fd);
	return 0;
}

uintptr
sysfauth(va_list list)
{
	Chan *c, *ac;
	char *aname;
	int fd;

	fd = va_arg(list, int);
	aname = va_arg(list, char*);
	validaddr((uintptr)aname, 1, 0);
	aname = validnamedup(aname, 1);
	if(waserror()){
		free(aname);
		nexterror();
	}
	c = fdtochan(fd, ORDWR, 0, 1);
	if(waserror()){
		cclose(c);
		nexterror();
	}

	ac = mntauth(c, aname);
	/* at this point ac is responsible for keeping c alive */
	poperror();	/* c */
	cclose(c);
	poperror();	/* aname */
	free(aname);

	if(waserror()){
		cclose(ac);
		nexterror();
	}

	fd = newfd(ac);
	if(fd < 0)
		error(Enofd);
	poperror();	/* ac */

	/* always mark it close on exec */
	ac->flag |= CCEXEC;
	return (uintptr)fd;
}

/*
 *  called by devcons() for user device
 *
 *  anyone can become none
 */
long
userwrite(char *a, int n)
{
	if(n!=4 || strncmp(a, "none", 4)!=0)
		error(Eperm);
	procsetuser(up, "none");
	return n;
}

/*
 *  called by devcons() for host owner/domain
 *
 *  writing hostowner also sets user
 */
long
hostownerwrite(char *a, int n)
{
	char buf[KNAMELEN];

	if(!iseve())
		error(Eperm);
	if(n <= 0)
		error(Ebadarg);
	if(n >= sizeof buf)
		error(Etoolong);
	memmove(buf, a, n);
	buf[n] = 0;

	renameuser(eve, buf);
	srvrenameuser(eve, buf);
	shrrenameuser(eve, buf);
	kstrdup(&eve, buf);
	procsetuser(up, buf);
	return n;
}

long
hostdomainwrite(char *a, int n)
{
	char buf[DOMLEN];

	if(!iseve())
		error(Eperm);
	if(n <= 0 || n >= DOMLEN)
		error(Ebadarg);
	memset(buf, 0, DOMLEN);
	strncpy(buf, a, n);
	if(buf[0] == 0)
		error(Ebadarg);
	memmove(hostdomain, buf, DOMLEN);
	return n;
}
