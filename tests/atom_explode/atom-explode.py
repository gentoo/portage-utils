#!/usr/bin/python

import sys,portage

for a in sys.argv[1:]:
	cpv = portage.catpkgsplit(a)
	pv = cpv[1] + "-" + cpv[2]
	if cpv[3] != "r0":
		pv += "-" + cpv[3]
	print a+" -> "+cpv[0]+" / ["+pv+"] "+cpv[1]+" - "+cpv[2]+"-"+cpv[3]+" ["+cpv[2]+"] ["+cpv[3]+"]"

for a in sys.stdin.readlines():
	a = a.strip()
	cpv = portage.catpkgsplit(a)
	pv = cpv[1] + "-" + cpv[2]
	if cpv[3] != "r0":
		pv += "-" + cpv[3]
	print a+" -> "+cpv[0]+" / ["+pv+"] "+cpv[1]+" - "+cpv[2]+"-"+cpv[3]+" ["+cpv[2]+"] ["+cpv[3]+"]"
