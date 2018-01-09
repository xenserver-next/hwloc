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

/*
 * This file provides general function to translate from utils_*_t
 * datatypes to netloc_*_t.
 */

#define _GNU_SOURCE         /* See feature_test_macros(7) */

#include <private/netloc.h>
#include <netloc.h>
#include <private/autogen/config.h>
#include <private/utils/netloc.h>

#include <netloc/uthash.h>
#include <netloc/utarray.h>

extern int netloc_topo_set_tree_topology(netloc_partition_t *partition);

static netloc_hwloc_topology_t *hwloc_topologies;

static void set_hwloc_topology(netloc_node_t *node, netloc_machine_t *machine)
{
    char *buff;
    int buffSize;
    FILE *fxml;
    /* If "ANONYMOUS-1" there can't be any topology file */
    if (0 == strncmp("ANONYMOUS", node->hostname, 9)) return;
    /* Check if file exists */
    buffSize = asprintf(&buff, "%s/%s.diff.xml",
                        machine->hwloc_dir_path, node->hostname);
    if (0 > buffSize)
        buff = NULL;
    if (!(fxml = fopen(buff, "r"))) {
        buffSize -= 8;
        strcpy(&buff[buffSize], "xml");
        if (!(fxml = fopen(buff, "r"))) {
            fprintf(stderr, "Hwloc file absent: %s\n", buff);
            buffSize = 0;
        } else
            fclose(fxml);
    } else
        fclose(fxml);
    /* If file exists */
    if (0 < buffSize) {
        /* Check if already defined hwloc_topo */
        netloc_hwloc_topology_t *hwloc_topo;
        char *topo_name = strdup(1 + strrchr(buff, '/'));
        HASH_FIND_STR(hwloc_topologies, topo_name, hwloc_topo);
        if (!hwloc_topo) {
            hwloc_topo = (netloc_hwloc_topology_t *)
                malloc(sizeof(netloc_hwloc_topology_t));
            hwloc_topo->hwloc_topo_idx = HASH_COUNT(hwloc_topologies);
            hwloc_topo->path = topo_name;
            HASH_ADD_STR(hwloc_topologies, path, hwloc_topo);
        }
        node->hwloc_topo_idx = 1 + hwloc_topo->hwloc_topo_idx;
    }
    free(buff); buff = NULL;
}

static netloc_node_t *
create_node(const utils_node_t *node, netloc_machine_t *machine,
            int add_to_part)
{
    netloc_network_t *network = machine->network;
    netloc_node_t *topo_node = netloc_node_construct();
    assert(topo_node);
    /* Copy attributes */
    topo_node->type = node->type;
    topo_node->logical_id = node->logical_id;
    strncpy(topo_node->physical_id, node->physical_id, 20);
    if (node->hostname && 0 < strlen(node->hostname)) {
        topo_node->hostname = strdup(node->hostname);
        /* Set hwloc_path iif node is a host */
        if (NETLOC_NODE_TYPE_HOST == node->type) {
            set_hwloc_topology(topo_node, machine);
        }
    } else if (NETLOC_NODE_TYPE_HOST == node->type) {
        fprintf(stderr, "WARN: Host node with address %s has no hostname\n",
                node->physical_id);
    }
    if (node->description) {
        topo_node->description = strdup(node->description);
    }
    /* Add the node to the network */
    HASH_ADD_STR(network->nodes, physical_id, topo_node);
    /* Add potential subnodes */
    unsigned int nsubnodes = HASH_COUNT(node->subnodes);
    if (nsubnodes) {
        unsigned int i = 0;
        utils_node_t *subnode, *subnode_tmp;
        topo_node->nsubnodes = nsubnodes;
        topo_node->subnodes =
            (netloc_node_t **) malloc(sizeof(netloc_node_t *[nsubnodes]));
        HASH_ITER(hh, node->subnodes, subnode, subnode_tmp) {
            netloc_node_t *topo_subnode = create_node(subnode, machine, 0);
            topo_subnode->virtual_node = topo_node;
            topo_node->subnodes[i] = topo_subnode;
            i += 1;
        }
    }
    /* Add to its partitions */
    netloc_partition_t *topo_part, *topo_part_tmp;
    netloc_network_iter_partitions(network, topo_part, topo_part_tmp) {
        if (node->partitions && node->partitions[topo_part->id]) {
            utarray_push_back(&topo_node->partitions, &topo_part);
            if (add_to_part) /* No subnode in part->desc->nodes */
                utarray_push_back(&topo_part->desc->nodes, &topo_node);
            if (topo_node->hostname) {
                netloc_network_explicit_add_node(topo_part->desc, topo_node);
            }
        }
    }
    return topo_node;
}

static netloc_edge_t *
create_edges(const utils_edge_t *edge, netloc_network_t *network)
{
    netloc_edge_t *topo_edge = netloc_edge_construct();
    if (!topo_edge)
        return NULL;
    /* Copy attributes */
    topo_edge->total_gbits = edge->total_gbits;
    assert(edge);
    assert(edge->reverse_edge);
    HASH_FIND_STR(network->nodes, edge->dest, topo_edge->dest);
    assert(topo_edge->dest);
    HASH_FIND_STR(network->nodes, edge->reverse_edge->dest, topo_edge->node);
    assert(topo_edge->node);
    /* Add topo_edge to its topo_node */
    HASH_ADD_STR(topo_edge->node->edges, dest->physical_id, topo_edge);
    /* Check if reverse edge is already defined */
    HASH_FIND_STR(topo_edge->dest->edges, topo_edge->node->physical_id,
                  topo_edge->other_way);
    if (topo_edge->other_way)
        topo_edge->other_way->other_way = topo_edge;
    unsigned int nsubedges =
        edge_is_virtual(edge) ? utarray_len(&edge->subedges) : (unsigned) 0;
    if (nsubedges) {
        topo_edge->nsubedges = nsubedges;
        topo_edge->subnode_edges =
            (netloc_edge_t **) malloc(sizeof(netloc_edge_t *[nsubedges]));
        assert(topo_edge->subnode_edges);
        for (unsigned int i = 0; i < utarray_len(&edge->subedges); ++i) {
            utils_edge_t *subedge =
                *(utils_edge_t **) utarray_eltptr(&edge->subedges, i);
            netloc_edge_t *topo_subedge =
                create_edges(subedge, network);
            assert(topo_subedge);
            topo_edge->subnode_edges[i] = topo_subedge;
        }
    }
    return topo_edge;
}

static inline netloc_physical_link_t *
create_physical_link(const utils_physical_link_t *link,
                     netloc_network_t *network)
{
    netloc_physical_link_t *topo_link = netloc_physical_link_construct();
    if (!topo_link)
        return NULL;
    /* Copy attributes */
    topo_link->id = link->int_id;
    topo_link->gbits = link->gbits;
    topo_link->width = strdup(link->width);
    topo_link->speed = strdup(link->speed);
    topo_link->description = strdup(link->description);
    topo_link->other_way_id = link->other_link->int_id;
    memcpy(topo_link->ports, link->ports, sizeof(int[2]));
    HASH_FIND_STR(network->nodes, link->parent_node->physical_id,
                  topo_link->src);
    assert(topo_link->src);
    HASH_FIND_STR(network->nodes, link->dest->physical_id, topo_link->dest);
    assert(topo_link->dest);
    HASH_FIND_STR(topo_link->src->edges, link->parent_edge->dest,
                  topo_link->edge);
    assert(topo_link->edge);
    HASH_FIND(hh, network->physical_links, &topo_link->other_way_id,
              sizeof(unsigned long long int), topo_link->other_way);
    if (topo_link->other_way)
        topo_link->other_way->other_way = topo_link;
    /* Add link to the proper structures */
    HASH_ADD(hh, network->physical_links, id, sizeof(unsigned long long int),
             topo_link);
    utarray_push_back(&topo_link->src->physical_links, &topo_link);
    if (topo_link->src->nsubnodes) {
        /* Add link to the parent virtual node in case of a subnode */
        utarray_push_back(&topo_link->src->virtual_node->physical_links,
                          &topo_link);
    }
    utarray_push_back(&topo_link->edge->physical_links, &topo_link);
    return topo_link;
}

int create_netloc_machine(netloc_machine_t *machine, const utils_node_t *nodes,
                          const UT_array *partitions)
{
    /* Sanity check */
    if (NULL == machine) return NETLOC_ERROR;

    hwloc_topologies = NULL;
    unsigned int npartitions = utarray_len(partitions);
    netloc_network_t *network = machine->network;

    /* Create partitions */
    netloc_partition_t *topo_partition;
    for (unsigned p = 0; p < npartitions; ++p) {
        utils_partition_t *partition =
            *(utils_partition_t **) utarray_eltptr(partitions, p);
        topo_partition = netloc_partition_construct(p, partition->name);
        HASH_ADD_STR(network->partitions, name, topo_partition);
        topo_partition->desc = netloc_network_explicit_construct();
    }
    /* Add nodes */
    const utils_node_t *node, *node_tmp;
    HASH_ITER(hh, nodes, node, node_tmp) {
        netloc_node_t *topo_node = create_node(node, machine, 1);
    }
    /* Add edges */
    HASH_ITER(hh, nodes, node, node_tmp) {
        utils_edge_t *edge, *edge_tmp;
        HASH_ITER(hh, node->edges, edge, edge_tmp) {
            netloc_edge_t *topo_edge = create_edges(edge, network);
            /* Add partition */
            netloc_partition_t *topo_part, *topo_part_tmp;
            netloc_network_iter_partitions(network, topo_part, topo_part_tmp) {
                if (edge->partitions && edge->partitions[topo_part->id]) {
                    utarray_push_back(&topo_edge->partitions, &topo_part);
                }
            }
        }
    }
    /* Add physical links */
    HASH_ITER(hh, nodes, node, node_tmp) {
        utils_edge_t *edge, *edge_tmp;
        HASH_ITER(hh, node->edges, edge, edge_tmp) {
            for (unsigned i = 0; i<utarray_len(&edge->physical_link_idx); ++i) {
                unsigned int id = *(unsigned int *)
                    utarray_eltptr(&edge->physical_link_idx, i);
                utils_physical_link_t *link = (utils_physical_link_t *)
                    utarray_eltptr(&node->physical_links, id);
                netloc_physical_link_t *topo_link;
                topo_link = create_physical_link(link, network);
                /* Add partition */
                netloc_partition_t *topo_part, *topo_part_tmp;
                netloc_network_iter_partitions(network, topo_part, topo_part_tmp) {
                    if (link->partitions && link->partitions[topo_part->id]) {
                        utarray_push_back(&topo_link->partitions, &topo_part);
                    }
                }
            }
        }
    }
    /* Add physical links to virtual edges */
    netloc_node_t *topo_node, *topo_node_tmp;
    netloc_network_iter_nodes(network, topo_node, topo_node_tmp) {
        netloc_edge_t *topo_edge, *topo_edge_tmp;
        netloc_node_iter_edges(topo_node, topo_edge, topo_edge_tmp) {
            for (unsigned i = 0; i < topo_edge->nsubedges; ++i)
                utarray_concat(&topo_edge->physical_links,
                               &topo_edge->subnode_edges[i]->physical_links);
        }
    }
    /* Change from netloc_hwloc_topology_t hashtable to array */
    machine->nb_hwloc_topos = HASH_COUNT(hwloc_topologies);
    if (machine->nb_hwloc_topos) {
        machine->hwlocpaths =
            (char **) malloc(sizeof(char *[machine->nb_hwloc_topos]));
        netloc_hwloc_topology_t *hwtopo, *hwtopo_tmp;
        HASH_ITER(hh, hwloc_topologies, hwtopo, hwtopo_tmp) {
            machine->hwlocpaths[hwtopo->hwloc_topo_idx] = hwtopo->path;
            HASH_DEL(hwloc_topologies, hwtopo);
            free(hwtopo);
        }
        /* Check reset global variable */
        assert(NULL == hwloc_topologies);
    }
    /* Compute the topology if any specific */
    netloc_partition_t *part, *part_tmp;
    netloc_machine_iter_partitions(machine, part, part_tmp) {
        int ret = netloc_topo_set_tree_topology(part);
        if (NETLOC_SUCCESS == ret) {
            fprintf(stderr, "%s is a tree\n", part->name);
        } else {
            fprintf(stderr, "%s is not a tree\n", part->name);
        }
    }

    return NETLOC_SUCCESS;
}
