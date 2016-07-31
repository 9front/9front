#include <u.h>
#include <libc.h>
#include <bio.h>
#include <authsrv.h>
#include "authcmdlib.h"

int
answer(char *q)
{
	char pr[128];
	int y;

	snprint(pr, sizeof(pr), "%s [y/n]", q);
	q = readcons(pr, nil, 0);
	y = q != nil && (*q == 'y' || *q == 'Y');
	free(q);
	return y;
}
