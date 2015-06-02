#!/bin/python

# enable importing on demand to reduce startup time
from mercurial import demandimport; demandimport.enable()

from mercurial.hgweb.hgweb_mod import hgweb
import mercurial.hgweb.wsgicgi as wsgicgi
import os
application = hgweb(os.environ["REPO_ROOT"], os.environ["REPO_NAME"])
wsgicgi.launch(application)
