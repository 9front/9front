#define NOP(x) x
#define CAT(a, b) a ## b
#define EOF	(-1)
x NOP(CAT(foo, EOF)) y
x NOP(CAT(EOF, foo)) y
x CAT(, EOF) y
y CAT(foo,) x
x CAT(,foo) y
X NOP(CAT(,)) y

#define NCAT(a)	foo ## a
NCAT(bar)

#define XCAT(a)	## a
foo XCAT(bar)

#define CAT3(foo)	a##foo##b
CAT3(blah)

#define BAR	3
#define FOO	CAT(BAR, 3)
FOO

/* Expected: a bc d */
CAT(a b, c d)

/*
 * CURRENTLY BROKEN:
 *     __VA_ARGS__ requires at least one item.
 *     It should accept an empty list.
#define xprint(a, ...)	print(a, __VA_ARGS__)
xprint("hi", "there")
xprint("hi")
*/

#define C	a,b
#define X(a)	a
#define Y	X(C)
Y

#define    x          3
#define    f(a)       f(x * (a))
#undef     x
#define    x          2
#define    g          f
#define    z          z[0]
#define    h          g(~
#define    m(a)       a(w)
#define    w          0,1
#define    t(a)       a
#define    p()        int
#define    q(x)       x
#define    r(x,y)     x ## y
#define    str(x)     # x
f(y+1) + f(f(z)) % t(t(g)(0) + t)(1);
g(x+(3,4)-w) | h 5) & m
(f)^m(m);
/*
 * CURRENTLY BROKEN:
 *     mac() needs at least one argument.
 *     It should treat no args as a single empty arg list.
p() i[q()] = { q(1), r(2,3), r(4,), r(,5), r(,) };
char c[2][6] = { str(hello), str() };
*/
