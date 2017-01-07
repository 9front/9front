#include <u.h>
#include <libc.h>
#include <bio.h>

void
main(int argc, char *argv[])
{
	Biobuf bin, bout;
	long len, slen;
	int c, x;

	if(argc != 2){
		fprint(2, "usage: data2s name\n");
		exits("usage");
	}
	Binit(&bin, 0, OREAD);
	Binit(&bout, 1, OWRITE);
	Bprint(&bout, "#define D(o,s) DATA %scode+o(SB)/8, $s\n", argv[1]);
	for(len=0; (c=Bgetc(&bin))!=Beof; len++){
		if((len&7) == 0)
			Bprint(&bout, "D(%ld,\"", len);
		if(c>=0x20 && c<=0x7E){
			if(c == '\\' || c == '"')
				Bprint(&bout, "\\");
			Bprint(&bout, "%c", c);
		}else{
			if((x=Bgetc(&bin))!=Beof)
				Bungetc(&bin);
			if(x>='0' && x<='7')
				Bprint(&bout, "\\%.3uo", c);
			else
				Bprint(&bout, "\\%uo", c);
		}
		if((len&7) == 7)
			Bprint(&bout, "\")\n");
	}
	slen = len;
	if(len & 7){
		while(len & 7){
			Bprint(&bout, "\\z");
			len++;
		}
		Bprint(&bout, "\")\n");
	}
	Bprint(&bout, "#undef D\n");
	Bprint(&bout, "GLOBL %scode+0(SB), $%ld\n", argv[1], len);
	Bprint(&bout, "GLOBL %slen+0(SB), $4\n", argv[1]);
	Bprint(&bout, "DATA %slen+0(SB)/4, $%ld\n", argv[1], slen);
	exits(0);
}
