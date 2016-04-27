</$objtype/mkfile

LIB=/$objtype/lib/libregexp.a
OFILES=\
	regcomp.$O\
	regerror.$O\
	regexec.$O\
	regsub.$O\
	rregexec.$O\
	rregsub.$O\
	regprint.$O\

HFILES=/sys/include/regexp.h\
	regimpl.h\

UPDATE=\
	mkfile\
	$HFILES\
	${OFILES:%.$O=%.c}\
	${LIB:/$objtype/%=/386/%}\

</sys/src/cmd/mksyslib
