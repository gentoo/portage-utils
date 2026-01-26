#!/bin/bash -e

v() { echo "$@"; "$@"; }

m4dir="autotools/m4"

# check for dependencies
if ! qlist -qI dev-libs/gnulib > /dev/null ; then
	echo "please install dev-libs/gnulib"
	exit 1
fi
if ! qlist -qI dev-build/autoconf-archive > /dev/null ; then
	echo "please install dev-build/autoconf-archive"
	exit 1
fi

# keep this list updated with non-generated M4 files
keepm4=( ac_check_sendfile.m4 )
for keepf in "${keepm4[@]}" ; do
	v mv "${m4dir}/${keepf}" "autotools/"
done
v rm -rf autotools/{gnulib,m4}
v mkdir "${m4dir}"
for keepf in "${keepm4[@]}" ; do
	v mv "autotools/${keepf}" "${m4dir}/"
done

# reload the gnulib code
PATH=/usr/local/src/gnu/gnulib:${PATH}
mods="
	crypto/md5-buffer
	crypto/sha1-buffer
	crypto/sha256-buffer
	crypto/sha512-buffer
	dirent-h
	faccessat
	fdopendir
	fstatat
	futimens
	getline
	getopt-posix
	inttypes-h
	mkdirat
	openat
	readlinkat
	renameat
	stat-time
	strcasestr-simple
	strncat
	strtoll
	symlinkat
	sys_stat-h
	unlinkat
	utimensat
	xalloc
"
v gnulib-tool \
	--source-base=autotools/gnulib --m4-base=autotools/m4 \
	--import \
	--no-vc-files \
	${mods}

{
	sed -e '/^# BEGIN GNULIB/,/^# END GNULIB/d' .gitignore
	cat <<- EOM
		# BEGIN GNULIB  --  keep this at the end of this file
		# regenerate using:
		#   ls autotools/gnulib/*.in.h | sed -e 's/\.in\.h/.h/' | sed -e 's:_:/:'
		# or use autogen.sh
	EOM
	ls autotools/gnulib/*.in.h | sed -e 's/\.in\.h/.h/' | sed -e 's:_:/:'
	cat <<- EOM
		# manual additions
		autotools/gnulib/sys
		autotools/gnulib/malloc/scratch_buffer.gl.h
		*.dirstamp
		# END GNULIB
	EOM
} > .gitignore.new
[[ -s .gitignore.new ]] && mv .gitignore.new .gitignore

export AUTOMAKE="automake --foreign"
v autoreconf -i -f
