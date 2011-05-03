# cmdutil.py - help for command processing in mercurial
#
# Copyright 2005-2007 Matt Mackall <mpm@selenic.com>
#
# This software may be used and distributed according to the terms of the
# GNU General Public License version 2, incorporated herein by reference.

from node import hex, nullid, nullrev, short
from i18n import _
import os, sys, errno, re, glob
import mdiff, bdiff, util, templater, patch, error, encoding
import match as _match

revrangesep = ':'

def findpossible(cmd, table, strict=False):
    """
    Return cmd -> (aliases, command table entry)
    for each matching command.
    Return debug commands (or their aliases) only if no normal command matches.
    """
    choice = {}
    debugchoice = {}
    for e in table.keys():
        aliases = e.lstrip("^").split("|")
        found = None
        if cmd in aliases:
            found = cmd
        elif not strict:
            for a in aliases:
                if a.startswith(cmd):
                    found = a
                    break
        if found is not None:
            if aliases[0].startswith("debug") or found.startswith("debug"):
                debugchoice[found] = (aliases, table[e])
            else:
                choice[found] = (aliases, table[e])

    if not choice and debugchoice:
        choice = debugchoice

    return choice

def findcmd(cmd, table, strict=True):
    """Return (aliases, command table entry) for command string."""
    choice = findpossible(cmd, table, strict)

    if cmd in choice:
        return choice[cmd]

    if len(choice) > 1:
        clist = choice.keys()
        clist.sort()
        raise error.AmbiguousCommand(cmd, clist)

    if choice:
        return choice.values()[0]

    raise error.UnknownCommand(cmd)

def bail_if_changed(repo):
    if repo.dirstate.parents()[1] != nullid:
        raise util.Abort(_('outstanding uncommitted merge'))
    modified, added, removed, deleted = repo.status()[:4]
    if modified or added or removed or deleted:
        raise util.Abort(_("outstanding uncommitted changes"))

def logmessage(opts):
    """ get the log message according to -m and -l option """
    message = opts.get('message')
    logfile = opts.get('logfile')

    if message and logfile:
        raise util.Abort(_('options --message and --logfile are mutually '
                           'exclusive'))
    if not message and logfile:
        try:
            if logfile == '-':
                message = sys.stdin.read()
            else:
                message = open(logfile).read()
        except IOError, inst:
            raise util.Abort(_("can't read commit message '%s': %s") %
                             (logfile, inst.strerror))
    return message

def loglimit(opts):
    """get the log limit according to option -l/--limit"""
    limit = opts.get('limit')
    if limit:
        try:
            limit = int(limit)
        except ValueError:
            raise util.Abort(_('limit must be a positive integer'))
        if limit <= 0: raise util.Abort(_('limit must be positive'))
    else:
        limit = sys.maxint
    return limit

def remoteui(src, opts):
    'build a remote ui from ui or repo and opts'
    if hasattr(src, 'baseui'): # looks like a repository
        dst = src.baseui.copy() # drop repo-specific config
        src = src.ui # copy target options from repo
    else: # assume it's a global ui object
        dst = src.copy() # keep all global options

    # copy ssh-specific options
    for o in 'ssh', 'remotecmd':
        v = opts.get(o) or src.config('ui', o)
        if v:
            dst.setconfig("ui", o, v)
    # copy bundle-specific options
    r = src.config('bundle', 'mainreporoot')
    if r:
        dst.setconfig('bundle', 'mainreporoot', r)

    return dst

def revpair(repo, revs):
    '''return pair of nodes, given list of revisions. second item can
    be None, meaning use working dir.'''

    def revfix(repo, val, defval):
        if not val and val != 0 and defval is not None:
            val = defval
        return repo.lookup(val)

    if not revs:
        return repo.dirstate.parents()[0], None
    end = None
    if len(revs) == 1:
        if revrangesep in revs[0]:
            start, end = revs[0].split(revrangesep, 1)
            start = revfix(repo, start, 0)
            end = revfix(repo, end, len(repo) - 1)
        else:
            start = revfix(repo, revs[0], None)
    elif len(revs) == 2:
        if revrangesep in revs[0] or revrangesep in revs[1]:
            raise util.Abort(_('too many revisions specified'))
        start = revfix(repo, revs[0], None)
        end = revfix(repo, revs[1], None)
    else:
        raise util.Abort(_('too many revisions specified'))
    return start, end

def revrange(repo, revs):
    """Yield revision as strings from a list of revision specifications."""

    def revfix(repo, val, defval):
        if not val and val != 0 and defval is not None:
            return defval
        return repo.changelog.rev(repo.lookup(val))

    seen, l = set(), []
    for spec in revs:
        if revrangesep in spec:
            start, end = spec.split(revrangesep, 1)
            start = revfix(repo, start, 0)
            end = revfix(repo, end, len(repo) - 1)
            step = start > end and -1 or 1
            for rev in xrange(start, end+step, step):
                if rev in seen:
                    continue
                seen.add(rev)
                l.append(rev)
        else:
            rev = revfix(repo, spec, None)
            if rev in seen:
                continue
            seen.add(rev)
            l.append(rev)

    return l

def make_filename(repo, pat, node,
                  total=None, seqno=None, revwidth=None, pathname=None):
    node_expander = {
        'H': lambda: hex(node),
        'R': lambda: str(repo.changelog.rev(node)),
        'h': lambda: short(node),
        }
    expander = {
        '%': lambda: '%',
        'b': lambda: os.path.basename(repo.root),
        }

    try:
        if node:
            expander.update(node_expander)
        if node:
            expander['r'] = (lambda:
                    str(repo.changelog.rev(node)).zfill(revwidth or 0))
        if total is not None:
            expander['N'] = lambda: str(total)
        if seqno is not None:
            expander['n'] = lambda: str(seqno)
        if total is not None and seqno is not None:
            expander['n'] = lambda: str(seqno).zfill(len(str(total)))
        if pathname is not None:
            expander['s'] = lambda: os.path.basename(pathname)
            expander['d'] = lambda: os.path.dirname(pathname) or '.'
            expander['p'] = lambda: pathname

        newname = []
        patlen = len(pat)
        i = 0
        while i < patlen:
            c = pat[i]
            if c == '%':
                i += 1
                c = pat[i]
                c = expander[c]()
            newname.append(c)
            i += 1
        return ''.join(newname)
    except KeyError, inst:
        raise util.Abort(_("invalid format spec '%%%s' in output filename") %
                         inst.args[0])

def make_file(repo, pat, node=None,
              total=None, seqno=None, revwidth=None, mode='wb', pathname=None):

    writable = 'w' in mode or 'a' in mode

    if not pat or pat == '-':
        return writable and sys.stdout or sys.stdin
    if hasattr(pat, 'write') and writable:
        return pat
    if hasattr(pat, 'read') and 'r' in mode:
        return pat
    return open(make_filename(repo, pat, node, total, seqno, revwidth,
                              pathname),
                mode)

def expandpats(pats):
    if not util.expandglobs:
        return list(pats)
    ret = []
    for p in pats:
        kind, name = _match._patsplit(p, None)
        if kind is None:
            try:
                globbed = glob.glob(name)
            except re.error:
                globbed = [name]
            if globbed:
                ret.extend(globbed)
                continue
        ret.append(p)
    return ret

def match(repo, pats=[], opts={}, globbed=False, default='relpath'):
    if not globbed and default == 'relpath':
        pats = expandpats(pats or [])
    m = _match.match(repo.root, repo.getcwd(), pats,
                    opts.get('include'), opts.get('exclude'), default)
    def badfn(f, msg):
        repo.ui.warn("%s: %s\n" % (m.rel(f), msg))
    m.bad = badfn
    return m

def matchall(repo):
    return _match.always(repo.root, repo.getcwd())

def matchfiles(repo, files):
    return _match.exact(repo.root, repo.getcwd(), files)

def findrenames(repo, added, removed, threshold):
    '''find renamed files -- yields (before, after, score) tuples'''
    ctx = repo['.']
    for a in added:
        aa = repo.wread(a)
        bestname, bestscore = None, threshold
        for r in removed:
            if r not in ctx:
                continue
            rr = ctx.filectx(r).data()

            # bdiff.blocks() returns blocks of matching lines
            # count the number of bytes in each
            equal = 0
            alines = mdiff.splitnewlines(aa)
            matches = bdiff.blocks(aa, rr)
            for x1,x2,y1,y2 in matches:
                for line in alines[x1:x2]:
                    equal += len(line)

            lengths = len(aa) + len(rr)
            if lengths:
                myscore = equal*2.0 / lengths
                if myscore >= bestscore:
                    bestname, bestscore = r, myscore
        if bestname:
            yield bestname, a, bestscore

def addremove(repo, pats=[], opts={}, dry_run=None, similarity=None):
    if dry_run is None:
        dry_run = opts.get('dry_run')
    if similarity is None:
        similarity = float(opts.get('similarity') or 0)
    # we'd use status here, except handling of symlinks and ignore is tricky
    added, unknown, deleted, removed = [], [], [], []
    audit_path = util.path_auditor(repo.root)
    m = match(repo, pats, opts)
    for abs in repo.walk(m):
        target = repo.wjoin(abs)
        good = True
        try:
            audit_path(abs)
        except:
            good = False
        rel = m.rel(abs)
        exact = m.exact(abs)
        if good and abs not in repo.dirstate:
            unknown.append(abs)
            if repo.ui.verbose or not exact:
                repo.ui.status(_('adding %s\n') % ((pats and rel) or abs))
        elif repo.dirstate[abs] != 'r' and (not good or not util.lexists(target)
            or (os.path.isdir(target) and not os.path.islink(target))):
            deleted.append(abs)
            if repo.ui.verbose or not exact:
                repo.ui.status(_('removing %s\n') % ((pats and rel) or abs))
        # for finding renames
        elif repo.dirstate[abs] == 'r':
            removed.append(abs)
        elif repo.dirstate[abs] == 'a':
            added.append(abs)
    if not dry_run:
        repo.remove(deleted)
        repo.add(unknown)
    if similarity > 0:
        for old, new, score in findrenames(repo, added + unknown,
                                           removed + deleted, similarity):
            if repo.ui.verbose or not m.exact(old) or not m.exact(new):
                repo.ui.status(_('recording removal of %s as rename to %s '
                                 '(%d%% similar)\n') %
                               (m.rel(old), m.rel(new), score * 100))
            if not dry_run:
                repo.copy(old, new)

def copy(ui, repo, pats, opts, rename=False):
    # called with the repo lock held
    #
    # hgsep => pathname that uses "/" to separate directories
    # ossep => pathname that uses os.sep to separate directories
    cwd = repo.getcwd()
    targets = {}
    after = opts.get("after")
    dryrun = opts.get("dry_run")

    def walkpat(pat):
        srcs = []
        m = match(repo, [pat], opts, globbed=True)
        for abs in repo.walk(m):
            state = repo.dirstate[abs]
            rel = m.rel(abs)
            exact = m.exact(abs)
            if state in '?r':
                if exact and state == '?':
                    ui.warn(_('%s: not copying - file is not managed\n') % rel)
                if exact and state == 'r':
                    ui.warn(_('%s: not copying - file has been marked for'
                              ' remove\n') % rel)
                continue
            # abs: hgsep
            # rel: ossep
            srcs.append((abs, rel, exact))
        return srcs

    # abssrc: hgsep
    # relsrc: ossep
    # otarget: ossep
    def copyfile(abssrc, relsrc, otarget, exact):
        abstarget = util.canonpath(repo.root, cwd, otarget)
        reltarget = repo.pathto(abstarget, cwd)
        target = repo.wjoin(abstarget)
        src = repo.wjoin(abssrc)
        state = repo.dirstate[abstarget]

        # check for collisions
        prevsrc = targets.get(abstarget)
        if prevsrc is not None:
            ui.warn(_('%s: not overwriting - %s collides with %s\n') %
                    (reltarget, repo.pathto(abssrc, cwd),
                     repo.pathto(prevsrc, cwd)))
            return

        # check for overwrites
        exists = os.path.exists(target)
        if not after and exists or after and state in 'mn':
            if not opts['force']:
                ui.warn(_('%s: not overwriting - file exists\n') %
                        reltarget)
                return

        if after:
            if not exists:
                return
        elif not dryrun:
            try:
                if exists:
                    os.unlink(target)
                targetdir = os.path.dirname(target) or '.'
                if not os.path.isdir(targetdir):
                    os.makedirs(targetdir)
                util.copyfile(src, target)
            except IOError, inst:
                if inst.errno == errno.ENOENT:
                    ui.warn(_('%s: deleted in working copy\n') % relsrc)
                else:
                    ui.warn(_('%s: cannot copy - %s\n') %
                            (relsrc, inst.strerror))
                    return True # report a failure

        if ui.verbose or not exact:
            if rename:
                ui.status(_('moving %s to %s\n') % (relsrc, reltarget))
            else:
                ui.status(_('copying %s to %s\n') % (relsrc, reltarget))

        targets[abstarget] = abssrc

        # fix up dirstate
        origsrc = repo.dirstate.copied(abssrc) or abssrc
        if abstarget == origsrc: # copying back a copy?
            if state not in 'mn' and not dryrun:
                repo.dirstate.normallookup(abstarget)
        else:
            if repo.dirstate[origsrc] == 'a' and origsrc == abssrc:
                if not ui.quiet:
                    ui.warn(_("%s has not been committed yet, so no copy "
                              "data will be stored for %s.\n")
                            % (repo.pathto(origsrc, cwd), reltarget))
                if repo.dirstate[abstarget] in '?r' and not dryrun:
                    repo.add([abstarget])
            elif not dryrun:
                repo.copy(origsrc, abstarget)

        if rename and not dryrun:
            repo.remove([abssrc], not after)

    # pat: ossep
    # dest ossep
    # srcs: list of (hgsep, hgsep, ossep, bool)
    # return: function that takes hgsep and returns ossep
    def targetpathfn(pat, dest, srcs):
        if os.path.isdir(pat):
            abspfx = util.canonpath(repo.root, cwd, pat)
            abspfx = util.localpath(abspfx)
            if destdirexists:
                striplen = len(os.path.split(abspfx)[0])
            else:
                striplen = len(abspfx)
            if striplen:
                striplen += len(os.sep)
            res = lambda p: os.path.join(dest, util.localpath(p)[striplen:])
        elif destdirexists:
            res = lambda p: os.path.join(dest,
                                         os.path.basename(util.localpath(p)))
        else:
            res = lambda p: dest
        return res

    # pat: ossep
    # dest ossep
    # srcs: list of (hgsep, hgsep, ossep, bool)
    # return: function that takes hgsep and returns ossep
    def targetpathafterfn(pat, dest, srcs):
        if _match.patkind(pat):
            # a mercurial pattern
            res = lambda p: os.path.join(dest,
                                         os.path.basename(util.localpath(p)))
        else:
            abspfx = util.canonpath(repo.root, cwd, pat)
            if len(abspfx) < len(srcs[0][0]):
                # A directory. Either the target path contains the last
                # component of the source path or it does not.
                def evalpath(striplen):
                    score = 0
                    for s in srcs:
                        t = os.path.join(dest, util.localpath(s[0])[striplen:])
                        if os.path.exists(t):
                            score += 1
                    return score

                abspfx = util.localpath(abspfx)
                striplen = len(abspfx)
                if striplen:
                    striplen += len(os.sep)
                if os.path.isdir(os.path.join(dest, os.path.split(abspfx)[1])):
                    score = evalpath(striplen)
                    striplen1 = len(os.path.split(abspfx)[0])
                    if striplen1:
                        striplen1 += len(os.sep)
                    if evalpath(striplen1) > score:
                        striplen = striplen1
                res = lambda p: os.path.join(dest,
                                             util.localpath(p)[striplen:])
            else:
                # a file
                if destdirexists:
                    res = lambda p: os.path.join(dest,
                                        os.path.basename(util.localpath(p)))
                else:
                    res = lambda p: dest
        return res


    pats = expandpats(pats)
    if not pats:
        raise util.Abort(_('no source or destination specified'))
    if len(pats) == 1:
        raise util.Abort(_('no destination specified'))
    dest = pats.pop()
    destdirexists = os.path.isdir(dest) and not os.path.islink(dest)
    if not destdirexists:
        if len(pats) > 1 or _match.patkind(pats[0]):
            raise util.Abort(_('with multiple sources, destination must be an '
                               'existing directory'))
        if util.endswithsep(dest):
            raise util.Abort(_('destination %s is not a directory') % dest)

    tfn = targetpathfn
    if after:
        tfn = targetpathafterfn
    copylist = []
    for pat in pats:
        srcs = walkpat(pat)
        if not srcs:
            continue
        copylist.append((tfn(pat, dest, srcs), srcs))
    if not copylist:
        raise util.Abort(_('no files to copy'))

    errors = 0
    for targetpath, srcs in copylist:
        for abssrc, relsrc, exact in srcs:
            if copyfile(abssrc, relsrc, targetpath(abssrc), exact):
                errors += 1

    if errors:
        ui.warn(_('(consider using --after)\n'))

    return errors

def service(opts, parentfn=None, initfn=None, runfn=None, logfile=None):
    '''Run a command as a service.'''

    if opts['daemon'] and not opts['daemon_pipefds']:
        rfd, wfd = os.pipe()
        args = sys.argv[:]
        args.append('--daemon-pipefds=%d,%d' % (rfd, wfd))
        # Don't pass --cwd to the child process, because we've already
        # changed directory.
        for i in xrange(1,len(args)):
            if args[i].startswith('--cwd='):
                del args[i]
                break
            elif args[i].startswith('--cwd'):
                del args[i:i+2]
                break
        pid = os.spawnvp(os.P_NOWAIT | getattr(os, 'P_DETACH', 0),
                         args[0], args)
        os.close(wfd)
        os.read(rfd, 1)
        if parentfn:
            return parentfn(pid)
        else:
            os._exit(0)

    if initfn:
        initfn()

    if opts['pid_file']:
        fp = open(opts['pid_file'], 'w')
        fp.write(str(os.getpid()) + '\n')
        fp.close()

    if opts['daemon_pipefds']:
        rfd, wfd = [int(x) for x in opts['daemon_pipefds'].split(',')]
        os.close(rfd)
        try:
            os.setsid()
        except AttributeError:
            pass
        os.write(wfd, 'y')
        os.close(wfd)
        sys.stdout.flush()
        sys.stderr.flush()

        nullfd = os.open(util.nulldev, os.O_RDWR)
        logfilefd = nullfd
        if logfile:
            logfilefd = os.open(logfile, os.O_RDWR | os.O_CREAT | os.O_APPEND)
        os.dup2(nullfd, 0)
        os.dup2(logfilefd, 1)
        os.dup2(logfilefd, 2)
        if nullfd not in (0, 1, 2):
            os.close(nullfd)
        if logfile and logfilefd not in (0, 1, 2):
            os.close(logfilefd)

    if runfn:
        return runfn()

class changeset_printer(object):
    '''show changeset information when templating not requested.'''

    def __init__(self, ui, repo, patch, diffopts, buffered):
        self.ui = ui
        self.repo = repo
        self.buffered = buffered
        self.patch = patch
        self.diffopts = diffopts
        self.header = {}
        self.hunk = {}
        self.lastheader = None

    def flush(self, rev):
        if rev in self.header:
            h = self.header[rev]
            if h != self.lastheader:
                self.lastheader = h
                self.ui.write(h)
            del self.header[rev]
        if rev in self.hunk:
            self.ui.write(self.hunk[rev])
            del self.hunk[rev]
            return 1
        return 0

    def show(self, ctx, copies=(), **props):
        if self.buffered:
            self.ui.pushbuffer()
            self._show(ctx, copies, props)
            self.hunk[ctx.rev()] = self.ui.popbuffer()
        else:
            self._show(ctx, copies, props)

    def _show(self, ctx, copies, props):
        '''show a single changeset or file revision'''
        changenode = ctx.node()
        rev = ctx.rev()

        if self.ui.quiet:
            self.ui.write("%d:%s\n" % (rev, short(changenode)))
            return

        log = self.repo.changelog
        changes = log.read(changenode)
        date = util.datestr(changes[2])
        extra = changes[5]
        branch = extra.get("branch")

        hexfunc = self.ui.debugflag and hex or short

        parents = [(p, hexfunc(log.node(p)))
                   for p in self._meaningful_parentrevs(log, rev)]

        self.ui.write(_("changeset:   %d:%s\n") % (rev, hexfunc(changenode)))

        # don't show the default branch name
        if branch != 'default':
            branch = encoding.tolocal(branch)
            self.ui.write(_("branch:      %s\n") % branch)
        for tag in self.repo.nodetags(changenode):
            self.ui.write(_("tag:         %s\n") % tag)
        for parent in parents:
            self.ui.write(_("parent:      %d:%s\n") % parent)

        if self.ui.debugflag:
            self.ui.write(_("manifest:    %d:%s\n") %
                          (self.repo.manifest.rev(changes[0]), hex(changes[0])))
        self.ui.write(_("user:        %s\n") % changes[1])
        self.ui.write(_("date:        %s\n") % date)

        if self.ui.debugflag:
            files = self.repo.status(log.parents(changenode)[0], changenode)[:3]
            for key, value in zip([_("files:"), _("files+:"), _("files-:")],
                                  files):
                if value:
                    self.ui.write("%-12s %s\n" % (key, " ".join(value)))
        elif changes[3] and self.ui.verbose:
            self.ui.write(_("files:       %s\n") % " ".join(changes[3]))
        if copies and self.ui.verbose:
            copies = ['%s (%s)' % c for c in copies]
            self.ui.write(_("copies:      %s\n") % ' '.join(copies))

        if extra and self.ui.debugflag:
            for key, value in sorted(extra.items()):
                self.ui.write(_("extra:       %s=%s\n")
                              % (key, value.encode('string_escape')))

        description = changes[4].strip()
        if description:
            if self.ui.verbose:
                self.ui.write(_("description:\n"))
                self.ui.write(description)
                self.ui.write("\n\n")
            else:
                self.ui.write(_("summary:     %s\n") %
                              description.splitlines()[0])
        self.ui.write("\n")

        self.showpatch(changenode)

    def showpatch(self, node):
        if self.patch:
            prev = self.repo.changelog.parents(node)[0]
            chunks = patch.diff(self.repo, prev, node, match=self.patch,
                                opts=patch.diffopts(self.ui, self.diffopts))
            for chunk in chunks:
                self.ui.write(chunk)
            self.ui.write("\n")

    def _meaningful_parentrevs(self, log, rev):
        """Return list of meaningful (or all if debug) parentrevs for rev.

        For merges (two non-nullrev revisions) both parents are meaningful.
        Otherwise the first parent revision is considered meaningful if it
        is not the preceding revision.
        """
        parents = log.parentrevs(rev)
        if not self.ui.debugflag and parents[1] == nullrev:
            if parents[0] >= rev - 1:
                parents = []
            else:
                parents = [parents[0]]
        return parents


class changeset_templater(changeset_printer):
    '''format changeset information.'''

    def __init__(self, ui, repo, patch, diffopts, mapfile, buffered):
        changeset_printer.__init__(self, ui, repo, patch, diffopts, buffered)
        formatnode = ui.debugflag and (lambda x: x) or (lambda x: x[:12])
        self.t = templater.templater(mapfile, {'formatnode': formatnode},
                                     cache={
                                         'parent': '{rev}:{node|formatnode} ',
                                         'manifest': '{rev}:{node|formatnode}',
                                         'filecopy': '{name} ({source})'})

    def use_template(self, t):
        '''set template string to use'''
        self.t.cache['changeset'] = t

    def _meaningful_parentrevs(self, ctx):
        """Return list of meaningful (or all if debug) parentrevs for rev.
        """
        parents = ctx.parents()
        if len(parents) > 1:
            return parents
        if self.ui.debugflag:
            return [parents[0], self.repo['null']]
        if parents[0].rev() >= ctx.rev() - 1:
            return []
        return parents

    def _show(self, ctx, copies, props):
        '''show a single changeset or file revision'''

        def showlist(name, values, plural=None, **args):
            '''expand set of values.
            name is name of key in template map.
            values is list of strings or dicts.
            plural is plural of name, if not simply name + 's'.

            expansion works like this, given name 'foo'.

            if values is empty, expand 'no_foos'.

            if 'foo' not in template map, return values as a string,
            joined by space.

            expand 'start_foos'.

            for each value, expand 'foo'. if 'last_foo' in template
            map, expand it instead of 'foo' for last key.

            expand 'end_foos'.
            '''
            if plural: names = plural
            else: names = name + 's'
            if not values:
                noname = 'no_' + names
                if noname in self.t:
                    yield self.t(noname, **args)
                return
            if name not in self.t:
                if isinstance(values[0], str):
                    yield ' '.join(values)
                else:
                    for v in values:
                        yield dict(v, **args)
                return
            startname = 'start_' + names
            if startname in self.t:
                yield self.t(startname, **args)
            vargs = args.copy()
            def one(v, tag=name):
                try:
                    vargs.update(v)
                except (AttributeError, ValueError):
                    try:
                        for a, b in v:
                            vargs[a] = b
                    except ValueError:
                        vargs[name] = v
                return self.t(tag, **vargs)
            lastname = 'last_' + name
            if lastname in self.t:
                last = values.pop()
            else:
                last = None
            for v in values:
                yield one(v)
            if last is not None:
                yield one(last, tag=lastname)
            endname = 'end_' + names
            if endname in self.t:
                yield self.t(endname, **args)

        def showbranches(**args):
            branch = ctx.branch()
            if branch != 'default':
                branch = encoding.tolocal(branch)
                return showlist('branch', [branch], plural='branches', **args)

        def showparents(**args):
            parents = [[('rev', p.rev()), ('node', p.hex())]
                       for p in self._meaningful_parentrevs(ctx)]
            return showlist('parent', parents, **args)

        def showtags(**args):
            return showlist('tag', ctx.tags(), **args)

        def showextras(**args):
            for key, value in sorted(ctx.extra().items()):
                args = args.copy()
                args.update(dict(key=key, value=value))
                yield self.t('extra', **args)

        def showcopies(**args):
            c = [{'name': x[0], 'source': x[1]} for x in copies]
            return showlist('file_copy', c, plural='file_copies', **args)

        files = []
        def getfiles():
            if not files:
                files[:] = self.repo.status(ctx.parents()[0].node(),
                                            ctx.node())[:3]
            return files
        def showfiles(**args):
            return showlist('file', ctx.files(), **args)
        def showmods(**args):
            return showlist('file_mod', getfiles()[0], **args)
        def showadds(**args):
            return showlist('file_add', getfiles()[1], **args)
        def showdels(**args):
            return showlist('file_del', getfiles()[2], **args)
        def showmanifest(**args):
            args = args.copy()
            args.update(dict(rev=self.repo.manifest.rev(ctx.changeset()[0]),
                             node=hex(ctx.changeset()[0])))
            return self.t('manifest', **args)

        def showdiffstat(**args):
            diff = patch.diff(self.repo, ctx.parents()[0].node(), ctx.node())
            files, adds, removes = 0, 0, 0
            for i in patch.diffstatdata(util.iterlines(diff)):
                files += 1
                adds += i[1]
                removes += i[2]
            return '%s: +%s/-%s' % (files, adds, removes)

        defprops = {
            'author': ctx.user(),
            'branches': showbranches,
            'date': ctx.date(),
            'desc': ctx.description().strip(),
            'file_adds': showadds,
            'file_dels': showdels,
            'file_mods': showmods,
            'files': showfiles,
            'file_copies': showcopies,
            'manifest': showmanifest,
            'node': ctx.hex(),
            'parents': showparents,
            'rev': ctx.rev(),
            'tags': showtags,
            'extras': showextras,
            'diffstat': showdiffstat,
            }
        props = props.copy()
        props.update(defprops)

        # find correct templates for current mode

        tmplmodes = [
            (True, None),
            (self.ui.verbose, 'verbose'),
            (self.ui.quiet, 'quiet'),
            (self.ui.debugflag, 'debug'),
        ]

        types = {'header': '', 'changeset': 'changeset'}
        for mode, postfix  in tmplmodes:
            for type in types:
                cur = postfix and ('%s_%s' % (type, postfix)) or type
                if mode and cur in self.t:
                    types[type] = cur

        try:

            # write header
            if types['header']:
                h = templater.stringify(self.t(types['header'], **props))
                if self.buffered:
                    self.header[ctx.rev()] = h
                else:
                    self.ui.write(h)

            # write changeset metadata, then patch if requested
            key = types['changeset']
            self.ui.write(templater.stringify(self.t(key, **props)))
            self.showpatch(ctx.node())

        except KeyError, inst:
            msg = _("%s: no key named '%s'")
            raise util.Abort(msg % (self.t.mapfile, inst.args[0]))
        except SyntaxError, inst:
            raise util.Abort(_('%s: %s') % (self.t.mapfile, inst.args[0]))

def show_changeset(ui, repo, opts, buffered=False, matchfn=False):
    """show one changeset using template or regular display.

    Display format will be the first non-empty hit of:
    1. option 'template'
    2. option 'style'
    3. [ui] setting 'logtemplate'
    4. [ui] setting 'style'
    If all of these values are either the unset or the empty string,
    regular display via changeset_printer() is done.
    """
    # options
    patch = False
    if opts.get('patch'):
        patch = matchfn or matchall(repo)

    tmpl = opts.get('template')
    style = None
    if tmpl:
        tmpl = templater.parsestring(tmpl, quoted=False)
    else:
        style = opts.get('style')

    # ui settings
    if not (tmpl or style):
        tmpl = ui.config('ui', 'logtemplate')
        if tmpl:
            tmpl = templater.parsestring(tmpl)
        else:
            style = ui.config('ui', 'style')

    if not (tmpl or style):
        return changeset_printer(ui, repo, patch, opts, buffered)

    mapfile = None
    if style and not tmpl:
        mapfile = style
        if not os.path.split(mapfile)[0]:
            mapname = (templater.templatepath('map-cmdline.' + mapfile)
                       or templater.templatepath(mapfile))
            if mapname: mapfile = mapname

    try:
        t = changeset_templater(ui, repo, patch, opts, mapfile, buffered)
    except SyntaxError, inst:
        raise util.Abort(inst.args[0])
    if tmpl: t.use_template(tmpl)
    return t

def finddate(ui, repo, date):
    """Find the tipmost changeset that matches the given date spec"""
    df = util.matchdate(date)
    get = util.cachefunc(lambda r: repo[r].changeset())
    changeiter, matchfn = walkchangerevs(ui, repo, [], get, {'rev':None})
    results = {}
    for st, rev, fns in changeiter:
        if st == 'add':
            d = get(rev)[2]
            if df(d[0]):
                results[rev] = d
        elif st == 'iter':
            if rev in results:
                ui.status(_("Found revision %s from %s\n") %
                          (rev, util.datestr(results[rev])))
                return str(rev)

    raise util.Abort(_("revision matching date not found"))

def walkchangerevs(ui, repo, pats, change, opts):
    '''Iterate over files and the revs in which they changed.

    Callers most commonly need to iterate backwards over the history
    in which they are interested. Doing so has awful (quadratic-looking)
    performance, so we use iterators in a "windowed" way.

    We walk a window of revisions in the desired order.  Within the
    window, we first walk forwards to gather data, then in the desired
    order (usually backwards) to display it.

    This function returns an (iterator, matchfn) tuple. The iterator
    yields 3-tuples. They will be of one of the following forms:

    "window", incrementing, lastrev: stepping through a window,
    positive if walking forwards through revs, last rev in the
    sequence iterated over - use to reset state for the current window

    "add", rev, fns: out-of-order traversal of the given filenames
    fns, which changed during revision rev - use to gather data for
    possible display

    "iter", rev, None: in-order traversal of the revs earlier iterated
    over with "add" - use to display data'''

    def increasing_windows(start, end, windowsize=8, sizelimit=512):
        if start < end:
            while start < end:
                yield start, min(windowsize, end-start)
                start += windowsize
                if windowsize < sizelimit:
                    windowsize *= 2
        else:
            while start > end:
                yield start, min(windowsize, start-end-1)
                start -= windowsize
                if windowsize < sizelimit:
                    windowsize *= 2

    m = match(repo, pats, opts)
    follow = opts.get('follow') or opts.get('follow_first')

    if not len(repo):
        return [], m

    if follow:
        defrange = '%s:0' % repo['.'].rev()
    else:
        defrange = '-1:0'
    revs = revrange(repo, opts['rev'] or [defrange])
    wanted = set()
    slowpath = m.anypats() or (m.files() and opts.get('removed'))
    fncache = {}

    if not slowpath and not m.files():
        # No files, no patterns.  Display all revs.
        wanted = set(revs)
    copies = []
    if not slowpath:
        # Only files, no patterns.  Check the history of each file.
        def filerevgen(filelog, node):
            cl_count = len(repo)
            if node is None:
                last = len(filelog) - 1
            else:
                last = filelog.rev(node)
            for i, window in increasing_windows(last, nullrev):
                revs = []
                for j in xrange(i - window, i + 1):
                    n = filelog.node(j)
                    revs.append((filelog.linkrev(j),
                                 follow and filelog.renamed(n)))
                for rev in reversed(revs):
                    # only yield rev for which we have the changelog, it can
                    # happen while doing "hg log" during a pull or commit
                    if rev[0] < cl_count:
                        yield rev
        def iterfiles():
            for filename in m.files():
                yield filename, None
            for filename_node in copies:
                yield filename_node
        minrev, maxrev = min(revs), max(revs)
        for file_, node in iterfiles():
            filelog = repo.file(file_)
            if not len(filelog):
                if node is None:
                    # A zero count may be a directory or deleted file, so
                    # try to find matching entries on the slow path.
                    if follow:
                        raise util.Abort(_('cannot follow nonexistent file: "%s"') % file_)
                    slowpath = True
                    break
                else:
                    ui.warn(_('%s:%s copy source revision cannot be found!\n')
                            % (file_, short(node)))
                    continue
            for rev, copied in filerevgen(filelog, node):
                if rev <= maxrev:
                    if rev < minrev:
                        break
                    fncache.setdefault(rev, [])
                    fncache[rev].append(file_)
                    wanted.add(rev)
                    if follow and copied:
                        copies.append(copied)
    if slowpath:
        if follow:
            raise util.Abort(_('can only follow copies/renames for explicit '
                               'filenames'))

        # The slow path checks files modified in every changeset.
        def changerevgen():
            for i, window in increasing_windows(len(repo) - 1, nullrev):
                for j in xrange(i - window, i + 1):
                    yield j, change(j)[3]

        for rev, changefiles in changerevgen():
            matches = filter(m, changefiles)
            if matches:
                fncache[rev] = matches
                wanted.add(rev)

    class followfilter(object):
        def __init__(self, onlyfirst=False):
            self.startrev = nullrev
            self.roots = []
            self.onlyfirst = onlyfirst

        def match(self, rev):
            def realparents(rev):
                if self.onlyfirst:
                    return repo.changelog.parentrevs(rev)[0:1]
                else:
                    return filter(lambda x: x != nullrev,
                                  repo.changelog.parentrevs(rev))

            if self.startrev == nullrev:
                self.startrev = rev
                return True

            if rev > self.startrev:
                # forward: all descendants
                if not self.roots:
                    self.roots.append(self.startrev)
                for parent in realparents(rev):
                    if parent in self.roots:
                        self.roots.append(rev)
                        return True
            else:
                # backwards: all parents
                if not self.roots:
                    self.roots.extend(realparents(self.startrev))
                if rev in self.roots:
                    self.roots.remove(rev)
                    self.roots.extend(realparents(rev))
                    return True

            return False

    # it might be worthwhile to do this in the iterator if the rev range
    # is descending and the prune args are all within that range
    for rev in opts.get('prune', ()):
        rev = repo.changelog.rev(repo.lookup(rev))
        ff = followfilter()
        stop = min(revs[0], revs[-1])
        for x in xrange(rev, stop-1, -1):
            if ff.match(x):
                wanted.discard(x)

    def iterate():
        if follow and not m.files():
            ff = followfilter(onlyfirst=opts.get('follow_first'))
            def want(rev):
                return ff.match(rev) and rev in wanted
        else:
            def want(rev):
                return rev in wanted

        for i, window in increasing_windows(0, len(revs)):
            yield 'window', revs[0] < revs[-1], revs[-1]
            nrevs = [rev for rev in revs[i:i+window] if want(rev)]
            for rev in sorted(nrevs):
                fns = fncache.get(rev)
                if not fns:
                    def fns_generator():
                        for f in change(rev)[3]:
                            if m(f):
                                yield f
                    fns = fns_generator()
                yield 'add', rev, fns
            for rev in nrevs:
                yield 'iter', rev, None
    return iterate(), m

def commit(ui, repo, commitfunc, pats, opts):
    '''commit the specified files or all outstanding changes'''
    date = opts.get('date')
    if date:
        opts['date'] = util.parsedate(date)
    message = logmessage(opts)

    # extract addremove carefully -- this function can be called from a command
    # that doesn't support addremove
    if opts.get('addremove'):
        addremove(repo, pats, opts)

    return commitfunc(ui, repo, message, match(repo, pats, opts), opts)

def commiteditor(repo, ctx, subs):
    if ctx.description():
        return ctx.description()
    return commitforceeditor(repo, ctx, subs)

def commitforceeditor(repo, ctx, subs):
    edittext = []
    modified, added, removed = ctx.modified(), ctx.added(), ctx.removed()
    if ctx.description():
        edittext.append(ctx.description())
    edittext.append("")
    edittext.append("") # Empty line between message and comments.
    edittext.append(_("HG: Enter commit message."
                      "  Lines beginning with 'HG:' are removed."))
    edittext.append(_("HG: Leave message empty to abort commit."))
    edittext.append("HG: --")
    edittext.append(_("HG: user: %s") % ctx.user())
    if ctx.p2():
        edittext.append(_("HG: branch merge"))
    if ctx.branch():
        edittext.append(_("HG: branch '%s'")
                        % encoding.tolocal(ctx.branch()))
    edittext.extend([_("HG: subrepo %s") % s for s in subs])
    edittext.extend([_("HG: added %s") % f for f in added])
    edittext.extend([_("HG: changed %s") % f for f in modified])
    edittext.extend([_("HG: removed %s") % f for f in removed])
    if not added and not modified and not removed:
        edittext.append(_("HG: no files changed"))
    edittext.append("")
    # run editor in the repository root
    olddir = os.getcwd()
    os.chdir(repo.root)
    text = repo.ui.edit("\n".join(edittext), ctx.user())
    text = re.sub("(?m)^HG:.*\n", "", text)
    os.chdir(olddir)

    if not text.strip():
        raise util.Abort(_("empty commit message"))

    return text
