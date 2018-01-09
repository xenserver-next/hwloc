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

#include <private/netloc.h>

netloc_network_ib_t *netloc_network_ib_construct(const char *subnet_id)
{
    netloc_network_ib_t *topology = NULL;

    /*
     * Allocate Memory
     */
    topology = (netloc_network_ib_t *)
        calloc(1, sizeof(netloc_network_ib_t));
    if ( NULL == topology ) {
        fprintf(stderr, "Error: Memory error: topology cannot be allocated\n");
        return NULL;
    }

    if ( NETLOC_SUCCESS != netloc_network_ib_init(subnet_id, topology) ) {
        netloc_network_destruct(&topology->super);
        return NULL;
    }
    
    return topology;
}

int netloc_network_ib_init(const char *subnet_id, netloc_network_ib_t *topo)
{
    /* Sanity check */
    if ( NULL == subnet_id ) {
        fprintf(stderr, "Error: Parameter error: subnet is NULL\n");
        return NETLOC_ERROR;
    }
    if ( NETLOC_SUCCESS != netloc_network_init(&(topo->super)) ) {
        fprintf(stderr, "Error: unable to initialize network\n");
        return NETLOC_ERROR;
    }
    
    /*
     * Initialize the structure
     */
    topo->super.type = NETLOC_TOPOLOGY_TYPE_TREE;
    topo->super.transport_type = NETLOC_NETWORK_TYPE_INFINIBAND;
    topo->subnet_id = strdup(subnet_id);
    if (NULL == topo->subnet_id) {
        fprintf(stderr, "Error: Memory error: subnet id cannot be retained\n");
        free(topo->subnet_id);
        return NETLOC_ERROR;
    }
    
    return NETLOC_SUCCESS;
}

int netloc_network_ib_destruct(netloc_network_ib_t *topo)
{
    /*
     * Sanity Check
     */
    if (NETLOC_NETWORK_TYPE_INFINIBAND != topo->super.transport_type) {
        fprintf(stderr, "Error: Parameter of wrong topology type\n");
        return NETLOC_ERROR;
    }
    
    if (topo->subnet_id)
        free(topo->subnet_id);

    return netloc_network_destruct(&(topo->super));
}
