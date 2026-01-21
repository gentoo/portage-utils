gtree-1
=======
The gtree-1 format, is a container format for Gentoo repositories aiming
at better performance reading and processing on the repository.  The
format structure is inspired by GLEP-78, in that it is based on nested
POSIX tar archives.  The uncompressed outer archive has a name ending
with `gtree.tar`.

Rationale
---------
In portage-utils, operations are often performed on trees.  Trees can be
VDB (installed packages database) or repositories, with or without
metadata.  Traversal is done each time by iterating over directory
structures that make the tree, and reading files as necessary.  These
I/O operations prove to be costly, inferring a lot of latency, which in
the synchronous model of portage-utils simply means a lot of time.

To reduce this time, a sequential, large I/O instead of many scattered
I/O operations is necessary.  This can be achieved using a container
format, such as a tar archive.  The gtree-1 format is exactly that, a
single file that can (only) be read sequentually to extract values
necessary.

It must be noted that this I/O issue also inhibits itself in for
instance syncing a repository over rsync.  A lot of files must be
checked and copied, which all is much less intense for remote and
local disk subsystems if a single sequential I/O is used.  This is
analogous to how Object Storage systems operate successfully at the time
of this writing.  Thus, offering a gtree-1 archive as alternative for an
rsync tree, would in situations where bandwidth is not of concern offer
serious performance improvements.

A second issue that comes up with trees, is their consistency.  Given
that they are comprised of many files and directories, it is important
to have checks and measures to ensure all files are indeed as they are
supposed to be when looking at derived information, such as metadata
cache.  Of course integrity of the data, as in, that it is not tampered
with is an additional issue on top of this.  These checks and measures
require additional I/Os which further slow down any access to the data.
With a single gtree-1 archive, none of these problems exist, as the
archive itself is considered consitent, and cryptographic signatures
inside the files can be used to verify that in a single read.

Format
------
A gtree-1 archive is an uncompressed tar archive whose filename should
end with `.gtree.tar`.  The format used is the POSIX ustar format as
defined by POSIX.1-2017.  The archive contains the following files, in
order:
1. The archive identifier file `gtree-1`, required
2. The repository data file `repo.tar{compr}`, required but compr is optional
3. The signature for the repository data file `repo.tar{compr}.sig`, optional

### The archive identifier
The archive identifier file serves the purpose of identifying the
repository container format and its version.

The current identifier is `gtree-1`.  The file can have any contents,
and may be empty.  It is encouraged to use the program and its version
that created the archive as contents for this file.

Note that it is important that this member is the first in the archive
for implementations to efficiently establish compatibility.  It also
allows tools like file(1) to identify such file accordingly.

### The repository data
The actual repository data is stored in this entry.  It is another,
nested, POSIX ustar tar archive and it is highly recommended to use
Zstandard compression on this archive to reduce the overall size of the
gtree-1 container with the best read performance.  Consider the
following table:

| compressor | q -c         | qlist -IStv |
| ---------- | ------------ | ----------- |
| Zstd (19)  | 25.4MiB, 22s | 0.12s       |
| XZ         | 25.2MiB, 34s | 0.24s       |
| Bzip2      | 29.7MiB, 17s | 0.55s       |
| gzip       | 38.2MiB, 10s | 0.16s       |
| LZ4        | 57.5MiB,  9s | 0.13s       |

Zstandard compression levels 3 and 9 would take 8 and 12 seconds
respectively, but since the creation is done only once, the cost here
does not matter much, the resulting size though does.  XZ beats by a
fraction, but takes twice the time to decompress.  Since we do this
multiple times, it is really beneficial to use Zstandard in this case,
which only LZ4 comes close, but for more than twice the data size.

This archive contains the following members (all optional), in the order
mentioned below to allow a reader to stop reading/processing in most
cases:
1. repository, contains the repository name
2. caches/{CAT}/{PF}, metadata cache entries as single key-val data
3. ebuilds/{CAT}/{PN}/metadata.xml, PN metadata.xml file
4. ebuilds/{CAT}/{PN}/Manifest, Manifest entries for DIST files
5. ebuilds/{CAT}/{PN}/files/..., files for the ebuilds
6. ebuilds/{CAT}/{PN}/{PF}.ebuild, ebuild file
7. eclasses/{ECLASS}.eclass, eclass file

For each members above, grouping must be applied to CAT and PN (also as
part of PF), such that a reader can consider a category or package to be
finished as soon as it sees a next entry that is different.  Ordering of
version numbers is not required.  The simplest way to group the items is
by simply sorting the entries, which is sufficient because version
numbers do not have to be ordered.

Performance
-----------
While a single container archive has benefits in compression and
transferability, the main reason for was for performance.  Below follow
a few tests conducted on the same repository tree with full md5-cache,
or its equivalent gtree-1.

For traversing the tree, the invocation of `qlist -IStv` is used.  This
causes `qlist` to look for all ebuilds (t), and list them (I) with their
SLOT (S) and package version (v).  The SLOT retrieval requires a lookup
in the metadata cache for the normal repository tree, as its value
cannot be derived from the directory structure.  In this experiment,
qlist returned 30321 ebuilds in all cases.  Observe the following
numbers on a MacBook Air M4 with SSD storage:

|           | md5-cache | gtree |
| --------- | --------- | ----- |
| cold run  | 7.20s     | 0.17s |
| run 1     | 5.30s     | 0.17s |
| run 2     | 4.81s     | 0.17s |
| run 3     | 5.05s     | 0.17s |

As can be observed, the gtree results are far more stable, and
outperform the md5-cache runs on this system.

On disk and networked systems this difference is only worse.  Consider
the same on a NFS-mounted volume where the repository and/or gtree
resides:

|           | md5-cache | gtree |
| --------- | --------- | ----- |
| run 1     | 2:32m     | 0.64s |

While the gtree case is still under a second, the md5-cache case takes
minutes, because on NFS the I/O latency problem is magnified, while the
throughput for a single large file is just fine.

Conclusion is that even on the fastest disk subsystems, the gtree-1 format
appears very beneficial for overall performance.
