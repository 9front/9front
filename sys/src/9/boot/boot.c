#include <u.h>
#include <libc.h>

char bin[] = "/bin";
char root[] = "/root";

void
main(int, char *argv[])
{
	char buf[32];

	/* setup the boot namespace */
	bind("/boot", bin, MAFTER);

	if(fork() == 0){
		execl("/bin/paqfs", "-qa", "-c", "8", "-m", root, "/boot/bootfs.paq", nil);
		goto Err;
	}
	if(await(buf, sizeof(buf)) < 0)
		goto Err;

	bind(root, "/", MAFTER);

	buf[0] = '/';
	buf[1+read(open("/env/cputype", OREAD|OCEXEC), buf+1, sizeof buf - 6)] = '\0';
	strcat(buf, bin);
	bind(buf, bin, MAFTER);
	bind("/root/rc", "/rc", MREPL);
	bind("/rc/bin", bin, MAFTER);

	exec("/bin/bootrc", argv);
Err:
	errstr(buf, sizeof buf);
	_exits(buf);
}
