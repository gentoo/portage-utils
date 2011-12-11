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
sed -i "/^AC_INIT/s:cvs:${ver}:" configure.ac
sed -i "1iPV := ${ver}" Makefile
make autotools
rm -rf autom4te.cache
cd ..

echo "Generating tarball ..."
find "${p}" -type d -name CVS -prune -print0 | xargs -0 rm -rf
tar cf - "${p}" | xz > "${p}".tar.xz
rm -r "${p}"
du -b "${p}".tar.*

exit 0
