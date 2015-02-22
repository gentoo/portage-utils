. /etc/init.d/functions.sh 2>/dev/null || :

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
	[[ ${a} == "." ]] && a=${PWD}
	a=${a##*/}

	: ${b:=.}
	: ${s:=.}
	: ${atb:=${PWD}/../..}
	: ${ats:=${PWD}/../..}
	: ${ab:=${atb}/tests/${a}}
	: ${as:=${ats}/tests/${a}}

	if [[ -z ${GOOD} ]] && [[ -d ${ats}/.git ]] ; then
		eval $(eval_ecolors) 2>/dev/null
	fi
}
setup_env

# clean any random vars from the host system
unset ROOT PORTAGE_CONFIGROOT PORTAGE_QUIET

q -i -q

tret=0
tpassed=0
tfailed=0

tfail() {
	echo "${BAD}FAIL:${NORMAL} $*"
	: $(( ++tfailed ))
	tret=1
	return 1
}
tpass() {
	echo "${GOOD}PASS:${NORMAL} $*"
	: $(( ++tpassed ))
	return 0
}
tend() {
	local r=$1; shift
	[[ $r -eq 0 ]] && tpass "$@" || tfail "$@"
	return $r
}

die() {
	tfail "$@"
	end
}

skip() {
	echo "${WARN}SKIPPED:${NORMAL} $*"
	exit 0
}

end() {
	echo "${HILITE}${PWD##*/}:${NORMAL} ${tpassed} passes / ${tfailed} fails"
	exit ${tret}
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

treedir() {
	local d=$1
	if ! tree "${d}" 2>/dev/null ; then
		ls -R "${d}"
	fi
}
