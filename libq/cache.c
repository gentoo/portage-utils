/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <xalloc.h>

#include "atom.h"
#include "cache.h"

#ifdef EBUG
static void
cache_dump(portage_cache *cache)
{
	if (!cache)
		errf("Cache is empty !");

	printf("DEPEND     : %s\n", cache->DEPEND);
	printf("RDEPEND    : %s\n", cache->RDEPEND);
	printf("SLOT       : %s\n", cache->SLOT);
	printf("SRC_URI    : %s\n", cache->SRC_URI);
	printf("RESTRICT   : %s\n", cache->RESTRICT);
	printf("HOMEPAGE   : %s\n", cache->HOMEPAGE);
	printf("LICENSE    : %s\n", cache->LICENSE);
	printf("DESCRIPTION: %s\n", cache->DESCRIPTION);
	printf("KEYWORDS   : %s\n", cache->KEYWORDS);
	printf("INHERITED  : %s\n", cache->INHERITED);
	printf("IUSE       : %s\n", cache->IUSE);
	printf("CDEPEND    : %s\n", cache->CDEPEND);
	printf("PDEPEND    : %s\n", cache->PDEPEND);
	printf("PROVIDE    : %s\n", cache->PROVIDE);
	printf("EAPI       : %s\n", cache->EAPI);
	printf("PROPERTIES : %s\n", cache->PROPERTIES);
	if (!cache->atom) return;
	printf("CATEGORY   : %s\n", cache->atom->CATEGORY);
	printf("PN         : %s\n", cache->atom->PN);
	printf("PV         : %s\n", cache->atom->PV);
	printf("PVR        : %s\n", cache->atom->PVR);
}
#endif

static portage_cache *cache_read_file_pms(const char *file);
static portage_cache *cache_read_file_md5(const char *file);

portage_cache *
cache_read_file(int portcachedir_type, const char *file)
{
	if (portcachedir_type == CACHE_METADATA_MD5)
		return(cache_read_file_md5(file));
	else if (portcachedir_type == CACHE_METADATA_PMS)
		return(cache_read_file_pms(file));
	warn("Unknown metadata cache type!");
	return NULL;
}

static portage_cache *
cache_read_file_pms(const char *file)
{
	struct stat s;
	char *ptr;
	FILE *f;
	portage_cache *ret = NULL;
	size_t len;

	if ((f = fopen(file, "r")) == NULL)
		goto err;

	if (fstat(fileno(f), &s) != 0)
		goto err;
	len = sizeof(*ret) + s.st_size + 1;
	ret = xzalloc(len);
	ptr = (char*)ret;
	ret->_data = ptr + sizeof(*ret);
	if ((off_t)fread(ret->_data, 1, s.st_size, f) != s.st_size)
		goto err;

	ret->atom = atom_explode(file);
	ret->DEPEND = ret->_data;
#define next_line(curr, next) \
	if ((ptr = strchr(ret->curr, '\n')) == NULL) { \
		warn("Invalid cache file '%s'", file); \
		goto err; \
	} \
	ret->next = ptr+1; \
	*ptr = '\0';
	next_line(DEPEND, RDEPEND)
	next_line(RDEPEND, SLOT)
	next_line(SLOT, SRC_URI)
	next_line(SRC_URI, RESTRICT)
	next_line(RESTRICT, HOMEPAGE)
	next_line(HOMEPAGE, LICENSE)
	next_line(LICENSE, DESCRIPTION)
	next_line(DESCRIPTION, KEYWORDS)
	next_line(KEYWORDS, INHERITED)
	next_line(INHERITED, IUSE)
	next_line(IUSE, CDEPEND)
	next_line(CDEPEND, PDEPEND)
	next_line(PDEPEND, PROVIDE)
	next_line(PROVIDE, EAPI)
	next_line(EAPI, PROPERTIES)
#undef next_line
	ptr = strchr(ptr+1, '\n');
	if (ptr == NULL) {
		warn("Invalid cache file '%s' - could not find end of cache data", file);
		goto err;
	}
	*ptr = '\0';

	fclose(f);

	return ret;

err:
	if (f) fclose(f);
	if (ret) cache_free(ret);
	return NULL;
}

static portage_cache *
cache_read_file_md5(const char *file)
{
	struct stat s;
	char *ptr, *endptr;
	FILE *f;
	portage_cache *ret = NULL;
	size_t len;

	if ((f = fopen(file, "r")) == NULL)
		goto err;

	if (fstat(fileno(f), &s) != 0)
		goto err;
	len = sizeof(*ret) + s.st_size + 1;
	ret = xzalloc(len);
	ptr = (char*)ret;
	ret->_data = ptr + sizeof(*ret);
	if ((off_t)fread(ret->_data, 1, s.st_size, f) != s.st_size)
		goto err;

	ret->atom = atom_explode(file);

	/* We have a block of key=value\n data.
	 * KEY=VALUE\n
	 * Where KEY does NOT contain:
	 * \0 \n =
	 * And VALUE does NOT contain:
	 * \0 \n
	 * */
#define assign_var_cmp(keyname, cmpkey) \
	if (strncmp(keyptr, cmpkey, strlen(cmpkey)) == 0) { \
		ret->keyname = valptr; \
		continue; \
	}
#define assign_var(keyname) \
	assign_var_cmp(keyname, #keyname);

	ptr = ret->_data;
	endptr = strchr(ptr, '\0');
	if (endptr == NULL) {
			warn("Invalid cache file '%s' - could not find end of cache data", file);
			goto err;
	}

	while (ptr != NULL && ptr != endptr) {
		char *keyptr;
		char *valptr;
		keyptr = ptr;
		valptr = strchr(ptr, '=');
		if (valptr == NULL) {
			warn("Invalid cache file '%s' val", file);
			goto err;
		}
		*valptr = '\0';
		valptr++;
		ptr = strchr(valptr, '\n');
		if (ptr == NULL) {
			warn("Invalid cache file '%s' key", file);
			goto err;
		}
		*ptr = '\0';
		ptr++;

		assign_var(CDEPEND);
		assign_var(DEPEND);
		assign_var(DESCRIPTION);
		assign_var(EAPI);
		assign_var(HOMEPAGE);
		assign_var(INHERITED);
		assign_var(IUSE);
		assign_var(KEYWORDS);
		assign_var(LICENSE);
		assign_var(PDEPEND);
		assign_var(PROPERTIES);
		assign_var(PROVIDE);
		assign_var(RDEPEND);
		assign_var(RESTRICT);
		assign_var(SLOT);
		assign_var(SRC_URI);
		assign_var(DEFINED_PHASES);
		assign_var(REQUIRED_USE);
		assign_var(_eclasses_);
		assign_var(_md5_);
		warn("Cache file '%s' with unknown key %s", file, keyptr);
	}
#undef assign_var
#undef assign_var_cmp

	fclose(f);

	return ret;

err:
	if (f) fclose(f);
	if (ret) cache_free(ret);
	return NULL;
}

void
cache_free(portage_cache *cache)
{
	if (!cache)
		errf("Cache is empty !");
	atom_implode(cache->atom);
	free(cache);
}
