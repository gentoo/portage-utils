abs_top_builddir ?= $(PWD)/../..
abs_top_srcdir ?= $(PWD)/../..

ifdef AUTOTOOLS
this_subdir = tests/$(thisdir)
builddir = $(abs_top_builddir)/$(this_subdir)
srcdir = $(abs_top_srcdir)/$(this_subdir)
else
builddir = .
srcdir = .
endif

mkdir = $(if $(wildcard $(dir $(1))),:,mkdir -p "$(dir $(1))")

b = $(builddir)
s = $(srcdir)
atb = $(abs_top_builddir)
ats = $(abs_top_srcdir)

export b s atb ats

CFLAGS += -Wall
CPPFLAGS += -I$(abs_top_srcdir)
