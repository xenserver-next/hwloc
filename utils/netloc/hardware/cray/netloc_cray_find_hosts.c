#define _GNU_SOURCE
#include <utmpx.h>
#include <unistd.h>
#include <mpi.h>
#include <pmi.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <hwloc.h>
//#include <private/netloc.h>

#include "uthash.h"
#include "utarray.h"
#include <assert.h>

#define max_nodes 4


typedef struct {
    UT_hash_handle hh; /* Makes this structure hashable */
    int idx; /* Hash key */
    int **ranks; /* ranks (slot as index) for nodes on the same switch */
    int *num_ranks;
    char **hostnames;
    int num_nodes;
} switch_t;


int get_coords(int rank, int *coords)
{
    pmi_mesh_coord_t xyz;
    int nid;

    PMI_Get_nid(rank, &nid);
    PMI_Get_meshcoord((pmi_nid_t) nid, &xyz);

    coords[0] = xyz.mesh_x;
    coords[1] = xyz.mesh_y;
    coords[2] = xyz.mesh_z;

    FILE * procfile = fopen("/proc/cray_xt/cname", "r");

    if (procfile != NULL) {
        char a, b, c, d;
        int col, row, cage, slot, anode;

        /* format example: c1-0c1s2n1 c3-0c2s15n3 */
        fscanf(procfile,
                "%c%d-%d%c%d%c%d%c%d",
                &a, &col, &row, &b, &cage, &c, &slot, &d, &anode);
        coords[3] = anode;

        fclose(procfile);
        return 0;

    } else {
        fprintf(stderr, "xctopo_get_mycoords: fopen has failed! \n");
        assert(0);
        return 1;
    }

}

void get_dimensions(int *dims)
{
    pmi_mesh_coord_t xyz;
    PMI_Get_max_dimension(&xyz);

    dims[0] = xyz.mesh_x+1;
    dims[1] = xyz.mesh_y+1;
    dims[2] = xyz.mesh_z+1;
    dims[3] = max_nodes;
}
int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);

    int rank;
    int num_ranks;
    int coords[4];
    int dims[4];
    char hostname[100];

    assert(argc == 2);

    PMI_Get_rank(&rank);
    int rank2;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank2);
    assert(rank == rank2);
    PMI_Get_size(&num_ranks);

    get_coords(rank, coords);
    get_dimensions(dims);

    gethostname(hostname, 100);

    /* Compute index */
    int torus_idx = 0;
    for (int i = 0; i < 3; i++) {
        torus_idx = torus_idx*dims[i]+coords[i];
    }

    hwloc_topology_t topology;
    hwloc_cpuset_t set;
    hwloc_topology_init(&topology);
    hwloc_topology_load(topology);
    set = hwloc_bitmap_alloc();
    hwloc_get_cpubind(topology, set, 0);
    int pu_rank = hwloc_bitmap_first(set);

    /* We integrate node idx in slot_idx since it is also a tree */
    int port_idx = coords[3];
    int slot_idx = pu_rank;

    /* Gather data */
    int all_torus_idx[num_ranks];
    int all_port_idx[num_ranks];
    int all_slot_idx[num_ranks];
    char all_hostnames[num_ranks][100];
    /* idx in the torus */
    MPI_Gather(&torus_idx, 1, MPI_INT, all_torus_idx, 1, MPI_INT, 0,
            MPI_COMM_WORLD);

    /* idx in the switch */
    MPI_Gather(&port_idx, 1, MPI_INT, all_port_idx, 1, MPI_INT, 0,
            MPI_COMM_WORLD);
    printf("[rank %d] port_idx %d\n", rank, port_idx);
    if (!rank) {
        printf("[line %d] port_idx %d\n", __LINE__, all_port_idx[2]);
    }
    //MPI_Finalize();
    //return 0;

    /* idx in the node topo */
    MPI_Gather(&slot_idx, 1, MPI_INT, all_slot_idx, 1, MPI_INT, 0,
            MPI_COMM_WORLD);
    if (!rank) printf("[line %d] port_idx %d\n", __LINE__, all_port_idx[2]);
    /* Hostnames */
    MPI_Gather(&hostname, 100, MPI_CHAR, all_hostnames, 100, MPI_CHAR, 0,
            MPI_COMM_WORLD);
    if (!rank) printf("[line %d] port_idx %d\n", __LINE__, all_port_idx[2]);

    MPI_Barrier(MPI_COMM_WORLD);
    if (!rank) printf("[line %d] port_idx %d\n", __LINE__, all_port_idx[2]);


    /* The root will handle data */
    if (!rank) {
        FILE *output;
        output = fopen(argv[1], "w");
        if (!output) {
            perror("fopen");
            MPI_Abort(MPI_COMM_WORLD, 2);
        }

        switch_t *sw, *sw_tmp;
        switch_t *switches = NULL;
        int max_switch_size = dims[3];

        printf("Num ranks: %d\n", num_ranks);
        for (int r = 0; r < num_ranks; r++) {
            printf("rank: %d\n", r);
            printf("line %d\n", __LINE__);
            printf("[line %d] port_idx %d\n", __LINE__, all_port_idx[2]);
            HASH_FIND_INT(switches, all_torus_idx+r, sw);
            /* If switch does not exist yet, create it */
            if (!sw) {
                sw = (switch_t *)malloc(sizeof(switch_t));
                sw->idx = all_torus_idx[r];
                sw->ranks = (int **)calloc(max_switch_size, sizeof(int *));
                sw->num_ranks = (int *)calloc(max_switch_size, sizeof(int));
                sw->hostnames = (char **)calloc(max_switch_size, sizeof(char *));
                sw->num_nodes = 0;
                HASH_ADD_INT(switches, idx, sw);
            }
            printf("[line %d] port_idx %d\n", __LINE__, all_port_idx[2]);
            printf("[line %d]\n", __LINE__);
            printf("r %d\n", r);
            printf("port_idx %d\n", all_port_idx[r]);
            printf("[line %d]\n", __LINE__);
            int port_idx = all_port_idx[r];
            /* if node in switch have not been found yet */
            if (!sw->ranks[port_idx]) {
                sw->ranks[port_idx] = (int *)malloc(sizeof(int[32]));
                sw->num_ranks[port_idx] = 0;
                sw->hostnames[port_idx] = all_hostnames[r];
                for (int s = 0; s < 32; s++) {
                    sw->ranks[port_idx][s] = -1;
                }
                sw->num_nodes++;
            }
            printf("line %d\n", __LINE__);
            sw->ranks[port_idx][all_slot_idx[r]] = r;
            sw->num_ranks[port_idx]++;
        }

        /* Write data into file */
        int num_switches = HASH_COUNT(switches);
        fprintf(output, "torus 3 24 160 24 160 24 160 %d", num_switches);
        if (num_switches == 24*24*24) {
            fprintf(output, " -1\n");
        } else {
            HASH_ITER(hh, switches, sw, sw_tmp) {
                fprintf(output, " %d", sw->idx);
            }
            fprintf(output, "\n");
        }

        HASH_ITER(hh, switches, sw, sw_tmp) {
            fprintf(output, "tree 1 %d 80 %d", max_switch_size, sw->num_nodes);
            for (int n = 0; n < max_switch_size; n++) {
                if (sw->num_ranks[n]) {
                    fprintf(output, " %d", n);
                }
            }
            fprintf(output, "\n");
            for (int n = 0; n < max_switch_size; n++) {
                if (sw->num_ranks[n]) {
                    fprintf(output, "node %s tree 4 2 8 2 4 4 2 2 1", sw->hostnames[n]);

                    /* List of cores */
                    if (sw->num_ranks[n] == 32) {
                        fprintf(output, " -1");
                    } else {
                        fprintf(output, " %d", sw->num_ranks[n]);
                        for (int r = 0; r < 32; r++) {
                            if (sw->ranks[n][r] != -1)
                                fprintf(output, " %d", r);
                        }
                    }

                    /* List of ranks */
                    for (int r = 0; r < 32; r++) {
                        if (sw->ranks[n][r] != -1) {
                            fprintf(output, " %d", sw->ranks[n][r]);
                        }
                    }
                    fprintf(output, "\n");
                }
            }

        }

        //fclose(output);
    }

    MPI_Finalize();
    return 0;
}

// format : nnodes nodenames... sizes... core1.1 rank1 core1.2 rank2...
//          dims
