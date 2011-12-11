#!/bin/bash -e

v() { echo "$@"; "$@"; }

m4dir="autotools/m4"

v rm -rf autotools
v ${MAKE:-make} autotools-update

# reload the gnulib code if possible
PATH=/usr/local/src/gnu/gnulib:${PATH}
mods="
	faccessat
	fstatat
	futimens
	getline
	mkdirat
	openat
	readlinkat
	renameat
	strncat
	symlinkat
	unlinkat
	utimensat
"
v gnulib-tool \
	--source-base=autotools/gnulib --m4-base=autotools/m4 \
	--import \
	${mods}

# not everyone has sys-devel/autoconf-archive installed
for macro in $(grep -o '\<AX[A-Z_]*\>' configure.ac | sort -u) ; do
	if m4=$(grep -rl "\[${macro}\]" /usr/share/aclocal/) ; then
		v cp $m4 ${m4dir}/
	fi
done

export AUTOMAKE="automake --foreign"
v autoreconf -i -f

if [[ -x ./test.sh ]] ; then
	exec ./test.sh "$@"
fi
