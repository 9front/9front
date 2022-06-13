#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/i2c.h"

#define	Image	IMAGE
#include	<draw.h>
#include	<memdraw.h>
#include	<cursor.h>
#include	"screen.h"

extern Memimage *gscreen;

/* pinmux registers */
enum {
	IOMUXC_GPR_GPR13	= 0x10034/4,	/* GPR13 for MIPI_MUX_SEL */
		MIPI_MUX_SEL = 1<<2,
		MIPI_MUX_INV = 1<<3,
};

/* gpio registers */
enum {
	GPIO_DR = 0x00/4,
	GPIO_GDIR = 0x04/4,
	GPIO_PSR = 0x08/4,
	GPIO_ICR1 = 0x0C/4,
	GPIO_ICR2 = 0x10/4,
	GPIO_IMR = 0x14/4,
	GPIO_ISR = 0x18/4,
	GPIO_EDGE_SEL = 0x1C/4,
};

/* system reset controller registers */
enum {
	SRC_MIPIPHY_RCR = 0x28/4,
		RCR_MIPI_DSI_PCLK_RESET_N	= 1<<5,
		RCR_MIPI_DSI_ESC_RESET_N	= 1<<4,
		RCR_MIPI_DSI_DPI_RESET_N	= 1<<3,
		RCR_MIPI_DSI_RESET_N		= 1<<2,
		RCR_MIPI_DSI_RESET_BYTE_N	= 1<<1,

	SRC_DISP_RCR = 0x34/4,
};

/* pwm controller registers */
enum {
	Pwmsrcclk = 25*Mhz,

	PWMCR = 0x00/4,
		CR_FWM_1 = 0<<26,
		CR_FWM_2 = 1<<26,
		CR_FWM_3 = 2<<26,
		CR_FWM_4 = 3<<26,

		CR_STOPEN = 1<<25,
		CR_DOZEN = 1<<24,
		CR_WAITEN = 1<<23,
		CR_DBGEN = 1<<22,
		CR_BCTR = 1<<21,
		CR_HCTR = 1<<20,

		CR_POUTC = 1<<18,

		CR_CLKSRC_OFF = 0<<16,
		CR_CLKSRC_IPG = 1<<16,
		CR_CLKSRC_HIGHFREQ = 2<<16,
		CR_CLKSRC_32K = 3<<16,

		CR_PRESCALER_SHIFT = 4,

		CR_SWR = 1<<3,

		CR_REPEAT_1 = 0<<1,
		CR_REPEAT_2 = 1<<1,
		CR_REPEAT_4 = 2<<1,
		CR_REPEAT_8 = 3<<1,

		CR_EN = 1<<0,

	PWMSR = 0x04/4,
	PWMIR = 0x08/4,
	PWMSAR = 0x0C/4,
		SAR_MASK = 0xFFFF,
	PWMPR = 0x10/4,
		PR_MASK = 0xFFFF,
	PWMCNR = 0x14/4,
		CNR_MASK = 0xFFFF,
};

/* dphy registers */
enum {
	DPHY_PD_PHY = 0x0/4,
	DPHY_M_PRG_HS_PREPARE = 0x4/4,
	DPHY_MC_PRG_HS_PREPARE = 0x8/4,
	DPHY_M_PRG_HS_ZERO = 0xc/4,
	DPHY_MC_PRG_HS_ZERO = 0x10/4,
	DPHY_M_PRG_HS_TRAIL = 0x14/4,
	DPHY_MC_PRG_HS_TRAIL = 0x18/4,
	DPHY_PD_PLL = 0x1c/4,
	DPHY_TST = 0x20/4,
	DPHY_CN = 0x24/4,
	DPHY_CM = 0x28/4,
	DPHY_CO = 0x2c/4,
	DPHY_LOCK = 0x30/4,
	DPHY_LOCK_BYP = 0x34/4,
	DPHY_RTERM_SEL = 0x38/4,
	DPHY_AUTO_PD_EN = 0x3c/4,
	DPHY_RXLPRP = 0x40/4,
	DPHY_RXCDR = 0x44/4,
	DPHY_RXHS_SETTLE = 0x48/4,	/* undocumented */
};

/* dsi-host registers */
enum {
	DSI_HOST_CFG_NUM_LANES = 0x0/4,
	DSI_HOST_CFG_NONCONTINUOUS_CLK = 0x4/4,
	DSI_HOST_CFG_AUTOINSERT_EOTP = 0x14/4,
	DSI_HOST_CFG_T_PRE = 0x8/4,
	DSI_HOST_CFG_T_POST = 0xc/4,
	DSI_HOST_CFG_TX_GAP = 0x10/4,
	DSI_HOST_CFG_EXTRA_CMDS_AFTER_EOTP = 0x18/4,
	DSI_HOST_CFG_HTX_TO_COUNT = 0x1c/4,
	DSI_HOST_CFG_LRX_H_TO_COUNT = 0x20/4,
	DSI_HOST_CFG_BTA_H_TO_COUNT = 0x24/4,
	DSI_HOST_CFG_TWAKEUP = 0x28/4,

	DSI_HOST_CFG_DPI_INTERFACE_COLOR_CODING = 0x208/4,
	DSI_HOST_CFG_DPI_PIXEL_FORMAT = 0x20c/4,
	DSI_HOST_CFG_DPI_VSYNC_POLARITY = 0x210/4,
	DSI_HOST_CFG_DPI_HSYNC_POLARITY = 0x214/4,
	DSI_HOST_CFG_DPI_VIDEO_MODE = 0x218/4,
	DSI_HOST_CFG_DPI_PIXEL_FIFO_SEND_LEVEL = 0x204/4,
	DSI_HOST_CFG_DPI_HFP = 0x21c/4,
	DSI_HOST_CFG_DPI_HBP = 0x220/4,
	DSI_HOST_CFG_DPI_HSA = 0x224/4,
	DSI_HOST_CFG_DPI_ENA_BLE_MULT_PKTS = 0x228/4,
	DSI_HOST_CFG_DPI_BLLP_MODE = 0x234/4,
	DSI_HOST_CFG_DPI_USE_NULL_PKT_BLLP = 0x238/4,
	DSI_HOST_CFG_DPI_VC = 0x240/4,
	DSI_HOST_CFG_DPI_PIXEL_PAYLOAD_SIZE = 0x200/4,
	DSI_HOST_CFG_DPI_VACTIVE = 0x23c/4,
	DSI_HOST_CFG_DPI_VBP = 0x22c/4,
	DSI_HOST_CFG_DPI_VFP = 0x230/4,
};

/* lcdif registers */
enum {
	LCDIF_CTRL	= 0x00/4,
	LCDIF_CTRL_SET	= 0x04/4,
	LCDIF_CTRL_CLR	= 0x08/4,
	LCDIF_CTRL_TOG	= 0x0C/4,
		CTRL_SFTRST			= 1<<31,
		CTRL_CLKGATE			= 1<<30,
		CTRL_YCBCR422_INPUT		= 1<<29,
		CTRL_READ_WEITEB		= 1<<28,
		CTRL_WAIT_FOR_VSYNC_EDGE	= 1<<27,
		CTRL_DATA_SHIFT_DIR		= 1<<26,
		CTRL_SHIFT_NUM_BITS		= 0x1F<<21,
		CTRL_DVI_MODE			= 1<<20,
		CTRL_BYPASS_COUNT		= 1<<19,
		CTRL_VSYNC_MODE			= 1<<18,
		CTRL_DOTCLK_MODE		= 1<<17,
		CTRL_DATA_SELECT		= 1<<16,

		CTRL_INPUT_DATA_NO_SWAP		= 0<<14,
		CTRL_INPUT_DATA_LITTLE_ENDIAN	= 0<<14,
		CTRL_INPUT_DATA_BIG_ENDIAB	= 1<<14,
		CTRL_INPUT_DATA_SWAP_ALL_BYTES	= 1<<14,
		CTRL_INPUT_DATA_HWD_SWAP	= 2<<14,
		CTRL_INPUT_DATA_HWD_BYTE_SWAP	= 3<<14,

		CTRL_CSC_DATA_NO_SWAP		= 0<<12,
		CTRL_CSC_DATA_LITTLE_ENDIAN	= 0<<12,
		CTRL_CSC_DATA_BIG_ENDIAB	= 1<<12,
		CTRL_CSC_DATA_SWAP_ALL_BYTES	= 1<<12,
		CTRL_CSC_DATA_HWD_SWAP		= 2<<12,
		CTRL_CSC_DATA_HWD_BYTE_SWAP	= 3<<12,

		CTRL_LCD_DATABUS_WIDTH_16_BIT	= 0<<10,
		CTRL_LCD_DATABUS_WIDTH_8_BIT	= 1<<10,
		CTRL_LCD_DATABUS_WIDTH_18_BIT	= 2<<10,
		CTRL_LCD_DATABUS_WIDTH_24_BIT	= 3<<10,

		CTRL_WORD_LENGTH_16_BIT		= 0<<8,
		CTRL_WORD_LENGTH_8_BIT		= 1<<8,
		CTRL_WORD_LENGTH_18_BIT		= 2<<8,
		CTRL_WORD_LENGTH_24_BIT		= 3<<8,

		CTRL_RGB_TO_YCBCR422_CSC	= 1<<7,

		CTRL_MASTER			= 1<<5,

		CTRL_DATA_FORMAT_16_BIT		= 1<<3,
		CTRL_DATA_FORMAT_18_BIT		= 1<<2,
		CTRL_DATA_FORMAT_24_BIT		= 1<<1,

		CTRL_RUN			= 1<<0,

	LCDIF_CTRL1	= 0x10/4,
	LCDIF_CTRL1_SET	= 0x14/4,
	LCDIF_CTRL1_CLR	= 0x18/4,
	LCDIF_CTRL1_TOG	= 0x1C/4,
		CTRL1_COMBINE_MPU_WR_STRB	= 1<<27,
		CTRL1_BM_ERROR_IRQ_EN		= 1<<26,
		CTRL1_BM_ERROR_IRQ		= 1<<25,
		CTRL1_RECOVER_ON_UNDERFLOW	= 1<<24,

		CTRL1_INTERLACE_FIELDS		= 1<<23,
		CTRL1_START_INTERLACE_FROM_SECOND_FIELD	= 1<<22,

		CTRL1_FIFO_CLEAR		= 1<<21,
		CTRL1_IRQ_ON_ALTERNATE_FIELDS	= 1<<20,

		CTRL1_BYTE_PACKING_FORMAT	= 0xF<<16,

		CTRL1_OVERFLOW_IRQ_EN		= 1<<15,
		CTRL1_UNDERFLOW_IRQ_EN		= 1<<14,
		CTRL1_CUR_FRAME_DONE_IRQ_EN	= 1<<13,
		CTRL1_VSYNC_EDGE_IRQ_EN		= 1<<12,
		CTRL1_OVERFLOW_IRQ		= 1<<11,
		CTRL1_UNDERFLOW_IRQ		= 1<<10,
		CTRL1_CUR_FRAME_DONE_IRQ	= 1<<9,
		CTRL1_VSYNC_EDGE_IRQ		= 1<<8,

		CTRL1_BUSY_ENABLE		= 1<<2,
		CTRL1_MODE86			= 1<<1,
		CTRL1_RESET			= 1<<0,

	LCDIF_CTRL2	= 0x20/4,
	LCDIF_CTRL2_SET	= 0x24/4,
	LCDIF_CTRL2_CLR	= 0x28/4,
	LCDIF_CTRL2_TOG	= 0x2C/4,
		CTRL2_OUTSTANDING_REQS_REQ_1	= 0<<21,
		CTRL2_OUTSTANDING_REQS_REQ_2	= 1<<21,
		CTRL2_OUTSTANDING_REQS_REQ_4	= 2<<21,
		CTRL2_OUTSTANDING_REQS_REQ_8	= 3<<21,
		CTRL2_OUTSTANDING_REQS_REQ_16	= 4<<21,

		CTRL2_BURST_LEN_8		= 1<<20,

		CTRL2_ODD_LINE_PATTERN_RGB	= 0<<16,
		CTRL2_ODD_LINE_PATTERN_RBG	= 1<<16,
		CTRL2_ODD_LINE_PATTERN_GBR	= 2<<16,
		CTRL2_ODD_LINE_PATTERN_GRB	= 3<<16,
		CTRL2_ODD_LINE_PATTERN_BRG	= 4<<16,
		CTRL2_ODD_LINE_PATTERN_BGR	= 5<<16,

		CTRL2_EVEN_LINE_PATTERN_RGB	= 0<<12,
		CTRL2_EVEN_LINE_PATTERN_RBG	= 1<<12,
		CTRL2_EVEN_LINE_PATTERN_GBR	= 2<<12,
		CTRL2_EVEN_LINE_PATTERN_GRB	= 3<<12,
		CTRL2_EVEN_LINE_PATTERN_BRG	= 4<<12,
		CTRL2_EVEN_LINE_PATTERN_BGR	= 5<<12,

		CTRL2_READ_PACK_DIR		= 1<<10,

		CTRL2_READ_MODE_OUTPUT_IN_RGB_FORMAT = 1<<9,
		CTRL2_READ_MODE_6_BIT_INPUT	= 1<<8,
		CTRL2_READ_MODE_NUM_PACKED_SUBWORDS = 7<<4,
		CTRL2_INITIAL_DUMMY_READS	= 7<<1,

	LCDIF_TRANSFER_COUNT= 0x30/4,
		TRANSFER_COUNT_V_COUNT		= 0xFFFF<<16,
		TRANSFER_COUNT_H_COUNT		= 0xFFFF,

	LCDIF_CUR_BUF	= 0x40/4,
	LCDIF_NEXT_BUF	= 0x50/4,

	LCDIF_TIMING	= 0x60/4,
		TIMING_CMD_HOLD			= 0xFF<<24,
		TIMING_CMD_SETUP		= 0xFF<<16,
		TIMING_DATA_HOLD		= 0xFF<<8,
		TIMING_DATA_SETUP		= 0xFF<<0,

	LCDIF_VDCTRL0	= 0x70/4,
		VDCTRL0_VSYNC_OEB		= 1<<29,
		VDCTRL0_ENABLE_PRESENT		= 1<<28,

		VDCTRL0_VSYNC_POL		= 1<<27,
		VDCTRL0_HSYNC_POL		= 1<<26,
		VDCTRL0_DOTCLK_POL		= 1<<25,
		VDCTRL0_ENABLE_POL		= 1<<24,

		VDCTRL0_VSYNC_PERIOD_UNIT	= 1<<21,
		VDCTRL0_VSYNC_PULSE_WIDTH_UNIT	= 1<<20,
		VDCTRL0_HALF_LINE		= 1<<19,
		VDCTRL0_HALF_LINE_MODE		= 1<<18,

		VDCTRL0_VSYNC_PULSE_WIDTH	= 0x3FFFF,

	LCDIF_VDCTRL1	= 0x80/4,
		VDCTRL1_VSYNC_PERIOD = 0xFFFFFFFF,

	LCDIF_VDCTRL2	= 0x90/4,
		VDCTRL2_HSYNC_PERIOD = 0x3FFFF,
		VDCTRL2_HSYNC_PULSE_WIDTH = 0x3FFF<<18,
		
	LCDIF_VDCTRL3	= 0xA0/4,
		VDCTRL3_VERTICAL_WAIT_CNT = 0xFFFF,
		VDCTRL3_HORIZONTAL_WAIT_CNT = 0xFFF<<16,
		VDCTRL3_VSYNC_ONLY = 1<<28,
		VDCTRL3_MUX_SYNC_SIGNALS = 1<<29,

	LCDIF_VDCTRL4	= 0xB0/4,
		VDCTRL4_DOTCLK_H_VALID_DATA_CNT = 0x3FFFF,
		VDCTRL4_SYNC_SIGNALS_ON = 1<<18,

		VDCTRL4_DOTCLK_DLY_2NS = 0<<29,
		VDCTRL4_DOTCLK_DLY_4NS = 1<<29,
		VDCTRL4_DOTCLK_DLY_6NS = 2<<29,
		VDCTRL4_DOTCLK_DLY_8NS = 3<<29,

	LCDIF_DATA	= 0x180/4,

	LCDIF_STAT	= 0x1B0/4,

	LCDIF_AS_CTRL	= 0x210/4,
};

struct video_mode {
	int	pixclk;
	int	hactive;
	int	vactive;
	int	hblank;
	int	vblank;
	int	hso;
	int	vso;
	int	hspw;
	int	vspw;

	char	vsync_pol;
	char	hsync_pol;
};

struct dsi_cfg {
	int	lanes;

	int	ref_clk;
	int	hs_clk;
	int	ui_ps;

	int	byte_clk;
	int	byte_t_ps;

	int	tx_esc_clk;
	int	tx_esc_t_ps;

	int	rx_esc_clk;

	int	clk_pre_ps;
	int	clk_prepare_ps;
	int	clk_zero_ps;

	int	hs_prepare_ps;
	int	hs_zero_ps;
	int	hs_trail_ps;
	int	hs_exit_ps;

	int	lpx_ps;

	vlong	wakeup_ps;
};

/* base addresses, VIRTIO is at 0x30000000 physical */

static u32int *iomuxc = (u32int*)(VIRTIO + 0x330000);

static u32int *gpio1 = (u32int*)(VIRTIO + 0x200000);
static u32int *gpio3 = (u32int*)(VIRTIO + 0x220000);

static u32int *pwm2 = (u32int*)(VIRTIO + 0x670000);

static u32int *resetc= (u32int*)(VIRTIO + 0x390000);

static u32int *dsi =   (u32int*)(VIRTIO + 0xA00000);
static u32int *dphy =  (u32int*)(VIRTIO + 0xA00300);

static u32int *lcdif = (u32int*)(VIRTIO + 0x320000);

/* shift and mask */
static u32int
sm(u32int v, u32int m)
{
	int s;

	if(m == 0)
		return 0;

	s = 0;
	while((m & 1) == 0){
		m >>= 1;
		s++;
	}

	return (v & m) << s;
}

static u32int
rr(u32int *base, int reg)
{
	u32int val = base[reg];
//	iprint("%#p+%#.4x -> %#.8ux\n", PADDR(base), reg*4, val);
	return val;
}
static void
wr(u32int *base, int reg, u32int val)
{
//	iprint("%#p+%#.4x <- %#.8ux\n", PADDR(base), reg*4, val);
	base[reg] = val;
}
static void
mr(u32int *base, int reg, u32int val, u32int mask)
{
	wr(base, reg, (rr(base, reg) & ~mask) | (val & mask));
}

static void
dsiparams(struct dsi_cfg *cfg, int lanes, int hs_clk, int ref_clk, int tx_esc_clk, int rx_esc_clk)
{
	cfg->lanes = lanes;
	cfg->ref_clk = ref_clk;

	cfg->hs_clk = hs_clk;
	cfg->ui_ps = (1000000000000LL + (cfg->hs_clk-1)) / cfg->hs_clk;

	cfg->byte_clk = cfg->hs_clk / 8;
	cfg->byte_t_ps = cfg->ui_ps * 8;

	cfg->tx_esc_clk = tx_esc_clk;
	cfg->tx_esc_t_ps = (1000000000000LL + (cfg->tx_esc_clk-1)) / cfg->tx_esc_clk;

	cfg->rx_esc_clk = rx_esc_clk;

	/* min 8*ui */
	cfg->clk_pre_ps = 8*cfg->ui_ps;

	/* min 38ns, max 95ns */
	cfg->clk_prepare_ps = 38*1000;

	/* clk_prepare + clk_zero >= 300ns */
	cfg->clk_zero_ps = 300*1000 - cfg->clk_prepare_ps;

	/* min 40ns + 4*ui, max 85ns + 6*ui */
	cfg->hs_prepare_ps = 40*1000 + 4*cfg->ui_ps;

	/* hs_prepae + hs_zero >= 145ns + 10*ui */
	cfg->hs_zero_ps = (145*1000 + 10*cfg->ui_ps) - cfg->hs_prepare_ps;

	/* min max(n*8*ui, 60ns + n*4*ui); n=1 */
	cfg->hs_trail_ps = 60*1000 + 1*4*cfg->ui_ps;
	if(cfg->hs_trail_ps < 1*8*cfg->ui_ps)
		cfg->hs_trail_ps = 1*8*cfg->ui_ps;

	/* min 100ns */
	cfg->hs_exit_ps = 100*1000;

	/* min 50ns */
	cfg->lpx_ps = 50*1000;

	/* min 1ms */
	cfg->wakeup_ps = 1000000000000LL;
}

static void
lcdifinit(struct video_mode *mode)
{
	wr(lcdif, LCDIF_CTRL_CLR, CTRL_SFTRST);
	while(rr(lcdif, LCDIF_CTRL) & CTRL_SFTRST)
		;
	wr(lcdif, LCDIF_CTRL_CLR, CTRL_CLKGATE);
	while(rr(lcdif, LCDIF_CTRL) & (CTRL_SFTRST|CTRL_CLKGATE))
		;

	wr(lcdif, LCDIF_CTRL1_SET, CTRL1_FIFO_CLEAR);
	wr(lcdif, LCDIF_AS_CTRL, 0);

	wr(lcdif, LCDIF_CTRL1, sm(7, CTRL1_BYTE_PACKING_FORMAT));

	wr(lcdif, LCDIF_CTRL,
		CTRL_BYPASS_COUNT |
		CTRL_MASTER |
		CTRL_LCD_DATABUS_WIDTH_24_BIT |
		CTRL_WORD_LENGTH_24_BIT);

	wr(lcdif, LCDIF_TRANSFER_COUNT,
		sm(mode->vactive, TRANSFER_COUNT_V_COUNT) |
		sm(mode->hactive, TRANSFER_COUNT_H_COUNT));

	wr(lcdif, LCDIF_VDCTRL0,
		VDCTRL0_ENABLE_PRESENT |
		VDCTRL0_VSYNC_POL | VDCTRL0_HSYNC_POL |
		VDCTRL0_VSYNC_PERIOD_UNIT |
		VDCTRL0_VSYNC_PULSE_WIDTH_UNIT |
		sm(mode->vspw, VDCTRL0_VSYNC_PULSE_WIDTH));

	wr(lcdif, LCDIF_VDCTRL1,
		sm(mode->vactive + mode->vblank, VDCTRL1_VSYNC_PERIOD));

	wr(lcdif, LCDIF_VDCTRL2,
		sm(mode->hactive + mode->hblank, VDCTRL2_HSYNC_PERIOD) |
		sm(mode->hspw, VDCTRL2_HSYNC_PULSE_WIDTH));

	wr(lcdif, LCDIF_VDCTRL3,
		sm(mode->vblank - mode->vso, VDCTRL3_VERTICAL_WAIT_CNT) |
		sm(mode->hblank - mode->hso, VDCTRL3_HORIZONTAL_WAIT_CNT));

	wr(lcdif, LCDIF_VDCTRL4,
		sm(mode->hactive, VDCTRL4_DOTCLK_H_VALID_DATA_CNT));

	wr(lcdif, LCDIF_CUR_BUF, PADDR(gscreen->data->bdata));
	wr(lcdif, LCDIF_NEXT_BUF, PADDR(gscreen->data->bdata));

	wr(lcdif, LCDIF_CTRL_SET, CTRL_DOTCLK_MODE);

	mr(lcdif, LCDIF_VDCTRL4, VDCTRL4_SYNC_SIGNALS_ON, VDCTRL4_SYNC_SIGNALS_ON);

	wr(lcdif, LCDIF_CTRL_SET, CTRL_RUN);
}

static void
bridgeinit(I2Cdev *dev, struct video_mode *mode, struct dsi_cfg *cfg)
{
	int n;

	// clock derived from dsi clock
	switch(cfg->hs_clk/2000000){
	case 384:
	default:	n = 1 << 1; break;
	case 416:	n = 2 << 1; break;
	case 468:	n = 0 << 1; break;
	case 486:	n = 3 << 1; break;
	case 461:	n = 4 << 1; break;
	}
	i2cwritebyte(dev, 0x0a, n);

	// single channel A
	n = 1<<5 | (cfg->lanes-4)<<3 | 3<<1;
	i2cwritebyte(dev, 0x10, n);

	// Enhanced framing and ASSR
	i2cwritebyte(dev, 0x5a, 0x05);

	// 2 DP lanes w/o SSC
	i2cwritebyte(dev, 0x93, 0x20);

	// 2.7Gbps DP data rate
	i2cwritebyte(dev, 0x94, 0x80);

	// Enable PLL and confirm PLL is locked
	i2cwritebyte(dev, 0x0d, 0x01);

	// wait for PLL to lock
	while((i2creadbyte(dev, 0x0a) & 0x80) == 0)
		;

	// Enable ASSR on display
	i2cwritebyte(dev, 0x64, 0x01);
	i2cwritebyte(dev, 0x75, 0x01);
	i2cwritebyte(dev, 0x76, 0x0a);
	i2cwritebyte(dev, 0x77, 0x01);
	i2cwritebyte(dev, 0x78, 0x81);

	// Train link and confirm trained
	i2cwritebyte(dev, 0x96, 0x0a);
	while(i2creadbyte(dev, 0x96) != 1)
		;

	// video timings
	i2cwritebyte(dev, 0x20, mode->hactive & 0xFF);
	i2cwritebyte(dev, 0x21, mode->hactive >> 8);
	i2cwritebyte(dev, 0x24, mode->vactive & 0xFF);
	i2cwritebyte(dev, 0x25, mode->vactive >> 8);
	i2cwritebyte(dev, 0x2c, mode->hspw);
	i2cwritebyte(dev, 0x2d, mode->hspw>>8 | (mode->hsync_pol=='-')<<7);
	i2cwritebyte(dev, 0x30, mode->vspw);
	i2cwritebyte(dev, 0x31, mode->vspw>>8 | (mode->vsync_pol=='-')<<7);
	i2cwritebyte(dev, 0x34, mode->hblank - mode->hspw - mode->hso);
	i2cwritebyte(dev, 0x36, mode->vblank - mode->vspw - mode->vso);
	i2cwritebyte(dev, 0x38, mode->hso);
	i2cwritebyte(dev, 0x3a, mode->vso);

	// Enable video stream, ASSR, enhanced framing
	i2cwritebyte(dev, 0x5a, 0x0d);
}

static char*
parseedid128(struct video_mode *mode, uchar edid[128])
{
	static uchar magic[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
	uchar *p, sum;
	int i;

	if(memcmp(edid, magic, 8) != 0)
		return "bad edid magic";

	sum = 0;
	for(i=0; i<128; i++) 
		sum += edid[i];
	if(sum != 0)
		return "bad edid checksum";

	/*
	 * Detailed Timings
	 */
	p = edid+8+10+2+5+10+3+16;
	for(i=0; i<4; i++, p+=18){
		if((p[0]|p[1])==0)
			continue;

		memset(mode, 0, sizeof(*mode));

		mode->pixclk = ((p[1]<<8) | p[0]) * 10000;

		mode->hactive = ((p[4] & 0xF0)<<4) | p[2];		/* horizontal active */
		mode->hblank = ((p[4] & 0x0F)<<8) | p[3];		/* horizontal blanking */
		mode->vactive = ((p[7] & 0xF0)<<4) | p[5];		/* vertical active */
		mode->vblank = ((p[7] & 0x0F)<<8) | p[6];		/* vertical blanking */
		mode->hso = ((p[11] & 0xC0)<<2) | p[8];			/* horizontal sync offset */
		mode->hspw = ((p[11] & 0x30)<<4) | p[9];		/* horizontal sync pulse width */
		mode->vso = ((p[11] & 0x0C)<<2) | ((p[10] & 0xF0)>>4);	/* vertical sync offset */
		mode->vspw = ((p[11] & 0x03)<<4) | (p[10] & 0x0F);	/* vertical sync pulse width */

		switch((p[17] & 0x18)>>3) {
		case 3:	/* digital separate sync signal; the norm */
			mode->vsync_pol = (p[17] & 0x04) ? '+' : '-';
			mode->hsync_pol = (p[17] & 0x02) ? '+' : '-';
			break;
		}

		return nil;
	}

	return "no edid timings available";
}

static char*
getmode(I2Cdev *dev, struct video_mode *mode)
{
	static uchar edid[128];
	static I2Cdev aux;

	aux.bus = dev->bus;
	aux.addr = 0x50;
	aux.subaddr = 1;
	aux.size = sizeof(edid);

	/* enable passthru mode for address 0x50 (EDID) */
	i2cwritebyte(dev, 0x60, aux.addr<<1 | 1);
	addi2cdev(&aux);

	if(i2crecv(&aux, edid, sizeof(edid), 0) != sizeof(edid))
		return "i2crecv failed to get edid bytes";

	return parseedid128(mode, edid);
}

static void
dphyinit(struct dsi_cfg *cfg)
{
	int n;

	/* powerdown */
	wr(dphy, DPHY_PD_PLL, 1);
	wr(dphy, DPHY_PD_PHY, 1);

	/* magic */
	wr(dphy, DPHY_LOCK_BYP, 0);
	wr(dphy, DPHY_RTERM_SEL, 1);
	wr(dphy, DPHY_AUTO_PD_EN, 0);
	wr(dphy, DPHY_RXLPRP, 2);
	wr(dphy, DPHY_RXCDR, 2);
	wr(dphy, DPHY_TST, 0x25);

	/* hs timings */
	n = (2*cfg->hs_prepare_ps - cfg->tx_esc_t_ps) / cfg->tx_esc_t_ps;
	if(n < 0)
		n = 0;
	else if(n > 3)
		n = 3;
	wr(dphy, DPHY_M_PRG_HS_PREPARE, n);

	n = (2*cfg->clk_prepare_ps - cfg->tx_esc_t_ps) / cfg->tx_esc_t_ps;
	if(n < 0)
		n = 0;
	else if(n > 1)
		n = 1;
	wr(dphy, DPHY_MC_PRG_HS_PREPARE, n);

	n = ((cfg->hs_zero_ps + (cfg->byte_t_ps-1)) / cfg->byte_t_ps) - 6;
	if(n < 1)
		n = 1;
	wr(dphy, DPHY_M_PRG_HS_ZERO, n);

	n = ((cfg->clk_zero_ps + (cfg->byte_t_ps-1)) / cfg->byte_t_ps) - 3;
	if(n < 1)
		n = 1;
	wr(dphy, DPHY_MC_PRG_HS_ZERO, n);

	n = (cfg->hs_trail_ps + (cfg->byte_t_ps-1)) / cfg->byte_t_ps;
	if(n < 1)
		n = 1;
	else if(n > 15)
		n = 15;
	wr(dphy, DPHY_M_PRG_HS_TRAIL, n);
	wr(dphy, DPHY_MC_PRG_HS_TRAIL, n);

	if(cfg->hs_clk < 80*Mhz)
		n = 0xD;
	else if(cfg->hs_clk < 90*Mhz)
		n = 0xC;
	else if(cfg->hs_clk < 125*Mhz)
		n = 0xB;
	else if(cfg->hs_clk < 150*Mhz)
		n = 0xA;
	else if(cfg->hs_clk < 225*Mhz)
		n = 0x9;
	else if(cfg->hs_clk < 500*Mhz)
		n = 0x8;
	else
		n = 0x7;
	wr(dphy, DPHY_RXHS_SETTLE, n);

	/* hs_clk = ref_clk * (CM / (CN*CO)); just set CN=CO=1 */
	n = (cfg->hs_clk + cfg->ref_clk-1) / cfg->ref_clk;

	/* strange encoding for CM */
	if(n < 32)
		n = 0xE0 | (n - 16);
	else if(n < 64)
		n = 0xC0 | (n - 32);
	else if(n < 128)
		n = 0x80 | (n - 64);
	else
		n = n - 128;
	wr(dphy, DPHY_CM, n);	

	wr(dphy, DPHY_CN, 0x1F);	/* CN==1 */
	wr(dphy, DPHY_CO, 0x00);	/* CO==1 */
}

static void
dphypowerup(void)
{
	wr(dphy, DPHY_PD_PLL, 0);
	while((rr(dphy, DPHY_LOCK) & 1) == 0)
		;
	wr(dphy, DPHY_PD_PHY, 0);
}

static void
dsiinit(struct dsi_cfg *cfg)
{
	int n;

	wr(dsi, DSI_HOST_CFG_NUM_LANES, cfg->lanes-1);

	wr(dsi, DSI_HOST_CFG_NONCONTINUOUS_CLK, 0x0);
	wr(dsi, DSI_HOST_CFG_AUTOINSERT_EOTP, 0x0);

	n = (cfg->clk_pre_ps + cfg->byte_t_ps-1) / cfg->byte_t_ps;
	wr(dsi, DSI_HOST_CFG_T_PRE, n);

	n = (cfg->clk_pre_ps + cfg->lpx_ps + cfg->clk_prepare_ps + cfg->clk_zero_ps + cfg->byte_t_ps-1) / cfg->byte_t_ps;
	wr(dsi, DSI_HOST_CFG_T_POST, n);

	n = (cfg->hs_exit_ps + cfg->byte_t_ps-1) / cfg->byte_t_ps;
	wr(dsi, DSI_HOST_CFG_TX_GAP, n);

	wr(dsi, DSI_HOST_CFG_EXTRA_CMDS_AFTER_EOTP, 0x1);

	wr(dsi, DSI_HOST_CFG_HTX_TO_COUNT, 0x0);
	wr(dsi, DSI_HOST_CFG_LRX_H_TO_COUNT, 0x0);
	wr(dsi, DSI_HOST_CFG_BTA_H_TO_COUNT, 0x0);

	n = (cfg->wakeup_ps + cfg->tx_esc_t_ps-1) / cfg->tx_esc_t_ps;
	wr(dsi, DSI_HOST_CFG_TWAKEUP, n);
}

static void
dpiinit(struct video_mode *mode)
{
	wr(dsi, DSI_HOST_CFG_DPI_INTERFACE_COLOR_CODING, 0x5); // 24-bit

	wr(dsi, DSI_HOST_CFG_DPI_PIXEL_FORMAT, 0x3); // 24-bit

	/* this seems wrong */
	wr(dsi, DSI_HOST_CFG_DPI_VSYNC_POLARITY, 0);
	wr(dsi, DSI_HOST_CFG_DPI_HSYNC_POLARITY, 0); 

	wr(dsi, DSI_HOST_CFG_DPI_VIDEO_MODE, 0x1); // non-burst mode with sync events

	wr(dsi, DSI_HOST_CFG_DPI_PIXEL_FIFO_SEND_LEVEL, mode->hactive); 

	wr(dsi, DSI_HOST_CFG_DPI_HFP, mode->hso); 
	wr(dsi, DSI_HOST_CFG_DPI_HBP, mode->hblank - mode->hspw - mode->hso); 
	wr(dsi, DSI_HOST_CFG_DPI_HSA, mode->hspw); 

	wr(dsi, DSI_HOST_CFG_DPI_ENA_BLE_MULT_PKTS, 0x0); 

	wr(dsi, DSI_HOST_CFG_DPI_BLLP_MODE, 0x1);

	wr(dsi, DSI_HOST_CFG_DPI_USE_NULL_PKT_BLLP, 0x0);

	wr(dsi, DSI_HOST_CFG_DPI_VC, 0x0); 
	wr(dsi, DSI_HOST_CFG_DPI_PIXEL_PAYLOAD_SIZE, mode->hactive); 
	wr(dsi, DSI_HOST_CFG_DPI_VACTIVE, mode->vactive - 1); 
	wr(dsi, DSI_HOST_CFG_DPI_VBP, mode->vblank - mode->vspw - mode->vso); 
	wr(dsi, DSI_HOST_CFG_DPI_VFP, mode->vso); 
}

void
lcdinit(void)
{
	struct dsi_cfg dsi_cfg;
	struct video_mode mode;
	I2Cdev *bridge;
	char *err;

	/* gpio3_io20: sn65dsi86 bridge */
	iomuxpad("pad_sai5_rxc", "gpio3_io20", nil);

	/* gpio1_io10: for panel */
	iomuxpad("pad_gpio1_io10", "gpio1_io10", nil);	

	/* pwm2_out: for panel backlight */
	iomuxpad("pad_spdif_rx", "pwm2_out", nil);
	
	/* lcdif to dpi=0, dcss=1 */
	mr(iomuxc, IOMUXC_GPR_GPR13, 0, MIPI_MUX_SEL);

	setclkgate("gpio1.ipg_clk_s", 1);
	setclkgate("gpio3.ipg_clk_s", 1);

	setclkrate("pwm2.ipg_clk_high_freq", "osc_25m_ref_clk", Pwmsrcclk);
	setclkgate("pwm2.ipg_clk_high_freq", 1);

	mr(gpio1, GPIO_GDIR, 1<<10, 1<<10);	/* gpio1 10 output */
	mr(gpio1, GPIO_DR, 0<<10, 1<<10);	/* gpio1 10 low: panel off */

	wr(pwm2, PWMIR, 0);	
	wr(pwm2, PWMCR, CR_STOPEN | CR_DOZEN | CR_WAITEN | CR_DBGEN | CR_CLKSRC_HIGHFREQ | 0<<CR_PRESCALER_SHIFT);
	wr(pwm2, PWMSAR, Pwmsrcclk/150000);
	wr(pwm2, PWMPR, (Pwmsrcclk/100000)-2);
	mr(pwm2, PWMCR, CR_EN, CR_EN);

	mr(gpio1, GPIO_DR, 1<<10, 1<<10);	/* gpio1 10 high: panel on */

	mr(gpio3, GPIO_GDIR, 1<<20, 1<<20);	/* gpio3 20 output */
	mr(gpio3, GPIO_DR, 1<<20, 1<<20);	/* gpio3 20 high: bridge on */

	bridge = i2cdev(i2cbus("i2c4"), 0x2C);
	if(bridge == nil)
		return;
	bridge->subaddr = 1;

	/* power on mipi dsi */
	powerup("mipi");

	mr(resetc, SRC_MIPIPHY_RCR, 0, RCR_MIPI_DSI_RESET_N);
	mr(resetc, SRC_MIPIPHY_RCR, 0, RCR_MIPI_DSI_PCLK_RESET_N);
	mr(resetc, SRC_MIPIPHY_RCR, 0, RCR_MIPI_DSI_ESC_RESET_N);
	mr(resetc, SRC_MIPIPHY_RCR, 0, RCR_MIPI_DSI_RESET_BYTE_N);
	mr(resetc, SRC_MIPIPHY_RCR, 0, RCR_MIPI_DSI_DPI_RESET_N);

	setclkgate("sim_display.mainclk", 0);
	setclkgate("disp.axi_clk", 0);
	setclkrate("disp.axi_clk", "system_pll1_clk", 800*Mhz);
	setclkrate("disp.rtrm_clk", "system_pll1_clk", 400*Mhz);
	setclkgate("disp.axi_clk", 1);
	setclkgate("sim_display.mainclk", 1);

	setclkrate("mipi.core", "system_pll1_div3", 266*Mhz);
	setclkrate("mipi.CLKREF", "system_pll2_clk", 25*Mhz);
	setclkrate("mipi.RxClkEsc", "system_pll1_clk", 80*Mhz);
	setclkrate("mipi.TxClkEsc", nil, 20*Mhz);

	/* dsi parameters are fixed for the bridge */
	dsiparams(&dsi_cfg, 4, 2*486*Mhz,
		getclkrate("mipi.CLKREF"),
		getclkrate("mipi.TxClkEsc"),
		getclkrate("mipi.RxClkEsc"));

	/* release dphy reset */
	mr(resetc, SRC_MIPIPHY_RCR, RCR_MIPI_DSI_PCLK_RESET_N, RCR_MIPI_DSI_PCLK_RESET_N);

	dphyinit(&dsi_cfg);
	dsiinit(&dsi_cfg);
	dphypowerup();

	/* release mipi clock resets (generated by the dphy) */
	mr(resetc, SRC_MIPIPHY_RCR, RCR_MIPI_DSI_ESC_RESET_N, RCR_MIPI_DSI_ESC_RESET_N);
	mr(resetc, SRC_MIPIPHY_RCR, RCR_MIPI_DSI_RESET_BYTE_N, RCR_MIPI_DSI_RESET_BYTE_N);

	/*
	 * get mode information from EDID, this can only be done after the clocks
	 * are generated by the DPHY and the clock resets have been released.
	 */
	err = getmode(bridge, &mode);
	if(err != nil)
		goto out;

	/* allocates the framebuffer (gscreen->data->bdata) */
	if(screeninit(mode.hactive, mode.vactive, 32) < 0){
		err = "screeninit failed";
		goto out;
	}

	/*
	 * start the pixel clock. running at the actual pixel clock
	 * causes the screen to shift horizontally after a while.
	 * using 80% seems to fix it - for now.
	 */
	setclkrate("lcdif.pix_clk", "system_pll1_clk", (mode.pixclk*8)/10);
	dpiinit(&mode);

	/* release dpi reset */
	mr(resetc, SRC_MIPIPHY_RCR, RCR_MIPI_DSI_DPI_RESET_N, RCR_MIPI_DSI_DPI_RESET_N);

	/* enable display port bridge */
	bridgeinit(bridge, &mode, &dsi_cfg);

	/* send the pixels */
	lcdifinit(&mode);
	return;

out:
	print("lcdinit: %s\n", err);
}
