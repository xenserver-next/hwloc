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

netloc_machine_t *netloc_machine_construct()
{
    netloc_machine_t *machine = NULL;

    /*
     * Allocate Memory
     */
    machine = (netloc_machine_t *)
        calloc(1, sizeof(netloc_machine_t));
    if( NULL == machine ) {
        fprintf(stderr, "ERROR: Memory error: machine cannot be allocated\n");
        return NULL;
    }

    return machine;
}

int netloc_machine_destruct(netloc_machine_t *machine)
{
    /*
     * Sanity Check
     */
    if( NULL == machine ) {
        fprintf(stderr, "Error: Detaching from a NULL pointer\n");
        return NETLOC_ERROR;
    }
    if (NULL != machine->topopath) {
        free(machine->topopath);
    }

    /** Allocation */
    if (NULL != machine->allocation) {
        //netloc_allocation_destruct(machine->allocation);
    }

    /** Network */
    if (NULL != machine->network) {
        if (NETLOC_NETWORK_TYPE_INFINIBAND == machine->network->transport_type)
            netloc_network_ib_destruct((netloc_network_ib_t *)machine->network);
        else {
            netloc_network_destruct(machine->network);
        }
    }
    
    /** Hwloc machine List */
    for (unsigned int t = 0; t < machine->nb_hwloc_topos; ++t) {
        if (machine->hwlocpaths && machine->hwlocpaths[t])
            free(machine->hwlocpaths[t]);
        if (machine->hwloc_topos && machine->hwloc_topos[t])
            hwloc_topology_destroy(machine->hwloc_topos[t]);
    }

    if (machine->hwlocpaths)
        free(machine->hwlocpaths);
    if (machine->hwloc_topos)
        free(machine->hwloc_topos);
    if (machine->hwloc_dir_path)
        free(machine->hwloc_dir_path);
    
    free(machine);

    return NETLOC_SUCCESS;
}
