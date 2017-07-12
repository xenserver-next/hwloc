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

#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <stdlib.h>

#include <private/autogen/config.h>
#include <private/netloc.h>
#include <netloc.h>

netloc_partition_t * netloc_partition_construct(unsigned int id, char *name)
{
    netloc_partition_t *partition = NULL;

    partition = (netloc_partition_t*)malloc(sizeof(netloc_partition_t));
    if (NULL == partition) {
        return NULL;
    }

    partition->id = id;
    partition->name = strdup(name);
    utarray_new(partition->nodes, &ut_ptr_icd);
    utarray_new(partition->edges, &ut_ptr_icd);
        
    return partition;
}

int netloc_partition_destruct(netloc_partition_t * partition)
{
    free(partition->name);
    if (partition->nodes)
        utarray_free(partition->nodes);
    if (partition->edges)
        utarray_free(partition->edges);
    
    free(partition);
    
    return NETLOC_SUCCESS;
}

int netloc_edge_is_in_partition(netloc_edge_t *edge, netloc_partition_t *partition)
{
    netloc_partition_iter_edges(partition, pedge) {
        if (edge == *pedge)
            return 1;
    }
    return 0;
}

int netloc_node_is_in_partition(netloc_node_t *node, netloc_partition_t *partition)
{
    netloc_partition_iter_nodes(partition, pnode) {
        if (node == *pnode)
            return 1;
    }
    return 0;
}
