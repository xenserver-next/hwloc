/*
 * Copyright © 2017      Inria.  All rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 * See COPYING in top-level directory.
 *
 * $HEADER$
 */

/* 
 * This file provides general function and global variables to
 * generate XML topology files.
 */

#define _GNU_SOURCE         /* See feature_test_macros(7) */

#include <private/netloc.h>
#include <netloc.h>

#include <private/utils/xml.h>
#include <netloc/uthash.h>
#include <netloc/utarray.h>
#include <dirent.h>
#include <libgen.h>

node_t *nodes = NULL;
UT_array *partitions = NULL;
route_source_t *routes = NULL;
path_source_t *paths = NULL;

static int get_virtual_id(char *id)
{
    static unsigned int virtual_id = 0;
    sprintf(id, "virtual%012u", ++virtual_id);
    return 0;
}

static int sort_by_dest(edge_t *a, edge_t *b)
{
    return strncmp(a->dest, b->dest, MAX_STR);
}

int node_belongs_to_a_partition(node_t *node) {
    for(int p = 0; node->partitions && p < utarray_len(partitions); ++p)
        if (node->partitions[p]) return 1;
    return 0;
}

int edge_belongs_to_a_partition(edge_t *edge) {
    for(int p = 0; edge->partitions && p < utarray_len(partitions); ++p)
        if (edge->partitions[p]) return 1;
    return 0;
}

static inline void edge_merge_into(node_t *virtual_node, edge_t *virtual_edge,
                                   node_t *node, edge_t *edge)
{
    unsigned int npartitions = utarray_len(partitions);
    /* Change corresponding edge in reverse */
    node_t *dest_node;
    edge_t *dest_edge, *reverse_virtual_edge;
    HASH_FIND_STR(nodes, edge->dest, dest_node);
    assert(dest_node);
    HASH_FIND_STR(dest_node->edges, node->physical_id, dest_edge);
    assert(dest_edge);
    /* Check if the virtual_reverse_node is already defined */
    HASH_FIND_STR(dest_node->edges, virtual_node->physical_id, reverse_virtual_edge);
    if (!reverse_virtual_edge) { /* No previous reverse_virtual_edge */
        reverse_virtual_edge = (edge_t *)malloc(sizeof(edge_t));
        strncpy(reverse_virtual_edge->dest, virtual_node->physical_id, MAX_STR);
        reverse_virtual_edge->total_gbits = 0;
        reverse_virtual_edge->reverse_edge = virtual_edge;
        reverse_virtual_edge->partitions = calloc(npartitions, sizeof(int));
        utarray_new(reverse_virtual_edge->physical_link_idx, &ut_int_icd);
        utarray_new(reverse_virtual_edge->subedges, &ut_ptr_icd);
        HASH_ADD_STR(dest_node->edges, dest, reverse_virtual_edge);
        virtual_edge->reverse_edge = reverse_virtual_edge;
    }
    /* Merge into already defined reverse_virtual_edge */
    utarray_concat(reverse_virtual_edge->physical_link_idx, dest_edge->physical_link_idx);
    reverse_virtual_edge->total_gbits += dest_edge->total_gbits;
    HASH_DEL(dest_node->edges, dest_edge);
    utarray_push_back(reverse_virtual_edge->subedges, &dest_edge);
    /* Merge into virtual_edge */
    virtual_edge->total_gbits += edge->total_gbits;
    HASH_DEL(node->edges, edge);
    utarray_push_back(virtual_edge->subedges, &edge);
    /* Set partitions */
    for (unsigned int p = 0; p < npartitions; ++p) {
        virtual_edge->partitions[p] |= edge->partitions[p];
        reverse_virtual_edge->partitions[p] |= dest_edge->partitions[p];
    }
    /* Add offset to each physical_link index */
    unsigned int offset =
        utarray_len(virtual_node->physical_links) - utarray_len(node->physical_links);
    unsigned int num_links = utarray_len(edge->physical_link_idx);
    for (unsigned int l = 0; l < num_links; l++) {
        unsigned int link_idx = *(unsigned int *)
            utarray_eltptr(edge->physical_link_idx, l);
        link_idx += offset;
        utarray_push_back(virtual_edge->physical_link_idx, &link_idx);
#ifdef NETLOC_DEBUG
        physical_link_t *link = (physical_link_t *)
            utarray_eltptr(virtual_node->physical_links, link_idx);
        assert(0 != *(int*)link);
#endif /* NETLOC_DEBUG */
    }
}

static int find_similar_nodes(void)
{
    int ret;
    unsigned int npartitions = utarray_len(partitions);
    /* Build edge lists by node */
    int num_nodes = HASH_COUNT(nodes);
    node_t **switch_nodes = (node_t **)malloc(sizeof(node_t *[num_nodes]));
    node_t ***edgedest_by_node = (node_t ***)malloc(sizeof(node_t **[num_nodes]));
    unsigned int *num_edges_by_node = (unsigned int *)malloc(sizeof(unsigned int [num_nodes]));
    node_t *node, *node_tmp;
    int idx = -1;
    HASH_ITER(hh, nodes, node, node_tmp) {
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
        edgedest_by_node[idx] = (node_t **)malloc(sizeof(node_t *[num_edges]));
        edge_t *edge, *edge_tmp;
        int edge_idx = 0;
        HASH_SORT(node->edges, sort_by_dest);
        netloc_node_iter_edges(node, edge, edge_tmp) {
            HASH_FIND_STR(nodes, edge->dest, edgedest_by_node[idx][edge_idx]);
            assert(edgedest_by_node[idx][edge_idx]);
            edge_idx++;
        }
    }

    /* We compare the edge lists to find similar nodes */
    for (int nodeIdx = 0; nodeIdx < num_nodes - 1; nodeIdx++) {
        node_t *node1 = switch_nodes[nodeIdx];
        node_t *virtual_node = NULL;

        for (int nodeCmpIdx = nodeIdx + 1; node1 && nodeCmpIdx < num_nodes; nodeCmpIdx++) {
            node_t *node2 = switch_nodes[nodeCmpIdx];

            int equal = node2 && num_edges_by_node[nodeIdx] == num_edges_by_node[nodeCmpIdx];

            /* Check if the destinations are the same */
            for (int i = 0; equal && i < num_edges_by_node[nodeIdx]; i++) {
                if (edgedest_by_node[nodeIdx][i] != edgedest_by_node[nodeCmpIdx][i])
                    equal = 0;
            }

            /* If we have similar nodes */
            if (equal) {
                /* We create a new virtual node to contain all of them */
                if (!virtual_node) {
                    /* virtual_node = netloc_node_construct(); */
                    virtual_node = (node_t *)calloc(1, sizeof(node_t));
                    get_virtual_id(virtual_node->physical_id);
                    virtual_node->description = strdup(virtual_node->physical_id);

                    virtual_node->partitions = calloc(npartitions, sizeof(int));
                    for (unsigned int p = 0; p < npartitions; ++p)
                        virtual_node->partitions[p] |= node1->partitions[p];

                    virtual_node->type = node1->type;
                    virtual_node->subnodes = NULL;
                    utarray_new(virtual_node->physical_links, &physical_link_icd);

                    /* add physical_links */
                    utarray_concat(virtual_node->physical_links, node1->physical_links);

                    HASH_DEL(nodes, node1);
                    HASH_ADD_STR(virtual_node->subnodes, physical_id, node1);
                    HASH_ADD_STR(nodes, physical_id, virtual_node);

                    /* Initialize destination for virtual edge */
                    for (int i = 0; i < num_edges_by_node[nodeIdx]; i++) {
                        edge_t *edge1, *virtual_edge = (edge_t *)malloc(sizeof(edge_t));
                        strncpy(virtual_edge->dest, edgedest_by_node[nodeIdx][i]->physical_id,
                                MAX_STR);
                        virtual_edge->partitions = calloc(npartitions, sizeof(int));
                        virtual_edge->total_gbits = 0;
                        utarray_new(virtual_edge->subedges, &ut_ptr_icd);
                        utarray_new(virtual_edge->physical_link_idx, &ut_int_icd);
                        HASH_FIND_STR(node1->edges, virtual_edge->dest, edge1);
                        assert(edge1);
                        edge_merge_into(virtual_node, virtual_edge, node1, edge1);
                        HASH_ADD_STR(virtual_node->edges, dest, virtual_edge);
                    }
                }

                /* add physical_links */
                utarray_concat(virtual_node->physical_links, node2->physical_links);

                for (unsigned int p = 0; p < npartitions; ++p)
                    virtual_node->partitions[p] |= node2->partitions[p];

                for (int i = 0; i < num_edges_by_node[nodeCmpIdx]; i++) {
                    edge_t *edge2, *virtual_edge;
                    HASH_FIND_STR(virtual_node->edges,
                                  edgedest_by_node[nodeCmpIdx][i]->physical_id, virtual_edge);
                    assert(virtual_edge);
                    HASH_FIND_STR(node2->edges, virtual_edge->dest, edge2);
                    assert(edge2);
                    edge_merge_into(virtual_node, virtual_edge, node2, edge2);
                }

                HASH_DEL(nodes, node2);
                HASH_ADD_STR(virtual_node->subnodes, physical_id, node2);
                switch_nodes[nodeCmpIdx] = NULL;

                    /* // TODO paths */

                    /* /\* Set edges *\/ */
                    /* netloc_edge_t *edge1, *edge_tmp1; */
                    /* netloc_node_iter_edges(node1, edge1, edge_tmp1) { */
                    /*     netloc_edge_t *virtual_edge = netloc_edge_construct(); */
                    /*     if (!first_virtual_edge) */
                    /*         first_virtual_edge = virtual_edge; */
                    /*     virtual_edge->node = virtual_node; */
                    /*     virtual_edge->dest = edge1->dest; */
                    /*     ret = edge_merge_into(virtual_edge, edge1, 0); */
                    /*     if (ret != NETLOC_SUCCESS) { */
                    /*         netloc_edge_destruct(virtual_edge); */
                    /*         goto ERROR; */
                    /*     } */
                    /*     HASH_ADD_PTR(virtual_node->edges, dest, virtual_edge); */

                    /*     /\* Change the reverse edge of the neighbours (reverse nodes) *\/ */
                    /*     netloc_node_t *reverse_node = edge1->dest; */
                    /*     netloc_edge_t *reverse_edge = edge1->other_way; */

                    /*     netloc_edge_t *reverse_virtual_edge = */
                    /*         netloc_edge_construct(); */
                    /*     reverse_virtual_edge->dest = virtual_node; */
                    /*     reverse_virtual_edge->node = reverse_node; */
                    /*     reverse_virtual_edge->other_way = virtual_edge; */
                    /*     virtual_edge->other_way = reverse_virtual_edge; */
                    /*     HASH_ADD_PTR(reverse_node->edges, dest, reverse_virtual_edge); */
                    /*     ret = edge_merge_into(reverse_virtual_edge, reverse_edge, 1); */
                    /*     if (ret != NETLOC_SUCCESS) { */
                    /*         goto ERROR; */
                    /*     } */
                    /*     HASH_DEL(reverse_node->edges, reverse_edge); */
                    /* } */

                    /* /\* We remove the node from the list of nodes *\/ */
                    /* HASH_DEL(topology->nodes, node1); */
                    /* HASH_ADD_STR(topology->nodes, physical_id, virtual_node); */
                    /* printf("First node found: %s (%s)\n", node1->description, node1->physical_id); */
            }

                /* utarray_concat(virtual_node->physical_links, node2->physical_links); */
                /* utarray_push_back(virtual_node->subnodes, &node2); */
                /* utarray_concat(virtual_node->partitions, node2->partitions); */

                /* /\* Set edges *\/ */
                /* netloc_edge_t *edge2, *edge_tmp2; */
                /* netloc_edge_t *virtual_edge = first_virtual_edge; */
                /* netloc_node_iter_edges(node2, edge2, edge_tmp2) { */
                /*     /\* Merge the edges from the physical node into the virtual node *\/ */
                /*     ret = edge_merge_into(virtual_edge, edge2, 0); */
                /*     if (ret != NETLOC_SUCCESS) { */
                /*         goto ERROR; */
                /*     } */

                /*     /\* Change the reverse edge of the neighbours (reverse nodes) *\/ */
                /*     netloc_node_t *reverse_node = edge2->dest; */
                /*     netloc_edge_t *reverse_edge = edge2->other_way; */

                /*     netloc_edge_t *reverse_virtual_edge; */
                /*     HASH_FIND_PTR(reverse_node->edges, &virtual_node, */
                /*             reverse_virtual_edge); */
                /*     ret = edge_merge_into(reverse_virtual_edge, reverse_edge, 1); */
                /*     if (ret != NETLOC_SUCCESS) { */
                /*         goto ERROR; */
                /*     } */
                /*     HASH_DEL(reverse_node->edges, reverse_edge); */

                /*     /\* Get the next edge *\/ */
                /*     virtual_edge = virtual_edge->hh.next; */
                /* } */

                /* /\* We remove the node from the list of nodes *\/ */
                /* HASH_DEL(topology->nodes, node2); */
                /* printf("\t node found: %s (%s)\n", node2->description, node2->physical_id); */

                /* nodes[idx2] = NULL; */
            /* } */
        }
    }

    ret = NETLOC_SUCCESS;
ERROR:
    free(switch_nodes);
    for (int idx = 0; idx < num_nodes; idx++) {
        if (edgedest_by_node[idx])
            free(edgedest_by_node[idx]);
    }
    free(edgedest_by_node);
    free(num_edges_by_node);
    return ret;
}

static inline void set_reverse_edges()
{
    node_t *node, *node_tmp, *dest_node;
    edge_t *edge, *edge_tmp, *reverse_edge;
    HASH_ITER(hh, nodes, node, node_tmp) {
        HASH_ITER(hh, node->edges, edge, edge_tmp) {
            HASH_FIND_STR(nodes, edge->dest, dest_node);
            assert(dest_node);
            HASH_FIND_STR(dest_node->edges, node->physical_id, edge->reverse_edge); 
            assert(edge->reverse_edge);
            assert(!edge->reverse_edge->reverse_edge || edge->reverse_edge->reverse_edge == edge);
        }
    }
}

int netloc_write_into_xml_file(const char *subnet, const char *path, const char *hwlocpath,
                               const netloc_network_type_t transportType)
{
    int ret = NETLOC_ERROR;

    set_reverse_edges();
    find_similar_nodes();

    /* Quick hack to generate XML even without libxml2 */
#ifdef HWLOC_HAVE_LIBXML2
    ret = netloc_libxml_write_xml_file(subnet, path, hwlocpath, transportType);
#else
    ret = netloc_nolibxml_write_xml_file(subnet, path, hwlocpath, transportType);
#endif /* HWLOC_HAVE_LIBXML2 */

    /* Untangle similar nodes so the virtualization is transparent */
    node_t *node, *node_tmp;
    HASH_ITER(hh, nodes, node, node_tmp) {
        if (node->subnodes) {
            node_t *subnode, *subnode_tmp;
            HASH_DEL(nodes, node);
            HASH_ITER(hh, node->subnodes, subnode, subnode_tmp) {
                HASH_DEL(node->subnodes, subnode);
                HASH_ADD_STR(nodes, physical_id, subnode);
            }
            free(node);
        }
    }
    return NETLOC_SUCCESS;
}
