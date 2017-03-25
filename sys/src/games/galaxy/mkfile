</$objtype/mkfile

TARG=galaxy mkgalaxy
BIN=/$objtype/bin/games
OGALAXY=galaxy.$O quad.$O body.$O simulate.$O
OMKGALAXY=mkgalaxy.$O body.$O

</sys/src/cmd/mkmany

$O.galaxy:	$OGALAXY
	$LD $LDFLAGS -o $target $prereq

$O.mkgalaxy: $OMKGALAXY
	$LD $LDFLAGS -o $target $prereq
