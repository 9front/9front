#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

#include <bio.h>

enum {
	NHASH = 251,
};

typedef struct Ident Ident;
struct Ident
{
	Ident	*nextid;
	Ident	*nextname;
	int	id;
	char	name[];
};

struct Idmap
{
	Ident	*tab[NHASH];
};

static Ident**
nametab(Idmap *map, char *name)
{
	return &map->tab[(uint)namehash(name) % NHASH];
}
static Ident**
idtab(Idmap *map, int id)
{
	return &map->tab[(uint)id % NHASH];
}

static int
name2id(Idmap *map, char *name)
{
	Ident *e;

	for(e = *nametab(map, name); e != nil; e = e->nextid){
		if(strcmp(e->name, name) == 0)
			return e->id;
	}
	return -1;
}

static char*
id2name(Idmap *map, int id)
{
	Ident *e;

	for(e = *idtab(map, id); e != nil; e = e->nextname){
		if(e->id == id)
			return e->name;
	}
	return nil;
}

static void
idmap(Idmap *map, char *name, int id)
{
	Ident *e, **h;
	int n;

	n = strlen(name)+1;
	e = malloc(sizeof(Ident)+n);
	if(e == nil)
		return;

	e->id = id;
	h = idtab(map, e->id);
	e->nextid = *h;
	*h = e;

	memmove(e->name, name, n);
	h = nametab(map, e->name);
	e->nextname = *h;
	*h = e;
}

static Idmap*
readidmap(char *file, int style)
{
	Biobuf *b;
	Idmap *m;
	char *l, *name;
	int id;

	if((b = Bopen(file, OREAD)) == nil)
		return nil;

	if((m = mallocz(sizeof(*m), 1)) == nil)
		goto Out;

	while((l = Brdline(b, '\n')) != nil){
		l[Blinelen(b)-1] = 0;
		switch(style){
		case '9':
			id = 9000000 + strtol(l, &l, 10);
			if(*l != ':')
				continue;
			name = ++l;
			l = strchr(l, ':');
			if(l == 0)
				continue;
			*l = 0;
			break;
		default:
			name = l;
			l = strchr(l, ':');
			if(l == 0)
				continue;
			*l++ = 0;
			/* skip password */
			l = strchr(l, ':');
			if(l == 0)
				continue;
			id = strtol(l+1, 0, 10);
			break;
		}
		idmap(m, name, id);
	}
Out:
	Bterm(b);

	return m;
}

void
unixidmap(Share *share)
{
	static Idmap emptymap;
	char *file;

	if(share->stype != STYPE_DISKTREE)
		goto Out;

	file = smprint("%s/etc/passwd", share->root);
	share->users = readidmap(file, 'u');
	free(file);
	file = smprint("%s/etc/group", share->root);
	share->groups = readidmap(file, 'u');
	free(file);
	if(share->users != nil && share->groups != nil)
		return;

	file = smprint("%s/adm/users", share->root);
	share->users = share->groups = readidmap(file, '9');
	free(file);
	if(share->users != nil && share->groups != nil)
		return;

Out:
	share->users = share->groups = &emptymap;
}

char*
unixname(Share *share, int id, int group)
{
	return id2name(group? share->groups: share->users, id);
}

int
unixuid(Share *share, char *name)
{
	return name2id(share->users, name);
}

int
unixgid(Share *share, char *name)
{
	return name2id(share->groups, name);
}
