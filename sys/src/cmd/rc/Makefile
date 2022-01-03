TARG=rc

OFILES=\
	code.o\
	exec.o\
	getflags.o\
	glob.o\
	here.o\
	io.o\
	lex.o\
	pcmd.o\
	pfnc.o\
	simple.o\
	subr.o\
	trap.o\
	tree.o\
	var.o\
	havefork.o\
	unix.o\
	y.tab.o\

HFILES=rc.h\
	y.tab.h\
	io.h\
	exec.h\
	fns.h\
	getflags.h\

YFILES=syn.y

PREFIX=/usr/local

all: $(TARG)

install: $(TARG) rcmain.unix
	cp $(TARG) $(PREFIX)/bin/
	cp rcmain.unix $(PREFIX)/lib/rcmain

$(TARG): $(OFILES)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARG) $(OFILES)

y.tab.h y.tab.c: $(YFILES)
	$(YACC) -d $(YFILES)

unix.o:	unix.c
	$(CC) $(CFLAGS) '-DPREFIX="$(PREFIX)"' -c unix.c

$(OFILES): $(HFILES)

clean:
	rm -f $(OFILES) $(TARG) y.tab.? y.debug
