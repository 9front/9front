#define STACKTOP 0xFFFFFE00

#define ARM_FDIV (40 << PLL_FDIV_SH)
#define DDR_FDIV (32 << PLL_FDIV_SH)
#define IO_FDIV (30 << PLL_FDIV_SH)
#define PLL_CFG_VAL(CP, RES, CNT) ((CP)<<8 | (RES)<<4 | (CNT)<<12)
#define ARM_PLL_CFG_VAL PLL_CFG_VAL(2, 2, 250)
#define DDR_PLL_CFG_VAL PLL_CFG_VAL(2, 2, 300)
#define IO_PLL_CFG_VAL PLL_CFG_VAL(2, 12, 325)
#define PLL_FDIV_SH 12
#define PLL_BYPASS_FORCE 0x10
#define PLL_RESET 0x01

#define CPU_DIV 2
#define DDR_DIV3 2
#define DDR_DIV2 3
#define UART_DIV 40
#define DCI_DIV0 20
#define DCI_DIV1 5
#define ETH_DIV0 8
#define ETH_DIV1 1
#define QSPI_DIV 5
#define SDIO_DIV 10
#define PCAP_DIV 5
#define MDC_DIV 6 /* this value depends on CPU_1xCLK, see TRM GEM.net_cfg description */

#define SLCR_BASE 0xF8000000
#define SLCR_LOCK 0x004
#define LOCK_KEY 0x767B
#define SLCR_UNLOCK 0x008
#define UNLOCK_KEY 0xDF0D

#define ARM_PLL_CTRL 0x100
#define DDR_PLL_CTRL 0x104
#define IO_PLL_CTRL 0x108
#define PLL_STATUS 0x10C
#define ARM_PLL_CFG 0x110
#define DDR_PLL_CFG 0x114
#define IO_PLL_CFG 0x118
#define ARM_CLK_CTRL 0x120
#define DDR_CLK_CTRL 0x124
#define DCI_CLK_CTRL 0x128
#define APER_CLK_CTRL 0x12C
#define GEM0_RCLK_CTRL 0x138
#define GEM1_RCLK_CTRL 0x13C
#define GEM0_CLK_CTRL 0x140
#define GEM1_CLK_CTRL 0x144
#define SMC_CLK_CTRL 0x148
#define LQSPI_CLK_CTRL 0x14C
#define SDIO_CLK_CTRL 0x150
#define UART_CLK_CTRL 0x154
#define SPI_CLK_CTRL 0x158
#define CAN_CLK_CTRL 0x15C
#define PCAP_CLK_CTRL 0x168
#define UART_RST_CTRL 0x228
#define A9_CPU_RST_CTRL 0x244

#define LQSPI_CLK_EN (1<<23)
#define GPIO_CLK_EN (1<<22)
#define UART0_CLK_EN (1<<20)
#define UART1_CLK_EN (1<<21)
#define I2C0_CLK_EN (1<<18)
#define SDIO1_CLK_EN (1<<11)
#define GEM0_CLK_EN (1<<6)
#define USB1_CLK_EN (1<<3)
#define USB0_CLK_EN (1<<2)
#define DMA_CLK_EN (1<<0)

#define MIO_PIN_0 0x00000700
#define MIO_MST_TRI0 0x80C
#define MIO_MST_TRI1 0x810
#define OCM_CFG 0x910
#define GPIOB_CTRL 0xB00
#define VREF_SW_EN (1<<11)
#define DDRIOB_ADDR0 0xB40
#define DDRIOB_DCI_CTRL 0xB70
#define DDRIOB_DCI_CTRL_MASK 0x1ffc3
#define DDRIOB_DCI_STATUS 0xB74
#define DCI_RESET 1
#define DCI_NREF (1<<11)
#define DCI_ENABLE 2

#define DDR_BASE 0xF8006000
#define DDRC_CTRL 0x0
#define DDR_MODE_STS 0x54

#define UART1_BASE 0xE0001000
#define UART_CTRL 0x0
#define UART_MODE 0x4
#define UART_BAUD 0x18
#define UART_STAT 0x2C
#define UART_DATA 0x30
#define UART_SAMP 0x34

#define QSPI_BASE 0xE000D000
#define QSPI_CFG 0x0
#define SPI_EN 0x4
#define QSPI_TX 0x1c

#define MP_BASE 0xF8F00000
#define FILTER_START 0x40

#define CpMMU 15

#define DSB WORD $0xf57ff04f
#define ISB WORD $0xf57ff06f
#define WFE WORD $0xe320f002
