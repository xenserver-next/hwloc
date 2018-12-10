#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <stdio.h>

#include <netloc.h>
#include <private/netloc.h>

netloc_machine_t *netloc_machine_construct(char *topodir)
{
    netloc_machine_t *machine = (netloc_machine_t*)malloc(sizeof(netloc_machine_t));
    if (NULL == machine) {
        return NULL;
    }

    machine->topodir = topodir;
    machine->topopath = NULL;
    machine->hwloc_dir_path = NULL;
    machine->hwlocpaths = NULL;
    machine->nb_hwloc_topos = 0;

    machine->partitions = NULL;
    machine->npartitions = 0;

    machine->explicit = NULL;

    machine->restriction = netloc_restriction_construct();

    return machine;
}

int netloc_machine_add_partitions(netloc_machine_t *machine,
        int npartitions, netloc_partition_t *partitions)
{
    machine->npartitions = npartitions;
    machine->partitions = partitions;
    return 0;
}

int netloc_machine_add_explicit(netloc_machine_t *machine)
{
    machine->explicit = netloc_explicit_construct();
    return 0;
}

netloc_topology_t * netloc_topology_construct(void)
{
    netloc_topology_t *topology;
    topology = (netloc_topology_t *)malloc(sizeof(netloc_topology_t));

    topology->type = -1;
    topology->dimensions = NULL;
    topology->costs = NULL;
    topology->ndims = 0;
    topology->subtopo = NULL;

    return topology;
}

netloc_explicit_t * netloc_explicit_construct(void)
{
    netloc_explicit_t *explicit;
    explicit = (netloc_explicit_t *)malloc(sizeof(netloc_explicit_t));
    explicit->nodes = NULL;
    explicit->nodes_by_name = NULL;
    explicit->physical_links = NULL;

    return explicit;
}

netloc_restriction_t * netloc_restriction_construct(void)
{
    netloc_restriction_t *restriction;
    restriction = malloc(sizeof(*restriction));
    restriction->num_nodes = 0;
    restriction->array_size = 0;
    restriction->nodes = NULL;

    return restriction;
}

int netloc_explicit_add_node( netloc_explicit_t *explicit, netloc_node_t *node)
{
    /* Check node is not already in table */
    netloc_node_t *node_tmp;
    HASH_FIND_STR(explicit->nodes, node->physical_id, node_tmp);
    assert(node_tmp == NULL);
    HASH_ADD_STR(explicit->nodes, physical_id, node);

    if (node->type == NETLOC_NODE_TYPE_HOST) {
        HASH_FIND(hh2, explicit->nodes_by_name, node->hostname,
                strlen(node->hostname), node_tmp);
        assert(node_tmp == NULL);
        HASH_ADD_KEYPTR(hh2, explicit->nodes_by_name,
                node->hostname, strlen(node->hostname), node);
    }

    return 0;
}


netloc_node_t * netloc_node_construct(void)
{
    netloc_node_t *node = (netloc_node_t*)malloc(sizeof(netloc_node_t));
    if (NULL == node) {
        return NULL;
    }
    memset(node, 0, sizeof(netloc_node_t));
    node->logical_id = -1;
    node->deco_position = -1;
    node->type = NETLOC_NODE_TYPE_INVALID;
    utarray_init(&node->physical_links, &ut_ptr_icd);
    node->hwloc_topo_idx = -1;
    node->in_restriction = 0;

    return node;
}

int netloc_node_destruct(netloc_node_t * node)
{
    utarray_done(&node->physical_links);

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

    /* Hostname */
    if (node->hostname)
        free(node->hostname);

    /* hwlocTopo: nothing to do beacause the pointer is stored also in the topology */

    free(node);

    return NETLOC_SUCCESS;
}


netloc_edge_t * netloc_edge_construct()
{
    static int cur_uid = 0;
    netloc_edge_t *edge = malloc(sizeof(netloc_edge_t));
    if( NULL == edge ) {
        return NULL;
    }
    memset(edge, 0, sizeof(netloc_edge_t));
    edge->id = cur_uid;
    cur_uid += 1;

    edge->partitions = NULL;
    utarray_init(&edge->physical_links, &ut_ptr_icd);

    return edge;
}

int netloc_edge_destruct(netloc_edge_t * edge)
{
    utarray_done(&edge->physical_links);
    free(edge->partitions);

    if (edge->nsubedges)
        free(edge->subnode_edges);
    free(edge);
    return NETLOC_SUCCESS;
}

netloc_physical_link_t * netloc_physical_link_construct(void)
{
    static int cur_uid = 0;
    netloc_physical_link_t *physical_link;

    physical_link = (netloc_physical_link_t*)
        malloc(sizeof(netloc_physical_link_t));
    if( NULL == physical_link ) {
        return NULL;
    }

    physical_link->id = cur_uid;
    cur_uid++;

    physical_link->src = NULL;
    physical_link->dest = NULL;

    physical_link->ports[0] = -1;
    physical_link->ports[1] = -1;

    physical_link->width = NULL;
    physical_link->speed = NULL;

    physical_link->edge = NULL;
    physical_link->other_way = NULL;

    physical_link->partitions = NULL;

    physical_link->gbits = -1;

    physical_link->description = NULL;

    return physical_link;
}

int netloc_physical_link_destruct(netloc_physical_link_t *link)
{
    free(link->width);
    free(link->description);
    free(link->speed);
    free(link->partitions);
    free(link);
    return NETLOC_SUCCESS;
}

