#include <u.h>
#include <libc.h>

/* We try to match the results specified by posix */
void
main(void)
{
	/*
	 * For any value of y (including NaN), 
	 *	if x is +1, 1.0 shall be returned.
	 */
	assert(pow(1.0, 132234.3) == 1.0);
	assert(pow(1.0, NaN()) == 1.0);

	/*
	 * For any value of x (including NaN),
	 *	if y is ±0, 1.0 shall be returned.
	 */
	assert(pow(10213.7, 0.0) == 1.0);
	assert(pow(NaN(), 0.0) == 1.0);

	/*
	 *  If x or y is a NaN, a NaN shall be returned (unless
	 * specified elsewhere in this description).
	 */
	assert(isNaN(pow(NaN(), NaN())));
	assert(isNaN(pow(NaN(), 42.42)));
	assert(isNaN(pow(42.42, NaN())));

	/*
	 * For any odd integer value of y > 0,
	 *	if x is ±0, ±0 shall be returned.
	 */
	assert(pow(0.0, 1.0) == 0.0);
	assert(pow(0.0, 39.0) == 0.0);
	assert(pow(-0.0, 1.0) == -0.0);

	/*
	 * For y > 0 and not an odd integer,
	 *	if x is ±0, +0 shall be returned.
	 */
	assert(pow(0.0, 2.0) == 0.0);
	assert(pow(0.0, 34.0) == 0.0);
	assert(pow(-0.0, 22.0) == 0.0);

	/* If x is -1, and y is ±Inf, 1.0 shall be returned. */
	assert(pow(-1.0, Inf(1)) == 1.0);
	/* For |x| < 1, if y is -Inf, +Inf shall be returned. */
	assert(isInf(pow(0.9, Inf(-1)), 1));
	/* For |x| > 1, if y is -Inf, +0 shall be returned. */
	assert(pow(1.1, Inf(-1)) == 0);
	/* For |x| < 1, if y is +Inf, +0 shall be returned. */
	assert(pow(0.9, Inf(1)) == 0.0);
	/* For |x| > 1, if y is +Inf, +Inf shall be returned. */
	assert(isInf(pow(1.1, Inf(1)), 1));
	/* For y an odd integer < 0, if x is -Inf, -0 shall be returned. */
	assert(pow(-7, Inf(-1)) == -0.0);
	/* For y < 0 and not an odd integer, if x is -Inf, +0 shall be returned. */
	assert(pow(Inf(-1), -0.3) == 0);
	/* For y an odd integer > 0, if x is -Inf, -Inf shall be returned. */
	assert(isInf(pow(Inf(-1), 7), -1));
	/* For y > 0 and not an odd integer, if x is -Inf, +Inf shall be returned. */
	assert(isInf(pow(Inf(-1), 19123.25324), 1));
	/* For y < 0, if x is +Inf, +0 shall be returned. */
	assert(pow(Inf(1), -1.3) == 0.0);
	/* For y > 0, if x is +Inf, +Inf shall be returned. */
	assert(isInf(pow(Inf(1), 1.7), 1));
	exits(nil);
}
