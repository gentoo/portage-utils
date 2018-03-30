#!/bin/bash -e

. "${0%/*}"/lib.sh

# We have to do this by hand rather than use the coverity addon because of
# matrix explosion: https://github.com/travis-ci/travis-ci/issues/1975
# We also do it by hand because when we're throttled, the addon will exit
# the build immediately and skip the main script!
coverity_scan() {
	local reason
	[[ ${TRAVIS_JOB_NUMBER} != *.1 ]] && reason="not first build job"
	[[ -n ${TRAVIS_TAG} ]] && reason="git tag"
	[[ ${TRAVIS_PULL_REQUEST} == "true" ]] && reason="pull request"
	if [[ -n ${reason} ]] ; then
		echo "Skipping coverity scan due to: ${reason}"
		return
	fi

	export COVERITY_SCAN_PROJECT_NAME="${TRAVIS_REPO_SLUG}"
	export COVERITY_SCAN_NOTIFICATION_EMAIL="vapier@gentoo.org"
	export COVERITY_SCAN_BUILD_COMMAND="make -j${ncpus}"
	export COVERITY_SCAN_BUILD_COMMAND_PREPEND="git clean -q -x -d -f; git checkout -f"
	export COVERITY_SCAN_BRANCH_PATTERN="master"

	curl -s "https://scan.coverity.com/scripts/travisci_build_coverity_scan.sh" | bash || :
}

main() {
	# For local deps like iniparser.
	export CPPFLAGS="-I${PWD}/../sysroot"
	export LDFLAGS="-L${PWD}/../sysroot"

	# Standard optimized build.
	m
	m check

	# Debug build w/ASAN and such enabled.
	export LSAN_OPTIONS=verbosity=1:log_threads=1
	m debug
	m check
	unset LSAN_OPTIONS

	# Do scans last as they like to dirty the tree and some tests
	# expect a clean tree (like code style checks).
	v --fold="coverity_scan" coverity_scan
}
main "$@"
