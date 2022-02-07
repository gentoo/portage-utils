# Gentoo Portage Utilities

| What     | How                                                       |
| -------- | --------------------------------------------------------- |
| HOMEPAGE | https://wiki.gentoo.org/wiki/Q_applets                    |
| GIT      | https://anongit.gentoo.org/git/proj/portage-utils.git     |
| VIEWVCS  | https://gitweb.gentoo.org/proj/portage-utils.git/         |
| GITHUB   | https://github.com/gentoo/portage-utils                   |
| STATUS   | [![Build Status](https://github.com/gentoo/portage-utils/actions/workflows/build-test-ci.yml/badge.svg)](https://github.com/gentoo/portage-utils/actions/workflows/build-test-ci.yml) [![Coverity Status](https://scan.coverity.com/projects/9213/badge.svg)](https://scan.coverity.com/projects/gentoo-portage-utils) |

portage-utils is a small set of utilities for working with Portage, Gentoo
ebuilds, Gentoo ebuild overlays, installed packages (vdb), and similar sources
of information.  The focus is on size and speed, so everything is in C.

## Building

Run `configure` followed by `make`.  If you're using git-sources, run
`configure` with `--disable-maintainer-mode` or run autoreconf to get
various timestamps correct.

## Helping out

There's a large [TODO](./TODO.md) list with various ideas for
improvements.  File a bug on Gentoo's Bugzilla, or use Github's issues
and pull requests.

There's also a [HACKING](./HACKING.md) doc to help you get started.

## Examples

* find elf files linking to old openssl (using app-misc/pax-utils)<br>
  `qlist -Cao | scanelf -BqgN libssl.so.0.9.6 -f -`

* produce a package.use file for currently installed packages<br>
  `qlist -UCq | grep ' ' > package.use`

* find orphan files not owned by any package in /lib and /usr/lib<br>
  `qfile -o {,/usr}/lib/*`
	
* get PORTDIR and see where it is defined<br>
  `q -ev PORTDIR`

* verify all packages<br>
  `qcheck`

* check validity of the Manifest files for the main tree<br>
  `qmanifest`

* get an overview of what the last emerge call did<br>
  `qlop -E`

## Contact

### Bugs

Please file bugs at:
	https://bugs.gentoo.org/enter_bug.cgi?product=Portage%20Development&component=Unclassified&assigned_to=portage-utils@gentoo.org&format=guided

### Developers

* solar@gentoo.org
* vapier@gentoo.org
* grobian@gentoo.org
