/*
 * Copyright © 2013-2014 Citrix Systems Ltd.
 * Copyright © 2014 Inria.  All rights reserved.
 * See COPYING in top-level directory.
 */

#include <private/autogen/config.h>
#include <hwloc.h>
#include <hwloc/plugins.h>

/* private headers allowed for convenience because this plugin is built within hwloc */
#include <private/debug.h>
#include <private/misc.h>

#include <inttypes.h>
#include <assert.h>

#include <xenctrl.h>


/* Xen private data for hwloc_backend */
struct hwloc_xen_priv {
  xc_interface *xch;        /* Xenctrl handle */
  xentoollog_logger logger; /* Xenctrl logger */
};

/* Topology and numa information from Xen */
struct hwloc_xen_info {
  uint32_t max_cpu_id;
  uint32_t max_node_id;

  /* From sysctl_topologyinfo */
  uint32_t cpu_count;   /* Number of elements in of cpu_to_* arrays */
  const uint32_t *cpu_to_core, *cpu_to_socket, *cpu_to_node;

  /* From sysctl_numainfo */
  uint32_t node_count;   /* Number of elements in of node_to_* arrays */
  const uint64_t *node_to_memsize, *node_to_memfree;
  const uint32_t *node_to_node_distance; /* (node_count * node_count) elements */
};

/* Xenctrl Logging function */
static void
xenctl_logfn(struct xentoollog_logger *logger __hwloc_attribute_unused,
             xentoollog_level level __hwloc_attribute_unused,
             int errnoval __hwloc_attribute_unused,
             const char *context __hwloc_attribute_unused,
             const char *format __hwloc_attribute_unused,
             va_list al __hwloc_attribute_unused)
{
#ifdef HWLOC_DEBUG
  /* Dont bother snprintf()ing the buffer if we will throw it away anyway. */
  static char logbuf[512];

  vsnprintf(logbuf, sizeof logbuf, format, al);
  hwloc_debug("%s: %s: %s\n", context, xtl_level_to_string(level), logbuf);
#endif
}

/* Allocate a hwloc_xen_info structure */
static struct hwloc_xen_info *
alloc_xen_info(void)
{
  struct hwloc_xen_info *p = malloc(sizeof *p);

  if (p)
    memset(p, 0, sizeof *p);

  return p;
}

/* Free a hwloc_xen_info structure */
static void
free_xen_info(struct hwloc_xen_info *p)
{
  if (p) {
    free((void *)p->node_to_node_distance);
    free((void *)p->node_to_memfree);
    free((void *)p->node_to_memsize);
    free((void *)p->cpu_to_node);
    free((void *)p->cpu_to_socket);
    free((void *)p->cpu_to_core);
    free(p);
  }
}

/* Perform hypercalls to query Xen for information, and fill in a
 * hwloc_xen_info structure. */
static struct hwloc_xen_info *
hwloc_get_xen_info(xc_interface *xch)
{
  struct hwloc_xen_info *data = alloc_xen_info();
  xc_physinfo_t physinfo;

  if (!data)
    return NULL;

  /* Ask Xen for the physical information.  This provides the sizes of arrays
   * required to query the topology and numa information. */
  if (xc_physinfo(xch, &physinfo))
    goto out;

  { /* Ask for the full topology information */
    uint32_t *cpu_to_core, *cpu_to_socket, *cpu_to_node;

    data->max_cpu_id = physinfo.max_cpu_id;
    data->cpu_count = data->max_cpu_id + 1;

    cpu_to_core   = calloc(data->cpu_count, sizeof *cpu_to_core);
    cpu_to_socket = calloc(data->cpu_count, sizeof *cpu_to_socket);
    cpu_to_node   = calloc(data->cpu_count, sizeof *cpu_to_node);

    if (!cpu_to_core || !cpu_to_socket || !cpu_to_node)
      goto out;

    if (xc_topologyinfo_bounced(xch, &data->max_cpu_id,
                                cpu_to_core, cpu_to_socket, cpu_to_node))
      goto out;

    assert((data->max_cpu_id < data->cpu_count) &&
           "Xen overflowed our arrays");

    data->cpu_to_core = cpu_to_core;
    data->cpu_to_socket = cpu_to_socket;
    data->cpu_to_node = cpu_to_node;
  }

  { /* Ask for the full numa memory information */
    uint64_t *node_to_memsize, *node_to_memfree;
    uint32_t *node_to_node_distance;

    data->max_node_id = physinfo.max_node_id;
    data->node_count = data->max_node_id + 1;

    node_to_memsize = calloc(data->node_count, sizeof *node_to_memsize);
    node_to_memfree = calloc(data->node_count, sizeof *node_to_memfree);
    node_to_node_distance = calloc(data->node_count * data->node_count,
                                   sizeof *node_to_node_distance);

    if (!node_to_memsize || !node_to_memfree || !node_to_node_distance)
      goto out;

    if (xc_numainfo_bounced(xch, &data->max_node_id,
                            node_to_memsize, node_to_memfree,
                            node_to_node_distance))
      goto out;

    assert((data->max_node_id < data->node_count) &&
           "Xen overflowed our arrays");

    data->node_to_memsize = node_to_memsize;
    data->node_to_memfree = node_to_memfree;
    data->node_to_node_distance = node_to_node_distance;
  }

  return data;

 out:
  free_xen_info(data);
  return NULL;
}

/* Enumerate the full Xen system and fill in appropriate objects into the
 * topology. */
static int
hwloc_xen_discover(struct hwloc_backend *backend)
{
  struct hwloc_topology *topology = backend->topology;
  struct hwloc_xen_priv *priv = backend->private_data;
  struct hwloc_xen_info *data;
  hwloc_bitmap_t each_socket, each_node, each_core;
  uint32_t i, j, z;

  if (hwloc_get_root_obj(topology)->cpuset)
    return 0;

  hwloc_debug("Discovering Xen topology\n");

  data = hwloc_get_xen_info(priv->xch);
  assert(data && "Failed to gather data from Xen\n");

  hwloc_debug("Xen topology information\n");
  hwloc_debug("  cpu count %"PRIu32", max cpu id %"PRIu32"\n",
              data->cpu_count, data->max_cpu_id);

  for ( z = 0; z <= data->max_cpu_id; ++z )
    hwloc_debug("  cpu[%3"PRIu32"], core %3"PRIu32", sock %3"PRIu32", node %3"PRIu32"\n",
                z, data->cpu_to_core[z], data->cpu_to_socket[z],
                data->cpu_to_node[z]);

  hwloc_debug("Xen NUMA information\n");
  hwloc_debug("  numa count %"PRIu32", max numa id %"PRIu32"\n",
              data->node_count, data->max_node_id);
  for ( z = 0; z <= data->max_node_id; ++z )
    hwloc_debug("  node[%3"PRIu32"], size %"PRIu64", free %"PRIu64"\n",
                z, data->node_to_memsize[z], data->node_to_memfree[z]);

  hwloc_alloc_root_sets(topology);
  hwloc_setup_pu_level(topology, data->max_cpu_id+1);

  /* Socket information */
  each_socket = hwloc_bitmap_alloc();
  for ( z = 0; z <= data->max_cpu_id; ++z )
    if (data->cpu_to_socket[z] != ~0U)
      hwloc_bitmap_set(each_socket, data->cpu_to_socket[z]);

  hwloc_debug_bitmap("Xen each_sockets is %s\n", each_socket);

  each_core = hwloc_bitmap_alloc();
  hwloc_bitmap_foreach_begin(i, each_socket)
    {
      struct hwloc_obj *sock =
        hwloc_alloc_setup_object(HWLOC_OBJ_SOCKET, i);
      sock->cpuset = hwloc_bitmap_alloc();

      for ( z = 0; z <= data->max_cpu_id; ++z )
        if (data->cpu_to_socket[z] == i)
          hwloc_bitmap_set(sock->cpuset, z);

      hwloc_insert_object_by_cpuset(topology, sock);
      hwloc_debug_1arg_bitmap(" Xen cpus on socket %u are %s\n", i, sock->cpuset);

      /* Core information (Core IDs are enumerated per-socket in Xen ) */
      hwloc_bitmap_zero(each_core);
      for ( z = 0; z <= data->max_cpu_id; ++z )
        if (data->cpu_to_socket[z] == i &&
            data->cpu_to_core[z] != ~0U )
          hwloc_bitmap_set(each_core, data->cpu_to_core[z]);
      hwloc_debug_bitmap("  Xen each_core is %s\n", each_core);

      hwloc_bitmap_foreach_begin(j, each_core)
        {
          struct hwloc_obj *core =
            hwloc_alloc_setup_object(HWLOC_OBJ_CORE, j);
          core->cpuset = hwloc_bitmap_alloc();

          for ( z = 0; z <= data->max_cpu_id; ++z )
            if (data->cpu_to_socket[z] == i &&
                data->cpu_to_core[z] == j)
              hwloc_bitmap_set(core->cpuset, z);

          hwloc_insert_object_by_cpuset(topology, core);
          hwloc_debug_1arg_bitmap("   Xen cpus on core %u are %s\n", j, core->cpuset);
        }
      hwloc_bitmap_foreach_end();
    }
  hwloc_bitmap_foreach_end();
  hwloc_bitmap_free(each_core);
  hwloc_bitmap_free(each_socket);

  /* Node information */
  each_node = hwloc_bitmap_alloc();
  for ( z = 0; z <= data->max_cpu_id; ++z )
    if (data->cpu_to_node[z] != ~0U)
      hwloc_bitmap_set(each_node, data->cpu_to_node[z]);

  hwloc_debug_bitmap("Xen each_node is %s\n", each_node);

  hwloc_bitmap_foreach_begin(i, each_node)
    {
      struct hwloc_obj *node =
        hwloc_alloc_setup_object(HWLOC_OBJ_NODE, i);
      node->cpuset = hwloc_bitmap_alloc();

      for ( z = 0; z <= data->max_cpu_id; ++z )
        if (data->cpu_to_node[z] == i)
          hwloc_bitmap_set(node->cpuset, z);

      /* Fill in some memory information, as we have it to hand.
       * Xen doesn't really do superpages yet. */
      if (data->node_to_memsize[i] != ~0U) {
        node->memory.page_types_len = 1;
        node->memory.page_types = calloc(1, sizeof *node->memory.page_types);

        node->memory.page_types[0].size = 4096;
        node->memory.page_types[0].count = data->node_to_memsize[i] >> 12;
        node->memory.local_memory = data->node_to_memsize[i];
      }

      hwloc_insert_object_by_cpuset(topology, node);

      hwloc_debug_1arg_bitmap(" Xen cpus on node %u are %s\n", i, node->cpuset);
    }
  hwloc_bitmap_foreach_end();
  hwloc_bitmap_free(each_node);


  hwloc_debug("All done discovering Xen topology\n\n");
  hwloc_obj_add_info(hwloc_get_root_obj(topology), "Backend", "Xen");

  free_xen_info(data);

  return 1;
}

/* Free the backend private data */
static void
hwloc_xen_backend_disable(struct hwloc_backend *backend)
{
  struct hwloc_xen_priv *priv = backend->private_data;

  if (priv) {

    if (priv->xch)
      xc_interface_close(priv->xch);
    free(priv);

    backend->private_data = NULL;
  }
}

/* Try to set up Xen topology enumeration */
static struct hwloc_backend *
hwloc_xen_component_instantiate(struct hwloc_disc_component *component,
                                const void *_data1 __hwloc_attribute_unused,
                                const void *_data2 __hwloc_attribute_unused,
                                const void *_data3 __hwloc_attribute_unused)
{
  struct hwloc_backend *backend = NULL;
  struct hwloc_xen_priv *priv = NULL;

  backend = hwloc_backend_alloc(component);
  if (!backend)
    goto err;

  priv = malloc(sizeof *priv);
  if (!priv)
    goto err;

  priv->logger.vmessage = xenctl_logfn;

  /* This will fail if we are not running as root in dom0. */
  priv->xch = xc_interface_open(&priv->logger, &priv->logger, 0);
  if (!priv->xch) {
    hwloc_debug("xc_interface_open() failed. Are you running as root in dom0?"
                " Disabling xen component.\n");
    goto err;
  }

  backend->private_data = priv;
  backend->discover = hwloc_xen_discover;
  backend->disable = hwloc_xen_backend_disable;

  return backend;

 err:
  if (priv && priv->xch)
    xc_interface_close(priv->xch);
  free(priv);
  free(backend);
  return NULL;
}

static struct hwloc_disc_component hwloc_xen_disc_component = {
  HWLOC_DISC_COMPONENT_TYPE_CPU,
  "xen",

  /* If Xen system support works, we can be absolutely certain that all other
   * CPU discovery methods will be wrong. */
  HWLOC_DISC_COMPONENT_TYPE_CPU | HWLOC_DISC_COMPONENT_TYPE_GLOBAL,
  hwloc_xen_component_instantiate,
  0, /* Should explicitly be requested with HWLOC_COMPONENTS=xen,... */
  NULL
};

#ifdef HWLOC_INSIDE_PLUGIN
HWLOC_DECLSPEC extern const struct hwloc_component hwloc_xen_component;
#endif

const struct hwloc_component hwloc_xen_component = {
  HWLOC_COMPONENT_ABI,
  HWLOC_COMPONENT_TYPE_DISC,
  0,
  &hwloc_xen_disc_component
};
