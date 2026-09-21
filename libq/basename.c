/*
 * Copyright 2010-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2010-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

#include "main.h"

#include <string.h>

#include "basename.h"

/* our own basename which does not modify its input */
const char *_basename(const char *filename)
{
  const char *p = strrchr(filename, '/');
  return p ? p + 1 : filename;
}

/* vim: set ts=2 sw=2 expandtab cino+=\:0 foldmethod=marker: */
