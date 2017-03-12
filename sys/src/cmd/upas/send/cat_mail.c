#include "common.h"
#include "send.h"

/*
 * warning will robinson
 *
 * mbox and mdir should likely be merged with ../common/folder.c
 * at a minimum, changes need to done in sync.
 */

static int
mbox(dest *dp, message *mp, char *s)
{
	char *tmp;
	int i, n, e;
	Biobuf *b;
	Mlock *l;

	for(i = 0;; i++){
		l = syslock(s);
		if(l == 0)
			return refuse(dp, mp, "can't lock mail file", 0, 0);
		b = sysopen(s, "al", Mboxmode);
		if(b)
			break;
		b = sysopen(tmp = smprint("%s.tmp", s), "al", Mboxmode);
		free(tmp);
		sysunlock(l);
		if(b){
			syslog(0, "mail", "error: used %s.tmp", s);
			break;
		}
		if(i >= 5)
			return refuse(dp, mp, "mail file cannot be opened", 0, 0);
		sleep(1000);
	}
	e = 0;
	n = m_print(mp, b, 0, 1);
	if(n == -1 || Bprint(b, "\n") == -1 || Bflush(b) == -1)
		e = 1;
	sysclose(b);
	sysunlock(l);
	if(e)
		return refuse(dp, mp, "error writing mail file", 0, 0);
	return 0;
}

static int
mdir(dest *dp, message *mp, char *s)
{
	char buf[100];
	int fd, i, n, e;
	ulong t;
	Biobuf b;

	t = time(0);
	for(i = 0; i < 100; i++){
		snprint(buf, sizeof buf, "%s/%lud.%.2d", s, t, i);
		if((fd = create(buf, OWRITE|OEXCL, DMAPPEND|0660)) != -1)
			goto found;
	}
	return refuse(dp, mp, "mdir file cannot be opened", 0, 0);
found:
	e = 0;
	Binit(&b, fd, OWRITE);
	n = m_print(mp, &b, 0, 1);
	if(n == -1 || Bprint(&b, "\n") == -1 || Bflush(&b) == -1)
		e = 1;
	Bterm(&b);
	close(fd);
	if(e){
		remove(buf);
		return refuse(dp, mp, "error writing mail file", 0, 0);
	}
	return 0;
}

/* dispose of local addresses */
int
cat_mail(dest *dp, message *mp)
{
	char *rcvr, *cp, *s;
	int e, isdir;
	Dir *d;
	String *ss;

	ss = unescapespecial(s_clone(dp->repl1));
	s = s_to_c(ss);
	if (flagn) {
		if(!flagx)
			print("upas/mbappend %s\n", s);
		else
			print("%s\n", s_to_c(dp->addr));
		s_free(ss);
		return 0;
	}
	/* avoid lock errors */
	if(strcmp(s, "/dev/null") == 0){
		s_free(ss);
		return(0);
	}
	if(d = dirstat(s)){
		isdir = d->mode&DMDIR;
		free(d);
	}else{
		isdir = create(s, OREAD, DMDIR|0777);
		if(isdir == -1)
			return refuse(dp, mp, "mdir cannot be created", 0, 0);
		close(isdir);
		isdir = 1;
	}
	if(isdir)
		e = mdir(dp, mp, s);
	else
		e = mbox(dp, mp, s);
	s_free(ss);
	if(e != 0)
		return e;
	rcvr = s_to_c(dp->addr);
	if(cp = strrchr(rcvr, '!'))
		rcvr = cp+1;
	logdelivery(dp, rcvr, mp);
	return 0;
}
