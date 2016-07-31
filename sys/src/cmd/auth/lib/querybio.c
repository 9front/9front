#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>
#include <authsrv.h>
#include "authcmdlib.h"

static void
ask(char *prompt, char **sp, int must, int *changed)
{
	char pr[128], *def, *ans;

	def = *sp;
	if(def && *def){
		if(must)
			snprint(pr, sizeof pr, "%s[return = %s]", prompt, def);
		else
			snprint(pr, sizeof pr, "%s[return = %s, space = none]", prompt, def);
	} else
		snprint(pr, sizeof pr, "%s", prompt);
	ans = readcons(pr, nil, 0);
	if(ans == nil || *ans == 0){
		free(ans);
		return;
	}
	if(*ans == ' ' && !must){
		free(ans);
		ans = nil;
	}
	*sp = ans;
	*changed = 1;
	free(def);
}

/*
 *  get bio from stdin
 */
int
querybio(char *file, char *user, Acctbio *a)
{
	int i;
	int changed;

	rdbio(file, user, a);
	ask("Post id", &a->postid, 0, &changed);
	ask("User's full name", &a->name, 1, &changed);
	ask("Department #", &a->dept, 1, &changed);
	ask("User's email address", &a->email[0], 1, &changed);
	ask("Sponsor's email address", &a->email[1], 0, &changed);
	for(i = 2; i < Nemail; i++){
		if(a->email[i-1] == 0)
			break;
		ask("other email address", &a->email[i], 0, &changed);
	}
	return changed;
}
