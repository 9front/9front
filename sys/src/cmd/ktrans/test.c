#include <u.h>
#include <libc.h>

struct {
	char *input;
	Rune *expect;
} set[] = {
	"n", L"ん",
	"no", L"の",
	"nno", L"んの",
	"neko", L"猫",
	"neko", L"ねこ",
	"watashi", L"私",
	"tanoShi", L"楽し",
	"oreNO", L"俺の",

	"watashiHAmainichi35funijouaruIte,saraNI10fundenshaNInoTtegakkouNIkayoImasu.\nkenkouNOijiNImoyakuDAtteimasuga,nakanakatanoshiImonodesu.\n",
	L"私は毎日35分以上歩いて、更に10分電車に乗って学校に通います。\n健康の維持にも役だっていますが、なかなかたのしいものです。\n",

	"wonengtunxiabolierbushangshenti",
	L"我能吞下玻璃而不伤身体",

	"wodeqidianchuanzhuangmanlemanyu",
	L"我的气垫船装满了鳗鱼",

	"renrenshengerziyouzaizunyanhequanlishangyilvpingdeng",
	L"人人生而自由在尊严和权利上一律平等",

	"ngang", L"ngang",
	"sawcs", L"sắc",
	"ngax", L"ngã",
	"nawngj", L"nặng",
	"trangw", L"trăng",
	"cana", L"cân",
	"ddeme", L"đêm",
	"nhoo", L"nhô",
	"mow", L"mơ",
	"tuw", L"tư",
	"cari xooong", L"cải xoong",
	"huyeenf", L"huyền",
	"hoom qua", L"hôm qua",
	"thuws baary", L"thứ bẩy",
	"ddaua", L"đâu",
	"hoir", L"hỏi",
	"giof", L"giò",
};

char*
makemsg(char *s)
{
	char *out, *d;
	int i, n;

	n = strlen(s) + 1;
	out = mallocz(n * 3, 1);
	for(d = out, i = 0; i < n; i++){
		*d++ = 'c';
		if(i == n - 1)
			*d++ = 1;
		else
			*d++ = s[i];
		*d++ = '\0';
	}
	return out;
}

void
main(int argc, char **argv)
{
	int io1[2], io2[2];
	int i;
	int n;
	char *p, *e;
	static char buf[256];
	Rune r;
	char *to;
	char *bin;
	static Rune stack[256];
	static int nstack;

	if(argc < 2)
		sysfatal("usage: %s binary", argv[0]);

	bin = argv[1];
	pipe(io1);
	pipe(io2);
	if(rfork(RFENVG|RFFDG|RFREND|RFPROC) == 0){
		putenv("zidian", "/lib/ktrans/pinyin.dict");
		dup(io1[0], 0);
		dup(io2[0], 1);
		close(io1[1]); close(io2[1]);
		execl(bin, "ktrans", "-l", "jp", "-G", nil);
		sysfatal("exec: %r");
	}
	close(io1[0]); close(io2[0]);
	for(i = 0; i < nelem(set); i++){
		nstack = 0;
		stack[nstack] = 0;
		to = makemsg(set[i].input);
		for(;;){
			write(io1[1], to, strlen(to) + 1);
			if(to[1] == 1)
					break;
			to += strlen(to)+1;
		}
		for(;;) {
			n = read(io2[1], buf, sizeof buf);
			if(n <= 0)
				break;
			e = buf + n;
			for(p = buf; p < e; p += (strlen(p)+1)){
				assert(*p == 'c');
				chartorune(&r, p+1);
				switch(r){
				case 1:
					goto Verify;
				case 8:
					if(nstack == 0)
						sysfatal("buffer underrun");
					nstack--;
					stack[nstack] = 0;
					break;
				default:
					stack[nstack++] = r;
					stack[nstack] = 0;
					break;
				}
			}
		}
	Verify:
		if(runestrcmp(set[i].expect, stack) != 0){
			fprint(2, "%S != %S\n", stack, set[i].expect);
			exits("fail");
		}
	}
	close(io1[1]); close(io2[1]);
	waitpid();
	exits(nil);
}
