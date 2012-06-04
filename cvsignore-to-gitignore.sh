#!/bin/bash

find -name .cvsignore | \
while read c ; do
	g=${c/cvs/git}
	sed \
		-e 's:[.]git:CVS:' \
		${c} > ${g}
done
