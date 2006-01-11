#!/usr/bin/python

import sys,portage

i=1
while i < len(sys.argv):
	atom1 = sys.argv[i]
	atom2 = sys.argv[i+1]
	try:
		ret = portage.pkgcmp(portage.pkgsplit(atom1), portage.pkgsplit(atom2))
		if ret == 1:
			rel = ">"
		elif ret == -1:
			rel = "<"
		else:
			rel = "=="
	except:
		rel = "!="
		pass
	print atom1+" "+rel+" "+atom2
	i += 2
