#include <u.h>
#include <libc.h>

enum {
	I2C_ADDR = 0x41,

	REG_INPUT = 0x00,
	REG_OUTPUT = 0x01,
	REG_POLARITY = 0x02,
	REG_CONFIG = 0x03,

	IO_DAT = 1<<0,
	IO_PWR = 1<<1,
	IO_DUT = 1<<2,
	IO_CARD = 1<<3,
};

enum {
	MODE_OFF,
	MODE_DUT,
	MODE_HOST,
};

int mode, fd;
char *file = "/dev/sdUdca10/raw";

void
wr(int reg, int val)
{
	static uchar cmd[16], dat[512];
	char status[32];
	int n;

	cmd[0] = 0xCF;
	cmd[1] = 0x23;
	cmd[2] = I2C_ADDR<<1;
	cmd[3] = 0;
	cmd[4] = 2>>8;
	cmd[5] = 2;
	cmd[6] = 0;

	if(write(fd, cmd, sizeof(cmd)) != sizeof(cmd))
		sysfatal("write command: %r");

	dat[0] = reg;
	dat[1] = val;
	if(write(fd, dat, sizeof(dat)) < 0)
		sysfatal("write data: %r");

	if((n = pread(fd, status, sizeof(status)-1, 0)) < 0)
		sysfatal("read status: %r");

	status[n] = '\0';
	if(atoi(status) != 0)
		sysfatal("command error status: %s", status);
}

void
usage(void)
{
	fprint(2, "usage: %s off|dut|host [/dev/sdUxxxxx/raw]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	ARGBEGIN {
	} ARGEND;

	switch(argc){
	case 2:
		file = argv[1];
		/* no break */
	case 1:
		break;
	default:
		usage();
	}
	
	if(cistrcmp(argv[0], "off") == 0)
		mode = MODE_OFF;
	else if(cistrcmp(argv[0], "dut") == 0)
		mode = MODE_DUT;
	else if(cistrcmp(argv[0], "host") == 0)
		mode = MODE_HOST;
	else
		usage();

	if((fd = open(file, ORDWR)) < 0)
		sysfatal("open: %r");

	/* disconnect mode */
	wr(REG_OUTPUT, IO_DAT|IO_PWR|IO_DUT|IO_CARD);
	wr(REG_CONFIG, 0);
	sleep(100);

	/* apply mode */
	switch(mode){
	case MODE_OFF:
		break;
	case MODE_DUT:
		wr(REG_OUTPUT, IO_DAT|IO_PWR|IO_CARD);
		wr(REG_OUTPUT, IO_DUT|IO_CARD);
		break;
	case MODE_HOST:
		wr(REG_OUTPUT, 0);
		break;
	}

	exits(nil);
}
