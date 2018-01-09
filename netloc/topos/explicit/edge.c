/*
 * Copyright © 2013-2014 University of Wisconsin-La Crosse.
 *                         All rights reserved.
 * Copyright © 2013      Cisco Systems, Inc.  All rights reserved.
 * Copyright © 2015-2018 Inria.  All rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 * See COPYING in top-level directory.
 *
 * $HEADER$
 */

#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <stdlib.h>

#include <private/autogen/config.h>
#include <private/netloc.h>

static int cur_uid = 0;

netloc_edge_t * netloc_edge_construct()
{
    netloc_edge_t *edge = NULL;

    edge = (netloc_edge_t*)malloc(sizeof(netloc_edge_t));
    if( NULL == edge ) {
        return NULL;
    }
    memset(edge, 0, sizeof(netloc_edge_t));
    edge->id = cur_uid;
    cur_uid += 1;

    utarray_init(&edge->partitions, &ut_ptr_icd);
    utarray_init(&edge->physical_links, &ut_ptr_icd);

    return edge;
}

char * netloc_edge_pretty_print(const netloc_edge_t *edge)
{
    char * str = NULL;

    asprintf(&str, " [%23s]/[%ld] -- [%23s]/[%ld] (%f gbits, %d links)",
             edge->node->physical_id, edge->node->logical_id,
             edge->dest->physical_id, edge->dest->logical_id,
             edge->total_gbits, utarray_len(&edge->physical_links));

    return str;
}

int netloc_edge_destruct(netloc_edge_t * edge)
{
    utarray_done(&edge->physical_links);
    utarray_done(&edge->partitions);

    if (edge->nsubedges)
        free(edge->subnode_edges);
    free(edge);
    return NETLOC_SUCCESS;
}

void netloc_edge_reset_uid(void)
{
    cur_uid = 0;
}
