#include <u.h>
#include <libc.h>
#include <geometry.h>

/* 2D */

void
identity(Matrix m)
{
	memset(m, 0, 3*3*sizeof(double));
	m[0][0] = m[1][1] = m[2][2] = 1;
}

void
addm(Matrix a, Matrix b)
{
	int i, j;

	for(i = 0; i < 3; i++)
		for(j = 0; j < 3; j++)
			a[i][j] += b[i][j];
}

void
subm(Matrix a, Matrix b)
{
	int i, j;

	for(i = 0; i < 3; i++)
		for(j = 0; j < 3; j++)
			a[i][j] -= b[i][j];
}

void
mulm(Matrix a, Matrix b)
{
	int i, j, k;
	Matrix tmp;

	for(i = 0; i < 3; i++)
		for(j = 0; j < 3; j++){
			tmp[i][j] = 0;
			for(k = 0; k < 3; k++)
				tmp[i][j] += a[i][k]*b[k][j];
		}
	memmove(a, tmp, 3*3*sizeof(double));
}

void
smulm(Matrix m, double s)
{
	int i, j;

	for(i = 0; i < 3; i++)
		for(j = 0; j < 3; j++)
			m[i][j] *= s;
}

void
transposem(Matrix m)
{
	int i, j;
	double tmp;

	for(i = 0; i < 3; i++)
		for(j = i; j < 3; j++){
			tmp = m[i][j];
			m[i][j] = m[j][i];
			m[j][i] = tmp;
		}
}

double
detm(Matrix m)
{
	return m[0][0]*(m[1][1]*m[2][2] - m[1][2]*m[2][1])+
	       m[0][1]*(m[1][2]*m[2][0] - m[1][0]*m[2][2])+
	       m[0][2]*(m[1][0]*m[2][1] - m[1][1]*m[2][0]);
}

double
tracem(Matrix m)
{
	return m[0][0] + m[1][1] + m[2][2];
}

void
adjm(Matrix m)
{
	Matrix tmp;

	tmp[0][0] =  m[1][1]*m[2][2] - m[1][2]*m[2][1];
	tmp[0][1] = -m[0][1]*m[2][2] + m[0][2]*m[2][1];
	tmp[0][2] =  m[0][1]*m[1][2] - m[0][2]*m[1][1];
	tmp[1][0] = -m[1][0]*m[2][2] + m[1][2]*m[2][0];
	tmp[1][1] =  m[0][0]*m[2][2] - m[0][2]*m[2][0];
	tmp[1][2] = -m[0][0]*m[1][2] + m[0][2]*m[1][0];
	tmp[2][0] =  m[1][0]*m[2][1] - m[1][1]*m[2][0];
	tmp[2][1] = -m[0][0]*m[2][1] + m[0][1]*m[2][0];
	tmp[2][2] =  m[0][0]*m[1][1] - m[0][1]*m[1][0];
	memmove(m, tmp, 3*3*sizeof(double));
}

/* Cayley-Hamilton */
//void
//invertm(Matrix m)
//{
//	Matrix m², r;
//	double det, trm, trm²;
//
//	det = detm(m);
//	if(det == 0)
//		return;
//	trm = tracem(m);
//	memmove(m², m, 3*3*sizeof(double));
//	mulm(m², m²);
//	trm² = tracem(m²);
//	identity(r);
//	smulm(r, (trm*trm - trm²)/2);
//	smulm(m, trm);
//	subm(r, m);
//	addm(r, m²);
//	smulm(r, 1/det);
//	memmove(m, r, 3*3*sizeof(double));
//}

/* Cramer's */
void
invm(Matrix m)
{
	double det;

	det = detm(m);
	if(det == 0)
		return; /* singular matrices are not invertible */
	adjm(m);
	smulm(m, 1/det);
}

Point2
xform(Point2 p, Matrix m)
{
	return (Point2){
		p.x*m[0][0] + p.y*m[0][1] + p.w*m[0][2],
		p.x*m[1][0] + p.y*m[1][1] + p.w*m[1][2],
		p.x*m[2][0] + p.y*m[2][1] + p.w*m[2][2]
	};
}

/* 3D */

void
identity3(Matrix3 m)
{
	memset(m, 0, 4*4*sizeof(double));
	m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1;
}

void
addm3(Matrix3 a, Matrix3 b)
{
	int i, j;

	for(i = 0; i < 4; i++)
		for(j = 0; j < 4; j++)
			a[i][j] += b[i][j];
}

void
subm3(Matrix3 a, Matrix3 b)
{
	int i, j;

	for(i = 0; i < 4; i++)
		for(j = 0; j < 4; j++)
			a[i][j] -= b[i][j];
}

void
mulm3(Matrix3 a, Matrix3 b)
{
	int i, j, k;
	Matrix3 tmp;

	for(i = 0; i < 4; i++)
		for(j = 0; j < 4; j++){
			tmp[i][j] = 0;
			for(k = 0; k < 4; k++)
				tmp[i][j] += a[i][k]*b[k][j];
		}
	memmove(a, tmp, 4*4*sizeof(double));
}

void
smulm3(Matrix3 m, double s)
{
	int i, j;

	for(i = 0; i < 4; i++)
		for(j = 0; j < 4; j++)
			m[i][j] *= s;
}

void
transposem3(Matrix3 m)
{
	int i, j;
	double tmp;

	for(i = 0; i < 4; i++)
		for(j = i; j < 4; j++){
			tmp = m[i][j];
			m[i][j] = m[j][i];
			m[j][i] = tmp;
		}
}

double
detm3(Matrix3 m)
{
	return m[0][0]*(m[1][1]*(m[2][2]*m[3][3] - m[2][3]*m[3][2])+
			m[1][2]*(m[2][3]*m[3][1] - m[2][1]*m[3][3])+
			m[1][3]*(m[2][1]*m[3][2] - m[2][2]*m[3][1]))
	      -m[0][1]*(m[1][0]*(m[2][2]*m[3][3] - m[2][3]*m[3][2])+
			m[1][2]*(m[2][3]*m[3][0] - m[2][0]*m[3][3])+
			m[1][3]*(m[2][0]*m[3][2] - m[2][2]*m[3][0]))
	      +m[0][2]*(m[1][0]*(m[2][1]*m[3][3] - m[2][3]*m[3][1])+
			m[1][1]*(m[2][3]*m[3][0] - m[2][0]*m[3][3])+
			m[1][3]*(m[2][0]*m[3][1] - m[2][1]*m[3][0]))
	      -m[0][3]*(m[1][0]*(m[2][1]*m[3][2] - m[2][2]*m[3][1])+
			m[1][1]*(m[2][2]*m[3][0] - m[2][0]*m[3][2])+
			m[1][2]*(m[2][0]*m[3][1] - m[2][1]*m[3][0]));
}

double
tracem3(Matrix3 m)
{
	return m[0][0] + m[1][1] + m[2][2] + m[3][3];
}

void
adjm3(Matrix3 m)
{
	Matrix3 tmp;

	tmp[0][0]=m[1][1]*(m[2][2]*m[3][3] - m[2][3]*m[3][2])+
		  m[2][1]*(m[1][3]*m[3][2] - m[1][2]*m[3][3])+
		  m[3][1]*(m[1][2]*m[2][3] - m[1][3]*m[2][2]);
	tmp[0][1]=m[0][1]*(m[2][3]*m[3][2] - m[2][2]*m[3][3])+
		  m[2][1]*(m[0][2]*m[3][3] - m[0][3]*m[3][2])+
		  m[3][1]*(m[0][3]*m[2][2] - m[0][2]*m[2][3]);
	tmp[0][2]=m[0][1]*(m[1][2]*m[3][3] - m[1][3]*m[3][2])+
		  m[1][1]*(m[0][3]*m[3][2] - m[0][2]*m[3][3])+
		  m[3][1]*(m[0][2]*m[1][3] - m[0][3]*m[1][2]);
	tmp[0][3]=m[0][1]*(m[1][3]*m[2][2] - m[1][2]*m[2][3])+
		  m[1][1]*(m[0][2]*m[2][3] - m[0][3]*m[2][2])+
		  m[2][1]*(m[0][3]*m[1][2] - m[0][2]*m[1][3]);
	tmp[1][0]=m[1][0]*(m[2][3]*m[3][2] - m[2][2]*m[3][3])+
		  m[2][0]*(m[1][2]*m[3][3] - m[1][3]*m[3][2])+
		  m[3][0]*(m[1][3]*m[2][2] - m[1][2]*m[2][3]);
	tmp[1][1]=m[0][0]*(m[2][2]*m[3][3] - m[2][3]*m[3][2])+
		  m[2][0]*(m[0][3]*m[3][2] - m[0][2]*m[3][3])+
		  m[3][0]*(m[0][2]*m[2][3] - m[0][3]*m[2][2]);
	tmp[1][2]=m[0][0]*(m[1][3]*m[3][2] - m[1][2]*m[3][3])+
		  m[1][0]*(m[0][2]*m[3][3] - m[0][3]*m[3][2])+
		  m[3][0]*(m[0][3]*m[1][2] - m[0][2]*m[1][3]);
	tmp[1][3]=m[0][0]*(m[1][2]*m[2][3] - m[1][3]*m[2][2])+
		  m[1][0]*(m[0][3]*m[2][2] - m[0][2]*m[2][3])+
		  m[2][0]*(m[0][2]*m[1][3] - m[0][3]*m[1][2]);
	tmp[2][0]=m[1][0]*(m[2][1]*m[3][3] - m[2][3]*m[3][1])+
		  m[2][0]*(m[1][3]*m[3][1] - m[1][1]*m[3][3])+
		  m[3][0]*(m[1][1]*m[2][3] - m[1][3]*m[2][1]);
	tmp[2][1]=m[0][0]*(m[2][3]*m[3][1] - m[2][1]*m[3][3])+
		  m[2][0]*(m[0][1]*m[3][3] - m[0][3]*m[3][1])+
		  m[3][0]*(m[0][3]*m[2][1] - m[0][1]*m[2][3]);
	tmp[2][2]=m[0][0]*(m[1][1]*m[3][3] - m[1][3]*m[3][1])+
		  m[1][0]*(m[0][3]*m[3][1] - m[0][1]*m[3][3])+
		  m[3][0]*(m[0][1]*m[1][3] - m[0][3]*m[1][1]);
	tmp[2][3]=m[0][0]*(m[1][3]*m[2][1] - m[1][1]*m[2][3])+
		  m[1][0]*(m[0][1]*m[2][3] - m[0][3]*m[2][1])+
		  m[2][0]*(m[0][3]*m[1][1] - m[0][1]*m[1][3]);
	tmp[3][0]=m[1][0]*(m[2][2]*m[3][1] - m[2][1]*m[3][2])+
		  m[2][0]*(m[1][1]*m[3][2] - m[1][2]*m[3][1])+
		  m[3][0]*(m[1][2]*m[2][1] - m[1][1]*m[2][2]);
	tmp[3][1]=m[0][0]*(m[2][1]*m[3][2] - m[2][2]*m[3][1])+
		  m[2][0]*(m[0][2]*m[3][1] - m[0][1]*m[3][2])+
		  m[3][0]*(m[0][1]*m[2][2] - m[0][2]*m[2][1]);
	tmp[3][2]=m[0][0]*(m[1][2]*m[3][1] - m[1][1]*m[3][2])+
		  m[1][0]*(m[0][1]*m[3][2] - m[0][2]*m[3][1])+
		  m[3][0]*(m[0][2]*m[1][1] - m[0][1]*m[1][2]);
	tmp[3][3]=m[0][0]*(m[1][1]*m[2][2] - m[1][2]*m[2][1])+
		  m[1][0]*(m[0][2]*m[2][1] - m[0][1]*m[2][2])+
		  m[2][0]*(m[0][1]*m[1][2] - m[0][2]*m[1][1]);
	memmove(m, tmp, 4*4*sizeof(double));
}

/* Cayley-Hamilton */
//void
//invertm3(Matrix3 m)
//{
//	Matrix3 m², m³, r;
//	double det, trm, trm², trm³;
//
//	det = detm3(m);
//	if(det == 0)
//		return;
//	trm = tracem3(m);
//	memmove(m³, m, 4*4*sizeof(double));
//	mulm(m³, m³);
//	mulm(m³, m);
//	trm³ = tracem3(m³);
//	memmove(m², m, 4*4*sizeof(double));
//	mulm(m², m²);
//	trm² = tracem3(m²);
//	identity3(r);
//	smulm3(r, (trm*trm*trm - 3*trm*trm² + 2*trm³)/6);
//	smulm3(m, (trm*trm - trm²)/2);
//	smulm3(m², trm);
//	subm(r, m);
//	addm(r, m²);
//	subm(r, m³);
//	smulm(r, 1/det);
//	memmove(m, r, 4*4*sizeof(double));
//}

/* Cramer's */
void
invm3(Matrix3 m)
{
	double det;

	det = detm3(m);
	if(det == 0)
		return; /* singular matrices are not invertible */
	adjm3(m);
	smulm3(m, 1/det);
}

Point3
xform3(Point3 p, Matrix3 m)
{
	return (Point3){
		p.x*m[0][0] + p.y*m[0][1] + p.z*m[0][2] + p.w*m[0][3],
		p.x*m[1][0] + p.y*m[1][1] + p.z*m[1][2] + p.w*m[1][3],
		p.x*m[2][0] + p.y*m[2][1] + p.z*m[2][2] + p.w*m[2][3],
		p.x*m[3][0] + p.y*m[3][1] + p.z*m[3][2] + p.w*m[3][3],
	};
}
