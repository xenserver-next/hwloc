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

#include <stdio.h>
#include <netloc/uthash.h>
#include <private/netloc.h>

int netloc_network_init(netloc_network_t *network)
{
    /* Sanity check */
    if ( NULL == network ) {
        fprintf(stderr, "Error: Paremeter error: network is NULL\n");
        return NETLOC_ERROR;
    }

    /*
     * Initialize the structure
     */
    network->type = NETLOC_TOPOLOGY_TYPE_INVALID;
    network->transport_type = NETLOC_NETWORK_TYPE_INVALID;
    network->nodes = NULL;
    network->physical_links = NULL;
    
    return NETLOC_SUCCESS;
}

int netloc_network_destruct(netloc_network_t *network)
{
    /*
     * Sanity Check
     */
    if ( NULL == network ) {
        fprintf(stderr, "Error: Detaching from a NULL pointer\n");
        return NETLOC_ERROR;
    }

    /** Partition List */
    netloc_partition_t *part, *part_tmp;
    netloc_network_iter_partitions(network, part, part_tmp) {
        HASH_DEL(network->partitions, part);
        netloc_partition_destruct(part);
    }

    /** Physical links */
    if (NULL != network->physical_links) {
        netloc_physical_link_t *link, *link_tmp;
        HASH_ITER(hh, network->physical_links, link, link_tmp) {
            HASH_DEL(network->physical_links, link);
            netloc_physical_link_destruct(link);
        }
    }

    /** Nodes */
    if (network->nodes) {
        netloc_node_t *node, *node_tmp;
        netloc_network_iter_nodes(network, node, node_tmp) {
            HASH_DEL(network->nodes, node);
            netloc_node_destruct(node);
        }
    }

    free(network);
    
    return NETLOC_SUCCESS;
}

int netloc_network_find_reverse_edges(netloc_network_t *network)
{
    netloc_node_t *node, *node_tmp;
    netloc_network_iter_nodes(network, node, node_tmp) {
        netloc_edge_t *edge, *edge_tmp;
        netloc_node_iter_edges(node, edge, edge_tmp) {
            netloc_node_t *dest = edge->dest;
            if (dest > node) {
                netloc_edge_t *reverse_edge;
                HASH_FIND_STR(dest->edges, node->physical_id, reverse_edge);
                if (NULL == reverse_edge) {
                    return NETLOC_ERROR;
                }
                edge->other_way = reverse_edge;
                reverse_edge->other_way = edge;
            }
        }
    }
    return NETLOC_SUCCESS;
}
