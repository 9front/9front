#include <u.h>
#include <libc.h>
#include <auth.h>

/*
 *  become the authenticated user
 */
int
auth_chuid(AuthInfo *ai, char *ns)
{
	int rv, fd;

	if(ai == nil || ai->cap == nil || ai->cap[0] == 0){
		werrstr("no capability");
		return -1;
	}

	/* change uid */
	fd = open("/dev/capuse", OCEXEC|OWRITE);
	if(fd < 0){
		werrstr("opening /dev/capuse: %r");
		return -1;
	}
	rv = write(fd, ai->cap, strlen(ai->cap));
	close(fd);
	if(rv < 0){
		werrstr("writing %s to /dev/capuse: %r", ai->cap);
		return -1;
	}

	/* get a link to factotum as new user */
	fd = open("/srv/factotum", ORDWR);
	if(fd >= 0){
		if(mount(fd, -1, "/mnt", MREPL, "") == -1)
			close(fd);
	}

	/* set up new namespace */
	return newns(ai->cuid, ns);
}
