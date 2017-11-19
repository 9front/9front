#define ROTATE(n, v1, v2) \
	MOVO	v1, v2; \
	PSLLL	$(n), v1; \
	PSRLL	$(32-n), v2; \
	POR	v1, v2

TEXT _chachablock(SB), 0, $0
	MOVOU	 0(RARG), X0
	MOVOU	16(RARG), X1
	MOVOU	32(RARG), X2
	MOVOU	48(RARG), X3

	MOVL	rounds+8(FP), CX
	SHRL	$1, CX

_loop:
	PADDL	X1, X0
	PXOR	X0, X3
	/* ROTATE(16, X3, X3) */
	PSHUFLW $(1<<0 | 0<<2 | 3<<4 | 2<<6), X3, X3
	PSHUFHW $(1<<0 | 0<<2 | 3<<4 | 2<<6), X3, X3

	PADDL	X3, X2
	MOVO	X1, X4
	PXOR	X2, X4
	ROTATE(12, X4, X1)
	
	PADDL	X1, X0
	MOVO	X0, X4
	PXOR	X3, X4
	ROTATE(8, X4, X3)

	PADDL	X3, X2
	MOVO	X1, X4
	PXOR	X2, X4
	ROTATE(7, X4, X1)

	PSHUFL $(1<<0 | 2<<2 | 3<<4 | 0<<6), X1, X1
	PSHUFL $(2<<0 | 3<<2 | 0<<4 | 1<<6), X2, X2
	PSHUFL $(3<<0 | 0<<2 | 1<<4 | 2<<6), X3, X3

	PADDL	X1, X0
	PXOR	X0, X3
	/* ROTATE(16, X3, X3) */
	PSHUFLW $(1<<0 | 0<<2 | 3<<4 | 2<<6), X3, X3
	PSHUFHW $(1<<0 | 0<<2 | 3<<4 | 2<<6), X3, X3

	PADDL	X3, X2
	MOVO	X1, X4
	PXOR	X2, X4
	ROTATE(12, X4, X1)
	
	PADDL	X1, X0
	MOVO	X0, X4
	PXOR	X3, X4
	ROTATE(8, X4, X3)

	PADDL	X3, X2
	MOVO	X1, X4
	PXOR	X2, X4
	ROTATE(7, X4, X1)

	PSHUFL $(3<<0 | 0<<2 | 1<<4 | 2<<6), X1, X1
	PSHUFL $(2<<0 | 3<<2 | 0<<4 | 1<<6), X2, X2
	PSHUFL $(1<<0 | 2<<2 | 3<<4 | 0<<6), X3, X3

	DECL CX
	JNE _loop

	MOVOU	X0, 0(RARG)
	MOVOU	X1, 16(RARG)
	MOVOU	X2, 32(RARG)
	MOVOU	X3, 48(RARG)
	RET
