/*
 * Copyright © 2019 Inria.  All rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 * See COPYING in top-level directory.
 *
 * $HEADER$
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <netloc.h>
#include <private/netloc.h>


int netloc_topology_get(netloc_machine_t *machine, netloc_filter_t *filter,
        int *pnlevels, int *pncoords, int **pdims, netloc_topology_type_t **ptypes,
        int **plevelidx,  int **pcosts)
{
    int ret;
    int nlevels;
    netloc_topology_type_t *types;
    int *levelidx;
    int *dims;
    int *costs;
    if (machine == NULL) {
        return NETLOC_ERROR_BADMACHINE;
    }

    // TODO choose partition_idx with filters
    int partition_idx = 0;
    netloc_partition_t partition = machine->partitions[partition_idx];
    netloc_topology_t *topology;

    /* Find sizes for allocations */
    int level = 0;
    int dimidx = 0;
    topology = partition.topology;
    while (topology) {
        dimidx += topology->ndims;
        level++;
        topology = topology->subtopo;
    }
    nlevels = level;

    /* Allocations */
    types = malloc(nlevels*sizeof(*types));
    levelidx = malloc((nlevels+1)*sizeof(*levelidx));
    dims = malloc(dimidx*sizeof(*dims));
    costs = malloc(dimidx*sizeof(*costs));

    /* Get topology infos */
    level = 0;
    dimidx = 0;
    topology = partition.topology;
    levelidx[0] = 0;
    while (topology) {
        types[level] = topology->type;
        memcpy(dims+dimidx, topology->dimensions, topology->ndims*sizeof(int));
        memcpy(costs+dimidx, topology->costs, topology->ndims*sizeof(int));

        dimidx += topology->ndims;
        levelidx[level+1] = dimidx;

        level++;
        topology = topology->subtopo;
    }

    *pnlevels = nlevels;
    *ptypes = types;
    *plevelidx = levelidx;
    *pdims = dims;
    *pcosts = costs;
    *pncoords = levelidx[nlevels];

    return NETLOC_SUCCESS;
}
