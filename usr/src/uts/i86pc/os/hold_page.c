/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/hold_page.h>

#if defined(__xpv)
#include <sys/hypervisor.h>
#endif

/*ARGSUSED*/
int
plat_hold_page(pfn_t pfn, int lock, page_t **pp_ret)
{
#if defined(__xpv)
	page_t *pp = page_numtopp_nolock(pfn);

	if (pp == NULL)
		return (PLAT_HOLD_FAIL);

	if (lock == PLAT_HOLD_LOCK) {
		ASSERT(pp_ret != NULL);
		if (page_trylock(pp, SE_EXCL) == 0)
			return (PLAT_HOLD_FAIL);
	}

	if (mfn_list[pfn] == MFN_INVALID) {
		/* We failed - release the lock if we grabbed it earlier */
		if (lock == PLAT_HOLD_LOCK) {
			page_unlock(pp);
		}
		return (PLAT_HOLD_FAIL);
	}
	if (lock == PLAT_HOLD_LOCK)
		*pp_ret = pp;
#endif	/* __xpv */

	return (PLAT_HOLD_OK);
}

/*ARGSUSED*/
void
plat_release_page(page_t *pp)
{
#if defined(__xpv)
	ASSERT((pp != NULL) && PAGE_LOCKED(pp));
	page_unlock(pp);
#endif
}
