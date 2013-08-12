/*
 * enlightenment sound daemon for plan9front
 *
 * usage: aux/listen1 -t 'tcp!*!16001' esd
 */

#include <u.h>
#include <libc.h>

int	bigendian = 0;

void
die(char *msg)
{
	exits(msg);
}

ulong
get4(void)
{
	uchar buf[4];

	if(readn(0, buf, 4) != 4)
		die("read");
	if(bigendian)
		return buf[3] | buf[2]<<8 | buf[1]<<16 | buf[0]<<24;
	else
		return buf[0] | buf[1]<<8 | buf[2]<<16 | buf[3]<<24;
}

char*
getname(char *buf)
{
	if(readn(0, buf, 128) != 128)
		die("read");
	return buf;
}

void
put4(ulong v)
{
	uchar buf[4];

	if(bigendian){
		buf[3] = v & 0xFF, v >>= 8;
		buf[2] = v & 0xFF, v >>= 8;
		buf[1] = v & 0xFF, v >>= 8;
		buf[0] = v & 0xFF;
	} else {
		buf[0] = v & 0xFF, v >>= 8;
		buf[1] = v & 0xFF, v >>= 8;
		buf[2] = v & 0xFF, v >>= 8;
		buf[3] = v & 0xFF;
	}
	if(write(1, buf, 4) != 4)
		die("write");
}

char*
pcmfmt(ulong fmt, ulong rate)
{
	static char buf[32];

	snprint(buf, sizeof(buf), "s%dc%dr%lud",
		(fmt & 0x000F) == 0x01 ? 16 : 8,
		(fmt & 0x00F0) == 0x20 ? 2 : 1,
		rate);
	return buf;
}

void
main(void)
{
	ulong op, id, len, fmt, rate;
	char buf[256];
	int i, fd;

Init:
	/* initial protocol */
	if(readn(0, buf, 16) != 16)	/* key */
		die("read");
	if((get4() & 0xFF) == 'E')	/* endian */
		bigendian = 1;
	put4(1);

	for(;;){
		op = get4();
		switch(op){
		case 0:		/* init */
		case 1:		/* lock */
		case 2:		/* unlock */
			goto Init;
		case 3:		/* stream-play */
			fmt = get4();
			rate = get4();
			getname(buf);
			fd = -1;
			/* wait 2 seconds, device might be busy */
			for(i=0; i<2000; i+=100){
				fd = open("/dev/audio", OWRITE);
				if(fd >= 0)
					break;
				sleep(100);
			}
			if(fd < 0)
				die("open");
			dup(fd, 1);
			execl("/bin/audio/pcmconv", "pcmconv",
				"-i", pcmfmt(fmt, rate), 0);
			die("exec");
			break;
		case 4:
		case 5:		/* stream-mon */
			fmt = get4();
			rate = get4();
			getname(buf);
			fd = open("/dev/audio", OREAD);
			if(fd < 0)
				die("open");
			dup(fd, 0);
			execl("/bin/audio/pcmconv", "pcmconv",
				"-o", pcmfmt(fmt, rate), 0);
			die("exec");
			break;
		case 6:		/* sample-cache */
			fmt = get4();	/* format */
			rate = get4();	/* rate */
			len = get4();	/* size */
			getname(buf);
			id = get4();	/* sample-id */
			if(fork() == 0){
				/* TODO */
				fd = open("/dev/null", OWRITE);
				if(fd < 0)
					die("open");
				dup(fd, 1);
				snprint(buf, sizeof(buf), "%lud", len);
				execl("/bin/audio/pcmconv", "pcmconv",
					"-l", buf,
					"-i", pcmfmt(fmt, rate), 0);
				die("exec");
			}
			waitpid();
			put4(id);
			break;
		case 7:		/* sample-free */
		case 8:		/* sample-play */
		case 9:		/* sample-loop */
		case 10:	/* sample-stop */
		case 11:	/* sample-kill */
			id = get4();
			put4(id);
			break;
		case 12:	/* standby */
		case 13:	/* resume */
			goto Init;
		case 14:	/* sample-getid */
			getname(buf);
			put4(0);/* sample-id */
			break;
		case 15:	/* stream-filter */
			fmt = get4();
			rate = get4();
			USED(fmt);
			USED(rate);
			getname(buf);
			put4(1);
			break;
		case 16:	/* server-info */
		case 17:	/* server-all-info */
			put4(1);	/* version */
			put4(44100);	/* rate */
			put4(0x0021);	/* fmt */
			if(op == 16)
				break;
			put4(0);
			memset(buf, 0, sizeof(buf));
			if(write(1, buf, 32) != 32)
				die("write");
			break;
		case 18:	/* subscribe */
		case 19:	/* unsubscribe */
			break;
		case 20:	/* stream-pan */
		case 21:	/* sample-pan */
			id = get4();
			USED(id);
			get4();	/* left */
			get4();	/* reight */
			put4(1);
			break;
		case 22:	/* standby-mode */
			get4();	/* version */
			put4(0);/* mode */
			put4(0);/* ok */
			break;
		case 23:	/* latency */
			put4(0);
		}
	}
}
