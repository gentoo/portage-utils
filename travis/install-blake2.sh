#!/bin/bash -e

. "${0%/*}"/lib.sh

main() {
	local pv="0.98.1"
	local S="libb2-${pv}"
	travis_fold start dep-blake2
	rm -rf libb2*
	v mkdir -p ../sysroot
	v wget https://github.com/BLAKE2/libb2/releases/download/v${pv}/libb2-${pv}.tar.gz
	v tar xf libb2-${pv}.tar.gz
	(
		cd "${S}"
		./configure \
			--enable-static \
			--disable-shared \
			--prefix=/ \
			--libdir=/ \
			--includedir=/
		m
		m DESTDIR="${PWD}/../../sysroot" install
	)
	v rm -rf libb2*
	travis_fold end dep-blake2
}
main "$@"
