#include <stdio.h> // for scotch
#include <stdint.h>

#include <netloc.h>

void help(char *name, FILE *f)
{
    fprintf(f, "Usage: %s <topofile> [node name]\n"
            "\t%s --help\n", name, name);
}

char *topology_type_to_name(netloc_topology_type_t type)
{
    switch (type) {
        case NETLOC_TOPOLOGY_TYPE_TREE:
            return strdup("tree");
        default:
            assert(0); // TODO implement
    }
}

/* TODO use MPI_and show all nodes */
int main(int argc, char **argv)
{
    int ret;

    char *topo_filename = NULL;

    if (argc != 2 && argc != 3) {
        help(argv[0], stdout);
        return 1;
    }

    topo_filename = argv[1];
    netloc_machine_t *machine;
    ret = netloc_machine_load(&machine, topo_filename);
    if( NETLOC_SUCCESS != ret ) {
        return ret;
    }

    netloc_node_t *node;
    if (argc == 3) {
        ret = netloc_node_find(machine, argv[2], &node);
    } else {
        ret = netloc_node_current(machine, &node);
    }
    if( NETLOC_SUCCESS != ret ) {
        return ret;
    }

    int nlevels;
    int ncoords;
    netloc_topology_type_t *types;
    int *levelidx;
    int *dims;
    int *costs;
    ret = netloc_topology_get(machine, NULL,
        &nlevels, &ncoords, &dims, &types, &levelidx, &costs);

    {
        netloc_node_t *node;
        netloc_node_find(machine, "miriel032", &node);
        netloc_restriction_add_node(machine, node);
        netloc_machine_save(machine, "/tmp/hop.xml");
    }

    if( NETLOC_SUCCESS != ret ) {
        return ret;
    }

    printf("Topology: ");
    for (int l = 0; l < nlevels-1; l++)
        printf("%s(", topology_type_to_name(types[l]));
    printf("%s", topology_type_to_name(types[nlevels-1]));
    for (int l = 0; l < nlevels-1; l++)
        printf(")");
    printf("\n");

    printf("\tDimensions: ");

    for (int l = 0; l < nlevels; l++) {
        for (int i = levelidx[l]; i < levelidx[l+1]; i++) {
            printf("%d%s", dims[i],
                    (i == levelidx[l+1]-1)?
                        ((l == nlevels-1)? "\n": "; "):
                        ", ");
        }
    }

    printf("\tCosts: ");
    for (int l = 0; l < nlevels; l++) {
        for (int i = levelidx[l]; i < levelidx[l+1]; i++) {
            printf("%d%s", costs[i],
                    (i == levelidx[l+1]-1)?
                        ((l == nlevels-1)? "\n": "; "):
                        ", ");
        }
    }

    int *coords = malloc(ncoords*sizeof(*coords));
    ret = netloc_node_get_coords(machine, NULL, node, coords);
    if( NETLOC_SUCCESS != ret ) {
        return ret;
    }

    printf("Node coords: ");
    for (int l = 0; l < nlevels; l++) {
        for (int i = levelidx[l]; i < levelidx[l+1]; i++) {
            printf("%d%s", coords[i],
                    (i == levelidx[l+1]-1)?
                        ((l == nlevels-1)? "\n": "; "):
                        ", ");
        }
    }

    return 0;
}

