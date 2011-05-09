''' factotum support '''

import mercurial.url
import urllib2
import factotum

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
			self.f.delkey(proto="httpdigest", realm=realm)
		self.f.start(proto="httpdigest", role="client", realm=realm)
		self.f.write(nonce + ' ' + req.get_method() + ' ' + req.get_selector())
		resp = self.f.read()
		self.f.close()
		val = 'Digest username="%s", realm="%s", nonce="%s", uri="%s", response="%s", algorithm=MD5' % ("aiju", realm, nonce, req.get_selector(), resp)
		if req.headers.get('Authorization', None) == val: return None
		req.add_unredirected_header('Authorization', val)
		result = self.parent.open(req)
		self.retried = 0
		return result

mercurial.url.httpdigestauthhandler = factotumdigest
