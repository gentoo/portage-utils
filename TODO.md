# Common

- unify match behavior:
	- default \*foo\*
	- -e foo
	- -r (-R ?) regexp foo.\*
- make default -e for apps like quse/qdepends?

- disable color when tty = NULL; may break less?

- standardize/unify/clean up misc handling of colors
  define rules:
    BOLD CATEGORY/ BLUE PKG BKBLUE -VER YELLOW :SLOT GREEN ::REPO NORM [ MAGENTA USE NORM ]

- remove odd rmspace for each string in libq/set.c (allows a lot less
  malloc/frees)

- make set.c to array (xarray) instead of C-array (list)

- env vars only get expanded once, so this fails:<br>
  `ACCEPT_LICENSE="foo"`<br>
  `ACCEPT_LICENSE="${ACCEPT_LICENSE} bar"`<br>
  we end up getting just:<br>
  `ACCEPT_LICENSE=" bar"`

- q\_vdb\_foreach\_pkg should have variant that takes an atom (or just
  cat?) to reduce search space, same for cache\_foreach\_pkg

- vdb repo/slot think about when it is freed (see cache\_pkg\_close)

# Atoms

- only 32bit values are supported for revision (-r#)
- only 64bit values are supported in any individual version component
  foo-(1234)\_alpha(56789)
- these limits should not be an issue for all practical purposes

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
- test should work on local vdb (so TRAVIS can test it too)

# qdepends

- support querying uninstalled packages (via libq/cache)
- add -S/-v/-R behavior like qlist #574934
- support printing full dep content (with -v?) from libq/cache
- bring back -k?  (but seems solved by using qlist -IF%{SLOT} pkg)

# qpkg

- fix "would be freed" message when --pretend is *not* active
- add a verbose output that describes why a package is cleaned
	- newer binpkgs available
	- newer installed version available

# qsync

- rewrite to use new repos.conf standard

# qgrep

- make it use standard xarray instead of its own buf\_list

# qlop

- have a mode that doesn't print timestamp (to get just atoms, -v should
  work)
- make a -d mode that allows to do equivalent of "last portage emerge"
  to make it easy to see what was newly merged/unmerged

# qlist
- have -F for use with -I so one can do things like print SLOT for
  package X

# quse
- make -v only print requested USE-flag when flags given
- read VDB on -v to print details about current USE-flag status, bug #656550

# qkeyword
- avoid multiple atom\_explode in path traversal
  * during qkeyword\_vercmp
  * during qkeyword\_results\_cb
  * in libq/cache\_read\_metadata
- drop -c argument? it can be fully expressed using -p cat/
