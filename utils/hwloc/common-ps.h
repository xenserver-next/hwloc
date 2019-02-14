/*
 * Copyright © 2009-2019 Inria.  All rights reserved.
 * Copyright © 2009-2012 Université Bordeaux
 * Copyright © 2009-2011 Cisco Systems, Inc.  All rights reserved.
 * See COPYING in top-level directory.
 */

#ifndef UTILS_HWLOC_PS_H
#define UTILS_HWLOC_PS_H

#include <private/autogen/config.h>
#include <hwloc.h>

#define HWLOC_PS_TIDNAME_MAX 16 /* specified in pthread_setname_np.3 */

struct hwloc_ps_process {
  long pid;
  char string[1024];
  char name[64];
  hwloc_bitmap_t cpuset;
  int bound;
  unsigned nthreads;
  unsigned nboundthreads;
  struct hwloc_ps_thread {
    long tid;
    hwloc_bitmap_t cpuset;
    int bound;
    char name[HWLOC_PS_TIDNAME_MAX];
  } *threads;
};

#define HWLOC_PS_FLAG_THREADS (1UL<<0)
#define HWLOC_PS_FLAG_LASTCPULOCATION (1UL<<1)

int hwloc_ps_read_process(hwloc_topology_t topology, hwloc_const_bitmap_t topocpuset,
			  struct hwloc_ps_process *proc,
			  unsigned long flags, char *pidcmd);

int hwloc_ps_foreach_process(hwloc_topology_t topology, hwloc_const_bitmap_t topocpuset,
			     void (*callback)(hwloc_topology_t topology, struct hwloc_ps_process *proc, void *cbdata),
			     void *cbdata,
			     unsigned long flags, char *pidcmd);

void hwloc_ps_free_process(struct hwloc_ps_process *proc);

#endif /* UTILS_HWLOC_PS_H */
