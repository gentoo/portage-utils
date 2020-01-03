#!/bin/bash -e

. "${0%/*}"/lib.sh

# For local deps like blake2b.
export CPPFLAGS="-I${PWD}/../sysroot"
export LDFLAGS="-L${PWD}/../sysroot"

# ignore timestamps which git doesn't preserve
# disable openmp because Clang's libomp isn't installed
DEFARGS="--disable-maintainer-mode --disable-openmp"

do_run() {
  v ./configure ${*}

  # Standard optimized build.
  m V=1
  m check
  m clean
}

if [[ ${CC} == coverity ]] ; then
  [[ -n ${COVERITY_SCAN_TOKEN} ]] || exit 0;  # don't fail on this for PRs
  # ensure we end up with an existing compiler
  export CC=gcc
  v ./configure ${DEFARGS} --enable-qmanifest --enable-qtegrity
  curl -s 'https://scan.coverity.com/scripts/travisci_build_coverity_scan.sh' | bash
elif [[ ${CC} == valgrind ]] ; then
  export CC=gcc
  do_run CFLAGS=-g ${DEFARGS} --enable-qmanifest --enable-qtegrity
else
  do_run ${DEFARGS}
  do_run ${DEFARGS} --enable-qmanifest --enable-qtegrity
  do_run ${DEFARGS} --disable-qmanifest --enable-qtegrity
  do_run ${DEFARGS} --enable-qmanifest --disable-qtegrity
  do_run ${DEFARGS} --disable-qmanifest --disable-qtegrity
fi
