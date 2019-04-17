this_subdir = tests/atom_compare
b = $(abs_top_builddir)/$(this_subdir)
s = $(abs_top_srcdir)/$(this_subdir)
atb = $(abs_top_builddir)
mkdir = $(if $(wildcard $(dir $(1))),:,mkdir -p "$(dir $(1))")

check-local: static static.q portage

t = \
	@set -f && \
	$(call mkdir,$(b)) && \
	good=`echo $< | sed 's:.tests$$:.good:'`; \
	out=$(@:.tests=); \
	ret=0; \
	$(realpath $(lastword $^)) $(1) `sed 's:\#.*::' $<` > $$out || ret=$$?; \
	case $$ret in \
	0) \
		if diff -u $$good $$out; then \
			echo "PASS atom_compare $$out"; \
		else \
			echo "FAIL atom_compare $$out"; \
			ret=1; \
		fi;; \
	77) echo "SKIP atom_compare $$out"; ret=0;; \
	*) echo "FAIL atom_compare $$out";; \
	esac; \
	[ $$ret -eq 0 ] && rm -f $$out; \
	exit $$ret

static: $(s)/static.tests $(atb)/q
	$(call t,atom -c)

static.q: $(s)/static.q.tests $(atb)/q
	$(call t,atom -c)

portage: $(s)/static.tests $(s)/atom-compare.py
	$(call t)

clean-local:
	rm -f $(b)/static $(b)/static.q $(b)/portage

.PHONY: all check clean test
