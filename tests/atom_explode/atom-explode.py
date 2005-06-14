#!/usr/bin/python

import sys,portage

for a in sys.argv[1:]:
	cpv = portage.catpkgsplit(a)
	print a+" -> "+cpv[0]+" / "+cpv[1]+" - "+cpv[2]+"-"+cpv[3]+" ["+cpv[2]+"] ["+cpv[3]+"]"
