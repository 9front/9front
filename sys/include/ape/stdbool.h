#ifndef __STDBOOL_H__
#define __STDBOOL_H__

/* Strictly speaking, this should be a built-in type. */
typedef char _Bool;

#define bool _Bool
#define true 1
#define false 0
#define __bool_true_false_are_defined 1

#endif
