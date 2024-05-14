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

#include <libxl.h>


/* Xen private data for hwloc_backend */
struct hwloc_xen_priv {
  libxl_ctx *ctx;           /* libxl context  */
};

/* Topology and numa information from Xen */
struct hwloc_xen_info {
  libxl_cputopology *cpu;
  libxl_numainfo *node;
  uint32_t max_cpu_id;
  uint32_t max_node_id;
  int cpu_count;
  int node_count;
};

/* Free a hwloc_xen_info structure */
static void
free_xen_info(struct hwloc_xen_info *p)
{
  if (p) {
    free(p->cpu);
    free(p->node);
    free(p);
  }
}

/* Perform hypercalls to query Xen for information, and fill in a
 * hwloc_xen_info structure. */
static struct hwloc_xen_info *
hwloc_get_xen_info(libxl_ctx *ctx)
{
  struct hwloc_xen_info *data = malloc(sizeof *data);
  if (!data)
    return NULL;
  memset(data, 0, sizeof *data);
  data->cpu = libxl_get_cpu_topology(ctx, &data->cpu_count);
  data->max_cpu_id = data->cpu_count - 1;
  data->node = libxl_get_numainfo(ctx, &data->node_count);
  data->max_node_id = data->node_count - 1;
  return data;
}

#if 0 && defined(HWLOC_HAVE_X86_CPUID)
/* Ask Xen to execute CPUID on a specific PU on our behalf. */
static int xen_cpuid_fn(void *_priv, unsigned pu, unsigned *eax, unsigned *ebx,
                        unsigned *ecx, unsigned *edx)
{
  struct hwloc_xen_priv *priv = _priv;
  int ret = xc_xen_cpuid(priv->xch, pu, eax, ebx, ecx, edx);

  /* Possible failures are -EINVAL for bad PU or -EFAULT for problems moving
   * hypercall parameters. */
  assert(!ret && "Something went very wrong with xc_xen_cpuid()\n");

  return 0;
}
#endif /* HWLOC_HAVE_X86_CPUID */

/* Enumerate the full Xen system and fill in appropriate objects into the
 * topology. */
static int
hwloc_xen_discover(struct hwloc_backend *backend)
{
  struct hwloc_topology *topology = backend->topology;
  struct hwloc_xen_priv *priv = backend->private_data;
  struct hwloc_xen_info *data;
  hwloc_bitmap_t each_socket, each_node, each_core;
  uint32_t current_node, current_core, current_socket, cpu;

  /* Set by the x86 backend already if HWLOC_COMPONENTS=xen is not exported */
  if (hwloc_get_root_obj(topology)->cpuset) {
    /* fprintf(stderr, "export HWLOC_COMPONENTS=xen,x86 to enable the Xen backend before x86\n"); */
    /* return 0; */
  }

  hwloc_debug("Discovering Xen topology\n");

  data = hwloc_get_xen_info(priv->ctx);
  assert(data && "Failed to gather data from Xen\n");

  hwloc_debug("Xen topology information\n");
  hwloc_debug("  cpu count %"PRIu32", max cpu id %"PRIu32"\n",
              data->cpu_count, data->max_cpu_id);

  for ( cpu = 0; cpu <= data->max_cpu_id; ++cpu )
    hwloc_debug("  cpu[%3"PRIu32"], core %3"PRIu32", sock %3"PRIu32", node %3"PRIu32"\n",
                cpu, data->cpu[cpu].core, data->cpu[cpu].socket, data->cpu[cpu].node);

  hwloc_debug("Xen NUMA information\n");
  hwloc_debug("  numa count %"PRIu32", max numa id %"PRIu32"\n",
              data->node_count, data->max_node_id);
  for ( current_node = 0; current_node <= data->max_node_id; ++current_node )
    hwloc_debug("  node[%3"PRIu32"], size %"PRIu64"MB\n",
                current_node, data->node[current_node].size >> 20);

  hwloc_alloc_root_sets(topology);
  hwloc_setup_pu_level(topology, data->max_cpu_id+1);

  /* Socket information */
  each_socket = hwloc_bitmap_alloc();
  for ( cpu = 0; cpu <= data->max_cpu_id; ++cpu )
    if (data->cpu[cpu].socket != LIBXL_CPUTOPOLOGY_INVALID_ENTRY)
      hwloc_bitmap_set(each_socket, data->cpu[cpu].socket);

  hwloc_debug_bitmap("Xen each_sockets is %s\n", each_socket);
  each_core = hwloc_bitmap_alloc();
  hwloc_bitmap_foreach_begin(current_socket, each_socket)
    {
      struct hwloc_obj *sock =
        hwloc_alloc_setup_object(HWLOC_OBJ_SOCKET, current_socket);
      sock->cpuset = hwloc_bitmap_alloc();

      for ( cpu = 0; cpu <= data->max_cpu_id; ++cpu )
        if (data->cpu[cpu].socket == current_socket)
          hwloc_bitmap_set(sock->cpuset, cpu);

      hwloc_insert_object_by_cpuset(topology, sock);

      if (!hwloc_get_root_obj(topology)->cpuset) /* Skip this debug if x86 backend set the cpuset: */
        /* If HWLOC_COMPONENTS inits x86 before xen, this warns of wrong magic and segfaults: */
        hwloc_debug_1arg_bitmap(" Xen cpus on socket %u are %s\n", current_socket, sock->cpuset);

      /* Core information (Core IDs are enumerated per-socket in Xen ) */
      hwloc_bitmap_zero(each_core);
      for ( cpu = 0; cpu <= data->max_cpu_id; ++cpu )
        if (data->cpu[cpu].socket == current_socket &&
            data->cpu[cpu].core != LIBXL_CPUTOPOLOGY_INVALID_ENTRY &&
            data->cpu[cpu].node != LIBXL_CPUTOPOLOGY_INVALID_ENTRY )
          hwloc_bitmap_set(each_core, data->cpu[cpu].core);
      hwloc_debug_bitmap("  Xen each_core is %s\n", each_core);

      hwloc_bitmap_foreach_begin(current_core, each_core)
        {
          struct hwloc_obj *core =
            hwloc_alloc_setup_object(HWLOC_OBJ_CORE, current_core);
          core->cpuset = hwloc_bitmap_alloc();

          for ( cpu = 0; cpu <= data->max_cpu_id; ++cpu )
            if (data->cpu[cpu].socket == current_socket &&
                data->cpu[cpu].core == current_core)
              hwloc_bitmap_set(core->cpuset, cpu);

          hwloc_insert_object_by_cpuset(topology, core);
          hwloc_debug_1arg_bitmap("   Xen cpus on core %u are %s\n", current_core, core->cpuset);
        }
      hwloc_bitmap_foreach_end();
    }
  hwloc_bitmap_foreach_end();
  hwloc_bitmap_free(each_core);
  hwloc_bitmap_free(each_socket);

  /* Node information */
  each_node = hwloc_bitmap_alloc();
  for ( cpu = 0; cpu <= data->max_cpu_id; ++cpu )
    if (data->cpu[cpu].node != LIBXL_NUMAINFO_INVALID_ENTRY)
      hwloc_bitmap_set(each_node, data->cpu[cpu].node);

  hwloc_debug_bitmap("Xen each_node is %s\n", each_node);

  hwloc_bitmap_foreach_begin(current_node, each_node)
    {
      struct hwloc_obj *node =
        hwloc_alloc_setup_object(HWLOC_OBJ_NODE, current_node);
      node->cpuset = hwloc_bitmap_alloc();

      for ( cpu = 0; cpu <= data->max_cpu_id; ++cpu )
        if (data->cpu[cpu].node == current_node)
          hwloc_bitmap_set(node->cpuset, cpu);

      /* Fill in some memory information, as we have it to hand.
       * Xen doesn't really do superpages yet. */
      if (data->node[current_node].size != LIBXL_NUMAINFO_INVALID_ENTRY) {
        node->memory.page_types_len = 1;
        node->memory.page_types = calloc(1, sizeof *node->memory.page_types);

        node->memory.page_types[0].size = 4096;
        node->memory.page_types[0].count = data->node[current_node].size >> 12;
        node->memory.local_memory = data->node[current_node].size;
      }

      hwloc_insert_object_by_cpuset(topology, node);

      hwloc_debug_1arg_bitmap(" Xen cpus on node %u are %s\n", current_node, node->cpuset);
    }
  hwloc_bitmap_foreach_end();
  hwloc_bitmap_free(each_node);

  hwloc_debug("All done discovering Xen topology\n\n");
  hwloc_obj_add_info(hwloc_get_root_obj(topology), "Backend", "Xen");

#if 0 && defined(HWLOC_HAVE_X86_CPUID)
  hwloc_debug("Using CPUID to find cache information\n\n");
  hwloc_x86_cpuid_discovery(
      topology, data->max_cpu_id + 1,
      HWLOC_X86_CPUID_DISC_FLAG_CACHES | HWLOC_X86_CPUID_DISC_FLAG_CPUINFO,
      xen_cpuid_fn, priv);
#endif /* HWLOC_HAVE_X86_CPUID */

  free_xen_info(data);

  return 1;
}

/* Free the backend private data */
static void
hwloc_xen_backend_disable(struct hwloc_backend *backend)
{
  struct hwloc_xen_priv *priv = backend->private_data;

  if (priv) {
    if (priv->ctx)
      libxl_ctx_free(priv->ctx);
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

  backend->private_data = priv = malloc(sizeof *priv);
  if (!priv)
    goto err;

  /* This will fail if we are not running as root in dom0. */
  if (libxl_ctx_alloc(&priv->ctx, LIBXL_VERSION, 0, NULL)) {
      fprintf(stderr, "xen: libxl_ctx_alloc failed. Need to run as root in dom0.\n");
      goto err;
  }

  backend->discover = hwloc_xen_discover;
  backend->disable = hwloc_xen_backend_disable;

  return backend;

 err:
  hwloc_xen_backend_disable(backend);
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
