# hgweb/wsgicgi.py - CGI->WSGI translator
#
# Copyright 2006 Eric Hopper <hopper@omnifarious.org>
#
# This software may be used and distributed according to the terms of the
# GNU General Public License version 2, incorporated herein by reference.
#
# This was originally copied from the public domain code at
# http://www.python.org/dev/peps/pep-0333/#the-server-gateway-side

import os, sys
from mercurial import util

def launch(application):
    util.set_binary(sys.stdin)
    util.set_binary(sys.stdout)

    environ = dict(os.environ.iteritems())
    environ.setdefault('PATH_INFO', '')
    if '.cgi' in environ['PATH_INFO']:
        environ['PATH_INFO'] = environ['PATH_INFO'].split('.cgi', 1)[1]

    environ['wsgi.input'] = sys.stdin
    environ['wsgi.errors'] = sys.stderr
    environ['wsgi.version'] = (1, 0)
    environ['wsgi.multithread'] = False
    environ['wsgi.multiprocess'] = True
    environ['wsgi.run_once'] = True

    if environ.get('HTTPS','off').lower() in ('on','1','yes'):
        environ['wsgi.url_scheme'] = 'https'
    else:
        environ['wsgi.url_scheme'] = 'http'

    headers_set = []
    headers_sent = []
    out = sys.stdout

    def write(data):
        if not headers_set:
            raise AssertionError("write() before start_response()")

        elif not headers_sent:
            # Before the first output, send the stored headers
            status, response_headers = headers_sent[:] = headers_set
            out.write('Status: %s\r\n' % status)
            for header in response_headers:
                out.write('%s: %s\r\n' % header)
            out.write('\r\n')

        out.write(data)
        out.flush()

    def start_response(status, response_headers, exc_info=None):
        if exc_info:
            try:
                if headers_sent:
                    # Re-raise original exception if headers sent
                    raise exc_info[0](exc_info[1], exc_info[2])
            finally:
                exc_info = None     # avoid dangling circular ref
        elif headers_set:
            raise AssertionError("Headers already set!")

        headers_set[:] = [status, response_headers]
        return write

    content = application(environ, start_response)
    for chunk in content:
        write(chunk)
