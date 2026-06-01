#include "os.h"

// Written by referencing:
//	SHA3IUF by Andrey Jivsov <crypto@brainhub.org>
//	tiny_sha3 by Markku-Juhani O. Saarinen <mjos@iki.fi>
//	keccakf1600.c in mlkem-native

#define GET8(p) (u64int)(p)[0] | (u64int)(p)[1]<<8 | (u64int)(p)[2]<<16 | (u64int)(p)[3]<<24 | (u64int)(p)[4]<<32 | (u64int)(p)[5]<<40 | (u64int)(p)[6]<<48 | (u64int)(p)[7]<<56

#define MLK_KECCAK_ROL(x, y) \
	(((x) << (y)) | ((x) >> ((sizeof(u64int)*8) - (y))))

enum{
	MLK_KECCAK_NROUNDS=24,
};

static u64int mlk_KeccakF_RoundConstants[MLK_KECCAK_NROUNDS] = {
    (u64int)0x0000000000000001ULL, (u64int)0x0000000000008082ULL,
    (u64int)0x800000000000808aULL, (u64int)0x8000000080008000ULL,
    (u64int)0x000000000000808bULL, (u64int)0x0000000080000001ULL,
    (u64int)0x8000000080008081ULL, (u64int)0x8000000000008009ULL,
    (u64int)0x000000000000008aULL, (u64int)0x0000000000000088ULL,
    (u64int)0x0000000080008009ULL, (u64int)0x000000008000000aULL,
    (u64int)0x000000008000808bULL, (u64int)0x800000000000008bULL,
    (u64int)0x8000000000008089ULL, (u64int)0x8000000000008003ULL,
    (u64int)0x8000000000008002ULL, (u64int)0x8000000000000080ULL,
    (u64int)0x000000000000800aULL, (u64int)0x800000008000000aULL,
    (u64int)0x8000000080008081ULL, (u64int)0x8000000000008080ULL,
    (u64int)0x0000000080000001ULL, (u64int)0x8000000080008008ULL
};

void
_keccakfblock(u64int state[25], int rsize, uchar *in, int len)
{
	int i;

	u64int Aba, Abe, Abi, Abo, Abu;
	u64int Aga, Age, Agi, Ago, Agu;
	u64int Aka, Ake, Aki, Ako, Aku;
	u64int Ama, Ame, Ami, Amo, Amu;
	u64int Asa, Ase, Asi, Aso, Asu;
	u64int BCa, BCe, BCi, BCo, BCu;
	u64int Da, De, Di, Do, Du;
	u64int Eba, Ebe, Ebi, Ebo, Ebu;
	u64int Ega, Ege, Egi, Ego, Egu;
	u64int Eka, Eke, Eki, Eko, Eku;
	u64int Ema, Eme, Emi, Emo, Emu;
	u64int Esa, Ese, Esi, Eso, Esu;

	Aba = state[0];
	Abe = state[1];
	Abi = state[2];
	Abo = state[3];
	Abu = state[4];
	Aga = state[5];
	Age = state[6];
	Agi = state[7];
	Ago = state[8];
	Agu = state[9];
	Aka = state[10];
	Ake = state[11];
	Aki = state[12];
	Ako = state[13];
	Aku = state[14];
	Ama = state[15];
	Ame = state[16];
	Ami = state[17];
	Amo = state[18];
	Amu = state[19];
	Asa = state[20];
	Ase = state[21];
	Asi = state[22];
	Aso = state[23];
	Asu = state[24];

	while(len > 0){
		switch(rsize){
		case 168:
			Asa ^= GET8((in+20*8));
			Amu ^= GET8((in+19*8));
			Amo ^= GET8((in+18*8));
		case 144:
			Ami ^= GET8((in+17*8));	
		case 136:
			Ame ^= GET8((in+16*8));
			Ama ^= GET8((in+15*8));
			Aku ^= GET8((in+14*8));
			Ako ^= GET8((in+13*8));
		case 104:
			Aki ^= GET8((in+12*8));
			Ake ^= GET8((in+11*8));
			Aka ^= GET8((in+10*8));
			Agu ^= GET8((in+9*8));
		case 72:
			Ago ^= GET8((in+8*8));
			Agi ^= GET8((in+7*8));
			Age ^= GET8((in+6*8));
			Aga ^= GET8((in+5*8));
			Abu ^= GET8((in+4*8));
			Abo ^= GET8((in+3*8));
			Abi ^= GET8((in+2*8));
			Abe ^= GET8((in+1*8));
			Aba ^= GET8((in+0*8));
			break;
		}
		in += rsize;
		len -= rsize;

		for (i = 0; i < MLK_KECCAK_NROUNDS; i += 2){
			/* prepareTheta */
			BCa = Aba ^ Aga ^ Aka ^ Ama ^ Asa;
			BCe = Abe ^ Age ^ Ake ^ Ame ^ Ase;
			BCi = Abi ^ Agi ^ Aki ^ Ami ^ Asi;
			BCo = Abo ^ Ago ^ Ako ^ Amo ^ Aso;
			BCu = Abu ^ Agu ^ Aku ^ Amu ^ Asu;

			/* thetaRhoPiChiIotaPrepareTheta(i, A, E) */
			Da = BCu ^ MLK_KECCAK_ROL(BCe, 1);
			De = BCa ^ MLK_KECCAK_ROL(BCi, 1);
			Di = BCe ^ MLK_KECCAK_ROL(BCo, 1);
			Do = BCi ^ MLK_KECCAK_ROL(BCu, 1);
			Du = BCo ^ MLK_KECCAK_ROL(BCa, 1);

			Aba ^= Da;
			BCa = Aba;
			Age ^= De;
			BCe = MLK_KECCAK_ROL(Age, 44);
			Aki ^= Di;
			BCi = MLK_KECCAK_ROL(Aki, 43);
			Amo ^= Do;
			BCo = MLK_KECCAK_ROL(Amo, 21);
			Asu ^= Du;
			BCu = MLK_KECCAK_ROL(Asu, 14);
			Eba = BCa ^ ((~BCe) & BCi);
			Eba ^= (u64int)mlk_KeccakF_RoundConstants[i];
			Ebe = BCe ^ ((~BCi) & BCo);
			Ebi = BCi ^ ((~BCo) & BCu);
			Ebo = BCo ^ ((~BCu) & BCa);
			Ebu = BCu ^ ((~BCa) & BCe);

			Abo ^= Do;
			BCa = MLK_KECCAK_ROL(Abo, 28);
			Agu ^= Du;
			BCe = MLK_KECCAK_ROL(Agu, 20);
			Aka ^= Da;
			BCi = MLK_KECCAK_ROL(Aka, 3);
			Ame ^= De;
			BCo = MLK_KECCAK_ROL(Ame, 45);
			Asi ^= Di;
			BCu = MLK_KECCAK_ROL(Asi, 61);
			Ega = BCa ^ ((~BCe) & BCi);
			Ege = BCe ^ ((~BCi) & BCo);
			Egi = BCi ^ ((~BCo) & BCu);
			Ego = BCo ^ ((~BCu) & BCa);
			Egu = BCu ^ ((~BCa) & BCe);

			Abe ^= De;
			BCa = MLK_KECCAK_ROL(Abe, 1);
			Agi ^= Di;
			BCe = MLK_KECCAK_ROL(Agi, 6);
			Ako ^= Do;
			BCi = MLK_KECCAK_ROL(Ako, 25);
			Amu ^= Du;
			BCo = MLK_KECCAK_ROL(Amu, 8);
			Asa ^= Da;
			BCu = MLK_KECCAK_ROL(Asa, 18);
			Eka = BCa ^ ((~BCe) & BCi);
			Eke = BCe ^ ((~BCi) & BCo);
			Eki = BCi ^ ((~BCo) & BCu);
			Eko = BCo ^ ((~BCu) & BCa);
			Eku = BCu ^ ((~BCa) & BCe);

			Abu ^= Du;
			BCa = MLK_KECCAK_ROL(Abu, 27);
			Aga ^= Da;
			BCe = MLK_KECCAK_ROL(Aga, 36);
			Ake ^= De;
			BCi = MLK_KECCAK_ROL(Ake, 10);
			Ami ^= Di;
			BCo = MLK_KECCAK_ROL(Ami, 15);
			Aso ^= Do;
			BCu = MLK_KECCAK_ROL(Aso, 56);
			Ema = BCa ^ ((~BCe) & BCi);
			Eme = BCe ^ ((~BCi) & BCo);
			Emi = BCi ^ ((~BCo) & BCu);
			Emo = BCo ^ ((~BCu) & BCa);
			Emu = BCu ^ ((~BCa) & BCe);

			Abi ^= Di;
			BCa = MLK_KECCAK_ROL(Abi, 62);
			Ago ^= Do;
			BCe = MLK_KECCAK_ROL(Ago, 55);
			Aku ^= Du;
			BCi = MLK_KECCAK_ROL(Aku, 39);
			Ama ^= Da;
			BCo = MLK_KECCAK_ROL(Ama, 41);
			Ase ^= De;
			BCu = MLK_KECCAK_ROL(Ase, 2);
			Esa = BCa ^ ((~BCe) & BCi);
			Ese = BCe ^ ((~BCi) & BCo);
			Esi = BCi ^ ((~BCo) & BCu);
			Eso = BCo ^ ((~BCu) & BCa);
			Esu = BCu ^ ((~BCa) & BCe);

			/* prepareTheta */
			BCa = Eba ^ Ega ^ Eka ^ Ema ^ Esa;
			BCe = Ebe ^ Ege ^ Eke ^ Eme ^ Ese;
			BCi = Ebi ^ Egi ^ Eki ^ Emi ^ Esi;
			BCo = Ebo ^ Ego ^ Eko ^ Emo ^ Eso;
			BCu = Ebu ^ Egu ^ Eku ^ Emu ^ Esu;

			/* thetaRhoPiChiIotaPrepareTheta(i+1, E, A) */
			Da = BCu ^ MLK_KECCAK_ROL(BCe, 1);
			De = BCa ^ MLK_KECCAK_ROL(BCi, 1);
			Di = BCe ^ MLK_KECCAK_ROL(BCo, 1);
			Do = BCi ^ MLK_KECCAK_ROL(BCu, 1);
			Du = BCo ^ MLK_KECCAK_ROL(BCa, 1);

			Eba ^= Da;
			BCa = Eba;
			Ege ^= De;
			BCe = MLK_KECCAK_ROL(Ege, 44);
			Eki ^= Di;
			BCi = MLK_KECCAK_ROL(Eki, 43);
			Emo ^= Do;
			BCo = MLK_KECCAK_ROL(Emo, 21);
			Esu ^= Du;
			BCu = MLK_KECCAK_ROL(Esu, 14);
			Aba = BCa ^ ((~BCe) & BCi);
			Aba ^= (u64int)mlk_KeccakF_RoundConstants[i + 1];
			Abe = BCe ^ ((~BCi) & BCo);
			Abi = BCi ^ ((~BCo) & BCu);
			Abo = BCo ^ ((~BCu) & BCa);
			Abu = BCu ^ ((~BCa) & BCe);

			Ebo ^= Do;
			BCa = MLK_KECCAK_ROL(Ebo, 28);
			Egu ^= Du;
			BCe = MLK_KECCAK_ROL(Egu, 20);
			Eka ^= Da;
			BCi = MLK_KECCAK_ROL(Eka, 3);
			Eme ^= De;
			BCo = MLK_KECCAK_ROL(Eme, 45);
			Esi ^= Di;
			BCu = MLK_KECCAK_ROL(Esi, 61);
			Aga = BCa ^ ((~BCe) & BCi);
			Age = BCe ^ ((~BCi) & BCo);
			Agi = BCi ^ ((~BCo) & BCu);
			Ago = BCo ^ ((~BCu) & BCa);
			Agu = BCu ^ ((~BCa) & BCe);

			Ebe ^= De;
			BCa = MLK_KECCAK_ROL(Ebe, 1);
			Egi ^= Di;
			BCe = MLK_KECCAK_ROL(Egi, 6);
			Eko ^= Do;
			BCi = MLK_KECCAK_ROL(Eko, 25);
			Emu ^= Du;
			BCo = MLK_KECCAK_ROL(Emu, 8);
			Esa ^= Da;
			BCu = MLK_KECCAK_ROL(Esa, 18);
			Aka = BCa ^ ((~BCe) & BCi);
			Ake = BCe ^ ((~BCi) & BCo);
			Aki = BCi ^ ((~BCo) & BCu);
			Ako = BCo ^ ((~BCu) & BCa);
			Aku = BCu ^ ((~BCa) & BCe);

			Ebu ^= Du;
			BCa = MLK_KECCAK_ROL(Ebu, 27);
			Ega ^= Da;
			BCe = MLK_KECCAK_ROL(Ega, 36);
			Eke ^= De;
			BCi = MLK_KECCAK_ROL(Eke, 10);
			Emi ^= Di;
			BCo = MLK_KECCAK_ROL(Emi, 15);
			Eso ^= Do;
			BCu = MLK_KECCAK_ROL(Eso, 56);
			Ama = BCa ^ ((~BCe) & BCi);
			Ame = BCe ^ ((~BCi) & BCo);
			Ami = BCi ^ ((~BCo) & BCu);
			Amo = BCo ^ ((~BCu) & BCa);
			Amu = BCu ^ ((~BCa) & BCe);

			Ebi ^= Di;
			BCa = MLK_KECCAK_ROL(Ebi, 62);
			Ego ^= Do;
			BCe = MLK_KECCAK_ROL(Ego, 55);
			Eku ^= Du;
			BCi = MLK_KECCAK_ROL(Eku, 39);
			Ema ^= Da;
			BCo = MLK_KECCAK_ROL(Ema, 41);
			Ese ^= De;
			BCu = MLK_KECCAK_ROL(Ese, 2);
			Asa = BCa ^ ((~BCe) & BCi);
			Ase = BCe ^ ((~BCi) & BCo);
			Asi = BCi ^ ((~BCo) & BCu);
			Aso = BCo ^ ((~BCu) & BCa);
			Asu = BCu ^ ((~BCa) & BCe);
		}

	}

	state[0] = Aba;
	state[1] = Abe;
	state[2] = Abi;
	state[3] = Abo;
	state[4] = Abu;
	state[5] = Aga;
	state[6] = Age;
	state[7] = Agi;
	state[8] = Ago;
	state[9] = Agu;
	state[10] = Aka;
	state[11] = Ake;
	state[12] = Aki;
	state[13] = Ako;
	state[14] = Aku;
	state[15] = Ama;
	state[16] = Ame;
	state[17] = Ami;
	state[18] = Amo;
	state[19] = Amu;
	state[20] = Asa;
	state[21] = Ase;
	state[22] = Asi;
	state[23] = Aso;
	state[24] = Asu;
}
