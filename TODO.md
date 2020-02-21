# Common
- unify match behavior:
	- default \*foo\*
	- -e foo
	- -r (-R ?) regexp foo.\*
- make default -e for apps like quse/qdepends/qlist?
- env vars only get expanded once, so this fails:<br>
  `ACCEPT_LICENSE="foo"`<br>
  `ACCEPT_LICENSE="${ACCEPT_LICENSE} bar"`<br>
  we end up getting just:<br>
  `ACCEPT_LICENSE=" bar"`
- tree\_get\_atoms should return atoms iso string set, needs a rewrite
  to use foreach\_pkg and get\_atom -- set is ready for storing objects
  now
- parse package.accept\_keywords such that we can provide the latest
  "available" version like Portage
- check timestamps in libq/tree for choosing which method to take:
	- ignore Packages when it is older than the last directory change
	- ignore metadata when ebuild is modified
	- add some method to skip these checks and assume everything is right
- add interface to retrieve a list/set of atoms from a tree
  - pkg\_ctx for each found match, NULL otherwise
  - more efficient than traversing the entire tree every time (scandir
    or parsing Packages)
  - cached, such that repeated matches for the same thing are served off
    the cache (set), as frequently happens in dependency calculations
  - tree\_{open,close}\_cat can be made static afterwards

# tests
- add test for qsearch to avoid repetitions like
  https://bugs.gentoo.org/701470

# qmerge
- dep resolver needs spanktastic love.
- needs safe deleting (merge in place rather than unmerge;merge)
- multiple binary repos (talk to zmedico)
- handle compressed Packages file (talk to zmedico)
- handle binary Packages file (talk to zmedico)
- gpg sign the packages file (before compression)
- binary vdb (sqlite) ... talk to zmedico
- remote vdb
- parallel fetch tbz2s
- check order of pkg\_{pre,post}{inst,rm} during install, unmerge, and upgrade
- env is not saved/restored between pkg\_{pre,post}inst (see portage and REPO\_LAYOUT\_CONF\_WARN)
- support installing via path to tbz2 package
- support TTL field in binpkgs file
- merge duplicate atoms on the CLI (`qmerge -Uq nano nano nano`)
- unmerging should clean out @world set
- fixup lame misnaming of force\_download (--fetch/--force) actually
  not-forcing things
- use xpak.h instead of shelling out to qxpak binary

# qdepends
- -v should lookup whether packages are installed for || cases/colouring

# qpkg
- add a verbose output that describes why a package is cleaned
	- newer binpkgs available
	- newer installed version available
- integrate qxpak and qtbz2 with this package (the latter are confusing,
  and qpkg is doing parts of qtbz2's compose
- share install\_mask code from qmerge to handle negatives from
  pkg\_install\_mask too

# qgrep
- make it use standard xarray instead of its own buf\_list

# quse
- make -v only print requested USE-flag when flags given
- list each package only once (e.g. quse -e lz4)

# qkeyword
- drop -c argument? it can be fully expressed using -p cat/
- add mode to list which packages needs to be keyworded (deps), talk to
  Kent Fredric on details, 20191229000543.001631d9@katipo2.lan
  Re: [gentoo-dev] Keywordreqs and slacking arch team

# qmanifest
- use openat in most places
- parse timestamps and print in local timezone
- implement python module for gemato interface (to use with Portage)

# qlop
- guestimate runtime based on best-matching pkg (e.g. with gcc)
- calculate or take some "smooth" factor just added on top of the
  guestimate alternative to current time jumping
- multiple files support -- current opinion: don't do it
- compressed file support, use guessing support from qmerge?

# qfile
- stop searching when absolute path argument was found?
