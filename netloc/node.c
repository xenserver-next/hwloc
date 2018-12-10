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

static int node_get_current_name(char **pname)
{
    char name[HOST_NAME_MAX];

    gethostname(name, HOST_NAME_MAX);

    *pname = strdup(name);
    return NETLOC_SUCCESS;
}

int netloc_node_find(netloc_machine_t *machine, char *name, netloc_node_t **pnode)
{
    if (machine->explicit == NULL) {
        assert(0); // TODO implement
    }

    netloc_node_t *node;
    netloc_node_t *nodes = machine->explicit->nodes_by_name;
    HASH_FIND(hh2, nodes, name, strlen(name), node);
    *pnode = node;
    if (!node) {
        return NETLOC_ERROR_NODENOTFOUND;
    }

    return NETLOC_SUCCESS;
}

int netloc_node_current(netloc_machine_t *machine, netloc_node_t **pnode)
{
    char *name;
    node_get_current_name(&name);
    return netloc_node_find(machine, name, pnode);
}

int netloc_node_get_coords(netloc_machine_t *machine, netloc_filter_t *filter,
        netloc_node_t *node, int *coords)
{
    int ret;
    if (machine == NULL) {
        return NETLOC_ERROR_BADMACHINE;
    }

    int partition_idx = node->partitions[0];
    assert(node->nparts == 1); // TODO handle with filters

    int ncoords = machine->partitions[partition_idx].topology->ndims;

    /* Get node infos */
    netloc_position_t position = node->topo_positions[partition_idx];
    memcpy(coords, position.coords, ncoords*sizeof(int));

    return NETLOC_SUCCESS;
}

int netloc_node_get_partition(netloc_machine_t *machine,  netloc_filter_t *filter,
        netloc_node_t *node, int *ppartition)
{
    int partition_idx = node->partitions[0];
    assert(node->nparts == 1); // TODO handle with filters

    *ppartition = partition_idx;

    return NETLOC_SUCCESS;
}

/* If *ppartition_idx is -1, check partition is shared by all nodes */
int netloc_node_find_shared_partition(netloc_machine_t *machine,
        int num_nodes, netloc_node_t **node_list, int *ppartition_idx)
{
    int partition_idx = *ppartition_idx;
    int *shared_partitions = NULL;

    assert(num_nodes > 0);

    /* Build node list and find partition from node_names */
    if (num_nodes > 0) {
        shared_partitions = (int *)calloc(machine->npartitions, sizeof(int));
        for (int p = 0; p < machine->npartitions; p++) {
            shared_partitions[p] = 1;
        }

        if (machine->explicit) {
            /* Look for nodes by name */
            for (int n = 0; n < num_nodes; n++) {
                netloc_node_t *node = node_list[n];

                for (int p = 0; p < node->nparts; p++) {
                    shared_partitions[node->partitions[p]]++;
                }
            }

        } else {
            assert(0); // TODO
        }

    }

    /* Check partitions shared by all nodes */
    if (shared_partitions) {
        int nfound = 0;
        int found_part = -1;
        int same = 0;
        for (int p = 0; p < machine->npartitions; ++p) {
            if (shared_partitions[p] == num_nodes) {
                nfound++;
                found_part = p;
                if (found_part == partition_idx) {
                    same = 1;
                }
            }
        }

        if (partition_idx != -1) {
            assert(same);

        } else {
            assert(nfound == 1);
            *ppartition_idx = found_part;
        }
    }

}
