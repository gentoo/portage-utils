# Common

- unify match behavior:
	- default \*foo\*
	- -e foo
	- -r (-R ?) regexp foo.\*

- disable color when tty = NULL; may break less?

- multiline reads don't yet work for quse/qsearch

- standardize/unify/clean up misc handling of colors

- remove odd rmspace for each string in libq/set.c (allows a lot less
  malloc/frees)

- make set.c to array (xarray) instead of C-array (list)

- equiv of `equery m` (metadata)

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
- need to handle USE deps like: cat/pkg-123[foo(+)]

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

- support querying uninstalled packages (via metadata/md5 cache)
- support atoms like `qdepends -rQ qtsql:4` (should match unslotted deps)
- add -S/-v/-R behavior like qlist #574934

# qpkg

- fix "would be freed" message when --pretend is *not* active
- add a verbose output that describes why a package is cleaned
	- newer binpkgs available
	- newer installed version available

# qsync

- rewrite to use new repos.conf standard

# qgrep

- make it use standard xarray instead of its own buf\_list
