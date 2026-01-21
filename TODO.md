# Common
- unify match behavior:
	- default \*foo\*
	- -e foo
	- -r (-R ?) regexp foo.\*
- make default -e for apps like quse/qdepends/qlist?
- parse package.accept\_keywords such that we can provide the latest
  "available" version like Portage
- check timestamps in libq/tree for choosing which method to take:
	- ignore Packages when it is older than the last directory change
	- add some method to skip freshness checks and assume everything is right
- make tree\_get\_metadata also retrieve maintainer type, such that
  qlist can query for maintainer email or type, ideally to do
  qlist -Iv $(portageq --repo gentoo --orphaned) in one step (bug 711466#c3)
- handle compressed Packages.gz file in tree

# tests
- add test for qsearch to avoid repetitions like
  https://bugs.gentoo.org/701470

# qmerge
- dep resolver needs spanktastic love.
- needs safe deleting (merge in place rather than unmerge;merge)
- multiple binary repos (talk to zmedico)
- gpg sign the packages file (before compression)
- binary vdb (sqlite) ... talk to zmedico
- remote binhost
- vdb tree is opened multiple times, need 1 global one (context?)
- parallel fetch tbz2s
- env is not saved/restored between pkg\_{pre,post}inst (see portage and REPO\_LAYOUT\_CONF\_WARN)
- support installing via path to tbz2 package
- support TTL field in binpkgs file
- unmerging should clean out @world set
- fixup lame misnaming of force\_download (--fetch/--force) actually
  not-forcing things

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
- make world agument really read world file, add @all?
- produce and/or update Packages (and Packages.gz) file

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
