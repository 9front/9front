''' webfs support '''

import mercurial.url
import re
import os

class Webconn:
	def __init__(self, mnt, req):
		while True:
			try:
				self.open(mnt, req)
				return
			except IOError, e:
				try:
					errstr = e.strerror
					params = errstr[errstr.index("needkey ")+8:]
					if params.find("!password?") < 0:
						raise e
					if os.spawnl(os.P_WAIT, "/boot/factotum", "getkey", "-g", params) != 0:
						raise e
				except:
					raise e

	def open(self, mnt, req):
		if type(req) == str:
			self.url = req
		else:
			self.url = req.get_full_url()
		if self.url[0:5] == 'file:':
			path = self.url[5:]
			while path[0:2] == '//':
				path = path[1:]
			self.dir = '/dev/null'
			self.body = open(path, 'r', 0)
			return
		ctl = open(mnt+'/clone', 'r+', 0)
		try:
			self.dir = mnt+'/'+ctl.readline().rstrip('\n')
			ctl.seek(0)
			ctl.write('url '+self.url)
			m = 'User-Agent: mercurial/proto-1.0\r\n';
			ctl.seek(0)
			ctl.write('headers '+m)
			for h in req.headers:
				ctl.seek(0)
				ctl.write('headers '+h+': '+req.headers[h]+'\r\n')

			if req.has_data():
				data = req.get_data()
				post = open(self.dir+'/postbody', 'w', 0);
				try:
					data.seek(0)
					while True:
						buf = data.read(4096)
						if len(buf) == 0:
							break
						post.write(buf)
				finally:
					post.close()
			self.body = open(self.dir+'/body', 'r', 0)
		finally:	
			ctl.close()

	def read(self, amt=-1):
		return self.body.read(amt);

	def close(self):
		self.body.close()
		self.body = None
		self.dir = None

	def geturl(self):
		return self.url

	def getheader(self, key):
		name = re.sub(r'[^a-z]+', '', key.lower())
		try:
			f = open(self.dir+'/'+name, 'r', 0)
			try:
				hdr = f.read()
			finally:
				f.close()
			return hdr
		except:
			return None

class Webopener:
	def __init__(self):
		self.handlers = []

	def add_handler(self, handler):
		return

	def open(self, req, data=None):
		return Webconn('/mnt/web', req)

	def close(self):
		pass


def webopener(ui, authinfo=None):
	return Webopener();

mercurial.url.has_https = 1
mercurial.url.opener = webopener
