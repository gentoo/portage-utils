#!/bin/bash -e

v() { echo "$@"; "$@"; }

m4dir="autotools/m4"

# check for dependencies
if ! qlist -qI dev-libs/gnulib > /dev/null ; then
	echo "please install dev-libs/gnulib"
	exit 1
fi
if ! qlist -qI sys-devel/autoconf-archive > /dev/null ; then
	echo "please install sys-devel/autoconf-archive"
	exit 1
fi

v rm -rf autotools

# reload the gnulib code
PATH=/usr/local/src/gnu/gnulib:${PATH}
mods="
	dirent
	faccessat
	fdopendir
	fstatat
	futimens
	getline
	getopt-posix
	inttypes
	mkdirat
	openat
	readlinkat
	renameat
	stat-time
	strcasestr-simple
	strncat
	symlinkat
	sys_stat
	unlinkat
	utimensat
	vasprintf-posix
	xalloc
"
v gnulib-tool \
	--source-base=autotools/gnulib --m4-base=autotools/m4 \
	--import \
	--no-vc-files \
	${mods}

export AUTOMAKE="automake --foreign"
v autoreconf -i -f
