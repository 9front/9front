#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

static u32int *regs = (u32int*)(VIRTIO + 0x380000);
static u32int *anatop = (u32int*)(VIRTIO + 0x360000);

enum {
	/* input clocks */
	ARM_PLL_CLK = 12,
	GPU_PLL_CLK,
	VPU_PLL_CLK,
	DRAM_PLL1_CLK,
	SYSTEM_PLL1_CLK,
	SYSTEM_PLL1_DIV2,
	SYSTEM_PLL1_DIV3,
	SYSTEM_PLL1_DIV4,
	SYSTEM_PLL1_DIV5,
	SYSTEM_PLL1_DIV6,
	SYSTEM_PLL1_DIV8,
	SYSTEM_PLL1_DIV10,
	SYSTEM_PLL1_DIV20,
	SYSTEM_PLL2_CLK,
	SYSTEM_PLL2_DIV2,
	SYSTEM_PLL2_DIV3,
	SYSTEM_PLL2_DIV4,
	SYSTEM_PLL2_DIV5,
	SYSTEM_PLL2_DIV6,
	SYSTEM_PLL2_DIV8,
	SYSTEM_PLL2_DIV10,
	SYSTEM_PLL2_DIV20,
	SYSTEM_PLL3_CLK,
	AUDIO_PLL1_CLK,
	AUDIO_PLL2_CLK,
	VIDEO_PLL1_CLK,
	VIDEO_PLL2_CLK,

	OSC_32K_REF_CLK,
	OSC_25M_REF_CLK,
	OSC_27M_REF_CLK,
	EXT_CLK_1,
	EXT_CLK_2,
	EXT_CLK_3,
	EXT_CLK_4,

	/* root clocks (slices) */
	ARM_A53_CLK_ROOT = 0,
	ARM_M4_CLK_ROOT = 1,
	VPU_A53_CLK_ROOT = 2,
	GPU_CORE_CLK_ROOT = 3,
	GPU_SHADER_CLK_ROOT = 4,

	MAIN_AXI_CLK_ROOT = 16,
	ENET_AXI_CLK_ROOT = 17,
	NAND_USDHC_BUS_CLK_ROOT = 18,
	VPU_BUS_CLK_ROOT = 19,
	DISPLAY_AXI_CLK_ROOT = 20,
	DISPLAY_APB_CLK_ROOT = 21,
	DISPLAY_RTRM_CLK_ROOT = 22,
	USB_BUS_CLK_ROOT = 23,
	GPU_AXI_CLK_ROOT = 24,
	GPU_AHB_CLK_ROOT = 25,
	NOC_CLK_ROOT = 26,
	NOC_APB_CLK_ROOT = 27,

	AHB_CLK_ROOT = 32,
		IPG_CLK_ROOT = 33,
	AUDIO_AHB_CLK_ROOT = 34,
		AUDIO_IPG_CLK_ROOT = 35,
	MIPI_DSI_ESC_RX_CLK_ROOT = 36,
		MIPI_DSI_ESC_CLK_ROOT = 37,

	DRAM_ALT_CLK_ROOT = 64,
	DRAM_APB_CLK_ROOT = 65,
	VPU_G1_CLK_ROOT = 66,
	VPU_G2_CLK_ROOT = 67,
	DISPLAY_DTRC_CLK_ROOT = 68,
	DISPLAY_DC8000_CLK_ROOT = 69,
	PCIE1_CTRL_CLK_ROOT = 70,
	PCIE1_PHY_CLK_ROOT = 71,
	PCIE1_AUX_CLK_ROOT = 72,
	DC_PIXEL_CLK_ROOT = 73,
	LCDIF_PIXEL_CLK_ROOT = 74,
	SAI1_CLK_ROOT = 75,
	SAI2_CLK_ROOT = 76,
	SAI3_CLK_ROOT = 77,
	SAI4_CLK_ROOT = 78,
	SAI5_CLK_ROOT = 79,
	SAI6_CLK_ROOT = 80,
	SPDIF1_CLK_ROOT = 81,
	SPDIF2_CLK_ROOT = 82,
	ENET_REF_CLK_ROOT = 83,
	ENET_TIMER_CLK_ROOT = 84,
	ENET_PHY_REF_CLK_ROOT = 85,
	NAND_CLK_ROOT = 86,
	QSPI_CLK_ROOT = 87,
	USDHC1_CLK_ROOT = 88,
	USDHC2_CLK_ROOT = 89,
	I2C1_CLK_ROOT = 90,
	I2C2_CLK_ROOT = 91,
	I2C3_CLK_ROOT = 92,
	I2C4_CLK_ROOT = 93,
	UART1_CLK_ROOT = 94,
	UART2_CLK_ROOT = 95,
	UART3_CLK_ROOT = 96,
	UART4_CLK_ROOT = 97,
	USB_CORE_REF_CLK_ROOT = 98,
	USB_PHY_REF_CLK_ROOT = 99,
	GIC_CLK_ROOT = 100,
	ECSPI1_CLK_ROOT = 101,
	ECSPI2_CLK_ROOT = 102,
	PWM1_CLK_ROOT = 103,
	PWM2_CLK_ROOT = 104,
	PWM3_CLK_ROOT = 105,
	PWM4_CLK_ROOT = 106,
	GPT1_CLK_ROOT = 107,
	GPT2_CLK_ROOT = 108,
	GPT3_CLK_ROOT = 109,
	GPT4_CLK_ROOT = 110,
	GPT5_CLK_ROOT = 111,
	GPT6_CLK_ROOT = 112,
	TRACE_CLK_ROOT = 113,
	WDOG_CLK_ROOT = 114,
	WRCLK_CLK_ROOT = 115,
	IPP_DO_CLKO1 = 116,
	IPP_DO_CLKO2 = 117,
	MIPI_DSI_CORE_CLK_ROOT = 118,
	MIPI_DSI_PHY_REF_CLK_ROOT = 119,
	MIPI_DSI_DBI_CLK_ROOT = 120,
	OLD_MIPI_DSI_ESC_CLK_ROOT = 121,
	MIPI_CSI1_CORE_CLK_ROOT = 122,
	MIPI_CSI1_PHY_REF_CLK_ROOT = 123,
	MIPI_CSI1_ESC_CLK_ROOT = 124,
	MIPI_CSI2_CORE_CLK_ROOT = 125,
	MIPI_CSI2_PHY_REF_CLK_ROOT = 126,
	MIPI_CSI2_ESC_CLK_ROOT = 127,
	PCIE2_CTRL_CLK_ROOT = 128,
	PCIE2_PHY_CLK_ROOT = 129,
	PCIE2_AUX_CLK_ROOT = 130,
	ECSPI3_CLK_ROOT = 131,
	OLD_MIPI_DSI_ESC_RX_CLK_ROOT = 132,
	DISPLAY_HDMI_CLK_ROOT = 133,
};

static int input_clk_freq[] = {
	[ARM_PLL_CLK] 1600*Mhz, 
	[GPU_PLL_CLK] 1600*Mhz,
	[VPU_PLL_CLK] 800*Mhz,
	[DRAM_PLL1_CLK] 800*Mhz,
	[SYSTEM_PLL1_CLK] 800*Mhz,
	[SYSTEM_PLL1_DIV2] 400*Mhz,
	[SYSTEM_PLL1_DIV3] 266*Mhz,
	[SYSTEM_PLL1_DIV4] 200*Mhz,
	[SYSTEM_PLL1_DIV5] 160*Mhz,
	[SYSTEM_PLL1_DIV6] 133*Mhz,
	[SYSTEM_PLL1_DIV8] 100*Mhz,
	[SYSTEM_PLL1_DIV10] 80*Mhz,
	[SYSTEM_PLL1_DIV20] 40*Mhz,
	[SYSTEM_PLL2_CLK] 1000*Mhz,
	[SYSTEM_PLL2_DIV2] 500*Mhz,
	[SYSTEM_PLL2_DIV3] 333*Mhz,
	[SYSTEM_PLL2_DIV4] 250*Mhz,
	[SYSTEM_PLL2_DIV5] 200*Mhz,
	[SYSTEM_PLL2_DIV6] 166*Mhz,
	[SYSTEM_PLL2_DIV8] 125*Mhz,
	[SYSTEM_PLL2_DIV10] 100*Mhz,
	[SYSTEM_PLL2_DIV20] 50*Mhz,
	[SYSTEM_PLL3_CLK] 1000*Mhz,
	[AUDIO_PLL1_CLK] 650*Mhz,
	[AUDIO_PLL2_CLK] 650*Mhz,
	[VIDEO_PLL1_CLK] 594*Mhz,
	[VIDEO_PLL2_CLK] 600*Mhz,
	[OSC_32K_REF_CLK] 32000,
	[OSC_25M_REF_CLK] 25*Mhz,
	[OSC_27M_REF_CLK] 27*Mhz,
	[EXT_CLK_1] 133*Mhz,
	[EXT_CLK_2] 133*Mhz,
	[EXT_CLK_3] 133*Mhz,
	[EXT_CLK_4] 133*Mhz,
};

static char *input_clk_name[] = {
	[ARM_PLL_CLK] "arm_pll_clk",
	[GPU_PLL_CLK] "gpu_pll_clk",
	[VPU_PLL_CLK] "vpu_pll_clk",
	[DRAM_PLL1_CLK] "dram_pll1_clk",
	[SYSTEM_PLL1_CLK] "system_pll1_clk",
	[SYSTEM_PLL1_DIV2] "system_pll1_div2",
	[SYSTEM_PLL1_DIV3] "system_pll1_div3",
	[SYSTEM_PLL1_DIV4] "system_pll1_div4",
	[SYSTEM_PLL1_DIV5] "system_pll1_div5",
	[SYSTEM_PLL1_DIV6] "system_pll1_div6",
	[SYSTEM_PLL1_DIV8] "system_pll1_div8",
	[SYSTEM_PLL1_DIV10] "system_pll1_div10",
	[SYSTEM_PLL1_DIV20] "system_pll1_div20",
	[SYSTEM_PLL2_CLK] "system_pll2_clk",
	[SYSTEM_PLL2_DIV2] "system_pll2_div2",
	[SYSTEM_PLL2_DIV3] "system_pll2_div3",
	[SYSTEM_PLL2_DIV4] "system_pll2_div4",
	[SYSTEM_PLL2_DIV5] "system_pll2_div5",
	[SYSTEM_PLL2_DIV6] "system_pll2_div6",
	[SYSTEM_PLL2_DIV8] "system_pll2_div8",
	[SYSTEM_PLL2_DIV10] "system_pll2_div10",
	[SYSTEM_PLL2_DIV20] "system_pll2_div20",
	[SYSTEM_PLL3_CLK] "system_pll3_clk",
	[AUDIO_PLL1_CLK] "audio_pll1_clk",
	[AUDIO_PLL2_CLK] "audio_pll2_clk",
	[VIDEO_PLL1_CLK] "video_pll1_clk",
	[VIDEO_PLL2_CLK] "video_pll2_clk",
	[OSC_32K_REF_CLK] "osc_32k_ref_clk",
	[OSC_25M_REF_CLK] "osc_25m_ref_clk",
	[OSC_27M_REF_CLK] "osc_27m_ref_clk",
	[EXT_CLK_1] "ext_clk_1",
	[EXT_CLK_2] "ext_clk_2",
	[EXT_CLK_3] "ext_clk_3",
	[EXT_CLK_4] "ext_clk_4",
};

static char *root_clk_name[] = {
	[ARM_A53_CLK_ROOT] "ccm_arm_a53_clk_root",
	[ARM_M4_CLK_ROOT] "ccm_arm_m4_clk_root",
	[VPU_A53_CLK_ROOT] "ccm_vpu_a53_clk_root",
	[GPU_CORE_CLK_ROOT] "ccm_gpu_core_clk_root",
	[GPU_SHADER_CLK_ROOT] "ccm_gpu_shader_clk_root",
	[MAIN_AXI_CLK_ROOT] "ccm_main_axi_clk_root",
	[ENET_AXI_CLK_ROOT] "ccm_enet_axi_clk_root",
	[NAND_USDHC_BUS_CLK_ROOT] "ccm_nand_usdhc_bus_clk_root",
	[VPU_BUS_CLK_ROOT] "ccm_vpu_bus_clk_root",
	[DISPLAY_AXI_CLK_ROOT] "ccm_display_axi_clk_root",
	[DISPLAY_APB_CLK_ROOT] "ccm_display_apb_clk_root",
	[DISPLAY_RTRM_CLK_ROOT] "ccm_display_rtrm_clk_root",
	[USB_BUS_CLK_ROOT] "ccm_usb_bus_clk_root",
	[GPU_AXI_CLK_ROOT] "ccm_gpu_axi_clk_root",
	[GPU_AHB_CLK_ROOT] "ccm_gpu_ahb_clk_root",
	[NOC_CLK_ROOT] "ccm_noc_clk_root",
	[NOC_APB_CLK_ROOT] "ccm_noc_apb_clk_root",
	[AHB_CLK_ROOT] "ccm_ahb_clk_root",
	[IPG_CLK_ROOT] "ccm_ipg_clk_root",
	[AUDIO_AHB_CLK_ROOT] "ccm_audio_ahb_clk_root",
	[AUDIO_IPG_CLK_ROOT] "ccm_audio_ipg_clk_root",
	[MIPI_DSI_ESC_RX_CLK_ROOT] "ccm_mipi_dsi_esc_rx_clk_root",
	[MIPI_DSI_ESC_CLK_ROOT] "ccm_mipi_dsi_esc_clk_root",
	[DRAM_ALT_CLK_ROOT] "ccm_dram_alt_clk_root",
	[DRAM_APB_CLK_ROOT] "ccm_dram_apb_clk_root",
	[VPU_G1_CLK_ROOT] "ccm_vpu_g1_clk_root",
	[VPU_G2_CLK_ROOT] "ccm_vpu_g2_clk_root",
	[DISPLAY_DTRC_CLK_ROOT] "ccm_display_dtrc_clk_root",
	[DISPLAY_DC8000_CLK_ROOT] "ccm_display_dc8000_clk_root",
	[PCIE1_CTRL_CLK_ROOT] "ccm_pcie1_ctrl_clk_root",
	[PCIE1_PHY_CLK_ROOT] "ccm_pcie1_phy_clk_root",
	[PCIE1_AUX_CLK_ROOT] "ccm_pcie1_aux_clk_root",
	[DC_PIXEL_CLK_ROOT] "ccm_dc_pixel_clk_root",
	[LCDIF_PIXEL_CLK_ROOT] "ccm_lcdif_pixel_clk_root",
	[SAI1_CLK_ROOT] "ccm_sai1_clk_root",
	[SAI2_CLK_ROOT] "ccm_sai2_clk_root",
	[SAI3_CLK_ROOT] "ccm_sai3_clk_root",
	[SAI4_CLK_ROOT] "ccm_sai4_clk_root",
	[SAI5_CLK_ROOT] "ccm_sai5_clk_root",
	[SAI6_CLK_ROOT] "ccm_sai6_clk_root",
	[SPDIF1_CLK_ROOT] "ccm_spdif1_clk_root",
	[SPDIF2_CLK_ROOT] "ccm_spdif2_clk_root",
	[ENET_REF_CLK_ROOT] "ccm_enet_ref_clk_root",
	[ENET_TIMER_CLK_ROOT] "ccm_enet_timer_clk_root",
	[ENET_PHY_REF_CLK_ROOT] "ccm_enet_phy_ref_clk_root",
	[NAND_CLK_ROOT] "ccm_nand_clk_root",
	[QSPI_CLK_ROOT] "ccm_qspi_clk_root",
	[USDHC1_CLK_ROOT] "ccm_usdhc1_clk_root",
	[USDHC2_CLK_ROOT] "ccm_usdhc2_clk_root",
	[I2C1_CLK_ROOT] "ccm_i2c1_clk_root",
	[I2C2_CLK_ROOT] "ccm_i2c2_clk_root",
	[I2C3_CLK_ROOT] "ccm_i2c3_clk_root",
	[I2C4_CLK_ROOT] "ccm_i2c4_clk_root",
	[UART1_CLK_ROOT] "ccm_uart1_clk_root",
	[UART2_CLK_ROOT] "ccm_uart2_clk_root",
	[UART3_CLK_ROOT] "ccm_uart3_clk_root",
	[UART4_CLK_ROOT] "ccm_uart4_clk_root",
	[USB_CORE_REF_CLK_ROOT] "ccm_usb_core_ref_clk_root",
	[USB_PHY_REF_CLK_ROOT] "ccm_usb_phy_ref_clk_root",
	[GIC_CLK_ROOT] "ccm_gic_clk_root",
	[ECSPI1_CLK_ROOT] "ccm_ecspi1_clk_root",
	[ECSPI2_CLK_ROOT] "ccm_ecspi2_clk_root",
	[PWM1_CLK_ROOT] "ccm_pwm1_clk_root",
	[PWM2_CLK_ROOT] "ccm_pwm2_clk_root",
	[PWM3_CLK_ROOT] "ccm_pwm3_clk_root",
	[PWM4_CLK_ROOT] "ccm_pwm4_clk_root",
	[GPT1_CLK_ROOT] "ccm_gpt1_clk_root",
	[GPT2_CLK_ROOT] "ccm_gpt2_clk_root",
	[GPT3_CLK_ROOT] "ccm_gpt3_clk_root",
	[GPT4_CLK_ROOT] "ccm_gpt4_clk_root",
	[GPT5_CLK_ROOT] "ccm_gpt5_clk_root",
	[GPT6_CLK_ROOT] "ccm_gpt6_clk_root",
	[TRACE_CLK_ROOT] "ccm_trace_clk_root",
	[WDOG_CLK_ROOT] "ccm_wdog_clk_root",
	[WRCLK_CLK_ROOT] "ccm_wrclk_clk_root",
	[IPP_DO_CLKO1] "ccm_ipp_do_clko1",
	[IPP_DO_CLKO2] "ccm_ipp_do_clko2",
	[MIPI_DSI_CORE_CLK_ROOT] "ccm_mipi_dsi_core_clk_root",
	[MIPI_DSI_PHY_REF_CLK_ROOT] "ccm_mipi_dsi_phy_ref_clk_root",
	[MIPI_DSI_DBI_CLK_ROOT] "ccm_mipi_dsi_dbi_clk_root",
	[OLD_MIPI_DSI_ESC_CLK_ROOT] "ccm_old_mipi_dsi_esc_clk_root",
	[MIPI_CSI1_CORE_CLK_ROOT] "ccm_mipi_csi1_core_clk_root",
	[MIPI_CSI1_PHY_REF_CLK_ROOT] "ccm_mipi_csi1_phy_ref_clk_root",
	[MIPI_CSI1_ESC_CLK_ROOT] "ccm_mipi_csi1_esc_clk_root",
	[MIPI_CSI2_CORE_CLK_ROOT] "ccm_mipi_csi2_core_clk_root",
	[MIPI_CSI2_PHY_REF_CLK_ROOT] "ccm_mipi_csi2_phy_ref_clk_root",
	[MIPI_CSI2_ESC_CLK_ROOT] "ccm_mipi_csi2_esc_clk_root",
	[PCIE2_CTRL_CLK_ROOT] "ccm_pcie2_ctrl_clk_root",
	[PCIE2_PHY_CLK_ROOT] "ccm_pcie2_phy_clk_root",
	[PCIE2_AUX_CLK_ROOT] "ccm_pcie2_aux_clk_root",
	[ECSPI3_CLK_ROOT] "ccm_ecspi3_clk_root",
	[OLD_MIPI_DSI_ESC_RX_CLK_ROOT] "ccm_old_mipi_dsi_esc_rx_clk_root",
	[DISPLAY_HDMI_CLK_ROOT] "ccm_display_hdmi_clk_root",
};

static uchar root_clk_input_mux[] = {
[ARM_A53_CLK_ROOT*8]
	OSC_25M_REF_CLK, ARM_PLL_CLK, SYSTEM_PLL2_DIV2, SYSTEM_PLL2_CLK,
	SYSTEM_PLL1_CLK, SYSTEM_PLL1_DIV2, AUDIO_PLL1_CLK, SYSTEM_PLL3_CLK,
[ARM_M4_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV5, SYSTEM_PLL2_DIV4, SYSTEM_PLL1_DIV3,
	SYSTEM_PLL1_CLK, AUDIO_PLL1_CLK, VIDEO_PLL1_CLK, SYSTEM_PLL3_CLK,
[VPU_A53_CLK_ROOT*8]
	OSC_25M_REF_CLK, ARM_PLL_CLK, SYSTEM_PLL2_DIV2, SYSTEM_PLL2_CLK,
	SYSTEM_PLL1_CLK, SYSTEM_PLL1_DIV2, AUDIO_PLL1_CLK, VPU_PLL_CLK,
[GPU_CORE_CLK_ROOT*8]
	OSC_25M_REF_CLK, GPU_PLL_CLK, SYSTEM_PLL1_CLK, SYSTEM_PLL3_CLK,
	SYSTEM_PLL2_CLK, AUDIO_PLL1_CLK, VIDEO_PLL1_CLK, AUDIO_PLL2_CLK,
[GPU_SHADER_CLK_ROOT*8]
	OSC_25M_REF_CLK, GPU_PLL_CLK, SYSTEM_PLL1_CLK, SYSTEM_PLL3_CLK,
	SYSTEM_PLL2_CLK, AUDIO_PLL1_CLK, VIDEO_PLL1_CLK, AUDIO_PLL2_CLK,
[MAIN_AXI_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV3, SYSTEM_PLL1_CLK, SYSTEM_PLL2_DIV4,
	SYSTEM_PLL2_CLK, AUDIO_PLL1_CLK, VIDEO_PLL1_CLK, SYSTEM_PLL1_DIV8,
[ENET_AXI_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV3, SYSTEM_PLL1_CLK, SYSTEM_PLL2_DIV4,
	SYSTEM_PLL2_DIV5, AUDIO_PLL1_CLK, VIDEO_PLL1_CLK, SYSTEM_PLL3_CLK,
[NAND_USDHC_BUS_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV3, SYSTEM_PLL1_CLK, SYSTEM_PLL2_DIV5,
	SYSTEM_PLL1_DIV6, SYSTEM_PLL3_CLK, SYSTEM_PLL2_DIV4, AUDIO_PLL1_CLK,
[VPU_BUS_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_CLK, VPU_PLL_CLK, AUDIO_PLL2_CLK,
	SYSTEM_PLL3_CLK, SYSTEM_PLL2_CLK, SYSTEM_PLL2_DIV5, SYSTEM_PLL1_DIV8,
[DISPLAY_AXI_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV8, SYSTEM_PLL1_CLK, SYSTEM_PLL3_CLK,
	SYSTEM_PLL1_DIV20, AUDIO_PLL2_CLK, EXT_CLK_1, EXT_CLK_4,
[DISPLAY_APB_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV8, SYSTEM_PLL1_CLK, SYSTEM_PLL3_CLK,
	SYSTEM_PLL1_DIV20, AUDIO_PLL2_CLK, EXT_CLK_1, EXT_CLK_3,
[DISPLAY_RTRM_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_CLK, SYSTEM_PLL2_DIV5, SYSTEM_PLL1_DIV2,
	AUDIO_PLL1_CLK, VIDEO_PLL1_CLK, EXT_CLK_2, EXT_CLK_3,
[USB_BUS_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV2, SYSTEM_PLL1_CLK, SYSTEM_PLL2_DIV10,
	SYSTEM_PLL2_DIV5, EXT_CLK_2, EXT_CLK_4, AUDIO_PLL2_CLK,
[GPU_AXI_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_CLK, GPU_PLL_CLK, SYSTEM_PLL3_CLK,
	SYSTEM_PLL2_CLK, AUDIO_PLL1_CLK, VIDEO_PLL1_CLK, AUDIO_PLL2_CLK,
[GPU_AHB_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_CLK, GPU_PLL_CLK, SYSTEM_PLL3_CLK,
	SYSTEM_PLL2_CLK, AUDIO_PLL1_CLK, VIDEO_PLL1_CLK, AUDIO_PLL2_CLK,
[NOC_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_CLK, SYSTEM_PLL3_CLK, SYSTEM_PLL2_CLK,
	SYSTEM_PLL2_DIV2, AUDIO_PLL1_CLK, VIDEO_PLL1_CLK, AUDIO_PLL2_CLK,
[NOC_APB_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV2, SYSTEM_PLL3_CLK, SYSTEM_PLL2_DIV3,
	SYSTEM_PLL2_DIV5, SYSTEM_PLL1_CLK, AUDIO_PLL1_CLK, VIDEO_PLL1_CLK,
[AHB_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV6, SYSTEM_PLL1_CLK, SYSTEM_PLL1_DIV2,
	SYSTEM_PLL2_DIV8, SYSTEM_PLL3_CLK, AUDIO_PLL1_CLK, VIDEO_PLL1_CLK,
[AUDIO_AHB_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV2, SYSTEM_PLL1_CLK, SYSTEM_PLL2_CLK,
	SYSTEM_PLL2_DIV6, SYSTEM_PLL3_CLK, AUDIO_PLL1_CLK, VIDEO_PLL1_CLK,
[MIPI_DSI_ESC_RX_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV10, SYSTEM_PLL1_DIV10, SYSTEM_PLL1_CLK,
	SYSTEM_PLL2_CLK, SYSTEM_PLL3_CLK, EXT_CLK_3, AUDIO_PLL2_CLK,
[DRAM_ALT_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_CLK, SYSTEM_PLL1_DIV8, SYSTEM_PLL2_DIV2,
	SYSTEM_PLL2_DIV4, SYSTEM_PLL1_DIV2, AUDIO_PLL1_CLK, SYSTEM_PLL1_DIV3,
[DRAM_APB_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV5, SYSTEM_PLL1_DIV20, SYSTEM_PLL1_DIV5,
	SYSTEM_PLL1_CLK, SYSTEM_PLL3_CLK, SYSTEM_PLL2_DIV4, AUDIO_PLL2_CLK,
[VPU_G1_CLK_ROOT*8]
	OSC_25M_REF_CLK, VPU_PLL_CLK, SYSTEM_PLL1_CLK, SYSTEM_PLL2_CLK,
	SYSTEM_PLL1_DIV8, SYSTEM_PLL2_DIV8, SYSTEM_PLL3_CLK, AUDIO_PLL1_CLK,
[VPU_G2_CLK_ROOT*8]
	OSC_25M_REF_CLK, VPU_PLL_CLK, SYSTEM_PLL1_CLK, SYSTEM_PLL2_CLK,
	SYSTEM_PLL1_DIV8, SYSTEM_PLL2_DIV8, SYSTEM_PLL3_CLK, AUDIO_PLL1_CLK,
[DISPLAY_DTRC_CLK_ROOT*8]
	OSC_25M_REF_CLK, VIDEO_PLL2_CLK, SYSTEM_PLL1_CLK, SYSTEM_PLL2_CLK,
	SYSTEM_PLL1_DIV5, VIDEO_PLL1_CLK, SYSTEM_PLL3_CLK, AUDIO_PLL2_CLK,
[DISPLAY_DC8000_CLK_ROOT*8]
	OSC_25M_REF_CLK, VIDEO_PLL2_CLK, SYSTEM_PLL1_CLK, SYSTEM_PLL2_CLK,
	SYSTEM_PLL1_DIV5, VIDEO_PLL1_CLK, SYSTEM_PLL3_CLK, AUDIO_PLL2_CLK,
[PCIE1_CTRL_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV4, SYSTEM_PLL2_DIV5, SYSTEM_PLL1_DIV3,
	SYSTEM_PLL1_CLK, SYSTEM_PLL2_DIV2, SYSTEM_PLL2_DIV3, SYSTEM_PLL3_CLK,
[PCIE1_PHY_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV10, SYSTEM_PLL2_DIV2, EXT_CLK_1,
	EXT_CLK_2, EXT_CLK_3, EXT_CLK_4, SYSTEM_PLL1_DIV2,
[PCIE1_AUX_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV5, SYSTEM_PLL2_DIV20, SYSTEM_PLL3_CLK,
	SYSTEM_PLL2_DIV10, SYSTEM_PLL1_DIV10, SYSTEM_PLL1_DIV5, SYSTEM_PLL1_DIV4,
[DC_PIXEL_CLK_ROOT*8]
	OSC_25M_REF_CLK, VIDEO_PLL1_CLK, AUDIO_PLL2_CLK, AUDIO_PLL1_CLK,
	SYSTEM_PLL1_CLK, SYSTEM_PLL2_CLK, SYSTEM_PLL3_CLK, EXT_CLK_4,
[LCDIF_PIXEL_CLK_ROOT*8]
	OSC_25M_REF_CLK, VIDEO_PLL1_CLK, AUDIO_PLL2_CLK, AUDIO_PLL1_CLK,
	SYSTEM_PLL1_CLK, SYSTEM_PLL2_CLK, SYSTEM_PLL3_CLK, EXT_CLK_4,
[SAI1_CLK_ROOT*8]
	OSC_25M_REF_CLK, AUDIO_PLL1_CLK, AUDIO_PLL2_CLK, VIDEO_PLL1_CLK,
	SYSTEM_PLL1_DIV6, OSC_27M_REF_CLK, EXT_CLK_1, EXT_CLK_2,
[SAI2_CLK_ROOT*8]
	OSC_25M_REF_CLK, AUDIO_PLL1_CLK, AUDIO_PLL2_CLK, VIDEO_PLL1_CLK,
	SYSTEM_PLL1_DIV6, OSC_27M_REF_CLK, EXT_CLK_2, EXT_CLK_3,
[SAI3_CLK_ROOT*8]
	OSC_25M_REF_CLK, AUDIO_PLL1_CLK, AUDIO_PLL2_CLK, VIDEO_PLL1_CLK,
	SYSTEM_PLL1_DIV6, OSC_27M_REF_CLK, EXT_CLK_3, EXT_CLK_4,
[SAI4_CLK_ROOT*8]
	OSC_25M_REF_CLK, AUDIO_PLL1_CLK, AUDIO_PLL2_CLK, VIDEO_PLL1_CLK,
	SYSTEM_PLL1_DIV6, OSC_27M_REF_CLK, EXT_CLK_1, EXT_CLK_2,
[SAI5_CLK_ROOT*8]
	OSC_25M_REF_CLK, AUDIO_PLL1_CLK, AUDIO_PLL2_CLK, VIDEO_PLL1_CLK,
	SYSTEM_PLL1_DIV6, OSC_27M_REF_CLK, EXT_CLK_2, EXT_CLK_3,
[SAI6_CLK_ROOT*8]
	OSC_25M_REF_CLK, AUDIO_PLL1_CLK, AUDIO_PLL2_CLK, VIDEO_PLL1_CLK,
	SYSTEM_PLL1_DIV6, OSC_27M_REF_CLK, EXT_CLK_3, EXT_CLK_4,
[SPDIF1_CLK_ROOT*8]
	OSC_25M_REF_CLK, AUDIO_PLL1_CLK, AUDIO_PLL2_CLK, VIDEO_PLL1_CLK,
	SYSTEM_PLL1_DIV6, OSC_27M_REF_CLK, EXT_CLK_2, EXT_CLK_3,
[SPDIF2_CLK_ROOT*8]
	OSC_25M_REF_CLK, AUDIO_PLL1_CLK, AUDIO_PLL2_CLK, VIDEO_PLL1_CLK,
	SYSTEM_PLL1_DIV6, OSC_27M_REF_CLK, EXT_CLK_3, EXT_CLK_4,
[ENET_REF_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV8, SYSTEM_PLL2_DIV20, SYSTEM_PLL2_DIV10,
	SYSTEM_PLL1_DIV5, AUDIO_PLL1_CLK, VIDEO_PLL1_CLK, EXT_CLK_4,
[ENET_TIMER_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV10, AUDIO_PLL1_CLK, EXT_CLK_1,
	EXT_CLK_2, EXT_CLK_3, EXT_CLK_4, VIDEO_PLL1_CLK,
[ENET_PHY_REF_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV20, SYSTEM_PLL2_DIV8, SYSTEM_PLL2_DIV5,
	SYSTEM_PLL2_DIV2, AUDIO_PLL1_CLK, VIDEO_PLL1_CLK, AUDIO_PLL2_CLK,
[NAND_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV2, AUDIO_PLL1_CLK, SYSTEM_PLL1_DIV2,
	AUDIO_PLL2_CLK, SYSTEM_PLL3_CLK, SYSTEM_PLL2_DIV4, VIDEO_PLL1_CLK,
[QSPI_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV2, SYSTEM_PLL1_CLK, SYSTEM_PLL2_DIV2,
	AUDIO_PLL2_CLK, SYSTEM_PLL1_DIV3, SYSTEM_PLL3_CLK, SYSTEM_PLL1_DIV8,
[USDHC1_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV2, SYSTEM_PLL1_CLK, SYSTEM_PLL2_DIV2,
	SYSTEM_PLL3_CLK, SYSTEM_PLL1_DIV3, AUDIO_PLL2_CLK, SYSTEM_PLL1_DIV8,
[USDHC2_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV2, SYSTEM_PLL1_CLK, SYSTEM_PLL2_DIV2,
	SYSTEM_PLL3_CLK, SYSTEM_PLL1_DIV3, AUDIO_PLL2_CLK, SYSTEM_PLL1_DIV8,
[I2C1_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV5, SYSTEM_PLL2_DIV20, SYSTEM_PLL3_CLK,
	AUDIO_PLL1_CLK, VIDEO_PLL1_CLK, AUDIO_PLL2_CLK, SYSTEM_PLL1_DIV6,
[I2C2_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV5, SYSTEM_PLL2_DIV20, SYSTEM_PLL3_CLK,
	AUDIO_PLL1_CLK, VIDEO_PLL1_CLK, AUDIO_PLL2_CLK, SYSTEM_PLL1_DIV6,
[I2C3_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV5, SYSTEM_PLL2_DIV20, SYSTEM_PLL3_CLK,
	AUDIO_PLL1_CLK, VIDEO_PLL1_CLK, AUDIO_PLL2_CLK, SYSTEM_PLL1_DIV6,
[I2C4_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV5, SYSTEM_PLL2_DIV20, SYSTEM_PLL3_CLK,
	AUDIO_PLL1_CLK, VIDEO_PLL1_CLK, AUDIO_PLL2_CLK, SYSTEM_PLL1_DIV6,
[UART1_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV10, SYSTEM_PLL2_DIV5, SYSTEM_PLL2_DIV10,
	SYSTEM_PLL3_CLK, EXT_CLK_2, EXT_CLK_4, AUDIO_PLL2_CLK,
[UART2_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV10, SYSTEM_PLL2_DIV5, SYSTEM_PLL2_DIV10,
	SYSTEM_PLL3_CLK, EXT_CLK_2, EXT_CLK_3, AUDIO_PLL2_CLK,
[UART3_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV10, SYSTEM_PLL2_DIV5, SYSTEM_PLL2_DIV10,
	SYSTEM_PLL3_CLK, EXT_CLK_2, EXT_CLK_4, AUDIO_PLL2_CLK,
[UART4_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV10, SYSTEM_PLL2_DIV5, SYSTEM_PLL2_DIV10,
	SYSTEM_PLL3_CLK, EXT_CLK_2, EXT_CLK_3, AUDIO_PLL2_CLK,
[USB_CORE_REF_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV8, SYSTEM_PLL1_DIV20, SYSTEM_PLL2_DIV10,
	SYSTEM_PLL2_DIV5, EXT_CLK_2, EXT_CLK_3, AUDIO_PLL2_CLK,
[USB_PHY_REF_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV8, SYSTEM_PLL1_DIV20, SYSTEM_PLL2_DIV10,
	SYSTEM_PLL2_DIV5, EXT_CLK_2, EXT_CLK_3, AUDIO_PLL2_CLK,
[GIC_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV5, SYSTEM_PLL1_DIV20, SYSTEM_PLL2_DIV10,
	SYSTEM_PLL1_CLK, EXT_CLK_2, EXT_CLK_4, AUDIO_PLL2_CLK,
[ECSPI1_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV5, SYSTEM_PLL1_DIV20, SYSTEM_PLL1_DIV5,
	SYSTEM_PLL1_CLK, SYSTEM_PLL3_CLK, SYSTEM_PLL2_DIV4, AUDIO_PLL2_CLK,
[ECSPI2_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV5, SYSTEM_PLL1_DIV20, SYSTEM_PLL1_DIV5,
	SYSTEM_PLL1_CLK, SYSTEM_PLL3_CLK, SYSTEM_PLL2_DIV4, AUDIO_PLL2_CLK,
[PWM1_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV10, SYSTEM_PLL1_DIV5, SYSTEM_PLL1_DIV20,
	SYSTEM_PLL3_CLK, EXT_CLK_1, SYSTEM_PLL1_DIV10, VIDEO_PLL1_CLK,
[PWM2_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV10, SYSTEM_PLL1_DIV5, SYSTEM_PLL1_DIV20,
	SYSTEM_PLL3_CLK, EXT_CLK_1, SYSTEM_PLL1_DIV10, VIDEO_PLL1_CLK,
[PWM3_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV10, SYSTEM_PLL1_DIV5, SYSTEM_PLL1_DIV20,
	SYSTEM_PLL3_CLK, EXT_CLK_2, SYSTEM_PLL1_DIV10, VIDEO_PLL1_CLK,
[PWM4_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV10, SYSTEM_PLL1_DIV5, SYSTEM_PLL1_DIV20,
	SYSTEM_PLL3_CLK, EXT_CLK_2, SYSTEM_PLL1_DIV10, VIDEO_PLL1_CLK,
[GPT1_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV10, SYSTEM_PLL1_DIV2, SYSTEM_PLL1_DIV20,
	VIDEO_PLL1_CLK, SYSTEM_PLL1_DIV10, AUDIO_PLL1_CLK, EXT_CLK_1,
[GPT2_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV10, SYSTEM_PLL1_DIV2, SYSTEM_PLL1_DIV20,
	VIDEO_PLL1_CLK, SYSTEM_PLL1_DIV10, AUDIO_PLL1_CLK, EXT_CLK_2,
[GPT3_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV10, SYSTEM_PLL1_DIV2, SYSTEM_PLL1_DIV20,
	VIDEO_PLL1_CLK, SYSTEM_PLL1_DIV10, AUDIO_PLL1_CLK, EXT_CLK_3,
[GPT4_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV10, SYSTEM_PLL1_DIV2, SYSTEM_PLL1_DIV20,
	VIDEO_PLL1_CLK, SYSTEM_PLL1_DIV10, AUDIO_PLL1_CLK, EXT_CLK_1,
[GPT5_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV10, SYSTEM_PLL1_DIV2, SYSTEM_PLL1_DIV20,
	VIDEO_PLL1_CLK, SYSTEM_PLL1_DIV10, AUDIO_PLL1_CLK, EXT_CLK_2,
[GPT6_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV10, SYSTEM_PLL1_DIV2, SYSTEM_PLL1_DIV20,
	VIDEO_PLL1_CLK, SYSTEM_PLL1_DIV10, AUDIO_PLL1_CLK, EXT_CLK_3,
[TRACE_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV6, SYSTEM_PLL1_DIV5, VPU_PLL_CLK,
	SYSTEM_PLL2_DIV8, SYSTEM_PLL3_CLK, EXT_CLK_1, EXT_CLK_3,
[WDOG_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV6, SYSTEM_PLL1_DIV5, VPU_PLL_CLK,
	SYSTEM_PLL2_DIV8, SYSTEM_PLL3_CLK, SYSTEM_PLL1_DIV10, SYSTEM_PLL2_DIV6,
[WRCLK_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV20, VPU_PLL_CLK, SYSTEM_PLL3_CLK,
	SYSTEM_PLL2_DIV5, SYSTEM_PLL1_DIV3, SYSTEM_PLL2_DIV2, SYSTEM_PLL1_DIV8,
[IPP_DO_CLKO1*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_CLK, OSC_27M_REF_CLK, SYSTEM_PLL1_DIV4,
	AUDIO_PLL2_CLK, SYSTEM_PLL2_DIV2, VPU_PLL_CLK, SYSTEM_PLL1_DIV10,
[IPP_DO_CLKO2*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV5, SYSTEM_PLL1_DIV2, SYSTEM_PLL2_DIV6,
	SYSTEM_PLL3_CLK, AUDIO_PLL1_CLK, VIDEO_PLL1_CLK, OSC_32K_REF_CLK,
[MIPI_DSI_CORE_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV3, SYSTEM_PLL2_DIV4, SYSTEM_PLL1_CLK,
	SYSTEM_PLL2_CLK, SYSTEM_PLL3_CLK, AUDIO_PLL2_CLK, VIDEO_PLL1_CLK,
[MIPI_DSI_PHY_REF_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV8, SYSTEM_PLL2_DIV10, SYSTEM_PLL1_CLK,
	SYSTEM_PLL2_CLK, EXT_CLK_2, AUDIO_PLL2_CLK, VIDEO_PLL1_CLK,
[MIPI_DSI_DBI_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV3, SYSTEM_PLL2_DIV10, SYSTEM_PLL1_CLK,
	SYSTEM_PLL2_CLK, SYSTEM_PLL3_CLK, AUDIO_PLL2_CLK, VIDEO_PLL1_CLK,
[MIPI_CSI1_CORE_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV3, SYSTEM_PLL2_DIV4, SYSTEM_PLL1_CLK,
	SYSTEM_PLL2_CLK, SYSTEM_PLL3_CLK, AUDIO_PLL2_CLK, VIDEO_PLL1_CLK,
[MIPI_CSI1_PHY_REF_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV3, SYSTEM_PLL2_DIV10, SYSTEM_PLL1_CLK,
	SYSTEM_PLL2_CLK, EXT_CLK_2, AUDIO_PLL2_CLK, VIDEO_PLL1_CLK,
[MIPI_CSI1_ESC_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV10, SYSTEM_PLL1_DIV10, SYSTEM_PLL1_CLK,
	SYSTEM_PLL2_CLK, SYSTEM_PLL3_CLK, EXT_CLK_3, AUDIO_PLL2_CLK,
[MIPI_CSI2_CORE_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV3, SYSTEM_PLL2_DIV4, SYSTEM_PLL1_CLK,
	SYSTEM_PLL2_CLK, SYSTEM_PLL3_CLK, AUDIO_PLL2_CLK, VIDEO_PLL1_CLK,
[MIPI_CSI2_PHY_REF_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV3, SYSTEM_PLL2_DIV10, SYSTEM_PLL1_CLK,
	SYSTEM_PLL2_CLK, EXT_CLK_2, AUDIO_PLL2_CLK, VIDEO_PLL1_CLK,
[MIPI_CSI2_ESC_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV10, SYSTEM_PLL1_DIV10, SYSTEM_PLL1_CLK,
	SYSTEM_PLL2_CLK, SYSTEM_PLL3_CLK, EXT_CLK_3, AUDIO_PLL2_CLK,
[PCIE2_CTRL_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV4, SYSTEM_PLL2_DIV5, SYSTEM_PLL1_DIV3,
	SYSTEM_PLL1_CLK, SYSTEM_PLL2_DIV2, SYSTEM_PLL2_DIV3, SYSTEM_PLL3_CLK,
[PCIE2_PHY_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV10, SYSTEM_PLL2_DIV2, EXT_CLK_1,
	EXT_CLK_2, EXT_CLK_3, EXT_CLK_4, SYSTEM_PLL1_DIV2,
[PCIE2_AUX_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV5, SYSTEM_PLL2_DIV20, SYSTEM_PLL3_CLK,
	SYSTEM_PLL2_DIV10, SYSTEM_PLL1_DIV10, SYSTEM_PLL1_DIV5, SYSTEM_PLL1_DIV4,
[ECSPI3_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV5, SYSTEM_PLL1_DIV20, SYSTEM_PLL1_DIV5,
	SYSTEM_PLL1_CLK, SYSTEM_PLL3_CLK, SYSTEM_PLL2_DIV4, AUDIO_PLL2_CLK,
[OLD_MIPI_DSI_ESC_RX_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL2_DIV10, SYSTEM_PLL1_DIV10, SYSTEM_PLL1_CLK,
	SYSTEM_PLL2_CLK, SYSTEM_PLL3_CLK, EXT_CLK_3, AUDIO_PLL2_CLK,
[DISPLAY_HDMI_CLK_ROOT*8]
	OSC_25M_REF_CLK, SYSTEM_PLL1_DIV4, SYSTEM_PLL2_DIV5, VPU_PLL_CLK,
	SYSTEM_PLL1_CLK, SYSTEM_PLL2_CLK, SYSTEM_PLL3_CLK, EXT_CLK_4,
};

typedef struct Clock Clock;
struct Clock {
	char	*name;	/* clock instance name */
	int	root;	/* root clock slice */
	int	ccgr;	/* clock gating register */
};

static Clock clocks[] = {
	{ "aips_tz1.hclk", AHB_CLK_ROOT, 28 },
	{ "ipmux1.master_clk", AHB_CLK_ROOT, 28 },
	{ "ipmux1.slave_clk", IPG_CLK_ROOT, 28 },

	{ "aips_tz2.hclk", AHB_CLK_ROOT, 29 },
	{ "ipmux2.master_clk", AHB_CLK_ROOT, 29 },
	{ "ipmux2.slave_clk", AHB_CLK_ROOT, 29 },

	{ "aips_tz3.hclk", AHB_CLK_ROOT, 30 },
	{ "ipmux3.master_clk", AHB_CLK_ROOT, 30 },
	{ "ipmux3.slave_clk", IPG_CLK_ROOT, 30 },

	{ "apbhdma.hclk", NAND_USDHC_BUS_CLK_ROOT, 48 },
	{ "apdhdma_sec.mst_hclk", NAND_USDHC_BUS_CLK_ROOT, 48 },
	{ "rawnand.u_bch_input_apb_clk", NAND_USDHC_BUS_CLK_ROOT, 48 },
	{ "u_bch_input_apb_clk", NAND_USDHC_BUS_CLK_ROOT, 48 },
	{ "rawnand.u_gpmi_bch_input_gpmi_io_clk", NAND_CLK_ROOT, 48 },
	{ "rawnand.U_gpmi_input_apb_clk", NAND_USDHC_BUS_CLK_ROOT, 48 },

	{ "caam.aclk", AHB_CLK_ROOT },
	{ "caam.ipg_clk", IPG_CLK_ROOT },
	{ "caam.ipg_clk_s", IPG_CLK_ROOT },
	{ "caam_exsc.aclk_exsc", AHB_CLK_ROOT },
	{ "caam_mem.clk", AHB_CLK_ROOT },

	{ "cm4.cm4_cti_clk", ARM_M4_CLK_ROOT },
	{ "cm4.cm4_fclk", ARM_M4_CLK_ROOT },
	{ "cm4.cm4_hclk", ARM_M4_CLK_ROOT },
	{ "cm4.dap_clk", AHB_CLK_ROOT },
	{ "cm4.ipg_clk_nic", ARM_M4_CLK_ROOT },
	{ "cm4.tcmc_hclk", ARM_M4_CLK_ROOT },
	{ "cm4_mem.tcmc_hclk", ARM_M4_CLK_ROOT },
	{ "cm4_sec.ipg_clk", IPG_CLK_ROOT },
	{ "cm4_sec.ipg_clk_s", IPG_CLK_ROOT },
	{ "cm4_sec.mst_hclk", ARM_M4_CLK_ROOT },

	{ "csi2_1.clk_vid", MIPI_CSI1_PHY_REF_CLK_ROOT },
	{ "csi2_1.clk", MIPI_CSI1_CORE_CLK_ROOT, 101},
	{ "csi2_1.clk_esc", MIPI_CSI1_ESC_CLK_ROOT, 101},
	{ "csi2_1.pclk", MIPI_CSI1_CORE_CLK_ROOT },
	{ "csi2_1.clk_ui", MIPI_CSI1_PHY_REF_CLK_ROOT, 101},

	{ "csi2_2.clk_vid", MIPI_CSI2_PHY_REF_CLK_ROOT },
	{ "csi2_2.clk", MIPI_CSI2_CORE_CLK_ROOT, 102 },
	{ "csi2_2.clk_esc", MIPI_CSI2_ESC_CLK_ROOT, 102 },
	{ "csi2_2.pclk", MIPI_CSI2_CORE_CLK_ROOT },
	{ "csi2_2.clk_ui", MIPI_CSI2_PHY_REF_CLK_ROOT, 102 },

	{ "csu.ipg_clk_s", IPG_CLK_ROOT, 3},

	{ "dap.dapclk_2_2", AHB_CLK_ROOT, 4},

	{ "ecspi1.ipg_clk", IPG_CLK_ROOT, 7},
	{ "ecspi1.ipg_clk_per", ECSPI1_CLK_ROOT, 7},
	{ "ecspi1.ipg_clk_s", IPG_CLK_ROOT, 7},

	{ "ecspi2.ipg_clk", IPG_CLK_ROOT, 8},
	{ "ecspi2.ipg_clk_per", ECSPI2_CLK_ROOT, 8},
	{ "ecspi2.ipg_clk_s", IPG_CLK_ROOT, 8},

	{ "ecspi2.ipg_clk", IPG_CLK_ROOT, 8},
	{ "ecspi2.ipg_clk_per", ECSPI2_CLK_ROOT, 8},
	{ "ecspi2.ipg_clk_s", IPG_CLK_ROOT, 8},

	{ "ecspi3.ipg_clk", IPG_CLK_ROOT, 9},
	{ "ecspi3.ipg_clk_per", ECSPI3_CLK_ROOT, 9},
	{ "ecspi3.ipg_clk_s", IPG_CLK_ROOT, 9},

	{ "enet1.ipp_ind_mac0_txclk", ENET_REF_CLK_ROOT, 10 },
	{ "enet1.ipg_clk", ENET_AXI_CLK_ROOT, 10 },
	{ "enet1.ipg_clk_mac0", ENET_AXI_CLK_ROOT, 10 },
	{ "enet1.ipg_clk_mac0_s", ENET_AXI_CLK_ROOT, 10 },
	{ "enet1.ipg_clk_s", ENET_AXI_CLK_ROOT, 10 },
	{ "enet1.ipg_clk_time", ENET_TIMER_CLK_ROOT, 10 },
	{ "enet1.mem.mac0_rxmem_clk", ENET_AXI_CLK_ROOT, 10 },
	{ "enet1.mem.mac0_txmem_clk", ENET_AXI_CLK_ROOT, 10 },

	{ "gpio1.ipg_clk_s", IPG_CLK_ROOT, 11 },
	{ "gpio2.ipg_clk_s", IPG_CLK_ROOT, 12 },
	{ "gpio3.ipg_clk_s", IPG_CLK_ROOT, 13 },
	{ "gpio4.ipg_clk_s", IPG_CLK_ROOT, 14 },
	{ "gpio5.ipg_clk_s", IPG_CLK_ROOT, 15 },

	{ "gpt1.ipg_clk", GPT1_CLK_ROOT, 16 },
	{ "gpt1.ipg_clk_highfreq", GPT1_CLK_ROOT, 16 },
	{ "gpt1.ipg_clk_s", GPT1_CLK_ROOT, 16 },

	{ "gpt2.ipg_clk", GPT2_CLK_ROOT, 17 },
	{ "gpt2.ipg_clk_highfreq", GPT2_CLK_ROOT, 17 },
	{ "gpt2.ipg_clk_s", GPT2_CLK_ROOT, 17 },

	{ "gpt3.ipg_clk", GPT3_CLK_ROOT, 18 },
	{ "gpt3.ipg_clk_highfreq", GPT3_CLK_ROOT, 18 },
	{ "gpt3.ipg_clk_s", GPT3_CLK_ROOT, 18 },

	{ "gpt4.ipg_clk", GPT4_CLK_ROOT, 19 },
	{ "gpt4.ipg_clk_highfreq", GPT4_CLK_ROOT, 19 },
	{ "gpt4.ipg_clk_s", GPT4_CLK_ROOT, 19 },

	{ "gpt5.ipg_clk", GPT5_CLK_ROOT, 20 },
	{ "gpt5.ipg_clk_highfreq", GPT5_CLK_ROOT, 20 },
	{ "gpt5.ipg_clk_s", GPT5_CLK_ROOT, 20 },

	{ "gpt6.ipg_clk", GPT6_CLK_ROOT, 21 },
	{ "gpt6.ipg_clk_highfreq", GPT6_CLK_ROOT, 21 },
	{ "gpt6.ipg_clk_s", GPT6_CLK_ROOT, 21 },

	{ "i2c1.ipg_clk_patref", I2C1_CLK_ROOT, 23 },
	{ "i2c1.iph_clk_s", I2C1_CLK_ROOT, 23 },

	{ "i2c2.ipg_clk_patref", I2C2_CLK_ROOT, 24 },
	{ "i2c2.iph_clk_s", I2C2_CLK_ROOT, 24 },

	{ "i2c3.ipg_clk_patref", I2C3_CLK_ROOT, 25 },
	{ "i2c3.iph_clk_s", I2C3_CLK_ROOT, 25 },

	{ "i2c4.ipg_clk_patref", I2C4_CLK_ROOT, 26 },
	{ "i2c4.iph_clk_s", I2C4_CLK_ROOT, 26 },

	{ "iomuxc.ipg_clk_s", IPG_CLK_ROOT, 27 },
	{ "iomuxc_gpr.ipg_clk_s", IPG_CLK_ROOT, 27 },
	{ "iomux.ipt_clk_io", IPG_CLK_ROOT, 27 },

	{ "lcdif.pix_clk", LCDIF_PIXEL_CLK_ROOT },
	{ "lcdif.apb_clk", MAIN_AXI_CLK_ROOT },

	{ "disp.apb_clk", DISPLAY_APB_CLK_ROOT, 93 },
	{ "disp.axi_clk", DISPLAY_AXI_CLK_ROOT, 93 },
	{ "disp.rtrm_clk", DISPLAY_RTRM_CLK_ROOT, 93 },
	{ "disp.dc8000_clk", DISPLAY_DC8000_CLK_ROOT, 93 },
	{ "disp.dtrc_clk", DISPLAY_DTRC_CLK_ROOT },

	{ "mipi.CLKREF", MIPI_DSI_PHY_REF_CLK_ROOT },
	{ "mipi.pclk", MAIN_AXI_CLK_ROOT },
	{ "mipi.RxClkEsc", MIPI_DSI_ESC_RX_CLK_ROOT },
	{ "mipi.TxClkEsc", MIPI_DSI_ESC_CLK_ROOT },
	{ "mipi.core", MIPI_DSI_CORE_CLK_ROOT },
	{ "mipi.ahb", MIPI_DSI_ESC_RX_CLK_ROOT },

	{ "mu.ipg_clk_dsp", IPG_CLK_ROOT, 33 },
	{ "mu.ipg_clk_mcu", IPG_CLK_ROOT, 33 },
	{ "mu.ipg_clk_s_dsp", IPG_CLK_ROOT, 33 },
	{ "mu.ipg_clk_s_mcu", IPG_CLK_ROOT, 33 },

	{ "ocotp.ipg_clk", IPG_CLK_ROOT, 34 },
	{ "ocotp.ipg_clk_s", IPG_CLK_ROOT, 34 },

	{ "ocram_ctrl.clk", MAIN_AXI_CLK_ROOT, 35 },
	{ "ocram_excs.aclk_exsc", MAIN_AXI_CLK_ROOT, 35 },
	{ "ocram_exsc.ipg_clk", IPG_CLK_ROOT, 35 },
	{ "ocram_mem.clk", MAIN_AXI_CLK_ROOT, 35 },

	{ "ocram_ctrl_s.clk", AHB_CLK_ROOT, 36 },
	{ "ocram_s_exsc.aclk_exsc", AHB_CLK_ROOT, 36 },
	{ "ocram_s_exsc.ipg_clk", IPG_CLK_ROOT, 36 },
	{ "ocram_s.mem_clk", AHB_CLK_ROOT, 36 },

	{ "pcie_clk_rst.auxclk", PCIE1_AUX_CLK_ROOT, 37 },
	{ "pcie_clk_rst.mstr_axi_clk", MAIN_AXI_CLK_ROOT, 37 },
	{ "pcie_clk_rst.slv_axi_clk", MAIN_AXI_CLK_ROOT, 37 },
	{ "pcie_ctrl.mstr_aclk", MAIN_AXI_CLK_ROOT, 37 },
	{ "pcie_ctrl.slv_aclk", MAIN_AXI_CLK_ROOT, 37 },
	{ "pcie_exsc.aclk_exsc", MAIN_AXI_CLK_ROOT, 37 },
	{ "pcie_exsc.ipg_clk", IPG_CLK_ROOT, 37 },
	{ "pcie_mem.mstr_axi_clk", MAIN_AXI_CLK_ROOT, 37 },
	{ "pcie_mem.slv_axi_clk", MAIN_AXI_CLK_ROOT, 37 },

	{ "tmu.clk", IPG_CLK_ROOT, 98 },

	{ "pcie2_clk_rst.auxclk", PCIE2_AUX_CLK_ROOT, 100 },
	{ "pcie2_clk_rst.mstr_axi_clk", MAIN_AXI_CLK_ROOT, 100 },
	{ "pcie2_clk_rst.slv_axi_clk", MAIN_AXI_CLK_ROOT, 100 },
	{ "pcie2_ctrl.mstr_aclk", MAIN_AXI_CLK_ROOT, 100 },
	{ "pcie2_ctrl.slv_aclk", MAIN_AXI_CLK_ROOT, 100 },
	{ "pcie2_exsc.aclk_exsc", MAIN_AXI_CLK_ROOT, 100 },
	{ "pcie2_exsc.ipg_clk", IPG_CLK_ROOT, 100 },
	{ "pcie2_mem.mstr_axi_clk", MAIN_AXI_CLK_ROOT, 100 },
	{ "pcie2_mem.slv_axi_clk", MAIN_AXI_CLK_ROOT, 100 },

	{ "pcie_phy.ref_alt_clk_p", PCIE1_PHY_CLK_ROOT },
	{ "pcie2_phy.ref_alt_clk_p", PCIE2_PHY_CLK_ROOT },

	{ "perfmon1.apb_clk", IPG_CLK_ROOT, 38 },
	{ "perfmon1.axi0_ACLK", MAIN_AXI_CLK_ROOT, 38 },

	{ "perfmon2.apb_clk", IPG_CLK_ROOT, 39 },
	{ "perfmon1.axi0_ACLK", MAIN_AXI_CLK_ROOT, 39 },

	{ "pwm1.ipg_clk", PWM1_CLK_ROOT, 40 },
	{ "pwm1.ipg_clk_high_freq", PWM1_CLK_ROOT, 40 },
	{ "pwm1.ipg_clk_s", PWM1_CLK_ROOT, 40 },

	{ "pwm2.ipg_clk", PWM2_CLK_ROOT, 41 },
	{ "pwm2.ipg_clk_high_freq", PWM2_CLK_ROOT, 41 },
	{ "pwm2.ipg_clk_s", PWM2_CLK_ROOT, 41 },

	{ "pwm3.ipg_clk", PWM3_CLK_ROOT, 42 },
	{ "pwm3.ipg_clk_high_freq", PWM3_CLK_ROOT, 42 },
	{ "pwm3.ipg_clk_s", PWM3_CLK_ROOT, 42 },
	
	{ "pwm4.ipg_clk", PWM4_CLK_ROOT, 43 },
	{ "pwm4.ipg_clk_high_freq", PWM4_CLK_ROOT, 43 },
	{ "pwm4.ipg_clk_s", PWM4_CLK_ROOT, 43 },

	{ "qspi.ahb_clk", AHB_CLK_ROOT, 47 },
	{ "qspi.ipg_clk", IPG_CLK_ROOT, 47 },
	{ "qspi.ipg_clk_4xsfif", QSPI_CLK_ROOT, 47 },
	{ "qspi.ipg_clk_s", IPG_CLK_ROOT, 47 },
	{ "qspi_sec.ipg_clk", IPG_CLK_ROOT, 47 },
	{ "qspi_sec.ipg_clk_s", IPG_CLK_ROOT, 47 },
	{ "qspi_sec.mst_hclk", AHB_CLK_ROOT, 47 },

	{ "rdc.ipg_clk_s", IPG_CLK_ROOT, 49 },
	{ "rdc.ipg_clk", IPG_CLK_ROOT, 49 },
	{ "rdc_mem.ipg_clk", IPG_CLK_ROOT, 49 },

	{ "romcp.hclk", AHB_CLK_ROOT, 50 },
	{ "romcp.hclk_reg", IPG_CLK_ROOT, 50 },
	{ "romcp_mem.rom_CLK", AHB_CLK_ROOT, 50 },
	{ "romcp_sec.mst_hclk", AHB_CLK_ROOT, 50 },

	{ "sai1.ipg_clk", AUDIO_IPG_CLK_ROOT, 51 },
	{ "sai1.ipg_clk_s", AUDIO_IPG_CLK_ROOT, 51 },
	{ "sai1.ipg_clk_sai_mclk_1", SAI1_CLK_ROOT, 51 },
	{ "sai1.ipt_clk_sai_bclk", SAI1_CLK_ROOT, 51 },
	{ "sai1.ipt_clk_sai_bclk_b", SAI1_CLK_ROOT, 51 },

	{ "sai2.ipg_clk", AUDIO_IPG_CLK_ROOT, 52 },
	{ "sai2.ipg_clk_s", AUDIO_IPG_CLK_ROOT, 52 },
	{ "sai2.ipg_clk_sai_mclk_1", SAI2_CLK_ROOT, 52 },
	{ "sai2.ipt_clk_sai_bclk", SAI2_CLK_ROOT, 52 },
	{ "sai2.ipt_clk_sai_bclk_b", SAI2_CLK_ROOT, 52 },

	{ "sai3.ipg_clk", AUDIO_IPG_CLK_ROOT, 53 },
	{ "sai3.ipg_clk_s", AUDIO_IPG_CLK_ROOT, 53 },
	{ "sai3.ipg_clk_sai_mclk_1", SAI3_CLK_ROOT, 53 },
	{ "sai3.ipt_clk_sai_bclk", SAI3_CLK_ROOT, 53 },
	{ "sai3.ipt_clk_sai_bclk_b", SAI3_CLK_ROOT, 53 },

	{ "sai4.ipg_clk", AUDIO_IPG_CLK_ROOT, 54 },
	{ "sai4.ipg_clk_s", AUDIO_IPG_CLK_ROOT, 54 },
	{ "sai4.ipg_clk_sai_mclk_1", SAI4_CLK_ROOT, 54 },
	{ "sai4.ipt_clk_sai_bclk", SAI4_CLK_ROOT, 54 },
	{ "sai4.ipt_clk_sai_bclk_b", SAI4_CLK_ROOT, 54 },

	{ "sai5.ipg_clk", AUDIO_IPG_CLK_ROOT, 55 },
	{ "sai5.ipg_clk_s", AUDIO_IPG_CLK_ROOT, 55 },
	{ "sai5.ipg_clk_sai_mclk_1", SAI5_CLK_ROOT, 55 },
	{ "sai5.ipt_clk_sai_bclk", SAI5_CLK_ROOT, 55 },
	{ "sai5.ipt_clk_sai_bclk_b", SAI5_CLK_ROOT, 55 },

	{ "sai6.ipg_clk", AUDIO_IPG_CLK_ROOT, 56 },
	{ "sai6.ipg_clk_s", AUDIO_IPG_CLK_ROOT, 56 },
	{ "sai6.ipg_clk_sai_mclk_1", SAI6_CLK_ROOT, 56 },
	{ "sai6.ipt_clk_sai_bclk", SAI6_CLK_ROOT, 56 },
	{ "sai6.ipt_clk_sai_bclk_b", SAI6_CLK_ROOT, 56 },

	{ "sctr.ipg_clk", IPG_CLK_ROOT, 57 },
	{ "sctr.ipg_clk_s", IPG_CLK_ROOT, 57 },

	{ "sdma1.ips_hostctrl_clk", IPG_CLK_ROOT, 58 },
	{ "sdma1.sdma_ap_ahb_clk", AHB_CLK_ROOT, 58 },
	{ "sdma1.sdma_core_clk", IPG_CLK_ROOT, 58 },

	{ "sdma2.ips_hostctrl_clk", AUDIO_IPG_CLK_ROOT, 59 },
	{ "sdma2.sdma_ap_ahb_clk", AUDIO_AHB_CLK_ROOT, 59 },
	{ "sdma2.sdma_core_clk", AUDIO_IPG_CLK_ROOT, 59 },

	{ "sec_wrapper.clk", IPG_CLK_ROOT, 60 },

	{ "sema1.clk", IPG_CLK_ROOT, 61 },
	{ "sema2.clk", IPG_CLK_ROOT, 62 },

	{ "sim_display.cm4clk", ARM_M4_CLK_ROOT },
	{ "sim_display.mainclk", MAIN_AXI_CLK_ROOT, 63 },
	{ "sim_display.mainclk_r", MAIN_AXI_CLK_ROOT, 63 },
	{ "sim_enet.mainclk", ENET_AXI_CLK_ROOT, 64 },
	{ "sim_enet.mainclk_r", ENET_AXI_CLK_ROOT, 64 },
	{ "sim_m.mainclk", AHB_CLK_ROOT, 65 },
	{ "sim_m.mainclk_r", AHB_CLK_ROOT, 65 },
	{ "sim_m.usdhcclk", NAND_USDHC_BUS_CLK_ROOT, 65 },
	{ "sim_m.usdhcclk_r", NAND_USDHC_BUS_CLK_ROOT, 65 },
	{ "sim_main.cm4clk", ARM_M4_CLK_ROOT },
	{ "sim_main.enetclk", ENET_AXI_CLK_ROOT, 64 },
	{ "sim_main.mainclk", MAIN_AXI_CLK_ROOT, 66 },
	{ "sim_main.mainclk_r", MAIN_AXI_CLK_ROOT, 66 },
	{ "sim_main.per_mclk", AHB_CLK_ROOT, 65 },
	{ "sim_main.per_sclk", AHB_CLK_ROOT, 67 },
	{ "sim_main.usdhcclk", NAND_USDHC_BUS_CLK_ROOT, 65 },
	{ "sim_main.wakeupclk", AHB_CLK_ROOT, 68 },
	{ "sim_s.apbhdmaclk", NAND_USDHC_BUS_CLK_ROOT, 48 },
	{ "sim_s.gpv4clk", ENET_AXI_CLK_ROOT, 64 },
	{ "sim_s.mainclk", AHB_CLK_ROOT, 67 },
	{ "sim_s.mainclk_r", AHB_CLK_ROOT, 67 },
	{ "sim_s.weimclk", AHB_CLK_ROOT },
	{ "sim_wakeup.mainclk", AHB_CLK_ROOT, 68 },
	{ "sim_wakeup.mainclk_r", AHB_CLK_ROOT, 68 },
	{ "pl301_audio.displayclk", MAIN_AXI_CLK_ROOT, 63 },

	{ "snvs_hs_wrapper.ipg_clk", IPG_CLK_ROOT, 71 },
	{ "snvs_hs.wrapper.ipg_clk_s", IPG_CLK_ROOT, 71 },
	{ "snvsmix.ipg_clk_root", IPG_CLK_ROOT },

	{ "spba1.ipg_clk", IPG_CLK_ROOT, 30 },
	{ "spba1.ipg_clk_s", IPG_CLK_ROOT, 30 },

	{ "spba2.ipg_clk", AUDIO_IPG_CLK_ROOT },
	{ "spba2.ipg_clk_s", AUDIO_IPG_CLK_ROOT },

	{ "spdif1.gclkw_t0", SPDIF1_CLK_ROOT},
	{ "spdif1.ipg_clk_s", IPG_CLK_ROOT},
	{ "spdif1.tx_clk", SPDIF1_CLK_ROOT},
	{ "spdif1.tx_clk1", SPDIF1_CLK_ROOT},
	{ "spdif1.tx_clk3", SPDIF1_CLK_ROOT},
	{ "spdif1.tx_clk4", SPDIF1_CLK_ROOT},
	{ "spdif1.tx_clk5", SPDIF1_CLK_ROOT},

	{ "spdif2.gclkw_t0", SPDIF2_CLK_ROOT},
	{ "spdif2.ipg_clk_s", IPG_CLK_ROOT},
	{ "spdif2.tx_clk", SPDIF2_CLK_ROOT},
	{ "spdif2.tx_clk1", SPDIF2_CLK_ROOT},
	{ "spdif2.tx_clk3", SPDIF2_CLK_ROOT},
	{ "spdif2.tx_clk4", SPDIF2_CLK_ROOT},
	{ "spdif2.tx_clk5", SPDIF2_CLK_ROOT},

	{ "coresight.DBGCLK", MAIN_AXI_CLK_ROOT, 72 },
	{ "coresight.traceclkin", TRACE_CLK_ROOT, 72 },
	{ "coresight_mem.cs_etf_clk", MAIN_AXI_CLK_ROOT, 72 },

	{ "uart1.ipg_clk", IPG_CLK_ROOT, 73 },
	{ "uart1.ipg_clk_s", IPG_CLK_ROOT, 73 },
	{ "uart1.ipg_perclk", UART1_CLK_ROOT, 73 },

	{ "uart2.ipg_clk", IPG_CLK_ROOT, 74 },
	{ "uart2.ipg_clk_s", IPG_CLK_ROOT, 74 },
	{ "uart2.ipg_perclk", UART2_CLK_ROOT, 74 },

	{ "uart3.ipg_clk", IPG_CLK_ROOT, 75 },
	{ "uart3.ipg_clk_s", IPG_CLK_ROOT, 75 },
	{ "uart3.ipg_perclk", UART3_CLK_ROOT, 75 },

	{ "uart4.ipg_clk", IPG_CLK_ROOT, 76 },
	{ "uart4.ipg_clk_s", IPG_CLK_ROOT, 76 },
	{ "uart4.ipg_perclk", UART4_CLK_ROOT, 76 },

	{ "usb.clk", IPG_CLK_ROOT, 22 },	/* HS */

	{ "usb1.ctrl", IPG_CLK_ROOT, 77 },	/* what is the root clock? */
	{ "usb2.ctrl", IPG_CLK_ROOT, 78 },
	{ "usb1.phy", IPG_CLK_ROOT, 79 },	/* what is the root clock? */
	{ "usb2.phy", IPG_CLK_ROOT, 80 },

	{ "usdhc1.hclk", NAND_USDHC_BUS_CLK_ROOT, 81 },
	{ "usdhc1.ipg_clk", IPG_CLK_ROOT, 81 },
	{ "usdhc1.ipg_clk_s", IPG_CLK_ROOT, 81 },
	{ "usdhc1.ipg_clk_perclk", USDHC1_CLK_ROOT, 81 },

	{ "usdhc2.hclk", NAND_USDHC_BUS_CLK_ROOT, 82 },
	{ "usdhc2.ipg_clk", IPG_CLK_ROOT, 82 },
	{ "usdhc2.ipg_clk_s", IPG_CLK_ROOT, 82 },
	{ "usdhc2.ipg_clk_perclk", USDHC2_CLK_ROOT, 82 },

	{ "wdog1.ipg_clk", WDOG_CLK_ROOT, 83 },
	{ "wdog1.ipg_clk_s", WDOG_CLK_ROOT, 83 },

	{ "wdog2.ipg_clk", WDOG_CLK_ROOT, 84 },
	{ "wdog2.ipg_clk_s", WDOG_CLK_ROOT, 84 },

	{ "wdog3.ipg_clk", WDOG_CLK_ROOT, 85 },
	{ "wdog3.ipg_clk_s", WDOG_CLK_ROOT, 85 },

	{ "vpu_g1.clk", VPU_G1_CLK_ROOT, 86 },
	{ "vpu_g2.clk", VPU_G2_CLK_ROOT,  90 },
	{ "vpu_dec.clk", VPU_BUS_CLK_ROOT, 99 },

	{ 0 }
};

static void
enablefracpll(u32int *reg, int ref_sel, int ref_freq, int freq)
{
	int divq, divr, ref, divfi, divff, pllout, error;
	u32int cfg0, cfg1;
	vlong v;

	error = freq;
	for(divq = 2; divq <= 64; divq += 2){
		for(divr = 2; divr <= 64; divr++){
			ref = ref_freq/divr;

			v = (vlong)freq*divq;
			v <<= 24;
			v /= ref * 8;

			divfi = v >> 24;
			divff = v & 0xFFFFFF;
			if(divfi < 1 || divfi > 128)
				continue;

			v *= (vlong)ref * 8;
			v /= (vlong)divq << 24;
			pllout = v;

			if(pllout > freq)
				continue;

			if(freq - pllout > error)
				continue;

			cfg0 = 1<<21 | ref_sel<<16 | 1<<15 | (divr-1)<<5 | (divq/2)-1;
			cfg1 = divff<<7 | (divfi-1);

			error = freq - pllout;
			if(error == 0)
				goto Found;
		}
	}
	panic("enablefracpll: %#p freq %d: out of range", PADDR(reg), freq);

Found:
	/* skip if nothing has changed */
	if(((reg[0] ^ cfg0) & (1<<21 | 3<<16 | 1<<15 | 0x3F<<5 | 0x1F)) == 0
	&& ((reg[1] ^ cfg1) & ~(1<<31)) == 0)
		return;

	/* bypass */
	reg[0] |= 1<<14;

	reg[1] = cfg1;

	reg[0] = cfg0 | (1<<14) | (1<<12);

	/* unbypass */
	reg[0] &= ~(1<<14);

	while((reg[0] & (1<<31)) == 0)
		;

	reg[0] &= ~(1<<12);
}

static void
enablepll(int input)
{
	u32int old, val = 2;

	if(input < 0 || input > 38 || input_clk_freq[input] <= 0)
		return;

	/* CCM_PLL_CTRL */
	old = regs[(0x800/4) + (16/4)*input] & 3;
	if(old < val){
// iprint("ccm: %s PLL_CTRL%d %.ux->%.ux\n", input_clk_name[input], input, old, val);
		regs[(0x800/4) + (16/4)*input] = val;
	}

	switch(input){
	case AUDIO_PLL1_CLK:
		enablefracpll(&anatop[0x00/4], 0, 25*Mhz, input_clk_freq[input]);
		break;
	case AUDIO_PLL2_CLK:
		enablefracpll(&anatop[0x08/4], 0, 25*Mhz, input_clk_freq[input]);
		break;
	case VIDEO_PLL1_CLK:
		enablefracpll(&anatop[0x10/4], 0, 25*Mhz, input_clk_freq[input]);
		break;
	case GPU_PLL_CLK:
		enablefracpll(&anatop[0x18/4], 0, 25*Mhz, input_clk_freq[input]);
		break;
	case VPU_PLL_CLK:
		enablefracpll(&anatop[0x20/4], 0, 25*Mhz, input_clk_freq[input]);
		break;
	case ARM_PLL_CLK:
		enablefracpll(&anatop[0x28/4], 0, 25*Mhz, input_clk_freq[input]);
		break;
	}
}

enum {
	CCM_ANALOG_PLLOUT_MONITOR_CFG	= 0x74/4,
		PLLOUT_MONITOR_CLK_CKE	= 1<<4,
	CCM_ANALOG_FRAC_PLLOUT_DIV_CFG	= 0x78/4,
	CCM_ANALOG_SCCG_PLLOUT_DIV_CFG	= 0x7C/4,
};

static struct {
	uchar	input;
	uchar	reg;		/* divider register */
	uchar	shift;		/* divider shift */
} anapllout_input[16] = {
[0]	OSC_25M_REF_CLK,
[1]	OSC_27M_REF_CLK,
/* [2]	HDMI_PHY_27M_CLK */
/* [3]	CLK1_P_N */
[4]	OSC_32K_REF_CLK,
[5]	AUDIO_PLL1_CLK,		CCM_ANALOG_FRAC_PLLOUT_DIV_CFG,	0,
[6]	AUDIO_PLL2_CLK,		CCM_ANALOG_FRAC_PLLOUT_DIV_CFG, 4,
[7]	GPU_PLL_CLK,		CCM_ANALOG_FRAC_PLLOUT_DIV_CFG,	12,
[8]	VPU_PLL_CLK,		CCM_ANALOG_FRAC_PLLOUT_DIV_CFG, 16,
[9]	VIDEO_PLL1_CLK,		CCM_ANALOG_FRAC_PLLOUT_DIV_CFG, 8,
[10]	ARM_PLL_CLK,		CCM_ANALOG_FRAC_PLLOUT_DIV_CFG, 20,
[11]	SYSTEM_PLL1_CLK,	CCM_ANALOG_SCCG_PLLOUT_DIV_CFG, 0,
[12]	SYSTEM_PLL2_CLK,	CCM_ANALOG_SCCG_PLLOUT_DIV_CFG, 4,
[13]	SYSTEM_PLL3_CLK,	CCM_ANALOG_SCCG_PLLOUT_DIV_CFG, 8,
[14]	VIDEO_PLL2_CLK,		CCM_ANALOG_SCCG_PLLOUT_DIV_CFG, 16,
[15]	DRAM_PLL1_CLK,		CCM_ANALOG_SCCG_PLLOUT_DIV_CFG, 12,
};

static void
setanapllout(int input, int freq)
{
	int mux, div, reg;

	for(mux = 0; mux < nelem(anapllout_input); mux++)
		if(anapllout_input[mux].input == input)
			goto Muxok;
	panic("setanapllout: bad input clock\n");
Muxok:
	anatop[CCM_ANALOG_PLLOUT_MONITOR_CFG] = mux;
	if(freq <= 0)
		return;
	div = input_clk_freq[input] / freq;
	if(div < 1 || div > 8){
		panic("setanapllout: divider out of range\n");
	}
	enablepll(input);
	reg = anapllout_input[mux].reg;
	if(reg){
		int shift = anapllout_input[mux].shift;
		anatop[reg] = (anatop[reg] & ~(7<<shift)) | ((div-1)<<shift);
	} else if(div != 1){
		panic("setanapllout: bad frequency\n");
	}
	anatop[CCM_ANALOG_PLLOUT_MONITOR_CFG] |= PLLOUT_MONITOR_CLK_CKE;
}

static int
getanapllout(void)
{
	int mux, input, freq, reg, div;
	u32int cfg = anatop[CCM_ANALOG_PLLOUT_MONITOR_CFG];

	mux = cfg & 0xF;
	input = anapllout_input[mux].input;
	if(input == 0)
		return 0;
	freq = input_clk_freq[input];
	if((cfg & PLLOUT_MONITOR_CLK_CKE) == 0)
		freq = -freq;
	reg = anapllout_input[mux].reg;
	if(reg){
		int shift = anapllout_input[mux].shift;
		div = ((anatop[reg] >> shift) & 7)+1;
	} else {
		div = 1;
	}
	return freq / div;
}

static u32int
clkgate(Clock *gate, u32int val)
{
	u32int old;

	if(gate == nil || gate->ccgr == 0)
		return 0;

	/* CCM_CCGR */
	old = regs[(0x4000/4) + (16/4)*gate->ccgr] & 3;
	if(old != val){
// if(gate->ccgr != 73) iprint("ccm: %s CCGR%d %.ux->%.ux\n", gate->name, gate->ccgr, old, val);
		regs[(0x4000/4) + (16/4)*gate->ccgr] = val;
	}
	return old;
}

static int
rootclkisipg(int root)
{
	switch(root){
	case IPG_CLK_ROOT:
	case AUDIO_IPG_CLK_ROOT:
	case MIPI_DSI_ESC_CLK_ROOT:
		return 1;
	}
	return 0;
}

static u32int
gettarget(int root)
{
	u32int val = regs[(0x8000/4) + (128/4)*root];
// if(root != UART1_CLK_ROOT) iprint("ccm: %s TARGET_ROOT%d -> %.8ux\n", root_clk_name[root], root, val);
	return val;
}

static void
settarget(int root, Clock *gate, u32int val)
{
	if(gate != nil){
		for(; gate->name != nil; gate++){
			u32int old;

			if(gate->root != root)
				continue;

			old = clkgate(gate, 0);
			if(old == 0)
				continue;

			/* skip restore when root is being disabled */
			if((val & (1<<28)) == 0)
				continue;

			/* now change the root clock target */
			settarget(root, gate+1, val);

			/* restore gate */
			clkgate(gate, old);
			return;
		}
	}

	if(rootclkisipg(root))
		val &= ~(1<<28);
// if(root != UART1_CLK_ROOT) iprint("ccm: %s TARGET_ROOT%d <- %.8ux\n", root_clk_name[root], root, val);
	regs[(0x8000/4) + (128/4)*root] = val;
}

static int
rootclkgetcfg(int root, int *input)
{
	u32int t = gettarget(root);
	int freq = input_clk_freq[*input = root_clk_input_mux[root*8 + ((t >> 24)&7)]];
	int pre_podf = (t >> 16)&7;
	int post_podf = (t >> 0)&0x3F;

	/* return negative frequency if disabled */
	if((t & (1<<28)) == 0)
		freq = -freq;

	switch(root){
	case ARM_A53_CLK_ROOT:
	case ARM_M4_CLK_ROOT:
	case VPU_A53_CLK_ROOT:
	case GPU_CORE_CLK_ROOT:
	case GPU_SHADER_CLK_ROOT:
		post_podf &= 7;
		/* wet floor */
	case GPU_AXI_CLK_ROOT:
	case GPU_AHB_CLK_ROOT:
	case NOC_CLK_ROOT:
		pre_podf = 0;
		break;
	case IPG_CLK_ROOT:
	case AUDIO_IPG_CLK_ROOT:
		post_podf &= 1;
	case MIPI_DSI_ESC_CLK_ROOT:
		freq = rootclkgetcfg(root-1, input);
		/* wet floor */
	case AHB_CLK_ROOT:
	case AUDIO_AHB_CLK_ROOT:
	case MIPI_DSI_ESC_RX_CLK_ROOT:
		pre_podf = 0;
		break;
	}
	freq /= pre_podf+1;
	freq /= post_podf+1;

	return freq;
}

static void
rootclksetcfg(int root, int input, int freq)
{
	u32int t = gettarget(root);

	if(!rootclkisipg(root)){
		int mux;

		for(mux = 0; mux < 8; mux++){
			if(root_clk_input_mux[root*8 + mux] == input){
				t = (t & ~(7<<24)) | (mux << 24);
				goto Muxok;
			}
		}
		panic("rootclksetcfg: invalid input clock %d for TARGET_ROOT%d\n", input, root);
Muxok:;
	}
	/* disable by default */
	t &= ~(1 << 28);

	if(freq){
		int pre_mask, pre_podf, post_mask, post_podf;
		int error, input_freq = input_clk_freq[input];

		if(freq < 0) {
			/* set dividers but keep disabled */
			freq = -freq;
		} else {
			/* set dividers and enable */
			t |= (1 << 28);
		}

		pre_mask = 7;
		post_mask = 0x3F;

		switch(root){
		case ARM_A53_CLK_ROOT:
		case ARM_M4_CLK_ROOT:
		case VPU_A53_CLK_ROOT:
		case GPU_CORE_CLK_ROOT:
		case GPU_SHADER_CLK_ROOT:
			post_mask = 7;
			/* wet floor */
		case GPU_AXI_CLK_ROOT:
		case GPU_AHB_CLK_ROOT:
		case NOC_CLK_ROOT:
			pre_mask = 0;
			break;
		case IPG_CLK_ROOT:
		case AUDIO_IPG_CLK_ROOT:
			post_mask = 1;
		case MIPI_DSI_ESC_CLK_ROOT:
			input_freq = rootclkgetcfg(root-1, &input);
			/* wet floor */
		case AHB_CLK_ROOT:
		case AUDIO_AHB_CLK_ROOT:
		case MIPI_DSI_ESC_RX_CLK_ROOT:
			pre_mask = 0;
			break;
		}
		if(input_freq < 0) input_freq = -input_freq;


		error = freq;
		for(pre_podf = 0; pre_podf <= pre_mask; pre_podf++){
			for(post_podf = 0; post_podf <= post_mask; post_podf++){
				int f = input_freq;
				f /= pre_podf+1;
				f /= post_podf+1;
				if(f <= freq && (freq - f) < error){
					t = (t & ~(7<<16)) | (pre_podf << 16);
					t = (t & ~0x3F) | post_podf;
					error = freq - f;
					if(error == 0)
						break;
				}
			}
		}
		if(error >= freq)
			panic("rootclksetcfg: frequency %d invalid for TARGET_ROOT%d\n", freq, root);
		if(t & (1<<28))
			enablepll(input);
	}
	settarget(root, clocks, t);
}

static int
lookinputclk(char *name)
{
	int i;

	for(i = 0; i < nelem(input_clk_name); i++){
		if(input_clk_name[i] != nil
		&& cistrcmp(name, input_clk_name[i]) == 0)
			return i;
	}

	return -1;
}

static Clock*
lookmodclk(char *name)
{
	Clock *clk;

	for(clk = clocks; clk->name != nil; clk++){
		if(cistrcmp(name, clk->name) == 0)
			return clk;
	}

	return nil;
}

static int
lookrootclk(char *name)
{
	Clock *clk;
	int i;

	for(i = 0; i < nelem(root_clk_name); i++){
		if(root_clk_name[i] != nil
		&& cistrcmp(name, root_clk_name[i]) == 0)
			return i;
	}

	if((clk = lookmodclk(name)) != nil)
		return clk->root;

	return -1;
}

void
setclkgate(char *name, int on)
{
	clkgate(lookmodclk(name), on ? 3 : 0);
}

void
setclkrate(char *name, char *source, int freq)
{
	int root, input;

	if(cistrcmp(name, "ccm_analog_pllout") == 0){
		setanapllout(lookinputclk(source), freq);
		return;
	}

	if((root = lookrootclk(name)) < 0)
		panic("setclkrate: clock %s not defined", name);
	if(source == nil)
		rootclkgetcfg(root, &input);
	else {
		if((input = lookinputclk(source)) < 0)
			panic("setclkrate: input clock %s not defined", source);
	}
	rootclksetcfg(root, input, freq);
}

int
getclkrate(char *name)
{
	int root, input;

	if(cistrcmp(name, "ccm_analog_pllout") == 0)
		return getanapllout();

	if((root = lookrootclk(name)) >= 0)
		return rootclkgetcfg(root, &input);

	if((input = lookinputclk(name)) > 0)
		return input_clk_freq[input];

	panic("getclkrate: clock %s not defined", name);
}
