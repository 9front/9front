#include <u.h>
#include <libc.h>
#include <gio.h>

int topen(ReadWriter*);
int tclose(ReadWriter*);
long tread(ReadWriter*, void*, long, vlong);
long twrite(ReadWriter*, void*, long, vlong);

ReadWriter tester = {
	.open = topen,
	.close = tclose,
	.pread = tread,
	.pwrite = twrite,
};

void
main(int argc, char *argv[])
{
	int gfd = gopen(&tester, nil);
	if(gfd < 0){
		print("gio_test: failed to open gio fd\n");
		exits("failure");
	}
	char *test1 = "Hello World!\n";
	char test2[256];

	gread(gfd, &test2, 256, 23);
	gwrite(gfd, test1, sizeof(test1), 8);
	print("gio_test: %s\n", test2);
	gclose(gfd);
	print("gio_test: passed\n");
	exits(nil);
}

int
topen(ReadWriter *rd)
{
	print("gio_test: topen: file opened!\n");
	return 0;
}

int
tclose(ReadWriter *rd)
{
	print("gio_test: tclose: file closed!\n");
	return 0;
}

long
tread(ReadWriter *rd, void *bf, long len, vlong offset)
{
	char *test = "this is a read string!";
	memcpy(bf, test, strlen(test)+1);
	print("gio_test: tread: len = %ud, offset = %ud\n", len, offset);
	return len;
}

long
twrite(ReadWriter *rd, void *bf, long len, vlong offset)
{
	print("gio_test: twrite: written string: %s\n", bf);
	print("gio_test: twrite: len = %ud, offset = %ud\n", len, offset);
	return len;
}
