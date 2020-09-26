#include "strtotm.c"

void
main(int argc, char **argv)
{
	Tm tm;

	ARGBEGIN{
	}ARGEND

	tmfmtinstall();
	for(; *argv; argv++)
		if(strtotm(*argv, &tm) >= 0)
			print("%τ\n", tmfmt(&tm, nil));
		else
			print("bad\n");
	exits("");
}
