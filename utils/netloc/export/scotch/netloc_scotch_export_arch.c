#include <stdio.h>
#include <stdint.h>
#include <scotch.h>
#include <netloc.h>
#include <netlocscotch.h>

void help(char *name, FILE *f)
{
    fprintf(f, "Usage: %s <topofile> <archfile>\n"
            "\t%s --help\n", name, name);
}

int main(int argc, char **argv)
{
    int ret;
    SCOTCH_Arch arch;
    SCOTCH_Arch subarch;

    char *topo_filename = NULL;
    char *arch_filename = NULL;

    if (argc == 1 || argc > 3) {
        help(argv[0], stdout);
        return 1;
    }

    if (argc == 2) {
        if (!strcmp(*argv, "--help")) {
            help(argv[0], stdout);
            return 0;
        } else {
            help(argv[0], stdout);
            return 1;
        }
    } else if (argc == 3) {
        topo_filename = argv[1];
        arch_filename = argv[2];
    }

    netloc_machine_t *machine;
    ret = netloc_machine_load(&machine, topo_filename);
    if( NETLOC_SUCCESS != ret ) {
        return ret;
    }

    ret = netlocscotch_export_topology(machine, NULL, &arch, NULL);
    if( NETLOC_SUCCESS != ret ) {
        return ret;
    }

    FILE *arch_file = fopen(arch_filename, "w");
    SCOTCH_archSave(&arch, arch_file);
    fclose(arch_file);

    SCOTCH_archExit(&arch);

    return 0;
}

