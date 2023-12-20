#pragma lib "libgeometry.a"
#pragma src "/sys/src/libgeometry"

#define DEG 0.01745329251994330	/* π/180 */

typedef struct Point2 Point2;
typedef struct Point3 Point3;
typedef double Matrix[3][3];
typedef double Matrix3[4][4];
typedef struct Quaternion Quaternion;
typedef struct RFrame RFrame;
typedef struct RFrame3 RFrame3;
typedef struct Triangle2 Triangle2;
typedef struct Triangle3 Triangle3;

struct Point2 {
	double x, y, w;
};

struct Point3 {
	double x, y, z, w;
};

struct Quaternion {
	double r, i, j, k;
};

struct RFrame {
	Point2 p;
	Point2 bx, by;
};

struct RFrame3 {
	Point3 p;
	Point3 bx, by, bz;
};

struct Triangle2
{
	Point2 p0, p1, p2;
};

struct Triangle3 {
	Point3 p0, p1, p2;
};

/* utils */
double flerp(double, double, double);
double fclamp(double, double, double);

/* Point2 */
Point2 Pt2(double, double, double);
Point2 Vec2(double, double);
Point2 addpt2(Point2, Point2);
Point2 subpt2(Point2, Point2);
Point2 mulpt2(Point2, double);
Point2 divpt2(Point2, double);
Point2 lerp2(Point2, Point2, double);
double dotvec2(Point2, Point2);
double vec2len(Point2);
Point2 normvec2(Point2);
int edgeptcmp(Point2, Point2, Point2);
int ptinpoly(Point2, Point2*, ulong);

/* Point3 */
Point3 Pt3(double, double, double, double);
Point3 Vec3(double, double, double);
Point3 addpt3(Point3, Point3);
Point3 subpt3(Point3, Point3);
Point3 mulpt3(Point3, double);
Point3 divpt3(Point3, double);
Point3 lerp3(Point3, Point3, double);
double dotvec3(Point3, Point3);
Point3 crossvec3(Point3, Point3);
double vec3len(Point3);
Point3 normvec3(Point3);

/* Matrix */
void identity(Matrix);
void addm(Matrix, Matrix);
void subm(Matrix, Matrix);
void mulm(Matrix, Matrix);
void smulm(Matrix, double);
void transposem(Matrix);
double detm(Matrix);
double tracem(Matrix);
double minorm(Matrix, int, int);
double cofactorm(Matrix, int, int);
void adjm(Matrix);
void invm(Matrix);
Point2 xform(Point2, Matrix);

/* Matrix3 */
void identity3(Matrix3);
void addm3(Matrix3, Matrix3);
void subm3(Matrix3, Matrix3);
void mulm3(Matrix3, Matrix3);
void smulm3(Matrix3, double);
void transposem3(Matrix3);
double detm3(Matrix3);
double tracem3(Matrix3);
double minorm3(Matrix3, int, int);
double cofactorm3(Matrix3, int, int);
void adjm3(Matrix3);
void invm3(Matrix3);
Point3 xform3(Point3, Matrix3);

/* Quaternion */
Quaternion Quat(double, double, double, double);
Quaternion Quatvec(double, Point3);
Quaternion addq(Quaternion, Quaternion);
Quaternion subq(Quaternion, Quaternion);
Quaternion mulq(Quaternion, Quaternion);
Quaternion smulq(Quaternion, double);
Quaternion sdivq(Quaternion, double);
double dotq(Quaternion, Quaternion);
Quaternion invq(Quaternion);
double qlen(Quaternion);
Quaternion normq(Quaternion);
Quaternion slerp(Quaternion, Quaternion, double);
Point3 qrotate(Point3, Point3, double);

/* RFrame */
Point2 rframexform(Point2, RFrame);
Point3 rframexform3(Point3, RFrame3);
Point2 invrframexform(Point2, RFrame);
Point3 invrframexform3(Point3, RFrame3);

/* Triangle2 */
Point2 centroid(Triangle2);
Point3 barycoords(Triangle2, Point2);

/* Triangle3 */
Point3 centroid3(Triangle3);

/* Fmt */
#pragma varargck type "v" Point2
#pragma varargck type "V" Point3
int vfmt(Fmt*);
int Vfmt(Fmt*);
void GEOMfmtinstall(void);
