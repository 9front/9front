#include <u.h>
#include <libc.h>
#include <bio.h>
#include "midifile.h"

typedef struct{
	uchar notes[Nchan][128];
} Notes;

int freq[128];
uchar outbuf[IOUNIT], *outp = outbuf;

void
samp(double n)
{
	int i, j, f, v, ns, no[128];
	short u;
	Track *t;
	Notes *nn;
	static double Δ, ε;
	static uvlong τ;

	Δ = delay(n) + ε;
	ns = Δ;
	ε = Δ - ns;
	if(ns <= 0)
		return;
	memset(no, 0, sizeof no);
	for(t=tracks; t<tracks+ntrk; t++){
		if(t->ended)
			continue;
		nn = t->aux;
		for(i=0; i<Nchan; i++)
			for(j=0; j<128; j++)
				no[j] += nn->notes[i][j];
	}
	while(ns-- > 0){
		v = 0;
		for(j=0; j<128; j++){
			f = (τ % freq[j]) >= freq[j]/2 ? 1 : 0;
			v += f * no[j];
		}
		u = v * 10;
		outp[0] = outp[2] = u;
		outp[1] = outp[3] = u >> 8;
		outp += 4;
		if(outp == outbuf + sizeof outbuf){
			Bwrite(outbf, outbuf, sizeof outbuf);
			outp = outbuf;
		}
		τ++;
	}
}

void
event(Track *t)
{
	int e, c;
	Msg msg;
	Notes *nn;

	e = nextev(t);
	translate(t, e, &msg);
	c = msg.chan - chan;
	nn = t->aux;
	switch(msg.type){
	case Cnoteoff: nn->notes[c][msg.arg1] = 0; break;
	case Cnoteon: nn->notes[c][msg.arg1] = msg.arg2; break;
	}
}

void
main(int argc, char **argv)
{
	int i, cat;
	Track *t;

	cat = 0;
	ARGBEGIN{
	case 'D': trace = 1; break;
	case 'c': cat = 1; break;
	}ARGEND;
	initmid();
	if(readmid(*argv) < 0)
		sysfatal("readmid: %r");
	for(i=0; i<128; i++)
		freq[i] = samprate / (440 * pow(1.05946, i - 69));
	for(t=tracks; t<tracks+ntrk; t++)
		t->aux = emalloc(sizeof(Notes));
	outbf = cat ? efdopen(1, OWRITE) : eopen("/dev/audio", OWRITE);
	evloop();
	Bwrite(outbf, outbuf, outp - outbuf);
	Bterm(outbf);
	exits(nil);
}
