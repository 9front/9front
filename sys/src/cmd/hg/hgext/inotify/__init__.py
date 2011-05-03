# __init__.py - inotify-based status acceleration for Linux
#
# Copyright 2006, 2007, 2008 Bryan O'Sullivan <bos@serpentine.com>
# Copyright 2007, 2008 Brendan Cully <brendan@kublai.com>
#
# This software may be used and distributed according to the terms of the
# GNU General Public License version 2, incorporated herein by reference.

'''accelerate status report using Linux's inotify service'''

# todo: socket permissions

from mercurial.i18n import _
from mercurial import cmdutil, util
import server
from weakref import proxy
from client import client, QueryFailed

def serve(ui, repo, **opts):
    '''start an inotify server for this repository'''
    timeout = opts.get('timeout')
    if timeout:
        timeout = float(timeout) * 1e3

    class service(object):
        def init(self):
            try:
                self.master = server.master(ui, repo.dirstate,
                                            repo.root, timeout)
            except server.AlreadyStartedException, inst:
                raise util.Abort(str(inst))

        def run(self):
            try:
                self.master.run()
            finally:
                self.master.shutdown()

    service = service()
    logfile = ui.config('inotify', 'log')
    cmdutil.service(opts, initfn=service.init, runfn=service.run,
                    logfile=logfile)

def debuginotify(ui, repo, **opts):
    '''debugging information for inotify extension

    Prints the list of directories being watched by the inotify server.
    '''
    cli = client(ui, repo)
    response = cli.debugquery()

    ui.write(_('directories being watched:\n'))
    for path in response:
        ui.write(('  %s/\n') % path)

def reposetup(ui, repo):
    if not hasattr(repo, 'dirstate'):
        return

    class inotifydirstate(repo.dirstate.__class__):

        # We'll set this to false after an unsuccessful attempt so that
        # next calls of status() within the same instance don't try again
        # to start an inotify server if it won't start.
        _inotifyon = True

        def status(self, match, ignored, clean, unknown=True):
            files = match.files()
            if '.' in files:
                files = []
            if self._inotifyon and not ignored:
                cli = client(ui, repo)
                try:
                    result = cli.statusquery(files, match, False,
                                            clean, unknown)
                except QueryFailed, instr:
                    ui.debug(str(instr))
                    # don't retry within the same hg instance
                    inotifydirstate._inotifyon = False
                    pass
                else:
                    if ui.config('inotify', 'debug'):
                        r2 = super(inotifydirstate, self).status(
                            match, False, clean, unknown)
                        for c,a,b in zip('LMARDUIC', result, r2):
                            for f in a:
                                if f not in b:
                                    ui.warn('*** inotify: %s +%s\n' % (c, f))
                            for f in b:
                                if f not in a:
                                    ui.warn('*** inotify: %s -%s\n' % (c, f))
                        result = r2
                    return result
            return super(inotifydirstate, self).status(
                match, ignored, clean, unknown)

    repo.dirstate.__class__ = inotifydirstate

cmdtable = {
    'debuginotify':
        (debuginotify, [], ('hg debuginotify')),
    '^inserve':
        (serve,
         [('d', 'daemon', None, _('run server in background')),
          ('', 'daemon-pipefds', '', _('used internally by daemon mode')),
          ('t', 'idle-timeout', '', _('minutes to sit idle before exiting')),
          ('', 'pid-file', '', _('name of file to write process ID to'))],
         _('hg inserve [OPTION]...')),
    }
