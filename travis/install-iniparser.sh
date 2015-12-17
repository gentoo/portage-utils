#!/bin/bash -e

. "${0%/*}"/lib.sh

main() {
	local pv="3.1"
	local S="iniparser"
	travis_fold start dep-iniparser
	rm -rf iniparser*
	v mkdir -p ../sysroot
	v wget http://distfiles.gentoo.org/distfiles/iniparser-${pv}.tar.gz
	v tar xf iniparser-${pv}.tar.gz
	m -C ${S}
	v cp ${S}/libiniparser.a ${S}/src/{dictionary,iniparser}.h ../sysroot/
	v rm -rf iniparser*
	travis_fold end dep-iniparser
}
main "$@"
