// Original Author Taru Karttunen <taruti@taruti.net>
// This file can be used as both Public Domain or Creative Commons CC0.
#include <u.h>
#include <libc.h>
#include <libsec.h>
#include <authsrv.h>

typedef struct {
	uchar Salt[16];
	uchar Key[32];
} Slot;

typedef struct {
	uchar Master[32];
	Slot Slots[8];
	AESstate C1, C2;
} XtsState;

uchar zeros[16] = {0};
uchar buf[64*1024];
AESstate cbc;
XtsState s;

void 
setupkey(char *pass, uchar salt[16], AESstate *aes)
{
	uchar tkey[32];

	pbkdf2_x((uchar*)pass, strlen(pass), salt, 16, 9999, tkey, 32, hmac_sha1, SHA1dlen);
	setupAESstate(aes, tkey, 16, zeros);
	memset(tkey, 0, sizeof(tkey));
}

void
freepass(char *pass)
{
	if(pass != nil){
		memset(pass, 0, strlen(pass));
		free(pass);
	}
}

void
cformat(char *files[])
{
	char *pass, *tmp;
	int fd, i, j;

	pass = nil;
	do {
		freepass(pass);
		pass = readcons("Password", nil, 1);
		if(pass == nil || pass[0] == 0)
			sysfatal("input aborted");
		tmp = readcons("Confirm", nil, 1);
		if(tmp == nil || tmp[0] == 0)
			sysfatal("input aborted");
		i = strcmp(pass, tmp);
		freepass(tmp);
	} while(i != 0);

	for(;*files != nil; files++) {
		genrandom((uchar*)&s, sizeof(s));
		setupkey(pass, s.Slots[0].Salt, &cbc);
		memcpy(s.Slots[0].Key, s.Master, 32);
		aesCBCencrypt(s.Slots[0].Key, 32, &cbc);

		genrandom(buf, 16*4096);
		for(i=0; i<16; i++)
			for(j=0; j<8; j++) {
				buf[(4096*i)+(4*j)+1] = s.Slots[j].Salt[i];
				buf[(4096*i)+(4*j)+2] = s.Slots[j].Key[i];
				buf[(4096*i)+(4*j)+3] = s.Slots[j].Key[i+16];
			}

		if((fd = open(*files, OWRITE)) < 0)
			sysfatal("open disk: %r");
	
		/* make the pad for checking crypto */
		for(i=0; i<8; i++)
			buf[(64*1024)-8+i] = ~buf[(64*1024)-16+i];

		setupAESstate(&cbc, s.Master, 16, zeros);
		aes_encrypt(cbc.ekey, cbc.rounds, &buf[(64*1024)-16], &buf[(64*1024)-16]);

		if(write(fd, buf, 64*1024) != 64*1024)
			sysfatal("writing disk: %r");
	}
}

void
copen(char *files[], int ctl)
{
	char *pass, *name;
	uchar cbuf[16];
	int fd, i, j;

	pass = nil;
	for(;*files != nil; files++) {
		memset(&s, 0, sizeof(s));
		if((fd = open(*files, OREAD)) < 0)
			sysfatal("open disk: %r");
	
		if(read(fd, buf, 1024*64) != 1024*64) 
			sysfatal("read disk: %r");
	
	retrypass:
		for(i=0; i<16; i++)
			for(j=0; j<8; j++) {
				s.Slots[j].Salt[i] = buf[(4096*i)+(4*j)+1];
				s.Slots[j].Key[i] = buf[(4096*i)+(4*j)+2];
				s.Slots[j].Key[i+16] = buf[(4096*i)+(4*j)+3];
			}

		if(pass == nil){
			pass = readcons("Password", nil, 1);
			if(pass == nil || pass[0] == 0)
				sysfatal("input aborted");
		}

		setupkey(pass, s.Slots[0].Salt, &cbc);
		memcpy(s.Master, s.Slots[0].Key, 32);
		aesCBCdecrypt(s.Master, 32, &cbc);
		setupAESstate(&cbc, s.Master, 16, zeros);

		memcpy(cbuf, &buf[(64*1024)-16], 16);
		aes_decrypt(cbc.dkey, cbc.rounds, cbuf, cbuf);

		/* make the pad for checking crypto */
		for(i=0; i<8; i++)
			if((cbuf[i] ^ cbuf[i+8]) != 255) {
				freepass(pass);
				pass = nil;
				fprint(2, "wrong key\n");
				goto retrypass;
			}

		fd2path(fd, (char*)buf, sizeof(buf));
		close(fd);

		if((name = strrchr(*files, '/')) != nil)
			name++;
		else
			name = *files;

		if(fprint(ctl, "crypt %q %q %.32H\n", name, (char*)buf, s.Master) < 0)
			sysfatal("write: %r");
	}
}

void 
usage(void)
{
	print("usage:\n"
		"%s -f files\t\t# Format file or device\n"
		"%s -o files\t\t# Print commandline for open\n"
		"%s -i files\t\t# Install (open) files\n",
		argv0, argv0, argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	enum {
		NoMode,
		Format,
		Open,
		Install,
	};
	int mode, ctl;

	quotefmtinstall();
	fmtinstall('H', encodefmt);

	ctl = 1;
	mode = NoMode;

	ARGBEGIN {
	default:
		usage();
	case 'f':
		mode = Format;
		break;
	case 'o':
		mode = Open;
		break;
	case 'i':
		mode = Install;
		break;
	} ARGEND;

	if(argc < 0)
		usage();

	switch(mode){
	default:
		usage();
	case Format:
		cformat(argv);
		break;
	case Install:
		if((ctl = open("/dev/fs/ctl", OWRITE)) < 0)
			sysfatal("open ctl: %r");
		/* no break */
	case Open:
		copen(argv, ctl);
		break;
	}

	exits(nil);
}
