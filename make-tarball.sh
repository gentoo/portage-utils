#!/bin/bash

set -e

if [[ $# -ne 1 ]] ; then
	echo "Usage: $0 <ver>" 1>&2
	exit 1
fi

ver="$1"
[[ "$ver" == "snap" ]] && ver=$(date -u +%Y%m%d)
p="portage-utils-$ver"

rm -rf "${p}"
mkdir "${p}"

echo "Checking out clean cvs sources ..."
cp -a CVS "${p}"/
cd "${p}"
cvs -Q up
cd ..

echo "Generating tarball ..."
find "${p}" -type d -name CVS -prune -print0 | xargs -0 rm -rf
tar jcf "${p}".tar.bz2 "${p}"
rm -r "${p}"
du -b "${p}".tar.bz2

exit 0
