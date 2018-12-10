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
#include <stdio.h>

#include <netloc.h>
#include <private/netloc.h>
#include <private/netloc-utils.h>

extern int netloc_topo_set_tree_topology(netloc_partition_t *partition);

static netloc_hwloc_topology_t *hwloc_topologies;
void flag_array_to_array(int size, int *flags, int *pnum, int **pvalues);

void flag_array_to_array(int size, int *flags, int *pnum, int **pvalues)
{
    int num = 0;
    int *values = (int *)malloc(sizeof(int[size])); /* maximal size */

    for (int i = 0; i < size; i++) {
        if (flags[i]) {
            values[num] = i;
            num++;
        }
    }

    *pnum = num;
    *pvalues = values;
}

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
create_node(const utils_node_t *node, netloc_machine_t *machine)
{
    netloc_node_t *topo_node = netloc_node_construct();
    assert(topo_node);

    /* Copy attributes */
    topo_node->type = node->type;
    topo_node->logical_id = node->logical_id;
    strncpy(topo_node->physical_id, node->physical_id, 20);

    /* Copy partitions */
    flag_array_to_array(machine->npartitions, node->partitions,
            &topo_node->nparts, &topo_node->partitions);

    /* Hostname */
    if (node->hostname && 0 < strlen(node->hostname)) {
        topo_node->hostname = strdup(node->hostname);
        /* Set hwloc_path if node is a host */
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
    printf("%p\n", machine->explicit->nodes);
    printf("%p\n", topo_node);
    HASH_ADD_STR(machine->explicit->nodes, physical_id, topo_node);

    /* Add potential subnodes */
    unsigned int nsubnodes = HASH_COUNT(node->subnodes);
    if (nsubnodes) {
        unsigned int i = 0;
        utils_node_t *subnode, *subnode_tmp;
        topo_node->nsubnodes = nsubnodes;
        topo_node->subnodes =
            (netloc_node_t **) malloc(sizeof(netloc_node_t *[nsubnodes]));
        HASH_ITER(hh, node->subnodes, subnode, subnode_tmp) {
            netloc_node_t *topo_subnode = create_node(subnode, machine);
            topo_subnode->virtual_node = topo_node;
            topo_node->subnodes[i] = topo_subnode;
            i += 1;
        }
    }

    return topo_node;
}

static netloc_edge_t *
create_edges(const utils_edge_t *edge, netloc_machine_t *machine)
{
    netloc_edge_t *topo_edge = netloc_edge_construct();
    if (!topo_edge)
        return NULL;

    /* Copy attributes */
    topo_edge->total_gbits = edge->total_gbits;
    assert(edge->reverse_edge);
    HASH_FIND_STR(machine->explicit->nodes, edge->dest, topo_edge->dest);
    assert(topo_edge->dest);
    HASH_FIND_STR(machine->explicit->nodes, edge->reverse_edge->dest,
            topo_edge->node);
    assert(topo_edge->node);

    /* Add partition */
    if (edge->partitions) {
        flag_array_to_array(machine->npartitions, edge->partitions,
                &topo_edge->nparts, &topo_edge->partitions);
    }

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
                create_edges(subedge, machine);
            assert(topo_subedge);

            topo_edge->subnode_edges[i] = topo_subedge;
        }
    }

    return topo_edge;
}

static inline netloc_physical_link_t *
create_physical_link(const utils_physical_link_t *link,
                     netloc_machine_t *machine)
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

    /* Find out src and dest nodes */
    HASH_FIND_STR(machine->explicit->nodes, link->parent_node->physical_id,
                  topo_link->src);
    assert(topo_link->src);
    HASH_FIND_STR(machine->explicit->nodes, link->dest->physical_id, topo_link->dest);
    assert(topo_link->dest);

    /* Find out edge */
    HASH_FIND_STR(topo_link->src->edges, link->parent_edge->dest,
                  topo_link->edge);
    assert(topo_link->edge);

    HASH_FIND(hh, machine->explicit->physical_links, &topo_link->other_way_id,
              sizeof(unsigned long long int), topo_link->other_way);
    if (topo_link->other_way)
        topo_link->other_way->other_way = topo_link;
    /* Add link to the proper structures */
    HASH_ADD(hh, machine->explicit->physical_links, id, sizeof(unsigned long long int),
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


netloc_machine_t *utils_to_netloc_machine(
        utils_node_t *nodes, const UT_array *partitions, char *subnet,
        char *outpath, const char *hwlocpath, netloc_network_type_t type)
{
    char *topodir;
    asprintf(&topodir, "%s/%s", outpath, subnet);
    netloc_machine_t *machine = netloc_machine_construct(topodir);

    /* Create partitions */
    unsigned int num_partitions = utarray_len(partitions);
    machine->npartitions = num_partitions;
    machine->partitions = (netloc_partition_t *)
        malloc(sizeof(netloc_partition_t[num_partitions]));
    for (int p = 0; p < num_partitions; p++) {
        utils_partition_t *u_partition = (*(utils_partition_t **)
                utarray_eltptr(partitions, p));
        machine->partitions[p].partition_name = strdup(u_partition->name);
        machine->partitions[p].subnet = strdup(subnet);
        machine->partitions[p].idx = p;
        machine->partitions[p].topology = NULL;

    }

    /* Add explicit */
    machine->explicit = (netloc_explicit_t *)calloc(1, sizeof(netloc_explicit_t));
    /* Add nodes */
    const utils_node_t *node, *node_tmp;
    HASH_ITER(hh, nodes, node, node_tmp) {
        create_node(node, machine);
    }

    /* Add edges */
    HASH_ITER(hh, nodes, node, node_tmp) {
        utils_edge_t *edge, *edge_tmp;
        HASH_ITER(hh, node->edges, edge, edge_tmp) {
            create_edges(edge, machine);
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
                netloc_physical_link_t *topo_link =
                    create_physical_link(link, machine);
                /* Add partition */
                if (link->partitions) {
                    flag_array_to_array(machine->npartitions, link->partitions,
                            &topo_link->nparts, &topo_link->partitions);
                }
            }
        }
    }

    /* Add physical links to virtual edges */
    netloc_node_t *topo_node, *topo_node_tmp;
    HASH_ITER(hh, machine->explicit->nodes, topo_node, topo_node_tmp) {
        netloc_edge_t *topo_edge, *topo_edge_tmp;
        HASH_ITER(hh, topo_node->edges, topo_edge, topo_edge_tmp) {
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

    // TODO
    ///* Compute the topology if any specific */
    //netloc_partition_t *part, *part_tmp;
    //netloc_machine_iter_partitions(machine, part, part_tmp) {
    //    int ret = netloc_topo_set_tree_topology(part);
    //    if (NETLOC_SUCCESS == ret) {
    //        fprintf(stderr, "%s is a tree\n", part->name);
    //    } else {
    //        fprintf(stderr, "%s is not a tree\n", part->name);
    //    }
    //}

    return machine;
}


static int get_virtual_id(char *id)
{
    static unsigned int virtual_id = 0;
    sprintf(id, "virtual%012u", ++virtual_id);
    return 0;
}

static int sort_by_dest(utils_edge_t *a, utils_edge_t *b)
{
    return strncmp(a->dest, b->dest, MAX_STR);
}

/**
 * Merge \ref edge going from \ref node to \ref dest_node into \ref
 * virtual_edge going from \ref virtual_node to \ref dest_node .
 *
 * At this point, \ref virtual_edge is not part of \ref
 * virtual_node->edges . \ref node is still part of \ref nodes and
 * \ref edge is still part of \ref node->edges . \ref node is always
 * of SW type.
 */
static inline void edge_merge_into(utils_node_t *virtual_node,
                                   utils_edge_t *virtual_edge,
                                   utils_node_t *node,
                                   utils_edge_t *edge,
                                   utils_node_t *dest_node,
                                   const unsigned int nparts)
{
    /* Change corresponding edge in reverse */
    utils_edge_t *reverse_edge, *virtual_reverse_edge;
    assert(dest_node);
    HASH_FIND_STR(dest_node->edges, node->physical_id, reverse_edge);
    assert(reverse_edge);
    HASH_DEL(dest_node->edges, reverse_edge);
    /* Check if the virtual_reverse_node is already defined */
    HASH_FIND_STR(dest_node->edges, virtual_node->physical_id,
                  virtual_reverse_edge);
    if (0 != strncmp("virtual", dest_node->physical_id, 7)) {
        if (!virtual_reverse_edge) {
            /* No previous virtual_reverse_edge */
            virtual_reverse_edge = (utils_edge_t *)malloc(sizeof(utils_edge_t));
            strncpy(virtual_reverse_edge->dest, virtual_node->physical_id,
                    MAX_STR);
            virtual_reverse_edge->total_gbits = 0;
            virtual_reverse_edge->reverse_edge = virtual_edge;
            virtual_reverse_edge->partitions = calloc(nparts, sizeof(int));
            utarray_init(&virtual_reverse_edge->physical_link_idx, &ut_int_icd);
            utarray_init(&virtual_reverse_edge->subedges, &ut_ptr_icd);
            HASH_ADD_STR(dest_node->edges, dest, virtual_reverse_edge);
            virtual_edge->reverse_edge = virtual_reverse_edge;
        }
        /* Merge reverse_edge into virtual_reverse_edge */
        utarray_push_back(&virtual_reverse_edge->subedges, &reverse_edge);
        /* Merge edge into virtual_edge */
        utarray_push_back(&virtual_edge->subedges, &edge);
    } else {
        assert(virtual_reverse_edge);
        /* Virtual to virtual: less updates to be done */
        /* Merge into reverse virtual */
        utarray_concat(&virtual_reverse_edge->subedges, &reverse_edge->subedges);
        /* Merge into virtual */
        utarray_concat(&virtual_edge->subedges, &edge->subedges);
    }
    /* Common merging operations */
    utarray_concat(&virtual_reverse_edge->physical_link_idx,
                   &reverse_edge->physical_link_idx);
    virtual_reverse_edge->total_gbits += reverse_edge->total_gbits;
    virtual_edge->total_gbits += edge->total_gbits;
    /* Set partitions */
    for (unsigned int p = 0; p < nparts; ++p) {
        virtual_edge->partitions[p] |= edge->partitions[p];
        virtual_reverse_edge->partitions[p] |= reverse_edge->partitions[p];
    }
    /* Add offset to each physical_link index */
    unsigned int offset =
        utarray_len(&virtual_node->physical_links) -
        utarray_len(&node->physical_links);
    unsigned int num_links = utarray_len(&edge->physical_link_idx);
    for (unsigned int l = 0; l < num_links; l++) {
        unsigned int link_idx = *(unsigned int *)
            utarray_eltptr(&edge->physical_link_idx, l);
        link_idx += offset;
        utarray_push_back(&virtual_edge->physical_link_idx, &link_idx);
#ifdef NETLOC_DEBUG
        utils_physical_link_t *link = (utils_physical_link_t *)
            utarray_eltptr(&virtual_node->physical_links, link_idx);
        assert(0 != *(int*)(link->dest));
        if (dest_node->subnodes) {
            utils_node_t*subnode;
            HASH_FIND_STR(dest_node->subnodes, link->parent_edge->dest, subnode);
            assert(subnode);
        } else {
            assert(!strncmp(link->parent_edge->dest, dest_node->physical_id,
                            MAX_STR));
        }
#endif /* NETLOC_DEBUG */
    }
    if (!strncmp("virtual", dest_node->physical_id, 7)) {
        /* Destroy now useless reverse edges */
        utarray_done(&reverse_edge->physical_link_idx);
        utarray_done(&reverse_edge->subedges);
        free(reverse_edge->partitions);
        free(reverse_edge);
    }
}

int find_similar_nodes(utils_node_t **pnodes,
                                     const unsigned int nparts)
{
    int ret = NETLOC_ERROR;
    /* Build edge lists by node */
    int num_nodes = HASH_COUNT(*pnodes);
    utils_node_t **switch_nodes =
        (utils_node_t **)malloc(sizeof(utils_node_t *[num_nodes]));
    utils_node_t ***edgedest_by_node =
        (utils_node_t ***)malloc(sizeof(utils_node_t **[num_nodes]));
    unsigned int *num_edges_by_node =
        (unsigned int *)malloc(sizeof(unsigned int [num_nodes]));
    utils_node_t *node, *node_tmp;
    int idx = -1;
    HASH_ITER(hh, *pnodes, node, node_tmp) {
        idx++;
        if (NETLOC_NODE_TYPE_HOST == node->type) {
            switch_nodes[idx] = NULL;
            num_edges_by_node[idx] = 0;
            edgedest_by_node[idx] = NULL;
            continue;
        }
        unsigned int num_edges = HASH_COUNT(node->edges);
        switch_nodes[idx] = node;
        num_edges_by_node[idx] = num_edges;
        edgedest_by_node[idx] =
            (utils_node_t **) malloc(sizeof(utils_node_t *[num_edges]));
        utils_edge_t *edge, *edge_tmp;
        int edge_idx = 0;
        HASH_SORT(node->edges, sort_by_dest);
        HASH_ITER(hh, node->edges, edge, edge_tmp) {
            HASH_FIND_STR(*pnodes, edge->dest, edgedest_by_node[idx][edge_idx]);
            assert(edgedest_by_node[idx][edge_idx]);
            edge_idx++;
        }
    }

    /* We compare the edge lists to find similar nodes */
    for (int nodeIdx = 0; nodeIdx < num_nodes - 1; nodeIdx++) {
        utils_node_t *node1 = switch_nodes[nodeIdx];
        utils_node_t *virtual_node = NULL;

        for (int nodeCmpIdx = nodeIdx + 1; node1 && nodeCmpIdx < num_nodes;
             nodeCmpIdx++) {

            utils_node_t *node2 = switch_nodes[nodeCmpIdx];

            int equal = node2
                && num_edges_by_node[nodeIdx] == num_edges_by_node[nodeCmpIdx];

            /* Check if the destinations are the same */
            for (unsigned i = 0; equal && i < num_edges_by_node[nodeIdx]; i++) {
                if (edgedest_by_node[nodeIdx][i]
                    != edgedest_by_node[nodeCmpIdx][i])
                    equal = 0;
            }

            /* If we have similar nodes */
            if (equal) {
                /* We create a new virtual node to contain all of them */
                if (!virtual_node) {
                    /* virtual_node = netloc_node_construct(); */
                    virtual_node = (utils_node_t *)
                        calloc(1, sizeof(utils_node_t));
                    get_virtual_id(virtual_node->physical_id);
                    virtual_node->description =
                        strdup(virtual_node->physical_id);
                    virtual_node->partitions = calloc(nparts, sizeof(int));
                    for (unsigned int p = 0; p < nparts; ++p) {
                        virtual_node->partitions[p] |= node1->partitions[p];
                    }
                    virtual_node->type = node1->type;
                    virtual_node->subnodes = NULL;
                    utarray_init(&virtual_node->physical_links,
                                &utils_physical_link_icd);

                    /* add physical_links */
                    utarray_concat(&virtual_node->physical_links,
                                   &node1->physical_links);

                    /* Initialize destination for virtual edge */
                    for (unsigned i = 0; i < num_edges_by_node[nodeIdx]; i++) {
                        utils_edge_t *edge1, *virtual_edge;
                        utils_node_t *node_dest = edgedest_by_node[nodeIdx][i];
                        HASH_FIND_STR(node1->edges, node_dest->physical_id,
                                      edge1);
                        assert(edge1);
                        if (!strncmp("virtual", node_dest->physical_id, 7)) {
                            /* Reuse already defined virtual edge */
                            /* Transfert virtual edge */
                            HASH_DEL(node1->edges, edge1);
                            HASH_ADD_STR(virtual_node->edges, dest, edge1);
                            /* Change reverse */
                            edge1 = edge1->reverse_edge;
                            HASH_DEL(node_dest->edges, edge1);
                            strncpy(edge1->dest, virtual_node->physical_id,
                                    MAX_STR);
                            HASH_ADD_STR(node_dest->edges, dest, edge1);
                        } else {
                            /* Create new virtual edge */
                            virtual_edge = (utils_edge_t *)
                                calloc(1, sizeof(utils_edge_t));
                            strncpy(virtual_edge->dest, node_dest->physical_id,
                                    MAX_STR);
                            virtual_edge->partitions =
                                calloc(nparts, sizeof(int));
                            virtual_edge->total_gbits = 0;
                            utarray_init(&virtual_edge->subedges, &ut_ptr_icd);
                            utarray_init(&virtual_edge->physical_link_idx,
                                        &ut_int_icd);
                            edge_merge_into(virtual_node, virtual_edge, node1,
                                            edge1, node_dest, nparts);
                            HASH_DEL(node1->edges, edge1);
                            HASH_ADD_STR(virtual_node->edges, dest,
                                         virtual_edge);
                        }
                    }
                    /* Remove node from nodes hashtable to add it to
                       the virtual_node subnodes */
                    HASH_DEL(*pnodes, node1);
                    HASH_ADD_STR(virtual_node->subnodes, physical_id, node1);
                    /* Add virtual_node to nodes hashtable */
                    HASH_ADD_STR(*pnodes, physical_id, virtual_node);
                }

                /* add physical_links */
                utarray_concat(&virtual_node->physical_links,
                               &node2->physical_links);

                for (unsigned int p = 0; p < nparts; ++p)
                    virtual_node->partitions[p] |= node2->partitions[p];

                for (unsigned i = 0; i < num_edges_by_node[nodeCmpIdx]; i++) {
                    utils_edge_t *edge2, *virtual_edge;
                    utils_node_t *node_dest = edgedest_by_node[nodeCmpIdx][i];
                    HASH_FIND_STR(virtual_node->edges, node_dest->physical_id,
                                  virtual_edge);
                    assert(virtual_edge);
                    HASH_FIND_STR(node2->edges, virtual_edge->dest, edge2);
                    assert(edge2);
                    edge_merge_into(virtual_node, virtual_edge, node2, edge2,
                                    node_dest, nparts);
                    HASH_DEL(node2->edges, edge2);
                    if (!strncmp("virtual", node_dest->physical_id, 7)) {
                        /* Destroy previous virtual edge */
                        utarray_done(&edge2->physical_link_idx);
                        utarray_done(&edge2->subedges);
                        free(edge2->partitions);
                        free(edge2);
                    }
                }

                HASH_DEL(*pnodes, node2);
                switch_nodes[nodeCmpIdx] = NULL;
                HASH_ADD_STR(virtual_node->subnodes, physical_id, node2);

                /* // TODO paths */

            }
        }

        /* If virtual_node is not NULL, then there have been a virtual
           node added. We need to update the entries */
        if (NULL != virtual_node) {
            unsigned int num_edges = HASH_COUNT(virtual_node->edges);
            switch_nodes[nodeIdx] = virtual_node;
            num_edges_by_node[nodeIdx] = num_edges;
            edgedest_by_node[nodeIdx] = (utils_node_t **)
                realloc(edgedest_by_node[nodeIdx],
                        sizeof(utils_node_t *[num_edges]));
            /* Reset edges destinations */
            utils_edge_t *edge, *edge_tmp;
            int edge_idx = 0;
            HASH_SORT(virtual_node->edges, sort_by_dest);
            HASH_ITER(hh, virtual_node->edges, edge, edge_tmp) {
                HASH_FIND_STR(*pnodes, edge->dest,
                              edgedest_by_node[nodeIdx][edge_idx]);
                assert(edgedest_by_node[nodeIdx][edge_idx]);
                edge_idx++;
            }
            for (int idx = nodeIdx + 1; idx < num_nodes; ++idx) {
                node = switch_nodes[idx];
                if (node /* SW node that has not yet been virtualized */
                    && /* linked to a virtualized node -> need to be updated */
                    (num_edges = HASH_COUNT(node->edges)) < num_edges_by_node[idx]) {
                    num_edges_by_node[idx] = num_edges;
                    edgedest_by_node[idx] = (utils_node_t **)
                        realloc(edgedest_by_node[idx],
                                sizeof(utils_node_t *[num_edges]));
                    /* Reset edges destinations */
                    edge_idx = 0;
                    HASH_SORT(node->edges, sort_by_dest);
                    HASH_ITER(hh, node->edges, edge, edge_tmp) {
                        HASH_FIND_STR(*pnodes, edge->dest,
                                      edgedest_by_node[idx][edge_idx]);
                        assert(edgedest_by_node[idx][edge_idx]);
                        edge_idx++;
                    }
                }
            }
        }
    }

    ret = NETLOC_SUCCESS;
ERROR:
    for (int idx = 0; idx < num_nodes; idx++) {
        if (edgedest_by_node[idx])
            free(edgedest_by_node[idx]);
    }
    free(edgedest_by_node);
    free(num_edges_by_node);
    free(switch_nodes);
    return ret;
}

void set_reverse_edges(utils_node_t *nodes)
{
    utils_node_t *node, *node_tmp, *dest_node;
    utils_edge_t *edge, *edge_tmp;
    HASH_ITER(hh, nodes, node, node_tmp) {
        HASH_ITER(hh, node->edges, edge, edge_tmp) {
            if (edge->reverse_edge) {
                assert(!strncmp(edge->reverse_edge->dest,
                                node->physical_id, MAX_STR));
                assert(edge->reverse_edge->reverse_edge == edge);
                continue;
            }
            HASH_FIND_STR(nodes, edge->dest, dest_node);
            assert(dest_node);
            HASH_FIND_STR(dest_node->edges, node->physical_id,
                          edge->reverse_edge);
            assert(edge->reverse_edge);
            assert(!edge->reverse_edge->reverse_edge);
            edge->reverse_edge->reverse_edge = edge;
        }
    }
#ifdef NETLOC_DEBUG
    {
        /* Check every edge has its reverse_edge set */
        utils_node_t *node, *node_tmp, *dest_node;
        utils_edge_t *edge, *edge_tmp, *reverse_edge;
        unsigned int tutu = 0;
        HASH_ITER(hh, nodes, node, node_tmp) {
            HASH_ITER(hh, node->edges, edge, edge_tmp) {
                ++tutu;
                if(!edge->reverse_edge) {
                    HASH_FIND_STR(nodes, edge->dest, dest_node);
                    assert(dest_node);
                    HASH_FIND_STR(dest_node->edges, node->physical_id,
                                  reverse_edge);
                }
                assert(edge->reverse_edge);
            }
        }
    }
#endif /* NETLOC_DEBUG */
}

