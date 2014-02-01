TEXT getcallerpc(SB), 1, $0
	MOVQ	-8(RARG), AX
	RET
