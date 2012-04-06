#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

#define lohi(L, H) (((u16int)L) | (((u16int)H) << 8))

u8int R[8], Fl;
u16int pc, sp, curpc;
int halt, IME;

static void
invalid(void)
{
	sysfatal("invalid instruction %.2x (pc = %.4x)", memread(curpc), curpc);
}

static u8int
fetch8(void)
{
	return memread(pc++);
}

static u16int
fetch16(void)
{
	u16int r;
	
	r = lohi(memread(pc), memread(pc+1));
	pc += 2;
	return r;
}

static void
push8(u8int n)
{
	memwrite(--sp, n);
}

static void
push16(u16int n)
{
	memwrite(--sp, n >> 8);
	memwrite(--sp, n);
}

static u8int
pop8(void)
{
	return memread(sp++);
}

static u16int
pop16(void)
{
	u8int a, b;
	
	b = pop8();
	a = pop8();
	return lohi(b, a);
}

static int
ld01(u8int op)
{
	u8int val, a, b;
	int time;
	
	a = (op & 0x38) >> 3;
	b = op & 7;
	time = 4;
	if(a == rHL && b == rHL){
		halt = 1;
		return 4;
	}
	if(b == rHL){
		val = memread(lohi(R[rL], R[rH]));
		time = 8;
	}else{
		val = R[b];
	}
	if(a == rHL){
		memwrite(lohi(R[rL], R[rH]), val);
		time = 8;
	}else{
		R[a] = val;
	}
	return time;
}

static int
ldi(u8int op)
{
	u8int val, a;
	
	val = fetch8();
	a = (op & 0x38) >> 3;
	if(a == rHL){
		memwrite(lohi(R[rL], R[rH]), val);
		return 12;
	}else{
		R[a] = val;
		return 8;
	}
}

static int
ld16(u8int op)
{
	u16int val;
	u8int a;
	
	val = fetch16();
	a = (op & 0x30) >> 4;
	switch(a){
	case 0:
		R[rB] = val >> 8;
		R[rC] = val;
		break;
	case 1:
		R[rD] = val >> 8;
		R[rE] = val;
		break;
	case 2:
		R[rH] = val >> 8;
		R[rL] = val;
		break;
	case 3:
		sp = val;
		break;
	}
	return 12;
}

static int
add16(u8int op)
{
	u16int val1, val2;
	u8int a;
	u32int val32;
	
	a = (op & 0x30) >> 4;
	switch(a){
	case 0:
		val1 = lohi(R[rC], R[rB]);
		break;
	case 1:
		val1 = lohi(R[rE], R[rD]);
		break;
	case 2:
		val1 = lohi(R[rL], R[rH]);
		break;
	default:
		val1 = sp;
	}
	Fl &= FLAGZ;
	val2 = lohi(R[rL], R[rH]);
	val32 = (u32int)(val1) + (u32int)(val2);
	if(val32 > 0xFFFF)
		Fl |= FLAGC;
	if(((val1&0xFFF)+(val2&0xFFF)) > 0xFFF)
		Fl |= FLAGH;
	R[rL] = val32;
	R[rH] = val32 >> 8;
	return 8;
}

static int
ldin(u8int op)
{
	u16int addr;

	switch(op >> 4){
	case 0:
		addr = lohi(R[rC], R[rB]);
		break;
	case 1:
		addr = lohi(R[rE], R[rD]);
		break;
	default:
		addr = lohi(R[rL], R[rH]);
	}
	if(op & 8){
		R[rA] = memread(addr);
	}else{
		memwrite(addr, R[rA]);
	}
	if((op >> 4) > 1){
		if(op & 16)
			addr--;
		else
			addr++;
		R[rL] = addr;
		R[rH] = addr >> 8;
	}
	return 8;
}

static int
inc16(u8int op)
{
	u16int val;
	u8int a;
	
	a = (op & 0x38) >> 3;
	switch(a >> 1){
	case 0:
		val = lohi(R[rC], R[rB]);
		break;
	case 1:
		val = lohi(R[rE], R[rD]);
		break;
	case 2:
		val = lohi(R[rL], R[rH]);
		break;
	default:
		val = sp;
	}
	if(a & 1)
		val--;
	else
		val++;
	switch(a >> 1){
	case 0:
		R[rB] = val >> 8;
		R[rC] = val;
		break;
	case 1:
		R[rD] = val >> 8;
		R[rE] = val;
		break;
	case 2:
		R[rH] = val >> 8;
		R[rL] = val;
		break;
	default:
		sp = val;
	}
	return 8;
}

static int
inc8(u8int op)
{
	u8int val, a;
	int time;
	
	a = (op & 0x38) >> 3;
	if(a == rHL){
		val = memread(lohi(R[rL], R[rH]));
		time = 12;
	}else{
		val = R[a];
		time = 4;
	}
	if(a == rHL){
		memwrite(lohi(R[rL], R[rH]), val+1);
	}else{
		R[a] = val + 1;
	}
	Fl &= FLAGC;
	if(val == 0xFF)
		Fl |= FLAGZ;
	if((val & 0xF) == 0xF)
		Fl |= FLAGH;
	return time;
}

static int
dec8(u8int op)
{
	u8int val, a;
	int time;
	
	a = (op & 0x38) >> 3;
	if(a == rHL){
		val = memread(lohi(R[rL], R[rH]));
		time = 12;
	}else{
		val = R[a];
		time = 4;
	}
	if(a == rHL){
		memwrite(lohi(R[rL], R[rH]), val - 1);
	}else{
		R[a] = val - 1;
	}
	Fl = (Fl & FLAGC) | FLAGN;
	if(val == 1)
		Fl |= FLAGZ;
	if((val & 0xF) == 0)
		Fl |= FLAGH;
	return time;
}

static int
alu(u8int op)
{
	u8int val4, val8, a, b;
	short val16;
	int time;
	
	a = op & 7;
	b = (op & 0x38) >> 3;
	if((op >> 6) == 3){
		val8 = fetch8();
		time = 8;
	}else if(a == rHL){
		val8 = memread(lohi(R[rL], R[rH]));
		time = 8;
	}else{
		val8 = R[a];
		time = 4;
	}
	switch(b){
	case 0:
	case 1:
		val16 = (ushort)(R[rA]) + (ushort)(val8);
		val4 = (R[rA] & 0xF) + (val8 & 0xF);
		if(b == 1 && (Fl & FLAGC)){
			val16++;
			val4++;
		}
		Fl = 0;
		val8 = val16;
		if(val16 >= 0x100)
			Fl |= FLAGC;
		if(val4 >= 0x10)
			Fl |= FLAGH;
		break;
	case 2:
	case 3:
	case 7:
		val16 = (ushort)R[rA];
		val16 -= (ushort)val8;
		val4 = val8 & 0xF;
		if(b == 3 && (Fl & FLAGC)){
			val16--;
			val4++;
		}
		val8 = val16;
		Fl = FLAGN;
		if(val16 < 0)
			Fl |= FLAGC;
		if(val4 > (R[rA] & 0xF))
			Fl |= FLAGH;
		break;
	case 4:
		val8 &= R[rA];
		Fl = FLAGH;
		break;
	case 5:
		val8 ^= R[rA];
		Fl = 0;
		break;
	default:
		Fl = 0;
		val8 |= R[rA];
	}
	if(val8 == 0)
		Fl |= FLAGZ;
	if(b != 7)
		R[rA] = val8;
	return time;
}

static int
jr(u8int op)
{
	u8int a;
	u16int addr;
	short step;
	
	a = (op & 0x38) >> 3;
	switch(a){
	case 0:
		return 4;
	case 1:
		addr = fetch16();
		memwrite(addr, sp);
		memwrite(addr + 1, sp >> 8);
		return 8;
	}
	step = (short)(schar)fetch8();
	switch(a){
	case 2:
		return 4;
	case 4:
		if(Fl & FLAGZ)
			return 8;
		break;
	case 5:
		if((Fl & FLAGZ) == 0)
			return 8;
		break;
	case 6:
		if(Fl & FLAGC)
			return 8;
		break;
	case 7:
		if((Fl & FLAGC) == 0)
			return 8;
	}
	pc += step;
	return 8;
}

static int
jp(u8int op)
{
	u16int addr;
	
	addr = fetch16();
	if(op != 0xC3){
		switch((op & 0x38) >> 3){
		case 0:
			if(Fl & FLAGZ)
				return 12;
			break;
		case 1:
			if((Fl & FLAGZ) == 0)
				return 12;
			break;
		case 2:
			if(Fl & FLAGC)
				return 12;
			break;
		case 3:
			if((Fl & FLAGC) == 0)
				return 12;
			break;
		}
	}
	pc = addr;
	return 12;
}

static int
call(u8int op)
{
	u16int addr;
	
	addr = fetch16();
	if(op != 0xCD){
		switch((op & 0x38) >> 3){
		case 0:
			if(Fl & FLAGZ)
				return 12;
			break;
		case 1:
			if((Fl & FLAGZ) == 0)
				return 12;
			break;
		case 2:
			if(Fl & FLAGC)
				return 12;
			break;
		case 3:
			if((Fl & FLAGC) == 0)
				return 12;
			break;
		}
	}
	push16(pc);
	pc = addr;
	return 12;
}

static int
rst(u8int op)
{
	u16int addr;

	addr = op & 0x38;
	push16(pc);
	pc = addr;
	return 32;
}

static int
ret(u8int op)
{
	if(op != 0xC9 && op!= 0xD9){
		switch((op & 0x38) >> 3){
		case 0:
			if(Fl & FLAGZ)
				return 8;
			break;
		case 1:
			if((Fl & FLAGZ) == 0)
				return 8;
			break;
		case 2:
			if(Fl & FLAGC)
				return 8;
			break;
		case 3:
			if((Fl & FLAGC) == 0)
				return 8;
			break;
		}
	}
	pc = pop16();
	if(op == 0xD9)
		IME = 1;
	return 8;
}

static int
push(u8int op)
{
	u8int a;

	a = (op & 0x38) >> 4;
	switch(a){
	case 0:
		push8(R[rB]);
		push8(R[rC]);
		break;
	case 1:
		push8(R[rD]);
		push8(R[rE]);
		break;
	case 2:
		push8(R[rH]);
		push8(R[rL]);
		break;
	default:
		push8(R[rA]);
		push8(Fl);
		break;
	}
	return 16;
}

static int
pop(u8int op)
{
	u8int a;
	
	a = (op & 0x38) >> 4;
	switch(a){
	case 0:
		R[rC] = pop8();
		R[rB] = pop8();
		break;
	case 1:
		R[rE] = pop8();
		R[rD] = pop8();
		break;
	case 2:
		R[rL] = pop8();
		R[rH] = pop8();
		break;
	default:
		Fl = pop8() & 0xF0;
		R[rA] = pop8();
	}
	return 12;
}

static int
shift(u8int op, int cb)
{
	u16int val;
	u8int a, b;
	int time;
	
	a = (op & 0x38) >> 3;
	b = op & 7;
	if(b == rHL){
		val = memread(lohi(R[rL], R[rH]));
		time = 16;
	}else{
		val = R[b];
		time = 8;
	}
	switch(a){
	case 0:
		Fl = 0;
		if(val & 0x80)
			Fl = FLAGC;
		val = (val << 1) | (val >> 7);
		break;
	case 1:
		Fl = 0;
		if(val & 1)
			Fl = FLAGC;
		val = (val >> 1) | (val << 7);
		break;
	case 2:
		val <<= 1;
		if(Fl & FLAGC)
			val |= 1;
		Fl = 0;
		if(val & 0x100)
			Fl = FLAGC;
		break;
	case 3:
		if(Fl & FLAGC)
			val |= 0x100;
		Fl = 0;
		if(val & 1)
			Fl = FLAGC;
		val >>= 1;
		break;
	case 4:
		Fl = 0;
		if(val & 0x80)
			Fl = FLAGC;
		val <<= 1;
		break;
	case 5:
		Fl = 0;
		if(val & 1)
			Fl = FLAGC;
		val = (val >> 1) | (val & 0x80);
		break;
	case 6:
		val = (val << 4) | (val >> 4);
		Fl = 0;
		break;
	default:
		Fl = 0;
		if(val & 1)
			Fl = FLAGC;
		val >>= 1;
	}
	if((val & 0xFF) == 0)
		Fl |= FLAGZ;
	if(b == rHL)
		memwrite(lohi(R[rL], R[rH]), val);
	else
		R[b] = val;
	if(!cb)
		Fl &= FLAGC;
	return time;
}

static int
bit(u8int op)
{
	u8int val, a, b;
	int time;
	
	a = (op & 0x38) >> 3;
	b = op & 7;
	if(b == rHL){
		val = memread(lohi(R[rL], R[rH])),
		time = 16;
	}else{
		val = R[b];
		time = 8;
	}
	Fl = (Fl & FLAGC) | FLAGH;
	if((val & (1<<a)) == 0)
		Fl |= FLAGZ;
	return time;
}

static int
setres(u8int op)
{
	u8int val, a, b;
	int time;
	
	a = (op & 0x38) >> 3;
	b = op & 7;
	if(b == rHL){
		val = memread(lohi(R[rL], R[rH]));
		time = 16;
	}else{
		val = R[b];
		time = 8;
	}
	if(op & 0x40)
		val |= (1 << a);
	else
		val &= ~(1 << a);
	if(b == rHL)
		memwrite(lohi(R[rL], R[rH]), val);
	else
		R[b] = val;
	return time;
}

static int
cb(void)
{
	u8int op;
	
	op = fetch8();
	if((op & 0xC0) == 0)
		return shift(op, 1);
	if((op & 0xC0) == 0x40)
		return bit(op);
	return setres(op);
}

void
interrupt(u8int t)
{
	mem[IF] |= (1 << t);
}

int
step(void)
{
	u8int op;
	ushort val;
	extern u8int daa[];
	int val32, i;

	if(halt){
		if(mem[IF] & mem[IE])
			halt = 0;
		else
			return 4;
	}
	if(IME && (mem[IF] & mem[IE]))
		for(i = 0; i < 5; i++)
			if(mem[IF] & mem[IE] & (1<<i)){
				mem[IF] &= ~(1<<i);
				push16(pc);
				IME = 0;
				halt = 0;
				pc = 0x40 + 8 * i;
				break;
			}
	curpc = pc;
	op = fetch8();
	if(0){
		print("%.4x A %.2x B %.2x C %.2x D %.2x E %.2x HL %.2x%.2x SP %.4x F %.2x ", curpc, R[rA], R[rB], R[rC], R[rD], R[rE], R[rH], R[rL], sp, Fl);
		disasm(curpc);
	}
	if((op & 0xC7) == 0x00)
		return jr(op);
	if((op & 0xCF) == 0x01)
		return ld16(op);
	if((op & 0xCF) == 0x09)
		return add16(op);
	if((op & 0xC7) == 0x02)
		return ldin(op);
	if((op & 0xC7) == 0x03)
		return inc16(op);
	if((op & 0xC7) == 0x04)
		return inc8(op);
	if((op & 0xC7) == 0x05)
		return dec8(op);
	if((op & 0xC7) == 0x06)
		return ldi(op);
	if((op & 0xE7) == 0x07)
		return shift(op, 0);
	if((op & 0xC0) == 0x40)
		return ld01(op);
	if((op & 0xC0) == 0x80 || (op & 0xC7) == 0xC6)
		return alu(op);
	if((op & 0xE7) == 0xC0 || op == 0xC9 || op == 0xD9)
		return ret(op);
	if((op & 0xCF) == 0xC1)
		return pop(op);
	if((op & 0xE7) == 0xC2 || op == 0xC3)
		return jp(op);
	if((op & 0xE7) == 0xC4 || op == 0xCD)
		return call(op);
	if((op & 0xCF) == 0xC5)
		return push(op);
	if((op & 0xC7) == 0xC7)
		return rst(op);
	switch(op){
	case 0x27:
		i = (((int)R[rA]) + (((int)Fl) * 16)) * 2;
		R[rA] = daa[i];
		Fl = daa[i+1];
		return 4;
	case 0x2F:
		R[rA] = ~R[rA];
		Fl |= FLAGN | FLAGH;
		return 4;
	case 0x37:
		Fl = (Fl & FLAGZ) | FLAGC;
		return 4;
	case 0x3F:
		Fl &= FLAGZ | FLAGC;
		Fl ^= FLAGC;
		return 4;
	case 0xE0:
		memwrite(lohi(fetch8(), 0xFF), R[rA]);
		return 8;
	case 0xE2:
		memwrite(lohi(R[rC], 0xFF), R[rA]);
		return 8;
	case 0xE8:
		val = (short)(schar)fetch8();
		val32 = (uint)sp + (uint)val;
		Fl = 0;
		if(((sp & 0xFF) + (val & 0xFF)) > 0xFF)
			Fl |= FLAGC;
		if(((sp & 0xF) + (val & 0xF)) > 0xF)
			Fl |= FLAGH;
		sp = val32;
		return 16;
	case 0xE9:
		pc = lohi(R[rL], R[rH]);
		return 4;
	case 0xEA:
		memwrite(fetch16(), R[rA]);
		return 16;
	case 0xF0:
		R[rA] = memread(lohi(fetch8(), 0xFF));
		return 12;
	case 0xFA:
		R[rA] = memread(fetch16());
		return 16;
	case 0xF2:
		R[rA] = memread(lohi(R[rC], 0xFF));
		return 8;
	case 0xCB:
		return cb();
	case 0xF3:
		IME= 0;
		return 4;
	case 0xF8:
		val = (short)(schar)fetch8();
		val32 = (uint)sp + (uint)val;
		Fl = 0;
		if(((sp & 0xFF) + (val & 0xFF)) > 0xFF)
			Fl |= FLAGC;
		if(((sp & 0xF) + (val & 0xF)) > 0xF)
			Fl |= FLAGH;
		R[rL] = val32;
		R[rH] = val32 >> 8;
		return 12;
	case 0xF9:
		sp = lohi(R[rL], R[rH]);
		return 8;
	case 0xFB:
		IME = 1;
		return 4;
	default:
		invalid();
	}
	return 0;
}
