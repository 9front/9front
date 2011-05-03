#
# Mercurial built-in replacement for cvsps.
#
# Copyright 2008, Frank Kingswood <frank@kingswood-consulting.co.uk>
#
# This software may be used and distributed according to the terms of the
# GNU General Public License version 2, incorporated herein by reference.

import os
import re
import cPickle as pickle
from mercurial import util
from mercurial.i18n import _

class logentry(object):
    '''Class logentry has the following attributes:
        .author    - author name as CVS knows it
        .branch    - name of branch this revision is on
        .branches  - revision tuple of branches starting at this revision
        .comment   - commit message
        .date      - the commit date as a (time, tz) tuple
        .dead      - true if file revision is dead
        .file      - Name of file
        .lines     - a tuple (+lines, -lines) or None
        .parent    - Previous revision of this entry
        .rcs       - name of file as returned from CVS
        .revision  - revision number as tuple
        .tags      - list of tags on the file
        .synthetic - is this a synthetic "file ... added on ..." revision?
        .mergepoint- the branch that has been merged from
                     (if present in rlog output)
        .branchpoints- the branches that start at the current entry
    '''
    def __init__(self, **entries):
        self.__dict__.update(entries)

    def __repr__(self):
        return "<%s at 0x%x: %s %s>" % (self.__class__.__name__,
                                        id(self),
                                        self.file,
                                        ".".join(map(str, self.revision)))

class logerror(Exception):
    pass

def getrepopath(cvspath):
    """Return the repository path from a CVS path.

    >>> getrepopath('/foo/bar')
    '/foo/bar'
    >>> getrepopath('c:/foo/bar')
    'c:/foo/bar'
    >>> getrepopath(':pserver:10/foo/bar')
    '/foo/bar'
    >>> getrepopath(':pserver:10c:/foo/bar')
    '/foo/bar'
    >>> getrepopath(':pserver:/foo/bar')
    '/foo/bar'
    >>> getrepopath(':pserver:c:/foo/bar')
    'c:/foo/bar'
    >>> getrepopath(':pserver:truc@foo.bar:/foo/bar')
    '/foo/bar'
    >>> getrepopath(':pserver:truc@foo.bar:c:/foo/bar')
    'c:/foo/bar'
    """
    # According to CVS manual, CVS paths are expressed like:
    # [:method:][[user][:password]@]hostname[:[port]]/path/to/repository
    #
    # Unfortunately, Windows absolute paths start with a drive letter
    # like 'c:' making it harder to parse. Here we assume that drive
    # letters are only one character long and any CVS component before
    # the repository path is at least 2 characters long, and use this
    # to disambiguate.
    parts = cvspath.split(':')
    if len(parts) == 1:
        return parts[0]
    # Here there is an ambiguous case if we have a port number
    # immediately followed by a Windows driver letter. We assume this
    # never happens and decide it must be CVS path component,
    # therefore ignoring it.
    if len(parts[-2]) > 1:
        return parts[-1].lstrip('0123456789')
    return parts[-2] + ':' + parts[-1]

def createlog(ui, directory=None, root="", rlog=True, cache=None):
    '''Collect the CVS rlog'''

    # Because we store many duplicate commit log messages, reusing strings
    # saves a lot of memory and pickle storage space.
    _scache = {}
    def scache(s):
        "return a shared version of a string"
        return _scache.setdefault(s, s)

    ui.status(_('collecting CVS rlog\n'))

    log = []      # list of logentry objects containing the CVS state

    # patterns to match in CVS (r)log output, by state of use
    re_00 = re.compile('RCS file: (.+)$')
    re_01 = re.compile('cvs \\[r?log aborted\\]: (.+)$')
    re_02 = re.compile('cvs (r?log|server): (.+)\n$')
    re_03 = re.compile("(Cannot access.+CVSROOT)|"
                       "(can't create temporary directory.+)$")
    re_10 = re.compile('Working file: (.+)$')
    re_20 = re.compile('symbolic names:')
    re_30 = re.compile('\t(.+): ([\\d.]+)$')
    re_31 = re.compile('----------------------------$')
    re_32 = re.compile('======================================='
                       '======================================$')
    re_50 = re.compile('revision ([\\d.]+)(\s+locked by:\s+.+;)?$')
    re_60 = re.compile(r'date:\s+(.+);\s+author:\s+(.+);\s+state:\s+(.+?);'
                       r'(\s+lines:\s+(\+\d+)?\s+(-\d+)?;)?'
                       r'(.*mergepoint:\s+([^;]+);)?')
    re_70 = re.compile('branches: (.+);$')

    file_added_re = re.compile(r'file [^/]+ was (initially )?added on branch')

    prefix = ''   # leading path to strip of what we get from CVS

    if directory is None:
        # Current working directory

        # Get the real directory in the repository
        try:
            prefix = open(os.path.join('CVS','Repository')).read().strip()
            if prefix == ".":
                prefix = ""
            directory = prefix
        except IOError:
            raise logerror('Not a CVS sandbox')

        if prefix and not prefix.endswith(os.sep):
            prefix += os.sep

        # Use the Root file in the sandbox, if it exists
        try:
            root = open(os.path.join('CVS','Root')).read().strip()
        except IOError:
            pass

    if not root:
        root = os.environ.get('CVSROOT', '')

    # read log cache if one exists
    oldlog = []
    date = None

    if cache:
        cachedir = os.path.expanduser('~/.hg.cvsps')
        if not os.path.exists(cachedir):
            os.mkdir(cachedir)

        # The cvsps cache pickle needs a uniquified name, based on the
        # repository location. The address may have all sort of nasties
        # in it, slashes, colons and such. So here we take just the
        # alphanumerics, concatenated in a way that does not mix up the
        # various components, so that
        #    :pserver:user@server:/path
        # and
        #    /pserver/user/server/path
        # are mapped to different cache file names.
        cachefile = root.split(":") + [directory, "cache"]
        cachefile = ['-'.join(re.findall(r'\w+', s)) for s in cachefile if s]
        cachefile = os.path.join(cachedir,
                                 '.'.join([s for s in cachefile if s]))

    if cache == 'update':
        try:
            ui.note(_('reading cvs log cache %s\n') % cachefile)
            oldlog = pickle.load(open(cachefile))
            ui.note(_('cache has %d log entries\n') % len(oldlog))
        except Exception, e:
            ui.note(_('error reading cache: %r\n') % e)

        if oldlog:
            date = oldlog[-1].date    # last commit date as a (time,tz) tuple
            date = util.datestr(date, '%Y/%m/%d %H:%M:%S %1%2')

    # build the CVS commandline
    cmd = ['cvs', '-q']
    if root:
        cmd.append('-d%s' % root)
        p = util.normpath(getrepopath(root))
        if not p.endswith('/'):
            p += '/'
        prefix = p + util.normpath(prefix)
    cmd.append(['log', 'rlog'][rlog])
    if date:
        # no space between option and date string
        cmd.append('-d>%s' % date)
    cmd.append(directory)

    # state machine begins here
    tags = {}     # dictionary of revisions on current file with their tags
    branchmap = {} # mapping between branch names and revision numbers
    state = 0
    store = False # set when a new record can be appended

    cmd = [util.shellquote(arg) for arg in cmd]
    ui.note(_("running %s\n") % (' '.join(cmd)))
    ui.debug(_("prefix=%r directory=%r root=%r\n") % (prefix, directory, root))

    pfp = util.popen(' '.join(cmd))
    peek = pfp.readline()
    while True:
        line = peek
        if line == '':
            break
        peek = pfp.readline()
        if line.endswith('\n'):
            line = line[:-1]
        #ui.debug('state=%d line=%r\n' % (state, line))

        if state == 0:
            # initial state, consume input until we see 'RCS file'
            match = re_00.match(line)
            if match:
                rcs = match.group(1)
                tags = {}
                if rlog:
                    filename = util.normpath(rcs[:-2])
                    if filename.startswith(prefix):
                        filename = filename[len(prefix):]
                    if filename.startswith('/'):
                        filename = filename[1:]
                    if filename.startswith('Attic/'):
                        filename = filename[6:]
                    else:
                        filename = filename.replace('/Attic/', '/')
                    state = 2
                    continue
                state = 1
                continue
            match = re_01.match(line)
            if match:
                raise Exception(match.group(1))
            match = re_02.match(line)
            if match:
                raise Exception(match.group(2))
            if re_03.match(line):
                raise Exception(line)

        elif state == 1:
            # expect 'Working file' (only when using log instead of rlog)
            match = re_10.match(line)
            assert match, _('RCS file must be followed by working file')
            filename = util.normpath(match.group(1))
            state = 2

        elif state == 2:
            # expect 'symbolic names'
            if re_20.match(line):
                branchmap = {}
                state = 3

        elif state == 3:
            # read the symbolic names and store as tags
            match = re_30.match(line)
            if match:
                rev = [int(x) for x in match.group(2).split('.')]

                # Convert magic branch number to an odd-numbered one
                revn = len(rev)
                if revn > 3 and (revn % 2) == 0 and rev[-2] == 0:
                    rev = rev[:-2] + rev[-1:]
                rev = tuple(rev)

                if rev not in tags:
                    tags[rev] = []
                tags[rev].append(match.group(1))
                branchmap[match.group(1)] = match.group(2)

            elif re_31.match(line):
                state = 5
            elif re_32.match(line):
                state = 0

        elif state == 4:
            # expecting '------' separator before first revision
            if re_31.match(line):
                state = 5
            else:
                assert not re_32.match(line), _('must have at least '
                                                'some revisions')

        elif state == 5:
            # expecting revision number and possibly (ignored) lock indication
            # we create the logentry here from values stored in states 0 to 4,
            # as this state is re-entered for subsequent revisions of a file.
            match = re_50.match(line)
            assert match, _('expected revision number')
            e = logentry(rcs=scache(rcs), file=scache(filename),
                    revision=tuple([int(x) for x in match.group(1).split('.')]),
                    branches=[], parent=None,
                    synthetic=False)
            state = 6

        elif state == 6:
            # expecting date, author, state, lines changed
            match = re_60.match(line)
            assert match, _('revision must be followed by date line')
            d = match.group(1)
            if d[2] == '/':
                # Y2K
                d = '19' + d

            if len(d.split()) != 3:
                # cvs log dates always in GMT
                d = d + ' UTC'
            e.date = util.parsedate(d, ['%y/%m/%d %H:%M:%S',
                                        '%Y/%m/%d %H:%M:%S',
                                        '%Y-%m-%d %H:%M:%S'])
            e.author = scache(match.group(2))
            e.dead = match.group(3).lower() == 'dead'

            if match.group(5):
                if match.group(6):
                    e.lines = (int(match.group(5)), int(match.group(6)))
                else:
                    e.lines = (int(match.group(5)), 0)
            elif match.group(6):
                e.lines = (0, int(match.group(6)))
            else:
                e.lines = None

            if match.group(7): # cvsnt mergepoint
                myrev = match.group(8).split('.')
                if len(myrev) == 2: # head
                    e.mergepoint = 'HEAD'
                else:
                    myrev = '.'.join(myrev[:-2] + ['0', myrev[-2]])
                    branches = [b for b in branchmap if branchmap[b] == myrev]
                    assert len(branches) == 1, 'unknown branch: %s' % e.mergepoint
                    e.mergepoint = branches[0]
            else:
                e.mergepoint = None
            e.comment = []
            state = 7

        elif state == 7:
            # read the revision numbers of branches that start at this revision
            # or store the commit log message otherwise
            m = re_70.match(line)
            if m:
                e.branches = [tuple([int(y) for y in x.strip().split('.')])
                                for x in m.group(1).split(';')]
                state = 8
            elif re_31.match(line) and re_50.match(peek):
                state = 5
                store = True
            elif re_32.match(line):
                state = 0
                store = True
            else:
                e.comment.append(line)

        elif state == 8:
            # store commit log message
            if re_31.match(line):
                state = 5
                store = True
            elif re_32.match(line):
                state = 0
                store = True
            else:
                e.comment.append(line)

        # When a file is added on a branch B1, CVS creates a synthetic
        # dead trunk revision 1.1 so that the branch has a root.
        # Likewise, if you merge such a file to a later branch B2 (one
        # that already existed when the file was added on B1), CVS
        # creates a synthetic dead revision 1.1.x.1 on B2.  Don't drop
        # these revisions now, but mark them synthetic so
        # createchangeset() can take care of them.
        if (store and
              e.dead and
              e.revision[-1] == 1 and      # 1.1 or 1.1.x.1
              len(e.comment) == 1 and
              file_added_re.match(e.comment[0])):
            ui.debug(_('found synthetic revision in %s: %r\n')
                     % (e.rcs, e.comment[0]))
            e.synthetic = True

        if store:
            # clean up the results and save in the log.
            store = False
            e.tags = sorted([scache(x) for x in tags.get(e.revision, [])])
            e.comment = scache('\n'.join(e.comment))

            revn = len(e.revision)
            if revn > 3 and (revn % 2) == 0:
                e.branch = tags.get(e.revision[:-1], [None])[0]
            else:
                e.branch = None

            # find the branches starting from this revision
            branchpoints = set()
            for branch, revision in branchmap.iteritems():
                revparts = tuple([int(i) for i in revision.split('.')])
                if revparts[-2] == 0 and revparts[-1] % 2 == 0:
                    # normal branch
                    if revparts[:-2] == e.revision:
                        branchpoints.add(branch)
                elif revparts == (1,1,1): # vendor branch
                    if revparts in e.branches:
                        branchpoints.add(branch)
            e.branchpoints = branchpoints

            log.append(e)

            if len(log) % 100 == 0:
                ui.status(util.ellipsis('%d %s' % (len(log), e.file), 80)+'\n')

    log.sort(key=lambda x: (x.rcs, x.revision))

    # find parent revisions of individual files
    versions = {}
    for e in log:
        branch = e.revision[:-1]
        p = versions.get((e.rcs, branch), None)
        if p is None:
            p = e.revision[:-2]
        e.parent = p
        versions[(e.rcs, branch)] = e.revision

    # update the log cache
    if cache:
        if log:
            # join up the old and new logs
            log.sort(key=lambda x: x.date)

            if oldlog and oldlog[-1].date >= log[0].date:
                raise logerror('Log cache overlaps with new log entries,'
                               ' re-run without cache.')

            log = oldlog + log

            # write the new cachefile
            ui.note(_('writing cvs log cache %s\n') % cachefile)
            pickle.dump(log, open(cachefile, 'w'))
        else:
            log = oldlog

    ui.status(_('%d log entries\n') % len(log))

    return log


class changeset(object):
    '''Class changeset has the following attributes:
        .id        - integer identifying this changeset (list index)
        .author    - author name as CVS knows it
        .branch    - name of branch this changeset is on, or None
        .comment   - commit message
        .date      - the commit date as a (time,tz) tuple
        .entries   - list of logentry objects in this changeset
        .parents   - list of one or two parent changesets
        .tags      - list of tags on this changeset
        .synthetic - from synthetic revision "file ... added on branch ..."
        .mergepoint- the branch that has been merged from
                     (if present in rlog output)
        .branchpoints- the branches that start at the current entry
    '''
    def __init__(self, **entries):
        self.__dict__.update(entries)

    def __repr__(self):
        return "<%s at 0x%x: %s>" % (self.__class__.__name__,
                                     id(self),
                                     getattr(self, 'id', "(no id)"))

def createchangeset(ui, log, fuzz=60, mergefrom=None, mergeto=None):
    '''Convert log into changesets.'''

    ui.status(_('creating changesets\n'))

    # Merge changesets

    log.sort(key=lambda x: (x.comment, x.author, x.branch, x.date))

    changesets = []
    files = set()
    c = None
    for i, e in enumerate(log):

        # Check if log entry belongs to the current changeset or not.

        # Since CVS is file centric, two different file revisions with
        # different branchpoints should be treated as belonging to two
        # different changesets (and the ordering is important and not
        # honoured by cvsps at this point).
        #
        # Consider the following case:
        # foo 1.1 branchpoints: [MYBRANCH]
        # bar 1.1 branchpoints: [MYBRANCH, MYBRANCH2]
        #
        # Here foo is part only of MYBRANCH, but not MYBRANCH2, e.g. a
        # later version of foo may be in MYBRANCH2, so foo should be the
        # first changeset and bar the next and MYBRANCH and MYBRANCH2
        # should both start off of the bar changeset. No provisions are
        # made to ensure that this is, in fact, what happens.
        if not (c and
                  e.comment == c.comment and
                  e.author == c.author and
                  e.branch == c.branch and
                  (not hasattr(e, 'branchpoints') or
                    not hasattr (c, 'branchpoints') or
                    e.branchpoints == c.branchpoints) and
                  ((c.date[0] + c.date[1]) <=
                   (e.date[0] + e.date[1]) <=
                   (c.date[0] + c.date[1]) + fuzz) and
                  e.file not in files):
            c = changeset(comment=e.comment, author=e.author,
                          branch=e.branch, date=e.date, entries=[],
                          mergepoint=getattr(e, 'mergepoint', None),
                          branchpoints=getattr(e, 'branchpoints', set()))
            changesets.append(c)
            files = set()
            if len(changesets) % 100 == 0:
                t = '%d %s' % (len(changesets), repr(e.comment)[1:-1])
                ui.status(util.ellipsis(t, 80) + '\n')

        c.entries.append(e)
        files.add(e.file)
        c.date = e.date       # changeset date is date of latest commit in it

    # Mark synthetic changesets

    for c in changesets:
        # Synthetic revisions always get their own changeset, because
        # the log message includes the filename.  E.g. if you add file3
        # and file4 on a branch, you get four log entries and three
        # changesets:
        #   "File file3 was added on branch ..." (synthetic, 1 entry)
        #   "File file4 was added on branch ..." (synthetic, 1 entry)
        #   "Add file3 and file4 to fix ..."     (real, 2 entries)
        # Hence the check for 1 entry here.
        synth = getattr(c.entries[0], 'synthetic', None)
        c.synthetic = (len(c.entries) == 1 and synth)

    # Sort files in each changeset

    for c in changesets:
        def pathcompare(l, r):
            'Mimic cvsps sorting order'
            l = l.split('/')
            r = r.split('/')
            nl = len(l)
            nr = len(r)
            n = min(nl, nr)
            for i in range(n):
                if i + 1 == nl and nl < nr:
                    return -1
                elif i + 1 == nr and nl > nr:
                    return +1
                elif l[i] < r[i]:
                    return -1
                elif l[i] > r[i]:
                    return +1
            return 0
        def entitycompare(l, r):
            return pathcompare(l.file, r.file)

        c.entries.sort(entitycompare)

    # Sort changesets by date

    def cscmp(l, r):
        d = sum(l.date) - sum(r.date)
        if d:
            return d

        # detect vendor branches and initial commits on a branch
        le = {}
        for e in l.entries:
            le[e.rcs] = e.revision
        re = {}
        for e in r.entries:
            re[e.rcs] = e.revision

        d = 0
        for e in l.entries:
            if re.get(e.rcs, None) == e.parent:
                assert not d
                d = 1
                break

        for e in r.entries:
            if le.get(e.rcs, None) == e.parent:
                assert not d
                d = -1
                break

        return d

    changesets.sort(cscmp)

    # Collect tags

    globaltags = {}
    for c in changesets:
        for e in c.entries:
            for tag in e.tags:
                # remember which is the latest changeset to have this tag
                globaltags[tag] = c

    for c in changesets:
        tags = set()
        for e in c.entries:
            tags.update(e.tags)
        # remember tags only if this is the latest changeset to have it
        c.tags = sorted(tag for tag in tags if globaltags[tag] is c)

    # Find parent changesets, handle {{mergetobranch BRANCHNAME}}
    # by inserting dummy changesets with two parents, and handle
    # {{mergefrombranch BRANCHNAME}} by setting two parents.

    if mergeto is None:
        mergeto = r'{{mergetobranch ([-\w]+)}}'
    if mergeto:
        mergeto = re.compile(mergeto)

    if mergefrom is None:
        mergefrom = r'{{mergefrombranch ([-\w]+)}}'
    if mergefrom:
        mergefrom = re.compile(mergefrom)

    versions = {}    # changeset index where we saw any particular file version
    branches = {}    # changeset index where we saw a branch
    n = len(changesets)
    i = 0
    while i<n:
        c = changesets[i]

        for f in c.entries:
            versions[(f.rcs, f.revision)] = i

        p = None
        if c.branch in branches:
            p = branches[c.branch]
        else:
            # first changeset on a new branch
            # the parent is a changeset with the branch in its
            # branchpoints such that it is the latest possible
            # commit without any intervening, unrelated commits.

            for candidate in xrange(i):
                if c.branch not in changesets[candidate].branchpoints:
                    if p is not None:
                        break
                    continue
                p = candidate

        c.parents = []
        if p is not None:
            p = changesets[p]

            # Ensure no changeset has a synthetic changeset as a parent.
            while p.synthetic:
                assert len(p.parents) <= 1, \
                       _('synthetic changeset cannot have multiple parents')
                if p.parents:
                    p = p.parents[0]
                else:
                    p = None
                    break

            if p is not None:
                c.parents.append(p)

        if c.mergepoint:
            if c.mergepoint == 'HEAD':
                c.mergepoint = None
            c.parents.append(changesets[branches[c.mergepoint]])

        if mergefrom:
            m = mergefrom.search(c.comment)
            if m:
                m = m.group(1)
                if m == 'HEAD':
                    m = None
                try:
                    candidate = changesets[branches[m]]
                except KeyError:
                    ui.warn(_("warning: CVS commit message references "
                              "non-existent branch %r:\n%s\n")
                            % (m, c.comment))
                if m in branches and c.branch != m and not candidate.synthetic:
                    c.parents.append(candidate)

        if mergeto:
            m = mergeto.search(c.comment)
            if m:
                try:
                    m = m.group(1)
                    if m == 'HEAD':
                        m = None
                except:
                    m = None   # if no group found then merge to HEAD
                if m in branches and c.branch != m:
                    # insert empty changeset for merge
                    cc = changeset(author=c.author, branch=m, date=c.date,
                            comment='convert-repo: CVS merge from branch %s' % c.branch,
                            entries=[], tags=[], parents=[changesets[branches[m]], c])
                    changesets.insert(i + 1, cc)
                    branches[m] = i + 1

                    # adjust our loop counters now we have inserted a new entry
                    n += 1
                    i += 2
                    continue

        branches[c.branch] = i
        i += 1

    # Drop synthetic changesets (safe now that we have ensured no other
    # changesets can have them as parents).
    i = 0
    while i < len(changesets):
        if changesets[i].synthetic:
            del changesets[i]
        else:
            i += 1

    # Number changesets

    for i, c in enumerate(changesets):
        c.id = i + 1

    ui.status(_('%d changeset entries\n') % len(changesets))

    return changesets


def debugcvsps(ui, *args, **opts):
    '''Read CVS rlog for current directory or named path in
    repository, and convert the log to changesets based on matching
    commit log entries and dates.
    '''
    if opts["new_cache"]:
        cache = "write"
    elif opts["update_cache"]:
        cache = "update"
    else:
        cache = None

    revisions = opts["revisions"]

    try:
        if args:
            log = []
            for d in args:
                log += createlog(ui, d, root=opts["root"], cache=cache)
        else:
            log = createlog(ui, root=opts["root"], cache=cache)
    except logerror, e:
        ui.write("%r\n"%e)
        return

    changesets = createchangeset(ui, log, opts["fuzz"])
    del log

    # Print changesets (optionally filtered)

    off = len(revisions)
    branches = {}    # latest version number in each branch
    ancestors = {}   # parent branch
    for cs in changesets:

        if opts["ancestors"]:
            if cs.branch not in branches and cs.parents and cs.parents[0].id:
                ancestors[cs.branch] = (changesets[cs.parents[0].id-1].branch,
                                        cs.parents[0].id)
            branches[cs.branch] = cs.id

        # limit by branches
        if opts["branches"] and (cs.branch or 'HEAD') not in opts["branches"]:
            continue

        if not off:
            # Note: trailing spaces on several lines here are needed to have
            #       bug-for-bug compatibility with cvsps.
            ui.write('---------------------\n')
            ui.write('PatchSet %d \n' % cs.id)
            ui.write('Date: %s\n' % util.datestr(cs.date,
                                                 '%Y/%m/%d %H:%M:%S %1%2'))
            ui.write('Author: %s\n' % cs.author)
            ui.write('Branch: %s\n' % (cs.branch or 'HEAD'))
            ui.write('Tag%s: %s \n' % (['', 's'][len(cs.tags)>1],
                                  ','.join(cs.tags) or '(none)'))
            branchpoints = getattr(cs, 'branchpoints', None)
            if branchpoints:
                ui.write('Branchpoints: %s \n' % ', '.join(branchpoints))
            if opts["parents"] and cs.parents:
                if len(cs.parents)>1:
                    ui.write('Parents: %s\n' % (','.join([str(p.id) for p in cs.parents])))
                else:
                    ui.write('Parent: %d\n' % cs.parents[0].id)

            if opts["ancestors"]:
                b = cs.branch
                r = []
                while b:
                    b, c = ancestors[b]
                    r.append('%s:%d:%d' % (b or "HEAD", c, branches[b]))
                if r:
                    ui.write('Ancestors: %s\n' % (','.join(r)))

            ui.write('Log:\n')
            ui.write('%s\n\n' % cs.comment)
            ui.write('Members: \n')
            for f in cs.entries:
                fn = f.file
                if fn.startswith(opts["prefix"]):
                    fn = fn[len(opts["prefix"]):]
                ui.write('\t%s:%s->%s%s \n' % (fn, '.'.join([str(x) for x in f.parent]) or 'INITIAL',
                                          '.'.join([str(x) for x in f.revision]), ['', '(DEAD)'][f.dead]))
            ui.write('\n')

        # have we seen the start tag?
        if revisions and off:
            if revisions[0] == str(cs.id) or \
                revisions[0] in cs.tags:
                off = False

        # see if we reached the end tag
        if len(revisions)>1 and not off:
            if revisions[1] == str(cs.id) or \
                revisions[1] in cs.tags:
                break
