#include <u.h>
#include <libc.h>
#include "group.h"

int
loadgroups(Groups *gs, char *raw)
{
	char *m, *s, *e, *a[5], *ide;
	Group *g, *memb;
	int line, n, k;
	vlong id;

	memset(gs, 0, sizeof(*gs));
	if((gs->raw = strdup(raw)) == nil)
		goto error;

	line = 1;
	for(s = gs->raw; *s; s = e+1, line++){
		if((e = strchr(s, '\n')) != nil)
			*e = 0;

		if((n = getfields(s, a, nelem(a), 1, ":")) >= 3 && strlen(a[0]) > 0 && strlen(a[2]) > 0){
			id = strtoll(a[2], &ide, 0);
			if(id < 0 || id > 0xffffffff || *ide != 0){
				werrstr("invalid uid: %s", a[2]);
				goto error;
			}

			if((g = realloc(gs->g, (gs->ng+1)*sizeof(Group))) == nil)
				goto error;
			gs->g = g;
			g += gs->ng++;
			memset(g, 0, sizeof(*g));
			g->id = id;
			g->name = a[0];
			for(m = a[3]; n > 3 && *m; *m++ = 0){
				if((memb = realloc(g->memb, (g->nmemb+1)*sizeof(Group))) == nil)
					goto error;
				g->memb = memb;
				memb += g->nmemb++;
				memset(memb, 0, sizeof(*memb));
				memb->name = m;
				if((m = strchr(m, ',')) == nil)
					break;
			}
		}else{
			werrstr("line %d: invalid record", line);
			goto error;
		}

		if(e == nil)
			break;
	}

	g = gs->g;
	for(n = 0; n < gs->ng; n++, g++){
		for(k = 0, memb = g->memb; k < g->nmemb; k++, memb++)
			findgroup(gs, memb->name, &memb->id);
	}

	return 0;
error:
	werrstr("togroups: %r");
	freegroups(gs);

	return -1;
}

void
freegroups(Groups *gs)
{
	int i;

	for(i = 0; i < gs->ng; i++)
		free(gs->g[i].memb);
	free(gs->g);
	free(gs->raw);
}

Group *
findgroup(Groups *gs, char *name, u32int *id)
{
	Group *g;
	int i;

	g = gs->g;
	for(i = 0; i < gs->ng; i++, g++){
		if(strcmp(g->name, name) == 0){
			if(id != nil)
				*id = g->id;
			return g;
		}
	}

	if(id != nil)
		*id = ~0;

	return nil;
}

Group *
findgroupid(Groups *gs, u32int id)
{
	Group *g;
	int i;

	g = gs->g;
	for(i = 0; i < gs->ng; i++, g++){
		if(g->id == id)
			return g;
	}

	return nil;
}

int
ingroup(Group *g, u32int id)
{
	int i;

	if(g->id == id)
		return 1;

	for(i = g->nmemb, g = g->memb; i > 0; i--, g++){
		if(g->id == id)
			return 1;
	}

	return 0;
}
