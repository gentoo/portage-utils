#!/bin/bash -e

. "${0%/*}"/lib.sh

main() {
	# For local deps like iniparser.
	export CPPFLAGS="-I${PWD}/../sysroot"
	export LDFLAGS="-L${PWD}/../sysroot"

	# Standard optimized build.
	m
	m check

	# Debug build w/ASAN and such enabled.
	m debug
	m check
}
main "$@"
