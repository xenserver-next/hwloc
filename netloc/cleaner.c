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

#include <private/netloc.h>
#include <netloc/utarray.h>
#include <private/utils/cleaner.h>

static void cleaner_dtor(void *e);
static void cleaner_arr_dtor(void *e);
static void cleaner_arr_init(void *e);

typedef struct {
    void *elt;
    dtor_f *dtor;
} cleaner_t;

static const UT_icd cleaner_icd =
    {sizeof(cleaner_t), NULL, NULL, cleaner_dtor};

static void cleaner_dtor(void *e) {
    cleaner_t *elt = (cleaner_t *)e;
    if (elt->dtor) (void) elt->dtor(elt->elt);
    else free(elt->elt);
}

static const UT_icd cleaner_arr_icd =
    {sizeof(UT_array), cleaner_arr_init, NULL, cleaner_arr_dtor};

static void cleaner_arr_init(void *e) {
    UT_array *arr = (UT_array *)e;
    utarray_init(arr, &cleaner_icd);
}

static void cleaner_arr_dtor(void *e) {
    UT_array *arr = (UT_array *)e;
    utarray_done(arr);
}

static UT_array cleaners = {0};

void netloc_cleaner_init()
{
    if (0 == cleaners.n) {
        utarray_init(&cleaners, &cleaner_arr_icd);
    }
    utarray_extend_back(&cleaners);
}

void netloc_cleaner_done()
{
    assert(0 < cleaners.n);
    utarray_pop_back(&cleaners);
    if (0 == utarray_len(&cleaners)) {
        utarray_done(&cleaners);
    }
}

void _netloc_cleaner_add(void *elt, dtor_f *dtor)
{
    cleaner_t new = { elt, dtor };
    UT_array *current = (UT_array *)utarray_back(&cleaners);
    utarray_push_back(current, &new);
}
