#!/bin/bash

if [[ $# -ne 1 ]] ; then
	echo "Usage: $0 <ver>" 1>&2
	exit 1
fi
old_files=$(find . -name '.#*')
if [[ -n ${old_files} ]] ; then
	echo "Remove these temp files before making a package:"
	echo "${old_files}"
	exit 1
fi

ver="$1"
[[ "$ver" == "snap" ]] && ver=$(date -u +%Y%m%d)
bn="$(basename $(pwd))-${ver}"
[[ -d "${bn}" ]] && rm -r "${bn}"
mkdir "${bn}" || exit 1
cp -r Makefile README *.[ch] man libq "${bn}/" || exit 1
ls q?*.c | sed -e 's:\.c$::' > "${bn}"/applet-list
rm -rf "${bn}"/{man,libq}/CVS
tar -jcvvf "${bn}".tar.bz2 ${bn} || exit 1
rm -r "${bn}" || exit 1
du -b "${bn}".tar.bz2
