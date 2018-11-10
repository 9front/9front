#include <u.h>
#include <libc.h>
#include <dtracy.h>

char *
dtstrdup(char *n)
{
	char *m;
	
	m = dtmalloc(strlen(n) + 1);
	strcpy(m, n);
	setmalloctag(m, getcallerpc(&n));
	return m;
}

DTProbe *
dtpnew(DTName name, DTProvider *prov, void *aux)
{
	DTProbe *p, **pp;

	p = dtmalloc(sizeof(DTProbe));
	p->provider = dtstrdup(name.provider);
	p->function = dtstrdup(name.function);
	p->name = dtstrdup(name.name);
	p->prov = prov;
	p->aux = aux;
	p->enablist.probnext = p->enablist.probprev = &p->enablist;
	for(pp = &prov->probes; *pp != nil; pp = &(*pp)->provnext)
		;
	*pp = p;
	return p;
}

int
dtstrmatch(char *a, char *b)
{
	if(a == nil || *a == 0) return 1;
	if(b == nil) return 0;
	return strcmp(a, b) == 0;
}

int
dtpmatch(DTName name, DTProbe ***ret)
{
	DTProbe **l;
	int nl;
	DTProvider **provp, *prov;
	DTProbe **pp, *p;
	
	l = nil;
	nl = 0;
	for(provp = dtproviders; prov = *provp, prov != nil; provp++){
		if(!dtstrmatch(name.provider, prov->name))
			continue;
		for(pp = &prov->probes; p = *pp, p != nil; pp = &p->provnext)
			if(dtstrmatch(name.function, p->function) && dtstrmatch(name.name, p->name)){
				if(ret != nil){
					l = dtrealloc(l, (nl + 1) * sizeof(DTProbe *));
					l[nl] = p;
				}
				nl++;
			}
		prov->provide(prov, name);
		for(; p = *pp, p != nil; pp = &p->provnext)
			if(dtstrmatch(name.function, p->function) && dtstrmatch(name.name, p->name)){
				if(ret != nil){
					l = dtrealloc(l, (nl + 1) * sizeof(DTProbe *));
					l[nl] = p;
				}
				nl++;
			}
	}
	if(ret != nil)
		*ret = l;
	return nl;
}
