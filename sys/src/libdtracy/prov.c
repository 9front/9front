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
dtpnew(char *name, DTProvider *prov, void *aux)
{
	DTProbe *p, **pp;

	p = dtmalloc(sizeof(DTProbe));
	p->name = dtstrdup(name);
	p->prov = prov;
	p->aux = aux;
	p->enablist.probnext = p->enablist.probprev = &p->enablist;
	for(pp = &prov->probes; *pp != nil; pp = &(*pp)->provnext)
		;
	*pp = p;
	return p;
}

/* does the pattern match at most one probe (or provider if provonly)? */
static int
patunique(char *pat, int provonly)
{
	for(;; pat++)
		switch(*pat){
		case ':':
			if(provonly){
		case 0:
				return 1;
			}
			break;
		case '?':
		case '*':
			return 0;
		}
}

static char *
partmatch(char *pat, char *str, int provonly)
{
	for(;; pat++, str++){
		if(*pat == '*')
			return pat;
		if(*pat != *str && (*pat != '?' || *str == ':') && (!provonly || *pat != ':' || *str != 0))
			return nil;
		if(*pat == 0 || *pat == ':' && provonly)
			return (void*)-1;
	}
}

/*	
	do a wildcard match with * and ?, but don't match : against a wildcard
	if provonly, stop at the first :
	
	replacing empty parts with * is done in user space
*/
int
dtnamematch(char *pat, char *str, int provonly)
{
	char *patp, *strp, *p;
	
	patp = partmatch(pat, str, provonly);
	if(patp == nil) return 0;
	if(patp == (void*)-1) return 1;
	/* reached a * */
	strp = str + (patp - pat);
	patp++;
	for(;;){
		/* try the rest of the pattern against each position */
		p = partmatch(patp, strp, provonly);
		if(p == nil){
			if(*strp == 0 || *strp == ':') return 0;
			strp++;
			continue;
		}
		if(p == (void*)-1)
			return 1;
		/* reached another * */
		strp += p - patp;
		patp = p + 1;
	}
}

int
dtpmatch(char *name, DTProbe ***ret)
{
	DTProbe **l;
	int nl;
	DTProvider **provp, *prov;
	DTProbe **pp, *p;
	int unique, uniqueprov;
	
	l = nil;
	nl = 0;
	unique = patunique(name, 0);
	uniqueprov = patunique(name, 1);
	for(provp = dtproviders; prov = *provp, prov != nil; provp++){
		if(!dtnamematch(name, prov->name, 1))
			continue;
		if(!prov->provided){
			prov->provided = 1;
			prov->provide(prov);
		}
		for(pp = &prov->probes; p = *pp, p != nil; pp = &p->provnext){
			if(dtnamematch(name, p->name, 0)){
				if(ret != nil){
					l = dtrealloc(l, (nl + 1) * sizeof(DTProbe *));
					l[nl] = p;
				}
				nl++;
				if(unique) goto out;
			}
		}
		if(uniqueprov) goto out;
	}
out:
	if(ret != nil)
		*ret = l;
	return nl;
}

int
dtplist(DTProbe ***ret)
{
	DTProbe **l;
	int nl;
	DTProvider **provp, *prov;
	DTProbe **pp, *p;
	
	l = nil;
	nl = 0;
	for(provp = dtproviders; prov = *provp, prov != nil; provp++){
		if(!prov->provided){
			prov->provided = 1;
			prov->provide(prov);
		}
		for(pp = &prov->probes; p = *pp, p != nil; pp = &p->provnext){
			if(ret != nil){
				l = dtrealloc(l, (nl + 1) * sizeof(DTProbe *));
				l[nl] = p;
			}
			nl++;
		}
	}
	if(ret != nil)
		*ret = l;
	else
		dtfree(l);
	return nl;
}
