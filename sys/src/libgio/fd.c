#include <u.h>
#include <libc.h>
#include <gio.h>

int fdopen(ReadWriter*);
int fdclose(ReadWriter*);
long fdread(ReadWriter*, void*, long, vlong);
long fdwrite(ReadWriter*, void*, long, vlong);

ReadWriter fdrdwr = {
	.open = fdopen,
	.close = fdclose,
	.pread = fdread,
	.pwrite = fdwrite,
};

int
fdopen(ReadWriter *rd)
{
	int *afd = (int*)rd->aux;

	rd->offset = 0;
	rd->length = (u64int) seek(*afd, 0, 2);
	seek(*afd, 0, 0);
	return 0;
}

int
fdclose(ReadWriter *rd)
{
	void *x = rd->aux;
	free(x);
	return 0;
}

long
fdread(ReadWriter *rd, void *bf, long len, vlong offset)
{
	int *afd = (int*)rd->aux;

	return pread(*afd, bf, len, offset);
}

long
fdwrite(ReadWriter *rd, void *bf, long len, vlong offset)
{
	int *afd = (int*)rd->aux;

	return pwrite(*afd, bf, len, offset);
}

int
fd2gio(int fd)
{
	void *x = malloc(sizeof(int));
	memcpy(x, &fd, sizeof(int));
	return gopen(&fdrdwr, x);
}

