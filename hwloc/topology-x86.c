/*
 * Copyright © 2010-2015 Inria.  All rights reserved.
 * Copyright © 2010-2013 Université Bordeaux
 * Copyright © 2010-2011 Cisco Systems, Inc.  All rights reserved.
 * See COPYING in top-level directory.
 *
 *
 * This backend is only used when the operating system does not export
 * the necessary hardware topology information to user-space applications.
 * Currently, only the FreeBSD backend relies on this x86 backend.
 *
 * Other backends such as Linux have their own way to retrieve various
 * pieces of hardware topology information from the operating system
 * on various architectures, without having to use this x86-specific code.
 */

#include <private/autogen/config.h>
#include <hwloc.h>
#include <private/private.h>
#include <private/debug.h>
#include <private/misc.h>

#include <private/cpuid-x86.h>

#include <sys/types.h>
#include <dirent.h>

struct hwloc_x86_backend_data_s {
  unsigned nbprocs;
  hwloc_bitmap_t apicid_set;
  int apicid_unique;
  char *src_cpuiddump_path;
};

/************************************
 * Management of cpuid dump as input
 */

struct cpuiddump {
  unsigned nr;
  struct cpuiddump_entry {
    unsigned inmask; /* which of ine[abcd]x are set on input */
    unsigned ineax;
    unsigned inebx;
    unsigned inecx;
    unsigned inedx;
    unsigned outeax;
    unsigned outebx;
    unsigned outecx;
    unsigned outedx;
  } *entries;
};

static void
cpuiddump_free(struct cpuiddump *cpuiddump)
{
  if (cpuiddump->nr)
    free(cpuiddump->entries);
  free(cpuiddump);
}

static struct cpuiddump *
cpuiddump_read(const char *dirpath, unsigned idx)
{
  struct cpuiddump *cpuiddump;
  struct cpuiddump_entry *cur;
  char *filename;
  size_t filenamelen = strlen(dirpath) + 15;
  FILE *file;
  char line[128];
  unsigned nr;

  cpuiddump = malloc(sizeof(*cpuiddump));
  cpuiddump->nr = 0; /* return a cpuiddump that will raise errors because it matches nothing */

  filename = malloc(filenamelen);
  snprintf(filename, filenamelen, "%s/pu%u", dirpath, idx);
  file = fopen(filename, "r");
  if (!file) {
    fprintf(stderr, "Could not read dumped cpuid file %s\n", filename);
    free(filename);
    return cpuiddump;
  }
  free(filename);

  nr = 0;
  while (fgets(line, sizeof(line), file))
    nr++;
  cpuiddump->entries = malloc(nr * sizeof(struct cpuiddump_entry));

  fseek(file, 0, SEEK_SET);
  cur = &cpuiddump->entries[0];
  nr = 0;
  while (fgets(line, sizeof(line), file)) {
    if (*line == '#')
      continue;
    if (sscanf(line, "%x %x %x %x %x => %x %x %x %x",
              &cur->inmask,
              &cur->ineax, &cur->inebx, &cur->inecx, &cur->inedx,
              &cur->outeax, &cur->outebx, &cur->outecx, &cur->outedx) == 9) {
      cur++;
      nr++;
    }
  }
  cpuiddump->nr = nr;
  fclose(file);
  return cpuiddump;
}

static void
cpuiddump_find_by_input(unsigned *eax, unsigned *ebx, unsigned *ecx, unsigned *edx, struct cpuiddump *cpuiddump)
{
  unsigned i;

  for(i=0; i<cpuiddump->nr; i++) {
    struct cpuiddump_entry *entry = &cpuiddump->entries[i];
    if ((entry->inmask & 0x1) && *eax != entry->ineax)
      continue;
    if ((entry->inmask & 0x2) && *ebx != entry->inebx)
      continue;
    if ((entry->inmask & 0x4) && *ecx != entry->inecx)
      continue;
    if ((entry->inmask & 0x8) && *edx != entry->inedx)
      continue;
    *eax = entry->outeax;
    *ebx = entry->outebx;
    *ecx = entry->outecx;
    *edx = entry->outedx;
    return;
  }

  fprintf(stderr, "Couldn't find %x,%x,%x,%x in dumped cpuid, returning 0s.\n",
          *eax, *ebx, *ecx, *edx);
  *eax = 0;
  *ebx = 0;
  *ecx = 0;
  *edx = 0;
}

static void cpuid_or_from_dump(unsigned *eax, unsigned *ebx, unsigned *ecx, unsigned *edx, struct cpuiddump *src_cpuiddump)
{
  if (src_cpuiddump) {
    cpuiddump_find_by_input(eax, ebx, ecx, edx, src_cpuiddump);
  } else {
    hwloc_x86_cpuid(eax, ebx, ecx, edx);
  }
}

/*******************************
 * Core detection routines and structures
 */

#define has_topoext(features) ((features)[6] & (1 << 22))
#define has_x2apic(features) ((features)[4] & (1 << 21))

struct cacheinfo {
  unsigned type;
  unsigned level;
  unsigned nbthreads_sharing;

  unsigned linesize;
  unsigned linepart;
  int ways;
  unsigned sets;
  unsigned long size;
  char inclusiveness;

};

struct tlbinfo {
  unsigned type;
  unsigned entriesnumber4KB;
  unsigned entriesnumber2MB;
  unsigned entriesnumber4MB;
  unsigned entriesnumber1GB;
  unsigned associativity;
};

struct procinfo {
  unsigned present;
  unsigned apicid;
  unsigned max_log_proc;
  unsigned max_nbcores;
  unsigned max_nbthreads;
  unsigned packageid;
  unsigned nodeid;
  unsigned unitid;
  unsigned logprocid;
  unsigned threadid;
  unsigned coreid;
  unsigned *otherids;
  unsigned levels;
  unsigned numcaches;
  struct cacheinfo *cache;
  unsigned numtlbs;
  struct tlbinfo *tlbs;
  char cpuvendor[13];
  char cpumodel[3*4*4+1];
  unsigned cpustepping;
  unsigned cpumodelnumber;
  unsigned cpufamilynumber;
};

enum cpuid_type {
  intel,
  amd,
  unknown
};

static void fill_amd_cache(struct procinfo *infos, unsigned level, int type, unsigned cpuid)
{
  struct cacheinfo *cache;
  unsigned cachenum;
  unsigned long size = 0;

  if (level == 1)
    size = ((cpuid >> 24)) << 10;
  else if (level == 2)
    size = ((cpuid >> 16)) << 10;
  else if (level == 3)
    size = ((cpuid >> 18)) << 19;
  if (!size)
    return;

  cachenum = infos->numcaches++;
  infos->cache = realloc(infos->cache, infos->numcaches*sizeof(*infos->cache));
  cache = &infos->cache[cachenum];

  cache->type = type;
  cache->level = level;
  if (level <= 2)
    cache->nbthreads_sharing = 1;
  else
    cache->nbthreads_sharing = infos->max_log_proc;
  cache->linesize = cpuid & 0xff;
  cache->linepart = 0;
  cache->inclusiveness = 0;
  /*get inclusiveness old AMD (K8-K10 suposed to have exclusive cache)
  *http://www.cpu-world.com/CPUs/K8/AMD-Opteron%20144%20-%20OSA144CCO5AG.html
  *http://www.cpu-world.com/CPUs/K10/AMD-Opteron%206164%20HE%20-%20OS6164VATCEGO.html
  */
  if (level == 1) {
    cache->ways = (cpuid >> 16) & 0xff;
    if (cache->ways == 0xff)
      /* Fully associative */
      cache->ways = -1;
  } else {

    static const unsigned ways_tab[] = { 0, 1, 2, 0, 4, 0, 8, 0, 16, 0, 32, 48, 64, 96, 128, -1 };
    unsigned ways = (cpuid >> 12) & 0xf;
    cache->ways = ways_tab[ways];
  }
  cache->size = size;
  cache->sets = 0;

      /*FIX AMD MagnyCours family 0x10 model 0x9 with 8 cores or more actually
       * have the L3 split in two halves, and associativity is divided as well (48)
       */
  if(infos->cpufamilynumber== 0x10 && infos->cpumodelnumber == 0x9 && level == 3
      && (cache->ways == -1 || (cache->ways % 2 == 0)) && cache->nbthreads_sharing >= 8){
          if(cache->nbthreads_sharing == 16)
            cache->nbthreads_sharing = 12; //this model has at most 12 pu by package 
          cache->nbthreads_sharing /= 2;
          cache->size /= 2;
          if(cache->ways != -1)
            cache->ways /= 2;
        }

  hwloc_debug("cache L%u t%u linesize %u ways %u size %luKB\n", cache->level, cache->nbthreads_sharing, cache->linesize, cache->ways, cache->size >> 10);
}
//TYPE : 0 : Instruction TLB, 1 : data TLB, x>=2 : Shared x-Level TLB
//PAGESIZE : string
//ASSOCIATIVITY : 0 Fully associative, 1 Direct mapped,  x>=2 : x-way associative

static void initialiseTLB(struct tlbinfo* intelTLBEnum, unsigned id,unsigned type,  unsigned entriesnumber4KB, unsigned entriesnumber2MB, unsigned entriesnumber4MB, unsigned entriesnumber1GB, unsigned associativity){
  intelTLBEnum[id].type = type;
  intelTLBEnum[id].associativity = associativity;
  intelTLBEnum[id].entriesnumber4KB = entriesnumber4KB;
  intelTLBEnum[id].entriesnumber2MB = entriesnumber2MB;
  intelTLBEnum[id].entriesnumber4MB = entriesnumber4MB;
  intelTLBEnum[id].entriesnumber1GB = entriesnumber1GB;
}

static void get_fill_intel_tlb(struct procinfo *infos, struct cpuiddump *src_cpuiddump){//if highest_ext_cpuid >= 0x02
  unsigned i,j;
  unsigned registers[4];
  static struct tlbinfo intelTLBEnum[0x100];
  if (intelTLBEnum[0x1].associativity == 0){ // initialise tlbinfo only once (it should be 4 if initialised)

    //initialiseTLB(struct tlbinfo* intelTLBEnum, unsigned id,unsigned type,  unsigned entriesnumber4KB, unsigned entriesnumber2MB, unsigned entriesnumber4MB, unsigned entriesnumber1GB, unsigned associativity)

    initialiseTLB(intelTLBEnum,0x01, 0,  32,   0,   0,   0,   4); //Instruction TLB: 4 KByte pages, 4-way set associative, 32 entries
    initialiseTLB(intelTLBEnum,0x02, 0,   0,   0,   2,   0,   0); //Instruction TLB: 4 MByte pages, fully associative, 2 entries
    initialiseTLB(intelTLBEnum,0x03, 1,  64,   0,   0,   0,   4); //Data TLB: 4 KByte pages, 4-way set associative, 64 entries
    initialiseTLB(intelTLBEnum,0x04, 1,   0,   0,   8,   0,   4); //Data TLB: 4 MByte pages, 4-way set associative, 8 entries
    initialiseTLB(intelTLBEnum,0x05, 1,   0,   0,  32,   0,   4); //Data TLB1: 4 MByte pages, 4-way set associative, 32 entries
    initialiseTLB(intelTLBEnum,0x0B, 0,   0,   0,   4,   0,   4); //Instruction TLB: 4 MByte pages, 4-way set associative, 4 entries
    initialiseTLB(intelTLBEnum,0x4F, 0,  32,   0,   0,   0,   1); //Instruction TLB: 4 KByte pages, 32 entries
    initialiseTLB(intelTLBEnum,0x50, 0,  64,  64,  64,   0,   1); //Instruction TLB: 4 KByte and 2-MByte or 4-MByte pages, 64 entries
    initialiseTLB(intelTLBEnum,0x51, 0, 128, 128, 128,   0,   1); //Instruction TLB: 4 KByte and 2-MByte or 4-MByte pages, 128 entries
    initialiseTLB(intelTLBEnum,0x52, 0, 256, 256, 256,   0,   1); //Instruction TLB: 4 KByte and 2-MByte or 4-MByte pages, 256 entries
    initialiseTLB(intelTLBEnum,0x55, 0,   0,   7,   7,   0,   0); //Instruction TLB: 2-MByte or 4-MByte pages, fully associative, 7 entries
    initialiseTLB(intelTLBEnum,0x56, 1,   0,   0,  16,   0,   4); //Data TLB0: 4 MByte pages, 4-way set associative, 16 entries
    initialiseTLB(intelTLBEnum,0x57, 1,  16,   0,   0,   0,   4); //Data TLB0: 4 KByte pages, 4-way associative, 16 entries
    initialiseTLB(intelTLBEnum,0x59, 1,  16,   0,   0,   0,   0); //Data TLB0: 4 KByte pages, fully associative, 16 entries
    initialiseTLB(intelTLBEnum,0x5A, 1,   0,  32,  32,   0,   4); //Data TLB0: 2-MByte or 4 MByte pages, 4-way set associative, 32 entries
    initialiseTLB(intelTLBEnum,0x5B, 1,  64,   0,  64,   0,   1); //Data TLB: 4 KByte and 4 MByte pages, 64 entries
    initialiseTLB(intelTLBEnum,0x5C, 1, 128,   0, 128,   0,   1); //Data TLB: 4 KByte and 4 MByte pages,128 entries
    initialiseTLB(intelTLBEnum,0x5D, 1, 256,   0, 256,   0,   1); //Data TLB: 4 KByte and 4 MByte pages,256 entries
    initialiseTLB(intelTLBEnum,0x61, 0,  48,   0,   0,   0,   0); //Instruction TLB: 4 KByte pages, fully associative, 48 entries
    initialiseTLB(intelTLBEnum,0x63, 1,   0,   0,   0,   4,   4); //Data TLB: 1 GByte pages, 4-way set associative, 4 entries
    initialiseTLB(intelTLBEnum,0x76, 0,   0,   8,   8,   0,   0); //Instruction TLB: 2M/4M pages, fully associative, 8 entries
    initialiseTLB(intelTLBEnum,0xA0, 1,  32,   0,   0,   0,   0); //DTLB: 4k pages, fully associative, 32 entries
    initialiseTLB(intelTLBEnum,0xB0, 0, 128,   0,   0,   0,   4); //Instruction TLB: 4 KByte pages, 4-way set associative, 128 entries
    initialiseTLB(intelTLBEnum,0xB1, 0,   0,   8,   4,   0,   4); //Instruction TLB: 2M pages, 4-way, 8 entries or 4M pages, 4-way, 4 entries
    initialiseTLB(intelTLBEnum,0xB2, 0,  64,   0,   0,   0,   4); //Instruction TLB: 4KByte pages, 4-way set associative, 64 entries
    initialiseTLB(intelTLBEnum,0xB3, 1, 128,   0,   0,   0,   4); //Data TLB: 4 KByte pages, 4-way set associative, 128 entries
    initialiseTLB(intelTLBEnum,0xB4, 1, 256,   0,   0,   0,   4); //Data TLB1: 4 KByte pages, 4-way associative, 256 entries
    initialiseTLB(intelTLBEnum,0xB5, 0,  64,   0,   0,   0,   8); //Instruction TLB: 4KByte pages, 8-way set associative, 64 entries
    initialiseTLB(intelTLBEnum,0xB6, 0, 128,   0,   0,   0,   8); //Instruction TLB: 4KByte pages, 8-way set associative, 128 entries
    initialiseTLB(intelTLBEnum,0xBA, 1,  64,   0,   0,   0,   4); //Data TLB1: 4 KByte pages, 4-way associative, 64 entries
    initialiseTLB(intelTLBEnum,0xC0, 1,   8,   0,   8,   0,   4); //Data TLB: 4 KByte and 4 MByte pages, 4-way associative, 8 entries
    initialiseTLB(intelTLBEnum,0xC1, 2,1024,1024,   0,   0,   8); //Shared 2nd-Level TLB: 4 KByte/2MByte pages, 8-way associative, 1024 entries
    initialiseTLB(intelTLBEnum,0xC2, 1,  16,  16,   0,   0,   4); //DTLB: 4 KByte/2 MByte pages, 4-way associative, 16 entries
    initialiseTLB(intelTLBEnum,0xC3, 2,1536,1536,   0,  16,   6); //Shared 2nd-Level TLB: 4 KByte /2 MByte pages, 6-way associative, 1536 entries. Also 1GBbyte pages, 4-way, 16 entries.
    initialiseTLB(intelTLBEnum,0xCA, 2, 512,   0,   0,   0,   4); //Shared 2nd-Level TLB: 4 KByte pages, 4-way associative, 512 entries

  }  

  infos->tlbs = NULL;
  infos->numtlbs = 0;
  registers[0] = 0x02; 
  cpuid_or_from_dump(&registers[0], &registers[1], &registers[2], &registers[3], src_cpuiddump);
  for(i=0;i<4;i++){// i register selection : (0 eax, 1 ebx, 2 ecx, 3 edx)
    if (registers[i]>>31){
      continue; //The most significant bit indicates whether the register contains valid information (0) or is reserved (1)
    }
    for(j=0;j<4;j++){// j byte of register selection. There can be an tlb descriptor by byte.
      unsigned tlbId = ((registers[i]>>8*j)&0xff);
      if(tlbId == 0x1)
        if(i==0 && j==0)//The least-significant byte in register EAX will always return 0x1. It must be ignored.
          continue;

      if(intelTLBEnum[tlbId].entriesnumber4KB != 0 ||intelTLBEnum[tlbId].entriesnumber2MB != 0 || intelTLBEnum[tlbId].entriesnumber4MB != 0 || intelTLBEnum[tlbId].entriesnumber1GB != 0){
        infos->numtlbs++;
        infos->tlbs = realloc(infos->tlbs,infos->numtlbs * sizeof(struct tlbinfo));
        infos->tlbs[infos->numtlbs-1] = intelTLBEnum[tlbId];
      }
    }
  }
}

static int add_tlb_from_amd_register(struct tlbinfo * tlbSet,unsigned* numTlb, unsigned regist, unsigned type, int size){
/*
  type: 0 : L1 instruction,   1 : L1 data,
   2 : L2 shared,    3 : L2 instruction,    4 : L2 data
  size: 0 : 4 KB pages,    1 : 2/4 MB pages,     2 : 1 GB pages
*/
  int associativity;
  if(type == 1 || type == 4)
    regist=regist>>16;
  associativity = (type>=2 || size == 2) ? (regist >> 12) & 0xF : (regist >> 8) & 0xFF;
  //L1 has 8 bit of entry number and 8 of associativity.
  //L2 (type>=2) and L1 1GB page syze have 12 bit of entry number and 4 of associativity.
  if (associativity == 0){
    return 0;//invalid or disabled TLB
  }
  if (type>=2 || size == 2)
    switch(associativity){ // AM2 L2 TLB / L1 1GB TLB: Associativity is an enum.
      case 0x1:
      case 0x2:
      case 0x4:
        tlbSet[*numTlb].associativity = associativity;
      break;
      case 0x6:
        tlbSet[*numTlb].associativity = 8;
      break;
      case 0x8:
        tlbSet[*numTlb].associativity = 16;
      break;
      case 0xA:
        tlbSet[*numTlb].associativity = 32;
      break;
      case 0xB:
        tlbSet[*numTlb].associativity = 48;
      break;
      case 0xC:
        tlbSet[*numTlb].associativity = 64;
      break;
      case 0xD:
        tlbSet[*numTlb].associativity = 96;
      break;
      case 0xE:
        tlbSet[*numTlb].associativity = 128;
      break;
      case 0xF:
        tlbSet[*numTlb].associativity = 0;
      break;
      default:
        return 0;// unknow associativity?
    }
  else
    tlbSet[*numTlb].associativity = associativity == 0xFF ? 0 : associativity;
    // AM2 L1 TLB : FF is fully associative, 1 direct mapped and i i-way associative

  tlbSet[*numTlb].type = type;
  tlbSet[*numTlb].entriesnumber4KB = (size == 0) ? regist & (type>=2 ? 0xFFF : 0xFF) : 0;
  tlbSet[*numTlb].entriesnumber2MB = (size == 1) ? regist & (type>=2 ? 0xFFF : 0xFF) : 0;
  tlbSet[*numTlb].entriesnumber4MB = tlbSet[*numTlb].entriesnumber2MB / 2;
  tlbSet[*numTlb].entriesnumber1GB = (size == 2) ? regist & 0xFFF : 0;
  (*numTlb)++;
  return 1;
}

static void get_fill_amd_tlb(struct procinfo *infos, struct cpuiddump *src_cpuiddump, unsigned highest_ext_cpuid){
  unsigned ebx,ecx,edx,eax;
  unsigned foundIL2TLB = 0;
  infos->numtlbs=0;//maximum of 12
  if(highest_ext_cpuid >= (eax = 0x80000005)){//get L1 TLB info
    cpuid_or_from_dump(&eax, &ebx, &ecx, &edx, src_cpuiddump);
    infos->tlbs = malloc(12 * sizeof(struct tlbinfo));

    add_tlb_from_amd_register(infos->tlbs, &infos->numtlbs,ebx,0,0);// instruction TLB 4 KB pages
    add_tlb_from_amd_register(infos->tlbs, &infos->numtlbs,eax,0,1);// instruction TLB 2/4 MB pages
    add_tlb_from_amd_register(infos->tlbs, &infos->numtlbs,ebx,1,0);// data TLB 4 KB pages
    add_tlb_from_amd_register(infos->tlbs, &infos->numtlbs,eax,1,1);// data TLB 2/4 MB pages
  }
  if(highest_ext_cpuid >= (eax = 0x80000006)){//get L2 TLB info
    cpuid_or_from_dump(&eax, &ebx, &ecx, &edx, src_cpuiddump);
    add_tlb_from_amd_register(infos->tlbs, &infos->numtlbs,ebx,3,0);// L2 instruction TLB 4 KB pages
    foundIL2TLB += add_tlb_from_amd_register(infos->tlbs, &infos->numtlbs,eax,3,1);// L2 instruction TLB 2/4 MB pages


    add_tlb_from_amd_register(infos->tlbs, &infos->numtlbs,ebx,4,0);// L2 data TLB 4 KB pages 
    add_tlb_from_amd_register(infos->tlbs, &infos->numtlbs,eax,4,1);// L2 data TLB 2/4 MB pages 
  }
  if(highest_ext_cpuid >= (eax = 0x80000019)){//get 1GB L1/2 TLB info
    cpuid_or_from_dump(&eax, &ebx, &ecx, &edx, src_cpuiddump);
    add_tlb_from_amd_register(infos->tlbs, &infos->numtlbs,eax,0,2);// instruction TLB 1 GB pages
    add_tlb_from_amd_register(infos->tlbs, &infos->numtlbs,eax,1,2);// data TLB 1 GB pages 

    foundIL2TLB += add_tlb_from_amd_register(infos->tlbs, &infos->numtlbs,ebx,3,2);// L2 instruction TLB 1 GB pages
    add_tlb_from_amd_register(infos->tlbs, &infos->numtlbs,ebx,4,2);// L2 data TLB 1 GB pages 
  }
  //error 658 CPUID Incorrectly Reports Large Page Support in L2 Instruction TLB
  if(!foundIL2TLB && infos->cpufamilynumber == 0x15 && infos->cpumodelnumber <= 0xF) {
    infos->tlbs[infos->numtlbs].type = 3;
    infos->tlbs[infos->numtlbs].associativity = 6;
    infos->tlbs[infos->numtlbs].entriesnumber4KB = 0;
    infos->tlbs[infos->numtlbs].entriesnumber2MB = 1024;
    infos->tlbs[infos->numtlbs].entriesnumber4MB = 512;
    infos->tlbs[infos->numtlbs].entriesnumber1GB = 1024;
  }

}

/* Fetch information from the processor itself thanks to cpuid and store it in
 * infos for summarize to analyze them globally */
static void look_proc(struct hwloc_backend *backend, struct procinfo *infos, unsigned highest_cpuid, unsigned highest_ext_cpuid, unsigned *features, enum cpuid_type cpuid_type, struct cpuiddump *src_cpuiddump)
{
  struct hwloc_x86_backend_data_s *data = backend->private_data;
  unsigned eax, ebx, ecx = 0, edx;
  unsigned cachenum;
  struct cacheinfo *cache;
  unsigned regs[4];
  unsigned _model, _extendedmodel, _family, _extendedfamily;

  infos->present = 1;

  /* on return from this function, the following fields must be set in infos:
   * packageid, nodeid, unitid, coreid, threadid, or -1
   * apicid
   * levels and levels slots in otherids[]
   * numcaches and numcaches slots in caches[]
   *
   * max_log_proc, max_nbthreads, max_nbcores, logprocid
   * are only used temporarily inside this function and its callees.
   */

  /* Get apicid, max_log_proc, packageid, logprocid from cpuid 0x01 */
  eax = 0x01;
  cpuid_or_from_dump(&eax, &ebx, &ecx, &edx, src_cpuiddump); 
  infos->apicid = ebx >> 24;
  if (edx & (1 << 28))
    infos->max_log_proc = 1 << hwloc_flsl(((ebx >> 16) & 0xff) - 1);
  else
    infos->max_log_proc = 1;
  hwloc_debug("APIC ID 0x%02x max_log_proc %u\n", infos->apicid, infos->max_log_proc);
  infos->packageid = infos->apicid / infos->max_log_proc;
  infos->logprocid = infos->apicid % infos->max_log_proc;
  hwloc_debug("phys %u thread %u\n", infos->packageid, infos->logprocid);

  /* Get cpu model/family/stepping numbers from same cpuid */
  _model          = (eax>>4) & 0xf;
  _extendedmodel  = (eax>>16) & 0xf;
  _family         = (eax>>8) & 0xf;
  _extendedfamily = (eax>>20) & 0xff;
  if ((cpuid_type == intel || cpuid_type == amd) && _family == 0xf) {
    infos->cpufamilynumber = _family + _extendedfamily;
  } else {
    infos->cpufamilynumber = _family;
  }
  if ((cpuid_type == intel && (_family == 0x6 || _family == 0xf))
      || (cpuid_type == amd && _family == 0xf)) {
    infos->cpumodelnumber = _model + (_extendedmodel << 4);
  } else {
    infos->cpumodelnumber = _model;
  }
  infos->cpustepping = eax & 0xf;

  /* Get cpu vendor string from cpuid 0x00 */
  memset(regs, 0, sizeof(regs));
  regs[0] = 0;
  cpuid_or_from_dump(&regs[0], &regs[1], &regs[3], &regs[2], src_cpuiddump);
  memcpy(infos->cpuvendor, regs+1, 4*3);
  /* infos was calloc'ed, already ends with \0 */

  /* Get cpu model string from cpuid 0x80000002-4 */
  if (highest_ext_cpuid >= 0x80000004) {
    memset(regs, 0, sizeof(regs));
    regs[0] = 0x80000002;
    cpuid_or_from_dump(&regs[0], &regs[1], &regs[2], &regs[3], src_cpuiddump);
    memcpy(infos->cpumodel, regs, 4*4);
    regs[0] = 0x80000003;
    cpuid_or_from_dump(&regs[0], &regs[1], &regs[2], &regs[3], src_cpuiddump);
    memcpy(infos->cpumodel + 4*4, regs, 4*4);
    regs[0] = 0x80000004;
    cpuid_or_from_dump(&regs[0], &regs[1], &regs[2], &regs[3], src_cpuiddump);
    memcpy(infos->cpumodel + 4*4*2, regs, 4*4);
    /* infos was calloc'ed, already ends with \0 */
  }

  /* Get core/thread information from cpuid 0x80000008
   * (not supported on Intel)
   */
  if (cpuid_type != intel && highest_ext_cpuid >= 0x80000008) {
    unsigned coreidsize;
    eax = 0x80000008;
    cpuid_or_from_dump(&eax, &ebx, &ecx, &edx, src_cpuiddump);
    coreidsize = (ecx >> 12) & 0xf;
    hwloc_debug("core ID size: %u\n", coreidsize);
    if (!coreidsize) {
      infos->max_nbcores = (ecx & 0xff) + 1;
    } else
      infos->max_nbcores = 1 << coreidsize;
    hwloc_debug("Thus max # of cores: %u\n", infos->max_nbcores);
    /* Still no multithreaded AMD */
    infos->max_nbthreads = 1 ;
    hwloc_debug("and max # of threads: %u\n", infos->max_nbthreads);
    /* The legacy max_log_proc is deprecated, it can be smaller than max_nbcores,
     * which is the maximum number of cores that the processor could theoretically support
     * (see "Multiple Core Calculation" in the AMD CPUID specification).
     * Recompute packageid/logprocid/threadid/coreid accordingly.
     */
    infos->packageid = infos->apicid / infos->max_nbcores;
    infos->logprocid = infos->apicid % infos->max_nbcores;
    infos->threadid = infos->logprocid % infos->max_nbthreads;
    infos->coreid = infos->logprocid / infos->max_nbthreads;
    hwloc_debug("this is thread %u of core %u\n", infos->threadid, infos->coreid);
  }

  /*
  * Get TLB informations
  */
  infos->numtlbs=0;
  if (cpuid_type != intel  && highest_ext_cpuid >= 0x80000005) 
    get_fill_amd_tlb(infos, src_cpuiddump,highest_ext_cpuid);
  else
    if (cpuid_type != amd && highest_cpuid >= 0x02)
      get_fill_intel_tlb(infos, src_cpuiddump);

  infos->numcaches = 0;
  infos->cache = NULL;

  /* Get apicid, nodeid, unitid from cpuid 0x8000001e
   * and cache information from cpuid 0x8000001d
   * (AMD topology extension)
   */
  if (cpuid_type != intel && has_topoext(features)) {
    unsigned apic_id, node_id, nodes_per_proc, unit_id, cores_per_unit;

    eax = 0x8000001e;
    cpuid_or_from_dump(&eax, &ebx, &ecx, &edx, src_cpuiddump);
    infos->apicid = apic_id = eax;
    infos->nodeid = node_id = ecx & 0xff;
    nodes_per_proc = ((ecx >> 8) & 7) + 1;
    if (nodes_per_proc > 2) {
      hwloc_debug("warning: undefined value %d, assuming it means %d\n", nodes_per_proc, nodes_per_proc);
    }
    infos->unitid = unit_id = ebx & 0xff;
    cores_per_unit = ((ebx >> 8) & 3) + 1;
    hwloc_debug("x2APIC %08x, %d nodes, node %d, %d cores in unit %d\n", apic_id, nodes_per_proc, node_id, cores_per_unit, unit_id);

    for (cachenum = 0; ; cachenum++) {
      unsigned type;
      eax = 0x8000001d;
      ecx = cachenum;
      cpuid_or_from_dump(&eax, &ebx, &ecx, &edx, src_cpuiddump);
      type = eax & 0x1f;
      if (type == 0)
        break;
      infos->numcaches++;
    }

    cache = infos->cache = malloc(infos->numcaches * sizeof(*infos->cache));

    for (cachenum = 0; ; cachenum++) {
      unsigned long linesize, linepart, ways, sets;
      unsigned type;
      eax = 0x8000001d;
      ecx = cachenum;
      cpuid_or_from_dump(&eax, &ebx, &ecx, &edx, src_cpuiddump);

      type = eax & 0x1f;

      if (type == 0)
        break;

      cache->type = type;
      cache->level = (eax >> 5) & 0x7;
      /* Note: actually number of cores */
      cache->nbthreads_sharing = ((eax >> 14) &  0xfff) + 1;

      cache->linesize = linesize = (ebx & 0xfff) + 1;
      cache->linepart = linepart = ((ebx >> 12) & 0x3ff) + 1;
      ways = ((ebx >> 22) & 0x3ff) + 1;

      if (eax & (1 << 9))
        /* Fully associative */
        cache->ways = -1;
      else
        cache->ways = ways;
      cache->sets = sets = ecx + 1;
      cache->size = linesize * linepart * ways * sets;
      cache->inclusiveness = edx & 0x2;


      hwloc_debug("cache %u type %u L%u t%u c%u linesize %lu linepart %lu ways %lu sets %lu, size %uKB\n", cachenum, cache->type, cache->level, cache->nbthreads_sharing, infos->max_nbcores, linesize, linepart, ways, sets, cache->size >> 10);

      cache++;
    }
  } else {
    /* If there's no topoext,
     * get cache information from cpuid 0x80000005 and 0x80000006
     * (not supported on Intel)
     */
    if (cpuid_type != intel && highest_ext_cpuid >= 0x80000005) {
      eax = 0x80000005;
      cpuid_or_from_dump(&eax, &ebx, &ecx, &edx, src_cpuiddump);
      fill_amd_cache(infos, 1, 1, ecx); /* L1d */
      fill_amd_cache(infos, 1, 2, edx); /* L1i */
    }
    if (cpuid_type != intel && highest_ext_cpuid >= 0x80000006) {
      eax = 0x80000006;
      cpuid_or_from_dump(&eax, &ebx, &ecx, &edx, src_cpuiddump);
      if (ecx & 0xf000)
        /* This is actually supported on Intel but LinePerTag isn't returned in bits 8-11.
         * Could be useful if some Intels (at least before Core micro-architecture)
         * support this leaf without leaf 0x4.
         */
        fill_amd_cache(infos, 2, 3, ecx); /* L2u */
      if (edx & 0xf000)
        fill_amd_cache(infos, 3, 3, edx); /* L3u */
    }
  }

  /* Get thread/core + cache information from cpuid 0x04
   * (not supported on AMD)
   */
  if (cpuid_type != amd && highest_cpuid >= 0x04) {
    for (cachenum = 0; ; cachenum++) {
      unsigned type;
      eax = 0x04;
      ecx = cachenum;
      cpuid_or_from_dump(&eax, &ebx, &ecx, &edx, src_cpuiddump);

      type = eax & 0x1f;

      hwloc_debug("cache %u type %u\n", cachenum, type);

      if (type == 0)
        break;
      infos->numcaches++;

      if (!cachenum) {
        /* by the way, get thread/core information from the first cache */
        infos->max_nbcores = ((eax >> 26) & 0x3f) + 1;
        infos->max_nbthreads = infos->max_log_proc / infos->max_nbcores;
        hwloc_debug("thus %u threads\n", infos->max_nbthreads);
        infos->threadid = infos->logprocid % infos->max_nbthreads;
        infos->coreid = infos->logprocid / infos->max_nbthreads;
        hwloc_debug("this is thread %u of core %u\n", infos->threadid, infos->coreid);
      }
    }

    cache = infos->cache = malloc(infos->numcaches * sizeof(*infos->cache));

    for (cachenum = 0; ; cachenum++) {
      unsigned long linesize, linepart, ways, sets;
      unsigned type;
      eax = 0x04;
      ecx = cachenum;
      cpuid_or_from_dump(&eax, &ebx, &ecx, &edx, src_cpuiddump);

      type = eax & 0x1f;

      if (type == 0)
        break;

      cache->type = type;
      cache->level = (eax >> 5) & 0x7;
      cache->nbthreads_sharing = ((eax >> 14) & 0xfff) + 1;

      cache->linesize = linesize = (ebx & 0xfff) + 1;
      cache->linepart = linepart = ((ebx >> 12) & 0x3ff) + 1;
      ways = ((ebx >> 22) & 0x3ff) + 1;
      if (eax & (1 << 9))
        /* Fully associative */
        cache->ways = -1;
      else
        cache->ways = ways;
      cache->sets = sets = ecx + 1;
      cache->size = linesize * linepart * ways * sets;
      cache->inclusiveness = edx & 0x2;

      hwloc_debug("cache %u type %u L%u t%u c%u linesize %lu linepart %lu ways %lu sets %lu, size %uKB\n", cachenum, cache->type, cache->level, cache->nbthreads_sharing, infos->max_nbcores, linesize, linepart, ways, sets, cache->size >> 10);

      cache++;
    }
  }

  /* Get package/core/thread information from cpuid 0x0b
   * (Intel x2APIC)
   */
  if (cpuid_type == intel && has_x2apic(features)) {
    unsigned level, apic_nextshift, apic_number, apic_type, apic_id = 0, apic_shift = 0, id;
    for (level = 0; ; level++) {
      ecx = level;
      eax = 0x0b;
      cpuid_or_from_dump(&eax, &ebx, &ecx, &edx, src_cpuiddump);
      if (!eax && !ebx)
        break;
    }
    if (level) {
      infos->levels = level;
      infos->otherids = malloc(level * sizeof(*infos->otherids));
      for (level = 0; ; level++) {
        ecx = level;
        eax = 0x0b;
        cpuid_or_from_dump(&eax, &ebx, &ecx, &edx, src_cpuiddump);
        if (!eax && !ebx)
          break;
        apic_nextshift = eax & 0x1f;
        apic_number = ebx & 0xffff;
        apic_type = (ecx & 0xff00) >> 8;
        apic_id = edx;
        id = (apic_id >> apic_shift) & ((1 << (apic_nextshift - apic_shift)) - 1);
        hwloc_debug("x2APIC %08x %d: nextshift %d num %2d type %d id %2d\n", apic_id, level, apic_nextshift, apic_number, apic_type, id);
        infos->apicid = apic_id;
        infos->otherids[level] = UINT_MAX;
        switch (apic_type) {
        case 1:
          infos->threadid = id;
          break;
        case 2:
          infos->coreid = id;
          break;
        default:
          hwloc_debug("x2APIC %d: unknown type %d\n", level, apic_type);
          infos->otherids[level] = apic_id >> apic_shift;
          break;
        }
        apic_shift = apic_nextshift;
      }
      infos->apicid = apic_id;
      infos->packageid = apic_id >> apic_shift;
      hwloc_debug("x2APIC remainder: %d\n", infos->packageid);
      hwloc_debug("this is thread %u of core %u\n", infos->threadid, infos->coreid);
    }
  }
  if (hwloc_bitmap_isset(data->apicid_set, infos->apicid))
    data->apicid_unique = 0;
  else
    hwloc_bitmap_set(data->apicid_set, infos->apicid);
}

static void
hwloc_x86_add_cpuinfos(hwloc_obj_t obj, struct procinfo *info, int nodup)
{
  char number[8];
  hwloc_obj_add_info_nodup(obj, "CPUVendor", info->cpuvendor, nodup);
  snprintf(number, sizeof(number), "%u", info->cpufamilynumber);
  hwloc_obj_add_info_nodup(obj, "CPUFamilyNumber", number, nodup);
  snprintf(number, sizeof(number), "%u", info->cpumodelnumber);
  hwloc_obj_add_info_nodup(obj, "CPUModelNumber", number, nodup);
  if (info->cpumodel[0]) {
    const char *c = info->cpumodel;
    while (*c == ' ')
      c++;
    hwloc_obj_add_info_nodup(obj, "CPUModel", c, nodup);
  }
  snprintf(number, sizeof(number), "%u", info->cpustepping);
  hwloc_obj_add_info_nodup(obj, "CPUStepping", number, nodup);
}

static hwloc_obj_t addCore(struct hwloc_backend *backend, struct procinfo *infos, int infoId, void* unused){//adding core if missing
  hwloc_bitmap_t core_cpuset;
  hwloc_obj_t core;
  unsigned j;
  unsigned packageid = infos[infoId].packageid;
  unsigned coreid = infos[infoId].coreid;
  unsigned nbprocs = ((struct hwloc_x86_backend_data_s *) backend->private_data)->nbprocs;
  (void)unused;
  if (coreid != (unsigned) -1) {
    core_cpuset = hwloc_bitmap_alloc();
    for (j = 0; j < nbprocs; j++)  //there can be multiple pu by core
      if (infos[j].packageid == packageid && infos[j].coreid == coreid) 
        hwloc_bitmap_set(core_cpuset, j);

    core = hwloc_alloc_setup_object(HWLOC_OBJ_CORE, coreid);
    core->cpuset = core_cpuset;
    hwloc_debug_1arg_bitmap("os core %u has cpuset %s\n",
        coreid, core_cpuset);
    hwloc_insert_object_by_cpuset(backend->topology, core);
    return core;
  }
  return NULL;
}

static int annotateCore(struct hwloc_backend *backend, struct procinfo *infos, hwloc_obj_t toAnnotate,int infoId,void* unused){
  unsigned i;
  (void) unused;
  (void) backend;
  (void) infoId;
  char propertyName[6]; //max of 100 TLB by core (propertyName = "TLB99\0").
  for (i = 0; i < infos->numtlbs ; i++){ // 
    sprintf(propertyName,"TLB%d",i);
    if (!hwloc_obj_get_info_by_name(toAnnotate,propertyName)){
      char property[128];//currently always < 90
      unsigned length = 0;
      switch(infos->tlbs[i].type){
        case 0:
          length += snprintf(property + length,128-length,"instruction");
        break;
        case 1:
          length += snprintf(property + length,128-length,"data");
        break;
        case 2:
          length += snprintf(property + length,128-length,"shared L2");
        break;
        case 3:
          length += snprintf(property + length,128-length,"instruction L2");
        break;
        case 4:
          length += snprintf(property + length,128-length,"data L2");
        break;
      }

      switch(infos->tlbs[i].associativity){
        case 0:
          length += snprintf(property + length,128-length,", fully associative");
        break;
        case 1:
          length += snprintf(property + length,128-length,", direct mapped");        
        break;
        default:
          length += snprintf(property + length,128-length,", %d-way associative",infos->tlbs[i].associativity);
        break;
      }
      if (infos->tlbs[i].entriesnumber4KB)
        length += snprintf(property + length,128-length,", 4KB : %d",infos->tlbs[i].entriesnumber4KB);

      if (infos->tlbs[i].entriesnumber2MB)
        length += snprintf(property + length,128-length,", 2MB : %d",infos->tlbs[i].entriesnumber2MB);

      if (infos->tlbs[i].entriesnumber4MB)
        length += snprintf(property + length,128-length,", 4MB : %d",infos->tlbs[i].entriesnumber4MB);

      if (infos->tlbs[i].entriesnumber1GB)
        length += snprintf(property + length,128-length,", 1GB : %d",infos->tlbs[i].entriesnumber1GB);

      if(length >= 128){
        fprintf(stderr, "buffer too small (annotateCore from topology-x86) tried to put %d in a buffer of size 128\n", length);
      }
      hwloc_obj_add_info(toAnnotate,propertyName,property);
    }
  }
  return 0;
}
static hwloc_obj_t addCache(struct hwloc_backend *backend, struct procinfo *infos, int infoId,void* data){
  struct{
        unsigned numberFound;
        struct cacheinfo ** caches;
    }* cacheList = data;

  if(infos[infoId].numcaches == 0){
      return NULL;
  }
  else
  {
    hwloc_bitmap_t cache_cpuset;
    hwloc_obj_t cacheAdded;
    unsigned j,level, type;
    unsigned packageid = infos[infoId].packageid;
    unsigned cacheid = (infos[infoId].apicid % infos[infoId].max_log_proc) / cacheList->caches[cacheList->numberFound]->nbthreads_sharing;
    unsigned nbprocs = ((struct hwloc_x86_backend_data_s *)backend->private_data)->nbprocs;
    cache_cpuset = hwloc_bitmap_alloc();
    level = cacheList->caches[cacheList->numberFound]->level;//numberFound : number of cache found
    type = cacheList->caches[cacheList->numberFound]->type;
    /* Now look for others PU sharing this cache */
    for (j = 0; j < nbprocs; j++) {
      unsigned l2;
      for (l2 = 0; l2 < infos[j].numcaches; l2++) {
        if (infos[j].cache[l2].level == level && infos[j].cache[l2].type == type)
          break;
      }
      if (l2 == infos[j].numcaches) {
        /* no cache level of that type in j */
        continue;
      }
      if (infos[j].packageid == packageid && (infos[j].apicid % infos[j].max_log_proc)/ infos[j].cache[l2].nbthreads_sharing == cacheid) {
        hwloc_bitmap_set(cache_cpuset, j);
      }
    }
    cacheAdded = hwloc_alloc_setup_object(HWLOC_OBJ_CACHE, cacheid);
    cacheAdded->attr->cache.depth = level;
    cacheAdded->attr->cache.size = cacheList->caches[cacheList->numberFound]->size;
    cacheAdded->attr->cache.linesize = cacheList->caches[cacheList->numberFound]->linesize;
    cacheAdded->attr->cache.associativity = cacheList->caches[cacheList->numberFound]->ways;
    switch (cacheList->caches[cacheList->numberFound]->type) {
      case 1:
        cacheAdded->attr->cache.type = HWLOC_OBJ_CACHE_DATA;
        break;
      case 2:
        cacheAdded->attr->cache.type = HWLOC_OBJ_CACHE_INSTRUCTION;
        break;
      case 3:
        cacheAdded->attr->cache.type = HWLOC_OBJ_CACHE_UNIFIED;
        break;
    }
    cacheAdded->cpuset = cache_cpuset;
    hwloc_debug_2args_bitmap("os L%u cache %u has cpuset %s\n",
        level, cacheid, cache_cpuset);
    hwloc_insert_object_by_cpuset(backend->topology, cacheAdded);
    return cacheAdded;
  }
}

static int annotateCache(struct hwloc_backend *backend, struct procinfo *infos, hwloc_obj_t toAnnotate,int infoId,void* data){// annotate cache found/created (add inclusiveness)
       // and remove it from caches not seen
  unsigned j, type = 0, cacheId = -1;
  struct{
      unsigned numberFound;
      struct cacheinfo ** caches;
  }* cacheList = data;
  (void) backend;

  switch(toAnnotate->attr->cache.type){
    case HWLOC_OBJ_CACHE_DATA : type = 1;
      break;
    case HWLOC_OBJ_CACHE_INSTRUCTION : type = 2;
      break;
    case HWLOC_OBJ_CACHE_UNIFIED : type = 3;
      break;
  }
  for(j=cacheList->numberFound;j<infos[infoId].numcaches;j++)
    if(cacheList->caches[j]->level == toAnnotate->attr->cache.depth){ // the level is exact, not always the type. 
    //If at the level there is a cache with the good type we return it. Else we return a cache of the level. 
      cacheId = j;
      if(cacheList->caches[j]->type == type)
        break;
    }
  if (cacheId != (unsigned)-1){
   //cache fond. We annotate it and move it in the first part of the list, where are annotated cache.
    struct cacheinfo * tmpCache = cacheList->caches[cacheList->numberFound];
    cacheList->caches[cacheList->numberFound] = cacheList->caches[cacheId];
    cacheList->caches[cacheId] = tmpCache;
    if (!hwloc_obj_get_info_by_name(toAnnotate,"inclusiveness")) 
      hwloc_obj_add_info(toAnnotate,"inclusiveness",cacheList->caches[cacheList->numberFound]->inclusiveness?"true":"false");
  }
  cacheList->numberFound++;
  return cacheList->numberFound<infos[infoId].numcaches;
}

static void foundNuma(struct hwloc_backend *backend, struct procinfo *infos, hwloc_obj_t found,int infoId,void* data){
  (void)backend;(void)infos;(void)found;(void)infoId;
  (*(int*)data)++; //NUMA can be found before or after the package. 
                   //We nedd to check both before adding one.
}

static hwloc_obj_t addNuma(struct hwloc_backend *backend, struct procinfo *infos, int infoId, void* unused){
  unsigned j, nbprocs = ((struct hwloc_x86_backend_data_s *)backend->private_data)->nbprocs;
  if (infos[infoId].nodeid != (unsigned) -1) {
    hwloc_bitmap_t node_cpuset;
    /* FIXME: if there's memory inside the root object, divide it into NUMA nodes? */
    unsigned packageid = infos[infoId].packageid;
    unsigned nodeid = infos[infoId].nodeid;
    hwloc_obj_t numa;
    (void) unused;
    node_cpuset = hwloc_bitmap_alloc();
    for (j = 0; j < nbprocs; j++)  // Look for others PU sharing this NUMA
      if (infos[j].packageid == packageid && infos[j].nodeid == nodeid) 
        hwloc_bitmap_set(node_cpuset, j);

    numa = hwloc_alloc_setup_object(HWLOC_OBJ_NUMANODE, nodeid);
    numa->cpuset = node_cpuset;
    numa->nodeset = hwloc_bitmap_alloc();
    hwloc_bitmap_set(numa->nodeset, nodeid);
    hwloc_debug_1arg_bitmap("os node %u has cpuset %s\n",
        nodeid, node_cpuset);
    hwloc_insert_object_by_cpuset(backend->topology, numa);
    return numa;
  }
  return NULL;
}

static hwloc_obj_t addPackage(struct hwloc_backend *backend, struct procinfo *infos, int infoId,void* unused){
  hwloc_bitmap_t package_cpuset;
  unsigned packageid = infos[infoId].packageid;
  unsigned j, nbprocs = ((struct hwloc_x86_backend_data_s *)backend->private_data)->nbprocs;
  hwloc_obj_t package;
  (void) unused;
  package_cpuset = hwloc_bitmap_alloc();

  for (j = 0; j < nbprocs; j++) { // Look for others PU sharing this package
    if (infos[j].packageid == packageid) {
      hwloc_bitmap_set(package_cpuset, j);
    }
  }
  package = hwloc_alloc_setup_object(HWLOC_OBJ_PACKAGE, packageid);
  package->cpuset = package_cpuset;

  hwloc_debug_1arg_bitmap("os package %u has cpuset %s\n",
      packageid, package_cpuset);
  hwloc_insert_object_by_cpuset(backend->topology, package);
  return package;
}
static int annotatePackage(struct hwloc_backend *backend, struct procinfo *infos, hwloc_obj_t toAnnotate,int infoId,void* unused){
  (void) unused;
  (void) backend;
  if (infos[infoId].packageid == toAnnotate->os_index || toAnnotate->os_index == (unsigned) -1)  
    hwloc_x86_add_cpuinfos(toAnnotate, &infos[infoId], 1);
  return 0;//only one
}

static void hwloc_x86_setup_pu_level(struct hwloc_topology *topology, struct procinfo *infos, unsigned nb_pus)
{
  struct hwloc_obj *obj;
  unsigned oscpu,cpu;

  hwloc_debug("%s", "\n\n * CPU cpusets *\n\n");
  for (cpu=0,oscpu=0; oscpu<nb_pus; oscpu++)
    if(infos[oscpu].present){//do not add inactive PU only detected by x86 backend
      obj = hwloc_alloc_setup_object(HWLOC_OBJ_PU, oscpu);
      obj->cpuset = hwloc_bitmap_alloc();
      hwloc_bitmap_only(obj->cpuset, oscpu);

      hwloc_debug_2args_bitmap("cpu %u (os %u) has cpuset %s\n", cpu, oscpu, obj->cpuset);
      hwloc_insert_object_by_cpuset(topology, obj);

      cpu++;
    }
}

/* Iterate over the parents of OBJECT. If his (direct) parent is of type TYPE then IFFOUND is called.
 * Else IFNOTFOUND is called. If IFNOTFOUND add an object of type TYPE then it should also return it.
 * ANNOTATE is called after with th object foun or added in parameter.It should return true if another iteration is needed
 * IFFOUND, IFNOTFOUND, ANNOTATE can have NULL as value. They won't be called. 
 * If IFNOTFOUND has NULL as value or return NULL, ANNOTATE won't be called and the iteration is ended when it should have been called.
 * If ANNOTATE has NULL as value the iteration is ended when it should have been called.
 * If IFNOTFOUND add an object that isn't the new father of object the value of object don't change at the next iteration.
 * If IFNOTFOUND add an object that is the new father of object or IFFOND is called the value of object change to the father at the next iteration.
 * DATA is a void* given as parameter to IFFOUND IFNOTFOUND ANNOTATE
 
 * The prototype of the function passed as parameter are:
 
 * void IFFOUND(struct hwloc_backend *backend, struct procinfo *infos, hwloc_obj_t parentFound,int infoId,void* data)

 * hwloc_obj_t IFNOTFOUND(struct hwloc_backend *backend, struct procinfo *infos, int infoId,void* data) // if an object is added it should be returned

 * int ANNOTATE(struct hwloc_backend *backend, struct procinfo *infos, hwloc_obj_t toAnnotate,int infoId,void* data) // if it return true, another object of type TYPE will be searched. Used to find cache.

 * infoId is the physical number of the pu from which parentFound/toAnnotate is the parent or for IFNOTFOUND who is missing an parent of type TYPE
 */
static void iterateOverParent(struct hwloc_backend *backend,
 struct procinfo *infos,
 hwloc_obj_t* object,
 int infoId,
 void* data,
 hwloc_obj_type_t type,
 void (*ifFound)(struct hwloc_backend *, struct procinfo *, hwloc_obj_t, int, void*),
 hwloc_obj_t ifNotFound(struct hwloc_backend *, struct procinfo *, int,void*), 
 int (*annotate)(struct hwloc_backend *, struct procinfo *, hwloc_obj_t, int,void*)){
  hwloc_obj_t toAnnotate;
  do{
    while((*object)->parent->type == HWLOC_OBJ_GROUP){// HWLOC_OBJ_GROUP are ignored.
      *object = (*object)->parent;
    }
    if((*object)->parent->type == type){
      (*object) = (*object)->parent;
      toAnnotate = *object;
      if(ifFound)
        ifFound(backend,infos,*object,infoId,data);
    }else{
      toAnnotate = ifNotFound ? ifNotFound(backend,infos,infoId,data) : NULL;
      if((*object)->parent && (*object)->parent == toAnnotate)
        *object = (*object)->parent;
      /* We check if the object added is the new parent.
      * If it is we continue with it as object in order to not see it twice.
      * Else it should have been added as a child/grandchild/ect of object. We still have not seen the parent and shouldn't modify our pointer.
      */ 
    }
  }while(toAnnotate && annotate && annotate(backend,infos,toAnnotate,infoId,data));
}

/* Analyse information stored in infos, and build/annotate topology levels accordingly */
static void summarize(struct hwloc_backend *backend, struct procinfo *infos)
{
  struct hwloc_topology *topology = backend->topology;
  struct hwloc_x86_backend_data_s *data = backend->private_data;
  unsigned nbprocs = data->nbprocs;
  hwloc_bitmap_t complete_cpuset = hwloc_bitmap_alloc();
  unsigned i, j, level;
  int one = -1;
  unsigned next_group_depth = topology->next_group_depth;
  hwloc_obj_t pu = hwloc_get_next_obj_by_type(topology,HWLOC_OBJ_PU ,NULL);
  hwloc_obj_t* puList = malloc((nbprocs+1) * sizeof(hwloc_obj_t)); // puList end by NULL

  if (!pu || pu->type != HWLOC_OBJ_PU)//add PU if not already added
    hwloc_x86_setup_pu_level(topology, infos, data->nbprocs);//only fist child, right brother and father initialised

  for (i = 0; i < nbprocs; i++)
    if (infos[i].present) {
      hwloc_bitmap_set(complete_cpuset, i);
      one = i;
    }

  if (one == -1) {
    hwloc_bitmap_free(complete_cpuset);
    return;
  }

  /* Ideally, we could add any object that doesn't exist yet.
   * But what if the x86 and the native backends disagree because one is buggy? Which one to trust?
   * Only annotate existing objects for now.
   */

  // We need to copy the pu list. If we added it and we add the fathers (the cores) it will fall in further chaos. And it will worsen for each level added.
  for(pu = hwloc_get_next_obj_by_type(topology,HWLOC_OBJ_PU ,NULL),i=0;;//return machine if PU just initialised
   pu = pu->next_sibling ? pu->next_sibling : pu->next_cousin,i++){
    if(pu == NULL){
      puList[i] = NULL; // puList end by NULL 
                      //the number of pu in puList can be <= puNumber if only x86 backend is called with some pu restricted by cpuset or inactive
      break;
    }
    while(pu->type != HWLOC_OBJ_PU && pu->first_child)//when we just created them pu can be the machine...
      pu = pu->first_child;
    if (pu->type == HWLOC_OBJ_PU)
      puList[i] = pu;
  }

 /*Annotate previously existing objects and add missing*/

  for(i=0;puList[i];i++){//iterate over pu. For each object will be added/annotate from core to package. puList end by NULL
    unsigned numberFound, infoId;
    hwloc_obj_t object = puList[i];

    infoId= puList[i]->os_index;

    if(!infos[infoId].present)
      continue;

    /* Look for cores*/

    iterateOverParent(backend, infos, &object, infoId, NULL, HWLOC_OBJ_CORE, NULL, &addCore, &annotateCore);

    /* Look for caches */
    {
      struct{
          unsigned numberFound;
          struct cacheinfo ** caches;
      } cacheList;
      cacheList.caches = malloc(infos[infoId].numcaches*sizeof(struct cacheinfo*));
      for(j = 0 ;j<infos[infoId].numcaches;j++){
        cacheList.caches[j] = &(infos[infoId].cache[j]);
      }
      //we will modify cacheList : in cacheList.caches before index cacheList.numberFound the cache were seen or added.
      cacheList.numberFound = 0;


      iterateOverParent(backend, infos, &object, infoId, &cacheList, HWLOC_OBJ_CACHE, NULL , &addCache, &annotateCache);
      free(cacheList.caches);
    }

    numberFound = 0; // number of numa node fond : we can found numa before or after package. If not found before we check again after then add it if still not found.
    iterateOverParent(backend, infos, &object, infoId, &numberFound, HWLOC_OBJ_NUMANODE,  &foundNuma , NULL, NULL);
    
  /* Look for package */
    iterateOverParent(backend, infos, &object, infoId, NULL, HWLOC_OBJ_PACKAGE, NULL , &addPackage, &annotatePackage);

    if(0 == numberFound)/* Look for Numa nodes if not already found as a son of package*/
      iterateOverParent(backend, infos, &object, infoId, NULL, HWLOC_OBJ_NUMANODE, NULL , &addNuma, NULL);
    
  }

  free(puList);

  /* If there was no package, annotate the Machine instead */
  if ((!hwloc_get_nbobjs_by_type(topology, HWLOC_OBJ_PACKAGE)) && infos[0].cpumodel[0]) {//the only place where we need nbpackage. And package would have already been added by x86-topo
    hwloc_x86_add_cpuinfos(hwloc_get_root_obj(topology), &infos[0], 1);
  }

  /* Look for Compute units inside packages */ 
  {
    hwloc_bitmap_t units_cpuset = hwloc_bitmap_dup(complete_cpuset);
    hwloc_bitmap_t unit_cpuset;
    hwloc_obj_t unit;

    while ((i = hwloc_bitmap_first(units_cpuset)) != (unsigned) -1) {
      unsigned packageid = infos[i].packageid;
      unsigned unitid = infos[i].unitid;

      if (unitid == (unsigned)-1) {
        hwloc_bitmap_clr(units_cpuset, i);
        continue;
      }

      unit_cpuset = hwloc_bitmap_alloc();
      for (j = i; j < nbprocs; j++) {
        if (infos[j].unitid == (unsigned) -1) {
          hwloc_bitmap_clr(units_cpuset, j);
          continue;
        }

        if (infos[j].packageid == packageid && infos[j].unitid == unitid) {
          hwloc_bitmap_set(unit_cpuset, j);
          hwloc_bitmap_clr(units_cpuset, j);
        }
      }
      unit = hwloc_alloc_setup_object(HWLOC_OBJ_GROUP, unitid);
      unit->cpuset = unit_cpuset;
      hwloc_debug_1arg_bitmap("os unit %u has cpuset %s\n",
          unitid, unit_cpuset);
      hwloc_insert_object_by_cpuset(topology, unit);
    }
    hwloc_bitmap_free(units_cpuset);
  }

  /* Look for unknown objects */
  if (infos[one].otherids) {
    for (level = infos[one].levels-1; level <= infos[one].levels-1; level--) {
      if (infos[one].otherids[level] != UINT_MAX) {
        hwloc_bitmap_t unknowns_cpuset = hwloc_bitmap_dup(complete_cpuset);
        hwloc_bitmap_t unknown_cpuset;
        hwloc_obj_t unknown_obj;

        while ((i = hwloc_bitmap_first(unknowns_cpuset)) != (unsigned) -1) {
          unsigned unknownid = infos[i].otherids[level];

          unknown_cpuset = hwloc_bitmap_alloc();
          for (j = i; j < nbprocs; j++) {
            if (infos[j].otherids[level] == unknownid) {
              hwloc_bitmap_set(unknown_cpuset, j);
              hwloc_bitmap_clr(unknowns_cpuset, j);
            }
          }
          unknown_obj = hwloc_alloc_setup_object(HWLOC_OBJ_GROUP, unknownid);
          unknown_obj->cpuset = unknown_cpuset;
          unknown_obj->attr->group.depth = topology->next_group_depth + level;
          if (next_group_depth <= topology->next_group_depth + level)
            next_group_depth = topology->next_group_depth + level + 1;
          hwloc_debug_2args_bitmap("os unknown%d %u has cpuset %s\n",
              level, unknownid, unknown_cpuset);
          hwloc_insert_object_by_cpuset(topology, unknown_obj);
        }
        hwloc_bitmap_free(unknowns_cpuset);
      }
    }
  }

  for (i = 0; i < nbprocs; i++) {
    free(infos[i].cache);
    free(infos[i].tlbs);
    if (infos[i].otherids)
      free(infos[i].otherids);
  }

  hwloc_bitmap_free(complete_cpuset);
  topology->next_group_depth = next_group_depth;
}

static int
look_procs(struct hwloc_backend *backend, struct procinfo *infos, int fulldiscovery,
           unsigned highest_cpuid, unsigned highest_ext_cpuid, unsigned *features, enum cpuid_type cpuid_type,
           int (*get_cpubind)(hwloc_topology_t topology, hwloc_cpuset_t set, int flags),
           int (*set_cpubind)(hwloc_topology_t topology, hwloc_const_cpuset_t set, int flags))
{
  struct hwloc_x86_backend_data_s *data = backend->private_data;
  struct hwloc_topology *topology = backend->topology;
  unsigned nbprocs = data->nbprocs;
  hwloc_bitmap_t orig_cpuset = NULL;
  hwloc_bitmap_t set = NULL;
  unsigned i;

  if (!data->src_cpuiddump_path) {
    orig_cpuset = hwloc_bitmap_alloc();
    if (get_cpubind(topology, orig_cpuset, HWLOC_CPUBIND_STRICT)) {
      hwloc_bitmap_free(orig_cpuset);
      return -1;
    }
    set = hwloc_bitmap_alloc();
  }

  for (i = 0; i < nbprocs; i++) {
    struct cpuiddump *src_cpuiddump = NULL;
    if (data->src_cpuiddump_path) {
      src_cpuiddump = cpuiddump_read(data->src_cpuiddump_path, i);
    } else {
      hwloc_bitmap_only(set, i);
      hwloc_debug("binding to CPU%d\n", i);
      if (set_cpubind(topology, set, HWLOC_CPUBIND_STRICT)) {
        hwloc_debug("could not bind to CPU%d: %s\n", i, strerror(errno));
        continue;
      }
    }

    look_proc(backend, &infos[i], highest_cpuid, highest_ext_cpuid, features, cpuid_type, src_cpuiddump);

    if (data->src_cpuiddump_path) {
      cpuiddump_free(src_cpuiddump);
    }
  }

  if (!data->src_cpuiddump_path) {
    set_cpubind(topology, orig_cpuset, 0);
    hwloc_bitmap_free(set);
    hwloc_bitmap_free(orig_cpuset);
  }

  if (!data->apicid_unique)
    fulldiscovery = 0;
  summarize(backend, infos);
  return fulldiscovery; /* success*/
}

#if defined HWLOC_FREEBSD_SYS && defined HAVE_CPUSET_SETID
#include <sys/param.h>
#include <sys/cpuset.h>
typedef cpusetid_t hwloc_x86_os_state_t;
static void hwloc_x86_os_state_save(hwloc_x86_os_state_t *state, struct cpuiddump *src_cpuiddump)
{
  if (!src_cpuiddump) {
    /* temporary make all cpus available during discovery */
    cpuset_getid(CPU_LEVEL_CPUSET, CPU_WHICH_PID, -1, state);
    cpuset_setid(CPU_WHICH_PID, -1, 0);
  }
}
static void hwloc_x86_os_state_restore(hwloc_x86_os_state_t *state, struct cpuiddump *src_cpuiddump)
{
  if (!src_cpuiddump) {
    /* restore initial cpuset */
    cpuset_setid(CPU_WHICH_PID, -1, *state);
  }
}
#else /* !defined HWLOC_FREEBSD_SYS || !defined HAVE_CPUSET_SETID */
typedef void * hwloc_x86_os_state_t;
static void hwloc_x86_os_state_save(hwloc_x86_os_state_t *state __hwloc_attribute_unused, struct cpuiddump *src_cpuiddump __hwloc_attribute_unused) { }
static void hwloc_x86_os_state_restore(hwloc_x86_os_state_t *state __hwloc_attribute_unused, struct cpuiddump *src_cpuiddump __hwloc_attribute_unused) { }
#endif /* !defined HWLOC_FREEBSD_SYS || !defined HAVE_CPUSET_SETID */


#define INTEL_EBX ('G' | ('e'<<8) | ('n'<<16) | ('u'<<24))
#define INTEL_EDX ('i' | ('n'<<8) | ('e'<<16) | ('I'<<24))
#define INTEL_ECX ('n' | ('t'<<8) | ('e'<<16) | ('l'<<24))

#define AMD_EBX ('A' | ('u'<<8) | ('t'<<16) | ('h'<<24))
#define AMD_EDX ('e' | ('n'<<8) | ('t'<<16) | ('i'<<24))
#define AMD_ECX ('c' | ('A'<<8) | ('M'<<16) | ('D'<<24))

/* fake cpubind for when nbprocs=1 and no binding support */
static int fake_get_cpubind(hwloc_topology_t topology __hwloc_attribute_unused,
                            hwloc_cpuset_t set __hwloc_attribute_unused,
                            int flags __hwloc_attribute_unused)
{
  return 0;
}
static int fake_set_cpubind(hwloc_topology_t topology __hwloc_attribute_unused,
                            hwloc_const_cpuset_t set __hwloc_attribute_unused,
                            int flags __hwloc_attribute_unused)
{
  return 0;
}

static
int hwloc_look_x86(struct hwloc_backend *backend, int fulldiscovery)
{
  struct hwloc_x86_backend_data_s *data = backend->private_data;
  unsigned nbprocs = data->nbprocs;
  unsigned eax, ebx, ecx = 0, edx;
  unsigned i;
  unsigned highest_cpuid;
  unsigned highest_ext_cpuid;
  /* This stores cpuid features with the same indexing as Linux */
  unsigned features[10] = { 0 };
  struct procinfo *infos = NULL;
  enum cpuid_type cpuid_type = unknown;
  hwloc_x86_os_state_t os_state;
  struct hwloc_binding_hooks hooks;
  struct hwloc_topology_support support;
  struct hwloc_topology_membind_support memsupport __hwloc_attribute_unused;
  int (*get_cpubind)(hwloc_topology_t topology, hwloc_cpuset_t set, int flags) = NULL;
  int (*set_cpubind)(hwloc_topology_t topology, hwloc_const_cpuset_t set, int flags) = NULL;
  struct cpuiddump *src_cpuiddump = NULL;
  int ret = -1;

  if (data->src_cpuiddump_path) {
    /* just read cpuid from the dump */
    src_cpuiddump = cpuiddump_read(data->src_cpuiddump_path, 0);
  } else {
    /* otherwise check if binding works */
    memset(&hooks, 0, sizeof(hooks));
    support.membind = &memsupport;
    hwloc_set_native_binding_hooks(&hooks, &support);
    if (hooks.get_thisproc_cpubind && hooks.set_thisproc_cpubind) {
      get_cpubind = hooks.get_thisproc_cpubind;
      set_cpubind = hooks.set_thisproc_cpubind;
    } else if (hooks.get_thisthread_cpubind && hooks.set_thisthread_cpubind) {
      get_cpubind = hooks.get_thisthread_cpubind;
      set_cpubind = hooks.set_thisthread_cpubind;
    } else {
      /* we need binding support if there are multiple PUs */
      if (nbprocs > 1)
        goto out;
      get_cpubind = fake_get_cpubind;
      set_cpubind = fake_set_cpubind;
    }
  }

  if (!src_cpuiddump && !hwloc_have_x86_cpuid())
    goto out;

  infos = calloc(nbprocs, sizeof(struct procinfo));
  if (NULL == infos)
    goto out;
  for (i = 0; i < nbprocs; i++) {
    infos[i].nodeid = (unsigned) -1;
    infos[i].packageid = (unsigned) -1;
    infos[i].unitid = (unsigned) -1;
    infos[i].coreid = (unsigned) -1;
    infos[i].threadid = (unsigned) -1;
  }

  eax = 0x00;
  cpuid_or_from_dump(&eax, &ebx, &ecx, &edx, src_cpuiddump);
  highest_cpuid = eax;
  if (ebx == INTEL_EBX && ecx == INTEL_ECX && edx == INTEL_EDX)
    cpuid_type = intel;
  if (ebx == AMD_EBX && ecx == AMD_ECX && edx == AMD_EDX)
    cpuid_type = amd;

  hwloc_debug("highest cpuid %x, cpuid type %u\n", highest_cpuid, cpuid_type);
  if (highest_cpuid < 0x01) {
      goto out_with_infos;
  }

  eax = 0x01;
  cpuid_or_from_dump(&eax, &ebx, &ecx, &edx, src_cpuiddump);
  features[0] = edx;
  features[4] = ecx;

  eax = 0x80000000;
  cpuid_or_from_dump(&eax, &ebx, &ecx, &edx, src_cpuiddump);
  highest_ext_cpuid = eax;

  hwloc_debug("highest extended cpuid %x\n", highest_ext_cpuid);

  if (highest_cpuid >= 0x7) {
    eax = 0x7;
    cpuid_or_from_dump(&eax, &ebx, &ecx, &edx, src_cpuiddump);
    features[9] = ebx;
  }

  if (cpuid_type != intel && highest_ext_cpuid >= 0x80000001) {
    eax = 0x80000001;
    cpuid_or_from_dump(&eax, &ebx, &ecx, &edx, src_cpuiddump);
    features[1] = edx;
    features[6] = ecx;
  }

  hwloc_x86_os_state_save(&os_state, src_cpuiddump);

  ret = look_procs(backend, infos, fulldiscovery,
                   highest_cpuid, highest_ext_cpuid, features, cpuid_type,
                   get_cpubind, set_cpubind);
  if (ret >= 0)
    /* success, we're done */
    goto out_with_os_state;

  if (nbprocs == 1) {
    /* only one processor, no need to bind */
    look_proc(backend, &infos[0], highest_cpuid, highest_ext_cpuid, features, cpuid_type, src_cpuiddump);
    summarize(backend, infos);
    ret = fulldiscovery;
  }

out_with_os_state:
  hwloc_x86_os_state_restore(&os_state, src_cpuiddump);

out_with_infos:
  if (NULL != infos) {
      free(infos);
  }

out:
  if (src_cpuiddump)
    cpuiddump_free(src_cpuiddump);
  return ret;
}

static int
hwloc_x86_discover(struct hwloc_backend *backend)
{
  struct hwloc_x86_backend_data_s *data = backend->private_data;
  struct hwloc_topology *topology = backend->topology;
  int ret;

  if (!data->src_cpuiddump_path) {
    data->nbprocs = hwloc_fallback_nbprocessors(topology);

    if (!topology->is_thissystem) {
      hwloc_debug("%s", "\nno x86 detection (not thissystem)\n");
      return 0;
    }
  }

  if (topology->levels[0][0]->cpuset) {
    /* somebody else discovered things */
    if (topology->nb_levels == 2 && topology->level_nbobjects[1] == data->nbprocs) {
      /* only PUs were discovered, as much as we would, complete the topology with everything else */
      goto fulldiscovery;
    }

    /* several object types were added, we can't easily complete, just annotate a bit */
    ret = hwloc_look_x86(backend, 0);
    if (ret)
      hwloc_obj_add_info(topology->levels[0][0], "Backend", "x86");
    return 0;
  } else {
    /* topology is empty, initialize it */
    hwloc_alloc_obj_cpusets(topology->levels[0][0]);
  }

fulldiscovery:
  hwloc_look_x86(backend, 1);
  /* if failed, just continue and create PUs *///TODO change comment

  hwloc_obj_add_info(topology->levels[0][0], "Backend", "x86");

  if (!data->src_cpuiddump_path) { /* CPUID dump works for both x86 and x86_64 */
#ifdef HAVE_UNAME
    hwloc_add_uname_info(topology, NULL); /* we already know is_thissystem() is true */
#else
    /* uname isn't available, manually setup the "Architecture" info */
#ifdef HWLOC_X86_64_ARCH
    hwloc_obj_add_info(topology->levels[0][0], "Architecture", "x86_64");
#else
    hwloc_obj_add_info(topology->levels[0][0], "Architecture", "x86");
#endif
#endif
  }

  return 1;
}

static int
hwloc_x86_check_cpuiddump_input(const char *src_cpuiddump_path, hwloc_bitmap_t set)
{
  struct dirent *dirent;
  DIR *dir;
  char *path;
  FILE *file;
  char line [32];

  dir = opendir(src_cpuiddump_path);
  if (!dir)
    return -1;

  path = malloc(strlen(src_cpuiddump_path) + strlen("/hwloc-cpuid-info") + 1);
  if (!path)
    goto out_with_dir;

  sprintf(path, "%s/hwloc-cpuid-info", src_cpuiddump_path);
  file = fopen(path, "r");
  if (!file) {
    fprintf(stderr, "Couldn't open dumped cpuid summary %s\n", path);
    free(path);
    goto out_with_dir;
  }
  if (!fgets(line, sizeof(line), file)) {
    fprintf(stderr, "Found read dumped cpuid summary in %s\n", path);
    fclose(file);
    free(path);
    goto out_with_dir;
  }
  fclose(file);
  if (strcmp(line, "Architecture: x86\n")) {
    fprintf(stderr, "Found non-x86 dumped cpuid summary in %s: %s\n", path, line);
    free(path);
    goto out_with_dir;
  }
  free(path);

  while ((dirent = readdir(dir)) != NULL) {
    if (!strncmp(dirent->d_name, "pu", 2)) {
      char *end;
      unsigned long idx = strtoul(dirent->d_name+2, &end, 10);
      if (!*end)
        hwloc_bitmap_set(set, idx);
      else
        fprintf(stderr, "Ignoring invalid dirent `%s' in dumped cpuid directory `%s'\n",
                dirent->d_name, src_cpuiddump_path);
    }
  }
  closedir(dir);

  if (hwloc_bitmap_iszero(set)) {
    fprintf(stderr, "Did not find any valid pu%%u entry in dumped cpuid directory `%s'\n",
            src_cpuiddump_path);
    return -1;
  } else if (hwloc_bitmap_last(set) != hwloc_bitmap_weight(set) - 1) {
    /* The x86 backends enforces contigous set of PUs starting at 0 so far */
    fprintf(stderr, "Found non-contigous pu%%u range in dumped cpuid directory `%s'\n",
            src_cpuiddump_path);
    return -1;
  }

  return 0;

out_with_dir:
  closedir(dir);
  return -1;
}

static void
hwloc_x86_backend_disable(struct hwloc_backend *backend)
{
  struct hwloc_x86_backend_data_s *data = backend->private_data;
  hwloc_bitmap_free(data->apicid_set);
  if (data->src_cpuiddump_path)
    free(data->src_cpuiddump_path);
  free(data);
}

static struct hwloc_backend *
hwloc_x86_component_instantiate(struct hwloc_disc_component *component,
                                const void *_data1 __hwloc_attribute_unused,
                                const void *_data2 __hwloc_attribute_unused,
                                const void *_data3 __hwloc_attribute_unused)
{
  struct hwloc_backend *backend;
  struct hwloc_x86_backend_data_s *data;
  const char *src_cpuiddump_path;

  backend = hwloc_backend_alloc(component);
  if (!backend)
    goto out;

  data = malloc(sizeof(*data));
  if (!data) {
    errno = ENOMEM;
    goto out_with_backend;
  }

  backend->private_data = data;
  backend->flags = HWLOC_BACKEND_FLAG_NEED_LEVELS;
  backend->discover = hwloc_x86_discover;
  backend->disable = hwloc_x86_backend_disable;

  /* default values */
  data->apicid_set = hwloc_bitmap_alloc();
  data->apicid_unique = 1;
  data->src_cpuiddump_path = NULL;

  src_cpuiddump_path = getenv("HWLOC_CPUID_PATH");
  if (src_cpuiddump_path) {
    hwloc_bitmap_t set = hwloc_bitmap_alloc();
    if (!hwloc_x86_check_cpuiddump_input(src_cpuiddump_path, set)) {
      backend->is_thissystem = 0;
      data->src_cpuiddump_path = strdup(src_cpuiddump_path);
      data->nbprocs = hwloc_bitmap_weight(set);
    } else {
      fprintf(stderr, "Ignoring dumped cpuid directory.\n");
    }
    hwloc_bitmap_free(set);
  }

  return backend;

 out_with_backend:
  free(backend);
 out:
  return NULL;
}

static struct hwloc_disc_component hwloc_x86_disc_component = {
  HWLOC_DISC_COMPONENT_TYPE_CPU,
  "x86",
  HWLOC_DISC_COMPONENT_TYPE_GLOBAL,
  hwloc_x86_component_instantiate,
  45, /* between native and no_os */
  NULL
};

const struct hwloc_component hwloc_x86_component = {
  HWLOC_COMPONENT_ABI,
  NULL, NULL,
  HWLOC_COMPONENT_TYPE_DISC,
  0,
  &hwloc_x86_disc_component
};
