/* © 2008 erik quanstrom; plan 9 license */
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ndb.h>
#include <ip.h>

char		*macro(char*, char*, char*, char*, char*);

Ndbtuple*	vdnsquery(char*, char*, int);
int		dncontains(char*, char *);
void		dnreverse(char*, int, char*);
