/*
 * Copyright © 2013-2014,2024 Citrix Systems Ltd.
 * Copyright © 2014 Inria.  All rights reserved.
 * See COPYING in top-level directory.
 */

#include <hwloc.h>
#include <hwloc/plugins.h>
#include <private/autogen/config.h>
#include <private/debug.h>
#include <private/misc.h>
#include <private/private.h>

#include <assert.h>
#include <inttypes.h>

#include <libxl.h>

/* Xen private data for hwloc_backend */
struct hwloc_xen_priv {
  libxl_ctx *ctx; /* libxl context  */
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

/* Add Xen Information to the Xen backend's XML node */
static void
hwloc_xen_add_infos(hwloc_obj_t root, libxl_ctx *ctx)
{
  char buf[128];
  const libxl_version_info *info;
  libxl_scheduler sched;
  libxl_physinfo phys;
  int rc;

  hwloc_obj_add_info(root, "Backend", "Xen");
  hwloc_obj_add_info(root, "XenBackendVersion", "0.1.0");

  if (libxl_get_physinfo(ctx, &phys) != 0) {
    fprintf(stderr, "libxl_physinfo failed.\n");
    return;
  }
  if (!(info = libxl_get_version_info(ctx))) {
    fprintf(stderr, "libxl_get_version_info failed.\n");
    return;
  }
  rc = libxl_get_scheduler(ctx);
  if (rc < 0) {
    fprintf(stderr, "get_scheduler sysctl failed.\n");
    return;
  }
  sched = rc;

  snprintf(buf,
           sizeof buf,
           "%d.%d%s",
           info->xen_version_major,
           info->xen_version_minor,
           info->xen_version_extra);
  hwloc_obj_add_info(root, "XenVersion", buf);
  hwloc_obj_add_info(root, "XenCommandLine", info->commandline);
  hwloc_obj_add_info(root, "XenCompileDate", info->compile_date);
  hwloc_obj_add_info(root, "XenChangeSet", info->changeset);
  hwloc_obj_add_info(root, "XenBuildId", info->build_id);

  snprintf(buf,
           sizeof buf,
           "%08x:%08x:%08x:%08x:%08x:%08x:%08x:%08x",
           phys.hw_cap[0],
           phys.hw_cap[1],
           phys.hw_cap[2],
           phys.hw_cap[3],
           phys.hw_cap[4],
           phys.hw_cap[5],
           phys.hw_cap[6],
           phys.hw_cap[7]);
  hwloc_obj_add_info(root, "XenHwCaps", buf);

  snprintf(buf,
           sizeof buf,
           "%s%s%s%s%s%s%s%s%s%s%s",
           phys.cap_pv ? " pv" : "",
           phys.cap_hvm ? " hvm" : "",
           phys.cap_hvm && phys.cap_hvm_directio ? " hvm_directio" : "",
           phys.cap_pv && phys.cap_hvm_directio ? " pv_directio" : "",
           phys.cap_hap ? " hap" : "",
           phys.cap_shadow ? " shadow" : "",
           phys.cap_iommu_hap_pt_share ? " iommu_hap_pt_share" : "",
           phys.cap_vmtrace ? " vmtrace" : "",
           phys.cap_vpmu ? " vpmu" : "",
           phys.cap_gnttab_v1 ? " gnttab-v1" : "",
           phys.cap_gnttab_v2 ? " gnttab-v2" : "");
  hwloc_obj_add_info(root, "XenVirtCaps", buf + 1);
  hwloc_obj_add_info(root, "XenScheduler", libxl_scheduler_to_string(sched));
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
hwloc_xen_discover(struct hwloc_backend *backend,
                   struct hwloc_disc_status *dstatus)
{
  struct hwloc_topology *topology = backend->topology;
  struct hwloc_xen_priv *priv = HWLOC_BACKEND_PRIVATE_DATA(backend);
  struct hwloc_xen_info *data;
  hwloc_bitmap_t each_socket, each_node, each_core;
  uint32_t current_node, current_core, current_socket, cpu;

  if (topology->levels[0][0]->cpuset)
    return -1;

  assert(dstatus->phase == HWLOC_DISC_PHASE_CPU);

  hwloc_debug("Discovering Xen topology\n");

  data = hwloc_get_xen_info(priv->ctx);
  assert(data && "Failed to gather data from Xen\n");

  /*
   * Prevent other backends from discovering CPUs: As Dom0 does not see all CPUs
   * on a larger NUMA system, the Linux backend would only see the first 16 CPUs
   * this would cause all other CPUs, sockets and Nodes to be removed.
   */
  dstatus->phase = 0;

  hwloc_debug("Xen topology information\n");
  hwloc_debug("  cpu count %" PRIu32 ", max cpu id %" PRIu32 "\n",
              data->cpu_count,
              data->max_cpu_id);

  for (cpu = 0; cpu <= data->max_cpu_id; ++cpu)
    hwloc_debug("  cpu[%3" PRIu32 "], core %3" PRIu32 ", sock %3" PRIu32
                ", node %3" PRIu32 "\n",
                cpu,
                data->cpu[cpu].core,
                data->cpu[cpu].socket,
                data->cpu[cpu].node);

  hwloc_debug("Xen NUMA information\n");
  hwloc_debug("  numa count %" PRIu32 ", max numa id %" PRIu32 "\n",
              data->node_count,
              data->max_node_id);
  for (current_node = 0; current_node <= data->max_node_id; ++current_node)
    hwloc_debug("  node[%3" PRIu32 "], size %" PRIu64 "MB\n",
                current_node,
                data->node[current_node].size >> 20);

  hwloc_alloc_root_sets(topology->levels[0][0]);
  hwloc_setup_pu_level(topology, data->max_cpu_id + 1);

  /* Socket information */
  each_socket = hwloc_bitmap_alloc();
  for (cpu = 0; cpu <= data->max_cpu_id; ++cpu)
    if (data->cpu[cpu].socket != LIBXL_CPUTOPOLOGY_INVALID_ENTRY)
      hwloc_bitmap_set(each_socket, data->cpu[cpu].socket);
  hwloc_debug_bitmap("Xen each_sockets is %s\n", each_socket);

  /* Cores */
  each_core = hwloc_bitmap_alloc();
  hwloc_bitmap_foreach_begin(current_socket, each_socket)
  {
    struct hwloc_obj *sock = hwloc_alloc_setup_object(topology,
                                                      HWLOC_OBJ_PACKAGE,
                                                      current_socket);
    sock->cpuset = hwloc_bitmap_alloc();

    for (cpu = 0; cpu <= data->max_cpu_id; ++cpu)
      if (data->cpu[cpu].socket == current_socket)
        hwloc_bitmap_set(sock->cpuset, cpu);

    hwloc__insert_object_by_cpuset(topology, NULL, sock, "xen:package");

    if (!hwloc_get_root_obj(topology)->cpuset) /* Skip this debug if x86 backend
                                                  set the cpuset: */
      /* If HWLOC_COMPONENTS inits x86 before xen, this warns of wrong magic and
       * segfaults: */
      hwloc_debug_1arg_bitmap(" Xen cpus on socket %u are %s\n",
                              current_socket,
                              sock->cpuset);

    /* Core information (Core IDs are enumerated per-socket in Xen ) */
    hwloc_bitmap_zero(each_core);
    for (cpu = 0; cpu <= data->max_cpu_id; ++cpu)
      if (data->cpu[cpu].socket == current_socket &&
          data->cpu[cpu].core != LIBXL_CPUTOPOLOGY_INVALID_ENTRY &&
          data->cpu[cpu].node != LIBXL_CPUTOPOLOGY_INVALID_ENTRY)
        hwloc_bitmap_set(each_core, data->cpu[cpu].core);
    hwloc_debug_bitmap("  Xen each_core is %s\n", each_core);

    hwloc_bitmap_foreach_begin(current_core, each_core)
    {
      struct hwloc_obj *core = hwloc_alloc_setup_object(topology,
                                                        HWLOC_OBJ_CORE,
                                                        current_core);
      core->cpuset = hwloc_bitmap_alloc();

      for (cpu = 0; cpu <= data->max_cpu_id; ++cpu)
        if (data->cpu[cpu].socket == current_socket &&
            data->cpu[cpu].core == current_core)
          hwloc_bitmap_set(core->cpuset, cpu);

      hwloc__insert_object_by_cpuset(topology, NULL, core, "xen:core");
      hwloc_debug_1arg_bitmap("   Xen cpus on core %u are %s\n",
                              current_core,
                              core->cpuset);
    }
    hwloc_bitmap_foreach_end();
  }
  hwloc_bitmap_foreach_end();
  hwloc_bitmap_free(each_core);
  hwloc_bitmap_free(each_socket);

  /* Node information */
  each_node = hwloc_bitmap_alloc();
  for (cpu = 0; cpu <= data->max_cpu_id; ++cpu)
    if (data->cpu[cpu].node != LIBXL_NUMAINFO_INVALID_ENTRY)
      hwloc_bitmap_set(each_node, data->cpu[cpu].node);

  hwloc_debug_bitmap("Xen each_node is %s\n", each_node);

  hwloc_bitmap_foreach_begin(current_node, each_node)
  {
    struct hwloc_obj *node = hwloc_alloc_setup_object(topology,
                                                      HWLOC_OBJ_NUMANODE,
                                                      current_node);
    node->cpuset = hwloc_bitmap_alloc();
    node->nodeset = hwloc_bitmap_alloc();
    hwloc_bitmap_set(node->nodeset, current_node);

    for (cpu = 0; cpu <= data->max_cpu_id; ++cpu)
      if (data->cpu[cpu].node == current_node) {
        /* Add PU */
        hwloc_obj_t pu = hwloc_alloc_setup_object(topology, HWLOC_OBJ_PU, cpu);
        pu->cpuset = hwloc_bitmap_alloc();
        hwloc_bitmap_set(pu->cpuset, cpu);
        hwloc__insert_object_by_cpuset(topology, NULL, pu, "xen:pu");

        /* Add this cpu to its node */
        hwloc_bitmap_set(node->cpuset, cpu);
      }

    /* Fill in some memory information, as we have it to hand.
     * Xen doesn't really do superpages yet? */
    if (data->node[current_node].size != LIBXL_NUMAINFO_INVALID_ENTRY) {
      struct hwloc_numanode_attr_s *numanode = &node->attr->numanode;

      numanode->local_memory = data->node[current_node].size;
      numanode->page_types_len = 1;
      numanode->page_types = calloc(1, sizeof *numanode->page_types);
      numanode->page_types[0].size = 4096;
      numanode->page_types[0].count = data->node[current_node].size >> 12;
      topology->support.discovery->numa_memory = 1;
    }
    hwloc__insert_object_by_cpuset(topology, NULL, node, "xen:numanode");
    hwloc_debug_1arg_bitmap(" Xen cpus on node %u are %s\n",
                            current_node,
                            node->cpuset);
  }

  hwloc_bitmap_foreach_end();
  hwloc_bitmap_free(each_node);

  topology->support.discovery->pu = 1;
  topology->support.discovery->numa = each_node ? 1 : 0;

  hwloc_debug("All done discovering Xen topology\n\n");
  hwloc_xen_add_infos(hwloc_get_root_obj(topology), priv->ctx);

#if 0 && defined(HWLOC_HAVE_X86_CPUID)
  hwloc_debug("Using CPUID to find cache information\n\n");
  hwloc_x86_cpuid_discovery(
      topology, data->max_cpu_id + 1,
      //HWLOC_X86_CPUID_DISC_FLAG_CACHES | HWLOC_X86_CPUID_DISC_FLAG_CPUINFO,
      xen_cpuid_fn, priv);
#endif /* HWLOC_HAVE_X86_CPUID */

  free_xen_info(data);

  return 1;
}

/* Free the backend private data */
static void
hwloc_xen_backend_disable(struct hwloc_backend *backend)
{
  struct hwloc_xen_priv *priv = HWLOC_BACKEND_PRIVATE_DATA(backend);
  if (priv->ctx) {
    libxl_ctx_free(priv->ctx);
  }
}

/* Try to set up Xen topology enumeration */
static struct hwloc_backend *
hwloc_xen_component_instantiate(struct hwloc_topology *topology,
                                struct hwloc_disc_component *component,
                                unsigned excluded_phases
                                    __hwloc_attribute_unused,
                                const void *_data1 __hwloc_attribute_unused,
                                const void *_data2 __hwloc_attribute_unused,
                                const void *_data3 __hwloc_attribute_unused)
{
  struct hwloc_backend *backend = NULL;
  struct hwloc_xen_priv *priv;

  /*
   * When we have no access to Xen privileged interfaces to make hypercalls,
   * skip the backend to prevent aborting the progam in libxl_ctx_alloc()
   */
  if (access("/dev/xen/privcmd", R_OK) && access("/proc/xen/privcmd", R_OK))
    return NULL;

  backend = hwloc_backend_alloc(topology, component, sizeof(*priv));
  if (!backend)
    goto err;

  priv = HWLOC_BACKEND_PRIVATE_DATA(backend);

  /* This will abort the program when not running as root in dom0 */
  if (libxl_ctx_alloc(&priv->ctx, LIBXL_VERSION, 0, NULL)) {
    fprintf(stderr,
            "xen: libxl_ctx_alloc failed. Need to run as root in dom0.\n");
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
  .name = "xen",

  /* Discovery phases performed by this component */
  .phases = HWLOC_DISC_PHASE_CPU,

  /* Discovery phases to exclude */
  .excluded_phases = HWLOC_DISC_PHASE_GLOBAL,

  /* Discovery phases performed by this component */
  .instantiate = hwloc_xen_component_instantiate,

  /*
   * Component priority:
   * Used to sort topology->components, higher priority first.
   * Also used to decide between two components with the same name.
   *
   * Usual values are
   * 50 for native OS (or platform) components such as the Linux component
   * 45 for x86,
   * 40 for no-OS fallback,
   * 30 for global components (xml, synthetic),
   * 20 for pci,
   * 10 for other misc components (opencl etc.).
   *
   * Run the Xen backend before all other CPU backends:
   * Because Dom0 does not see all CPUs on a larger NUMA system,
   * the Linux backend would invaliate all CPUs except the first 16 CPUs.
   * This would cause those other CPUs, sockets and Nodes to be removed.
   *
   * To prevent this, we need to run the Xen Backend first and then end
   * discovery phase HWLOC_DISC_PHASE_CPU so that no other backends are
   * called after the Xen Backend has defined the CPU and memory topology.
   *
   * Initially, set hwloc_disc_status->phase from HWLOC_DISC_PHASE_CPU to 0
   * to terminate the CPU topology discovery phase after we Xen discovery.
   * A priority of 100 should be higher than all other CPU backends:
   */
  .priority = 100,

  /*
   * Enable the Xen backend by default: If not running as root in Dom0,
   * will not be able to issue Xen Hypercalls, and will make a pass.
   */
  .enabled_by_default = 1,

  /*
   * Next component
   * Used internally to list components by priority on topology->components
   * (the component structure is usually read-only,
   *  the core copies it before using this field for queueing)
   */
  .next = NULL
};

#ifdef HWLOC_INSIDE_PLUGIN
HWLOC_DECLSPEC extern const struct hwloc_component hwloc_xen_component;
#endif

const struct hwloc_component hwloc_xen_component = {
  HWLOC_COMPONENT_ABI,       NULL, NULL,
  HWLOC_COMPONENT_TYPE_DISC, 0,    &hwloc_xen_disc_component
};
