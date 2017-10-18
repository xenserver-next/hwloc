#define _GNU_SOURCE
#include <mpi.h>
#include <pmi.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include <assert.h>

#define max_nodes 4

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

    PMI_Get_rank(&rank);
    PMI_Get_size(&num_ranks);

    /* The root will retrieve info */
    if (!rank) {
        int dims[4];
        FILE *output;

        assert(argc == 2);

        output = fopen(argv[1], "w");
        if (!output) {
            perror("fopen");
            MPI_Abort(MPI_COMM_WORLD, 2);
        }

        get_dimensions(dims);
        fprintf(output, "torus 3");
        for (int d = 0; d < 3; d++) {
            fprintf(output, " %d", dims[d]);
        }
        fprintf(output, "\n");
        fprintf(output, "tree 1 %d", dims[3]);
        fprintf(output, "\n");
        fclose(output);
    }

    MPI_Finalize();
    return 0;
}
