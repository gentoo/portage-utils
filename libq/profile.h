/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _PROFILE_H
#define _PROFILE_H 1

typedef void *(q_profile_callback_t)(void *, char *);
void *q_profile_walk(
		const char *file, q_profile_callback_t callback,
		void *data);

#endif
