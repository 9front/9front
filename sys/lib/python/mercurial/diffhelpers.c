/*
 * diffhelpers.c - helper routines for mpatch
 *
 * Copyright 2007 Chris Mason <chris.mason@oracle.com>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License v2, incorporated herein by reference.
 */

#include <Python.h>
#include <stdlib.h>
#include <string.h>

static char diffhelpers_doc[] = "Efficient diff parsing";
static PyObject *diffhelpers_Error;


/* fixup the last lines of a and b when the patch has no newline at eof */
static void _fix_newline(PyObject *hunk, PyObject *a, PyObject *b)
{
	int hunksz = PyList_Size(hunk);
	PyObject *s = PyList_GET_ITEM(hunk, hunksz-1);
	char *l = PyString_AS_STRING(s);
	int sz = PyString_GET_SIZE(s);
	int alen = PyList_Size(a);
	int blen = PyList_Size(b);
	char c = l[0];

	PyObject *hline = PyString_FromStringAndSize(l, sz-1);
	if (c == ' ' || c == '+') {
		PyObject *rline = PyString_FromStringAndSize(l+1, sz-2);
		PyList_SetItem(b, blen-1, rline);
	}
	if (c == ' ' || c == '-') {
		Py_INCREF(hline);
		PyList_SetItem(a, alen-1, hline);
	}
	PyList_SetItem(hunk, hunksz-1, hline);
}

/* python callable form of _fix_newline */
static PyObject *
fix_newline(PyObject *self, PyObject *args)
{
	PyObject *hunk, *a, *b;
	if (!PyArg_ParseTuple(args, "OOO", &hunk, &a, &b))
		return NULL;
	_fix_newline(hunk, a, b);
	return Py_BuildValue("l", 0);
}

/*
 * read lines from fp into the hunk.  The hunk is parsed into two arrays
 * a and b.  a gets the old state of the text, b gets the new state
 * The control char from the hunk is saved when inserting into a, but not b
 * (for performance while deleting files)
 */
static PyObject *
addlines(PyObject *self, PyObject *args)
{

	PyObject *fp, *hunk, *a, *b, *x;
	int i;
	int lena, lenb;
	int num;
	int todoa, todob;
	char *s, c;
	PyObject *l;
	if (!PyArg_ParseTuple(args, "OOiiOO", &fp, &hunk, &lena, &lenb, &a, &b))
		return NULL;

	while(1) {
		todoa = lena - PyList_Size(a);
		todob = lenb - PyList_Size(b);
		num = todoa > todob ? todoa : todob;
		if (num == 0)
		    break;
		for (i = 0 ; i < num ; i++) {
			x = PyFile_GetLine(fp, 0);
			s = PyString_AS_STRING(x);
			c = *s;
			if (strcmp(s, "\\ No newline at end of file\n") == 0) {
				_fix_newline(hunk, a, b);
				continue;
			}
			if (c == '\n') {
				/* Some patches may be missing the control char
				 * on empty lines. Supply a leading space. */
				Py_DECREF(x);
				x = PyString_FromString(" \n");
			}
			PyList_Append(hunk, x);
			if (c == '+') {
				l = PyString_FromString(s + 1);
				PyList_Append(b, l);
				Py_DECREF(l);
			} else if (c == '-') {
				PyList_Append(a, x);
			} else {
				l = PyString_FromString(s + 1);
				PyList_Append(b, l);
				Py_DECREF(l);
				PyList_Append(a, x);
			}
			Py_DECREF(x);
		}
	}
	return Py_BuildValue("l", 0);
}

/*
 * compare the lines in a with the lines in b.  a is assumed to have
 * a control char at the start of each line, this char is ignored in the
 * compare
 */
static PyObject *
testhunk(PyObject *self, PyObject *args)
{

	PyObject *a, *b;
	long bstart;
	int alen, blen;
	int i;
	char *sa, *sb;

	if (!PyArg_ParseTuple(args, "OOl", &a, &b, &bstart))
		return NULL;
	alen = PyList_Size(a);
	blen = PyList_Size(b);
	if (alen > blen - bstart) {
		return Py_BuildValue("l", -1);
	}
	for (i = 0 ; i < alen ; i++) {
		sa = PyString_AS_STRING(PyList_GET_ITEM(a, i));
		sb = PyString_AS_STRING(PyList_GET_ITEM(b, i + bstart));
		if (strcmp(sa+1, sb) != 0)
			return Py_BuildValue("l", -1);
	}
	return Py_BuildValue("l", 0);
}

static PyMethodDef methods[] = {
	{"addlines", addlines, METH_VARARGS, "add lines to a hunk\n"},
	{"fix_newline", fix_newline, METH_VARARGS, "fixup newline counters\n"},
	{"testhunk", testhunk, METH_VARARGS, "test lines in a hunk\n"},
	{NULL, NULL}
};

PyMODINIT_FUNC
initdiffhelpers(void)
{
	Py_InitModule3("diffhelpers", methods, diffhelpers_doc);
	diffhelpers_Error = PyErr_NewException("diffhelpers.diffhelpersError",
	                                        NULL, NULL);
}

