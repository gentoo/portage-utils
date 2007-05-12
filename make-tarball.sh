#!/bin/bash

if [[ $# -ne 1 ]] ; then
	echo "Usage: $0 <ver>" 1>&2
	exit 1
fi
old_files=$(find . -name '.#*' -o -name '*.o')
if [[ -n ${old_files} ]] ; then
	echo "Remove these temp files before making a package:"
	echo "${old_files}"
	exit 1
fi
find . -perm -1 -exec chmod u+rwx '{}' \;
find . -type d -exec chmod 755 '{}' \;
find . -name '*.c' -exec chmod 644 '{}' \;
find . -name '*.h' -exec chmod 644 '{}' \;
find . -name '*.1' -exec chmod 644 '{}' \;
chmod 644 COPYING  HACKING  Makefile  README  TODO

ver="$1"
[[ "$ver" == "snap" ]] && ver=$(date -u +%Y%m%d)
bn="$(basename $(pwd))-${ver}"
[[ -d "${bn}" ]] && rm -r "${bn}"
mkdir "${bn}" || exit 1
cp -r .depend Makefile README TODO *.[ch] qsync man libq tests mod "${bn}/" || exit 1
APPLETS=$(awk -F'"' '{print $2}'  include_applets.h | cut -d . -f 1)
for applet in ${APPLETS} ; do
	[[ $applet != q ]] && echo $applet
done | sort > "${bn}"/applet-list
find "${bn}" -type d -name CVS -exec rm -rf '{}' \; 2>/dev/null
tar jcf "${bn}".tar.bz2 ${bn} || exit 1
rm -r "${bn}" || exit 1
du -b "${bn}".tar.bz2
