# fetch.py - pull and merge remote changes
#
# Copyright 2006 Vadim Gelfer <vadim.gelfer@gmail.com>
#
# This software may be used and distributed according to the terms of the
# GNU General Public License version 2, incorporated herein by reference.

'''pull, update and merge in one command'''

from mercurial.i18n import _
from mercurial.node import nullid, short
from mercurial import commands, cmdutil, hg, util, url, error
from mercurial.lock import release

def fetch(ui, repo, source='default', **opts):
    '''pull changes from a remote repository, merge new changes if needed.

    This finds all changes from the repository at the specified path
    or URL and adds them to the local repository.

    If the pulled changes add a new branch head, the head is
    automatically merged, and the result of the merge is committed.
    Otherwise, the working directory is updated to include the new
    changes.

    When a merge occurs, the newly pulled changes are assumed to be
    "authoritative". The head of the new changes is used as the first
    parent, with local changes as the second. To switch the merge
    order, use --switch-parent.

    See 'hg help dates' for a list of formats valid for -d/--date.
    '''

    date = opts.get('date')
    if date:
        opts['date'] = util.parsedate(date)

    parent, p2 = repo.dirstate.parents()
    branch = repo.dirstate.branch()
    branchnode = repo.branchtags().get(branch)
    if parent != branchnode:
        raise util.Abort(_('working dir not at branch tip '
                           '(use "hg update" to check out branch tip)'))

    if p2 != nullid:
        raise util.Abort(_('outstanding uncommitted merge'))

    wlock = lock = None
    try:
        wlock = repo.wlock()
        lock = repo.lock()
        mod, add, rem, del_ = repo.status()[:4]

        if mod or add or rem:
            raise util.Abort(_('outstanding uncommitted changes'))
        if del_:
            raise util.Abort(_('working directory is missing some files'))
        bheads = repo.branchheads(branch)
        bheads = [head for head in bheads if len(repo[head].children()) == 0]
        if len(bheads) > 1:
            raise util.Abort(_('multiple heads in this branch '
                               '(use "hg heads ." and "hg merge" to merge)'))

        other = hg.repository(cmdutil.remoteui(repo, opts),
                              ui.expandpath(source))
        ui.status(_('pulling from %s\n') %
                  url.hidepassword(ui.expandpath(source)))
        revs = None
        if opts['rev']:
            try:
                revs = [other.lookup(rev) for rev in opts['rev']]
            except error.CapabilityError:
                err = _("Other repository doesn't support revision lookup, "
                        "so a rev cannot be specified.")
                raise util.Abort(err)

        # Are there any changes at all?
        modheads = repo.pull(other, heads=revs)
        if modheads == 0:
            return 0

        # Is this a simple fast-forward along the current branch?
        newheads = repo.branchheads(branch)
        newheads = [head for head in newheads if len(repo[head].children()) == 0]
        newchildren = repo.changelog.nodesbetween([parent], newheads)[2]
        if len(newheads) == 1:
            if newchildren[0] != parent:
                return hg.clean(repo, newchildren[0])
            else:
                return

        # Are there more than one additional branch heads?
        newchildren = [n for n in newchildren if n != parent]
        newparent = parent
        if newchildren:
            newparent = newchildren[0]
            hg.clean(repo, newparent)
        newheads = [n for n in newheads if n != newparent]
        if len(newheads) > 1:
            ui.status(_('not merging with %d other new branch heads '
                        '(use "hg heads ." and "hg merge" to merge them)\n') %
                      (len(newheads) - 1))
            return

        # Otherwise, let's merge.
        err = False
        if newheads:
            # By default, we consider the repository we're pulling
            # *from* as authoritative, so we merge our changes into
            # theirs.
            if opts['switch_parent']:
                firstparent, secondparent = newparent, newheads[0]
            else:
                firstparent, secondparent = newheads[0], newparent
                ui.status(_('updating to %d:%s\n') %
                          (repo.changelog.rev(firstparent),
                           short(firstparent)))
            hg.clean(repo, firstparent)
            ui.status(_('merging with %d:%s\n') %
                      (repo.changelog.rev(secondparent), short(secondparent)))
            err = hg.merge(repo, secondparent, remind=False)

        if not err:
            # we don't translate commit messages
            message = (cmdutil.logmessage(opts) or
                       ('Automated merge with %s' %
                        url.removeauth(other.url())))
            editor = cmdutil.commiteditor
            if opts.get('force_editor') or opts.get('edit'):
                editor = cmdutil.commitforceeditor
            n = repo.commit(message, opts['user'], opts['date'], editor=editor)
            ui.status(_('new changeset %d:%s merges remote changes '
                        'with local\n') % (repo.changelog.rev(n),
                                           short(n)))

    finally:
        release(lock, wlock)

cmdtable = {
    'fetch':
        (fetch,
        [('r', 'rev', [], _('a specific revision you would like to pull')),
         ('e', 'edit', None, _('edit commit message')),
         ('', 'force-editor', None, _('edit commit message (DEPRECATED)')),
         ('', 'switch-parent', None, _('switch parents when merging')),
        ] + commands.commitopts + commands.commitopts2 + commands.remoteopts,
        _('hg fetch [SOURCE]')),
}
