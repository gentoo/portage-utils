this_subdir = tests/quse
b = $(abs_top_builddir)/$(this_subdir)
s = $(abs_top_srcdir)/$(this_subdir)

check-local:
	$(Q)$(s)/dotest

clean-local:
	rm -f $(b)/PORTDIR-* $(b)/*.squashfs
