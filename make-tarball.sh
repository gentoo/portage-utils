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

PUSH=
case $1 in
snap) ver=$(date -u +%Y%m%d) ;;
git) ver="HEAD" ;;
*)
	ver="v$1"
	if git describe --tags "${ver}" >&/dev/null ; then
		die "Tag ${ver} already exists!"
	fi
	PUSH=yes
	# grab current tree credentials to commit with
	GIT_AUTHOR_NAME=$(git config user.name)
	GIT_AUTHOR_EMAIL=$(git config user.email)
	SIGING_KEY=$(git config user.signingkey)
	;;
esac
p="${TMPDIR:-/var/tmp}/portage-utils-${ver#v}"
pb=${p}_build

rm -rf "${p}"
mkdir "${p}"
rm -rf "${pb}"
mkdir "${pb}"

einfo "Checking out clean git sources ..."
HERE=${PWD}
pushd "${p}" >/dev/null
git clone "${HERE}"
pt=${p}/${HERE##*/}
popd >/dev/null

einfo "Adapting configure for release ${ver} ..."
pushd "${pt}" >/dev/null
sed -e "1s/2011-20[0-9][0-9]/2011-$(date +%Y)/" \
    -e "/^AC_INIT/s:\[git\(-.*\)\?\]:[${ver#v}]:" \
	-e "/^AM_MAINTAINER_MODE/s:\[enable\]:[disable]:" \
	configure.ac \
	| diff -u \
		--label "git/configure.ac" \
		--label "${ver#v}/configure.ac" \
		configure.ac - \
	| tee "${pb}/configure.diff" \
	| patch -p1
cat "${pb}/configure.diff"

einfo "Rebuilding autotools ..."
export AUTOMAKE="automake --foreign"
v autoreconf -i -f
rm -rf autom4te.cache
popd >/dev/null

einfo "Checking sources ..."
pushd "${pb}" >/dev/null
tar cf - -C "${pt}" . | tar xf -
v ./configure -q
v ${MAKE} -s
v ${MAKE} -s check
v ${MAKE} -s DESTDIR="${PWD}"/install install
v ./man/mkman.py
# ensure the tar contains up-to-date manpages
cp -a man/*.1 "${pt}"/man
popd >/dev/null
rm -rf "${pb}"

einfo "Creating release commit + tag ..."
pushd "${pt}" >/dev/null
git config user.name "${GIT_AUTHOR_NAME}"
git config user.email "${GIT_AUTHOR_EMAIL}"
git config user.signingkey "${SIGING_KEY}"
git add -f man/*.1
git commit --signoff -am "version bump to ${ver#v}" || die
git tag "${ver}" || die
popd >/dev/null

einfo "Generating tarball ..."
pushd "${pt%/*}" >/dev/null
tar --numeric-owner -cf - "${pt##*/}" | xz > "${p}".tar.xz
popd >/dev/null

einfo "Reverting for development ..."
pushd "${pt}" >/dev/null
sed -i \
	-e "/^AC_INIT/s:\[${ver#v}\]:[git-post-${ver#v}]:" \
	-e "/^AM_MAINTAINER_MODE/s:\[disable\]:[enable]:" \
	configure.ac || die
git rm -f man/*.1
v autoreconf -i -f
git commit --signoff -am "configure.ac: restoring maintainer mode"
popd >/dev/null

if [[ x${PUSH} == xyes ]] ; then
	git pull "${pt}"
fi
rm -rf "${p}"

echo
einfo "All ready for distribution!"
mv "${p}".tar.* .
du -b "${p##*/}".tar.*

exit 0
