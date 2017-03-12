#include "common.h"
#include "send.h"

static String*
mboxpath(char *path, char *user, String *to)
{
	char buf[Pathlen];

	mboxpathbuf(buf, sizeof buf, user, path);
	return s_append(to, buf);
}

static void
mboxfile(dest *dp, String *user, String *path, char *file)
{
	char *cp;

	mboxpath(s_to_c(user), s_to_c(dp->addr), path);
	cp = strrchr(s_to_c(path), '/');
	if(cp)
		path->ptr = cp+1;
	else
		path->ptr = path->base;
	s_append(path, file);
}

/*
 * BOTCH, BOTCH
 * the problem is that we don't want to say a user exists
 * just because the user has a mail box directory.  that
 * precludes using mode bits to disable mailboxes.
 *
 * botch #1: we pretend like we know that there must be a
 * corresponding file or directory /mail/box/$user[/folder]/mbox
 * this is wrong, but we get away with this for local mail boxes.
 *
 * botch #2: since the file server and not the auth server owns
 * groups, it's not possible to get groups right.  this means that
 * a mailbox that only allows members of a group to post but
 * not read wouldn't work.
 */
static uint accesstx[] = {
[OREAD]	1<<2,
[OWRITE]	1<<1,
[ORDWR]	3<<1,
[OEXEC]	1<<0
};

static int
accessmbox(char *f, int m)
{
	int r, n;
	Dir *d;

	d = dirstat(f);
	if(d == nil)
		return -1;
	n = 0;
	if(m < nelem(accesstx))
		n = accesstx[m];
	if(d->mode & DMDIR)
		n |= OEXEC;
	r = (d->mode & n<<0) == n<<0;
//	if(r == 0 && inlist(mygids(), d->gid) == 0)
//		r = (d->mode & n<<3) == n<<3;
	if(r == 0 && strcmp(getlog(), d->uid) == 0)
		r = (d->mode & n<<6) == n<<6;
	r--;
	free(d);
	return r;
}

/*
 *  Check forwarding requests
 */
extern dest*
expand_local(dest *dp)
{
	Biobuf *fp;
	String *file, *line, *s;
	dest *rv;
	int forwardok;
	char *user;

	/* short circuit obvious security problems */
	if(strstr(s_to_c(dp->addr), "/../")){
		dp->status = d_unknown;
		return 0;
	}

	/* isolate user's name if part of a path */
	user = strrchr(s_to_c(dp->addr), '!');
	if(user)
		user++;
	else
		user = s_to_c(dp->addr);

	/* if no replacement string, plug in user's name */
	if(dp->repl1 == 0){
		dp->repl1 = s_new();
		mboxpath("mbox", user, dp->repl1);
	}

	s = unescapespecial(s_clone(dp->repl1));

	/*
	 *  if this is the descendant of a `forward' file, don't
	 *  look for a forward.
	 */
	forwardok = 1;
	for(rv = dp->parent; rv; rv = rv->parent)
		if(rv->status == d_cat){
			forwardok = 0;
			break;
		}
	file = s_new();
	if(forwardok){
		/*
		 *  look for `forward' file for forwarding address(es)
		 */
		mboxfile(dp, s, file, "forward");
		fp = sysopen(s_to_c(file), "r", 0);
		if (fp != 0) {
			line = s_new();
			for(;;){
				if(s_read_line(fp, line) == nil)
					break;
				if(*(line->ptr - 1) != '\n')
					break;
				if(*(line->ptr - 2) == '\\')
					*(line->ptr-2) = ' ';
				*(line->ptr-1) = ' ';
			}
			sysclose(fp);
			if(debug)
				fprint(2, "forward = %s\n", s_to_c(line));
			rv = s_to_dest(s_restart(line), dp);
			s_free(line);
			if(rv){
				s_free(file);
				s_free(s);
				return rv;
			}
		}
	}

	/*
	 *  look for a 'pipe' file.  This won't work if there are
	 *  special characters in the account name since the file
	 *  name passes through a shell.  tdb.
	 */
	mboxfile(dp, dp->repl1, s_reset(file), "pipeto");
	if(access(s_to_c(file), AEXEC) == 0){
		if(debug)
			fprint(2, "found a pipeto file\n");
		dp->status = d_pipeto;
		line = s_new();
		s_append(line, "upasname='");
		s_append(line, user);
		s_append(line, "' ");
		s_append(line, s_to_c(file));
		s_append(line, " ");
		s_append(line, s_to_c(dp->addr));
		s_append(line, " ");
		s_append(line, s_to_c(dp->repl1));
		s_free(dp->repl1);
		dp->repl1 = line;
		s_free(file);
		s_free(s);
		return dp;
	}

	/*
	 *  see if the mailbox directory exists
	 */
	mboxfile(dp, s, s_reset(file), "mbox");
	if(accessmbox(s_to_c(file), OWRITE) != -1)
		dp->status = d_cat;
	else
		dp->status = d_unknown;
	s_free(file);
	s_free(s);
	return 0;
}
