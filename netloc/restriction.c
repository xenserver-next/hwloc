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

int netloc_restriction_add_node(netloc_machine_t *machine, netloc_node_t *node)
{
    netloc_restriction_t *restriction = machine->restriction;
    assert(restriction);

    if (node->in_restriction)
        return NETLOC_ALRD_IN_RESTRICT;

    node->in_restriction = 1;

    if (restriction->num_nodes == restriction->array_size) {
        if (!restriction->array_size) {
            restriction->array_size = 32;
        } else {
            restriction->array_size *= 2;
        }
        restriction->nodes = realloc(restriction->nodes,
                restriction->array_size*sizeof(*restriction->nodes));
    }
    restriction->nodes[restriction->num_nodes] = node;
    restriction->num_nodes++;

    return NETLOC_SUCCESS;
}

int netloc_restriction_set_node_list(netloc_machine_t *machine, int num_nodes,
        netloc_node_t **nodes)
{
    assert(machine->restriction);
    assert(!machine->restriction->nodes);

    netloc_restriction_t *restriction = machine->restriction;

    restriction->num_nodes = num_nodes;
    restriction->array_size = num_nodes;
    restriction->nodes = nodes;

    for (int n = 0; n < num_nodes; n++) {
        assert(nodes[n]->in_restriction == 0);
        nodes[n]->in_restriction = 1;
    }

    machine->restriction = restriction;

    return NETLOC_SUCCESS;
}
