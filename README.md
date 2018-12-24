# Gentoo Portage Utilities

| What     | How                                                       |
| -------- | --------------------------------------------------------- |
| HOMEPAGE | https://wiki.gentoo.org/wiki/Q\_applets                   |
| GIT      | https://anongit.gentoo.org/git/proj/portage-utils.git     |
| VIEWVCS  | https://gitweb.gentoo.org/proj/portage-utils.git/         |
| GITHUB   | https://github.com/gentoo/portage-utils                   |
| STATUS   | [![Build Status](https://travis-ci.org/gentoo/portage-utils.svg?branch=master)](https://travis-ci.org/gentoo/portage-utils) [![Coverity Status](https://scan.coverity.com/projects/9213/badge.svg)](https://scan.coverity.com/projects/gentoo-portage-utils) |

portage-utils is a small set of utilities for working with Portage, Gentoo
ebuilds, Gentoo ebuild overlays, installed packages (vdb), and similar sources
of information.  The focus is on size and speed, so everything is in C.

## Building

Just run `make`.  This should work on any recent Linux system.
Alternatively, run `configure` followed by `make`.

## Helping out

There's a large [TODO](./TODO.md) list with various ideas for
improvements.  File a bug on Gentoo's Bugzilla, or use Github's issues
and pull requests.

There's also a [HACKING](./HACKING.md) doc to help you get started.

## Examples

* find elf files linking to old openssl (using app-misc/pax-utils)<br>
  `qlist -ao | scanelf -BqgN libssl.so.0.9.6 -f -`

* produce a package.use file for currently installed packages<br>
  `qlist -UCq | grep ' ' > package.use`

* find orphan files not owned by any package in /lib and /usr/lib<br>
  `qfile -o {,/usr}/lib/*`
	
* get PORTDIR<br>
  `env DEBUG=: q -Ch 2>&1 | grep ^PORTDIR | awk '{print $3}`

* Verify all packages<br>
  `qcheck`

## Contact

### Bugs

Please file bugs at:
	https://bugs.gentoo.org/enter_bug.cgi?product=Portage%20Development&component=Tools&format=guided

### Developers

* solar@gentoo.org
* vapier@gentoo.org
* grobian@gentoo.org

## Notes

### Speed is everything.

Having your PORTDIR and VDB on the right file system helps dramatically

Nowadays this should rarely matter, but on smaller scale (embedded) or
older hardware, consider the following.

IDE raid with PORTDIR on reiserfs:

```sh
$ q -r
q: Finished 20655 entries in 1.990951 seconds
```

IDE raid with PORTDIR on ext3:

```sh
$ q -r
q: Finished 20655 entries in 203.664252 seconds
```
