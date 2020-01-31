#!/bin/env bash

set -e

if ! .  ${EPREFIX}/lib/gentoo/functions.sh 2>/dev/null ; then
	einfo() { printf ' * %b\n' "$*"; }
	eerror() { einfo "$@" 1>&2; }
fi
die() { eerror "$@"; exit 1; }

v() { printf '\t%s\n' "$*"; "$@"; }

: ${MAKE:=make}

if [[ $# -ne 1 ]] ; then
	die "Usage: $0 <ver>"
fi

case $1 in
snap) ver=$(date -u +%Y%m%d) ;;
git) ver="HEAD" ;;
*)
	ver="v$1"
	if ! git describe --tags "${ver}" >&/dev/null ; then
		die "Please create the tag first: git tag ${ver}"
	fi
	;;
esac
p="${TMPDIR:-/var/tmp}/portage-utils-${ver#v}"
pb=${p}_build

rm -rf "${p}"
mkdir "${p}"
rm -rf "${pb}"
mkdir "${pb}"

einfo "Checking out clean git sources ..."
git archive "${ver}" | tar xf - -C "${p}"
pushd "${p}" >/dev/null

einfo "Building autotools ..."
sed -i "/^AC_INIT/s:git:${ver#v}:" configure.ac
sed -i "/^AM_MAINTAINER_MODE/s:(.*)$::" configure.ac
./autogen.sh
rm -rf autom4te.cache
popd >/dev/null

einfo "Checking sources ..."
pushd "${pb}" >/dev/null
tar cf - -C "${p}" . | tar xf -
v ./configure -q
v ${MAKE} -s
v ${MAKE} -s check
v ${MAKE} -s DESTDIR="${PWD}"/install install
v ./man/mkman.py
# ensure the tar contains up-to-date manpages
cp -a man/*.1 "${p}"/man
popd >/dev/null
rm -rf "${pb}"

einfo "Generating tarball ..."
pushd "${p%/*}" >/dev/null
tar --numeric-owner -cf - "${p##*/}" | xz > "${p}".tar.xz
popd >/dev/null
rm -r "${p}"

echo
einfo "All ready for distribution!"
mv "${p}".tar.* .
du -b "${p##*/}".tar.*

exit 0
