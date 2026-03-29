#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/netif.h"
#include "../port/etherif.h"

#include "ethermii.h"

/* hook for devmii */
static void dummy(Mii*){}
void (*addmiibus)(Mii*) = dummy;
void (*delmiibus)(Mii*) = dummy;

static MiiPhy*
newphy(Mii *mii, int phyno)
{
	MiiPhy *phy;

	phy = malloc(sizeof(MiiPhy));
	if(phy == nil)
		return nil;

	phy->mii = mii;
	phy->phyno = phyno;

	phy->id = 0;
	phy->oui = 0;

	phy->anar = ~0;
	phy->fc = ~0;
	phy->mscr = ~0;

	phy->pagereg = nil;

	mii->phy[phyno] = phy;
	if(mii->curphy == nil)
		mii->curphy = phy;

	mii->mask |= 1<<phyno;
	mii->nphy++;
	
	return phy;
}

uint
mii(Mii* mii, uint mask)
{
	MiiPhy *phy;
	int oui, phyno;
	uint bit, rmask, id;

	qlock(mii);
	if(up != nil && waserror()){
		qunlock(mii);
		nexterror();
	}

	/*
	 * Probe through mii for PHYs in mask;
	 * return the mask of those found in the current probe.
	 * If the PHY has not already been probed, update
	 * the Mii information.
	 */
	rmask = 0;
	for(phyno = 0; phyno < NMiiPhy; phyno++){
		bit = 1U<<phyno;
		if(!(mask & bit))
			continue;
		if(mii->mask & bit){
			rmask |= bit;
			continue;
		}
		if((*mii->mir)(mii, phyno, Bmsr) == -1)
			continue;
		id = (*mii->mir)(mii, phyno, Phyidr1) << 16;
		id |= (*mii->mir)(mii, phyno, Phyidr2);
		oui = (id & 0x3FFFFC00)>>10;
		if(oui == 0xFFFFF || oui == 0)
			continue;

		phy = newphy(mii, phyno);
		if(phy == nil)
			continue;

		phy->id = id;
		phy->oui = oui;

		rmask |= bit;
	}

	qunlock(mii);
	if(up != nil) poperror();

	return rmask;
}

MiiPhy*
miiphy(Mii *mii, int phyno)
{
	MiiPhy *phy;

	assert((uint)phyno < NMiiPhy);

	qlock(mii);
	if((phy = mii->phy[phyno]) == nil)
		phy = newphy(mii, phyno);
	qunlock(mii);

	return phy;
}

static int
setpage(MiiPhy *phy, int reg)
{
	Mii *mii;
	int page, preg;

	if(phy->pagereg == nil)
		return 0;

	page = (reg >> 8) & 0xFFFF;
	reg &= 0x1F;
	preg = (*phy->pagereg)(&page, reg);
	if(preg < 0 || page < 0 || preg == reg)
		return 0;

	mii = phy->mii;
	return (*mii->miw)(mii, phy->phyno, preg & 0x1F, page & 0xFFFF);
}

int
miimir(MiiPhy *phy, int reg)
{
	Mii *mii;
	int ret;

	if(phy == nil || (mii = phy->mii) == nil)
		return -1;

	qlock(mii);
	if(up != nil && waserror()){
		qunlock(mii);
		nexterror();
	}
	if((ret = setpage(phy, reg)) >= 0)
		ret = (*mii->mir)(mii, phy->phyno, reg & 0x1F);
	qunlock(mii);
	if(up != nil) poperror();
	return ret;
}

int
miimiw(MiiPhy *phy, int reg, int data)
{
	Mii *mii;
	int ret;

	if(phy == nil || (mii = phy->mii) == nil)
		return -1;
	qlock(mii);
	if(up != nil && waserror()){
		qunlock(mii);
		nexterror();
	}
	if((ret = setpage(phy, reg)) >= 0)
		ret = (*mii->miw)(mii, phy->phyno, reg & 0x1F, data & 0xFFFF);
	qunlock(mii);
	if(up != nil) poperror();
	return ret;
}

int
miireset(MiiPhy *phy)
{
	int bmcr;

	bmcr = miimir(phy, Bmcr);
	if(bmcr == -1)
		return -1;
	bmcr |= BmcrR;
	miimiw(phy, Bmcr, bmcr);
	microdelay(1);

	return 0;
}

int
miiane(MiiPhy *phy, int a, int p, int e)
{
	int anar, bmsr, mscr, r;

	bmsr = miimir(phy, Bmsr);
	if(bmsr == -1)
		return -1;
	if(!(bmsr & BmsrAna))
		return -1;

	if(a != ~0)
		anar = (AnaTXFD|AnaTXHD|Ana10FD|Ana10HD) & a;
	else if(phy->anar != ~0)
		anar = phy->anar;
	else{
		anar = miimir(phy, Anar);
		if(anar == -1)
			return -1;
		anar &= ~(AnaAP|AnaP|AnaT4|AnaTXFD|AnaTXHD|Ana10FD|Ana10HD);
		if(bmsr & Bmsr10THD)
			anar |= Ana10HD;
		if(bmsr & Bmsr10TFD)
			anar |= Ana10FD;
		if(bmsr & Bmsr100TXHD)
			anar |= AnaTXHD;
		if(bmsr & Bmsr100TXFD)
			anar |= AnaTXFD;
	}
	phy->anar = anar;

	if(p != ~0)
		anar |= (AnaAP|AnaP) & p;
	else if(phy->fc != ~0)
		anar |= phy->fc;
	phy->fc = (AnaAP|AnaP) & anar;

	if(bmsr & BmsrEs){
		mscr = miimir(phy, Mscr);
		if(mscr == -1)
			return -1;
		mscr &= ~(Mscr1000TFD|Mscr1000THD);
		if(e != ~0)
			mscr |= (Mscr1000TFD|Mscr1000THD) & e;
		else if(phy->mscr != ~0)
			mscr = phy->mscr;
		else{
			r = miimir(phy, Esr);
			if(r == -1)
				return -1;
			if(r & Esr1000THD)
				mscr |= Mscr1000THD;
			if(r & Esr1000TFD)
				mscr |= Mscr1000TFD;
		}
		phy->mscr = mscr;
		miimiw(phy, Mscr, mscr);
	}
	if(miimiw(phy, Anar, anar) == -1)
		return -1;

	r = miimir(phy, Bmcr);
	if(r == -1)
		return -1;
	if(!(r & BmcrR)){
		r |= BmcrAne|BmcrRan;
		miimiw(phy, Bmcr, r);
	}

	return 0;
}

int
miianec45(MiiPhy* phy, int m)
{
	int r;

	phy->mgcr = m;
	phy->mgcr &= (MMDanmgcr2500T|MMDanmgcr5000T|MMDanmgcr10000T);

	r = miimmdr(phy, MMDan, MMDanmgcr);
	if (r == -1)
		return -1;

	r &= ~phy->mgcr;
	r |= phy->mgcr;
	if (miimmdw(phy, MMDan, MMDanmgcr, r) == -1)
		return -1;

	return 0;
}

int
miistatus(MiiPhy* phy)
{
	int anlpar, bmsr, p, r;
	
	/*
	 * Check Auto-Negotiation is complete and link is up.
	 * (Read status twice as the Ls bit is sticky).
	 */
	bmsr = miimir(phy, Bmsr);
	if(bmsr == -1)
		return -1;
	if(!(bmsr & (BmsrAnc|BmsrAna))) {
		// print("miistatus: auto-neg incomplete\n");
		return -1;
	}

	bmsr = miimir(phy, Bmsr);
	if(bmsr == -1)
		return -1;
	if(!(bmsr & BmsrLs)){
		// print("miistatus: link down\n");
		phy->link = 0;
		return -1;
	}

	phy->speed = phy->fd = phy->rfc = phy->tfc = 0;
	if(phy->mscr){
		r = miimir(phy, Mssr);
		if(r == -1)
			return -1;
		if((phy->mscr & Mscr1000TFD) && (r & Mssr1000TFD)){
			phy->speed = 1000;
			phy->fd = 1;
		}
		else if((phy->mscr & Mscr1000THD) && (r & Mssr1000THD))
			phy->speed = 1000;
	}

	anlpar = miimir(phy, Anlpar);
	if(anlpar == -1)
		return -1;
	if(phy->speed == 0){
		r = phy->anar & anlpar;
		if(r & AnaTXFD){
			phy->speed = 100;
			phy->fd = 1;
		}
		else if(r & AnaTXHD)
			phy->speed = 100;
		else if(r & Ana10FD){
			phy->speed = 10;
			phy->fd = 1;
		}
		else if(r & Ana10HD)
			phy->speed = 10;
	}
	if(phy->speed == 0) {
		// print("miistatus: phy speed 0\n");
		return -1;
	}

	if(phy->fd){
		p = phy->fc;
		r = anlpar & (AnaAP|AnaP);
		if(p == AnaAP && r == (AnaAP|AnaP))
			phy->tfc = 1;
		else if(p == (AnaAP|AnaP) && r == AnaAP)
			phy->rfc = 1;
		else if((p & AnaP) && (r & AnaP))
			phy->rfc = phy->tfc = 1;
	}

	phy->link = 1;
	return 0;
}

int
miistatusc45(MiiPhy *phy)
{
	int r;

	r = miimmdr(phy, MMDan, MMDanmgsr);
	if (r == -1)
		return -1;

	if (r & MMDanmgsr2500T) {
		phy->speed = 2500;
		phy->fd = 1;
	}

	if (r & MMDanmgsr5000T) {
		phy->speed = 5000;
		phy->fd = 1;
	}

	if (r & MMDanmgsr10000T) {
		phy->speed = 10000;
		phy->fd = 1;
	}

	return 0;
}

int
miimmdr(MiiPhy *phy, int a, int r)
{
	Mii *mii;
	int ret;

	if(phy == nil || (mii = phy->mii) == nil)
		return -1;
	qlock(mii);
	if(up != nil && waserror()){
		qunlock(mii);
		nexterror();
	}
	a &= 0x1F;
	if((ret = setpage(phy, Mmdctrl)) < 0)
		goto out;
	if((ret = (*mii->miw)(mii, phy->phyno, Mmdctrl, a)) == -1)
		goto out;
	if((ret = (*mii->miw)(mii, phy->phyno, Mmddata, r & 0xFFFF)) == -1)
		goto out;
	if((ret = (*mii->miw)(mii, phy->phyno, Mmdctrl, a | 0x4000)) == -1)
		goto out;
	ret = (*mii->mir)(mii, phy->phyno, Mmddata);
out:
	qunlock(mii);
	if(up != nil) poperror();
	return ret;
}

int
miimmdw(MiiPhy *phy, int a, int r, int data)
{
	Mii *mii;
	int ret;

	if(phy == nil || (mii = phy->mii) == nil)
		return -1;
	qlock(mii);
	if(up != nil && waserror()){
		qunlock(mii);
		nexterror();
	}
	a &= 0x1F;
	if((ret = setpage(phy, Mmdctrl)) < 0)
		goto out;
	if((ret = (*mii->miw)(mii, phy->phyno, Mmdctrl, a)) == -1)
		goto out;
	if((ret = (*mii->miw)(mii, phy->phyno, Mmddata, r & 0xFFFF)) == -1)
		goto out;
	if((ret = (*mii->miw)(mii, phy->phyno, Mmdctrl, a | 0x4000)) == -1)
		goto out;
	ret = (*mii->miw)(mii, phy->phyno, Mmddata, data & 0xFFFF);
out:
	qunlock(mii);
	if(up != nil) poperror();
	return ret;
}
