</$objtype/mkfile

LIB=/$objtype/lib/libgio.a
OFILES=\
	fd.$O \
	openclose.$O \
	readp.$O \
	writep.$O \

HFILES=\
	/sys/include/gio.h\

UPDATE=\
	mkfile\
	$HFILES\
	${OFILES:%.$O=%.c}\
	${LIB:/$objtype/%=/386/%}\

</sys/src/cmd/mksyslib
