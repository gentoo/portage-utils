SUBDIRS = \
	atom_compare atom_explode copy_file mkdir rmspace \
	qatom qcheck qdepends qfile qlist qlop qmerge qtbz2 quse qxpak \
	install profile

if QMANIFEST_ENABLED
SUBDIRS += qmanifest
endif

SUBDIRS += source

%_subdir:
	$(Q)cd $(@:_subdir=) || exit 1; \
	out=`$(MAKE) check 2>&1`; \
	ret=$$?; \
	echo "$$out"; \
	exit $$ret
