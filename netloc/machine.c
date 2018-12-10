/*
 * Copyright © 2019 Inria.  All rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 * See COPYING in top-level directory.
 *
 * $HEADER$
 */

#include <stdlib.h>
#include <libgen.h>

#include <netloc.h>
#include <private/netloc.h>


int netloc_machine_load(netloc_machine_t **pmachine, char *path)
{
    return netloc_read_xml(pmachine, path);

}


int netloc_machine_save(netloc_machine_t *machine, char *path)
{
    if (path != NULL) {
        /* FIXME can break hwloc dir link */
        free(machine->topopath);
        machine->topopath = strdup(path);
        machine->topodir = dirname(strdup(path));
    }
    return netloc_machine_to_xml(machine);
}
