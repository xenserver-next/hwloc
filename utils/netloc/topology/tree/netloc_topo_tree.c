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
 * This file provides general function to generate XML topology
 * files. The objective is to determine whether the topology is a
 * fat-tree, and determine its characteristics. 
 */

#define _GNU_SOURCE         /* See feature_test_macros(7) */

#include <private/netloc.h>
#include <private/netloc-xml.h>
#include <netloc.h>

typedef struct netloc_analysis_data_t {
    int level;
    void *userdata;
} netloc_analysis_data;

/* Complete the topology to have a complete balanced tree  */
static inline void
netloc_arch_tree_complete(const netloc_arch_tree_t *tree,
                          UT_array *down_degrees_by_level,
                          const int num_hosts, int **parch_idx)
{
    int num_levels = tree->num_levels;
    NETLOC_int *max_degrees = tree->degrees;

    /* Complete the tree by inserting nodes */
    for (int l = 0; l < num_levels-1; l++) { // from the root to the leaves
        unsigned int num_degrees = utarray_len(&down_degrees_by_level[l]);
        int *degrees = (int *) down_degrees_by_level[l].d;
        NETLOC_int max_degree = max_degrees[l];

        unsigned int down_level_idx = 0;
        UT_array *down_level_degrees = &down_degrees_by_level[l+1];
        NETLOC_int down_level_max_degree = max_degrees[l+1];
        for (unsigned int d = 0; d < num_degrees; d++) {
            int degree = degrees[d];
            if (degree > 0) {
                down_level_idx += degree;
                if (degree < max_degree) {
                    int missing_degree =
                        (degree-max_degree)*down_level_max_degree;
                    utarray_insert(down_level_degrees, &missing_degree,
                                   down_level_idx);
                    down_level_idx++;
                }
            } else {
                int missing_degree = degree*down_level_max_degree;
                utarray_insert(down_level_degrees, &missing_degree,
                               down_level_idx);
                down_level_idx++;
            }
        }
    }

    /* Indices for the list of hosts, in the complete architecture */
    unsigned int num_degrees = utarray_len(&down_degrees_by_level[num_levels-1]);
    int *degrees = (int *) down_degrees_by_level[num_levels-1].d;
    NETLOC_int max_degree = max_degrees[num_levels-1];
    int ghost_idx = 0;
    int idx = 0;
    int *arch_idx = (int *)malloc(sizeof(int[num_hosts]));
    for (unsigned int d = 0; d < num_degrees; d++) {
        int diff, degree = degrees[d];

        if (degree > 0) {
            diff = max_degree-degree;
        } else {
            diff = -degree;
        }

        for (int i = 0; i < degree; i++) {
            arch_idx[idx++] = ghost_idx++;
        }
        ghost_idx += diff;
    }
    *parch_idx = arch_idx;
}

int netloc_topo_set_tree_topology(netloc_partition_t *partition)
{
    int ret = NETLOC_SUCCESS;
    netloc_topology_t *arch = netloc_arch_construct();

    netloc_arch_tree_t *tree = (netloc_arch_tree_t *)
        calloc(1, sizeof(netloc_arch_tree_t));
    arch->arch.node_tree = tree;
    arch->type = NETLOC_ARCH_TREE;

    if (1 == HASH_CNT(hh2, partition->desc->nodesByHostname)) {
        /* Init arch */
        partition->topo = arch;
        tree->degrees = (NETLOC_int *) malloc(sizeof(NETLOC_int));
        *tree->degrees = 0;
        tree->costs = (NETLOC_int *) malloc(sizeof(NETLOC_int));
        *tree->costs = 0;
        return NETLOC_SUCCESS;
    }
    if (2 > HASH_CNT(hh2, partition->desc->nodesByHostname)) {
        /* Cannot be a tree with less than 2 nodes */
        partition->topo = NULL;
        netloc_arch_destruct(arch);
        return NETLOC_ERROR;
    }

    UT_array nodes;
    utarray_init(&nodes, &ut_ptr_icd);

    /* we build nodes from host list in the given partition
     * and we init all the analysis data */
    netloc_analysis_data *analysis_data;
    netloc_partition_iter_nodes(partition, pnode) {
        analysis_data = (netloc_analysis_data *)
            malloc(sizeof(netloc_analysis_data));
        analysis_data->level = -1;
        analysis_data->userdata = (*pnode)->userdata;
        (*pnode)->userdata = (void *) analysis_data;

        netloc_edge_t *edge, *edge_tmp;
        netloc_node_iter_edges((*pnode), edge, edge_tmp) {
            analysis_data = (netloc_analysis_data *)
                malloc(sizeof(netloc_analysis_data));
            analysis_data->level = -1;
            analysis_data->userdata = edge->userdata;
            edge->userdata = (void *) analysis_data;
        }

        if (netloc_node_is_host(*pnode)) {
            utarray_push_back(&nodes, pnode);
        }
    }

    /* pointer to one host node */
    netloc_node_t *current_node =
        *(netloc_node_t **) utarray_eltptr(&nodes, 0);

    /* We set the levels in the analysis data */
    /* Upward edges will have the level of the source node and downward edges
     * will have -1 as level */
    int num_levels = 0;
    while (0 < utarray_len(&nodes)) {
        UT_array new_nodes;
        utarray_init(&new_nodes, &ut_ptr_icd);

        for (unsigned int n = 0; n < utarray_len(&nodes); ++n) {
            netloc_node_t *node = *(netloc_node_t **) utarray_eltptr(&nodes, n);
            netloc_analysis_data *node_data =
                (netloc_analysis_data *) node->userdata;
            /* There is a problem, this is not a tree */
            if (node_data->level != -1 && node_data->level != num_levels) {
                utarray_done(&new_nodes);
                ret = NETLOC_ERROR;
                goto end;
            } else {
                node_data->level = num_levels;
                netloc_edge_t *edge, *edge_tmp;
                netloc_node_iter_edges(node, edge, edge_tmp) {
                    if (!netloc_edge_is_in_partition(edge, partition))
                        continue;
                    netloc_analysis_data *edge_data =
                        (netloc_analysis_data *) edge->userdata;
                    netloc_node_t *dest = edge->dest;
                    netloc_analysis_data *dest_data =
                        (netloc_analysis_data *) dest->userdata;
                    /* If we are going back */
                    if (dest_data->level != -1 && dest_data->level < num_levels)
                        continue;
                    else if (dest_data->level != num_levels) {
                        edge_data->level = num_levels;
                        utarray_push_back(&new_nodes, &dest);
                    }
                }
            }
        }
        num_levels += 1;
        utarray_done(&nodes);
        nodes = new_nodes;
    }

    /* We go though the tree to order the leaves and find the tree
     * structure */
    UT_array ordered_name_array;
    UT_array *down_degrees_by_level = NULL;
    NETLOC_int *max_down_degrees_by_level = NULL;

    utarray_init(&ordered_name_array, &ut_ptr_icd);

    down_degrees_by_level = (UT_array *) malloc(sizeof(UT_array[num_levels]));
    for (int l = 0; l < num_levels; l++) {
        utarray_init(&down_degrees_by_level[l], &ut_int_icd);
    }
    max_down_degrees_by_level = (NETLOC_int *)
        calloc(num_levels-1, sizeof(NETLOC_int));

    UT_array down_edges;
    utarray_init(&down_edges, &ut_ptr_icd);
    netloc_edge_t *up_edge = /* Should be the only edge */
        netloc_edge_is_in_partition(current_node->edges, partition) ?
        current_node->edges : NULL;
    utarray_push_back(&ordered_name_array, &current_node);
    while (1) {
        if (utarray_len(&down_edges)) {
            netloc_edge_t *down_edge = *(void **)utarray_back(&down_edges);
            utarray_pop_back(&down_edges);
            netloc_node_t *dest_node = down_edge->dest;
            if (netloc_node_is_host(dest_node)) {
                utarray_push_back(&ordered_name_array, &dest_node);
            } else {
                netloc_edge_t *edge, *edge_tmp;
                int num_edges = 0;
                netloc_node_iter_edges(dest_node, edge, edge_tmp) {
                    if (!netloc_edge_is_in_partition(edge, partition))
                        continue;
                    netloc_analysis_data *edge_data =
                        (netloc_analysis_data *)edge->userdata;
                    int edge_level = edge_data->level;
                    if (edge_level == -1) {
                        utarray_push_back(&down_edges, &edge);
                        num_edges++;
                    }
                }
                int level =
                    ((netloc_analysis_data *)dest_node->userdata)->level;
                utarray_push_back(&down_degrees_by_level[num_levels-1-level],
                                  &num_edges);
                max_down_degrees_by_level[num_levels-1-level] =
                    max_down_degrees_by_level[num_levels-1-level] > num_edges ?
                    max_down_degrees_by_level[num_levels-1-level] : num_edges;
            }
        } else {
            netloc_edge_t *new_up_edge = NULL;
            if (!up_edge)
                break;

            netloc_node_t *up_node = up_edge->dest;
            netloc_edge_t *edge, *edge_tmp;
            int num_edges = 0;
            netloc_node_iter_edges(up_node, edge, edge_tmp) {
                if (!netloc_edge_is_in_partition(edge, partition))
                    continue;
                netloc_analysis_data *edge_data =
                    (netloc_analysis_data *)edge->userdata;
                int edge_level = edge_data->level;

                netloc_node_t *dest_node = edge->dest;

                /* If dest_node is the node where we are from */
                if (dest_node == up_edge->node) {
                    num_edges++;
                    continue;
                }

                /* Downward edge */
                if (edge_level == -1) {
                    utarray_push_back(&down_edges, &edge);
                    num_edges++;
                }
                /* Upward edge */
                else {
                    new_up_edge = edge;
                }

            }
            int level = ((netloc_analysis_data *)up_node->userdata)->level;
            utarray_push_back(&down_degrees_by_level[num_levels-1-level],
                              &num_edges);
            max_down_degrees_by_level[num_levels-1-level] =
                max_down_degrees_by_level[num_levels-1-level] > num_edges ?
                max_down_degrees_by_level[num_levels-1-level] : num_edges;
            up_edge = new_up_edge;
        }
    }

    tree->num_levels = num_levels-1;
    tree->degrees = max_down_degrees_by_level;

    int network_coeff = 2;
    tree->costs = (NETLOC_int *)malloc(sizeof(NETLOC_int[tree->num_levels]));
    tree->costs[tree->num_levels-1] = 1;
    for (int i = tree->num_levels-2; i >= 0 ; i--) {
        tree->costs[i] = tree->costs[i+1]*network_coeff;
    }

    /* Now we have the degree of each node, so we can complete the topology to
     * have a complete balanced tree as requested by the tleaf structure */
    int *arch_idx;
    int num_nodes = utarray_len(&ordered_name_array);
    netloc_arch_tree_complete(tree, down_degrees_by_level, num_nodes, &arch_idx);

    netloc_node_t **ordered_nodes = (netloc_node_t **)ordered_name_array.d;
    netloc_arch_node_t *named_nodes = NULL;
    for (int i = 0; i < num_nodes; i++) {
        netloc_arch_node_t *node = netloc_arch_node_construct();
        node->node = ordered_nodes[i];
        node->name = ordered_nodes[i]->hostname;
        node->idx_in_topo = arch_idx[i];
        HASH_ADD_STR(named_nodes, name, node);
    }
    free(arch_idx);

    arch->nodes_by_name = named_nodes;

    partition->topo = arch;

 end:
    if (nodes.n)
        utarray_done(&nodes);

    if (ordered_name_array.n)
        utarray_done(&ordered_name_array);

    if (down_degrees_by_level) {
        for (int l = 0; l < num_levels; l++) {
            utarray_done(&down_degrees_by_level[l]);
        }
        free(down_degrees_by_level);
    }

    if (down_edges.n)
        utarray_done(&down_edges);

    /* We copy back all userdata */
    netloc_partition_iter_nodes(partition, pnode) {
        netloc_analysis_data *analysis_data =
            (netloc_analysis_data *) (*pnode)->userdata;
        if (analysis_data->level == -1 && ret != NETLOC_ERROR) {
            ret = NETLOC_ERROR;
            printf("The node %s was not browsed\n", (*pnode)->description);
        }
        free(analysis_data);
        netloc_edge_t *edge, *edge_tmp;
        netloc_node_iter_edges(*pnode, edge, edge_tmp) {
            netloc_analysis_data *analysis_data =
                (netloc_analysis_data *) edge->userdata;
            (*pnode)->userdata = analysis_data->userdata;
            free(analysis_data);
        }
    }

    return ret;
}
