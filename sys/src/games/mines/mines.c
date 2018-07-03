#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <mp.h>
#include "dat.h"
#include "fns.h"

struct {
	int MaxX, MaxY, Mines;
} Settings[] = { {8, 8, 10}, {16, 16, 40}, {30, 16, 99}, {0, 0, 0} };

int MaxX, MaxY, Mines, Level, UnknownCell, Playing, MinesRemain, Time, Status, UseQuery = TRUE, UseGhost = FALSE, UseColor = TRUE;

Point Origin;
Mouse LastMouse;

Image *RGB000000, *RGB0000FF, *RGB007F00, *RGB7F7F7F, *RGBBFBFBF, *RGBFF0000, *RGBFFFF00, *RGBFFFFFF, *ImageButton[5], *ImageSign, *ImageDigit[10], *ImageCell[16];

FieldCell **MineField;

void Pack(Point *p, Point p0, Point p1, Point p2, Point p3, Point p4, Point p5) {

	p[0] = p0;
	p[1] = p1;
	p[2] = p2;
	p[3] = p3;
	p[4] = p4;
	p[5] = p5;
}

void Button3d(Image *im, Rectangle r, int i, Image *color1, Image *color2, Image *color3, Point sp) {

	Point p[6];

	if(i < 0){
		r = insetrect(r, i);
		i = -i;
	}

	draw(im, Rect(r.min.x + i, r.min.y + i, r.max.x - i, r.max.y - i), color1, nil, Pt(sp.x + i, sp.y + i));

	Pack(p, r.min, Pt(r.min.x, r.max.y), Pt(r.min.x + i, r.max.y - i), Pt(r.min.x + i, r.min.y + i), Pt(r.max.x - i, r.min.y + i), Pt(r.max.x, r.min.y));
	fillpoly(im, p, 6, 0, color2, sp);

	Pack(p, r.max, Pt(r.min.x, r.max.y), Pt(r.min.x + i, r.max.y - i), Pt(r.max.x - i, r.max.y - i), Pt(r.max.x - i, r.min.y + i), Pt(r.max.x, r.min.y));
	fillpoly(im, p, 6, 0, color3, sp);
}

void DisplayCounter(int x, int n) {

	Image *A, *B, *C;

	Button3d(screen, Rect(x, Origin.y + 16, x + 41, Origin.y + 41), 1, RGB000000, RGB7F7F7F, RGBFFFFFF, ZP);

	if(n < -99) n = -99;
	if(n > 999) n = 999;

	A = ImageDigit[abs(n / 100 - n / 1000 * 10)];
	B = ImageDigit[abs(n / 10 - n / 100 * 10)];
	C = ImageDigit[abs(n / 1 - n / 10 * 10)];

	if(n < 0) A = ImageSign;

	draw(screen, Rect(x + 2, Origin.y + 18, x + 13, Origin.y + 39), A, nil, ZP);
	draw(screen, Rect(x + 15, Origin.y + 18, x + 26, Origin.y + 39), B, nil, ZP);
	draw(screen, Rect(x + 28, Origin.y + 18, x + 39, Origin.y + 39), C, nil, ZP);
}

void DrawButton(int Index) {

	draw(screen, Rect(Origin.x +  MaxX * 8 - 1, Origin.y + 16, Origin.x + MaxX * 8 + 24, Origin.y + 41), ImageButton[Index], nil, ZP);
}

void DrawCell(Point Cell) {

	draw(screen, Rect(Origin.x + 12 + Cell.x * 16, Origin.y + 57 + Cell.y * 16, Origin.x + 12 + Cell.x * 16 + Dx(ImageCell[MineField[Cell.x][Cell.y].Picture]->r), Origin.y + 57 + Cell.y * 16 + Dy(ImageCell[MineField[Cell.x][Cell.y].Picture]->r)), ImageCell[MineField[Cell.x][Cell.y].Picture], nil, ZP);
}

void eresized(int New) {

	if(New && getwindow(display, Refmesg) < 0)
		fprint(2,"%s: can't reattach to window", argv0);

	Origin.x = screen->r.min.x + (screen->r.max.x - screen->r.min.x - 23 - MaxX * 16) / 2;
	Origin.y = screen->r.min.y + (screen->r.max.y - screen->r.min.y - 68 - MaxY * 16) / 2;

	draw(screen, screen->r, RGB0000FF, nil, ZP);

	/* main window */
	Button3d(screen, Rect(Origin.x, Origin.y, Origin.x + 23 + MaxX * 16, Origin.y + 68 + MaxY * 16), 3, RGBBFBFBF, RGBFFFFFF, RGB7F7F7F, ZP);

	/* top small window with button and 2 counters */
	Button3d(screen, Rect(Origin.x + 9, Origin.y + 9, Origin.x + 14 + MaxX * 16, Origin.y + 48), 2, RGBBFBFBF, RGB7F7F7F, RGBFFFFFF, ZP);

	/* button */
	DrawButton(Status);

	/* counter on the left side - remaining mines */
	DisplayCounter(Origin.x + 16, MinesRemain);

	/* counter on the right side - timer */
	DisplayCounter(Origin.x -34 + MaxX * 16, Time);

	/* bottom window - mine field */
	Button3d(screen, Rect(Origin.x + 9, Origin.y + 54, Origin.x + 14 + MaxX * 16, Origin.y + 59 + MaxY * 16), 3, RGBBFBFBF, RGB7F7F7F, RGBFFFFFF, ZP);

	{
		int x, y;

		for(x = 1; x < MaxX; x++)
			line(screen, Pt(Origin.x + 11 + x * 16, Origin.y + 57), Pt(Origin.x + 11 + x * 16, Origin.y + 55 + MaxY * 16), Endsquare, Endsquare, 0, RGB7F7F7F, ZP);

		for(y = 1; y < MaxY; y++)
			line(screen, Pt(Origin.x + 12, Origin.y + 56 + y * 16), Pt(Origin.x + 10 + MaxX * 16, Origin.y + 56 + y * 16), Endsquare, Endsquare, 0, RGB7F7F7F, ZP);

		for(y = 0; y < MaxY; y++)
			for(x = 0; x < MaxX; x++)
				DrawCell(Pt(x, y));
	}
}

void MouseCell(Point Cell) {

	int Picture;

	switch(MineField[Cell.x][Cell.y].Picture) {
		case Unknown:
			Picture = Empty0;
			break;
		case Query:
			Picture = MouseQuery;
			break;
		default:
			return;
	}
	draw(screen, Rect(Origin.x + 12 + Cell.x * 16, Origin.y + 57 + Cell.y * 16, Origin.x + 12 + Cell.x * 16 + Dx(ImageCell[Picture]->r), Origin.y + 57 + Cell.y * 16 + Dy(ImageCell[Picture]->r)), ImageCell[Picture], nil, ZP);
}

void RecoverCell(Point Cell) {

	switch(MineField[Cell.x][Cell.y].Picture) {
		case Unknown:
		case Query:
			draw(screen, Rect(Origin.x + 12 + Cell.x * 16, Origin.y + 57 + Cell.y * 16, Origin.x + 12 + Cell.x * 16 + Dx(ImageCell[MineField[Cell.x][Cell.y].Picture]->r), Origin.y + 57 + Cell.y * 16 + Dy(ImageCell[MineField[Cell.x][Cell.y].Picture]->r)), ImageCell[MineField[Cell.x][Cell.y].Picture], nil, ZP);
	}
}

void *emalloc(ulong size) {

	void *p;

	p = malloc(size);
	if(p == 0) {
		fprint(2, "%s: no memory: %r\n", argv0);
		exits("no mem");
	}
	return p;
}

void InitMineField(void) {

	/* clean up mine field, make all cells unknown and place new mines */
	{
		int i, x, y;

		for(y = 0; y < MaxY; y++)
			for(x = 0; x < MaxX; x++) {
				MineField[x][y].Mine = FALSE;
				MineField[x][y].Picture = Unknown;
			}

		for(i = 0; i < Mines; ) {
			x = nrand(MaxX);
			y = nrand(MaxY);
			if(MineField[x][y].Mine) continue;
			MineField[x][y].Mine = TRUE;
			i++;
		}
	}

	/* count mines in neighbourhood */
	{
		int x, y;

		for(y = 0; y < MaxY; y++)
			for(x = 0; x < MaxX; x++) {
				MineField[x][y].Neighbours = 0;
				if(x > 0 && MineField[x - 1][y].Mine) MineField[x][y].Neighbours++;
				if(y > 0 && MineField[x][y - 1].Mine) MineField[x][y].Neighbours++;
				if(x < MaxX -1 && MineField[x + 1][y].Mine) MineField[x][y].Neighbours++;
				if(y < MaxY - 1 && MineField[x][y + 1].Mine) MineField[x][y].Neighbours++;
				if(x > 0 && y > 0 && MineField[x - 1][y - 1].Mine) MineField[x][y].Neighbours++;
				if(x > 0 && y < MaxY - 1 && MineField[x - 1][y + 1].Mine) MineField[x][y].Neighbours++;
				if(x < MaxX - 1 && y > 0 && MineField[x + 1][y - 1].Mine) MineField[x][y].Neighbours++;
				if(x < MaxX - 1 && y < MaxY - 1 && MineField[x + 1][y + 1].Mine) MineField[x][y].Neighbours++;
			}
	}

	Status = Game;
	Playing = FALSE;
	MinesRemain = Mines;
	Time = 0;
	UnknownCell = MaxX * MaxY - Mines;
}

void NewMineField(int NewLevel) {

	int CurrentMaxX, CurrentMaxY;

	CurrentMaxX = MaxX;
	CurrentMaxY = MaxY;

	Level = NewLevel;

	/* set size of mine field and number of mines */
	if(Level == Custom) {

		/* here should ask a player about custom size of mine field and number of mines; in next release, may be... */

		if(MaxX < 8) MaxX = 8;
		if(MaxY < 8) MaxY = 8;
		if(Mines < 10) Mines = 10;
		if(MaxX > 30) MaxX = 30;
		if(MaxY > 24) MaxY = 24;
		if(Mines > (MaxX - 1) * (MaxY - 1)) Mines = (MaxX - 1) * (MaxY - 1);
	}
	else {
		MaxX = Settings[Level].MaxX;
		MaxY = Settings[Level].MaxY;
		Mines = Settings[Level].Mines;
		Settings[Custom].MaxX = MaxX;
		Settings[Custom].MaxY = MaxY;
		Settings[Custom].Mines = Mines;
	}

	if(MaxX != CurrentMaxX || MaxY != CurrentMaxY) {

		int x;

		/* if some memory is in use, release it */
		if(MineField) {
			for(x = 0; x < CurrentMaxX; x++)
				free(MineField[x]);
			free(MineField);
		}

		/* allocate new memory */
		MineField = (FieldCell **)emalloc(MaxX * sizeof(FieldCell *));
		for(x = 0; x < MaxX; x++)
			MineField[x] = (FieldCell *)emalloc(MaxY * sizeof(FieldCell));
	}
	InitMineField();
}

void GameOver(void) {

	int x, y;

	Playing = FALSE;
	for(y = 0; y < MaxY; y++)
		for(x = 0; x < MaxX; x++)
			switch(MineField[x][y].Picture) {
				case Unknown:
				case Query:
					if(MineField[x][y].Mine) {
						switch(Status) {
							case Win:
								MineField[x][y].Picture = Mark;
								DisplayCounter(Origin.x + 16, --MinesRemain);
								break;
							case Oops:
								MineField[x][y].Picture = Mined;
						}
						DrawCell(Pt(x, y));
					}
					break;
				case Mark:
					if(! MineField[x][y].Mine) {
						MineField[x][y].Picture = Fault;
						DrawCell(Pt(x, y));
					}
			}
}

void LeftClick(Point Cell) {

	if(! (Status == Game) || Cell.x < 0 || Cell.y < 0) return;
	Playing = TRUE;
	switch(MineField[Cell.x][Cell.y].Picture) {
		case Query:
		case Unknown:
			if(MineField[Cell.x][Cell.y].Mine) {
				MineField[Cell.x][Cell.y].Picture = Explosion;
				DrawCell(Cell);
				Status = Oops;
				GameOver();
				return;
			}
			MineField[Cell.x][Cell.y].Picture = MineField[Cell.x][Cell.y].Neighbours;
			DrawCell(Cell);
			if(! --UnknownCell) {
				Status = Win;
				GameOver();
			}
			else if(MineField[Cell.x][Cell.y].Neighbours == 0) {
				if(Cell.x > 0) LeftClick(Pt(Cell.x - 1, Cell.y));
				if(Cell.y > 0) LeftClick(Pt(Cell.x, Cell.y - 1));
				if(Cell.x < MaxX - 1) LeftClick(Pt(Cell.x + 1, Cell.y));
				if(Cell.y < MaxY - 1) LeftClick(Pt(Cell.x, Cell.y + 1));
				if(Cell.x > 0 && Cell.y > 0) LeftClick(Pt(Cell.x - 1, Cell.y - 1));
				if(Cell.x > 0 && Cell.y < MaxY - 1) LeftClick(Pt(Cell.x - 1, Cell.y + 1));
				if(Cell.x < MaxX - 1 && Cell.y > 0) LeftClick(Pt(Cell.x + 1, Cell.y - 1));
				if(Cell.x < MaxX - 1 && Cell.y < MaxY - 1) LeftClick(Pt(Cell.x + 1, Cell.y + 1));
			}
	}
}

void MiddleClick(Point Cell) {

	int Neighbours = 0;

	if(! (Status == Game) || Cell.x < 0 || Cell.y < 0) return;
	Playing = TRUE;
	switch(MineField[Cell.x][Cell.y].Picture) {
		case Empty1:
		case Empty2:
		case Empty3:
		case Empty4:
		case Empty5:
		case Empty6:
		case Empty7:
		case Empty8:
			break;
		default:
			 return;
	}

	if(Cell.x > 0 && MineField[Cell.x - 1][Cell.y].Picture == Mark) Neighbours++;
	if(Cell.y > 0 && MineField[Cell.x][Cell.y - 1].Picture == Mark) Neighbours++;
	if(Cell.x < MaxX - 1 && MineField[Cell.x + 1][Cell.y].Picture == Mark) Neighbours++;
	if(Cell.y < MaxY - 1 && MineField[Cell.x][Cell.y + 1].Picture == Mark) Neighbours++;
	if(Cell.x > 0 && Cell.y > 0 && MineField[Cell.x - 1][Cell.y - 1].Picture == Mark) Neighbours++;
	if(Cell.x > 0 && Cell.y < MaxY - 1 && MineField[Cell.x - 1][Cell.y + 1].Picture == Mark) Neighbours++;
	if(Cell.x < MaxX - 1 && Cell.y > 0 && MineField[Cell.x + 1][Cell.y - 1].Picture == Mark) Neighbours++;
	if(Cell.x < MaxX - 1 && Cell.y < MaxY - 1 && MineField[Cell.x + 1][Cell.y + 1].Picture == Mark) Neighbours++;

	if(Neighbours == MineField[Cell.x][Cell.y].Neighbours) {
		if(Cell.x > 0) LeftClick(Pt(Cell.x - 1, Cell.y));
		if(Cell.y > 0) LeftClick(Pt(Cell.x, Cell.y - 1));
		if(Cell.x < MaxX - 1) LeftClick(Pt(Cell.x + 1, Cell.y));
		if(Cell.y < MaxY - 1) LeftClick(Pt(Cell.x, Cell.y + 1));
		if(Cell.x > 0 && Cell.y > 0) LeftClick(Pt(Cell.x - 1, Cell.y - 1));
		if(Cell.x > 0 && Cell.y < MaxY - 1) LeftClick(Pt(Cell.x - 1, Cell.y + 1));
		if(Cell.x < MaxX - 1 && Cell.y > 0) LeftClick(Pt(Cell.x + 1, Cell.y - 1));
		if(Cell.x < MaxX - 1 && Cell.y < MaxY - 1) LeftClick(Pt(Cell.x + 1, Cell.y + 1));
	}
}

void RightClick(Point Cell) {

	if(! (Status == Game) || Cell.x < 0 || Cell.y < 0) return;

	Playing = TRUE;
	switch(MineField[Cell.x][Cell.y].Picture) {
		case Unknown:
			MineField[Cell.x][Cell.y].Picture = Mark;
			DrawCell(Cell);
			DisplayCounter(Origin.x + 16, --MinesRemain);
			break;
		case Mark:
			MineField[Cell.x][Cell.y].Picture = (UseQuery ? Query : Unknown);
			DrawCell(Cell);
			DisplayCounter(Origin.x + 16, ++MinesRemain);
			break;
		case Query:
			MineField[Cell.x][Cell.y].Picture = Unknown;
			DrawCell(Cell);
	}
}

void Usage(void) {

	fprint(2, "Usage: %s [-aeq]\n", argv0);
	exits("usage");
}

void main(int argc, char **argv) {

	Level = Beginner;

	ARGBEGIN {
	case 'a': Level = Advanced; break;
	case 'e': Level = Expert; break;
	case 'q': UseQuery = FALSE; break;
	case 'g': UseGhost = TRUE; break;
	default:
		Usage();
	} ARGEND

	if(argc > 0) Usage();
	
	{
		Tm *tm;
		
		tm = localtime(time(0));
		if(tm->mon == 3 && tm->mday == 1)
			UseGhost = !UseGhost;
	}

	if(initdraw(nil, nil, "mines") < 0) {
		fprint(2, "%s: can't open display: %r\n", argv0);
		exits("initdraw");
	}

	RGB000000 = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0x000000ff);
	RGB0000FF = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0x0000ffff);
	RGB007F00 = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0x007f00ff);
	RGB7F7F7F = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0x7f7f7fff);
	RGBBFBFBF = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0xbfbfbfff);
	RGBFF0000 = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0xff0000ff);
	RGBFFFF00 = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0xffff00ff);
	RGBFFFFFF = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0xffffffff);

	ImageButton[Game] = allocimage(display, Rect(0, 0, 25, 25), RGB24, 0, DNofill);
	loadimage(ImageButton[Game], ImageButton[Game]->r, SrcButtonGame, ButtonBytes);

	ImageButton[Push] = allocimage(display, Rect(0, 0, 25, 25), RGB24, 0, DNofill);
	loadimage(ImageButton[Push], ImageButton[Push]->r, SrcButtonPush, ButtonBytes);

	ImageButton[Move] = allocimage(display, Rect(0, 0, 25, 25), RGB24, 0, DNofill);
	loadimage(ImageButton[Move], ImageButton[Move]->r, SrcButtonMove, ButtonBytes);

	ImageButton[Win] = allocimage(display, Rect(0, 0, 25, 25), RGB24, 0, DNofill);
	loadimage(ImageButton[Win], ImageButton[Win]->r, SrcButtonWin, ButtonBytes);

	ImageButton[Oops] = allocimage(display, Rect(0, 0, 25, 25), RGB24, 0, DNofill);
	loadimage(ImageButton[Oops], ImageButton[Oops]->r, SrcButtonOops, ButtonBytes);

	ImageSign = allocimage(display, Rect(0, 0, 11, 21), RGB24, 0, DNofill);
	loadimage(ImageSign, ImageSign->r, SrcSign, DigitBytes);

	ImageDigit[0] = allocimage(display, Rect(0, 0, 11, 21), RGB24, 0, DNofill);
	loadimage(ImageDigit[0], ImageDigit[0]->r, SrcDigit0, DigitBytes);

	ImageDigit[1] = allocimage(display, Rect(0, 0, 11, 21), RGB24, 0, DNofill);
	loadimage(ImageDigit[1], ImageDigit[1]->r, SrcDigit1, DigitBytes);

	ImageDigit[2] = allocimage(display, Rect(0, 0, 11, 21), RGB24, 0, DNofill);
	loadimage(ImageDigit[2], ImageDigit[2]->r, SrcDigit2, DigitBytes);

	ImageDigit[3] = allocimage(display, Rect(0, 0, 11, 21), RGB24, 0, DNofill);
	loadimage(ImageDigit[3], ImageDigit[3]->r, SrcDigit3, DigitBytes);

	ImageDigit[4] = allocimage(display, Rect(0, 0, 11, 21), RGB24, 0, DNofill);
	loadimage(ImageDigit[4], ImageDigit[4]->r, SrcDigit4, DigitBytes);

	ImageDigit[5] = allocimage(display, Rect(0, 0, 11, 21), RGB24, 0, DNofill);
	loadimage(ImageDigit[5], ImageDigit[5]->r, SrcDigit5, DigitBytes);

	ImageDigit[6] = allocimage(display, Rect(0, 0, 11, 21), RGB24, 0, DNofill);
	loadimage(ImageDigit[6], ImageDigit[6]->r, SrcDigit6, DigitBytes);

	ImageDigit[7] = allocimage(display, Rect(0, 0, 11, 21), RGB24, 0, DNofill);
	loadimage(ImageDigit[7], ImageDigit[7]->r, SrcDigit7, DigitBytes);

	ImageDigit[8] = allocimage(display, Rect(0, 0, 11, 21), RGB24, 0, DNofill);
	loadimage(ImageDigit[8], ImageDigit[8]->r, SrcDigit8, DigitBytes);

	ImageDigit[9] = allocimage(display, Rect(0, 0, 11, 21), RGB24, 0, DNofill);
	loadimage(ImageDigit[9], ImageDigit[9]->r, SrcDigit9, DigitBytes);

	ImageCell[Empty0] = allocimage(display, Rect(0, 0, 15, 15), RGB24, 0, DNofill);
	loadimage(ImageCell[Empty0], ImageCell[Empty0]->r, SrcEmpty0, CellBytes);

	ImageCell[Empty1] = allocimage(display, Rect(0, 0, 15, 15), RGB24, 0, DNofill);
	loadimage(ImageCell[Empty1], ImageCell[Empty1]->r, SrcEmpty1, CellBytes);

	ImageCell[Empty2] = allocimage(display, Rect(0, 0, 15, 15), RGB24, 0, DNofill);
	loadimage(ImageCell[Empty2], ImageCell[Empty2]->r, SrcEmpty2, CellBytes);

	ImageCell[Empty3] = allocimage(display, Rect(0, 0, 15, 15), RGB24, 0, DNofill);
	loadimage(ImageCell[Empty3], ImageCell[Empty3]->r, SrcEmpty3, CellBytes);

	ImageCell[Empty4] = allocimage(display, Rect(0, 0, 15, 15), RGB24, 0, DNofill);
	loadimage(ImageCell[Empty4], ImageCell[Empty4]->r, SrcEmpty4, CellBytes);

	ImageCell[Empty5] = allocimage(display, Rect(0, 0, 15, 15), RGB24, 0, DNofill);
	loadimage(ImageCell[Empty5], ImageCell[Empty5]->r, SrcEmpty5, CellBytes);

	ImageCell[Empty6] = allocimage(display, Rect(0, 0, 15, 15), RGB24, 0, DNofill);
	loadimage(ImageCell[Empty6], ImageCell[Empty6]->r, SrcEmpty6, CellBytes);

	ImageCell[Empty7] = allocimage(display, Rect(0, 0, 15, 15), RGB24, 0, DNofill);
	loadimage(ImageCell[Empty7], ImageCell[Empty7]->r, SrcEmpty7, CellBytes);

	ImageCell[Empty8] = allocimage(display, Rect(0, 0, 15, 15), RGB24, 0, DNofill);
	loadimage(ImageCell[Empty8], ImageCell[Empty8]->r, SrcEmpty8, CellBytes);

	ImageCell[Unknown] = allocimage(display, Rect(0, 0, 15, 15), RGB24, 0, DNofill);
	loadimage(ImageCell[Unknown], ImageCell[Unknown]->r, SrcUnknown, CellBytes);

	ImageCell[Mark] = allocimage(display, Rect(0, 0, 15, 15), RGB24, 0, DNofill);
	loadimage(ImageCell[Mark], ImageCell[Mark]->r, SrcMark, CellBytes);

	ImageCell[Query] = allocimage(display, Rect(0, 0, 15, 15), RGB24, 0, DNofill);
	loadimage(ImageCell[Query], ImageCell[Query]->r, SrcQuery, CellBytes);

	ImageCell[MouseQuery] = allocimage(display, Rect(0, 0, 15, 15), RGB24, 0, DNofill);
	loadimage(ImageCell[MouseQuery], ImageCell[MouseQuery]->r, SrcMouseQuery, CellBytes);

	ImageCell[Mined] = allocimage(display, Rect(0, 0, 15, 15), RGB24, 0, DNofill);
	loadimage(ImageCell[Mined], ImageCell[Mined]->r, SrcMined, CellBytes);

	ImageCell[Explosion] = allocimage(display, Rect(0, 0, 15, 15), RGB24, 0, DNofill);
	loadimage(ImageCell[Explosion], ImageCell[Explosion]->r, SrcExplosion, CellBytes);

	ImageCell[Fault] = allocimage(display, Rect(0, 0, 15, 15), RGB24, 0, DNofill);
	loadimage(ImageCell[Fault], ImageCell[Fault]->r, SrcFault, CellBytes);

	srand(time(0)); /* initialize generator of random numbers */

	NewMineField(Level);

	eresized(0);

	einit(Emouse | Ekeyboard);

	{
		int PushButton = FALSE, Button = FALSE, CurrentButton, ChargedButton = FALSE, MiddleButton = FALSE, LastButton = 0;
		int Counter = 0;
		ulong Key, Etimer;
		uvlong LastAction;
		Event Event;
		Point CurrentCell, Cell = Pt(-1, -1);

		if(UseGhost) GhostInit();
		Etimer = etimer(0, UseGhost ? 10 : 1000);
		LastAction = nsec();

		for(;;) {
			Key = event(&Event);

			if(Key == Etimer) {
			
				if(UseGhost && nsec() - LastAction > 1000000000ULL && LastMouse.buttons == 0)
					GhostMode();
			
				if(++Counter == (UseGhost ? 100 : 1)){
				
					Counter = 0;
					
					if(Playing && Time < INT_MAX)
						DisplayCounter(Origin.x -34 + MaxX * 16, ++Time);
				
				}
			}

			if(Key == Emouse) {
			
				if(UseGhost && !GhostCheck(Event.mouse.xy) || LastMouse.buttons != Event.mouse.buttons){
					LastAction = nsec();
					LastMouse = Event.mouse;
					GhostReset(Event.mouse.buttons != 0);
				}

				/* mouse over button? */
				CurrentButton = FALSE;
				if(Event.mouse.xy.x > Origin.x + MaxX * 8 && Event.mouse.xy.x < Origin.x + MaxX * 8 + 25 && Event.mouse.xy.y > Origin.y + 17 && Event.mouse.xy.y < Origin.y + 42) CurrentButton = TRUE;

				/* mouse over any cell? */
				CurrentCell = Pt(-1, -1);
				if(Event.mouse.xy.x > Origin.x + 12 && Event.mouse.xy.x < Origin.x + 12 + MaxX * 16)
					CurrentCell.x = (Event.mouse.xy.x - Origin.x - 13) / 16;
				if(Event.mouse.xy.y > Origin.y + 57 && Event.mouse.xy.y < Origin.y + 57 + MaxY * 16)
					CurrentCell.y = (Event.mouse.xy.y - Origin.y - 58) / 16;

				/* pressed mouse button */
				if(Event.mouse.buttons > LastButton) {

					if(CurrentButton)
						switch(Event.mouse.buttons) {
							case 1:
								PushButton = TRUE;
								break;
							default:
								if(PushButton) DrawButton(Status);
								PushButton = FALSE;
						}

					if(! (CurrentCell.x < 0) && ! (CurrentCell.y < 0)) {
						switch(Event.mouse.buttons) {
							case 1:
								ChargedButton = TRUE;
								MiddleButton = FALSE;
								break;
							case 2:
								if(LastButton == 0) ChargedButton = TRUE;
								MiddleButton = TRUE;
								break;
							case 4:
								if(LastButton == 0) RightClick(CurrentCell);
								else MiddleButton = TRUE;
								break;
							default:
								ChargedButton = TRUE;
								MiddleButton = TRUE;
						}
						if(ChargedButton && Status == Game) DrawButton(Move);
					}
				}

				if(PushButton && CurrentButton != Button) {
					if(CurrentButton) DrawButton(Push);
					else DrawButton(Status);
					Button = CurrentButton;
				}

				if(ChargedButton && (! eqpt(CurrentCell, Cell) || Event.mouse.buttons != LastButton) && Status == Game) {
					if(! (Cell.x < 0) && ! (Cell.y < 0)) {
						RecoverCell(Cell);
						if(Cell.x > 0) RecoverCell(Pt(Cell.x - 1, Cell.y));
						if(Cell.y > 0) RecoverCell(Pt(Cell.x, Cell.y - 1));
						if(Cell.x < MaxX - 1) RecoverCell(Pt(Cell.x + 1, Cell.y));
						if(Cell.y < MaxY - 1) RecoverCell(Pt(Cell.x, Cell.y + 1));
						if(Cell.x > 0 && Cell.y > 0) RecoverCell(Pt(Cell.x - 1, Cell.y - 1));
						if(Cell.x > 0 && Cell.y < MaxY - 1) RecoverCell(Pt(Cell.x - 1, Cell.y + 1));
						if(Cell.x < MaxX - 1 && Cell.y > 0) RecoverCell(Pt(Cell.x + 1, Cell.y - 1));
						if(Cell.x < MaxX - 1 && Cell.y < MaxY - 1) RecoverCell(Pt(Cell.x + 1, Cell.y + 1));
					}
					Cell = CurrentCell;
					if(! (Cell.x < 0) && ! (Cell.y < 0) && ! (Event.mouse.buttons < LastButton)) {
						MouseCell(Cell);
						if(MiddleButton) {
							if(Cell.x > 0) MouseCell(Pt(Cell.x - 1, Cell.y));
							if(Cell.y > 0) MouseCell(Pt(Cell.x, Cell.y - 1));
							if(Cell.x < MaxX - 1) MouseCell(Pt(Cell.x + 1, Cell.y));
							if(Cell.y < MaxY - 1) MouseCell(Pt(Cell.x, Cell.y + 1));
							if(Cell.x > 0 && Cell.y > 0) MouseCell(Pt(Cell.x - 1, Cell.y - 1));
							if(Cell.x > 0 && Cell.y < MaxY - 1) MouseCell(Pt(Cell.x - 1, Cell.y + 1));
							if(Cell.x < MaxX - 1 && Cell.y > 0) MouseCell(Pt(Cell.x + 1, Cell.y - 1));
							if(Cell.x < MaxX - 1 && Cell.y < MaxY - 1) MouseCell(Pt(Cell.x + 1, Cell.y + 1));
						}
					}
				}

				/* released mouse button */
				if(Event.mouse.buttons < LastButton) {
					if(PushButton && CurrentButton) {
						InitMineField();
						eresized(0);
						}
					Button = FALSE;
					PushButton = FALSE;

					if(ChargedButton) {
						if(MiddleButton) MiddleClick(Cell);
						else LeftClick(Cell);
						DrawButton(Status);
					}
					Cell = Pt(-1, -1);
					ChargedButton = FALSE;
				}

				LastButton = Event.mouse.buttons;
			}

			if(Key == Ekeyboard) {
			
				LastAction = nsec();
				if(UseGhost) GhostReset(1);

				switch(Event.kbdc) {
					case 'n':
					case 'N':
						InitMineField();
						eresized(0);
						break;
					case 'b':
					case 'B':
						NewMineField(Beginner);
						eresized(0);
						break;
					case 'a':
					case 'A':
						NewMineField(Advanced);
						eresized(0);
						break;
					case 'e':
					case 'E':
						NewMineField(Expert);
						eresized(0);
						break;
					case 'q':
					case 'Q':
					case 127:
						exits(nil);
				}
			}
		}
	}
}
