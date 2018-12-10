/*
 * Copyright © 2016-2017 Inria.  All rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 * See COPYING in top-level directory.
 *
 * $HEADER$
 */

#define _GNU_SOURCE	   /* See feature_test_macros(7) */
#include <stdio.h>

#include <netloc.h>
#include <private/netloc.h>

#include "tree.h"

static void add_node_idx(int *idx, netloc_node_t *node, int partition_idx)
{
   netloc_position_t *position =
      node->topo_positions+partition_idx;
   assert(!position->coords);
   position->coords = &position->idx;
   position->idx = *idx;
   (*idx)++;
}

int netloc_arch_build(netloc_machine_t *machine)
{
    // XXX TODO choose right function depending on network
    partition_topology_to_tleaf(machine);
    //partition_topology_to_deco(machine);

    /* If topology was not recognized, number nodes */
    for (int p = 0; p < machine->npartitions; p++) {
        if (machine->partitions[p].topology)
            continue;

        int idx = 0;
        netloc_node_t *node, *node_tmp;
        HASH_ITER(hh, machine->explicit->nodes, node, node_tmp) {
            add_node_idx(&idx, node, p);
        }
    }

    return NETLOC_SUCCESS;
}
