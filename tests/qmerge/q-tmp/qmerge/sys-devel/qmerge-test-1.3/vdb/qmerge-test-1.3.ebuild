# Copyright 1999-2012 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: $

EAPI="4"

DESCRIPTION="my desc"
HOMEPAGE="http://gentoo.org"
SRC_URI=""

LICENSE="GPL-2"
SLOT="0"
KEYWORDS="~amd64 ~x86"
IUSE=""

S=${WORKDIR}

p() {
	einfo "FUNC = $1"
	einfo "EBUILD_PHASE = ${EBUILD_PHASE}"
	[ "$1" = "pkg_${EBUILD_PHASE}" ] || echo "FAIL: EBUILD_PHASE is wrong"
	einfo "ROOT = ${ROOT}"
	einfo "EROOT = ${EROOT}"
	einfo "ED = ${ED}"
	einfo "D = ${D}"
	einfo "T = ${T}"
	[ -d "${T}" ] || echo "FAIL: T does not exist"
	einfo "PN = ${PN}"
}
pkg_pretend() {
	p pkg_pretend
}
pkg_setup() {
	p pkg_setup

	echo
	[ "${MERGE_TYPE}" = "binary" ] || echo "FAIL: MERGE_TYPE is wrong"

	echo
	elog "elog test"
	ewarn "ewarn test"
	eqawarn "eqawarn test"
	eerror "eerror test"
	ebegin "ebegin/eend pass test"
	eend 0 || echo "FAIL: eend did not return 0"
	ebegin "ebegin/eend fail test"
	eend 1 "ignore the !! part" && echo "FAIL: eend did not return 1"
}
pkg_preinst() {
	p pkg_preinst
	[ -d "${D}" ] || echo "FAIL: D does not exist"
	[ -x "${D}/usr/bin/${PN}" ] || echo "FAIL: ${PN} not in /usr/bin"
}
pkg_postinst() {
	p pkg_postinst
}
pkg_prerm() {
	p pkg_prerm
}
pkg_postrm() {
	p pkg_postrm
}

src_install() {
	printf '#!/bin/sh\necho hi\n' > test.sh
	newbin test.sh ${PN}

	cat <<-EOF > some.conf
	# file conf
	foo
	EOF
	insinto /etc
	doins some.conf
	newins some.conf another.conf
}
