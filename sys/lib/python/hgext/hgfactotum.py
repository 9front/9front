''' factotum support '''

import mercurial.url
import urllib2
import factotum
import base64

class factotumbasic(urllib2.BaseHandler):
	def __init__(self, passmgr=None):
		self.f = factotum.Factotum()
		self.retried = 0
		self.auth = None
	def http_error_401(self, req, fp, code, msg, headers):
		host = urllib2.urlparse.urlparse(req.get_full_url())[1]
		authreq = headers.get('www-authenticate', None)
		if authreq == None: return None
		authreq = authreq.split(' ', 1)
		if authreq[0].lower() != 'basic': return None
		chal = urllib2.parse_keqv_list(urllib2.parse_http_list(authreq[1]))
		realm = chal['realm']
		self.auth = (host, realm)
		self.retried += 1
		if self.retried >= 3:
			self.f.delkey(proto="pass", host=host, realm=realm, role="client")
		self.f.start(proto="pass", host=host, realm=realm, role="client")
		pw = self.f.read().replace(' ', ':', 1)
		val = 'Basic %s' % base64.b64encode(pw).strip()
		if req.headers.get('Authorization', None) == val: return None
		req.add_header('Authorization', val)
		result = self.parent.open(req)
		self.retried = 0
		return result
	def http_error_403(self, req, fp, code, msg, headers):
		if self.auth != None:
			self.f.delkey(proto="pass", host=self.auth[0], realm=self.auth[1], role="client")
			self.auth = None
		
class factotumdigest(urllib2.BaseHandler):
	auth_header = 'Authorization'
	handler_order = 490
	
	def __init__(self, passmgr=None):
		self.f = factotum.Factotum()
		self.retried = 0
	def http_error_401(self, req, fp, code, msg, headers):
		self.retried += 1
		host = urllib2.urlparse.urlparse(req.get_full_url())[1]
		authreq = headers.get('www-authenticate', None)
		if authreq == None: return None
		authreq = authreq.split(' ', 1)
		if authreq[0].lower() != 'digest': return None
		chal = urllib2.parse_keqv_list(urllib2.parse_http_list(authreq[1]))
		realm = chal['realm']
		nonce = chal['nonce']
		if self.retried >= 6:
			self.f.delkey(proto="httpdigest", realm=realm, host=host)
		self.f.start(proto="httpdigest", role="client", realm=realm, host=host)
		self.f.write(nonce + ' ' + req.get_method() + ' ' + req.get_selector())
		resp = self.f.read()
		user = self.f.attr()["user"]
		self.f.close()
		val = 'Digest username="%s", realm="%s", nonce="%s", uri="%s", response="%s", algorithm=MD5' % (user, realm, nonce, req.get_selector(), resp)
		if req.headers.get('Authorization', None) == val: return None
		req.add_unredirected_header('Authorization', val)
		result = self.parent.open(req)
		self.retried = 0
		return result

urllib2.HTTPBasicAuthHandler = factotumbasic
mercurial.url.httpdigestauthhandler = factotumdigest
