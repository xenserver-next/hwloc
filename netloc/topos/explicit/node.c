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
#include <netloc.h>

netloc_node_t * netloc_node_construct()
{
    netloc_node_t *node = NULL;

    node = (netloc_node_t*)malloc(sizeof(netloc_node_t));
    if (NULL == node) {
        return NULL;
    }
    memset(node, 0, sizeof(netloc_node_t));
    node->logical_id = -1;
    node->type = NETLOC_NODE_TYPE_INVALID;
    utarray_init(&node->physical_links, &ut_ptr_icd);
    utarray_init(&node->partitions, &ut_ptr_icd);

    return node;
}

int netloc_node_destruct(netloc_node_t * node)
{
    utarray_done(&node->physical_links);
    utarray_done(&node->partitions);

    /* Description */
    if (node->description)
        free(node->description);

    /* Edges */
    netloc_edge_t *edge, *edge_tmp;
    HASH_ITER(hh, node->edges, edge, edge_tmp) {
        HASH_DEL(node->edges, edge);  /* delete; edge advances to next */
        netloc_edge_destruct(edge);
    }

    /* Subnodes */
    /* WARNING: The subnodes are to be removed from the hashtable PRIOR TO THIS CALL */
    if (0 < node->nsubnodes)
        free(node->subnodes);

    /* Paths */
    netloc_path_t *path, *path_tmp;
    HASH_ITER(hh, node->paths, path, path_tmp) {
        HASH_DEL(node->paths, path);  /* delete; path advances to next */
        netloc_path_destruct(path);
    }

    /* Hostname */
    if (node->hostname)
        free(node->hostname);

    /* hwlocTopo: nothing to do beacause the pointer is stored also in the topology */

    free(node);

    return NETLOC_SUCCESS;
}

char *netloc_node_pretty_print(const netloc_node_t *node)
{
    char * str = NULL;

    asprintf(&str, " [%23s]/[%ld] -- %s (%d links)",
             node->physical_id,
             node->logical_id,
             node->description,
             utarray_len(&node->physical_links));

    return str;
}
