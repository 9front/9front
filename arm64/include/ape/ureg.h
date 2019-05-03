#ifndef __UREG_H
#define __UREG_H
#if !defined(_PLAN9_SOURCE)
    This header file is an extension to ANSI/POSIX
#endif

struct Ureg {
	unsigned long long	r0;
	unsigned long long	r1;
	unsigned long long	r2;
	unsigned long long	r3;
	unsigned long long	r4;
	unsigned long long	r5;
	unsigned long long	r6;
	unsigned long long	r7;
	unsigned long long	r8;
	unsigned long long	r9;
	unsigned long long	r10;
	unsigned long long	r11;
	unsigned long long	r12;
	unsigned long long	r13;
	unsigned long long	r14;
	unsigned long long	r15;
	unsigned long long	r16;
	unsigned long long	r17;
	unsigned long long	r18;
	unsigned long long	r19;
	unsigned long long	r20;
	unsigned long long	r21;
	unsigned long long	r22;
	unsigned long long	r23;
	unsigned long long	r24;
	unsigned long long	r25;
	unsigned long long	r26;
	unsigned long long	r27;
	unsigned long long	r28;	/* sb */
	union {
		unsigned long long	r29;
		unsigned long long	sp;
	};
	union {
		unsigned long long	r30;
		unsigned long long	link;
	};
	unsigned long long	type;	/* of exception */
	unsigned long long	psr;
	unsigned long long	pc;	/* interrupted addr */
};

#endif
