#include <u.h>
#include <libc.h>
#include <ip.h>
#include "dns.h"

static RR*	doextquery(DNSmsg*, Request*, int);
static void	hint(RR**, RR*);

/*
 *  answer a dns request
 */
void
dnserver(DNSmsg *reqp, DNSmsg *repp, Request *req, uchar *srcip, int rcode)
{
	char tname[32], *cp;
	DN *nsdp;
	Area *myarea;
	RR *rp, *neg;

	repp->id = reqp->id;
	repp->flags = Fresp | (reqp->flags & Omask);
	if(!cfg.nonrecursive && (reqp->flags & Omask) == Oquery)
		repp->flags |= Fcanrec;
	setercode(repp, Rok);

	/* move one question from reqp to repp */
	rp = reqp->qd;
	reqp->qd = rp->next;
	rp->next = nil;
	repp->qd = rp;

	if(rcode){
		dnslog("%d: server: response code %d %s, req from %I",
			req->id, rcode, rcname(rcode), srcip);
		/* provide feedback to clients who send us trash */
		setercode(repp, rcode);
		return;
	}
	if(repp->qd->type == Topt || !rrsupported(repp->qd->type)){
		if(debug)
			dnslog("%d: server: unsupported request %s from %I",
				req->id, rrname(repp->qd->type, tname, sizeof tname), srcip);
		setercode(repp, Runimplimented);
		return;
	}

	if(repp->qd->owner->class != Cin){
		if(debug)
			dnslog("%d: server: unsupported class %d from %I",
				req->id, repp->qd->owner->class, srcip);
		setercode(repp, Runimplimented);
		return;
	}

	myarea = inmyarea(repp->qd->owner->name);
	if(myarea){
		if(repp->qd->type == Tixfr || repp->qd->type == Taxfr){
			if(debug)
				dnslog("%d: server: unsupported xfr request %s for %s from %I",
					req->id, rrname(repp->qd->type, tname, sizeof tname),
					repp->qd->owner->name, srcip);
			setercode(repp, Runimplimented);
			return;
		}
	}
	if(myarea == nil && cfg.nonrecursive) {
		/* we don't recurse and we're not authoritative */
		repp->flags &= ~(Fauth|Fcanrec);
		neg = nil;
	} else {
		int recurse = (reqp->flags & Frecurse) && (repp->flags & Fcanrec);

		/*
		 *  get the answer if we can, in *repp
		 */
		neg = doextquery(repp, req, recurse? Recurse: Dontrecurse);

		/* authority is transitive */
		if(myarea || (repp->an && repp->an->auth))
			repp->flags |= Fauth;

		/* pass on error codes */
		if(recurse && repp->an == nil && repp->qd->owner->rr == nil){
			repp->flags |= Fauth;
			setercode(repp, repp->qd->owner->respcode);
		}
	}

	if(myarea == nil){
		/*
		 *  add name server if we know
		 */
		for(cp = repp->qd->owner->name; cp; cp = walkup(cp)){
			nsdp = dnlookup(cp, repp->qd->owner->class, 0);
			if(nsdp == nil)
				continue;

			repp->ns = rrlookup(nsdp, Tns, OKneg);
			if(repp->ns){
				/* don't pass on anything we know is wrong */
				if(repp->ns->negative){
					rp = repp->ns;
					repp->ns = nil;
					rrfreelist(rp);
				}
				break;
			}

			repp->ns = dblookup(cp, repp->qd->owner->class, Tns, 0, 0);
			if(repp->ns)
				break;
		}
	}

	/*
	 *  add ip addresses as hints
	 */
	if(repp->qd->type != Taxfr && repp->qd->type != Tixfr){
		for(rp = repp->ns; rp; rp = rp->next)
			hint(&repp->ar, rp);
		for(rp = repp->an; rp; rp = rp->next)
			hint(&repp->ar, rp);
	}

	/*
	 *  add an soa to the authority section to help client
	 *  with negative caching
	 */
	if(repp->an == nil){
		if(myarea){
			rrcopy(myarea->soarr, &rp);
			rrcat(&repp->ns, rp);
		} else if(neg != nil) {
			if(neg->negsoaowner != nil) {
				rp = rrlookup(neg->negsoaowner, Tsoa, NOneg);
				rrcat(&repp->ns, rp);
			}
			setercode(repp, neg->negrcode);
		}
	}

	/*
	 *  get rid of duplicates
	 */
	unique(repp->an);
	unique(repp->ns);
	unique(repp->ar);

	rrfreelist(neg);
}

/*
 *  satisfy a recursive request.  dnlookup will handle cnames.
 */
static RR*
doextquery(DNSmsg *mp, Request *req, int recurse)
{
	ushort type;
	char *name;
	RR *rp, *neg;

	name = mp->qd->owner->name;
	type = mp->qd->type;
	rp = dnresolve(name, Cin, type, req, &mp->an, 0, recurse, 1, nil);

	/* don't return soa hints as answers, it's wrong */
	if(rp && rp->db && !rp->auth && rp->type == Tsoa) {
		rrfreelist(rp);
		rp = nil;
	}

	/* don't let negative cached entries escape */
	neg = rrremneg(&rp);
	rrcat(&mp->an, rp);

	return neg;
}

static void
hint(RR **last, RR *rp)
{
	RR *hp;

	switch(rp->type){
	case Tns:
	case Tmx:
	case Tmb:
	case Tmf:
	case Tmd:
		hp = rrlookup(rp->host, Ta, NOneg);
		if(hp == nil)
			hp = dblookup(rp->host->name, Cin, Ta, 0, 0);
		if(hp == nil)
			hp = rrlookup(rp->host, Taaaa, NOneg);
		if(hp == nil)
			hp = dblookup(rp->host->name, Cin, Taaaa, 0, 0);
		if (hp && strncmp(hp->owner->name, "local#", 6) == 0)
			dnslog("returning %s as hint", hp->owner->name);
		rrcat(last, hp);
		break;
	}
}
