enum  {
	FALSE = 0,
	TRUE = 1,
	CellBytes = 15 * 15 * 3,
	DigitBytes = 11 * 21 * 3,
	ButtonBytes = 25 * 25 * 3,
	INT_MAX = 0x7fffffff
};

enum {
	Beginner,
	Advanced,
	Expert,
	Custom
};

enum {
	Game,
	Push,
	Move,
	Win,
	Oops
};

enum {
	Empty0 = 0,
	Empty1 = 1,
	Empty2 = 2,
	Empty3 = 3,
	Empty4 = 4,
	Empty5 = 5,
	Empty6 = 6,
	Empty7 = 7,
	Empty8 = 8,
	Query,
	MouseQuery,
	Mark,
	Fault,
	Mined,
	Explosion,
	Unknown
};

typedef
struct {
	int Mine, Picture, Neighbours;
} FieldCell;


extern int MaxX, MaxY, Mines, Level, UnknownCell, Playing, MinesRemain, Time, Status, UseQuery, UseGhost, UseColor;
extern Point Origin;
extern FieldCell **MineField;
extern Mouse LastMouse;

extern uchar SrcDigit0[];
extern uchar SrcDigit1[];
extern uchar SrcDigit2[];
extern uchar SrcDigit3[];
extern uchar SrcDigit4[];
extern uchar SrcDigit5[];
extern uchar SrcDigit6[];
extern uchar SrcDigit7[];
extern uchar SrcDigit8[];
extern uchar SrcDigit9[];
extern uchar SrcEmpty0[];
extern uchar SrcEmpty1[];
extern uchar SrcEmpty2[];
extern uchar SrcEmpty3[];
extern uchar SrcEmpty4[];
extern uchar SrcEmpty5[];
extern uchar SrcEmpty6[];
extern uchar SrcEmpty7[];
extern uchar SrcEmpty8[];
extern uchar SrcExplosion[];
extern uchar SrcFault[];
extern uchar SrcButtonGame[];
extern uchar SrcMark[];
extern uchar SrcMined[];
extern uchar SrcMouseQuery[];
extern uchar SrcButtonMove[];
extern uchar SrcButtonOops[];
extern uchar SrcButtonPush[];
extern uchar SrcQuery[];
extern uchar SrcSign[];
extern uchar SrcUnknown[];
extern uchar SrcButtonWin[];
