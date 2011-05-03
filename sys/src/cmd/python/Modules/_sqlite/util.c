/* util.c - various utility functions
 *
 * Copyright (C) 2005-2006 Gerhard H�ring <gh@ghaering.de>
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

#include "module.h"
#include "connection.h"

int _sqlite_step_with_busyhandler(sqlite3_stmt* statement, Connection* connection
)
{
    int rc;

    Py_BEGIN_ALLOW_THREADS
    rc = sqlite3_step(statement);
    Py_END_ALLOW_THREADS

    return rc;
}

/**
 * Checks the SQLite error code and sets the appropriate DB-API exception.
 * Returns the error code (0 means no error occurred).
 */
int _seterror(sqlite3* db)
{
    int errorcode;

    errorcode = sqlite3_errcode(db);

    switch (errorcode)
    {
        case SQLITE_OK:
            PyErr_Clear();
            break;
        case SQLITE_INTERNAL:
        case SQLITE_NOTFOUND:
            PyErr_SetString(InternalError, sqlite3_errmsg(db));
            break;
        case SQLITE_NOMEM:
            (void)PyErr_NoMemory();
            break;
        case SQLITE_ERROR:
        case SQLITE_PERM:
        case SQLITE_ABORT:
        case SQLITE_BUSY:
        case SQLITE_LOCKED:
        case SQLITE_READONLY:
        case SQLITE_INTERRUPT:
        case SQLITE_IOERR:
        case SQLITE_FULL:
        case SQLITE_CANTOPEN:
        case SQLITE_PROTOCOL:
        case SQLITE_EMPTY:
        case SQLITE_SCHEMA:
            PyErr_SetString(OperationalError, sqlite3_errmsg(db));
            break;
        case SQLITE_CORRUPT:
            PyErr_SetString(DatabaseError, sqlite3_errmsg(db));
            break;
        case SQLITE_TOOBIG:
            PyErr_SetString(DataError, sqlite3_errmsg(db));
            break;
        case SQLITE_CONSTRAINT:
        case SQLITE_MISMATCH:
            PyErr_SetString(IntegrityError, sqlite3_errmsg(db));
            break;
        case SQLITE_MISUSE:
            PyErr_SetString(ProgrammingError, sqlite3_errmsg(db));
            break;
        default:
            PyErr_SetString(DatabaseError, sqlite3_errmsg(db));
            break;
    }

    return errorcode;
}

