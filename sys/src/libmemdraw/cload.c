#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>

int
cloadmemimage(Memimage *i, Rectangle r, uchar *data, int ndata)
{
	int y, bpl, c, cnt, offs, r;
	uchar mem[NMEM], *memp, *omemp, *emem, *linep, *elinep, *u, *eu;

	if(badrect(r) || !rectinrect(r, i->r)){
		werrstr("cloadmemimage: bad rectangle");
		return -1;
	}

	/* avoid sw cursor */
	static Memdrawparam par;
	par.dst = i;
	par.r = r;
	hwdraw(&par);

	r = -1;
	bpl = bytesperline(r, i->depth);
	u = data;
	eu = data+ndata;
	memset(mem, 0, sizeof mem);
	memp = mem;
	emem = mem+NMEM;
	y = r.min.y;
	linep = byteaddr(i, Pt(r.min.x, y));
	elinep = linep+bpl;
	for(;;){
		if(linep == elinep){
			if(++y == r.max.y)
				break;
			linep = byteaddr(i, Pt(r.min.x, y));
			elinep = linep+bpl;
		}
		if(u == eu){
			werrstr("cloadmemimage: buffer too small");
			return -1;
		}
		c = *u++;
		if(c >= 128){
			for(cnt=c-128+1; cnt!=0 ;--cnt){
				if(u == eu){
					werrstr("cloadmemimage: buffer too small");
					return -1;
				}
				if(linep == elinep){
					werrstr("cloadmemimage: phase error");
					return -1;
				}
				*linep++ = *u;
				*memp++ = *u++;
				if(memp == emem)
					memp = mem;
			}
		}
		else{
			if(u == eu){
				werrstr("cloadmemimage: short buffer");
				return -1;
			}
			offs = *u++ + ((c&3)<<8)+1;
			if(memp-mem < offs)
				omemp = memp+(NMEM-offs);
			else
				omemp = memp-offs;
			for(cnt=(c>>2)+NMATCH; cnt!=0; --cnt){
				if(linep == elinep){
					werrstr("cloadmemimage: phase error");
					return -1;
				}
				*linep++ = *omemp;
				*memp++ = *omemp++;
				if(omemp == emem)
					omemp = mem;
				if(memp == emem)
					memp = mem;
			}
		}
	}
	r = u-data;
Out:
	memset(memp, 0, emem-memp);
	return r;
}
