#include <u.h>
#include <libc.h>
#include <draw.h>

/*
 * Default version: treat as file name
 */

Subfont*
_getsubfont(Display *d, char *name)
{
	int dolock, fd;
	Subfont *f;

	/*
	 * unlock display so i/o happens with display released, unless
	 * user is doing his own locking, in which case this could break things.
	 * _getsubfont is called only from string.c and stringwidth.c,
	 * which are known to be safe to have this done.
	 */
	dolock = d != nil && d->locking == 0;
	if(dolock)
		unlockdisplay(d);

	fd = open(name, OREAD|OCEXEC);
	if(fd < 0) {
		fprint(2, "getsubfont: can't open %s: %r\n", name);
		f = nil;
	} else {
		f = readsubfont(d, name, fd, dolock);
		if(f == nil)
			fprint(2, "getsubfont: can't read %s: %r\n", name);
		close(fd);
	}

	if(dolock)
		lockdisplay(d);

	return f;
}
