#ifndef _WIP_H
#define _WIP_H
#include "utils/uthash.h"
#include "utils/utarray.h"

#include <hwloc.h>

//#include "netloc-utils.h"

// TODO scotch int
#define NETLOC_INT int

enum {
    NETLOC_SUCCESS         =  0, /** Success */
    /* Errors */
    NETLOC_ERROR,              /** General condition */
    NETLOC_ERROR_BADMACHINE,   /** Bad machine */
    NETLOC_ERROR_NODENOTFOUND, /** Node not found */
    NETLOC_ALRD_IN_RESTRICT,   /** Node already in restriction */
    NETLOC_ERROR_NOENT,        /** URI is invalid, no such entry */
};

/**
 * Enumerated type for the various types of supported topologies
 */
typedef enum {
    NETLOC_TOPOLOGY_TYPE_INVALID  = 0,  /**< Invalid */
    NETLOC_TOPOLOGY_TYPE_RANDOM   = 1,  /**< Random */
    NETLOC_TOPOLOGY_TYPE_RING     = 2,  /**< Ring */
    NETLOC_TOPOLOGY_TYPE_GRID     = 3,  /**< Grid */
    NETLOC_TOPOLOGY_TYPE_ALL2ALL  = 4,  /**< All2All */
    NETLOC_TOPOLOGY_TYPE_TREE     = 5,  /**< Tree */
    NETLOC_TOPOLOGY_TYPE_TORUS    = 6,  /**< Torus */
} netloc_topology_type_t;


/* Pre declarations to avoid inter dependency problems */
/** \cond IGNORE */
struct netloc_machine_t;
typedef struct netloc_machine_t netloc_machine_t;
struct netloc_arch_t;
typedef struct netloc_arch_t netloc_arch_t;
struct netloc_node_t;
typedef struct netloc_node_t netloc_node_t;
struct netloc_filter_t;
typedef struct netloc_filter_t netloc_filter_t;
/** \endcond */

/******************************************************************************/
/* PUBLIC API */
/******************************************************************************/
int netloc_machine_load(netloc_machine_t **pmachine, char *path);
int netloc_machine_save(netloc_machine_t *machine, char *path);

int netloc_topology_get(netloc_machine_t *machine, netloc_filter_t *filter,
        int *nlevels, int *ncoords, int **dims, netloc_topology_type_t **types,
        int **levelidx,  int **costs);

int netloc_node_find(netloc_machine_t *machine, char *name, netloc_node_t **node);
int netloc_node_current(netloc_machine_t *machine, netloc_node_t **node);
int netloc_node_get_coords(netloc_machine_t *machine, netloc_filter_t *filter,
        netloc_node_t *node, int *coords);
int netloc_node_get_partition(netloc_machine_t *machine, netloc_filter_t *filter,
        netloc_node_t *node, int *partition);

int netloc_restriction_add_node(netloc_machine_t *machine, netloc_node_t *node);
#endif
