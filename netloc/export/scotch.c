#include <stdint.h>
#include <stdio.h>
#include <scotch.h>

#include <netloc.h>
#include <netlocscotch.h>
#include <private/netloc.h>

static int compareint(void const *a, void const *b)
{
    const int *int_a = (const int *)a;
    const int *int_b = (const int *)b;
    return *int_a-*int_b;
}

static int build_subarch(SCOTCH_Arch *arch, int partition_idx, NETLOC_INT num_nodes, netloc_node_t **node_list,
        SCOTCH_Arch *subarch)
{
    int ret;

    /* Build list of indices of nodes */
    NETLOC_INT *node_idx = (NETLOC_INT *)malloc(sizeof(NETLOC_INT[num_nodes]));
    for (int n = 0; n < num_nodes; n++) {
       netloc_node_t *node = node_list[n];

       netloc_position_t pos = node->topo_positions[partition_idx];
       node_idx[n] = pos.idx;
    }

    /* Hack to avoid problem with unsorted node list in the subarch and scotch
     * FIXME */
    qsort(node_idx, num_nodes, sizeof(*node_idx), compareint);

    ret = SCOTCH_archSub(subarch, arch, num_nodes, node_idx);
    if (ret != 0) {
        fprintf(stderr, "Error: SCOTCH_archSub failed\n");
    }

    return ret;
}

static int node_get_idx(netloc_node_t *node, int partition_idx)
{
   netloc_position_t *position =
      node->topo_positions+partition_idx;
   return position->idx;
}

static int explicit_to_deco(netloc_explicit_t *explicit, int partition_idx,
        int num_nodes, netloc_node_t **node_list,
        SCOTCH_Arch *arch, SCOTCH_Arch *subarch)
{
   int ret;
   /* NOTE We consider all nodes, with any partition, avoiding finding paths between nodes */
   int total_num_nodes = HASH_COUNT(explicit->nodes);
   NETLOC_INT *edge_idx = (NETLOC_INT *)malloc(sizeof(NETLOC_INT[total_num_nodes+1]));


   netloc_node_t *node, *node_tmp;
   HASH_ITER(hh, explicit->nodes, node, node_tmp) {
      netloc_edge_t *edge, *edge_tmp;
      int node_idx = node_get_idx(node, partition_idx);
      int local_num_edges = HASH_COUNT(node->edges);
      edge_idx[node_idx+1] = local_num_edges;
   }

   /* Cumulative number of edges */
   for (int n = 0; n < total_num_nodes; n++) {
      edge_idx[n+1] += edge_idx[n];
   }
   int num_edges = edge_idx[total_num_nodes];
   NETLOC_INT *edges = (NETLOC_INT *)malloc(sizeof(NETLOC_INT[num_edges]));

   HASH_ITER(hh, explicit->nodes, node, node_tmp) {
      netloc_edge_t *edge, *edge_tmp;
      int node_idx = node_get_idx(node, partition_idx);
      int local_num_edges = HASH_COUNT(node->edges);

      int edge_cur_idx = edge_idx[node_idx];
      HASH_ITER(hh, node->edges, edge, edge_tmp) {
         netloc_node_t *dest = edge->dest;
         int dest_idx = node_get_idx(dest, partition_idx);
         edges[edge_cur_idx] = dest_idx;
         edge_idx++;
      }
   }
   SCOTCH_Graph *graph = (SCOTCH_Graph *)malloc(sizeof(SCOTCH_Graph));
   ret = SCOTCH_graphBuild(graph, 0, total_num_nodes, edge_idx,
         NULL, NULL, NULL, num_edges, edges, NULL);
   assert(!ret);
   ret = SCOTCH_archBuild2(arch, graph, 0, NULL); // XXX TODO which function to use
   assert(!ret);

   build_subarch(arch, partition_idx, num_nodes, node_list,
         subarch);

    return 0;
}


static int tree_to_tleaf(netloc_topology_t *topology, SCOTCH_Arch *arch)
{
    int ret = SCOTCH_archTleaf(arch, topology->ndims, topology->dimensions, topology->costs);
    if (ret != 0) {
        fprintf(stderr, "Error: SCOTCH_archTleaf failed\n");
        return NETLOC_ERROR;
    }

    return 0;
}

int netlocscotch_export_topology(netloc_machine_t *machine,
      netloc_filter_t *filter,
      SCOTCH_Arch *arch, SCOTCH_Arch *subarch)
{
    // TODO read filter to get partition
    int partition_idx = 0;
    netloc_partition_t *partition = machine->partitions+partition_idx;

    /* Check if we have node restriction */
    int num_nodes = machine->restriction->num_nodes;
    netloc_node_t **node_list = machine->restriction->nodes;

    /* No recognized topology */
    if (!partition->topology) {
        explicit_to_deco(machine->explicit, machine->partitions[partition_idx].idx,
                num_nodes, node_list, arch, subarch);

    } else {
        netloc_topology_t *topology = partition->topology;
        if (topology->type == NETLOC_TOPOLOGY_TYPE_TREE) {
            if (!topology->subtopo) { /* Tree and nothing else */
                tree_to_tleaf(topology, arch);
                if (node_list) {
                    build_subarch(arch, partition_idx, num_nodes, node_list,
                            subarch);
                }

            } else { /* Hierachical topology */
                assert(0); // XXX TODO implement with deco
            }
        } else {
                assert(0); // XXX TODO implement
        }
    }

    return 0;
}
