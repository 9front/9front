    /* module.c - the module itself
     *
     * Copyright (C) 2004-2006 Gerhard H�ring <gh@ghaering.de>
     *
     * This file is part of pysqlite.
     *
     * This software is provided 'as-is', without any express or implied
     * warranty.  In no event will the authors be held liable for any damages
     * arising from the use of this software.
     *
     * Permission is granted to anyone to use this software for any purpose,
     * including commercial applications, and to alter it and redistribute it
     * freely, subject to the following restrictions:
     *
     * 1. The origin of this software must not be misrepresented; you must not
     *    claim that you wrote the original software. If you use this software
     *    in a product, an acknowledgment in the product documentation would be
     *    appreciated but is not required.
     * 2. Altered source versions must be plainly marked as such, and must not be
     *    misrepresented as being the original software.
     * 3. This notice may not be removed or altered from any source distribution.
     */

#include "connection.h"
#include "statement.h"
#include "cursor.h"
#include "cache.h"
#include "prepare_protocol.h"
#include "microprotocols.h"
#include "row.h"

#if SQLITE_VERSION_NUMBER >= 3003003
#define HAVE_SHARED_CACHE
#endif

/* static objects at module-level */

PyObject* Error, *Warning, *InterfaceError, *DatabaseError, *InternalError,
    *OperationalError, *ProgrammingError, *IntegrityError, *DataError,
    *NotSupportedError, *OptimizedUnicode;

PyObject* converters;
int _enable_callback_tracebacks;

static PyObject* module_connect(PyObject* self, PyObject* args, PyObject*
        kwargs)
{
    /* Python seems to have no way of extracting a single keyword-arg at
     * C-level, so this code is redundant with the one in connection_init in
     * connection.c and must always be copied from there ... */

    static char *kwlist[] = {"database", "timeout", "detect_types", "isolation_level", "check_same_thread", "factory", "cached_statements", NULL, NULL};
    char* database;
    int detect_types = 0;
    PyObject* isolation_level;
    PyObject* factory = NULL;
    int check_same_thread = 1;
    int cached_statements;
    double timeout = 5.0;

    PyObject* result;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|diOiOi", kwlist,
                                     &database, &timeout, &detect_types, &isolation_level, &check_same_thread, &factory, &cached_statements))
    {
        return NULL; 
    }

    if (factory == NULL) {
        factory = (PyObject*)&ConnectionType;
    }

    result = PyObject_Call(factory, args, kwargs);

    return result;
}

static PyObject* module_complete(PyObject* self, PyObject* args, PyObject*
        kwargs)
{
    static char *kwlist[] = {"statement", NULL, NULL};
    char* statement;

    PyObject* result;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s", kwlist, &statement))
    {
        return NULL; 
    }

    if (sqlite3_complete(statement)) {
        result = Py_True;
    } else {
        result = Py_False;
    }

    Py_INCREF(result);

    return result;
}

#ifdef HAVE_SHARED_CACHE
static PyObject* module_enable_shared_cache(PyObject* self, PyObject* args, PyObject*
        kwargs)
{
    static char *kwlist[] = {"do_enable", NULL, NULL};
    int do_enable;
    int rc;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "i", kwlist, &do_enable))
    {
        return NULL; 
    }

    rc = sqlite3_enable_shared_cache(do_enable);

    if (rc != SQLITE_OK) {
        PyErr_SetString(OperationalError, "Changing the shared_cache flag failed");
        return NULL;
    } else {
        Py_INCREF(Py_None);
        return Py_None;
    }
}
#endif /* HAVE_SHARED_CACHE */

static PyObject* module_register_adapter(PyObject* self, PyObject* args, PyObject* kwargs)
{
    PyTypeObject* type;
    PyObject* caster;

    if (!PyArg_ParseTuple(args, "OO", &type, &caster)) {
        return NULL;
    }

    microprotocols_add(type, (PyObject*)&SQLitePrepareProtocolType, caster);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* module_register_converter(PyObject* self, PyObject* args, PyObject* kwargs)
{
    char* orig_name;
    char* name = NULL;
    char* c;
    PyObject* callable;
    PyObject* retval = NULL;

    if (!PyArg_ParseTuple(args, "sO", &orig_name, &callable)) {
        return NULL;
    }

    /* convert the name to lowercase */
    name = PyMem_Malloc(strlen(orig_name) + 2);
    if (!name) {
        goto error;
    }
    strcpy(name, orig_name);
    for (c = name; *c != (char)0; c++) {
        *c = (*c) & 0xDF;
    }

    if (PyDict_SetItemString(converters, name, callable) != 0) {
        goto error;
    }

    Py_INCREF(Py_None);
    retval = Py_None;
error:
    if (name) {
        PyMem_Free(name);
    }
    return retval;
}

static PyObject* enable_callback_tracebacks(PyObject* self, PyObject* args, PyObject* kwargs)
{
    if (!PyArg_ParseTuple(args, "i", &_enable_callback_tracebacks)) {
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

void converters_init(PyObject* dict)
{
    converters = PyDict_New();
    if (!converters) {
        return;
    }

    PyDict_SetItemString(dict, "converters", converters);
}

static PyMethodDef module_methods[] = {
    {"connect",  (PyCFunction)module_connect,  METH_VARARGS|METH_KEYWORDS, PyDoc_STR("Creates a connection.")},
    {"complete_statement",  (PyCFunction)module_complete,  METH_VARARGS|METH_KEYWORDS, PyDoc_STR("Checks if a string contains a complete SQL statement. Non-standard.")},
#ifdef HAVE_SHARED_CACHE
    {"enable_shared_cache",  (PyCFunction)module_enable_shared_cache,  METH_VARARGS|METH_KEYWORDS, PyDoc_STR("Enable or disable shared cache mode for the calling thread. Experimental/Non-standard.")},
#endif
    {"register_adapter", (PyCFunction)module_register_adapter, METH_VARARGS, PyDoc_STR("Registers an adapter with pysqlite's adapter registry. Non-standard.")},
    {"register_converter", (PyCFunction)module_register_converter, METH_VARARGS, PyDoc_STR("Registers a converter with pysqlite. Non-standard.")},
    {"adapt",  (PyCFunction)psyco_microprotocols_adapt, METH_VARARGS, psyco_microprotocols_adapt_doc},
    {"enable_callback_tracebacks",  (PyCFunction)enable_callback_tracebacks, METH_VARARGS, PyDoc_STR("Enable or disable callback functions throwing errors to stderr.")},
    {NULL, NULL}
};

struct _IntConstantPair {
    char* constant_name;
    int constant_value;
};

typedef struct _IntConstantPair IntConstantPair;

static IntConstantPair _int_constants[] = {
    {"PARSE_DECLTYPES", PARSE_DECLTYPES},
    {"PARSE_COLNAMES", PARSE_COLNAMES},

    {"SQLITE_OK", SQLITE_OK},
    {"SQLITE_DENY", SQLITE_DENY},
    {"SQLITE_IGNORE", SQLITE_IGNORE},
    {"SQLITE_CREATE_INDEX", SQLITE_CREATE_INDEX},
    {"SQLITE_CREATE_TABLE", SQLITE_CREATE_TABLE},
    {"SQLITE_CREATE_TEMP_INDEX", SQLITE_CREATE_TEMP_INDEX},
    {"SQLITE_CREATE_TEMP_TABLE", SQLITE_CREATE_TEMP_TABLE},
    {"SQLITE_CREATE_TEMP_TRIGGER", SQLITE_CREATE_TEMP_TRIGGER},
    {"SQLITE_CREATE_TEMP_VIEW", SQLITE_CREATE_TEMP_VIEW},
    {"SQLITE_CREATE_TRIGGER", SQLITE_CREATE_TRIGGER},
    {"SQLITE_CREATE_VIEW", SQLITE_CREATE_VIEW},
    {"SQLITE_DELETE", SQLITE_DELETE},
    {"SQLITE_DROP_INDEX", SQLITE_DROP_INDEX},
    {"SQLITE_DROP_TABLE", SQLITE_DROP_TABLE},
    {"SQLITE_DROP_TEMP_INDEX", SQLITE_DROP_TEMP_INDEX},
    {"SQLITE_DROP_TEMP_TABLE", SQLITE_DROP_TEMP_TABLE},
    {"SQLITE_DROP_TEMP_TRIGGER", SQLITE_DROP_TEMP_TRIGGER},
    {"SQLITE_DROP_TEMP_VIEW", SQLITE_DROP_TEMP_VIEW},
    {"SQLITE_DROP_TRIGGER", SQLITE_DROP_TRIGGER},
    {"SQLITE_DROP_VIEW", SQLITE_DROP_VIEW},
    {"SQLITE_INSERT", SQLITE_INSERT},
    {"SQLITE_PRAGMA", SQLITE_PRAGMA},
    {"SQLITE_READ", SQLITE_READ},
    {"SQLITE_SELECT", SQLITE_SELECT},
    {"SQLITE_TRANSACTION", SQLITE_TRANSACTION},
    {"SQLITE_UPDATE", SQLITE_UPDATE},
    {"SQLITE_ATTACH", SQLITE_ATTACH},
    {"SQLITE_DETACH", SQLITE_DETACH},
#if SQLITE_VERSION_NUMBER >= 3002001
    {"SQLITE_ALTER_TABLE", SQLITE_ALTER_TABLE},
    {"SQLITE_REINDEX", SQLITE_REINDEX},
#endif
#if SQLITE_VERSION_NUMBER >= 3003000
    {"SQLITE_ANALYZE", SQLITE_ANALYZE},
#endif
    {(char*)NULL, 0}
};

PyMODINIT_FUNC init_sqlite3(void)
{
    PyObject *module, *dict;
    PyObject *tmp_obj;
    int i;

    module = Py_InitModule("_sqlite3", module_methods);

    if (!module ||
        (row_setup_types() < 0) ||
        (cursor_setup_types() < 0) ||
        (connection_setup_types() < 0) ||
        (cache_setup_types() < 0) ||
        (statement_setup_types() < 0) ||
        (prepare_protocol_setup_types() < 0)
       ) {
        return;
    }

    Py_INCREF(&ConnectionType);
    PyModule_AddObject(module, "Connection", (PyObject*) &ConnectionType);
    Py_INCREF(&CursorType);
    PyModule_AddObject(module, "Cursor", (PyObject*) &CursorType);
    Py_INCREF(&CacheType);
    PyModule_AddObject(module, "Statement", (PyObject*)&StatementType);
    Py_INCREF(&StatementType);
    PyModule_AddObject(module, "Cache", (PyObject*) &CacheType);
    Py_INCREF(&SQLitePrepareProtocolType);
    PyModule_AddObject(module, "PrepareProtocol", (PyObject*) &SQLitePrepareProtocolType);
    Py_INCREF(&RowType);
    PyModule_AddObject(module, "Row", (PyObject*) &RowType);

    if (!(dict = PyModule_GetDict(module))) {
        goto error;
    }

    /*** Create DB-API Exception hierarchy */

    if (!(Error = PyErr_NewException(MODULE_NAME ".Error", PyExc_StandardError, NULL))) {
        goto error;
    }
    PyDict_SetItemString(dict, "Error", Error);

    if (!(Warning = PyErr_NewException(MODULE_NAME ".Warning", PyExc_StandardError, NULL))) {
        goto error;
    }
    PyDict_SetItemString(dict, "Warning", Warning);

    /* Error subclasses */

    if (!(InterfaceError = PyErr_NewException(MODULE_NAME ".InterfaceError", Error, NULL))) {
        goto error;
    }
    PyDict_SetItemString(dict, "InterfaceError", InterfaceError);

    if (!(DatabaseError = PyErr_NewException(MODULE_NAME ".DatabaseError", Error, NULL))) {
        goto error;
    }
    PyDict_SetItemString(dict, "DatabaseError", DatabaseError);

    /* DatabaseError subclasses */

    if (!(InternalError = PyErr_NewException(MODULE_NAME ".InternalError", DatabaseError, NULL))) {
        goto error;
    }
    PyDict_SetItemString(dict, "InternalError", InternalError);

    if (!(OperationalError = PyErr_NewException(MODULE_NAME ".OperationalError", DatabaseError, NULL))) {
        goto error;
    }
    PyDict_SetItemString(dict, "OperationalError", OperationalError);

    if (!(ProgrammingError = PyErr_NewException(MODULE_NAME ".ProgrammingError", DatabaseError, NULL))) {
        goto error;
    }
    PyDict_SetItemString(dict, "ProgrammingError", ProgrammingError);

    if (!(IntegrityError = PyErr_NewException(MODULE_NAME ".IntegrityError", DatabaseError,NULL))) {
        goto error;
    }
    PyDict_SetItemString(dict, "IntegrityError", IntegrityError);

    if (!(DataError = PyErr_NewException(MODULE_NAME ".DataError", DatabaseError, NULL))) {
        goto error;
    }
    PyDict_SetItemString(dict, "DataError", DataError);

    if (!(NotSupportedError = PyErr_NewException(MODULE_NAME ".NotSupportedError", DatabaseError, NULL))) {
        goto error;
    }
    PyDict_SetItemString(dict, "NotSupportedError", NotSupportedError);

    /* We just need "something" unique for OptimizedUnicode. It does not really
     * need to be a string subclass. Just anything that can act as a special
     * marker for us. So I pulled PyCell_Type out of my magic hat.
     */
    Py_INCREF((PyObject*)&PyCell_Type);
    OptimizedUnicode = (PyObject*)&PyCell_Type;
    PyDict_SetItemString(dict, "OptimizedUnicode", OptimizedUnicode);

    /* Set integer constants */
    for (i = 0; _int_constants[i].constant_name != 0; i++) {
        tmp_obj = PyInt_FromLong(_int_constants[i].constant_value);
        if (!tmp_obj) {
            goto error;
        }
        PyDict_SetItemString(dict, _int_constants[i].constant_name, tmp_obj);
        Py_DECREF(tmp_obj);
    }

    if (!(tmp_obj = PyString_FromString(PYSQLITE_VERSION))) {
        goto error;
    }
    PyDict_SetItemString(dict, "version", tmp_obj);
    Py_DECREF(tmp_obj);

    if (!(tmp_obj = PyString_FromString(sqlite3_libversion()))) {
        goto error;
    }
    PyDict_SetItemString(dict, "sqlite_version", tmp_obj);
    Py_DECREF(tmp_obj);

    /* initialize microprotocols layer */
    microprotocols_init(dict);

    /* initialize the default converters */
    converters_init(dict);

    _enable_callback_tracebacks = 0;

    /* Original comment form _bsddb.c in the Python core. This is also still
     * needed nowadays for Python 2.3/2.4.
     * 
     * PyEval_InitThreads is called here due to a quirk in python 1.5
     * - 2.2.1 (at least) according to Russell Williamson <merel@wt.net>:
     * The global interepreter lock is not initialized until the first
     * thread is created using thread.start_new_thread() or fork() is
     * called.  that would cause the ALLOW_THREADS here to segfault due
     * to a null pointer reference if no threads or child processes
     * have been created.  This works around that and is a no-op if
     * threads have already been initialized.
     *  (see pybsddb-users mailing list post on 2002-08-07)
     */
    PyEval_InitThreads();

error:
    if (PyErr_Occurred())
    {
        PyErr_SetString(PyExc_ImportError, MODULE_NAME ": init failed");
    }
}
