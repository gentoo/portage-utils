setup_path() {
	local d=$PWD
	while [[ $d != "/" ]] ; do
		[[ -e $d/q ]] && break
		d=${d%/*}
	done
	PATH=$d:$PATH
}
setup_path

# matches tests/subdir.mk
setup_env() {
	local a=${0%/*}
	a=${a##*/}

	: ${b:=.}
	: ${s:=.}
	: ${atb:=${PWD}/../..}
	: ${ats:=${PWD}/../..}
	: ${ab:=${atb}/tests/${a}}
	: ${as:=${ats}/tests/${a}}
}
setup_env

# clean any random vars from the host system
unset ROOT PORTAGE_CONFIGROOT PORTAGE_QUIET

q -i -q

fail() {
	echo "FAILED: $*"
	exit 1
}
die() { fail "$@" ; }

skip() {
	echo "SKIPPED: $*"
	exit 0
}

pass() {
	echo "PASSED"
	exit 0
}

mktmpdir() {
	local d=${1:-${ab}/tmp}
	rm -rf "$d" && \
	mkdir -p "$d" && \
	pushd "$d" >/dev/null \
		|| fail "could not make tmp dir '$d'"
}
_cleantmpdir() {
	local cmd=$1; shift
	local d=${1:-${ab}/tmp}
	popd >/dev/null
	${cmd} "${d}" || fail "could not clean tmp dir '$d'"
}
cleantmpdir() { _cleantmpdir "rm -rf" "$@" ; }
trimtmpdir() { _cleantmpdir "rmdir" "$@" ; }
