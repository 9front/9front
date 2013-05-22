#include	"l.h"

uchar	ynone[] =
{
	Ynone,	Ynone,	Zlit,	1,
	0
};
uchar	ytext[] =
{
	Ymb,	Yi32,	Zpseudo,1,
	0
};
uchar	ynop[] =
{
	Ynone,	Ynone,	Zpseudo,1,
	Ynone,	Yml,	Zpseudo,1,
	Ynone,	Yrf,	Zpseudo,1,
	Ynone,	Yxr,	Zpseudo,1,
	Yml,	Ynone,	Zpseudo,1,
	Yrf,	Ynone,	Zpseudo,1,
	Yxr,	Ynone,	Zpseudo,1,
	0
};
uchar	yxorb[] =
{
	Yi32,	Yal,	Zib_,	1,
	Yi32,	Ymb,	Zibo_m,	2,
	Yrb,	Ymb,	Zr_m,	1,
	Ymb,	Yrb,	Zm_r,	1,
	0
};
uchar	yxorl[] =
{
	Yi8,	Yml,	Zibo_m,	2,
	Yi32,	Yax,	Zil_,	1,
	Yi32,	Yml,	Zilo_m,	2,
	Yrl,	Yml,	Zr_m,	1,
	Yml,	Yrl,	Zm_r,	1,
	0
};
uchar	yaddl[] =
{
	Yi8,	Yml,	Zibo_m,	2,
	Yi32,	Yax,	Zil_,	1,
	Yi32,	Yml,	Zilo_m,	2,
	Yrl,	Yml,	Zr_m,	1,
	Yml,	Yrl,	Zm_r,	1,
	0
};
uchar	yincb[] =
{
	Ynone,	Ymb,	Zo_m,	2,
	0
};
uchar	yincl[] =
{
	Ynone,	Yrl,	Z_rp,	1,
	Ynone,	Yml,	Zo_m,	2,
	0
};
uchar	ycmpb[] =
{
	Yal,	Yi32,	Z_ib,	1,
	Ymb,	Yi32,	Zm_ibo,	2,
	Ymb,	Yrb,	Zm_r,	1,
	Yrb,	Ymb,	Zr_m,	1,
	0
};
uchar	ycmpl[] =
{
	Yml,	Yi8,	Zm_ibo,	2,
	Yax,	Yi32,	Z_il,	1,
	Yml,	Yi32,	Zm_ilo,	2,
	Yml,	Yrl,	Zm_r,	1,
	Yrl,	Yml,	Zr_m,	1,
	0
};
uchar	yshb[] =
{
	Yi1,	Ymb,	Zo_m,	2,
	Yi32,	Ymb,	Zibo_m,	2,
	Ycx,	Ymb,	Zo_m,	2,
	0
};
uchar	yshl[] =
{
	Yi1,	Yml,	Zo_m,	2,
	Yi32,	Yml,	Zibo_m,	2,
	Ycl,	Yml,	Zo_m,	2,
	Ycx,	Yml,	Zo_m,	2,
	0
};
uchar	ytestb[] =
{
	Yi32,	Yal,	Zib_,	1,
	Yi32,	Ymb,	Zibo_m,	2,
	Yrb,	Ymb,	Zr_m,	1,
	Ymb,	Yrb,	Zm_r,	1,
	0
};
uchar	ytestl[] =
{
	Yi32,	Yax,	Zil_,	1,
	Yi32,	Yml,	Zilo_m,	2,
	Yrl,	Yml,	Zr_m,	1,
	Yml,	Yrl,	Zm_r,	1,
	0
};
uchar	ymovb[] =
{
	Yrb,	Ymb,	Zr_m,	1,
	Ymb,	Yrb,	Zm_r,	1,
	Yi32,	Yrb,	Zib_rp,	1,
	Yi32,	Ymb,	Zibo_m,	2,
	0
};
uchar	ymovl[] =
{
	Yrl,	Yml,	Zr_m,	1,
	Yml,	Yrl,	Zm_r,	1,
	Yi0,	Yrl,	Zclr,	1+2,
//	Yi0,	Yml,	Zibo_m,	2,	// shorter but slower AND $0,dst
	Yi32,	Yrl,	Zil_rp,	1,
	Yi32,	Yml,	Zilo_m,	2,
	Yml,	Ymr,	Zm_r_xm,	1,	// MMX MOVD
	Ymr,	Yml,	Zr_m_xm,	1,	// MMX MOVD
	Yml,	Yxr,	Zm_r_xm,	2,	// XMM MOVD (32 bit)
	Yxr,	Yml,	Zr_m_xm,	2,	// XMM MOVD (32 bit)
	Yiauto,	Yrl,	Zaut_r,	2,
	0
};
uchar	ym_rl[] =
{
	Ym,	Yrl,	Zm_r,	1,
	0
};
uchar	yrl_m[] =
{
	Yrl,	Ym,	Zr_m,	1,
	0
};
uchar	ymb_rl[] =
{
	Ymb,	Yrl,	Zm_r,	1,
	0
};
uchar	yml_rl[] =
{
	Yml,	Yrl,	Zm_r,	1,
	0
};
uchar	yrl_ml[] =
{
	Yrl,	Yml,	Zr_m,	1,
	0
};
uchar	yml_mb[] =
{
	Yrb,	Ymb,	Zr_m,	1,
	Ymb,	Yrb,	Zm_r,	1,
	0
};
uchar	yml_ml[] =
{
	Yrl,	Yml,	Zr_m,	1,
	Yml,	Yrl,	Zm_r,	1,
	0
};
uchar	ydivl[] =
{
	Yml,	Ynone,	Zm_o,	2,
	0
};
uchar	ydivb[] =
{
	Ymb,	Ynone,	Zm_o,	2,
	0
};
uchar	yimul[] =
{
	Yml,	Ynone,	Zm_o,	2,
	Yi8,	Yrl,	Zib_rr,	1,
	Yi32,	Yrl,	Zil_rr,	1,
	0
};
uchar	ybyte[] =
{
	Yi32,	Ynone,	Zbyte,	1,
	0
};
uchar	yin[] =
{
	Yi32,	Ynone,	Zib_,	1,
	Ynone,	Ynone,	Zlit,	1,
	0
};
uchar	yint[] =
{
	Yi32,	Ynone,	Zib_,	1,
	0
};
uchar	ypushl[] =
{
	Yrl,	Ynone,	Zrp_,	1,
	Ym,	Ynone,	Zm_o,	2,
	Yi8,	Ynone,	Zib_,	1,
	Yi32,	Ynone,	Zil_,	1,
	0
};
uchar	ypopl[] =
{
	Ynone,	Yrl,	Z_rp,	1,
	Ynone,	Ym,	Zo_m,	2,
	0
};
uchar	yscond[] =
{
	Ynone,	Ymb,	Zo_m,	2,
	0
};
uchar	yjcond[] =
{
	Ynone,	Ybr,	Zbr,	1,
	0
};
uchar	yloop[] =
{
	Ynone,	Ybr,	Zloop,	1,
	0
};
uchar	ycall[] =
{
	Ynone,	Yml,	Zo_m,	2,
	Ynone,	Ybr,	Zcall,	1,
	0
};
uchar	yjmp[] =
{
	Ynone,	Yml,	Zo_m,	2,
	Ynone,	Ybr,	Zjmp,	1,
	0
};

uchar	yfmvd[] =
{
	Ym,	Yf0,	Zm_o,	2,
	Yf0,	Ym,	Zo_m,	2,
	Yrf,	Yf0,	Zm_o,	2,
	Yf0,	Yrf,	Zo_m,	2,
	0
};
uchar	yfmvdp[] =
{
	Yf0,	Ym,	Zo_m,	2,
	Yf0,	Yrf,	Zo_m,	2,
	0
};
uchar	yfmvf[] =
{
	Ym,	Yf0,	Zm_o,	2,
	Yf0,	Ym,	Zo_m,	2,
	0
};
uchar	yfmvx[] =
{
	Ym,	Yf0,	Zm_o,	2,
	0
};
uchar	yfmvp[] =
{
	Yf0,	Ym,	Zo_m,	2,
	0
};
uchar	yfadd[] =
{
	Ym,	Yf0,	Zm_o,	2,
	Yrf,	Yf0,	Zm_o,	2,
	Yf0,	Yrf,	Zo_m,	2,
	0
};
uchar	yfaddp[] =
{
	Yf0,	Yrf,	Zo_m,	2,
	0
};
uchar	yfxch[] =
{
	Yf0,	Yrf,	Zo_m,	2,
	Yrf,	Yf0,	Zm_o,	2,
	0
};
uchar	ycompp[] =
{
	Yf0,	Yrf,	Zo_m,	2,	/* botch is really f0,f1 */
	0
};
uchar	ystsw[] =
{
	Ynone,	Ym,	Zo_m,	2,
	Ynone,	Yax,	Zlit,	1,
	0
};
uchar	ystcw[] =
{
	Ynone,	Ym,	Zo_m,	2,
	Ym,	Ynone,	Zm_o,	2,
	0
};
uchar	ysvrs[] =
{
	Ynone,	Ym,	Zo_m,	2,
	Ym,	Ynone,	Zm_o,	2,
	0
};
uchar	ymm[] = 
{
	Ymm,	Ymr,	Zm_r_xm,	1,
	Yxm,	Yxr,	Zm_r_xm,	2,
	0
};
uchar	yxm[] = 
{
	Yxm,	Yxr,	Zm_r_xm,	1,
	0
};
uchar	yxcvm1[] = 
{
	Yxm,	Yxr,	Zm_r_xm,	2,
	0
};
uchar	yxcvm2[] =
{
	Yxm,	Yxr,	Zm_r_xm,	2,
	0
};
uchar	yxmq[] = 
{
	Yxm,	Yxr,	Zm_r_xm,	2,
	0
};
uchar	yxr[] = 
{
	Yxr,	Yxr,	Zm_r_xm,	1,
	0
};
uchar	yxr_ml[] =
{
	Yxr,	Yml,	Zr_m_xm,	1,
	0
};
uchar	ymr[] =
{
	Ymr,	Ymr,	Zm_r,	1,
	0
};
uchar	ymr_ml[] =
{
	Ymr,	Yml,	Zr_m_xm,	1,
	0
};
uchar	yxcmp[] =
{
	Yxm,	Yxr, Zm_r_xm,	1,
	0
};
uchar	yxcmpi[] =
{
	Yxm,	Yxr, Zm_r_i_xm,	2,
	0
};
uchar	yxmov[] =
{
	Yxm,	Yxr,	Zm_r_xm,	1,
	Yxr,	Yxm,	Zr_m_xm,	1,
	0
};
uchar	yxcvfl[] = 
{
	Yxm,	Yrl,	Zm_r_xm,	1,
	0
};
uchar	yxcvlf[] =
{
	Yml,	Yxr,	Zm_r_xm,	1,
	0
};
uchar	yps[] = 
{
	Ymm,	Ymr,	Zm_r_xm,	1,
	Yi8,	Ymr,	Zibo_m_xm,	2,
	Yxm,	Yxr,	Zm_r_xm,	2,
	Yi8,	Yxr,	Zibo_m_xm,	3,
	0
};
uchar	yxrrl[] =
{
	Yxr,	Yrl,	Zm_r,	1,
	0
};
uchar	ymfp[] =
{
	Ymm,	Ymr,	Zm_r_3d,	1,
	0,
};
uchar	ymrxr[] =
{
	Ymr,	Yxr,	Zm_r,	1,
	Yxm,	Yxr,	Zm_r_xm,	1,
	0
};
uchar	ymshuf[] =
{
	Ymm,	Ymr,	Zibm_r,	1,
	0
};
uchar	yxshuf[] =
{
	Yxm,	Yxr,	Zibm_r,	1,
	0
};
uchar	yextrw[] =
{
	Yxr,	Yrl,	Zibm_r,	1,
	0
};
uchar	ypsdq[] =
{
	Yi8,	Yxr,	Zibo_m,	2,
	0
};
uchar	ymskb[] =
{
	Yxr,	Yrl,	Zm_r_xm,	2,
	Ymr,	Yrl,	Zm_r_xm,	1,
	0
};

Optab optab[] =
/*	as, ytab, andproto, opcode */
{
	{ AXXX },
	{ AAAA,		ynone,	Px, 0x37 },
	{ AAAD,		ynone,	Px, 0xd5,0x0a },
	{ AAAM,		ynone,	Px, 0xd4,0x0a },
	{ AAAS,		ynone,	Px, 0x3f },
	{ AADCB,	yxorb,	Pb, 0x14,0x80,(02),0x10,0x10 },
	{ AADCL,	yxorl,	Px, 0x83,(02),0x15,0x81,(02),0x11,0x13 },
	{ AADCW,	yxorl,	Pe, 0x83,(02),0x15,0x81,(02),0x11,0x13 },
	{ AADDB,	yxorb,	Px, 0x04,0x80,(00),0x00,0x02 },
	{ AADDL,	yaddl,	Px, 0x83,(00),0x05,0x81,(00),0x01,0x03 },
	{ AADDPD,	yxm,	Pq, 0x58 },
	{ AADDPS,	yxm,	Pm, 0x58 },
	{ AADDSD,	yxm,	Pf2, 0x58 },
	{ AADDSS,	yxm,	Pf3, 0x58 },
	{ AADDW,	yaddl,	Pe, 0x83,(00),0x05,0x81,(00),0x01,0x03 },
	{ AADJSP },
	{ AANDB,	yxorb,	Pb, 0x24,0x80,(04),0x20,0x22 },
	{ AANDL,	yxorl,	Px, 0x83,(04),0x25,0x81,(04),0x21,0x23 },
	{ AANDNPD,	yxm,	Pq, 0x55 },
	{ AANDNPS,	yxm,	Pm, 0x55 },
	{ AANDPD,	yxm,	Pq, 0x54 },
	{ AANDPS,	yxm,	Pq, 0x54 },
	{ AANDW,	yxorl,	Pe, 0x83,(04),0x25,0x81,(04),0x21,0x23 },
	{ AARPL,	yrl_ml,	Px, 0x63 },
	{ ABOUNDL,	yrl_m,	Px, 0x62 },
	{ ABOUNDW,	yrl_m,	Pe, 0x62 },
	{ ABSFL,	yml_rl,	Pm, 0xbc },
	{ ABSFW,	yml_rl,	Pq, 0xbc },
	{ ABSRL,	yml_rl,	Pm, 0xbd },
	{ ABSRW,	yml_rl,	Pq, 0xbd },
	{ ABTL,		yml_rl,	Pm, 0xa3 },
	{ ABTW,		yml_rl,	Pq, 0xa3 },
	{ ABTCL,	yml_rl,	Pm, 0xbb },
	{ ABTCW,	yml_rl,	Pq, 0xbb },
	{ ABTRL,	yml_rl,	Pm, 0xb3 },
	{ ABTRW,	yml_rl,	Pq, 0xb3 },
	{ ABTSL,	yml_rl,	Pm, 0xab },
	{ ABTSW,	yml_rl,	Pq, 0xab },
	{ ABYTE,	ybyte,	Px, 1 },
	{ ACALL,	ycall,	Px, 0xff,(02),0xe8 },
	{ ACLC,		ynone,	Px, 0xf8 },
	{ ACLD,		ynone,	Px, 0xfc },
	{ ACLI,		ynone,	Px, 0xfa },
	{ ACLTS,	ynone,	Pm, 0x06 },
	{ ACMC,		ynone,	Px, 0xf5 },
	{ ACMPB,	ycmpb,	Pb, 0x3c,0x80,(07),0x38,0x3a },
	{ ACMPL,	ycmpl,	Px, 0x83,(07),0x3d,0x81,(07),0x39,0x3b },
	{ ACMPPD,	yxcmpi,	Px, Pe,0xc2 },
	{ ACMPPS,	yxcmpi,	Pm, 0xc2,0 },
	{ ACMPW,	ycmpl,	Pe, 0x83,(07),0x3d,0x81,(07),0x39,0x3b },
	{ ACOMISD,	yxcmp,	Pe, 0x2f },
	{ ACOMISS,	yxcmp,	Pm, 0x2f },
	{ ACPUID,	ynone,	Pm, 0xa2 },
	{ ACVTPL2PD,	yxcvm2,	Px, Pf3,0xe6 },
	{ ACVTPL2PS,	yxcvm2,	Pm, 0x5b },
	{ ACVTPD2PL,	yxcvm1,	Px, Pf2,0xe6 },
	{ ACVTPD2PS,	yxm,	Pe, 0x5a },
	{ ACVTPS2PL,	yxcvm1, Px, Pe,0x5b },
	{ ACVTPS2PD,	yxm,	Pm, 0x5a },
	{ ACVTSD2SL,	yxcvfl, Pf2, 0x2d },
	{ ACVTSD2SS,	yxm,	Pf2, 0x5a },
	{ ACVTSL2SD,	yxcvlf, Pf2, 0x2a },
	{ ACVTSL2SS,	yxcvlf, Pf3, 0x2a },
	{ ACVTSS2SD,	yxm,	Pf3, 0x5a },
	{ ACVTSS2SL,	yxcvfl, Pf3, 0x2d },
	{ ACVTTPD2PL,	yxcvm1,	Px, Pe,0xe6 },
	{ ACVTTPS2PL,	yxcvm1,	Px, Pf3,0x5b },
	{ ACVTTSD2SL,	yxcvfl, Pf2, 0x2c },
	{ ACVTTSS2SL,	yxcvfl,	Pf3, 0x2c },
	{ ACMPSB,	ynone,	Pb, 0xa6 },
	{ ACMPSD,	yxcmpi,	Px, Pf2,0xc2 },
	{ ACMPSL,	ynone,	Px, 0xa7 },
	{ ACMPSS,	yxcmpi,	Px, Pf3,0xc2 },
	{ ACMPSW,	ynone,	Pe, 0xa7 },
	{ ADAA,		ynone,	Px, 0x27 },
	{ ADAS,		ynone,	Px, 0x2f },
	{ ADATA },
	{ ADECB,	yincb,	Pb, 0xfe,(01) },
	{ ADECL,	yincl,	Px, 0x48,0xff,(01) },
	{ ADECW,	yincl,	Pe, 0x48,0xff,(01) },
	{ ADIVB,	ydivb,	Pb, 0xf6,(06) },
	{ ADIVL,	ydivl,	Px, 0xf7,(06) },
	{ ADIVPD,	yxm,	Pe, 0x5e },
	{ ADIVPS,	yxm,	Pm, 0x5e },
	{ ADIVSD,	yxm,	Pf2, 0x5e },
	{ ADIVSS,	yxm,	Pf3, 0x5e },
	{ ADIVW,	ydivl,	Pe, 0xf7,(06) },
	{ AENTER },				/* botch */
	{ AFXRSTOR,	ysvrs,	Pm, 0xae,(01),0xae,(01) },
	{ AFXSAVE,	ysvrs,	Pm, 0xae,(00),0xae,(00) },
	{ AGLOBL },
	{ AGOK },
	{ AHISTORY },
	{ AHLT,		ynone,	Px, 0xf4 },
	{ AIDIVB,	ydivb,	Pb, 0xf6,(07) },
	{ AIDIVL,	ydivl,	Px, 0xf7,(07) },
	{ AIDIVW,	ydivl,	Pe, 0xf7,(07) },
	{ AIMULB,	ydivb,	Pb, 0xf6,(05) },
	{ AIMULL,	yimul,	Px, 0xf7,(05),0x6b,0x69 },
	{ AIMULW,	yimul,	Pe, 0xf7,(05),0x6b,0x69 },
	{ AINB,		yin,	Pb, 0xe4,0xec },
	{ AINL,		yin,	Px, 0xe5,0xed },
	{ AINW,		yin,	Pe, 0xe5,0xed },
	{ AINCB,	yincb,	Pb, 0xfe,(00) },
	{ AINCL,	yincl,	Px, 0x40,0xff,(00) },
	{ AINCW,	yincl,	Pe, 0x40,0xff,(00) },
	{ AINSB,	ynone,	Pb, 0x6c },
	{ AINSL,	ynone,	Px, 0x6d },
	{ AINSW,	ynone,	Pe, 0x6d },
	{ AINT,		yint,	Px, 0xcd },
	{ AINTO,	ynone,	Px, 0xce },
	{ AIRETL,	ynone,	Px, 0xcf },
	{ AIRETW,	ynone,	Pe, 0xcf },
	{ AJCC,		yjcond,	Px, 0x73,0x83,(00) },
	{ AJCS,		yjcond,	Px, 0x72,0x82 },
	{ AJCXZ,	yloop,	Px, 0xe3 },
	{ AJEQ,		yjcond,	Px, 0x74,0x84 },
	{ AJGE,		yjcond,	Px, 0x7d,0x8d },
	{ AJGT,		yjcond,	Px, 0x7f,0x8f },
	{ AJHI,		yjcond,	Px, 0x77,0x87 },
	{ AJLE,		yjcond,	Px, 0x7e,0x8e },
	{ AJLS,		yjcond,	Px, 0x76,0x86 },
	{ AJLT,		yjcond,	Px, 0x7c,0x8c },
	{ AJMI,		yjcond,	Px, 0x78,0x88 },
	{ AJMP,		yjmp,	Px, 0xff,(04),0xeb,0xe9 },
	{ AJNE,		yjcond,	Px, 0x75,0x85 },
	{ AJOC,		yjcond,	Px, 0x71,0x81,(00) },
	{ AJOS,		yjcond,	Px, 0x70,0x80,(00) },
	{ AJPC,		yjcond,	Px, 0x7b,0x8b },
	{ AJPL,		yjcond,	Px, 0x79,0x89 },
	{ AJPS,		yjcond,	Px, 0x7a,0x8a },
	{ ALAHF,	ynone,	Px, 0x9f },
	{ ALARL,	yml_rl,	Pm, 0x02 },
	{ ALARW,	yml_rl,	Pq, 0x02 },
	{ ALDMXCSR,	ysvrs,	Pm, 0xae,(02),0xae,(02) },
	{ ALEAL,	ym_rl,	Px, 0x8d },
	{ ALEAW,	ym_rl,	Pe, 0x8d },
	{ ALEAVEL,	ynone,	Px, 0xc9 },
	{ ALEAVEW,	ynone,	Pe, 0xc9 },
	{ ALOCK,	ynone,	Px, 0xf0 },
	{ ALODSB,	ynone,	Pb, 0xac },
	{ ALODSL,	ynone,	Px, 0xad },
	{ ALODSW,	ynone,	Pe, 0xad },
	{ ALONG,	ybyte,	Px, 4 },
	{ ALOOP,	yloop,	Px, 0xe2 },
	{ ALOOPEQ,	yloop,	Px, 0xe1 },
	{ ALOOPNE,	yloop,	Px, 0xe0 },
	{ ALSLL,	yml_rl,	Pm, 0x03  },
	{ ALSLW,	yml_rl,	Pq, 0x03  },
	{ AMASKMOVOU,	yxr,	Pe, 0xf7 },
	{ AMASKMOVQ,	ymr,	Pm, 0xf7 },
	{ AMAXPD,	yxm,	Pe, 0x5f },
	{ AMAXPS,	yxm,	Pm, 0x5f },
	{ AMAXSD,	yxm,	Pf2, 0x5f },
	{ AMAXSS,	yxm,	Pf3, 0x5f },
	{ AMINPD,	yxm,	Pe, 0x5d },
	{ AMINPS,	yxm,	Pm, 0x5d },
	{ AMINSD,	yxm,	Pf2, 0x5d },
	{ AMINSS,	yxm,	Pf3, 0x5d },
	{ AMOVAPD,	yxmov,	Pe, 0x28,0x29 },
	{ AMOVAPS,	yxmov,	Pm, 0x28,0x29 },
	{ AMOVB,	ymovb,	Pb, 0x88,0x8a,0xb0,0xc6,(00) },
	{ AMOVL,	ymovl,	Px, 0x89,0x8b,0x31,0x83,(04),0xb8,0xc7,(00),0x6e,0x7e,Pe,0x6e,Pe,0x7e },
	{ AMOVW,	ymovl,	Pe, 0x89,0x8b,0x31,0x83,(04),0xb8,0xc7,(00) },
	{ AMOVBLSX,	ymb_rl,	Pm, 0xbe },
	{ AMOVBLZX,	ymb_rl,	Pm, 0xb6 },
	{ AMOVBWSX,	ymb_rl,	Pq, 0xbe },
	{ AMOVBWZX,	ymb_rl,	Pq, 0xb6 },
	{ AMOVWLSX,	yml_rl,	Pm, 0xbf },
	{ AMOVWLZX,	yml_rl,	Pm, 0xb7 },
	{ AMOVO,	yxmov,	Pe, 0x6f,0x7f },
	{ AMOVOU,	yxmov,	Pf3, 0x6f,0x7f },
	{ AMOVHLPS,	yxr,	Pm, 0x12 },
	{ AMOVHPD,	yxmov,	Pe, 0x16,0x17 },
	{ AMOVHPS,	yxmov,	Pm, 0x16,0x17 },
	{ AMOVLHPS,	yxr,	Pm, 0x16 },
	{ AMOVLPD,	yxmov,	Pe, 0x12,0x13 },
	{ AMOVLPS,	yxmov,	Pm, 0x12,0x13 },
	{ AMOVMSKPD,	yxrrl,	Pq, 0x50 },
	{ AMOVMSKPS,	yxrrl,	Pm, 0x50 },
	{ AMOVNTO,	yxr_ml,	Pe, 0xe7 },
	{ AMOVNTPD,	yxr_ml,	Pe, 0x2b },
	{ AMOVNTPS,	yxr_ml,	Pm, 0x2b },
	{ AMOVNTQ,	ymr_ml,	Pm, 0xe7 },
	{ AMOVQOZX,	ymrxr,	Pf3, 0xd6,0x7e },
	{ AMOVSB,	ynone,	Pb, 0xa4 },
	{ AMOVSD,	yxmov,	Pf2, 0x10,0x11 },
	{ AMOVSL,	ynone,	Px, 0xa5 },
	{ AMOVSS,	yxmov,	Pf3, 0x10,0x11 },
	{ AMOVSW,	ynone,	Pe, 0xa5 },
	{ AMOVUPD,	yxmov,	Pe, 0x10,0x11 },
	{ AMOVUPS,	yxmov,	Pm, 0x10,0x11 },
	{ AMULB,	ydivb,	Pb, 0xf6,(04) },
	{ AMULL,	ydivl,	Px, 0xf7,(04) },
	{ AMULPD,	yxm,	Pe, 0x59 },
	{ AMULPS,	yxm,	Ym, 0x59 },
	{ AMULSD,	yxm,	Pf2, 0x59 },
	{ AMULSS,	yxm,	Pf3, 0x59 },
	{ AMULW,	ydivl,	Pe, 0xf7,(04) },
	{ ANAME },
	{ ANEGB,	yscond,	Px, 0xf6,(03) },
	{ ANEGL,	yscond,	Px, 0xf7,(03) },
	{ ANEGW,	yscond,	Pe, 0xf7,(03) },
	{ ANOP,		ynop,	Px,0,0 },
	{ ANOTB,	yscond,	Px, 0xf6,(02) },
	{ ANOTL,	yscond,	Px, 0xf7,(02) },
	{ ANOTW,	yscond,	Pe, 0xf7,(02) },
	{ AORB,		yxorb,	Pb, 0x0c,0x80,(01),0x08,0x0a },
	{ AORL,		yxorl,	Px, 0x83,(01),0x0d,0x81,(01),0x09,0x0b },
	{ AORPD,	yxm,	Pq, 0x56 },
	{ AORPS,	yxm,	Pm, 0x56 },
	{ AORW,		yxorl,	Pe, 0x83,(01),0x0d,0x81,(01),0x09,0x0b },
	{ AOUTB,	yin,	Pb, 0xe6,0xee },
	{ AOUTL,	yin,	Px, 0xe7,0xef },
	{ AOUTW,	yin,	Pe, 0xe7,0xef },
	{ AOUTSB,	ynone,	Pb, 0x6e },
	{ AOUTSL,	ynone,	Px, 0x6f },
	{ AOUTSW,	ynone,	Pe, 0x6f },
	{ APACKSSLW,	ymm,	Px, 0x6b,Pe,0x6b },
	{ APACKSSWB,	ymm,	Px, 0x63,Pe,0x63 },
	{ APACKUSWB,	ymm,	Px, 0x67,Pe,0x67 },
	{ APADDB,	ymm,	Px, 0xfc,Pe,0xfc },
	{ APADDL,	ymm,	Px, 0xfe,Pe,0xfe },
	{ APADDQ,	yxm,	Pe, 0xd4 },
	{ APADDSB,	ymm,	Px, 0xec,Pe,0xec },
	{ APADDSW,	ymm,	Px, 0xed,Pe,0xed },
	{ APADDUSB,	ymm,	Px, 0xdc,Pe,0xdc },
	{ APADDUSW,	ymm,	Px, 0xdd,Pe,0xdd },
	{ APADDW,	ymm,	Px, 0xfd,Pe,0xfd },
	{ APAND,	ymm,	Px, 0xdb,Pe,0xdb },
	{ APANDN,	ymm,	Px, 0xdf,Pe,0xdf },
	{ APAVGB,	ymm,	Px, 0xe0,Pe,0xe0 },
	{ APAVGW,	ymm,	Px, 0xe3,Pe,0xe3 },
	{ APCMPEQB,	ymm,	Px, 0x74,Pe,0x74 },
	{ APCMPEQL,	ymm,	Px, 0x76,Pe,0x76 },
	{ APCMPEQW,	ymm,	Px, 0x75,Pe,0x75 },
	{ APCMPGTB,	ymm,	Px, 0x64,Pe,0x64 },
	{ APCMPGTL,	ymm,	Px, 0x66,Pe,0x66 },
	{ APCMPGTW,	ymm,	Px, 0x65,Pe,0x65 },
	{ APEXTRW,	yextrw,	Pq, 0xc5 },
	{ APF2IL,	ymfp,	Px, 0x1d },
	{ APF2IW,	ymfp,	Px, 0x1c },
	{ API2FL,	ymfp,	Px, 0x0d },
	{ API2FW,	ymfp,	Px, 0x0c },
	{ APINSRW,	yextrw,	Pq, 0xc4 },
	{ APMADDWL,	ymm,	Px, 0xf5,Pe,0xf5 },
	{ APMAXSW,	yxm,	Pe, 0xee },
	{ APMAXUB,	yxm,	Pe, 0xde },
	{ APMINSW,	yxm,	Pe, 0xea },
	{ APMINUB,	yxm,	Pe, 0xda },
	{ APMOVMSKB,	ymskb,	Px, Pe,0xd7,0xd7 },
	{ APMULHRW,	ymfp,	Px, 0xb7 },
	{ APMULHUW,	ymm,	Px, 0xe4,Pe,0xe4 },
	{ APMULHW,	ymm,	Px, 0xe5,Pe,0xe5 },
	{ APMULLW,	ymm,	Px, 0xd5,Pe,0xd5 },
	{ APMULULQ,	ymm,	Px, 0xf4,Pe,0xf4 },
	{ APOPAL,	ynone,	Px, 0x61 },
	{ APOPAW,	ynone,	Pe, 0x61 },
	{ APOPFL,	ynone,	Px, 0x9d },
	{ APOPFW,	ynone,	Pe, 0x9d },
	{ APOPL,	ypopl,	Px, 0x58,0x8f,(00) },
	{ APOPW,	ypopl,	Pe, 0x58,0x8f,(00) },
	{ APOR,		ymm,	Px, 0xeb,Pe,0xeb },
	{ APSADBW,	yxm,	Pq, 0xf6 },
	{ APSHUFHW,	yxshuf,	Pf3, 0x70 },
	{ APSHUFL,	yxshuf,	Pq, 0x70 },
	{ APSHUFLW,	yxshuf,	Pf2, 0x70 },
	{ APSHUFW,	ymshuf,	Pm, 0x70 },
	{ APSLLO,	ypsdq,	Pq, 0x73,(07) },
	{ APSLLL,	yps,	Px, 0xf2, 0x72,(06), Pe,0xf2, Pe,0x72,(06) },
	{ APSLLQ,	yps,	Px, 0xf3, 0x73,(06), Pe,0xf3, Pe,0x7e,(06) },
	{ APSLLW,	yps,	Px, 0xf1, 0x71,(06), Pe,0xf1, Pe,0x71,(06) },
	{ APSRAL,	yps,	Px, 0xe2, 0x72,(04), Pe,0xe2, Pe,0x72,(04) },
	{ APSRAW,	yps,	Px, 0xe1, 0x71,(04), Pe,0xe1, Pe,0x71,(04) },
	{ APSRLO,	ypsdq,	Pq, 0x73,(03) },
	{ APSRLL,	yps,	Px, 0xd2, 0x72,(02), Pe,0xd2, Pe,0x72,(02) },
	{ APSRLQ,	yps,	Px, 0xd3, 0x73,(02), Pe,0xd3, Pe,0x73,(02) },
	{ APSRLW,	yps,	Px, 0xd1, 0x71,(02), Pe,0xe1, Pe,0x71,(02) },
	{ APSUBB,	yxm,	Pe, 0xf8 },
	{ APSUBL,	yxm,	Pe, 0xfa },
	{ APSUBQ,	yxm,	Pe, 0xfb },
	{ APSUBSB,	yxm,	Pe, 0xe8 },
	{ APSUBSW,	yxm,	Pe, 0xe9 },
	{ APSUBUSB,	yxm,	Pe, 0xd8 },
	{ APSUBUSW,	yxm,	Pe, 0xd9 },
	{ APSUBW,	yxm,	Pe, 0xf9 },
	{ APUNPCKHBW,	ymm,	Px, 0x68,Pe,0x68 },
	{ APUNPCKHLQ,	ymm,	Px, 0x6a,Pe,0x6a },
	{ APUNPCKHQDQ,	yxm,	Pe, 0x6d },
	{ APUNPCKHWL,	ymm,	Px, 0x69,Pe,0x69 },
	{ APUNPCKLBW,	ymm,	Px, 0x60,Pe,0x60 },
	{ APUNPCKLLQ,	ymm,	Px, 0x62,Pe,0x62 },
	{ APUNPCKLQDQ,	yxm,	Pe, 0x6c },
	{ APUNPCKLWL,	ymm,	Px, 0x61,Pe,0x61 },
	{ APUSHAL,	ynone,	Px, 0x60 },
	{ APUSHAW,	ynone,	Pe, 0x60 },
	{ APUSHFL,	ynone,	Px, 0x9c },
	{ APUSHFW,	ynone,	Pe, 0x9c },
	{ APUSHL,	ypushl,	Px, 0x50,0xff,(06),0x6a,0x68 },
	{ APUSHW,	ypushl,	Pe, 0x50,0xff,(06),0x6a,0x68 },
	{ APXOR,	ymm,	Px, 0xef,Pe,0xef },
	{ ARCLB,	yshb,	Pb, 0xd0,(02),0xc0,(02),0xd2,(02) },
	{ ARCLL,	yshl,	Px, 0xd1,(02),0xc1,(02),0xd3,(02),0xd3,(02) },
	{ ARCLW,	yshl,	Pe, 0xd1,(02),0xc1,(02),0xd3,(02),0xd3,(02) },
	{ ARCPPS,	yxm,	Pm, 0x53 },
	{ ARCPSS,	yxm,	Pf3, 0x53 },
	{ ARCRB,	yshb,	Pb, 0xd0,(03),0xc0,(03),0xd2,(03) },
	{ ARCRL,	yshl,	Px, 0xd1,(03),0xc1,(03),0xd3,(03),0xd3,(03) },
	{ ARCRW,	yshl,	Pe, 0xd1,(03),0xc1,(03),0xd3,(03),0xd3,(03) },
	{ AREP,		ynone,	Px, 0xf3 },
	{ AREPN,	ynone,	Px, 0xf2 },
	{ ARET,		ynone,	Px, 0xc3 },
	{ AROLB,	yshb,	Pb, 0xd0,(00),0xc0,(00),0xd2,(00) },
	{ AROLL,	yshl,	Px, 0xd1,(00),0xc1,(00),0xd3,(00),0xd3,(00) },
	{ AROLW,	yshl,	Pe, 0xd1,(00),0xc1,(00),0xd3,(00),0xd3,(00) },
	{ ARORB,	yshb,	Pb, 0xd0,(01),0xc0,(01),0xd2,(01) },
	{ ARORL,	yshl,	Px, 0xd1,(01),0xc1,(01),0xd3,(01),0xd3,(01) },
	{ ARORW,	yshl,	Pe, 0xd1,(01),0xc1,(01),0xd3,(01),0xd3,(01) },
	{ ARSQRTPS,	yxm,	Pm, 0x52 },
	{ ARSQRTSS,	yxm,	Pf3, 0x52 },
	{ ASAHF,	ynone,	Px, 0x9e },
	{ ASALB,	yshb,	Pb, 0xd0,(04),0xc0,(04),0xd2,(04) },
	{ ASALL,	yshl,	Px, 0xd1,(04),0xc1,(04),0xd3,(04),0xd3,(04) },
	{ ASALW,	yshl,	Pe, 0xd1,(04),0xc1,(04),0xd3,(04),0xd3,(04) },
	{ ASARB,	yshb,	Pb, 0xd0,(07),0xc0,(07),0xd2,(07) },
	{ ASARL,	yshl,	Px, 0xd1,(07),0xc1,(07),0xd3,(07),0xd3,(07) },
	{ ASARW,	yshl,	Pe, 0xd1,(07),0xc1,(07),0xd3,(07),0xd3,(07) },
	{ ASBBB,	yxorb,	Pb, 0x1c,0x80,(03),0x18,0x1a },
	{ ASBBL,	yxorl,	Px, 0x83,(03),0x1d,0x81,(03),0x19,0x1b },
	{ ASBBW,	yxorl,	Pe, 0x83,(03),0x1d,0x81,(03),0x19,0x1b },
	{ ASCASB,	ynone,	Pb, 0xae },
	{ ASCASL,	ynone,	Px, 0xaf },
	{ ASCASW,	ynone,	Pe, 0xaf },
	{ ASETCC,	yscond,	Pm, 0x93,(00) },
	{ ASETCS,	yscond,	Pm, 0x92,(00) },
	{ ASETEQ,	yscond,	Pm, 0x94,(00) },
	{ ASETGE,	yscond,	Pm, 0x9d,(00) },
	{ ASETGT,	yscond,	Pm, 0x9f,(00) },
	{ ASETHI,	yscond,	Pm, 0x97,(00) },
	{ ASETLE,	yscond,	Pm, 0x9e,(00) },
	{ ASETLS,	yscond,	Pm, 0x96,(00) },
	{ ASETLT,	yscond,	Pm, 0x9c,(00) },
	{ ASETMI,	yscond,	Pm, 0x98,(00) },
	{ ASETNE,	yscond,	Pm, 0x95,(00) },
	{ ASETOC,	yscond,	Pm, 0x91,(00) },
	{ ASETOS,	yscond,	Pm, 0x90,(00) },
	{ ASETPC,	yscond,	Pm, 0x96,(00) },
	{ ASETPL,	yscond,	Pm, 0x99,(00) },
	{ ASETPS,	yscond,	Pm, 0x9a,(00) },
	{ ACDQ,		ynone,	Px, 0x99 },
	{ ACWD,		ynone,	Pe, 0x99 },
	{ ASHLB,	yshb,	Pb, 0xd0,(04),0xc0,(04),0xd2,(04) },
	{ ASHLL,	yshl,	Px, 0xd1,(04),0xc1,(04),0xd3,(04),0xd3,(04) },
	{ ASHLW,	yshl,	Pe, 0xd1,(04),0xc1,(04),0xd3,(04),0xd3,(04) },
	{ ASHRB,	yshb,	Pb, 0xd0,(05),0xc0,(05),0xd2,(05) },
	{ ASHRL,	yshl,	Px, 0xd1,(05),0xc1,(05),0xd3,(05),0xd3,(05) },
	{ ASHRW,	yshl,	Pe, 0xd1,(05),0xc1,(05),0xd3,(05),0xd3,(05) },
	{ ASHUFPD,	yxshuf,	Pq, 0xc6 },
	{ ASHUFPS,	yxshuf,	Pm, 0xc6 },
	{ ASQRTPD,	yxm,	Pe, 0x51 },
	{ ASQRTPS,	yxm,	Pm, 0x51 },
	{ ASQRTSD,	yxm,	Pf2, 0x51 },
	{ ASQRTSS,	yxm,	Pf3, 0x51 },
	{ ASTC,		ynone,	Px, 0xf9 },
	{ ASTD,		ynone,	Px, 0xfd },
	{ ASTI,		ynone,	Px, 0xfb },
	{ ASTMXCSR,	ysvrs,	Pm, 0xae,(03),0xae,(03) },
	{ ASTOSB,	ynone,	Pb, 0xaa },
	{ ASTOSL,	ynone,	Px, 0xab },
	{ ASTOSW,	ynone,	Pe, 0xab },
	{ ASUBB,	yxorb,	Pb, 0x2c,0x80,(05),0x28,0x2a },
	{ ASUBL,	yaddl,	Px, 0x83,(05),0x2d,0x81,(05),0x29,0x2b },
	{ ASUBPD,	yxm,	Pe, 0x5c },
	{ ASUBPS,	yxm,	Pm, 0x5c },
	{ ASUBSD,	yxm,	Pf2, 0x5c },
	{ ASUBSS,	yxm,	Pf3, 0x5c },
	{ ASUBW,	yaddl,	Pe, 0x83,(05),0x2d,0x81,(05),0x29,0x2b },
	{ ASYSCALL,	ynone,	Px, 0xcd,100 },
	{ ATESTB,	ytestb,	Pb, 0xa8,0xf6,(00),0x84,0x84 },
	{ ATESTL,	ytestl,	Px, 0xa9,0xf7,(00),0x85,0x85 },
	{ ATESTW,	ytestl,	Pe, 0xa9,0xf7,(00),0x85,0x85 },
	{ ATEXT,	ytext,	Px },
	{ AUCOMISD,	yxcmp,	Pe, 0x2e },
	{ AUCOMISS,	yxcmp,	Pm, 0x2e },
	{ AUNPCKHPD,	yxm,	Pe, 0x15 },
	{ AUNPCKHPS,	yxm,	Pm, 0x15 },
	{ AUNPCKLPD,	yxm,	Pe, 0x14 },
	{ AUNPCKLPS,	yxm,	Pm, 0x14 },
	{ AVERR,	ydivl,	Pm, 0x00,(04) },
	{ AVERW,	ydivl,	Pm, 0x00,(05) },
	{ AWAIT,	ynone,	Px, 0x9b },
	{ AWORD,	ybyte,	Px, 2 },
	{ AXCHGB,	yml_mb,	Pb, 0x86,0x86 },
	{ AXCHGL,	yml_ml,	Px, 0x87,0x87 },
	{ AXCHGW,	yml_ml,	Pe, 0x87,0x87 },
	{ AXLAT,	ynone,	Px, 0xd7 },
	{ AXORB,	yxorb,	Pb, 0x34,0x80,(06),0x30,0x32 },
	{ AXORL,	yxorl,	Px, 0x83,(06),0x35,0x81,(06),0x31,0x33 },
	{ AXORPD,	yxm,	Pe, 0x57 },
	{ AXORPS,	yxm,	Pm, 0x57 },
	{ AXORW,	yxorl,	Pe, 0x83,(06),0x35,0x81,(06),0x31,0x33 },

	{ AFMOVB,	yfmvx,	Px, 0xdf,(04) },
	{ AFMOVBP,	yfmvp,	Px, 0xdf,(06) },
	{ AFMOVD,	yfmvd,	Px, 0xdd,(00),0xdd,(02),0xd9,(00),0xdd,(02) },
	{ AFMOVDP,	yfmvdp,	Px, 0xdd,(03),0xdd,(03) },
	{ AFMOVF,	yfmvf,	Px, 0xd9,(00),0xd9,(02) },
	{ AFMOVFP,	yfmvp,	Px, 0xd9,(03) },
	{ AFMOVL,	yfmvf,	Px, 0xdb,(00),0xdb,(02) },
	{ AFMOVLP,	yfmvp,	Px, 0xdb,(03) },
	{ AFMOVV,	yfmvx,	Px, 0xdf,(05) },
	{ AFMOVVP,	yfmvp,	Px, 0xdf,(07) },
	{ AFMOVW,	yfmvf,	Px, 0xdf,(00),0xdf,(02) },
	{ AFMOVWP,	yfmvp,	Px, 0xdf,(03) },
	{ AFMOVX,	yfmvx,	Px, 0xdb,(05) },
	{ AFMOVXP,	yfmvp,	Px, 0xdb,(07) },

	{ AFCOMB },
	{ AFCOMBP },
	{ AFCOMD,	yfadd,	Px, 0xdc,(02),0xd8,(02),0xdc,(02) },	/* botch */
	{ AFCOMDP,	yfadd,	Px, 0xdc,(03),0xd8,(03),0xdc,(03) },	/* botch */
	{ AFCOMDPP,	ycompp,	Px, 0xde,(03) },
	{ AFCOMF,	yfmvx,	Px, 0xd8,(02) },
	{ AFCOMFP,	yfmvx,	Px, 0xd8,(03) },
	{ AFCOML,	yfmvx,	Px, 0xda,(02) },
	{ AFCOMLP,	yfmvx,	Px, 0xda,(03) },
	{ AFCOMW,	yfmvx,	Px, 0xde,(02) },
	{ AFCOMWP,	yfmvx,	Px, 0xde,(03) },

	{ AFUCOM,	ycompp,	Px, 0xdd,(04) },
	{ AFUCOMP,	ycompp, Px, 0xdd,(05) },
	{ AFUCOMPP,	ycompp,	Px, 0xda,(13) },

	{ AFADDDP,	yfaddp,	Px, 0xde,(00) },
	{ AFADDW,	yfmvx,	Px, 0xde,(00) },
	{ AFADDL,	yfmvx,	Px, 0xda,(00) },
	{ AFADDF,	yfmvx,	Px, 0xd8,(00) },
	{ AFADDD,	yfadd,	Px, 0xdc,(00),0xd8,(00),0xdc,(00) },

	{ AFMULDP,	yfaddp,	Px, 0xde,(01) },
	{ AFMULW,	yfmvx,	Px, 0xde,(01) },
	{ AFMULL,	yfmvx,	Px, 0xda,(01) },
	{ AFMULF,	yfmvx,	Px, 0xd8,(01) },
	{ AFMULD,	yfadd,	Px, 0xdc,(01),0xd8,(01),0xdc,(01) },

	{ AFSUBDP,	yfaddp,	Px, 0xde,(05) },
	{ AFSUBW,	yfmvx,	Px, 0xde,(04) },
	{ AFSUBL,	yfmvx,	Px, 0xda,(04) },
	{ AFSUBF,	yfmvx,	Px, 0xd8,(04) },
	{ AFSUBD,	yfadd,	Px, 0xdc,(04),0xd8,(04),0xdc,(05) },

	{ AFSUBRDP,	yfaddp,	Px, 0xde,(04) },
	{ AFSUBRW,	yfmvx,	Px, 0xde,(05) },
	{ AFSUBRL,	yfmvx,	Px, 0xda,(05) },
	{ AFSUBRF,	yfmvx,	Px, 0xd8,(05) },
	{ AFSUBRD,	yfadd,	Px, 0xdc,(05),0xd8,(05),0xdc,(04) },

	{ AFDIVDP,	yfaddp,	Px, 0xde,(07) },
	{ AFDIVW,	yfmvx,	Px, 0xde,(06) },
	{ AFDIVL,	yfmvx,	Px, 0xda,(06) },
	{ AFDIVF,	yfmvx,	Px, 0xd8,(06) },
	{ AFDIVD,	yfadd,	Px, 0xdc,(06),0xd8,(06),0xdc,(07) },

	{ AFDIVRDP,	yfaddp,	Px, 0xde,(06) },
	{ AFDIVRW,	yfmvx,	Px, 0xde,(07) },
	{ AFDIVRL,	yfmvx,	Px, 0xda,(07) },
	{ AFDIVRF,	yfmvx,	Px, 0xd8,(07) },
	{ AFDIVRD,	yfadd,	Px, 0xdc,(07),0xd8,(07),0xdc,(06) },

	{ AFXCHD,	yfxch,	Px, 0xd9,(01),0xd9,(01) },
	{ AFFREE },
	{ AFLDCW,	ystcw,	Px, 0xd9,(05),0xd9,(05) },
	{ AFLDENV,	ystcw,	Px, 0xd9,(04),0xd9,(04) },
	{ AFRSTOR,	ysvrs,	Px, 0xdd,(04),0xdd,(04) },
	{ AFSAVE,	ysvrs,	Px, 0xdd,(06),0xdd,(06) },
	{ AFSTCW,	ystcw,	Px, 0xd9,(07),0xd9,(07) },
	{ AFSTENV,	ystcw,	Px, 0xd9,(06),0xd9,(06) },
	{ AFSTSW,	ystsw,	Px, 0xdd,(07),0xdf,0xe0 },
	{ AF2XM1,	ynone,	Px, 0xd9, 0xf0 },
	{ AFABS,	ynone,	Px, 0xd9, 0xe1 },
	{ AFCHS,	ynone,	Px, 0xd9, 0xe0 },
	{ AFCLEX,	ynone,	Px, 0xdb, 0xe2 },
	{ AFCOS,	ynone,	Px, 0xd9, 0xff },
	{ AFDECSTP,	ynone,	Px, 0xd9, 0xf6 },
	{ AFINCSTP,	ynone,	Px, 0xd9, 0xf7 },
	{ AFINIT,	ynone,	Px, 0xdb, 0xe3 },
	{ AFLD1,	ynone,	Px, 0xd9, 0xe8 },
	{ AFLDL2E,	ynone,	Px, 0xd9, 0xea },
	{ AFLDL2T,	ynone,	Px, 0xd9, 0xe9 },
	{ AFLDLG2,	ynone,	Px, 0xd9, 0xec },
	{ AFLDLN2,	ynone,	Px, 0xd9, 0xed },
	{ AFLDPI,	ynone,	Px, 0xd9, 0xeb },
	{ AFLDZ,	ynone,	Px, 0xd9, 0xee },
	{ AFNOP,	ynone,	Px, 0xd9, 0xd0 },
	{ AFPATAN,	ynone,	Px, 0xd9, 0xf3 },
	{ AFPREM,	ynone,	Px, 0xd9, 0xf8 },
	{ AFPREM1,	ynone,	Px, 0xd9, 0xf5 },
	{ AFPTAN,	ynone,	Px, 0xd9, 0xf2 },
	{ AFRNDINT,	ynone,	Px, 0xd9, 0xfc },
	{ AFSCALE,	ynone,	Px, 0xd9, 0xfd },
	{ AFSIN,	ynone,	Px, 0xd9, 0xfe },
	{ AFSINCOS,	ynone,	Px, 0xd9, 0xfb },
	{ AFSQRT,	ynone,	Px, 0xd9, 0xfa },
	{ AFTST,	ynone,	Px, 0xd9, 0xe4 },
	{ AFXAM,	ynone,	Px, 0xd9, 0xe5 },
	{ AFXTRACT,	ynone,	Px, 0xd9, 0xf4 },
	{ AFYL2X,	ynone,	Px, 0xd9, 0xf1 },
	{ AFYL2XP1,	ynone,	Px, 0xd9, 0xf9 },

	{ AEND },
	0
};

Optab*	opindex[ALAST+1];
