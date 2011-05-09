'''factotum for py'''

import subprocess

class FactotumError(Exception):
	pass

class PhaseError(Exception):
	pass

class NeedkeyError(Exception):
	pass

class Factotum:
	def start(self, **args):
		self.f = open('/mnt/factotum/rpc', 'r+', 0)
		msg = 'start'
		for k, v in args.iteritems():
			msg += ' ' + k + '=\'' + v + '\''
		self.f.write(msg)
		ret = self.f.read(4096)
		if ret == "ok": return
		if ret[:5] == "error": raise FactotumError(ret[6:])
		raise FactotumError("unexpected " + ret)
	def needkey(self, string):
		subprocess.call(['/bin/auth/factotum', '-g', string])
	def read(self):
		while True:
			self.f.write('read')
			ret = self.f.read(4096)
			if ret[:7] != "needkey": break
			self.needkey(ret[8:])
		if ret == "ok": return ""
		if ret[:3] == "ok ": return ret[3:]
		if ret[:5] == "error": raise FactotumError(ret[6:])
		if ret[:5] == "phase": raise PhaseError(ret[6:])
		raise FactotumError("unexpected " + ret)
	def write(self, data):
		while True:
			self.f.write('write ' + data)
			ret = self.f.read(4096)
			if ret[:7] != "needkey": break
			self.needkey(ret[8:])
		if ret == "ok": return 0
		if ret[:3] == "toosmall ": return int(ret[4:])
		if ret[:5] == "error": raise FactotumError(ret[6:])
		if ret[:5] == "phase": raise PhaseError(ret[6:])
		raise FactotumError("unexpected " + ret)
	def close(self):
		self.f.close()
	def delkey(self, **args):
		f = open('/mnt/factotum/ctl', 'w', 0)
		msg = 'delkey'
		for k, v in args.iteritems():
			msg += ' ' + k + '=\'' + v + '\''
		f.write(msg)
		f.close()
