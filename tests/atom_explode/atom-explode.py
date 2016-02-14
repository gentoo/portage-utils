#!/usr/bin/python

import sys,portage

def doit(a):
	# ['x11-terms', 'wterm', '6.2.9', 'r2']
	cpv = portage.catpkgsplit(a)
	# input -> CATEGORY / [P] PN - PVR [PV] [PR_int]
	CATEGORY = cpv[0]
	PN = cpv[1]
	PV = cpv[2]
	PR_int = cpv[3]
	P = PN + "-" + PV
	PVR = PV + "-" + cpv[3]
	print(a+" -> "+CATEGORY+" / ["+P+"] "+PN+" - "+PVR+" ["+PV+"] ["+PR_int+"]")

for a in sys.argv[1:]:
	doit(a)

for a in sys.stdin.readlines():
	doit(a.strip())
