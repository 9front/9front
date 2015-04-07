#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

#define	Image	IMAGE
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include "screen.h"

/*
 * Register constant below from XFree86 driver.
 */ 

#define DM1_PLANES         0x00000007
#define    DM1_NOPLANES    0x00000000
#define    DM1_RGBPLANES   0x00000001
#define    DM1_RGBAPLANES  0x00000002
#define    DM1_OLAYPLANES  0x00000004
#define    DM1_PUPPLANES   0x00000005
#define    DM1_CIDPLANES   0x00000006
	
#define NPORT_DMODE1_DDMASK      0x00000018
#define NPORT_DMODE1_DD4         0x00000000
#define NPORT_DMODE1_DD8         0x00000008
#define NPORT_DMODE1_DD12        0x00000010
#define NPORT_DMODE1_DD24        0x00000018
#define NPORT_DMODE1_DSRC        0x00000020
#define NPORT_DMODE1_YFLIP       0x00000040
#define NPORT_DMODE1_RWPCKD      0x00000080
#define NPORT_DMODE1_HDMASK      0x00000300
#define NPORT_DMODE1_HD4         0x00000000
#define NPORT_DMODE1_HD8         0x00000100
#define NPORT_DMODE1_HD12        0x00000200
#define NPORT_DMODE1_HD32        0x00000300
#define NPORT_DMODE1_RWDBL       0x00000400
#define NPORT_DMODE1_ESWAP       0x00000800 /* Endian swap */
#define NPORT_DMODE1_CCMASK      0x00007000
#define NPORT_DMODE1_CCLT        0x00001000
#define NPORT_DMODE1_CCEQ        0x00002000
#define NPORT_DMODE1_CCGT        0x00004000
#define NPORT_DMODE1_RGBMD       0x00008000
#define NPORT_DMODE1_DENAB       0x00010000 /* Dither enable */
#define NPORT_DMODE1_FCLR        0x00020000 /* Fast clear */
#define NPORT_DMODE1_BENAB       0x00040000 /* Blend enable */
#define NPORT_DMODE1_SFMASK      0x00380000
#define NPORT_DMODE1_SF0         0x00000000
#define NPORT_DMODE1_SF1         0x00080000
#define NPORT_DMODE1_SFDC        0x00100000
#define NPORT_DMODE1_SFMDC       0x00180000
#define NPORT_DMODE1_SFSA        0x00200000
#define NPORT_DMODE1_SFMSA       0x00280000
#define NPORT_DMODE1_DFMASK      0x01c00000
#define NPORT_DMODE1_DF0         0x00000000
#define NPORT_DMODE1_DF1         0x00400000
#define NPORT_DMODE1_DFSC        0x00800000
#define NPORT_DMODE1_DFMSC       0x00c00000
#define NPORT_DMODE1_DFSA        0x01000000
#define NPORT_DMODE1_DFMSA       0x01400000
#define NPORT_DMODE1_BBENAB      0x02000000 /* Back blend enable */
#define NPORT_DMODE1_PFENAB      0x04000000 /* Pre-fetch enable */
#define NPORT_DMODE1_ABLEND      0x08000000 /* Alpha blend */
#define NPORT_DMODE1_LOMASK      0xf0000000
#define NPORT_DMODE1_LOZERO      0x00000000
#define NPORT_DMODE1_LOAND       0x10000000
#define NPORT_DMODE1_LOANDR      0x20000000
#define NPORT_DMODE1_LOSRC       0x30000000
#define NPORT_DMODE1_LOANDI      0x40000000
#define NPORT_DMODE1_LODST       0x50000000
#define NPORT_DMODE1_LOXOR       0x60000000
#define NPORT_DMODE1_LOOR        0x70000000
#define NPORT_DMODE1_LONOR       0x80000000
#define NPORT_DMODE1_LOXNOR      0x90000000
#define NPORT_DMODE1_LONDST      0xa0000000
#define NPORT_DMODE1_LOORR       0xb0000000
#define NPORT_DMODE1_LONSRC      0xc0000000
#define NPORT_DMODE1_LOORI       0xd0000000
#define NPORT_DMODE1_LONAND      0xe0000000
#define NPORT_DMODE1_LOONE       0xf0000000

	/* These bits define the graphics opcode being performed. */
#define NPORT_DMODE0_OPMASK   0x00000003 /* Opcode mask */
#define NPORT_DMODE0_NOP      0x00000000 /* No operation */
#define NPORT_DMODE0_RD       0x00000001 /* Read operation */
#define NPORT_DMODE0_DRAW     0x00000002 /* Draw operation */
#define NPORT_DMODE0_S2S      0x00000003 /* Screen to screen operation */

	/* The following decide what addressing mode(s) are to be used */
#define NPORT_DMODE0_AMMASK   0x0000001c /* Address mode mask */
#define NPORT_DMODE0_SPAN     0x00000000 /* Spanning address mode */
#define NPORT_DMODE0_BLOCK    0x00000004 /* Block address mode */
#define NPORT_DMODE0_ILINE    0x00000008 /* Iline address mode */
#define NPORT_DMODE0_FLINE    0x0000000c /* Fline address mode */
#define NPORT_DMODE0_ALINE    0x00000010 /* Aline address mode */
#define NPORT_DMODE0_TLINE    0x00000014 /* Tline address mode */
#define NPORT_DMODE0_BLINE    0x00000018 /* Bline address mode */

	/* And now some misc. operation control bits. */
#define NPORT_DMODE0_DOSETUP  0x00000020
#define NPORT_DMODE0_CHOST    0x00000040
#define NPORT_DMODE0_AHOST    0x00000080
#define NPORT_DMODE0_STOPX    0x00000100
#define NPORT_DMODE0_STOPY    0x00000200
#define NPORT_DMODE0_SK1ST    0x00000400
#define NPORT_DMODE0_SKLST    0x00000800
#define NPORT_DMODE0_ZPENAB   0x00001000
#define NPORT_DMODE0_LISPENAB 0x00002000
#define NPORT_DMODE0_LISLST   0x00004000
#define NPORT_DMODE0_L32      0x00008000
#define NPORT_DMODE0_ZOPQ     0x00010000
#define NPORT_DMODE0_LISOPQ   0x00020000
#define NPORT_DMODE0_SHADE    0x00040000
#define NPORT_DMODE0_LRONLY   0x00080000
#define NPORT_DMODE0_XYOFF    0x00100000
#define NPORT_DMODE0_CLAMP    0x00200000
#define NPORT_DMODE0_ENDPF    0x00400000
#define NPORT_DMODE0_YSTR     0x00800000

#define NPORT_LSMODE_REPMASK  0x0000ff00
#define NPORT_LSMODE_LENMASK  0x0f000000

#define NPORT_DMODE_WMASK   0x00000003	/* dataWidth of data being transfered */
#define NPORT_DMODE_W4      0x00000000
#define NPORT_DMODE_W1      0x00000001
#define NPORT_DMODE_W2      0x00000002
#define NPORT_DMODE_W3      0x00000003
#define NPORT_DMODE_EDPACK  0x00000004
#define NPORT_DMODE_ECINC   0x00000008
#define NPORT_DMODE_CMASK   0x00000070
#define NPORT_DMODE_AMASK   0x00000780
#define NPORT_DMODE_AVC2    0x00000000
#define NPORT_DMODE_ACMALL  0x00000080
#define NPORT_DMODE_ACM0    0x00000100
#define NPORT_DMODE_ACM1    0x00000180
#define NPORT_DMODE_AXMALL  0x00000200
#define NPORT_DMODE_AXM0    0x00000280
#define NPORT_DMODE_AXM1    0x00000300
#define NPORT_DMODE_ABT     0x00000380
#define NPORT_DMODE_AVCC1   0x00000400
#define NPORT_DMODE_AVAB1   0x00000480
#define NPORT_DMODE_ALG3V0  0x00000500
#define NPORT_DMODE_A1562   0x00000580
#define NPORT_DMODE_ESACK   0x00000800
#define NPORT_DMODE_EASACK  0x00001000
#define NPORT_DMODE_CWMASK  0x0003e000
#define NPORT_DMODE_CHMASK  0x007c0000
#define NPORT_DMODE_CSMASK  0x0f800000
#define NPORT_DMODE_SENDIAN 0x10000000

#define NPORT_SMASKXOFF 	4096
#define NPORT_SMASKYOFF 	4096
#define NPORT_CMODE_SM0   0x00000001
#define NPORT_CMODE_SM1   0x00000002
#define NPORT_CMODE_SM2   0x00000004
#define NPORT_CMODE_SM3   0x00000008
#define NPORT_CMODE_SM4   0x00000010
#define NPORT_CMODE_CMSK  0x00001e00

#define NPORT_CFG_G32MD   0x00000001
#define NPORT_CFG_BWIDTH  0x00000002
#define NPORT_CFG_ERCVR   0x00000004
#define NPORT_CFG_BDMSK   0x00000078
#define NPORT_CFG_BFAINT  0x00000080
#define NPORT_CFG_GDMSK   0x00001f00
#define NPORT_CFG_GD0     0x00000100
#define NPORT_CFG_GD1     0x00000200
#define NPORT_CFG_GD2     0x00000400
#define NPORT_CFG_GD3     0x00000800
#define NPORT_CFG_GD4     0x00001000
#define NPORT_CFG_GFAINT  0x00002000
#define NPORT_CFG_TOMSK   0x0001C000
#define NPORT_CFG_VRMSK   0x000E0000
#define NPORT_CFG_FBTYP   0x00100000

#define NPORT_STAT_VERS   0x00000007
#define NPORT_STAT_GBUSY  0x00000008
#define NPORT_STAT_BBUSY  0x00000010
#define NPORT_STAT_VRINT  0x00000020
#define NPORT_STAT_VIDINT 0x00000040
#define NPORT_STAT_GLMSK  0x00001f80
#define NPORT_STAT_BLMSK  0x0007e000
#define NPORT_STAT_BFIRQ  0x00080000
#define NPORT_STAT_GFIRQ  0x00100000

/* Reading/writing VC2 registers. */
#define VC2_REGADDR_INDEX      0x00000000
#define VC2_REGADDR_IREG       0x00000010
#define VC2_REGADDR_RAM        0x00000030
#define VC2_PROTOCOL           (NPORT_DMODE_EASACK | 0x00800000 | 0x00040000)

#define VC2_VLINET_ADDR        0x000
#define VC2_VFRAMET_ADDR       0x400
#define VC2_CGLYPH_ADDR        0x500

/* Now the Indexed registers of the VC2. */
#define VC2_IREG_VENTRY        0x00
#define VC2_IREG_CENTRY        0x01
#define VC2_IREG_CURSX         0x02
#define VC2_IREG_CURSY         0x03
#define VC2_IREG_CCURSX        0x04
#define VC2_IREG_DENTRY        0x05
#define VC2_IREG_SLEN          0x06
#define VC2_IREG_RADDR         0x07
#define VC2_IREG_VFPTR         0x08
#define VC2_IREG_VLSPTR        0x09
#define VC2_IREG_VLIR          0x0a
#define VC2_IREG_VLCTR         0x0b
#define VC2_IREG_CTPTR         0x0c
#define VC2_IREG_WCURSY        0x0d
#define VC2_IREG_DFPTR         0x0e
#define VC2_IREG_DLTPTR        0x0f
#define VC2_IREG_CONTROL       0x10
#define VC2_IREG_CONFIG        0x20

/* VC2 Control register bits */
#define VC2_CTRL_EVIRQ     0x0001
#define VC2_CTRL_EDISP     0x0002
#define VC2_CTRL_EVIDEO    0x0004
#define VC2_CTRL_EDIDS     0x0008
#define VC2_CTRL_ECURS     0x0010
#define VC2_CTRL_EGSYNC    0x0020
#define VC2_CTRL_EILACE    0x0040
#define VC2_CTRL_ECDISP    0x0080
#define VC2_CTRL_ECCURS    0x0100
#define VC2_CTRL_ECG64     0x0200
#define VC2_CTRL_GLSEL     0x0400

/* Controlling the color map on Newport. */
#define NCMAP_REGADDR_AREG   0x00000000
#define NCMAP_REGADDR_ALO    0x00000000		/* address register low  */
#define NCMAP_REGADDR_AHI    0x00000010		/* address register high */
#define NCMAP_REGADDR_PBUF   0x00000020		/* color palette buffer  */
#define NCMAP_REGADDR_CREG   0x00000030		/* command register 	 */
#define NCMAP_REGADDR_SREG   0x00000040		/* color buffer register */
#define NCMAP_REGADDR_RREG   0x00000060		/* revision register 	 */
#define NCMAP_PROTOCOL       (0x00008000 | 0x00040000 | 0x00800000)

/*
 * DCBMODE register defines:
 */

/* Widht of the data being transfered for each DCBDATA[01] word */
#define DCB_DATAWIDTH_4 0x0
#define DCB_DATAWIDTH_1 0x1
#define DCB_DATAWIDTH_2 0x2
#define DCB_DATAWIDTH_3 0x3

/* If set, all of DCBDATA will be moved, otherwise only DATAWIDTH bytes */
#define DCB_ENDATAPACK   (1 << 2)

/* Enables DCBCRS auto increment after each DCB transfer */
#define DCB_ENCRSINC     (1 << 3)

/* shift for accessing the control register select address (DBCCRS, 3 bits) */
#define DCB_CRS_SHIFT    4

/* DCBADDR (4 bits): display bus slave address */
#define DCB_ADDR_SHIFT   7
#define DCB_VC2          (0 <<  DCB_ADDR_SHIFT)
#define DCB_CMAP_ALL     (1 <<  DCB_ADDR_SHIFT)
#define DCB_CMAP0        (2 <<  DCB_ADDR_SHIFT)
#define DCB_CMAP1        (3 <<  DCB_ADDR_SHIFT)
#define DCB_XMAP_ALL     (4 <<  DCB_ADDR_SHIFT)
#define DCB_XMAP0        (5 <<  DCB_ADDR_SHIFT)
#define DCB_XMAP1        (6 <<  DCB_ADDR_SHIFT)
#define DCB_BT445        (7 <<  DCB_ADDR_SHIFT)
#define DCB_VCC1         (8 <<  DCB_ADDR_SHIFT)
#define DCB_VAB1         (9 <<  DCB_ADDR_SHIFT)
#define DCB_LG3_BDVERS0  (10 << DCB_ADDR_SHIFT)
#define DCB_LG3_ICS1562  (11 << DCB_ADDR_SHIFT)
#define DCB_RESERVED     (15 << DCB_ADDR_SHIFT)

/* DCB protocol ack types */
#define DCB_ENSYNCACK    (1 << 11)
#define DCB_ENASYNCACK   (1 << 12)

#define DCB_CSWIDTH_SHIFT 13
#define DCB_CSHOLD_SHIFT  18
#define DCB_CSSETUP_SHIFT 23

/* XMAP9 specific defines */
/*   XMAP9 -- registers as seen on the DCBMODE register*/
#   define XM9_CRS_CONFIG            (0 << DCB_CRS_SHIFT)
#       define XM9_PUPMODE           (1 << 0)
#       define XM9_ODD_PIXEL         (1 << 1)
#       define XM9_8_BITPLANES       (1 << 2)
#       define XM9_SLOW_DCB          (1 << 3)
#       define XM9_VIDEO_RGBMAP_MASK (3 << 4)
#	define XM9_VIDEO_RGBMAP_M0   (1 << 4)	
#	define XM9_VIDEO_RGMPAP_M1   (1 << 5)
#	define XM9_VIDEO_RGBMAP_M2   (3 << 4)
#       define XM9_EXPRESS_VIDEO     (1 << 6)
#       define XM9_VIDEO_OPTION      (1 << 7)
#   define XM9_CRS_REVISION          (1 << DCB_CRS_SHIFT)
#   define XM9_CRS_FIFO_AVAIL        (2 << DCB_CRS_SHIFT)
#       define XM9_FIFO_0_AVAIL      0
#       define XM9_FIFO_1_AVAIL      1
#       define XM9_FIFO_2_AVAIL      3
#       define XM9_FIFO_3_AVAIL      2
#       define XM9_FIFO_FULL         XM9_FIFO_0_AVAIL
#       define XM9_FIFO_EMPTY        XM9_FIFO_3_AVAIL
#   define XM9_CRS_CURS_CMAP_MSB     (3 << DCB_CRS_SHIFT)
#   define XM9_CRS_PUP_CMAP_MSB      (4 << DCB_CRS_SHIFT)
#   define XM9_CRS_MODE_REG_DATA     (5 << DCB_CRS_SHIFT)
#   define XM9_CRS_MODE_REG_INDEX    (7 << DCB_CRS_SHIFT)


#define DCB_CYCLES(setup,hold,width)                \
                  ((hold << DCB_CSHOLD_SHIFT)  |    \
		   (setup << DCB_CSSETUP_SHIFT)|    \
		   (width << DCB_CSWIDTH_SHIFT))

#define W_DCB_XMAP9_PROTOCOL       DCB_CYCLES (2, 1, 0)
#define WSLOW_DCB_XMAP9_PROTOCOL   DCB_CYCLES (5, 5, 0)
#define WAYSLOW_DCB_XMAP9_PROTOCOL DCB_CYCLES (12, 12, 0)
#define R_DCB_XMAP9_PROTOCOL       DCB_CYCLES (2, 1, 3)

/* xmap9 mode register layout */
#define XM9_MREG_BUF_SEL	(1 << 0)
#define XM9_MREG_OVL_BUF_SEL	(1 << 1)	
#define XM9_MREG_GAMMA_BYPASS	(1 << 2)
#define XM9_MREG_MSB_CMAP	(31 << 3)
#define XM9_MREG_PIX_MODE_MASK	(3 << 8)
#define XM9_MREG_PIX_MODE_RGB0	(1 << 8)
#define XM9_MREG_PIX_MODE_RGB1	(1 << 9)
#define XM9_MREG_PIX_MODE_RGB2	(3 << 8)
#define XM9_MREG_PIX_SIZE_MASK	(3 << 10)
#define XM9_MREG_PIX_SIZE_8BPP	(1 << 10)
#define XM9_MREG_PIX_SIZE_12BPP	(1 << 11)
#define XM9_MREG_PIX_SIZE_24BPP	(3 << 10)
#define XM9_MREG_VID_MODE_MASK	(3 << 12)
#define XM9_MREG_VID_MODE_OVL	(1 << 12)
#define XM9_MREG_VID_MODE_UDL	(1 << 13)
#define XM9_MREG_VID_MODE_RPL	(3 << 12)
#define XM9_MREG_BUF_VID_ALPHA	(1 << 15)
#define XM9_MREG_APIX_MODE_MASK	(7 << 16)
#define XM9_MREG_APIX_MODE_FUDL	(1 << 16)
#define XM9_MREG_APIX_MODE_FOVL	(1 << 17)
#define XM9_MREG_APIX_MODE_ODB	(3 << 17)
#define XM9_MREG_APIX_MODE_BOTH	(7 << 16)
#define XM9_MREG_AMSB_CMAP_MASK	(31 << 19)


typedef struct Newport Newport;
struct Newport
{
	ulong	drawmode1;
	ulong	drawmode0;
	ulong	lsmode;
	ulong	lspattern;
	ulong	lspatsave;
	ulong	zpattern;
	ulong	colorback;
	ulong	colorvram;
	ulong	alpharef;
	ulong	unused0;
	ulong	smask0x;
	ulong	smask0y;
	ulong	_setup;
	ulong	_stepz;
	ulong	_lsrestore;
	ulong	_lssave;
	ulong	unused1[0x30];
	ulong	_xstart;
	ulong	_ystart;
	ulong	_xend;
	ulong	_yend;
	ulong	xsave;
	ulong	xymode;
	ulong	bresd;
	ulong	bress1;
	ulong	bresoctinc1;
	ulong	bresoctinc2;
	ulong	brese1;
	ulong	bress2;
	ulong	aweight0;
	ulong	aweight1;
	ulong	xstartf;
	ulong	ystartf;
	ulong	xendf;
	ulong	yendf;
	ulong	xstarti;
	ulong	xendfi;
	ulong	xystarti;
	ulong	xyendi;
	ulong	xstartendi;
	ulong	unused2[0x29];
	ulong	colorred;
	ulong	coloralpha;
	ulong	colorgrn;
	ulong	colorblue;
	ulong	slopered;
	ulong	slopealpha;
	ulong	slopegrn;
	ulong	slopeblue;
	ulong	wrmask;
	ulong	colori;
	ulong	colorx;
	ulong	slopered1;
	ulong	hostrw0;
	ulong	hostrw1;
	ulong	dcbmode;
	ulong	unused3;
	ulong	dcbdata0;
	ulong	dcbdata1;
	ulong	unused4[0x36e];
	ulong	smask1x;
	ulong	smask1y;
	ulong	smask2x;
	ulong	smask2y;
	ulong	smask3x;
	ulong	smask3y;
	ulong	smask4x;
	ulong	smask4y;
	ulong	topscan;
	ulong	xywin;
	ulong	clipmode;
	ulong	unused5;
	ulong	config;
	ulong	unused6;
	ulong	stat;
	ulong	ustat;
	ulong	dreset;
};

static Newport *regs = IO(Newport, GIO_NEWPORT);
static Newport *regsgo = IO(Newport, GIO_NEWPORT|0x800);

Memimage *gscreen;
static Point curoff;

static void 
vc2set(uchar r, ushort val)
{
	regs->dcbmode = NPORT_DMODE_AVC2 | VC2_REGADDR_INDEX |
		NPORT_DMODE_W3 | NPORT_DMODE_ECINC | VC2_PROTOCOL;
	regs->dcbdata0 = r << 24 | val << 8;
}

static ushort
vc2get(uchar r)
{
	regs->dcbmode = NPORT_DMODE_AVC2 | VC2_REGADDR_INDEX |
		NPORT_DMODE_W1 | NPORT_DMODE_ECINC | VC2_PROTOCOL;
	regs->dcbdata0 = r << 24;
	regs->dcbmode = NPORT_DMODE_AVC2 | VC2_REGADDR_IREG |
		NPORT_DMODE_W2 | NPORT_DMODE_ECINC | VC2_PROTOCOL;
	return regs->dcbdata0 >> 16;
}


void
cursoron(void)
{
	Point xy;
	int s;

	xy = addpt(mousexy(), curoff);

	s = splhi();
	vc2set(VC2_IREG_CURSX, xy.x);
	vc2set(VC2_IREG_CURSY, xy.y);
	vc2set(VC2_IREG_CONTROL, vc2get(VC2_IREG_CONTROL) | VC2_CTRL_ECDISP);
	splx(s);
}

void
cursoroff(void)
{
	int s;

	s = splhi();
	vc2set(VC2_IREG_CONTROL, vc2get(VC2_IREG_CONTROL) & ~VC2_CTRL_ECDISP);
	splx(s);
}

void
setcursor(Cursor *curs)
{
	static uchar mem[(2*32*32)/8];
	uchar *set, *clr;
	int i, s;

	memset(mem, 0, sizeof(mem));

	/*
	 * convert to two 32x32 bitmaps
	 */
	set = mem;
	clr = mem + (32*32)/8;
	for(i=0;i<32;i++) {
		*set++ = curs->set[i];
		*clr++ = curs->clr[i];
		if(i & 1){
			set += 2;
			clr += 2;
		}
	}
	curoff = addpt(Pt(30,30), curs->offset);

	/*
	 * upload two bytes at a time
	 */
	s = splhi();
	vc2set(VC2_IREG_RADDR, vc2get(VC2_IREG_CENTRY));
	regs->dcbmode = NPORT_DMODE_AVC2 | VC2_REGADDR_RAM |
		NPORT_DMODE_W2 | VC2_PROTOCOL;
	for(i = 0; i < sizeof(mem); i += 2){
		while(regs->stat & NPORT_STAT_BBUSY)
			;
		regs->dcbdata0 = *(ushort*)(&mem[i]) << 16;
	}
	splx(s);
}	

static void
setmode(void)
{
	while(regs->stat & NPORT_STAT_BBUSY)
		;
	regs->dcbmode = (DCB_XMAP_ALL | W_DCB_XMAP9_PROTOCOL |
		XM9_CRS_CONFIG | NPORT_DMODE_W1);
	regs->dcbdata0 &= ~((XM9_8_BITPLANES | XM9_PUPMODE)<<24);

	while(regs->stat & NPORT_STAT_BBUSY)
		;
	regs->dcbmode = (DCB_XMAP_ALL | W_DCB_XMAP9_PROTOCOL |
		XM9_CRS_MODE_REG_DATA | NPORT_DMODE_W4);
	regs->dcbdata0 = (XM9_MREG_PIX_SIZE_24BPP | 
		XM9_MREG_PIX_MODE_RGB1 | XM9_MREG_GAMMA_BYPASS);

	while(regs->stat & NPORT_STAT_BBUSY)
		;
	regs->dcbmode = (DCB_XMAP_ALL | W_DCB_XMAP9_PROTOCOL |
		XM9_CRS_MODE_REG_INDEX | NPORT_DMODE_W1);
	regs->dcbdata0 &= ~(0xFF<<24);

	regs->drawmode1 = DM1_RGBPLANES | 
		NPORT_DMODE1_CCLT | NPORT_DMODE1_CCEQ | NPORT_DMODE1_CCGT | NPORT_DMODE1_LOSRC |
		NPORT_DMODE1_DD24 | NPORT_DMODE1_RGBMD | NPORT_DMODE1_HD32 | NPORT_DMODE1_RWPCKD;
}

static void
arcsoff(void)
{
	if(consuart != nil && consuart->console && strcmp(consuart->name, "arcs") == 0){
		consuart = nil;
		serialoq = nil;
	}
}

void
flushmemscreen(Rectangle r)
{
	static int modeset = 0;
	ulong *x0;
	int s, w;

	if(rectclip(&r, gscreen->r) == 0)
		return;

	s = splhi();
	if(!modeset){
		modeset = 1;
		arcsoff();
		setmode();
	}

	while(regs->stat & NPORT_STAT_GBUSY)
		;

	regs->drawmode0 = (NPORT_DMODE0_DRAW | NPORT_DMODE0_BLOCK | NPORT_DMODE0_CHOST);
	regs->xystarti = r.min.x << 16 | r.min.y;
	regs->xyendi = r.max.x-1 << 16 | r.max.y-1;

	x0 = wordaddr(gscreen, r.min); 
	for(w = r.max.x - r.min.x; r.min.y < r.max.y; r.min.y++){
		outl(&regsgo->hostrw0, x0, w);
		x0 += gscreen->width;
	}

	splx(s);
}

void
screeninit(void)
{
	enum {
		RGBX32 = CHAN4(CRed, 8, CGreen, 8, CBlue, 8, CIgnore, 8),
	};
	memimageinit();
	gscreen = allocmemimage(Rect(0,0,1280,1024), RGBX32);
	if(gscreen == nil)
		panic("screeninit: gscreen == nil");
	memfillcolor(gscreen, 0xFFFFFFFF);
	mouseaccelerate(3);
}

uchar*
attachscreen(Rectangle *r, ulong *chan, int* d, int *width, int *softscreen)
{
	*r = gscreen->r;
	*d = gscreen->depth;
	*chan = gscreen->chan;
	*width = gscreen->width;

	/* make devdraw use gscreen->data */
	*softscreen = 0xa110c;
	gscreen->data->ref++;

	return gscreen->data->bdata;
}

void
getcolor(ulong, ulong *, ulong *, ulong *)
{
}

int
setcolor(ulong, ulong, ulong, ulong)
{
	return 0;
}

void
blankscreen(int)
{
}

void
mousectl(Cmdbuf *)
{
}

/*
 * sgi mouse protocol
 *	byte 0 -	Y0 X0 Y7 X7  F  M  R  L
 *	byte 1 -	X7 X6 X5 X4 X3 X2 X1 X0
 *	byte 2 -	Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0
 */
void
sgimouseputc(int c)
{
	static uchar msg[3];
	static int nb;
	int dx, dy, newbuttons;
	static uchar b[] = { 0, 1, 4, 5, 2, 3, 6, 7 };
	static ulong lasttick;
	ulong m;

	/* Resynchronize in stream with timing. */
	m = MACHP(0)->ticks;
	if(TK2SEC(m - lasttick) > 2)
		nb = 0;
	lasttick = m;

	msg[nb] = c;
	if(++nb == 3){
		nb = 0;
		newbuttons = b[msg[0]&7];
		dx = (char)msg[1];
		dy = -(char)msg[2];
		mousetrack(dx, dy, newbuttons, TK2MS(MACHP(0)->ticks));
	}
}
