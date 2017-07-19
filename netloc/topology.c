/*
 * Copyright © 2013-2014 University of Wisconsin-La Crosse.
 *                         All rights reserved.
 * Copyright © 2016-2017 Inria.  All rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 * See COPYING in top-level directory.
 *
 * $HEADER$
 */

#define _GNU_SOURCE	   /* See feature_test_macros(7) */
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <libgen.h>

#include <private/netloc.h>

static int find_reverse_edges(netloc_topology_t *topology);

netloc_topology_t *netloc_topology_construct(char *path)
{
    int ret;
    char *line = NULL;
    size_t linesize = 0;

    netloc_topology_t *topology = NULL;

    /*
     * Allocate Memory
     */
    topology = (netloc_topology_t *)calloc(1, sizeof(netloc_topology_t));
    if( NULL == topology ) {
        fprintf(stderr, "ERROR: Memory error: topology cannot be allocated\n");
        return NULL;
    }

    xmlNode *root_node, *crt_node = NULL, *tmp = NULL;
    xmlChar *buff = NULL;
    size_t buffSize;
    int num_nodes = 0;
    netloc_hwloc_topology_t *hwloc_topos = NULL;
    xmlDoc *doc = netloc_xml_reader_init(path);
    if (NULL == doc) {
        return (free(topology), topology = NULL);
    }

    root_node = xmlDocGetRootElement(doc);
    if (root_node && XML_ELEMENT_NODE == root_node->type
        && !strcmp("topology", (char *)root_node->name)) {
        /* Check netloc file version */
        buff = xmlGetProp(root_node, BAD_CAST "version");
        if (!buff || 0 != strcmp(NETLOC_STR_VERS(NETLOCFILE_VERSION_2_0), (char *)buff)) {
            fprintf(stderr, "Incorrect version number (\"%s\"), "
                    "please generate your input file again.\n", (char *)buff);
            xmlFree(buff); buff = NULL;
            free(topology); topology = NULL;
            goto clean_and_out;
        }
        xmlFree(buff); buff = NULL;
    } else {
        fprintf(stderr, "Cannot read the topology in %s.\n", path);
        free(topology); topology = NULL;
        goto clean_and_out;
    }
    netloc_network_type_t transport_type;
    /* Check transport type */
    buff = xmlGetProp(root_node, BAD_CAST "transport");
    if (!buff) {
        fprintf(stderr, "Transport type not found, please generate your input file again.\n");
        xmlFree(buff); buff = NULL;
        free(topology); topology = NULL;
        goto clean_and_out;
    } else if (NETLOC_NETWORK_TYPE_INVALID ==
               (transport_type = netloc_network_type_decode((const char *)buff))) {
        fprintf(stderr, "Invalid network type (\"%s\"), please generate your "
                "input file again.\n", (char *)buff);
        xmlFree(buff); buff = NULL;
        free(topology); topology = NULL;
        goto clean_and_out;
    }
    xmlFree(buff); buff = NULL;
    /* Retreive subnet */
    char *subnet = NULL;
    if (root_node->children &&
        ((XML_TEXT_NODE == root_node->children->type && (crt_node = root_node->children->next))
         || (crt_node = root_node->children))
        && XML_ELEMENT_NODE == crt_node->type && !strcmp("subnet", (char *)crt_node->name)
        && crt_node->children && crt_node->children->content) {
        subnet = strdup((char *)crt_node->children->content);
    } else {
        fprintf(stderr, "Cannot read the subnet in %s\n", path);
        free(topology); topology = NULL;
        goto clean_and_out;
    }
    /* Retreive hwloc path */
    char *hwlocpath = NULL;
    if (crt_node->next &&
        ((XML_TEXT_NODE == crt_node->next->type && (crt_node = crt_node->next->next))
         || (crt_node = crt_node->next))
        && XML_ELEMENT_NODE == crt_node->type && !strcmp("hwloc_path", (char *)crt_node->name)
        && crt_node->children && crt_node->children->content) {
        hwlocpath = strdup((char *)crt_node->children->content);
    } else {
        fprintf(stderr, "Cannot read hwloc path in %s\n", path);
        free(subnet);
        free(topology);
        topology = NULL;
        goto clean_and_out;
    }

    if (hwlocpath) {
        DIR *hwlocdir;
        char *realhwlocpath;
        if (hwlocpath[0] != '/') {
            char *path_tmp = strdup(path);
            asprintf(&realhwlocpath, "%s/%s", dirname(path_tmp), hwlocpath);
            free(path_tmp);
            free(hwlocpath);
            hwlocpath = realhwlocpath;
        }
        if (!(hwlocdir = opendir(hwlocpath))) {
            fprintf(stderr, "Couldn't open hwloc directory: \"%s\"\n", hwlocpath);
            perror("opendir");
            free(subnet);
            free(hwlocpath);
            free(topology);
            topology = NULL;
            goto clean_and_out;
        } else {
            closedir(hwlocdir);
        }
    }

    /*
     * Initialize the structure
     */
    topology->nodes           = NULL;
    topology->physical_links  = NULL;
    topology->partitions      = NULL;
    topology->type            = NETLOC_TOPOLOGY_TYPE_INVALID;
    topology->nodesByHostname = NULL;
    topology->hwloc_topos     = NULL;
    topology->hwlocpaths      = NULL;
    topology->transport_type  = NETLOC_NETWORK_TYPE_INVALID;

    /* Read partitions from file */

    for (xmlNode *part = (crt_node->next && XML_TEXT_NODE == crt_node->next->type
                          ? crt_node->next->next : crt_node->next);
         part && XML_ELEMENT_NODE == part->type && part->children
             && !strcmp("partition", (char *)part->name);
         part = part->next && XML_TEXT_NODE == part->next->type ? part->next->next : part->next) {

        /* Check partition's size */
        long int nnodes = 0;
        char *strBuff;
        size_t strBuffSize;
        buff = xmlGetProp(part, BAD_CAST "size");
        if (!buff || (!(nnodes = strtol((char *)buff, &strBuff, 10)) && strBuff == (char *)buff)){
            fprintf(stderr, "WARN: cannot read partition's size.\n");
        }
        xmlFree(buff); buff = NULL; strBuff = NULL;
        num_nodes += (int)nnodes;

        /* Check partition's name */
        netloc_partition_t *partition;
        char *name = NULL;
        buff = xmlGetProp(part, BAD_CAST "name");
        if (!buff || !strlen((char *)buff)) {
            fprintf(stderr, "WARN: cannot read partition's name.\n");
            if (!buff)
                buff = BAD_CAST "";
        }
        if ('/' == *(char *)buff) { /* Exclude /extra+structural/ partition */
            partition = NULL;
        } else {
            partition = netloc_partition_construct(HASH_COUNT(topology->partitions),(char *)buff);
            HASH_ADD_STR(topology->partitions, name, partition);
        }
        if ('\0' != *(char *)buff)
            xmlFree(buff);
        buff = NULL;

        /* Read nodes from file */

        /* Check for <nodes> tag */
        if (!part->children
            || !((XML_TEXT_NODE == part->children->type && (crt_node = part->children->next))
                 || (crt_node = part->children))
            || XML_ELEMENT_NODE != crt_node->type
            || 0 != strcmp("nodes", (char *)crt_node->name)
            || (!crt_node->children && 0 < nnodes)) {
            fprintf(stderr, "WARN: No \"nodes\" tag, but %ld nodes required.\n", nnodes);
            continue;
        }
        for (xmlNode *it_node = crt_node->children && XML_ELEMENT_NODE != crt_node->children->type
                 ? crt_node->children->next : crt_node->children;
             it_node;
             it_node = it_node->next && XML_ELEMENT_NODE != it_node->next->type
                 ? it_node->next->next : it_node->next) {
            netloc_node_t *node = NULL;
            /* Prefetch physical_id to know if it's worth loading the node */
            buff = xmlGetProp(it_node, BAD_CAST "mac_addr");
            if (!buff) {
                continue;
            }
            HASH_FIND_STR(topology->nodes, buff, node);
            if (!node) {
                node = netloc_node_xml_load(it_node, hwlocpath, &hwloc_topos);
                if (!node) {
                    fprintf(stderr, "WARN: node cannot be loaded. Skipped\n");
                    continue;
                }
                /* Add to the hashtables */
                for (int n = 0; n < node->nsubnodes; ++n) {
                    HASH_ADD_STR(topology->nodes, physical_id, node->subnodes[n]);
                }
                HASH_ADD_STR(topology->nodes, physical_id, node);
                if (NETLOC_NODE_TYPE_HOST == node->type && 0 < strlen(node->hostname)) {
                    HASH_ADD_KEYPTR(hh2, topology->nodesByHostname, node->hostname,
                                    strlen(node->hostname), node);
                }
            }
            /* Add to the partition */
            if (partition) {
                for (int n = 0; n < node->nsubnodes; ++n) {
                    utarray_push_back(node->subnodes[n]->partitions, &partition);
                }
                utarray_push_back(partition->nodes, &node);
                utarray_push_back(node->partitions, &partition);
            }
        }
        if (partition && utarray_len(partition->nodes) != nnodes) {
            fprintf(stderr, "WARN: Found %u nodes, but %ld required.\n",
                    utarray_len(partition->nodes), nnodes);
            num_nodes -= nnodes;
            num_nodes += utarray_len(partition->nodes);
        }

        /* Read edges from file */

        /* Check for <connexions> tag */
        if (!crt_node->next
            || !((XML_TEXT_NODE == crt_node->next->type && (crt_node = crt_node->next->next))
                 || (crt_node = crt_node->next))
            || XML_ELEMENT_NODE != crt_node->type
            || 0 != strcmp("connexions", (char *)crt_node->name) || !crt_node->children) {
            if (1 < nnodes)
                fprintf(stderr, "WARN: No \"connexions\" tag.\n");
            continue;
        }
        for (xmlNode *it_edge = crt_node->children && XML_ELEMENT_NODE != crt_node->children->type
                 ? crt_node->children->next : crt_node->children;
             it_edge;
             it_edge = it_edge->next && XML_ELEMENT_NODE != it_edge->next->type
                 ? it_edge->next->next : it_edge->next) {
            netloc_edge_t *edge = netloc_edge_xml_load(it_edge, topology, partition);
            if (partition && edge) {
                utarray_push_back(partition->edges, &edge);
            }
        }
    }

    /* Set topology->physical_links->other_way */
    netloc_physical_link_t *plink = NULL, *plink_tmp = NULL, *plink_found = NULL;
    HASH_ITER(hh, topology->physical_links, plink, plink_tmp) {
        HASH_FIND(hh, topology->physical_links, &plink->other_way_id,
                  sizeof(unsigned long long int), plink_found);
        if (NULL == plink_found) {
            fprintf(stderr, "WARN: Strangely enough, the corresponding reverse physical link "
                    "seems to be absent...\n");
        } else {
            plink->other_way = plink_found;
            if (NULL == plink->edge->other_way)
                plink->edge->other_way = plink_found->edge;
        }
    }

    /* Change from hashtable to array for hwloc topologies */
    topology->nb_hwloc_topos = HASH_COUNT(hwloc_topos);
    if (0 < topology->nb_hwloc_topos) {
        topology->hwlocpaths     = malloc(sizeof(char *[topology->nb_hwloc_topos]));
        netloc_hwloc_topology_t *ptr, *ptr_tmp;
        HASH_ITER(hh, hwloc_topos, ptr, ptr_tmp) {
            topology->hwlocpaths[ptr->hwloc_topo_idx] = ptr->path;
            HASH_DEL(hwloc_topos, ptr);
            free(ptr);
        }
    } else {
        fprintf(stderr, "WARN: No hwloc topology found\n");
    }

    topology->topopath       = path;
    topology->hwloc_dir_path = hwlocpath;
    topology->subnet_id      = subnet;
    topology->transport_type = transport_type;

    if (find_reverse_edges(topology) != NETLOC_SUCCESS) {
        netloc_topology_destruct(topology);
        topology = NULL;
        goto clean_and_out;
    }

 clean_and_out:
    netloc_xml_reader_clean_and_out(doc);

    return topology;
}

int netloc_topology_destruct(netloc_topology_t *topology)
{
    /*
     * Sanity Check
     */
    if( NULL == topology ) {
        fprintf(stderr, "Error: Detaching from a NULL pointer\n");
        return NETLOC_ERROR;
    }

    free(topology->topopath);
    free(topology->subnet_id);

    /** Partition List */
    /* utarray_free(topology->partitions); */
    netloc_partition_t *part, *part_tmp;
    HASH_ITER(hh, topology->partitions, part, part_tmp) {
        HASH_DEL(topology->partitions, part);
        netloc_partition_destruct(part);
    }

    /* Nodes */
    netloc_node_t *node, *node_tmp;
    HASH_ITER(hh2, topology->nodesByHostname, node, node_tmp) {
        HASH_DELETE(hh2, topology->nodesByHostname, node);
    }
    netloc_topology_iter_nodes(topology, node, node_tmp) {
        HASH_DEL(topology->nodes, node);
        netloc_node_destruct(node);
    }

    /** Physical links */
    netloc_physical_link_t *link, *link_tmp;
    HASH_ITER(hh, topology->physical_links, link, link_tmp) {
        HASH_DEL(topology->physical_links, link);
        netloc_physical_link_destruct(link);
    }

    /** Hwloc topology List */
    for (unsigned int t = 0; t < topology->nb_hwloc_topos; ++t) {
        if (topology->hwlocpaths && topology->hwlocpaths[t])
            free(topology->hwlocpaths[t]);
        if (topology->hwloc_topos && topology->hwloc_topos[t])
            hwloc_topology_destroy(topology->hwloc_topos[t]);
    }
    free(topology->hwlocpaths);
    free(topology->hwloc_topos);
    free(topology->hwloc_dir_path);

    free(topology);

    return NETLOC_SUCCESS;
}

static int find_reverse_edges(netloc_topology_t *topology)
{
    netloc_node_t *node, *node_tmp;
    netloc_topology_iter_nodes(topology, node, node_tmp) {
        netloc_edge_t *edge, *edge_tmp;
        netloc_node_iter_edges(node, edge, edge_tmp) {
            netloc_node_t *dest = edge->dest;
            if (dest > node) {
                netloc_edge_t *reverse_edge;
                HASH_FIND_PTR(dest->edges, &node, reverse_edge);
                if (reverse_edge == NULL) {
                    return NETLOC_ERROR;
                }
                edge->other_way = reverse_edge;
                reverse_edge->other_way = edge;
            }
        }
    }
    return NETLOC_SUCCESS;
}
