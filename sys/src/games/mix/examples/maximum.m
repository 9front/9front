# Entry condition R1 = n.
# Exit: RA = max R2 = index of max in X
X	EQU	1000
ORIG	3000
MAXIMUM	STJ	EXIT	# Subroutine linkage.
INIT	ENT3	0,1	# M1. Initialize k ← n.
	JMP	CHANGEM	# j ← n, m ← X[n], k ← n-1.
LOOP	CMPA	X,3	# M3. Compare.
	JGE	*+3	# To M5 if m ≥ X[k].
CHANGEM	ENT2	0,3	# M4. Change m. j ← k.
	LDA	X,3	# m ← X[k].
	DEC3	1	# M5. Decrease k.
	J3P	LOOP	# M2. All tested? To M3 if k > 0.
EXIT	JMP	*	# Return to main program.
