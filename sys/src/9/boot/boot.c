#include <u.h>
#include <libc.h>
#include <auth.h>
#include <fcall.h>
#include "../boot/boot.h"

char	cputype[64];
int	mflag;
int	fflag;
int	kflag;

void
boot(int argc, char *argv[])
{
	char buf[32];

	fmtinstall('r', errfmt);

	bind("#c", "/dev", MBEFORE);
	open("/dev/cons", OREAD);
	open("/dev/cons", OWRITE);
	open("/dev/cons", OWRITE);
	/*
	 * init will reinitialize its namespace.
	 * #ec gets us plan9.ini settings (*var variables).
	 */
	bind("#ec", "/env", MREPL);
	bind("#e", "/env", MBEFORE|MCREATE);
	bind("#s", "/srv", MREPL|MCREATE);

	if(Debug){
		int i;

		print("argc=%d\n", argc);
		for(i = 0; i < argc; i++)
			print("%lux %s ", (ulong)argv[i], argv[i]);
		print("\n");
	}

	ARGBEGIN{
	case 'k':
		kflag = 1;
		break;
	case 'm':
		mflag = 1;
		break;
	case 'f':
		fflag = 1;
		break;
	}ARGEND

	readfile("#e/cputype", cputype, sizeof(cputype));
	setenv("bootdisk", bootdisk, 0);
	setenv("cpuflag", cpuflag ? "1" : "0", 0);

	/* setup the boot namespace */
	bind("/boot", "/bin", MAFTER);
	run("/bin/paqfs", "-q", "-c", "8", "-m" "/root", "/boot/bootfs.paq", nil);
	bind("/root", "/", MAFTER);
	snprint(buf, sizeof(buf), "/%s/bin", cputype);
	bind(buf, "/bin", MAFTER);
	bind("/rc/bin", "/bin", MAFTER);
	execl("/bin/bootrc", "bootrc", nil);
}
