#include "common.h"
#include "send.h"

/* dispose of local addresses */
int
cat_mail(dest *dp, message *mp)
{
	char *rcvr, *cp, *s;
	String *ss;
	Biobuf *b;
	int e;

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
	b = openfolder(s, time(0));
	s_free(ss);
	if(b == nil)
		return refuse(dp, mp, "mail file cannot be created", 0, 0);
	e = m_print(mp, b, 0, 1) == -1 || Bprint(b, "\n") == -1;
	e |= closefolder(b);
	if(e != 0)
		return refuse(dp, mp, "error writing mail file", 0, 0);
	rcvr = s_to_c(dp->addr);
	if(cp = strrchr(rcvr, '!'))
		rcvr = cp+1;
	logdelivery(dp, rcvr, mp);
	return 0;
}
