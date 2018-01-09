/*
 * Copyright © 2013-2014 University of Wisconsin-La Crosse.
 *                         All rights reserved.
 * Copyright © 2016-2018 Inria.  All rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 * See COPYING in top-level directory.
 *
 * $HEADER$
 */

#include <stdio.h>

#include <private/netloc.h>

netloc_network_explicit_t *netloc_network_explicit_construct()
{
    netloc_network_explicit_t *topology = NULL;

    /*
     * Allocate Memory
     */
    topology = (netloc_network_explicit_t *)
        calloc(1, sizeof(netloc_network_explicit_t));
    if ( NULL == topology ) {
        fprintf(stderr, "ERROR: Memory error: topology cannot be allocated\n");
        return NULL;
    }

    topology->nodesByHostname = NULL;
    utarray_init(&topology->nodes, &ut_ptr_icd);

    return topology;
}

int netloc_network_explicit_destruct(netloc_network_explicit_t *topology)
{
    /*
     * Sanity Check
     */
    if ( NULL == topology ) {
        fprintf(stderr, "ERROR: Detaching from a NULL pointer\n");
        return NETLOC_ERROR;
    }

    /* Nodes */
    HASH_CLEAR(hh2, topology->nodesByHostname);

    utarray_done(&topology->nodes);
    
    free(topology);

    return NETLOC_SUCCESS;
}
