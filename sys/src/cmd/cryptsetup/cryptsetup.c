// Author Taru Karttunen <taruti@taruti.net>
// This file can be used as both Public Domain or Creative Commons CC0.
#include		<u.h>
#include		<libc.h>
#include		"crypt.h"

void format(char *file);
void copen(char *file);
char*readcons(char *prompt, char *def, int raw, char *buf, int nbuf);
int pkcs5_pbkdf2(const unsigned char *pass, int pass_len, const unsigned char *salt, int salt_len, unsigned char *key, int key_len, int rounds);

void 
usage(void)
{
	print("usage: \ncryptsetup -f deviceorfile \t\t\t\t\t# Format file or device\ncryptsetup -o deviceorfile >> /dev/fs/ctl \t# Open file or device\n");
	exits("usage");
}

enum
{
	NoMode,
	Format,
	Open,
};


void
main(int argc, char *argv[])
{
	int mode;
	char *file;

	mode = 0;

	ARGBEGIN {
	default:
		usage();
	case 'f':
		mode = Format;
		break;
	case 'o':
		mode = Open;
		break;
	} ARGEND;

	if((mode == NoMode) || (argc != 1))
		usage();

	file = argv[0];

	switch(mode) {
	case Format:
		format(file);
		break;
	case Open:
		copen(file);
		break;
	}
}

void
format(char *file)
{
	char trand[48], pass1[64], pass2[64];
	unsigned char tkey[16], tivec[16], buf[64*1024];
	XtsState s;
	AESstate cbc;
	int i,j, fd;

	do {
		readcons("password", nil, 1, pass1, 64);
		readcons("confirm", nil, 1, pass2, 64);
	} while(strcmp(pass1, pass2) != 0);

	do {
		readcons("Are you sure you want to delete all data? (YES to procees)", nil, 0, (char*)buf, 4);
	} while(strcmp((char*)buf, "YES") != 0);

	srand(truerand());

	for(i = 0; i < 16*4096; i++)
		buf[i] = rand();
	
	for(i = 0; i < 48; i+=4)
		*((unsigned*)&trand[i]) = truerand();
	memcpy(s.Master, trand, 32);
	memcpy(s.Slots[0].Salt, trand+32, 16);

	pkcs5_pbkdf2((unsigned char*)pass1, strlen(pass1), s.Slots[0].Salt, 16, (unsigned char*)tkey, 16, 9999);
	memset(tivec, 0, 16);
	setupAESstate(&cbc, tkey, 16, tivec);
	memcpy(s.Slots[0].Key, s.Master, 32);
	aesCBCencrypt(s.Slots[0].Key, 32, &cbc);

	for(i=0; i<16; i++)
		for(j=0; j<8; j++) {
			buf[(4096*i)]  = 1;
			buf[(4096*i)+(4*j)+1] = s.Slots[j].Salt[i];
			buf[(4096*i)+(4*j)+2] = s.Slots[j].Key[i];
			buf[(4096*i)+(4*j)+3] = s.Slots[j].Key[i+16];
		}

	if((fd = open(file, OWRITE)) < 0)
		exits("Cannot open disk");
	
	/* make the pad for checking crypto */
	for(i=0; i<8; i++) {
		buf[(64*1024)-8+i] = ~buf[(64*1024)-16+i];
	}
	memset(tivec, 0, 16);
	setupAESstate(&cbc, s.Master, 16, tivec);
	aes_encrypt(cbc.ekey, cbc.rounds, &buf[(64*1024)-16], &buf[(64*1024)-16]);

	write(fd, buf, 16*4096);

	print("Disk written\n");
}

void copen(char *file) {
	unsigned char pass[32], buf[1024*64], tkey[16], tivec[16];
	XtsState s;
	int i,j,fd;
	AESstate cbc;
	char *base, fdpath[1024];


	if((fd = open(file, OREAD)) < 0)
		exits("Cannot open disk");
	
	if(read(fd, buf, 1024*64) != 1024*64) 
		exits("Cannot read disk");
	
	for(i=0; i<16; i++) 
		for(j=0; j<8; j++) {
			s.Slots[j].Salt[i] = buf[(4096*i)+(4*j)+1];
			s.Slots[j].Key[i] = buf[(4096*i)+(4*j)+2];
			s.Slots[j].Key[i+16] = buf[(4096*i)+(4*j)+3];
		}



	openpass:
		readcons("Password", nil, 1, (char*)pass, 32);

		memcpy(s.Master, s.Slots[0].Key, 32);

		pkcs5_pbkdf2(pass, strlen((char*)pass), s.Slots[0].Salt, 16, tkey, 16, 9999);
		memset(tivec, 0, 16);
		setupAESstate(&cbc, tkey, 16, tivec);
		aesCBCdecrypt(s.Master, 32, &cbc);
		
		memset(tivec, 0, 16);
		setupAESstate(&cbc, s.Master, 16, tivec);

		aes_decrypt(cbc.dkey, cbc.rounds, &buf[(64*1024)-16], &buf[(64*1024)-16]);

		/* make the pad for checking crypto */
		for(i=0; i<8; i++)
			if((buf[(64*1024)-8+i] ^ buf[(64*1024)-16+i]) != 255) {
				goto openpass;
			}

	base = utfrrune(file, '/');
	fd2path(fd, fdpath, 1024);
	j = sprint((char*)buf, "crypt %s %s ", base ? base+1 : file, fdpath);
	
	for(i=0; i<32; i++) {
		sprint((char*)&buf[j], "%02X", s.Master[i]);
		j += 2; 
	}
	sprint((char*)&buf[j], "\n");
	print("%s\n", (char*)buf);
}