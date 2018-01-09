/*
 * Copyright © 2017-2018 Inria.  All rights reserved.
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
#include <stdio.h>

#include <private/autogen/config.h>
#include <private/netloc.h>
#include <netloc.h>

netloc_partition_t * netloc_partition_construct(const unsigned int id,
                                                const char *name)
{
    netloc_partition_t *partition = NULL;

    partition = (netloc_partition_t *) malloc(sizeof(netloc_partition_t));
    if (NULL == partition) {
        return NULL;
    }

    partition->id = id;
    partition->size = 0;
    partition->name = strdup(name);
    partition->desc = NULL;
    partition->topo = NULL;

    return partition;
}

int netloc_partition_destruct(netloc_partition_t * partition)
{
    /* Sanity check */
    if (NULL == partition) {
        fprintf(stderr, "Error: Detaching from a NULL pointer\n");
        return NETLOC_ERROR;
    }

    /* Free network_explicit */
    if (NULL != partition->desc) {
        netloc_network_explicit_destruct(partition->desc);
    }

    /* Free abstract topology */
    if (NULL != partition->topo) {
        netloc_arch_destruct(partition->topo);
    }

    free(partition->name);

    free(partition);

    return NETLOC_SUCCESS;
}

int netloc_edge_is_in_partition(const netloc_edge_t *edge,
                                const netloc_partition_t *partition)
{
    for (unsigned int p = 0; p < netloc_get_num_partitions(edge); ++p) {
        if (partition == netloc_get_partition(edge, p)) {
            return 1;
        }
    }
    return 0;
}

int netloc_node_is_in_partition(const netloc_node_t *node,
                                const netloc_partition_t *partition)
{
    for (unsigned int p = 0; p < netloc_get_num_partitions(node); ++p) {
        if (partition == netloc_get_partition(node, p)) {
            return 1;
        }
    }
    return 0;
}
