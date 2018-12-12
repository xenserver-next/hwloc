#ifndef _WIP_H
#define _WIP_H
#include "utils/uthash.h"
#include "utils/utarray.h"

#include <hwloc.h>

//#include "netloc-utils.h"

#define NETLOC_int int // XXX TODO

enum {
    NETLOC_SUCCESS         =  0, /** Success */
    /* Errors */
    NETLOC_ERROR           = 1, /**  General condition */
    NETLOC_ERROR_NOTDIR    = 2, /** URI is not a directory */
    NETLOC_ERROR_NOENT     = 3, /** URI is invalid, no such entry */
    NETLOC_ERROR_EMPTY     = 4, /** No networks found */
    NETLOC_ERROR_MULTIPLE  = 5, /** Multiple matching networks found */
    NETLOC_ERROR_NOT_IMPL  = 6, /** Interface not implemented */
    NETLOC_ERROR_EXISTS    = 7, /** Entry already exists in a lookup table */
    NETLOC_ERROR_NOT_FOUND = 8, /** No path found */
};



/* Pre declarations to avoid inter dependency problems */
/** \cond IGNORE */
struct netloc_machine_t;
typedef struct netloc_machine_t netloc_machine_t;
/** \endcond */

typedef enum {
    NETLOC_ARCH_TREE    =  0,  /* Fat tree */
} netloc_arch_type_t;

struct netloc_arch_t;
typedef struct netloc_arch_t netloc_arch_t;
struct netloc_machine_t;
typedef struct netloc_machine_t netloc_machine_t;
/** \endcond */


struct netloc_arch_t {
};


/******************************************************************************/
/* PUBLIC API */
/******************************************************************************/
int netloc_read_xml_from_path(netloc_machine_t **pmachine, const char *path);
int netloc_read_xml_from_env(netloc_machine_t **pmachine);

int netloc_get_local_coords(int *nlevels, int **types, int **ndims,
        int **dims, int **coords, int **costs);

/* Scotch functions: TODO import */


#endif
