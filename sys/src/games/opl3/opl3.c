#include <u.h>
#include <libc.h>

typedef struct Op Op;
typedef struct Chan Chan;

#define Clk	14318180.0
#define FREQ_SH	16
#define EG_SH	16
#define LFO_SH	24
#define TIMER_SH	16
#define FREQ_MASK	((1 << FREQ_SH) - 1)
#define ENV_BITS	10
#define ENV_LEN	(1 << ENV_BITS)
#define ENV_STEP	(128.0 / ENV_LEN)
#define MAX_ATT_INDEX	((1 << ENV_BITS - 1) - 1)
#define MIN_ATT_INDEX	0
#define SIN_BITS	10
#define SIN_LEN	(1 << SIN_BITS)
#define SIN_MASK	(SIN_LEN - 1)
#define TL_RES_LEN	256

enum{
	EG_OFF,
	EG_REL,
	EG_SUS,
	EG_DEC,
	EG_ATT,
};

struct Op
{
	u32int ar;	/* attack rate: AR<<2 */
	u32int dr;	/* decay rate: DR<<2 */
	u32int rr;	/* release rate:RR<<2 */
	u8int KSR;	/* key scale rate */
	u8int ksl;	/* keyscale level */
	u8int ksr;	/* key scale rate: kcode>>KSR */
	u8int mul;	/* multiple: mul_tab[ML] */

	/* Phase Generator */
	u32int Cnt;	/* frequency counter */
	u32int Incr;	/* frequency counter step */
	u8int FB;	/* feedback shift value */
	s32int *connect;	/* slot output pointer */
	s32int op1_out[2];	/* slot1 output for feedback */
	u8int CON;	/* connection (algorithm) type */

	/* Envelope Generator */
	u8int eg_type;	/* percussive/non-percussive mode */
	u8int state;	/* phase type */
	u32int TL;	/* total level: TL << 2 */
	s32int TLL;	/* adjusted now TL */
	s32int volume;	/* envelope counter */
	u32int sl;	/* sustain level: sl_tab[SL] */

	u32int eg_m_ar;	/* (attack state) */
	u8int eg_sh_ar;	/* (attack state) */
	u8int eg_sel_ar;	/* (attack state) */
	u32int eg_m_dr;	/* (decay state) */
	u8int eg_sh_dr;	/* (decay state) */
	u8int eg_sel_dr;	/* (decay state) */
	u32int eg_m_rr;	/* (release state) */
	u8int eg_sh_rr;	/* (release state) */
	u8int eg_sel_rr;	/* (release state) */

	u32int key;

	u32int AMmask;	/* LFO Amplitude Modulation enable mask */
	u8int vib;	/* LFO Phase Modulation enable flag (active high)*/

	u8int waveform_number;
	uint wavetable;
};

struct Chan
{
	Op SLOT[2];
	u32int block_fnum;	/* block+fnum */
	u32int fc;	/* Freq. Increment base */
	u32int ksl_base;	/* KeyScaleLevel Base step */
	u8int kcode;	/* key code (for key scaling) */
	u8int extended;
};
static Chan chs[18];
static u32int pan[18*4];	/* channels output masks (0xffffffff = enable); 4 masks per one channel */
static u32int pan_ctrl_value[18];	/* output control values 1 per one channel (1 value contains 4 masks) */
static int chanout[18];
static int phase_modulation, phase_modulation2;	/* phase modulation input (SLOT 2 and 3/4) */
static u32int eg_cnt;	/* global envelope generator counter */
static u32int eg_timer;	/* global envelope generator counter works at frequency = chipclock/288 (288=8*36) */
static u32int eg_timer_add;	/* step of eg_timer */
static u32int eg_timer_overflow;	/* envelope generator timer overlfows every 1 sample (on real chip) */
static u32int fn_tab[1024];
static u32int LFO_AM;
static s32int LFO_PM;
static u8int lfo_am_depth, lfo_pm_depth_range;
static u32int lfo_am_cnt, lfo_am_inc, lfo_pm_cnt, lfo_pm_inc;
static u32int noise_rng, noise_p, noise_f;
static int OPL3_mode, nts;
static u8int rhythm;

static int slot_array[32]=
{
	0, 2, 4, 1, 3, 5,-1,-1,
	6, 8,10, 7, 9,11,-1,-1,
	12,14,16,13,15,17,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1
};

/* key scale level */
/* table is 3dB/octave , DV converts this into 6dB/octave */
/* 0.1875 is bit 0 weight of the envelope counter (volume) expressed in the 'decibel' scale */
#define DV (0.1875/2.0)
static u32int ksl_tab[8*16]=
{
	/* OCT 0 */
		0.000/DV, 0.000/DV, 0.000/DV, 0.000/DV,
		0.000/DV, 0.000/DV, 0.000/DV, 0.000/DV,
		0.000/DV, 0.000/DV, 0.000/DV, 0.000/DV,
		0.000/DV, 0.000/DV, 0.000/DV, 0.000/DV,
	/* OCT 1 */
		0.000/DV, 0.000/DV, 0.000/DV, 0.000/DV,
		0.000/DV, 0.000/DV, 0.000/DV, 0.000/DV,
		0.000/DV, 0.750/DV, 1.125/DV, 1.500/DV,
		1.875/DV, 2.250/DV, 2.625/DV, 3.000/DV,
	/* OCT 2 */
		0.000/DV, 0.000/DV, 0.000/DV, 0.000/DV,
		0.000/DV, 1.125/DV, 1.875/DV, 2.625/DV,
		3.000/DV, 3.750/DV, 4.125/DV, 4.500/DV,
		4.875/DV, 5.250/DV, 5.625/DV, 6.000/DV,
	/* OCT 3 */
		0.000/DV, 0.000/DV, 0.000/DV, 1.875/DV,
		3.000/DV, 4.125/DV, 4.875/DV, 5.625/DV,
		6.000/DV, 6.750/DV, 7.125/DV, 7.500/DV,
		7.875/DV, 8.250/DV, 8.625/DV, 9.000/DV,
	/* OCT 4 */
		0.000/DV, 0.000/DV, 3.000/DV, 4.875/DV,
		6.000/DV, 7.125/DV, 7.875/DV, 8.625/DV,
		9.000/DV, 9.750/DV,10.125/DV,10.500/DV,
		10.875/DV,11.250/DV,11.625/DV,12.000/DV,
	/* OCT 5 */
		0.000/DV, 3.000/DV, 6.000/DV, 7.875/DV,
		9.000/DV,10.125/DV,10.875/DV,11.625/DV,
		12.000/DV,12.750/DV,13.125/DV,13.500/DV,
		13.875/DV,14.250/DV,14.625/DV,15.000/DV,
	/* OCT 6 */
		0.000/DV, 6.000/DV, 9.000/DV,10.875/DV,
		12.000/DV,13.125/DV,13.875/DV,14.625/DV,
		15.000/DV,15.750/DV,16.125/DV,16.500/DV,
		16.875/DV,17.250/DV,17.625/DV,18.000/DV,
	/* OCT 7 */
		0.000/DV, 9.000/DV,12.000/DV,13.875/DV,
		15.000/DV,16.125/DV,16.875/DV,17.625/DV,
		18.000/DV,18.750/DV,19.125/DV,19.500/DV,
		19.875/DV,20.250/DV,20.625/DV,21.000/DV
};
#undef DV

/* 0 / 3.0 / 1.5 / 6.0 dB/OCT */
static u32int ksl_shift[4] = { 31, 1, 2, 0 };
/* sustain level table (3dB per step) */
/* 0 - 15: 0, 3, 6, 9,12,15,18,21,24,27,30,33,36,39,42,93 (dB)*/
#define SC(db) (u32int) (db * (2.0/ENV_STEP))
static u32int sl_tab[16]={
	SC(0),SC(1),SC(2),SC(3),SC(4),SC(5),SC(6),SC(7),
	SC(8),SC(9),SC(10),SC(11),SC(12),SC(13),SC(14),SC(31)
};
#undef SC
#define RATE_STEPS (8)
static uchar eg_inc[15*RATE_STEPS]={
/*cycle:0 1 2 3 4 5 6 7*/

/* 0 */ 0,1, 0,1, 0,1, 0,1, /* rates 00..12 0 (increment by 0 or 1) */
/* 1 */ 0,1, 0,1, 1,1, 0,1, /* rates 00..12 1 */
/* 2 */ 0,1, 1,1, 0,1, 1,1, /* rates 00..12 2 */
/* 3 */ 0,1, 1,1, 1,1, 1,1, /* rates 00..12 3 */

/* 4 */ 1,1, 1,1, 1,1, 1,1, /* rate 13 0 (increment by 1) */
/* 5 */ 1,1, 1,2, 1,1, 1,2, /* rate 13 1 */
/* 6 */ 1,2, 1,2, 1,2, 1,2, /* rate 13 2 */
/* 7 */ 1,2, 2,2, 1,2, 2,2, /* rate 13 3 */

/* 8 */ 2,2, 2,2, 2,2, 2,2, /* rate 14 0 (increment by 2) */
/* 9 */ 2,2, 2,4, 2,2, 2,4, /* rate 14 1 */
/*10 */ 2,4, 2,4, 2,4, 2,4, /* rate 14 2 */
/*11 */ 2,4, 4,4, 2,4, 4,4, /* rate 14 3 */

/*12 */ 4,4, 4,4, 4,4, 4,4, /* rates 15 0, 15 1, 15 2, 15 3 for decay */
/*13 */ 8,8, 8,8, 8,8, 8,8, /* rates 15 0, 15 1, 15 2, 15 3 for attack (zero time) */
/*14 */ 0,0, 0,0, 0,0, 0,0, /* infinity rates for attack and decay(s) */
};
#define O(a) (a*RATE_STEPS)

/* note that there is no O(13) in this table - it's directly in the code */
static uchar eg_rate_select[16+64+16]={ /* Envelope Generator rates (16 + 64 rates + 16 RKS) */
/* 16 infinite time rates */
O(14),O(14),O(14),O(14),O(14),O(14),O(14),O(14),
O(14),O(14),O(14),O(14),O(14),O(14),O(14),O(14),

/* rates 00-12 */
O(0),O(1),O(2),O(3),
O(0),O(1),O(2),O(3),
O(0),O(1),O(2),O(3),
O(0),O(1),O(2),O(3),
O(0),O(1),O(2),O(3),
O(0),O(1),O(2),O(3),
O(0),O(1),O(2),O(3),
O(0),O(1),O(2),O(3),
O(0),O(1),O(2),O(3),
O(0),O(1),O(2),O(3),
O(0),O(1),O(2),O(3),
O(0),O(1),O(2),O(3),
O(0),O(1),O(2),O(3),

/* rate 13 */
O(4),O(5),O(6),O(7),

/* rate 14 */
O(8),O(9),O(10),O(11),

/* rate 15 */
O(12),O(12),O(12),O(12),

/* 16 dummy rates (same as 15 3) */
O(12),O(12),O(12),O(12),O(12),O(12),O(12),O(12),
O(12),O(12),O(12),O(12),O(12),O(12),O(12),O(12),

};
#undef O

/*rate 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 */
/*shift 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 0, 0 */
/*mask 4095, 2047, 1023, 511, 255, 127, 63, 31, 15, 7, 3, 1, 0, 0, 0, 0 */

#define O(a) (a*1)
static uchar eg_rate_shift[16+64+16]={ /* Envelope Generator counter shifts (16 + 64 rates + 16 RKS) */
/* 16 infinite time rates */
O(0),O(0),O(0),O(0),O(0),O(0),O(0),O(0),
O(0),O(0),O(0),O(0),O(0),O(0),O(0),O(0),

/* rates 00-12 */
O(12),O(12),O(12),O(12),
O(11),O(11),O(11),O(11),
O(10),O(10),O(10),O(10),
O(9),O(9),O(9),O(9),
O(8),O(8),O(8),O(8),
O(7),O(7),O(7),O(7),
O(6),O(6),O(6),O(6),
O(5),O(5),O(5),O(5),
O(4),O(4),O(4),O(4),
O(3),O(3),O(3),O(3),
O(2),O(2),O(2),O(2),
O(1),O(1),O(1),O(1),
O(0),O(0),O(0),O(0),

/* rate 13 */
O(0),O(0),O(0),O(0),

/* rate 14 */
O(0),O(0),O(0),O(0),

/* rate 15 */
O(0),O(0),O(0),O(0),

/* 16 dummy rates (same as 15 3) */
O(0),O(0),O(0),O(0),O(0),O(0),O(0),O(0),
O(0),O(0),O(0),O(0),O(0),O(0),O(0),O(0),

};
#undef O
/* multiple table */
#define ML 2
static u8int mul_tab[16]= {
/* 1/2, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,10,12,12,15,15 */
	ML/2, 1*ML, 2*ML, 3*ML, 4*ML, 5*ML, 6*ML, 7*ML,
	8*ML, 9*ML,10*ML,10*ML,12*ML,12*ML,15*ML,15*ML
};
#undef ML

#define TL_TAB_LEN (13*2*TL_RES_LEN)
static int tl_tab[TL_TAB_LEN];

#define ENV_QUIET (TL_TAB_LEN>>4)

static uint sin_tab[SIN_LEN * 8];

#define LFO_AM_TAB_ELEMENTS 210

static u8int lfo_am_table[LFO_AM_TAB_ELEMENTS] = {
0,0,0,0,0,0,0,
1,1,1,1,
2,2,2,2,
3,3,3,3,
4,4,4,4,
5,5,5,5,
6,6,6,6,
7,7,7,7,
8,8,8,8,
9,9,9,9,
10,10,10,10,
11,11,11,11,
12,12,12,12,
13,13,13,13,
14,14,14,14,
15,15,15,15,
16,16,16,16,
17,17,17,17,
18,18,18,18,
19,19,19,19,
20,20,20,20,
21,21,21,21,
22,22,22,22,
23,23,23,23,
24,24,24,24,
25,25,25,25,
26,26,26,
25,25,25,25,
24,24,24,24,
23,23,23,23,
22,22,22,22,
21,21,21,21,
20,20,20,20,
19,19,19,19,
18,18,18,18,
17,17,17,17,
16,16,16,16,
15,15,15,15,
14,14,14,14,
13,13,13,13,
12,12,12,12,
11,11,11,11,
10,10,10,10,
9,9,9,9,
8,8,8,8,
7,7,7,7,
6,6,6,6,
5,5,5,5,
4,4,4,4,
3,3,3,3,
2,2,2,2,
1,1,1,1
};

/* LFO Phase Modulation table (verified on real YM3812) */
static s8int lfo_pm_table[8*8*2] = {
/* FNUM2/FNUM = 00 0xxxxxxx (0x0000) */
0, 0, 0, 0, 0, 0, 0, 0, /*LFO PM depth = 0*/
0, 0, 0, 0, 0, 0, 0, 0, /*LFO PM depth = 1*/

/* FNUM2/FNUM = 00 1xxxxxxx (0x0080) */
0, 0, 0, 0, 0, 0, 0, 0, /*LFO PM depth = 0*/
1, 0, 0, 0,-1, 0, 0, 0, /*LFO PM depth = 1*/

/* FNUM2/FNUM = 01 0xxxxxxx (0x0100) */
1, 0, 0, 0,-1, 0, 0, 0, /*LFO PM depth = 0*/
2, 1, 0,-1,-2,-1, 0, 1, /*LFO PM depth = 1*/

/* FNUM2/FNUM = 01 1xxxxxxx (0x0180) */
1, 0, 0, 0,-1, 0, 0, 0, /*LFO PM depth = 0*/
3, 1, 0,-1,-3,-1, 0, 1, /*LFO PM depth = 1*/

/* FNUM2/FNUM = 10 0xxxxxxx (0x0200) */
2, 1, 0,-1,-2,-1, 0, 1, /*LFO PM depth = 0*/
4, 2, 0,-2,-4,-2, 0, 2, /*LFO PM depth = 1*/

/* FNUM2/FNUM = 10 1xxxxxxx (0x0280) */
2, 1, 0,-1,-2,-1, 0, 1, /*LFO PM depth = 0*/
5, 2, 0,-2,-5,-2, 0, 2, /*LFO PM depth = 1*/

/* FNUM2/FNUM = 11 0xxxxxxx (0x0300) */
3, 1, 0,-1,-3,-1, 0, 1, /*LFO PM depth = 0*/
6, 3, 0,-3,-6,-3, 0, 3, /*LFO PM depth = 1*/

/* FNUM2/FNUM = 11 1xxxxxxx (0x0380) */
3, 1, 0,-1,-3,-1, 0, 1, /*LFO PM depth = 0*/
7, 3, 0,-3,-7,-3, 0, 3 /*LFO PM depth = 1*/
};

#define SLOT7_1 (&chs[7].SLOT[0])
#define SLOT7_2 (&chs[7].SLOT[1])
#define SLOT8_1 (&chs[8].SLOT[0])
#define SLOT8_2 (&chs[8].SLOT[1])

static void
advance_lfo(void)
{
	u8int tmp;

	lfo_am_cnt += lfo_am_inc;
	if(lfo_am_cnt >= ((u32int)LFO_AM_TAB_ELEMENTS<<LFO_SH))
		lfo_am_cnt -= ((u32int)LFO_AM_TAB_ELEMENTS<<LFO_SH);
	tmp = lfo_am_table[lfo_am_cnt >> LFO_SH];
	if(lfo_am_depth)
		LFO_AM = tmp;
	else
		LFO_AM = tmp>>2;
	lfo_pm_cnt += lfo_pm_inc;
	LFO_PM = (lfo_pm_cnt>>LFO_SH & 7) | lfo_pm_depth_range;
}

static void
advance(void)
{
	Chan *CH;
	Op *op;
	int i;

	eg_timer += eg_timer_add;
	while (eg_timer >= eg_timer_overflow){
		eg_timer -= eg_timer_overflow;
		eg_cnt++;
		for (i=0; i<9*2*2; i++){
			CH = &chs[i/2];
			op = &CH->SLOT[i&1];
			switch(op->state){
			case EG_ATT:
				if(!(eg_cnt & op->eg_m_ar)){
					op->volume += (s32int)(~op->volume *
												(eg_inc[op->eg_sel_ar + ((eg_cnt>>op->eg_sh_ar)&7)])
												) >>3;
					if(op->volume <= MIN_ATT_INDEX){
						op->volume = MIN_ATT_INDEX;
						op->state = EG_DEC;
					}
				}
				break;
			case EG_DEC:
				if(!(eg_cnt & op->eg_m_dr)){
					op->volume += eg_inc[op->eg_sel_dr + ((eg_cnt>>op->eg_sh_dr)&7)];
					if(op->volume >= op->sl)
						op->state = EG_SUS;
				}
				break;
			case EG_SUS:
				if(op->eg_type)
				{
				}else{
					if(!(eg_cnt & op->eg_m_rr)){
						op->volume += eg_inc[op->eg_sel_rr + ((eg_cnt>>op->eg_sh_rr)&7)];
						if(op->volume >= MAX_ATT_INDEX)
							op->volume = MAX_ATT_INDEX;
					}
				}
				break;
			case EG_REL:
				if(!(eg_cnt & op->eg_m_rr)){
					op->volume += eg_inc[op->eg_sel_rr + ((eg_cnt>>op->eg_sh_rr)&7)];
					if(op->volume >= MAX_ATT_INDEX){
						op->volume = MAX_ATT_INDEX;
						op->state = EG_OFF;
					}
				}
				break;
			}
		}
	}
	for (i=0; i<9*2*2; i++){
		CH = &chs[i/2];
		op = &CH->SLOT[i&1];
		if(op->vib){
			u8int block;
			uint block_fnum = CH->block_fnum;
			uint fnum_lfo = (block_fnum&0x0380) >> 7;
			int lfo_fn_table_index_offset = lfo_pm_table[LFO_PM + 16*fnum_lfo];
			if(lfo_fn_table_index_offset){
				block_fnum += lfo_fn_table_index_offset;
				block = (block_fnum&0x1c00) >> 10;
				op->Cnt += (fn_tab[block_fnum&0x03ff] >> (7-block)) * op->mul;
			}else
				op->Cnt += op->Incr;
		}else
			op->Cnt += op->Incr;
	}
	noise_p += noise_f;
	i = noise_p >> FREQ_SH;
	noise_p &= FREQ_MASK;
	while (i){
		if(noise_rng & 1) noise_rng ^= 0x800302;
		noise_rng >>= 1;
		i--;
	}
}

static int
op_calc(u32int phase, uint env, int pm, uint wave_tab)
{
	u32int p;

	p = (env<<4) + sin_tab[wave_tab + ((((int)((phase & ~FREQ_MASK) + (pm<<16))) >> FREQ_SH) & SIN_MASK)];
	if(p >= TL_TAB_LEN)
		return 0;
	return tl_tab[p];
}

static int
op_calc1(u32int phase, uint env, int pm, uint wave_tab)
{
	u32int p;

	p = (env<<4) + sin_tab[wave_tab + ((((int)((phase & ~FREQ_MASK) + pm))>>FREQ_SH) & SIN_MASK)];
	if(p >= TL_TAB_LEN)
		return 0;
	return tl_tab[p];
}

#define volume_calc(OP) ((OP)->TLL + ((u32int)(OP)->volume) + (LFO_AM & (OP)->AMmask))

static void
chan_calc(Chan *CH)
{
	Op *SLOT;
	uint env;
	int out;

	phase_modulation = 0;
	phase_modulation2= 0;
	SLOT = &CH->SLOT[0];
	env = volume_calc(SLOT);
	out = SLOT->op1_out[0] + SLOT->op1_out[1];
	SLOT->op1_out[0] = SLOT->op1_out[1];
	SLOT->op1_out[1] = 0;
	if(env < ENV_QUIET){
		if(!SLOT->FB)
			out = 0;
		SLOT->op1_out[1] = op_calc1(SLOT->Cnt, env, (out<<SLOT->FB), SLOT->wavetable);
	}
	*SLOT->connect += SLOT->op1_out[1];
	SLOT++;
	env = volume_calc(SLOT);
	if(env < ENV_QUIET)
		*SLOT->connect += op_calc(SLOT->Cnt, env, phase_modulation, SLOT->wavetable);
}

static void
chan_calc_ext(Chan *CH)
{
	Op *SLOT;
	uint env;

	phase_modulation = 0;
	SLOT = &CH->SLOT[0];
	env = volume_calc(SLOT);
	if(env < ENV_QUIET)
		*SLOT->connect += op_calc(SLOT->Cnt, env, phase_modulation2, SLOT->wavetable);
	SLOT++;
	env = volume_calc(SLOT);
	if(env < ENV_QUIET)
		*SLOT->connect += op_calc(SLOT->Cnt, env, phase_modulation, SLOT->wavetable);
}

static void
chan_calc_rhythm(Chan *CH, uint noise)
{
	Op *SLOT;
	int out;
	uint env;

	phase_modulation = 0;
	SLOT = &CH[6].SLOT[0];
	env = volume_calc(SLOT);
	out = SLOT->op1_out[0] + SLOT->op1_out[1];
	SLOT->op1_out[0] = SLOT->op1_out[1];
	if(!SLOT->CON)
		phase_modulation = SLOT->op1_out[0];
	SLOT->op1_out[1] = 0;
	if(env < ENV_QUIET){
		if(!SLOT->FB)
			out = 0;
		SLOT->op1_out[1] = op_calc1(SLOT->Cnt, env, (out<<SLOT->FB), SLOT->wavetable);
	}
	SLOT++;
	env = volume_calc(SLOT);
	if(env < ENV_QUIET)
		chanout[6] += op_calc(SLOT->Cnt, env, phase_modulation, SLOT->wavetable) * 2;
	env = volume_calc(SLOT7_1);
	if(env < ENV_QUIET){
		uchar bit7 = ((SLOT7_1->Cnt>>FREQ_SH)>>7)&1;
		uchar bit3 = ((SLOT7_1->Cnt>>FREQ_SH)>>3)&1;
		uchar bit2 = ((SLOT7_1->Cnt>>FREQ_SH)>>2)&1;
		uchar res1 = (bit2 ^ bit7) | bit3;
		u32int phase = res1 ? (0x200|(0xd0>>2)) : 0xd0;
		uchar bit5e= ((SLOT8_2->Cnt>>FREQ_SH)>>5)&1;
		uchar bit3e= ((SLOT8_2->Cnt>>FREQ_SH)>>3)&1;
		uchar res2 = (bit3e ^ bit5e);
		if(res2)
			phase = (0x200|(0xd0>>2));
		if(phase&0x200){
			if(noise)
				phase = 0x200|0xd0;
		}else
		{
			if(noise)
				phase = 0xd0>>2;
		}
		chanout[7] += op_calc(phase<<FREQ_SH, env, 0, SLOT7_1->wavetable) * 2;
	}
	env = volume_calc(SLOT7_2);
	if(env < ENV_QUIET){
		uchar bit8 = ((SLOT7_1->Cnt>>FREQ_SH)>>8)&1;
		u32int phase = bit8 ? 0x200 : 0x100;
		if(noise)
			phase ^= 0x100;
		chanout[7] += op_calc(phase<<FREQ_SH, env, 0, SLOT7_2->wavetable) * 2;
	}
	env = volume_calc(SLOT8_1);
	if(env < ENV_QUIET)
		chanout[8] += op_calc(SLOT8_1->Cnt, env, 0, SLOT8_1->wavetable) * 2;
	env = volume_calc(SLOT8_2);
	if(env < ENV_QUIET){
		uchar bit7 = ((SLOT7_1->Cnt>>FREQ_SH)>>7)&1;
		uchar bit3 = ((SLOT7_1->Cnt>>FREQ_SH)>>3)&1;
		uchar bit2 = ((SLOT7_1->Cnt>>FREQ_SH)>>2)&1;
		uchar res1 = (bit2 ^ bit7) | bit3;
		u32int phase = res1 ? 0x300 : 0x100;
		uchar bit5e= ((SLOT8_2->Cnt>>FREQ_SH)>>5)&1;
		uchar bit3e= ((SLOT8_2->Cnt>>FREQ_SH)>>3)&1;
		uchar res2 = (bit3e ^ bit5e);
		if(res2)
			phase = 0x300;
		chanout[8] += op_calc(phase<<FREQ_SH, env, 0, SLOT8_2->wavetable) * 2;
	}
}

static void
FM_KEYON(Op *SLOT, u32int key_set)
{
	if(!SLOT->key){
		SLOT->Cnt = 0;
		SLOT->state = EG_ATT;
	}
	SLOT->key |= key_set;
}

static void
FM_KEYOFF(Op *SLOT, u32int key_clr)
{
	if(SLOT->key){
		SLOT->key &= key_clr;
		if(!SLOT->key){
			if(SLOT->state>EG_REL)
				SLOT->state = EG_REL;
		}
	}
}

/* update phase increment counter of operator (also update the EG rates if necessary) */
static void
CALC_FCSLOT(Chan *CH, Op *SLOT)
{
	int ksr;

	SLOT->Incr = CH->fc * SLOT->mul;
	ksr = CH->kcode >> SLOT->KSR;
	if(SLOT->ksr != ksr){
		SLOT->ksr = ksr;
		if((SLOT->ar + SLOT->ksr) < 16+60){
			SLOT->eg_sh_ar = eg_rate_shift [SLOT->ar + SLOT->ksr];
			SLOT->eg_m_ar = (1<<SLOT->eg_sh_ar)-1;
			SLOT->eg_sel_ar = eg_rate_select[SLOT->ar + SLOT->ksr];
		}else{
			SLOT->eg_sh_ar = 0;
			SLOT->eg_m_ar = (1<<SLOT->eg_sh_ar)-1;
			SLOT->eg_sel_ar = 13*RATE_STEPS;
		}
		SLOT->eg_sh_dr = eg_rate_shift [SLOT->dr + SLOT->ksr];
		SLOT->eg_m_dr = (1<<SLOT->eg_sh_dr)-1;
		SLOT->eg_sel_dr = eg_rate_select[SLOT->dr + SLOT->ksr];
		SLOT->eg_sh_rr = eg_rate_shift [SLOT->rr + SLOT->ksr];
		SLOT->eg_m_rr = (1<<SLOT->eg_sh_rr)-1;
		SLOT->eg_sel_rr = eg_rate_select[SLOT->rr + SLOT->ksr];
	}
}

static void
set_mul(int slot, int v)
{
	Chan *CH = &chs[slot/2];
	Op *SLOT = &CH->SLOT[slot&1];

	SLOT->mul = mul_tab[v&0x0f];
	SLOT->KSR = (v&0x10) ? 0 : 2;
	SLOT->eg_type = (v&0x20);
	SLOT->vib = (v&0x40);
	SLOT->AMmask = (v&0x80) ? ~0 : 0;
	if(OPL3_mode & 1){
		int chan_no = slot/2;
		switch(chan_no){
		case 0: case 1: case 2: case 9: case 10: case 11:
			CALC_FCSLOT(CH,SLOT);
			break;
		case 3: case 4: case 5: case 12: case 13: case 14:
			if((CH-3)->extended)
				CALC_FCSLOT(CH-3,SLOT);
			else
				CALC_FCSLOT(CH,SLOT);
			break;
		default:
			CALC_FCSLOT(CH,SLOT);
			break;
		}
	}else{
		CALC_FCSLOT(CH,SLOT);
	}
}

static void
set_ksl_tl(int slot, int v)
{
	Chan *CH = &chs[slot/2];
	Op *SLOT = &CH->SLOT[slot&1];

	SLOT->ksl = ksl_shift[v >> 6];
	SLOT->TL = (v&0x3f)<<(ENV_BITS-1-7); /* 7 bits TL (bit 6 = always 0) */
	if(OPL3_mode & 1){
		int chan_no = slot/2;
		switch(chan_no){
		case 0: case 1: case 2: case 9: case 10: case 11:
			SLOT->TLL = SLOT->TL + (CH->ksl_base>>SLOT->ksl);
			break;
		case 3: case 4: case 5: case 12: case 13: case 14:
			if((CH-3)->extended)
				SLOT->TLL = SLOT->TL + ((CH-3)->ksl_base>>SLOT->ksl);
			else
				SLOT->TLL = SLOT->TL + (CH->ksl_base>>SLOT->ksl);
			break;
		default:
			SLOT->TLL = SLOT->TL + (CH->ksl_base>>SLOT->ksl);
			break;
		}
	}else
		SLOT->TLL = SLOT->TL + (CH->ksl_base>>SLOT->ksl);
}

static void
set_ar_dr(int slot, int v)
{
	Chan *CH = &chs[slot/2];
	Op *SLOT = &CH->SLOT[slot&1];

	SLOT->ar = (v>>4) ? 16 + ((v>>4) <<2) : 0;
	if((SLOT->ar + SLOT->ksr) < 16+60){
		SLOT->eg_sh_ar = eg_rate_shift [SLOT->ar + SLOT->ksr];
		SLOT->eg_m_ar = (1<<SLOT->eg_sh_ar)-1;
		SLOT->eg_sel_ar = eg_rate_select[SLOT->ar + SLOT->ksr];
	}else{
		SLOT->eg_sh_ar = 0;
		SLOT->eg_m_ar = (1<<SLOT->eg_sh_ar)-1;
		SLOT->eg_sel_ar = 13*RATE_STEPS;
	}
	SLOT->dr = (v&0x0f)? 16 + ((v&0x0f)<<2) : 0;
	SLOT->eg_sh_dr = eg_rate_shift [SLOT->dr + SLOT->ksr];
	SLOT->eg_m_dr = (1<<SLOT->eg_sh_dr)-1;
	SLOT->eg_sel_dr = eg_rate_select[SLOT->dr + SLOT->ksr];
}

static void
set_sl_rr(int slot, int v)
{
	Chan *CH = &chs[slot/2];
	Op *SLOT = &CH->SLOT[slot&1];

	SLOT->sl = sl_tab[v>>4];
	SLOT->rr = (v&0x0f)? 16 + ((v&0x0f)<<2) : 0;
	SLOT->eg_sh_rr = eg_rate_shift [SLOT->rr + SLOT->ksr];
	SLOT->eg_m_rr = (1<<SLOT->eg_sh_rr)-1;
	SLOT->eg_sel_rr = eg_rate_select[SLOT->rr + SLOT->ksr];
}

void
opl3wr(int r, int v)
{
	Chan *CH;
	uint ch_offset = 0;
	int slot;
	int block_fnum;

	v &= 0xff;
	if(r&0x100){
		switch(r){
		case 0x101:
			return;
		case 0x104:
			CH = &chs[0];
			CH->extended = (v>>0) & 1;
			CH++;
			CH->extended = (v>>1) & 1;
			CH++;
			CH->extended = (v>>2) & 1;
			CH = &chs[9];
			CH->extended = (v>>3) & 1;
			CH++;
			CH->extended = (v>>4) & 1;
			CH++;
			CH->extended = (v>>5) & 1;
			return;
		case 0x105:
			OPL3_mode = v & 1;
			return;
		}
		ch_offset = 9;
	}
	r &= 0xff;
	v &= 0xff;
	switch(r&0xe0){
	case 0x00:
		switch(r&0x1f){
		case 0x08:
			nts = v;
			break;
		}
		break;
	case 0x20:
		slot = slot_array[r&0x1f];
		if(slot < 0) return;
		set_mul(slot + ch_offset*2, v);
		break;
	case 0x40:
		slot = slot_array[r&0x1f];
		if(slot < 0) return;
		set_ksl_tl(slot + ch_offset*2, v);
		break;
	case 0x60:
		slot = slot_array[r&0x1f];
		if(slot < 0) return;
		set_ar_dr(slot + ch_offset*2, v);
		break;
	case 0x80:
		slot = slot_array[r&0x1f];
		if(slot < 0) return;
		set_sl_rr(slot + ch_offset*2, v);
		break;
	case 0xa0:
		if(r == 0xbd){
			if(ch_offset != 0)
				return;
			lfo_am_depth = v & 0x80;
			lfo_pm_depth_range = (v&0x40) ? 8 : 0;
			rhythm = v & 0x3f;
			if(rhythm & 0x20){
				if(v&0x10){
					FM_KEYON (&chs[6].SLOT[0], 2);
					FM_KEYON (&chs[6].SLOT[1], 2);
				}else{
					FM_KEYOFF(&chs[6].SLOT[0],~2);
					FM_KEYOFF(&chs[6].SLOT[1],~2);
				}
				if(v&0x01) FM_KEYON (&chs[7].SLOT[0], 2);
				else FM_KEYOFF(&chs[7].SLOT[0],~2);
				if(v&0x08) FM_KEYON (&chs[7].SLOT[1], 2);
				else FM_KEYOFF(&chs[7].SLOT[1],~2);
				if(v&0x04) FM_KEYON (&chs[8].SLOT[0], 2);
				else FM_KEYOFF(&chs[8].SLOT[0],~2);
				if(v&0x02) FM_KEYON (&chs[8].SLOT[1], 2);
				else FM_KEYOFF(&chs[8].SLOT[1],~2);
			}else{
				FM_KEYOFF(&chs[6].SLOT[0],~2);
				FM_KEYOFF(&chs[6].SLOT[1],~2);
				FM_KEYOFF(&chs[7].SLOT[0],~2);
				FM_KEYOFF(&chs[7].SLOT[1],~2);
				FM_KEYOFF(&chs[8].SLOT[0],~2);
				FM_KEYOFF(&chs[8].SLOT[1],~2);
			}
			return;
		}
		if((r&0x0f) > 8) return;
		CH = &chs[(r&0x0f) + ch_offset];
		if(!(r&0x10)){
			block_fnum = (CH->block_fnum&0x1f00) | v;
		}else{
			block_fnum = ((v&0x1f)<<8) | (CH->block_fnum&0xff);
			if(OPL3_mode & 1){
				int chan_no = (r&0x0f) + ch_offset;
				switch(chan_no){
				case 0: case 1: case 2: case 9: case 10: case 11:
					if(CH->extended){
						if(v&0x20){
							FM_KEYON (&CH->SLOT[0], 1);
							FM_KEYON (&CH->SLOT[1], 1);
							FM_KEYON (&(CH+3)->SLOT[0], 1);
							FM_KEYON (&(CH+3)->SLOT[1], 1);
						}else{
							FM_KEYOFF(&CH->SLOT[0],~1);
							FM_KEYOFF(&CH->SLOT[1],~1);
							FM_KEYOFF(&(CH+3)->SLOT[0],~1);
							FM_KEYOFF(&(CH+3)->SLOT[1],~1);
						}
					}else{
						if(v&0x20){
							FM_KEYON (&CH->SLOT[0], 1);
							FM_KEYON (&CH->SLOT[1], 1);
						}else{
							FM_KEYOFF(&CH->SLOT[0],~1);
							FM_KEYOFF(&CH->SLOT[1],~1);
						}
					}
					break;
				case 3: case 4: case 5: case 12: case 13: case 14:
					if((CH-3)->extended){
					}else{
						if(v&0x20){
							FM_KEYON (&CH->SLOT[0], 1);
							FM_KEYON (&CH->SLOT[1], 1);
						}else{
							FM_KEYOFF(&CH->SLOT[0],~1);
							FM_KEYOFF(&CH->SLOT[1],~1);
						}
					}
					break;
				default:
					if(v&0x20){
						FM_KEYON (&CH->SLOT[0], 1);
						FM_KEYON (&CH->SLOT[1], 1);
					}else{
						FM_KEYOFF(&CH->SLOT[0],~1);
						FM_KEYOFF(&CH->SLOT[1],~1);
					}
					break;
				}
			}else{
				if(v&0x20){
					FM_KEYON (&CH->SLOT[0], 1);
					FM_KEYON (&CH->SLOT[1], 1);
				}else{
					FM_KEYOFF(&CH->SLOT[0],~1);
					FM_KEYOFF(&CH->SLOT[1],~1);
				}
			}
		}
		if(CH->block_fnum != block_fnum){
			u8int block = block_fnum >> 10;
			CH->block_fnum = block_fnum;
			CH->ksl_base = ksl_tab[block_fnum>>6];
			CH->fc = fn_tab[block_fnum&0x03ff] >> (7-block);
			CH->kcode = (CH->block_fnum&0x1c00)>>9;
			if(nts&0x40)
				CH->kcode |= (CH->block_fnum&0x100)>>8;
			else
				CH->kcode |= (CH->block_fnum&0x200)>>9;
			if(OPL3_mode & 1){
				int chan_no = (r&0x0f) + ch_offset;
				switch(chan_no){
				case 0: case 1: case 2: case 9: case 10: case 11:
					if(CH->extended){
						CH->SLOT[0].TLL = CH->SLOT[0].TL + (CH->ksl_base>>CH->SLOT[0].ksl);
						CH->SLOT[1].TLL = CH->SLOT[1].TL + (CH->ksl_base>>CH->SLOT[1].ksl);
						(CH+3)->SLOT[0].TLL = (CH+3)->SLOT[0].TL + (CH->ksl_base>>(CH+3)->SLOT[0].ksl);
						(CH+3)->SLOT[1].TLL = (CH+3)->SLOT[1].TL + (CH->ksl_base>>(CH+3)->SLOT[1].ksl);
						CALC_FCSLOT(CH,&CH->SLOT[0]);
						CALC_FCSLOT(CH,&CH->SLOT[1]);
						CALC_FCSLOT(CH,&(CH+3)->SLOT[0]);
						CALC_FCSLOT(CH,&(CH+3)->SLOT[1]);
					}else{
						CH->SLOT[0].TLL = CH->SLOT[0].TL + (CH->ksl_base>>CH->SLOT[0].ksl);
						CH->SLOT[1].TLL = CH->SLOT[1].TL + (CH->ksl_base>>CH->SLOT[1].ksl);
						CALC_FCSLOT(CH,&CH->SLOT[0]);
						CALC_FCSLOT(CH,&CH->SLOT[1]);
					}
					break;
				case 3: case 4: case 5: case 12: case 13: case 14:
					if((CH-3)->extended){
					}else{
						CH->SLOT[0].TLL = CH->SLOT[0].TL + (CH->ksl_base>>CH->SLOT[0].ksl);
						CH->SLOT[1].TLL = CH->SLOT[1].TL + (CH->ksl_base>>CH->SLOT[1].ksl);
						CALC_FCSLOT(CH,&CH->SLOT[0]);
						CALC_FCSLOT(CH,&CH->SLOT[1]);
					}
					break;
				default:
					CH->SLOT[0].TLL = CH->SLOT[0].TL + (CH->ksl_base>>CH->SLOT[0].ksl);
					CH->SLOT[1].TLL = CH->SLOT[1].TL + (CH->ksl_base>>CH->SLOT[1].ksl);
					CALC_FCSLOT(CH,&CH->SLOT[0]);
					CALC_FCSLOT(CH,&CH->SLOT[1]);
					break;
				}
			}else{
				CH->SLOT[0].TLL = CH->SLOT[0].TL + (CH->ksl_base>>CH->SLOT[0].ksl);
				CH->SLOT[1].TLL = CH->SLOT[1].TL + (CH->ksl_base>>CH->SLOT[1].ksl);
				CALC_FCSLOT(CH,&CH->SLOT[0]);
				CALC_FCSLOT(CH,&CH->SLOT[1]);
			}
		}
		break;
	case 0xc0:
		if((r&0xf) > 8) return;
		CH = &chs[(r&0xf) + ch_offset];
		if(OPL3_mode & 1){
			int base = ((r&0xf) + ch_offset) * 4;
			pan[base] = (v & 0x10) ? ~0 : 0;
			pan[base +1] = (v & 0x20) ? ~0 : 0;
			pan[base +2] = (v & 0x40) ? ~0 : 0;
			pan[base +3] = (v & 0x80) ? ~0 : 0;
		}else{
			int base = ((r&0xf) + ch_offset) * 4;
			pan[base] = ~0;
			pan[base +1] = ~0;
			pan[base +2] = ~0;
			pan[base +3] = ~0;
		}
		pan_ctrl_value[(r&0xf) + ch_offset] = v;
		CH->SLOT[0].FB = (v>>1)&7 ? ((v>>1)&7) + 7 : 0;
		CH->SLOT[0].CON = v&1;
		if(OPL3_mode & 1){
			int chan_no = (r&0x0f) + ch_offset;
			switch(chan_no){
			case 0: case 1: case 2: case 9: case 10: case 11:
				if(CH->extended){
					u8int conn = (CH->SLOT[0].CON<<1) | ((CH+3)->SLOT[0].CON<<0);
					switch(conn){
					case 0:
						CH->SLOT[0].connect = &phase_modulation;
						CH->SLOT[1].connect = &phase_modulation2;
						(CH+3)->SLOT[0].connect = &phase_modulation;
						(CH+3)->SLOT[1].connect = &chanout[chan_no + 3];
						break;
					case 1:
						CH->SLOT[0].connect = &phase_modulation;
						CH->SLOT[1].connect = &chanout[chan_no];
						(CH+3)->SLOT[0].connect = &phase_modulation;
						(CH+3)->SLOT[1].connect = &chanout[chan_no + 3];
						break;
					case 2:
						CH->SLOT[0].connect = &chanout[chan_no];
						CH->SLOT[1].connect = &phase_modulation2;
						(CH+3)->SLOT[0].connect = &phase_modulation;
						(CH+3)->SLOT[1].connect = &chanout[chan_no + 3];
						break;
					case 3:
						CH->SLOT[0].connect = &chanout[chan_no];
						CH->SLOT[1].connect = &phase_modulation2;
						(CH+3)->SLOT[0].connect = &chanout[chan_no + 3];
						(CH+3)->SLOT[1].connect = &chanout[chan_no + 3];
						break;
					}
				}else{
					CH->SLOT[0].connect = CH->SLOT[0].CON ? &chanout[(r&0xf)+ch_offset] : &phase_modulation;
					CH->SLOT[1].connect = &chanout[(r&0xf)+ch_offset];
				}
				break;
			case 3: case 4: case 5: case 12: case 13: case 14:
				if((CH-3)->extended){
					u8int conn = ((CH-3)->SLOT[0].CON<<1) | (CH->SLOT[0].CON<<0);
					switch(conn){
					case 0:
						(CH-3)->SLOT[0].connect = &phase_modulation;
						(CH-3)->SLOT[1].connect = &phase_modulation2;
						CH->SLOT[0].connect = &phase_modulation;
						CH->SLOT[1].connect = &chanout[chan_no];
						break;
					case 1:
						(CH-3)->SLOT[0].connect = &phase_modulation;
						(CH-3)->SLOT[1].connect = &chanout[chan_no - 3];
						CH->SLOT[0].connect = &phase_modulation;
						CH->SLOT[1].connect = &chanout[chan_no];
						break;
					case 2:
						(CH-3)->SLOT[0].connect = &chanout[chan_no - 3];
						(CH-3)->SLOT[1].connect = &phase_modulation2;
						CH->SLOT[0].connect = &phase_modulation;
						CH->SLOT[1].connect = &chanout[chan_no];
						break;
					case 3:
						(CH-3)->SLOT[0].connect = &chanout[chan_no - 3];
						(CH-3)->SLOT[1].connect = &phase_modulation2;
						CH->SLOT[0].connect = &chanout[chan_no];
						CH->SLOT[1].connect = &chanout[chan_no];
						break;
					}
				}else{
					CH->SLOT[0].connect = CH->SLOT[0].CON ? &chanout[(r&0xf)+ch_offset] : &phase_modulation;
					CH->SLOT[1].connect = &chanout[(r&0xf)+ch_offset];
				}
				break;
			default:
					CH->SLOT[0].connect = CH->SLOT[0].CON ? &chanout[(r&0xf)+ch_offset] : &phase_modulation;
					CH->SLOT[1].connect = &chanout[(r&0xf)+ch_offset];
				break;
			}
		}else{
			CH->SLOT[0].connect = CH->SLOT[0].CON ? &chanout[(r&0xf)+ch_offset] : &phase_modulation;
			CH->SLOT[1].connect = &chanout[(r&0xf)+ch_offset];
		}
		break;
	case 0xe0:
		slot = slot_array[r&0x1f];
		if(slot < 0) return;
		slot += ch_offset*2;
		CH = &chs[slot/2];
		v &= 7;
		CH->SLOT[slot&1].waveform_number = v;
		if(!(OPL3_mode & 1))
			v &= 3;
		CH->SLOT[slot&1].wavetable = v * SIN_LEN;
		break;
	}
}

void
opl3out(uchar *p, int n)
{
	uchar *e;

	for(e=p+n; p<e; p+=4){
		int a,b;
		advance_lfo();
		memset(chanout, 0, sizeof(chanout));
		chan_calc(&chs[0]);
		if(chs[0].extended)
			chan_calc_ext(&chs[3]);
		else
			chan_calc(&chs[3]);
		chan_calc(&chs[1]);
		if(chs[1].extended)
			chan_calc_ext(&chs[4]);
		else
			chan_calc(&chs[4]);
		chan_calc(&chs[2]);
		if(chs[2].extended)
			chan_calc_ext(&chs[5]);
		else
			chan_calc(&chs[5]);
		if((rhythm & 0x20) == 0){
			chan_calc(&chs[6]);
			chan_calc(&chs[7]);
			chan_calc(&chs[8]);
		}else
			chan_calc_rhythm(&chs[0], (noise_rng>>0)&1);
		chan_calc(&chs[9]);
		if(chs[9].extended)
			chan_calc_ext(&chs[12]);
		else
			chan_calc(&chs[12]);
		chan_calc(&chs[10]);
		if(chs[10].extended)
			chan_calc_ext(&chs[13]);
		else
			chan_calc(&chs[13]);
		chan_calc(&chs[11]);
		if(chs[11].extended)
			chan_calc_ext(&chs[14]);
		else
			chan_calc(&chs[14]);
		chan_calc(&chs[15]);
		chan_calc(&chs[16]);
		chan_calc(&chs[17]);
		a = chanout[0] & pan[0];
		b = chanout[0] & pan[1];
		a += chanout[1] & pan[4];
		b += chanout[1] & pan[5];
		a += chanout[2] & pan[8];
		b += chanout[2] & pan[9];
		a += chanout[3] & pan[12];
		b += chanout[3] & pan[13];
		a += chanout[4] & pan[16];
		b += chanout[4] & pan[17];
		a += chanout[5] & pan[20];
		b += chanout[5] & pan[21];
		a += chanout[6] & pan[24];
		b += chanout[6] & pan[25];
		a += chanout[7] & pan[28];
		b += chanout[7] & pan[29];
		a += chanout[8] & pan[32];
		b += chanout[8] & pan[33];
		a += chanout[9] & pan[36];
		b += chanout[9] & pan[37];
		a += chanout[10] & pan[40];
		b += chanout[10] & pan[41];
		a += chanout[11] & pan[44];
		b += chanout[11] & pan[45];
		a += chanout[12] & pan[48];
		b += chanout[12] & pan[49];
		a += chanout[13] & pan[52];
		b += chanout[13] & pan[53];
		a += chanout[14] & pan[56];
		b += chanout[14] & pan[57];
		a += chanout[15] & pan[60];
		b += chanout[15] & pan[61];
		a += chanout[16] & pan[64];
		b += chanout[16] & pan[65];
		a += chanout[17] & pan[68];
		b += chanout[17] & pan[69];
		if(a > 32767)
			a = 32767;
		else if(a < -32768)
			a = -32768;
		if(b > 32767)
			b = 32767;
		else if(b < -32768)
			b = -32768;
		p[0] = a;
		p[1] = a >> 8;
		p[2] = b;
		p[3] = b >> 8;
		advance();
	}
}

static int
init_tables(void)
{
	int i, x, n;
	double o, m;

	for (x=0; x<TL_RES_LEN; x++){
		m = (1<<16) / pow(2, (x+1) * (ENV_STEP/4.0) / 8.0);
		m = floor(m);
		n = (int)m;
		n >>= 4;
		if(n&1)
			n = (n>>1)+1;
		else
			n = n>>1;
		n <<= 1;
		tl_tab[x*2 + 0] = n;
		tl_tab[x*2 + 1] = ~tl_tab[x*2 + 0];
		for (i=1; i<13; i++){
			tl_tab[x*2+0 + i*2*TL_RES_LEN] = tl_tab[x*2+0]>>i;
			tl_tab[x*2+1 + i*2*TL_RES_LEN] = ~tl_tab[x*2+0 + i*2*TL_RES_LEN];
		}
	}
	for (i=0; i<SIN_LEN; i++){
		m = sin(((i*2)+1) * PI / SIN_LEN);
		if(m>0.0)
			o = 8*log(1.0/m)/log(2.0);
		else
			o = 8*log(-1.0/m)/log(2.0);
		o = o / (ENV_STEP/4);
		n = (int)(2.0*o);
		if(n&1)
			n = (n>>1)+1;
		else
			n = n>>1;
		sin_tab[i] = n*2 + (m>=0.0? 0: 1);
	}
	for (i=0; i<SIN_LEN; i++){
		if(i & (1<<(SIN_BITS-1)))
			sin_tab[1*SIN_LEN+i] = TL_TAB_LEN;
		else
			sin_tab[1*SIN_LEN+i] = sin_tab[i];
		sin_tab[2*SIN_LEN+i] = sin_tab[i & (SIN_MASK>>1)];
		if(i & (1<<(SIN_BITS-2)))
			sin_tab[3*SIN_LEN+i] = TL_TAB_LEN;
		else
			sin_tab[3*SIN_LEN+i] = sin_tab[i & (SIN_MASK>>2)];
		if(i & (1<<(SIN_BITS-1)))
			sin_tab[4*SIN_LEN+i] = TL_TAB_LEN;
		else
			sin_tab[4*SIN_LEN+i] = sin_tab[i*2];
		if(i & (1<<(SIN_BITS-1)))
			sin_tab[5*SIN_LEN+i] = TL_TAB_LEN;
		else
			sin_tab[5*SIN_LEN+i] = sin_tab[(i*2) & (SIN_MASK>>1)];
		if(i & (1<<(SIN_BITS-1)))
			sin_tab[6*SIN_LEN+i] = 1;
		else
			sin_tab[6*SIN_LEN+i] = 0;
		if(i & (1<<(SIN_BITS-1)))
			x = ((SIN_LEN-1)-i)*16 + 1;
		else
			x = i*16;
		if(x > TL_TAB_LEN)
			x = TL_TAB_LEN;
		sin_tab[7*SIN_LEN+i] = x;
	}
	return 1;
}

void
opl3init(int rate)
{
	int i, o;
	double f0;

	init_tables();
	f0 = (Clk / (8.0*36)) / rate;
	for(i=0 ; i < 1024 ; i++)
		fn_tab[i] = (u32int)((double)i * 64 * f0 * (1<<(FREQ_SH-10)));
	lfo_am_inc = (1.0 / 64.0) * (1<<LFO_SH) * f0;
	lfo_pm_inc = (1.0 / 1024.0) * (1<<LFO_SH) * f0;
	noise_f = (1.0 / 1.0) * (1<<FREQ_SH) * f0;
	eg_timer_add = (1<<EG_SH) * f0;
	eg_timer_overflow = (1) * (1<<EG_SH);
	noise_rng = 1;
	for(i=0xff; i>=0x20; i--)
		opl3wr(i, 0);
	for(i=0x1ff; i>=0x120; i--)
		opl3wr(i, 0);
	for(i=0; i<9*2; i++){
		Chan *CH = &chs[i];
		for(o=0; o<2; o++){
			CH->SLOT[o].state = EG_OFF;
			CH->SLOT[o].volume = MAX_ATT_INDEX;
		}
	}
}
