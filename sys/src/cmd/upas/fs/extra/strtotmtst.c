#include "strtotm.c"

void
main(int argc, char **argv)
{
	Tm tm;

	ARGBEGIN{
	}ARGEND

	for(; *argv; argv++)
		if(strtotm(*argv, &tm) >= 0)
			print("%s", asctime(&tm));
		else
			print("bad\n");
	exits("");
}
