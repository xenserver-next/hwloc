/*
 * Copyright © 2017      Inria.  All rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 * See COPYING in top-level directory.
 *
 * $HEADER$
 */

#ifndef _UTILS_CLEANER_H_
#define _UTILS_CLEANER_H_

#include <netloc/utarray.h>
#include <private/netloc.h>

extern void netloc_cleaner_init();

extern void _netloc_cleaner_add(void *elt, dtor_f *dtor);

#define netloc_cleaner_add(elt, dtor)           \
    _netloc_cleaner_add(elt, (dtor_f *) dtor)

extern void netloc_cleaner_done();

#endif /* _UTILS_CLEANER_H_ */
