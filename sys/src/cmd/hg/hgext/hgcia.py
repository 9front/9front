# Copyright (C) 2007-8 Brendan Cully <brendan@kublai.com>
# Published under the GNU GPL

"""hooks for integrating with the CIA.vc notification service

This is meant to be run as a changegroup or incoming hook. To
configure it, set the following options in your hgrc::

  [cia]
  # your registered CIA user name
  user = foo
  # the name of the project in CIA
  project = foo
  # the module (subproject) (optional)
  #module = foo
  # Append a diffstat to the log message (optional)
  #diffstat = False
  # Template to use for log messages (optional)
  #template = {desc}\\n{baseurl}/rev/{node}-- {diffstat}
  # Style to use (optional)
  #style = foo
  # The URL of the CIA notification service (optional)
  # You can use mailto: URLs to send by email, eg
  # mailto:cia@cia.vc
  # Make sure to set email.from if you do this.
  #url = http://cia.vc/
  # print message instead of sending it (optional)
  #test = False

  [hooks]
  # one of these:
  changegroup.cia = python:hgcia.hook
  #incoming.cia = python:hgcia.hook

  [web]
  # If you want hyperlinks (optional)
  baseurl = http://server/path/to/repo
"""

from mercurial.i18n import _
from mercurial.node import *
from mercurial import cmdutil, patch, templater, util, mail
import email.Parser

import xmlrpclib
from xml.sax import saxutils

socket_timeout = 30 # seconds
try:
    # set a timeout for the socket so you don't have to wait so looooong
    # when cia.vc is having problems. requires python >= 2.3:
    import socket
    socket.setdefaulttimeout(socket_timeout)
except:
    pass

HGCIA_VERSION = '0.1'
HGCIA_URL = 'http://hg.kublai.com/mercurial/hgcia'


class ciamsg(object):
    """ A CIA message """
    def __init__(self, cia, ctx):
        self.cia = cia
        self.ctx = ctx
        self.url = self.cia.url

    def fileelem(self, path, uri, action):
        if uri:
            uri = ' uri=%s' % saxutils.quoteattr(uri)
        return '<file%s action=%s>%s</file>' % (
            uri, saxutils.quoteattr(action), saxutils.escape(path))

    def fileelems(self):
        n = self.ctx.node()
        f = self.cia.repo.status(self.ctx.parents()[0].node(), n)
        url = self.url or ''
        elems = []
        for path in f[0]:
            uri = '%s/diff/%s/%s' % (url, short(n), path)
            elems.append(self.fileelem(path, url and uri, 'modify'))
        for path in f[1]:
            # TODO: copy/rename ?
            uri = '%s/file/%s/%s' % (url, short(n), path)
            elems.append(self.fileelem(path, url and uri, 'add'))
        for path in f[2]:
            elems.append(self.fileelem(path, '', 'remove'))

        return '\n'.join(elems)

    def sourceelem(self, project, module=None, branch=None):
        msg = ['<source>', '<project>%s</project>' % saxutils.escape(project)]
        if module:
            msg.append('<module>%s</module>' % saxutils.escape(module))
        if branch:
            msg.append('<branch>%s</branch>' % saxutils.escape(branch))
        msg.append('</source>')

        return '\n'.join(msg)

    def diffstat(self):
        class patchbuf(object):
            def __init__(self):
                self.lines = []
                # diffstat is stupid
                self.name = 'cia'
            def write(self, data):
                self.lines.append(data)
            def close(self):
                pass

        n = self.ctx.node()
        pbuf = patchbuf()
        patch.export(self.cia.repo, [n], fp=pbuf)
        return patch.diffstat(pbuf.lines) or ''

    def logmsg(self):
        diffstat = self.cia.diffstat and self.diffstat() or ''
        self.cia.ui.pushbuffer()
        self.cia.templater.show(self.ctx, changes=self.ctx.changeset(),
                                url=self.cia.url, diffstat=diffstat)
        return self.cia.ui.popbuffer()

    def xml(self):
        n = short(self.ctx.node())
        src = self.sourceelem(self.cia.project, module=self.cia.module,
                              branch=self.ctx.branch())
        # unix timestamp
        dt = self.ctx.date()
        timestamp = dt[0]

        author = saxutils.escape(self.ctx.user())
        rev = '%d:%s' % (self.ctx.rev(), n)
        log = saxutils.escape(self.logmsg())

        url = self.url and '<url>%s/rev/%s</url>' % (saxutils.escape(self.url),
                                                     n) or ''

        msg = """
<message>
  <generator>
    <name>Mercurial (hgcia)</name>
    <version>%s</version>
    <url>%s</url>
    <user>%s</user>
  </generator>
  %s
  <body>
    <commit>
      <author>%s</author>
      <version>%s</version>
      <log>%s</log>
      %s
      <files>%s</files>
    </commit>
  </body>
  <timestamp>%d</timestamp>
</message>
""" % \
            (HGCIA_VERSION, saxutils.escape(HGCIA_URL),
            saxutils.escape(self.cia.user), src, author, rev, log, url,
            self.fileelems(), timestamp)

        return msg


class hgcia(object):
    """ CIA notification class """

    deftemplate = '{desc}'
    dstemplate = '{desc}\n-- \n{diffstat}'

    def __init__(self, ui, repo):
        self.ui = ui
        self.repo = repo

        self.ciaurl = self.ui.config('cia', 'url', 'http://cia.vc')
        self.user = self.ui.config('cia', 'user')
        self.project = self.ui.config('cia', 'project')
        self.module = self.ui.config('cia', 'module')
        self.diffstat = self.ui.configbool('cia', 'diffstat')
        self.emailfrom = self.ui.config('email', 'from')
        self.dryrun = self.ui.configbool('cia', 'test')
        self.url = self.ui.config('web', 'baseurl')

        style = self.ui.config('cia', 'style')
        template = self.ui.config('cia', 'template')
        if not template:
            template = self.diffstat and self.dstemplate or self.deftemplate
        template = templater.parsestring(template, quoted=False)
        t = cmdutil.changeset_templater(self.ui, self.repo, False, None,
                                        style, False)
        t.use_template(template)
        self.templater = t

    def sendrpc(self, msg):
        srv = xmlrpclib.Server(self.ciaurl)
        srv.hub.deliver(msg)

    def sendemail(self, address, data):
        p = email.Parser.Parser()
        msg = p.parsestr(data)
        msg['Date'] = util.datestr(format="%a, %d %b %Y %H:%M:%S %1%2")
        msg['To'] = address
        msg['From'] = self.emailfrom
        msg['Subject'] = 'DeliverXML'
        msg['Content-type'] = 'text/xml'
        msgtext = msg.as_string()

        self.ui.status(_('hgcia: sending update to %s\n') % address)
        mail.sendmail(self.ui, util.email(self.emailfrom),
                      [address], msgtext)


def hook(ui, repo, hooktype, node=None, url=None, **kwargs):
    """ send CIA notification """
    def sendmsg(cia, ctx):
        msg = ciamsg(cia, ctx).xml()
        if cia.dryrun:
            ui.write(msg)
        elif cia.ciaurl.startswith('mailto:'):
            if not cia.emailfrom:
                raise util.Abort(_('email.from must be defined when '
                                   'sending by email'))
            cia.sendemail(cia.ciaurl[7:], msg)
        else:
            cia.sendrpc(msg)

    n = bin(node)
    cia = hgcia(ui, repo)
    if not cia.user:
        ui.debug(_('cia: no user specified'))
        return
    if not cia.project:
        ui.debug(_('cia: no project specified'))
        return
    if hooktype == 'changegroup':
        start = repo.changelog.rev(n)
        end = len(repo.changelog)
        for rev in xrange(start, end):
            n = repo.changelog.node(rev)
            ctx = repo.changectx(n)
            sendmsg(cia, ctx)
    else:
        ctx = repo.changectx(n)
        sendmsg(cia, ctx)
