#!/usr/bin/python

import sys,portage

i = 1
while i < len(sys.argv):
	atom1, atom2 = sys.argv[i:i + 2]
	if atom2[0] in ('>', '<', '=', '~'):
		if '/' not in atom1:
			# Portage really wants a category.
			a1 = 'c/' + atom1
			pfx = atom2[0]
			if atom2[1] == '=':
				pfx += atom2[1]
			a2 = '%sc/%s' % (pfx, atom2[len(pfx):])
		else:
			a1, a2 = atom1, atom2
		ret = portage.dep.match_from_list(a2, [a1])
		rel = '==' if ret else '!='
	else:
		try:
			pkg1 = portage.pkgsplit(atom1)
			pkg2 = portage.pkgsplit(atom2)
			ret = portage.pkgcmp(pkg1, pkg2)
			if ret == 1:
				rel = '>'
			elif ret == -1:
				rel = '<'
			else:
				rel = '=='
		except Exception as e:
			rel = '!='
	print('%s %s %s' % (atom1, rel, atom2))
	i += 2
