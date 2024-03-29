#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"

#include "../port/sd.h"

static int
scsitest(SDreq* r)
{
	r->write = 0;
	memset(r->cmd, 0, sizeof(r->cmd));
	r->cmd[1] = r->lun<<5;
	r->clen = 6;
	r->data = nil;
	r->dlen = 0;
	r->flags = 0;

	r->status = ~0;

	return r->unit->dev->ifc->rio(r);
}

int
scsiverify(SDunit* unit)
{
	SDreq *r;
	int i, status;
	uchar *inquiry;

	if((r = malloc(sizeof(SDreq))) == nil)
		return 0;
	if((inquiry = sdmalloc(sizeof(unit->inquiry))) == nil){
		free(r);
		return 0;
	}
	r->unit = unit;
	r->lun = 0;		/* ??? */

	memset(unit->inquiry, 0, sizeof(unit->inquiry));
	r->write = 0;
	r->cmd[0] = 0x12;
	r->cmd[1] = r->lun<<5;
	r->cmd[4] = 36;
	r->clen = 6;
	r->data = inquiry;
	r->dlen = 36;
	r->flags = 0;

	r->status = ~0;
	if(unit->dev->ifc->rio(r) != SDok){
		free(r);
		return 0;
	}
	memmove(unit->inquiry, inquiry, r->dlen);
	free(inquiry);

	SET(status);
	for(i = 0; i < 3; i++){
		while((status = scsitest(r)) == SDbusy)
			;
		if(status == SDok || status != SDcheck)
			break;
		if(!(r->flags & SDvalidsense))
			break;
		if((r->sense[2] & 0x0F) != 0x02)
			continue;

		/*
		 * Unit is 'not ready'.
		 * If it is in the process of becoming ready or needs
		 * an initialising command, set status so it will be spun-up
		 * below.
		 * If there's no medium, that's OK too, but don't
		 * try to spin it up.
		 */
		if(r->sense[12] == 0x04){
			if(r->sense[13] == 0x02 || r->sense[13] == 0x01){
				status = SDok;
				break;
			}
		}
		if(r->sense[12] == 0x3A)
			break;
	}

	if(status == SDok){
		/*
		 * Try to ensure a direct-access device is spinning.
		 * Don't wait for completion, ignore the result.
		 */
		if((unit->inquiry[0] & 0x1F) == 0){
			memset(r->cmd, 0, sizeof(r->cmd));
			r->write = 0;
			r->cmd[0] = 0x1B;
			r->cmd[1] = (r->lun<<5)|0x01;
			r->cmd[4] = 1;
			r->clen = 6;
			r->data = nil;
			r->dlen = 0;
			r->flags = 0;

			r->status = ~0;
			unit->dev->ifc->rio(r);
		}
	}
	free(r);

	if(status == SDok || status == SDcheck)
		return 1;
	return 0;
}

static int
scsirio(SDreq* r)
{
	/*
	 * Perform an I/O request, returning
	 *	-1	failure
	 *	 0	ok
	 *	 1	no medium present
	 *	 2	retry
	 * The contents of r may be altered so the
	 * caller should re-initialise if necesary.
	 */
	r->status = ~0;
	switch(r->unit->dev->ifc->rio(r)){
	default:
		break;
	case SDcheck:
		if(!(r->flags & SDvalidsense))
			break;
		switch(r->sense[2] & 0x0F){
		case 0x00:		/* no sense */
		case 0x01:		/* recovered error */
			return 2;
		case 0x06:		/* check condition */
			/*
			 * 0x28 - not ready to ready transition,
			 *	  medium may have changed.
			 * 0x29 - power on or some type of reset.
			 */
			if(r->sense[12] == 0x28 && r->sense[13] == 0)
				return 2;
			if(r->sense[12] == 0x29)
				return 2;
			break;
		case 0x02:		/* not ready */
			/*
			 * If no medium present, bail out.
			 * If unit is becoming ready, rather than not
			 * not ready, wait a little then poke it again.
			 */
			if(r->sense[12] == 0x3A)
				break;
			if(r->sense[12] != 0x04 || r->sense[13] != 0x01)
				break;

			while(waserror())
				;
			tsleep(&up->sleep, return0, 0, 500);
			poperror();
			scsitest(r);
			return 2;
		default:
			break;
		}
		break;
	case SDok:
		return 0;
	}
	return -1;
}

static void
cap10(SDreq *r)
{
	r->cmd[0] = 0x25;
	r->cmd[1] = r->lun<<5;
	r->clen = 10;
	r->dlen = 8;
}

static void
cap16(SDreq *r)
{
	uint i;

	i = 32;
	r->cmd[0] = 0x9e;
	r->cmd[1] = 0x10;
	r->cmd[10] = i>>24;
	r->cmd[11] = i>>16;
	r->cmd[12] = i>>8;
	r->cmd[13] = i;
	r->clen = 16;
	r->dlen = i;
}

static uint
belong(uchar *u)
{
	return u[0]<<24 | u[1]<<16 | u[2]<<8 | u[3];
}

static uvlong
capreply(SDreq *r, ulong *secsize)
{
	uchar *u;
	ulong ss;
	uvlong s;

	u = r->data;
	if(r->clen == 16){
		s = (uvlong)belong(u)<<32 | belong(u + 4);
		ss = belong(u + 8);
	}else{
		s = belong(u);
		ss = belong(u + 4);
	}
	if(secsize)
		*secsize = ss;
	return s;
}

int
scsionline(SDunit* unit)
{
	SDreq *r;
	uchar *p;
	ulong ss;
	uvlong s;
	int ok, retries;
	void (*cap)(SDreq*);

	if((r = malloc(sizeof *r)) == nil)
		return 0;
	if((p = sdmalloc(32)) == nil){
		free(r);
		return 0;
	}

	ok = 0;
	cap = cap10;
	r->unit = unit;
	r->lun = 0;				/* ??? */
	for(retries = 0; retries < 10; retries++){
		/*
		 * Read-capacity is mandatory for DA, WORM, CD-ROM and
		 * MO. It may return 'not ready' if type DA is not
		 * spun up, type MO or type CD-ROM are not loaded or just
		 * plain slow getting their act together after a reset.
		 */
		r->write = 0;
		r->data = p;
		r->flags = 0;
		memset(r->cmd, 0, sizeof r->cmd);
		cap(r);

		switch(scsirio(r)){
		default:
			/*
			 * ATAPI returns error and no sense information
			 * on media change / no media present.
			 * count as retries.
			 */
			if(retries < 4)
				continue;
			break;
		case 0:
			s = capreply(r, &ss);
			if(s == 0xffffffff && cap == cap10){
				cap = cap16;
				continue;
			}
			if(s == 0xffffffffffffffffLL)
				s = 0;

			/*
			 * Some ATAPI CD readers lie about the block size.
			 * Since we don't read audio via this interface
			 * it's okay to always fudge this.
			 */
			if(ss == 2352)
				ss = 2048;

			/*
			 * Devices with removable media may return 0 sectors
			 * when they have empty media (e.g. sata dvd writers);
			 * if so, keep the count zero.
			 *	
			 * Read-capacity returns the LBA of the last sector,
			 * therefore the number of sectors must be incremented.
			 */
			if(s != 0)
				s++;

			ok = (unit->sectors != s) ? 2 : 1;
			unit->sectors = s;
			unit->secsize = ss;
			break;
		case 1:
			ok = (unit->sectors != 0) ? 2 : 1;
			unit->sectors = 0;
			break;
		case 2:
			continue;
		}
		break;
	}
	free(p);
	free(r);

	/*
	print("scsionline: %s: ok=%d retries=%d sectors=%llud secsize=%lud\n",
		unit->name, ok, retries, unit->sectors, unit->secsize);
	*/

	if(ok)
		return ok+retries;
	else
		return 0;
}

static void
scsifmt10(SDreq *r, int write, int lun, ulong nb, uvlong bno)
{
	uchar *c;

	c = r->cmd;
	if(write == 0)
		c[0] = 0x28;
	else
		c[0] = 0x2A;
	c[1] = lun<<5;
	c[2] = bno>>24;
	c[3] = bno>>16;
	c[4] = bno>>8;
	c[5] = bno;
	c[6] = 0;
	c[7] = nb>>8;
	c[8] = nb;
	c[9] = 0;

	r->clen = 10;
}

static void
scsifmt16(SDreq *r, int write, int lun, ulong nb, uvlong bno)
{
	uchar *c;

	c = r->cmd;
	if(write == 0)
		c[0] = 0x88;
	else
		c[0] = 0x8A;
	c[1] = lun<<5;		/* so wrong */
	c[2] = bno>>56;
	c[3] = bno>>48;
	c[4] = bno>>40;
	c[5] = bno>>32;
	c[6] = bno>>24;
	c[7] = bno>>16;
	c[8] = bno>>8;
	c[9] = bno;
	c[10] = nb>>24;
	c[11] = nb>>16;
	c[12] = nb>>8;
	c[13] = nb;
	c[14] = 0;
	c[15] = 0;

	r->clen = 16;
}

long
scsibio(SDunit* unit, int lun, int write, void* data, long nb, uvlong bno)
{
	SDreq *r;
	long rlen;

	r = smalloc(sizeof(SDreq));
	r->unit = unit;
	r->lun = lun;
again:
	r->write = write;
	if(bno > 0xffffffff)
		scsifmt16(r, write, lun, nb, bno);
	else
		scsifmt10(r, write, lun, nb, bno);
	r->data = data;
	r->dlen = nb*unit->secsize;
	r->flags = 0;

	r->status = ~0;
	switch(scsirio(r)){
	default:
		rlen = -1;
		break;
	case 0:
		/*
		 * scsi allows commands to return successfully
		 * but return sense data, indicating that the
		 * operation didn't proceed as expected.
		 * (confusing, no).  this allows the raw commands
		 * to successfully return errors.  but any sense
		 * data bio sees must be an error.  bomb out.
		 */
		if(r->status == SDok && r->rlen > 0
		&& ((r->flags & SDvalidsense) == 0 || r->sense[2] == 0)){
			rlen = r->rlen;
			break;
		}
	case 2:
		rlen = -1;
		if(!(r->flags & SDvalidsense))
			break;
		switch(r->sense[2] & 0x0F){
		default:
			break;
		case 0x01:		/* recovered error */
			print("%s: recovered error at sector %llud\n",
				unit->name, bno);
			break;
		case 0x06:		/* check condition */
			/*
			 * Check for a removeable media change.
			 * If so, mark it by zapping the geometry info
			 * to force an online request.
			 */
			if(r->sense[12] != 0x28 || r->sense[13] != 0)
				break;
			if(unit->inquiry[1] & 0x80)
				unit->sectors = 0;
			break;
		case 0x02:		/* not ready */
			/*
			 * If unit is becoming ready,
			 * rather than not not ready, try again.
			 */
			if(r->sense[12] == 0x04 && r->sense[13] == 0x01)
				goto again;
			break;
		}
		snprint(up->genbuf, sizeof up->genbuf, "%s %.2ux%.2ux%.2ux %lld",
			Eio, r->sense[2], r->sense[12], r->sense[13], bno);
		free(r);
		error(up->genbuf);
		break;
	}
	free(r);

	return rlen;
}

