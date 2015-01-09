#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>
#include <keyboard.h>

int
enter(char *ask, char *buf, int len, Mousectl *mc, Keyboardctl *kc, Screen *scr)
{
	int done, down, tick, n, h, w, l, i;
	Image *b, *save, *backcol, *bordcol;
	Point p, o, t;
	Rectangle r, sc;
	Alt a[3];
	Mouse m;
	Rune k;

	o = screen->r.min;
	backcol = allocimagemix(display, DPurpleblue, DWhite);
	bordcol = allocimage(display, Rect(0,0,1,1), screen->chan, 1, DPurpleblue);
	if(backcol == nil || bordcol == nil)
		return -1;

	sc = screen->clipr;
	replclipr(screen, 0, screen->r);

	n = 0;
	if(kc){
		while(nbrecv(kc->c, nil) == 1)
			;
		a[n].op = CHANRCV;
		a[n].c = kc->c;
		a[n].v = &k;
		n++;
	}
	if(mc){
		o = mc->xy;
		a[n].op = CHANRCV;
		a[n].c = mc->c;
		a[n].v = &m;
		n++;
	}
	a[n].op = CHANEND;
	a[n].c = nil;
	a[n].v = nil;

	if(buf && len > 0)
		n = strlen(buf);
	else {
		buf = nil;
		len = 0;
		n = 0;
	}

	k = -1;
	b = nil;
	tick = n;
	save = nil;
	done = down = 0;

	p = stringsize(font, " ");
	h = p.y;
	w = p.x;

	while(!done){
		p = stringsize(font, buf ? buf : "");
		if(ask && ask[0]){
			if(buf) p.x += w;
			p.x += stringwidth(font, ask);
		}
		r = rectaddpt(insetrect(Rpt(ZP, p), -4), o);
		p.x = 0;
		r = rectsubpt(r, p);

		p = ZP;
		if(r.min.x < screen->r.min.x)
			p.x = screen->r.min.x - r.min.x;
		if(r.min.y < screen->r.min.y)
			p.y = screen->r.min.y - r.min.y;
		r = rectaddpt(r, p);
		p = ZP;
		if(r.max.x > screen->r.max.x)
			p.x = r.max.x - screen->r.max.x;
		if(r.max.y > screen->r.max.y)
			p.y = r.max.y - screen->r.max.y;
		r = rectsubpt(r, p);

		r = insetrect(r, -2);
		if(scr){
			if(b == nil)
				b = allocwindow(scr, r, Refbackup, DWhite);
			if(b == nil)
				scr = nil;
		}
		if(scr == nil && save == nil){
			if(b == nil)
				b = screen;
			save = allocimage(display, r, b->chan, 0, DNofill);
			if(save == nil){
				n = -1;
				break;
			}
			draw(save, r, b, nil, r.min);
		}
		draw(b, r, backcol, nil, ZP);
		border(b, r, 2, bordcol, ZP);
		p = addpt(r.min, Pt(6, 6));
		if(ask && ask[0]){
			p = string(b, p, bordcol, ZP, font, ask);
			if(buf) p.x += w;
		}
		if(buf){
			t = p;
			p = stringn(b, p, display->black, ZP, font, buf, utfnlen(buf, tick));
			draw(b, Rect(p.x-1, p.y, p.x+2, p.y+3), display->black, nil, ZP);
			draw(b, Rect(p.x, p.y, p.x+1, p.y+h), display->black, nil, ZP);
			draw(b, Rect(p.x-1, p.y+h-3, p.x+2, p.y+h), display->black, nil, ZP);
			p = string(b, p, display->black, ZP, font, buf+tick);
		}
		flushimage(display, 1);

nodraw:
		switch(alt(a)){
		case -1:
			done = 1;
			n = -1;
			break;
		case 0:
			if(buf == nil || k == Keof || k == '\n'){
				done = 1;
				break;
			}
			if(k == Knack || k == Kesc){
				done = !n;
				buf[n = tick = 0] = 0;
				break;
			}
			if(k == Ksoh || k == Khome){
				tick = 0;
				continue;
			}
			if(k == Kenq || k == Kend){
				tick = n;
				continue;
			}
			if(k == Kright){
				if(tick < n)
					tick += chartorune(&k, buf+tick);
				continue;
			}
			if(k == Kleft){
				for(i = 0; i < n; i += l){
					l = chartorune(&k, buf+tick);
					if(i+l >= tick){
						tick = i;
						break;
					}
				}
				continue;
			}
			if(k == Ketb){
				while(tick > 0){
					tick--;
					if(tick == 0 ||
					   strchr(" !\"#$%&'()*+,-./:;<=>?@`[\\]^{|}~", buf[tick-1]))
						break;
				}
				buf[n = tick] = 0;
				break;
			}
			if(k == Kbs){
				if(tick <= 0)
					continue;
				for(i = 0; i < n; i += l){
					l = chartorune(&k, buf+i);
					if(i+l >= tick){
						memmove(buf+i, buf+i+l, n - (i+l));
						buf[n -= l] = 0;
						tick -= l;
						break;
					}
				}
				break;
			}
			if(k < 0x20 || k == Kdel || (k & 0xFF00) == KF || (k & 0xFF00) == Spec)
				continue;
			if((len-n) <= (l = runelen(k)))
				continue;
			memmove(buf+tick+l, buf+tick, n - tick);
			runetochar(buf+tick, &k);
			buf[n += l] = 0;
			tick += l;
			break;
		case 1:
			if(!ptinrect(m.xy, r)){
				down = 0;
				goto nodraw;
			}
			if(m.buttons & 7){
				down = 1;
				if(buf && m.xy.x >= (t.x - w)){
					down = 0;
					for(i = 0; i < n; i += l){
						l = chartorune(&k, buf+i);
						t.x += stringnwidth(font, buf+i, 1);
						if(t.x > m.xy.x)
							break;
					}
					tick = i;
				}
				continue;
			}
			done = down;
			break;
		}

		if(b != screen) {
			freeimage(b);
			b = nil;
		} else {
			draw(b, save->r, save, nil, save->r.min);
			freeimage(save);
			save = nil;
		}
	}

	replclipr(screen, 0, sc);

	freeimage(backcol);
	freeimage(bordcol);
	flushimage(display, 1);

	return n;
}
