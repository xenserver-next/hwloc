/*
 * Copyright © 2014 Inria.  All rights reserved.
 * See COPYING in top-level directory.
 */

#ifndef HWLOC_PORT_XEN_XENCTRL_H
#define HWLOC_PORT_XEN_XENCTRL_H

typedef struct xc_interface_core xc_interface;

xc_interface *xc_interface_open(void *, void *, unsigned);
int xc_interface_close(xc_interface *);

struct xen_sysctl_physinfo {
    uint32_t threads_per_core;
    uint32_t cores_per_socket;
    uint32_t nr_cpus;
    uint32_t max_cpu_id;
    uint32_t nr_nodes;
    uint32_t max_node_id;
};
typedef struct xen_sysctl_physinfo xen_sysctl_physinfo_t;
typedef xen_sysctl_physinfo_t xc_physinfo_t;

int xc_physinfo(xc_interface *, xc_physinfo_t *);

int xc_topologyinfo_bounced(xc_interface *, uint32_t *, uint32_t *, uint32_t *, uint32_t *);

int xc_numainfo_bounced(xc_interface *, uint32_t *, uint64_t *, uint64_t *, uint32_t *);

#endif /* HWLOC_PORT_XEN_XENCTRL_H */
