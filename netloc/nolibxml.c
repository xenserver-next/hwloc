/* -*- encoding: utf-8 -*- */
/*
 * Copyright © 2017-2018 Inria.  All rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 * See COPYING in top-level directory.
 *
 * $HEADER$
 */

#define _GNU_SOURCE	   /* See feature_test_macros(7) */
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <libgen.h>

#include <private/autogen/config.h>
#include <private/netloc-xml.h>
#include <private/utils/xml.h>
#include <netloc/utarray.h>
#include <netloc/uthash.h>
#include <private/utils/cleaner.h>

#ifndef HWLOC_HAVE_LIBXML2

/**
 * Load the netloc partition as described in the xml file, of which
 * \ref part_node is a valid pointer to a DOM element.
 *
 * The returned element is to be added to the \ref
 * netloc_network_explicit_t. If NULL is returned, then the partition
 * is to be ignored (i.e., it may be invalid or the /extra+structural/
 * partition).
 *
 * \param part_id The partition rank in the hashtable linked list
 * \param part_node A valid XML DOM node pointing to the proper <partition> tag
 * \param hwlocpath The path to the directory containing the hwloc
 *                  topology files
 * \param hwloc_topos A valid pointer to the hwloc_topos field in
 *                    \ref netloc_network_explicit_t
 * \param topology A valid pointer to the current topology being loaded
 *
 * Returns
 *   A newly allocated and initialized pointer to the partition information.
 */
static netloc_partition_t *
netloc_part_xml_load(const unsigned int part_id,
                     xml_node_ptr part_node, char *hwlocpath,
                     netloc_hwloc_topology_t **hwloc_topos,
                     netloc_network_t *network);

/**
 * Load the netloc explicit topology as described in the xml file, of which
 * \ref it_expl is a valid pointer to a DOM element.
 *
 * The returned element is to be added to the \ref
 * netloc_partition_t.
 *
 * \param it_expl A valid XML DOM node pointing to the proper <explicit> tag
 * \param network A valid pointer to the current network
 * \param partition A valid pointer to the current partition
 * \param hwloc_path The path to the directory containing the hwloc topology
 *                  files
 * \param hwloc_topos A valid pointer to the hwloc_topos field in
 *                    \ref netloc_network_explicit_t
 *
 * Returns
 *   A newly allocated and initialized pointer to the explicit topology
 *   information.
 */
static netloc_network_explicit_t *
netloc_explicit_xml_load(xml_node_ptr expl, netloc_network_t *network,
                         netloc_partition_t *partition, char *hwloc_path,
                         netloc_hwloc_topology_t **hwloc_topos);

/**
 * Load the netloc node as described in the xml file, of which
 * \ref it_node is a valid pointer to a DOM element.
 *
 * The returned element is to be added to the \ref
 * netloc_network_t. If the \ref netloc_node_t returned has nsubnodes
 * > 0, this means that the node is virtual. The subnodes (contained
 * in the node->subnodes array) have to be added before the virtual
 * node to ease the hashtable liberation.
 *
 * \param it_node A valid XML DOM node pointing to the proper <node> tag
 * \param hwlocpath The path to the directory containing the hwloc topology
 *                  files
 * \param hwloc_topos A valid pointer to the hwloc_topos field in
 *                    \ref netloc_network_explicit_t
 *
 * Returns
 *   A newly allocated and initialized pointer to the node information.
 */
static netloc_node_t *
netloc_node_xml_load(xml_node_ptr it_node, char *hwlocpath,
                     netloc_hwloc_topology_t **hwloc_topos,
                     netloc_partition_t *partition);

/**
 * Load the netloc edge as described in the xml file, of which
 * \ref it_edge is a valid pointer to a DOM element.
 *
 * The returned element is to be added to the corresponding \ref
 * netloc_node_t. The node has to be already part of the \ref
 * netloc_network_t
 *
 * \param it_edge A valid XML DOM node pointing to the proper <connexion> tag
 * \param network A valid pointer to the current network being loaded
 * \param partition A valid pointer to the current partition being loaded
 *
 * Returns
 *   A newly allocated and initialized pointer to the edge information.
 */
static netloc_edge_t *
netloc_edge_xml_load(xml_node_ptr it_edge, netloc_network_t *network,
                     netloc_partition_t *partition);

/**
 * Load the netloc physical link as described in the xml file, of which
 * \ref it_link is a valid pointer to a DOM element.
 *
 * The returned element is to be added to the \ref netloc_edge_t.
 *
 * \param it_link A valid XML DOM node pointing to the proper <link> tag
 * \param edge A valid pointer to the current edge being loaded
 * \param partition A valid pointer to the current partition being loaded
 *
 * Returns
 *   A newly allocated and initialized pointer to the physical link information.
 */
static netloc_physical_link_t *
netloc_physical_link_xml_load(xml_node_ptr it_link, netloc_edge_t *edge,
                              netloc_partition_t *partition);

int netloc_machine_nolibxml_load(const char *path,
                                 netloc_machine_t **pmachine)
{
    netloc_cleaner_init();
    int ret = NETLOC_ERROR;
    char *buff;
    xml_node_ptr machine_node, net_node = NULL, crt_node = NULL;
    xml_doc_ptr doc = NULL;
    netloc_machine_t *machine = NULL;
    netloc_network_t *network = NULL;
    netloc_hwloc_topology_t *hwloc_topos = NULL;
    char *hwlocpath = NULL;

    if (NULL == pmachine) {
        if (netloc__xml_verbose())
            fprintf(stderr,
                    "ERROR: invalid machine pointer given as parameter\n");
        return NETLOC_ERROR;
    }
    if (NULL == path || 0 >= strlen(path)) {
        if (netloc__xml_verbose())
            fprintf(stderr, "ERROR: invalid path given (%s)\n", path);
        ret = NETLOC_ERROR_NOENT;
        goto clean_and_out;
    }

    machine = netloc_machine_construct();
    if (NULL == machine)
        return NETLOC_ERROR;

    doc = xml_node_read_file(path);
    machine_node = xml_doc_get_root_element(doc);
    netloc_cleaner_add(doc, xml_doc_free);
    if (NULL == machine_node) {
        if (netloc__xml_verbose())
            fprintf(stderr, "ERROR: unable to parse the XML file.\n");
        netloc_cleaner_add(machine, netloc_machine_destruct);
        ret = NETLOC_ERROR;
        goto clean_and_out;
    }
    if ( !strcmp("machine", xml_node_get_name(machine_node))
         && 0 < xml_node_get_num_children(machine_node) ) {
        /* Check netloc file version */
        buff = xml_node_attr_get(machine_node, "version");
        if (!buff ||
            0 != strcmp(NETLOC_STR_VERS(NETLOCFILE_VERSION_2_0), buff)) {
            if (netloc__xml_verbose())
                fprintf(stderr, "ERROR: incorrect version number (\"%s\"), "
                        "please generate your input file again.\n", buff);
            free(buff); buff = NULL;
            netloc_cleaner_add(machine, netloc_machine_destruct);
            machine = NULL;
            goto clean_and_out;
        }
        free(buff); buff = NULL;
        /* Retrieve hwloc path */
        size_t net_id = 1;
        if (net_id < xml_node_get_num_children(machine_node) &&
            (crt_node = (xml_node_ptr) xml_node_get_child(machine_node, 0))
            && !strcmp("hwloc_path", xml_node_get_name(crt_node))
            && xml_node_get_content(crt_node)) {
            hwlocpath = strdup(xml_node_get_content(crt_node));
        } else {
            if (netloc__xml_verbose())
                fprintf(stderr, "WARN: unable to read hwloc path in %s\n",
                        path);
            net_id -= 1;
        }
        if (net_id < xml_node_get_num_children(machine_node)) {
            /* Find machine topology root_node */
            net_node = (xml_node_ptr) xml_node_get_child(machine_node, net_id);
        }
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
        netloc_cleaner_add(hwlocpath, free);
        if (!(hwlocdir = opendir(hwlocpath))) {
            if (netloc__xml_verbose())
                fprintf(stderr, "ERROR: could not open hwloc directory: "
                        "\"%s\"\n", hwlocpath);
            perror("opendir");
            netloc_cleaner_add(machine, netloc_machine_destruct);
            machine = NULL;
            goto clean_and_out;
        } else {
            closedir(hwlocdir);
        }
    }

    /* Network */
    if (NULL == net_node || 0 != strcmp("network", xml_node_get_name(net_node))) {
        if (netloc__xml_verbose())
            fprintf(stderr, "ERROR: invalid XML file. Please, generate your "
                    "input file again.\n");
        netloc_cleaner_add(machine, netloc_machine_destruct); machine = NULL;
        goto clean_and_out;
    }

    netloc_network_type_t transport_type;
    /* Check transport type */
    buff = xml_node_attr_get(net_node, "transport");
    if (!buff) {
        if (netloc__xml_verbose())
            fprintf(stderr, "ERROR: transport type not found, please generate "
                    "your input file again.\n");
        netloc_cleaner_add(machine, netloc_machine_destruct); machine = NULL;
        goto clean_and_out;
    } else if (NETLOC_NETWORK_TYPE_INVALID ==
               (transport_type = netloc_network_type_decode(buff))) {
        if (netloc__xml_verbose())
            fprintf(stderr, "ERROR: invalid network type (\"%s\"), please "
                    "generate your input file again.\n", buff);
        free(buff); buff = NULL;
        netloc_cleaner_add(machine, netloc_machine_destruct);
        machine = NULL;
        goto clean_and_out;
    }
    free(buff); buff = NULL;

#warning should be depending on the transport type
    {
        /* Retrieve subnet */
        char *subnet = NULL, *content = NULL;
        if (0 < xml_node_get_num_children(net_node)
            && (crt_node = (xml_node_ptr) xml_node_get_child(net_node, 0))
            && !strcmp("subnet", xml_node_get_name(crt_node))
            && (content = xml_node_get_content(crt_node))) {
            subnet = strdup(content);
            netloc_cleaner_add(subnet, free);
        } else {
            if (netloc__xml_verbose())
                fprintf(stderr, "ERROR: cannot read the subnet in %s\n", path);
            netloc_cleaner_add(machine, netloc_machine_destruct);
            machine = NULL;
            goto clean_and_out;
        }

        netloc_network_ib_t *ib_net = netloc_network_ib_construct(subnet);
        if (NULL == ib_net) {
            netloc_cleaner_add(ib_net, netloc_network_ib_destruct);
            netloc_cleaner_add(machine, netloc_machine_destruct);
            machine = NULL;
            goto clean_and_out;
        }
        network = &ib_net->super;
    }

    /* Read partitions from file */
    for (size_t part_id = 1;
            part_id < xml_node_get_num_children(net_node);
            ++part_id) {
        xml_node_ptr part = xml_node_get_child(net_node, part_id);
        if (!part) continue;
        /* Load partition */
        netloc_partition_t *partition = NULL;
        partition =
            netloc_part_xml_load(HASH_COUNT(network->partitions), part,
                                 hwlocpath, &hwloc_topos, network);
        if (NULL != partition)
            HASH_ADD_STR(network->partitions, name, partition);
    }

    /* Change from hashtable to array for hwloc topologies */
    machine->nb_hwloc_topos = HASH_COUNT(hwloc_topos);
    if (0 < machine->nb_hwloc_topos) {
        machine->hwlocpaths = malloc(sizeof(char *[HASH_COUNT(hwloc_topos)]));
        netloc_hwloc_topology_t *ptr, *ptr_tmp;
        HASH_ITER(hh, hwloc_topos, ptr, ptr_tmp) {
            machine->hwlocpaths[ptr->hwloc_topo_idx] = ptr->path;
            HASH_DEL(hwloc_topos, ptr);
            free(ptr);
        }
    } else {
        if (netloc__xml_verbose())
            fprintf(stderr, "WARN: no hwloc topology found\n");
    }

    machine->hwloc_dir_path = strdup(hwlocpath);
    if (netloc_network_find_reverse_edges(network) != NETLOC_SUCCESS) {
        if (netloc__xml_verbose())
            fprintf(stderr, "ERROR: cannot find reverse corresponding "
                    "edges.\n");
        netloc_cleaner_add(machine, netloc_machine_destruct);
        machine = NULL;
        goto clean_and_out;
    }

    machine->topopath = strdup(path);
    machine->network = network;
    ret = NETLOC_SUCCESS;

 clean_and_out:
    *pmachine = machine;
    netloc_cleaner_done();

    return ret;
}

static netloc_network_explicit_t *
netloc_explicit_xml_load(xml_node_ptr expl, netloc_network_t *network,
                         netloc_partition_t *partition, char *hwloc_path,
                         netloc_hwloc_topology_t **hwloc_topos)
{
    xml_node_ptr nodes_node, edges_node;
    netloc_network_explicit_t *topo;
    const unsigned int nnodes = 0;
    char *buff = NULL;

    if (!xml_node_get_num_children(expl)) {
        if (netloc__xml_verbose())
            fprintf(stderr, "WARN: empty \"explicit\" tag.\n");
        return NULL;
    }

    /* Read nodes from file */

    /* Check for <nodes> tag */
    if (!xml_node_get_num_children(expl)
        || !(nodes_node = (xml_node_ptr) xml_node_get_child(expl, 0))
        || 0 != strcmp("nodes", xml_node_get_name(nodes_node))
        || (!xml_node_get_num_children(nodes_node) && 0 < nnodes)) {
        if (netloc__xml_verbose())
            fprintf(stderr, "WARN: no \"nodes\" tag, but %u nodes "
                    "required.\n", nnodes);
        return NULL;
    }

    topo = netloc_network_explicit_construct();

    for (size_t node_id = 0;
            node_id < xml_node_get_num_children(nodes_node);
            ++node_id) {
        xml_node_ptr it_node =
            (xml_node_ptr) xml_node_get_child(nodes_node, node_id);
        if (!it_node) continue;
        netloc_node_t *node = NULL;
        /* Prefetch physical_id to know if it's worth loading the node */
        buff = xml_node_attr_get(it_node, "mac_addr");
        if (!buff) continue;
        HASH_FIND_STR(network->nodes, buff, node);
        if (!node) {
            node = netloc_node_xml_load(it_node, hwloc_path, hwloc_topos,
                                        partition);
            if (!node) {
                if (netloc__xml_verbose())
                    fprintf(stderr, "WARN: node cannot be loaded. Skipped\n");
                continue;
            }
            /* Add to the hashtables */
            HASH_ADD_STR(network->nodes, physical_id, node);
            for (unsigned int n = 0; n < node->nsubnodes; ++n) {
                HASH_ADD_STR(network->nodes, physical_id,
                             node->subnodes[n]);
            }
            if (NETLOC_NODE_TYPE_HOST == node->type
                && 0 < strlen(node->hostname)) {
                HASH_ADD_KEYPTR(hh2, topo->nodesByHostname, node->hostname,
                                strlen(node->hostname), node);
            }
        }
        free(buff); buff = NULL;
        /* Add to the partition */
        if (partition) {
            for (unsigned int n = 0; n < node->nsubnodes; ++n) {
                utarray_push_back(&node->subnodes[n]->partitions,
                                  &partition);
            }
            utarray_push_back(&topo->nodes, &node);
            utarray_push_back(&node->partitions, &partition);
        }
    }

    /* Read edges from file */

    /* Check for <connexions> tag:
       - expl has to have children
       - if nnodes > 0, then, there has already been 1 child, so we need
       at least 2
       - we set edges_node to the last children, and it can't be NULL
       - edges_node's name has to be connexions
    */
    size_t nchildren = xml_node_get_num_children(expl);
    if (!nchildren || (partition && nnodes > 0 && 2 > nchildren)
        || !(edges_node =
             (xml_node_ptr) xml_node_get_child(expl, nchildren-1))
        || strcmp("connexions", xml_node_get_name(edges_node))) {
        if (1 < nnodes)
            if (netloc__xml_verbose())
                fprintf(stderr, "WARN: no \"connexions\" tag.\n");
        return topo;
    } else if (!xml_node_get_num_children(edges_node)
            || !xml_node_has_child_data(edges_node)) {
        if (netloc__xml_verbose())
            fprintf(stderr, "WARN: empty \"connexions\" tag.\n");
        return topo;
    }
    for (size_t edge_id = 0;
            edge_id < xml_node_get_num_children(edges_node);
            ++edge_id) {
        xml_node_ptr it_edge =
            (xml_node_ptr) xml_node_get_child(edges_node, edge_id);
        (void) netloc_edge_xml_load(it_edge, network, partition);
    }

    /* Set topo->physical_links->other_way */
    netloc_physical_link_t *plink = NULL,
        *plink_tmp = NULL, *plink_found = NULL;
    HASH_ITER(hh, network->physical_links, plink, plink_tmp) {
        HASH_FIND(hh, network->physical_links, &plink->other_way_id,
                  sizeof(unsigned long long int), plink_found);
        if (NULL == plink_found) {
            if (netloc__xml_verbose())
                fprintf(stderr, "WARN: strangely enough, the corresponding "
                        "reverse physical link seems to be absent...\n");
        } else {
            plink->other_way = plink_found;
            if (NULL == plink->edge->other_way)
                plink->edge->other_way = plink_found->edge;
        }
    }

    return topo;
}

static netloc_partition_t *
netloc_part_xml_load(const unsigned int part_id,
                     xml_node_ptr part, char *hwloc_path,
                     netloc_hwloc_topology_t **hwloc_topos,
                     netloc_network_t *network)
{
    char *buff, *strBuff;
    xml_node_ptr explicit_node = NULL;

    /* Check partition's name */
    netloc_partition_t *partition;
    buff = xml_node_attr_get(part, "name");
    if (!buff || !strlen(buff)) {
        if (netloc__xml_verbose())
            fprintf(stderr, "WARN: cannot read partition's name.\n");
        if (!buff)
            buff = "";
    }

    partition = netloc_partition_construct(part_id, buff);
    if ('\0' != *buff)
        free(buff);
    buff = NULL;

    /* Check for <explicit> tag */
    if (!xml_node_get_num_children(part)
        || !(explicit_node = (xml_node_ptr) xml_node_get_child(part, 0))
        || (0 != strcmp("explicit", xml_node_get_name(explicit_node))
            && (!(explicit_node = (xml_node_ptr) xml_node_get_child(part, 1))
                || strcmp("explicit", xml_node_get_name(explicit_node))))) {
        if (netloc__xml_verbose())
            fprintf(stderr, "WARN: no \"explicit\" tag.\n");
        return partition;
    }

    if ('/' == *partition->name) {
        netloc_network_explicit_t *topo =
            netloc_explicit_xml_load(explicit_node, network, NULL,
                                     hwloc_path, hwloc_topos);
        netloc_partition_destruct(partition);
        netloc_network_explicit_destruct(topo);
        partition = NULL;
    } else {
        /* Check partition's size */
        unsigned long int nnodes = 0;
        buff = xml_node_attr_get(explicit_node, "size");
        if (!buff || (!(nnodes = strtoul(buff, &strBuff, 10))
                      && strBuff == buff)){
            if (netloc__xml_verbose())
                fprintf(stderr, "WARN: cannot read partition's size.\n");
        }
        free(buff); buff = NULL; strBuff = NULL;
        partition->size = (unsigned) nnodes;

        netloc_network_explicit_t *topo =
            netloc_explicit_xml_load(explicit_node, network, partition,
                                     hwloc_path, hwloc_topos);
        partition->desc = topo;

        /* Check that size correspond to the number of nodes found */
        if (utarray_len(&topo->nodes) != (unsigned) nnodes) {
            if (netloc__xml_verbose())
                fprintf(stderr, "WARN: found %u nodes, but %lu required.\n",
                        utarray_len(&topo->nodes), nnodes);
        }
    }

    return partition;
}

static netloc_node_t *
netloc_node_xml_load(xml_node_ptr it_node, char *hwlocpath,
                     netloc_hwloc_topology_t **hwloc_topos,
                     netloc_partition_t *partition) {
    xml_node_ptr tmp = NULL;
    char *strBuff = NULL, *buff = NULL;
    size_t strBuffSize, buffSize;
    netloc_node_t *node = netloc_node_construct();
    /* read hostname */
    strBuff = xml_node_attr_get(it_node, "name");
    if (!strBuff || 0 >= strlen(strBuff)) {
        free(strBuff); strBuff = strdup("");
    }
    /* set physical_id */
    buff = xml_node_attr_get(it_node, "mac_addr");
    if (buff && 0 < strlen(buff)) {
        strncpy(node->physical_id, buff, 20);
        /* Should be shorter than 20 */
        node->physical_id[19] = '\0'; /* If a problem occurs */
        free(buff); buff = NULL;
    } else {
        if (netloc__xml_verbose())
            fprintf(stderr, "ERROR: node \"%s\" has no physical address and "
                    "is not added to the topology.\n",
                    strBuff && strlen(strBuff) ? strBuff : "(no name)");
        free(buff); free(strBuff);
        netloc_node_destruct(node);
        return NULL;
    }
    /* set hostname */
    node->hostname = strBuff; strBuff = NULL;
    /* set type (i.e., host or switch) */
    buff = xml_node_attr_get(it_node, "type");
    if (buff)
        node->type = netloc_node_type_decode(buff);
    free(buff); buff = NULL;
    /* Set description */
    if (xml_node_get_num_children(it_node) && xml_node_has_child_data(it_node)
        && (tmp = xml_node_get_child(it_node, 0))
        && 0 == strcmp("description", xml_node_get_name(tmp))
        && xml_node_get_content(tmp)) {
        node->description = strdup(xml_node_get_content(tmp));
    }
    /* set hwloc topology field iif node is a host */
    if (NETLOC_NODE_TYPE_HOST == node->type) {
        if ((buff = xml_node_attr_get(it_node, "hwloc_file"))
            && 0 < (buffSize = strlen(buff))) {
            netloc_hwloc_topology_t *hwloc_topo = NULL;
            char *refname;
            strBuffSize = asprintf(&strBuff, "%s/%s", hwlocpath, buff);
            if (5 > strBuffSize) { /* bad return from asprintf() */
                if (netloc__xml_verbose())
                    fprintf(stderr, "WARN: invalid topology file \"%s/%s\", or "
                            "memory exhaustion\n", hwlocpath, buff);
            } else if (9 < strBuffSize
                       && 0 == strcmp(".diff.xml", &strBuff[strBuffSize-9])) {
                /* hwloc_file point to a diff file */
                hwloc_topology_diff_t diff;
                if (0 <= hwloc_topology_diff_load_xml(strBuff, &diff, &refname)
                    && refname) {
                    /* Load diff file to check on refname */
                    hwloc_topology_diff_destroy(diff);
                    free(strBuff);
                    strBuffSize = asprintf(&strBuff, "%s/%s", hwlocpath,
                                           refname);
                    free(refname);
                } else {
                    if (netloc__xml_verbose())
                        fprintf(stderr, "WARN: no refname for topology file "
                                "\"%s/%s\"\n", hwlocpath, buff);
                }
            }
            HASH_FIND(hh, *hwloc_topos, strBuff,
                      (unsigned)strBuffSize, hwloc_topo);
            if (!hwloc_topo) {
                /* The topology cannot be retrieved in the hashtable */
                size_t hwloc_topo_idx = (size_t)HASH_COUNT(*hwloc_topos);
                hwloc_topo = malloc(sizeof(netloc_hwloc_topology_t));
                hwloc_topo->path = strdup(strBuff);
                hwloc_topo->hwloc_topo_idx = hwloc_topo_idx;
                node->hwloc_topo_idx = hwloc_topo_idx;
                hwloc_topo_idx += 1;
                HASH_ADD_STR(*hwloc_topos, path, hwloc_topo);
            } else {
                node->hwloc_topo_idx = hwloc_topo->hwloc_topo_idx;
            }
            free(strBuff); strBuff = NULL;
            free(buff); buff = NULL;
        }
        if (NULL != partition->topo
            && (buff = xml_node_attr_get(it_node, "index"))) {
            netloc_arch_node_t *arch_node = netloc_arch_node_construct();
            arch_node->node = node;
            arch_node->name = strdup(node->hostname);
            arch_node->idx_in_topo = atoi(buff);
            HASH_ADD_STR(partition->topo->nodes_by_name, name, arch_node);
            free(buff); buff = NULL;
        }
    } else if (NETLOC_NODE_TYPE_SWITCH == node->type
               && (buff = xml_node_attr_get(it_node, "virtual"))
               && 0 < (buffSize = strlen(buff))
               && 0 == strncmp("yes", buff, 3)) {
        /* Virtual node */
        free(buff); buff = NULL;
        buff = xml_node_attr_get(it_node, "size");
        if (1 != sscanf(buff, "%u", &node->nsubnodes)) {
            if (netloc__xml_verbose())
                fprintf(stderr, "WARN: cannot read how many subnodes are "
                        "included into the virtual node \"%s\"\n",
                        node->physical_id);
        }
        free(buff); buff = NULL;
        /* Add subnodes */
        if (!node->description) {
            tmp = (xml_node_ptr) xml_node_get_child(it_node, 0);
            node->description = strndup(node->physical_id, 20);
        } else {
            tmp = (xml_node_ptr) xml_node_get_child(it_node, 1);
        }
        if (!tmp || !xml_node_get_name(tmp)
                || 0 != strcmp("subnodes", xml_node_get_name(tmp))) {
            if (netloc__xml_verbose())
                fprintf(stderr, "WARN: virtual node \"%s\" is empty, and thus, "
                        "ignored\n", node->physical_id);
            netloc_node_destruct(node);
            return NULL;
        }
        /* Allocate subnodes array */
        node->subnodes = malloc(sizeof(netloc_node_t *[node->nsubnodes]));
        if (!node->subnodes) {
            if (netloc__xml_verbose())
                fprintf(stderr, "WARN: unable to allocate the memory for "
                        "virtual node \"%s\" subnodes. This node is thus "
                        "ignored\n", node->physical_id);
            netloc_node_destruct(node);
            return NULL;
        }
        size_t subnode_id;
        for (subnode_id = 0;
                subnode_id < xml_node_get_num_children(tmp);
                ++subnode_id) {
            xml_node_ptr it_subnode = xml_node_get_child(tmp, subnode_id);
            netloc_node_t *subnode =
                netloc_node_xml_load(it_subnode, hwlocpath,
                                     hwloc_topos, partition);
            if (subnode)
                subnode->virtual_node = node;
            node->subnodes[subnode_id] = subnode;
        }
        /* Check we found all the subnodes */
        if (xml_node_get_num_children(tmp) != node->nsubnodes) {
            if (netloc__xml_verbose())
                fprintf(stderr, "WARN: expecting %u subnodes, but %zu found\n",
                        node->nsubnodes, xml_node_get_num_children(tmp));
        }
    }
    return node;
}

static netloc_edge_t *
netloc_edge_xml_load(xml_node_ptr it_edge, netloc_network_t *network,
                     netloc_partition_t *partition)
{
    xml_node_ptr tmp;
    char *buff = NULL, *strBuff = NULL;
    size_t strBuffSize;
    netloc_edge_t *edge = netloc_edge_construct();
    /* set total_gbits */
    float total_gbits;
    buff = xml_node_attr_get(it_edge, "bandwidth");
    if (buff && 0 < strlen(buff)) {
        total_gbits = strtof(buff, &strBuff);
        if (0 == total_gbits && strBuff == buff)
            if (netloc__xml_verbose())
                fprintf(stderr, "WARN: cannot read connexion's bandwidth.\n");
        free(buff); buff = NULL; strBuff = NULL;
        edge->total_gbits = total_gbits;
    }
    /* get number of links */
    unsigned int nlinks;
    buff = xml_node_attr_get(it_edge, "nblinks");
    if (!buff || (1 > sscanf(buff, "%u", &nlinks))) {
        if (netloc__xml_verbose())
            fprintf(stderr, "WARN: cannot read connexion's physical links.\n");
    } else if (0 >= nlinks) {
        if (netloc__xml_verbose())
            fprintf(stderr, "WARN: less than 1 connexion's physical link "
                    "required (%u).\n", nlinks);
    }
    free(buff); buff = NULL;
    /* Move tmp to the proper xml node and set src node */
    if (0 < xml_node_get_num_children(it_edge)
            && xml_node_has_child_data(it_edge)
            && (tmp = xml_node_get_child(it_edge, 0))
            && 0 == strcmp("src", xml_node_get_name(tmp))
            && xml_node_get_content(tmp)) {
        netloc_network_find_node(network, xml_node_get_content(tmp),
                                 edge->node);
    } else {
        if (netloc__xml_verbose())
            fprintf(stderr, "ERROR: cannot find connexion's source node.\n");
        goto ERROR;
    }
    /* Move tmp to the proper xml node and set dest node */
    if (1 < xml_node_get_num_children(it_edge) && xml_node_has_child_data(it_edge)
        && (tmp = xml_node_get_child(it_edge, 1))
        && 0 == strcmp("dest", xml_node_get_name(tmp))
        && xml_node_get_content(tmp)) {
        netloc_network_find_node(network, xml_node_get_content(tmp),
                                 edge->dest);
    } else {
        if (netloc__xml_verbose())
            fprintf(stderr, "ERROR: cannot find connexion's destination "
                    "node.\n");
        goto ERROR;
    }
    /* Add edge to its node if it does not already exists */
    netloc_edge_t *edge_tmp = NULL;
    HASH_FIND_STR(edge->node->edges, edge->dest->physical_id, edge_tmp);
    if (NULL == edge_tmp) {
        HASH_ADD_STR(edge->node->edges, dest->physical_id, edge);
        /* Set partition */
        if (partition)
            utarray_push_back(&edge->partitions, &partition);
    } else {
        /* Set partition */
        if (partition) {
            utarray_push_back(&edge_tmp->partitions, &partition);
            for (unsigned int se = 0; se < edge_tmp->nsubedges; ++se) {
                utarray_push_back(&edge_tmp->subnode_edges[se]->partitions,
                                  &partition);
            }
        }
        /* Edge already created from another partition */
        netloc_edge_destruct(edge);
        return edge_tmp;
    }

    if ((buff = xml_node_attr_get(it_edge, "virtual"))
        && 0 < (strBuffSize = strlen(buff))
        && 0 == strncmp("yes", buff, 3)) {
        /*** VIRTUAL EDGE ***/
        free(buff); buff = NULL;
        /* Move tmp to the proper xml node */
        unsigned int nsubedges;
        if (!(2 < xml_node_get_num_children(it_edge)
              && xml_node_has_child_data(it_edge)
              && (tmp = xml_node_get_child(it_edge, 2))
              && 0 == strcmp("subconnexions", xml_node_get_name(tmp))
              && xml_node_get_num_children(tmp))) {
            if (netloc__xml_verbose())
                fprintf(stderr, "ERROR: cannot find the subedges. Failing to "
                        "read this connexion\n");
            goto ERROR;
        }
        buff = xml_node_attr_get(tmp, "size");
        if (!buff || (1 > sscanf(buff, "%u", &nsubedges))) {
            if (netloc__xml_verbose())
                fprintf(stderr, "ERROR: cannot read virtual edge subnodes.\n");
            goto ERROR;
        } else if (2 > nsubedges) {
            if (netloc__xml_verbose())
                fprintf(stderr, "WARN: less than 2 edges (%u).\n", nsubedges);
        }
        edge->nsubedges = nsubedges;
        edge->subnode_edges =
            (netloc_edge_t **)malloc(sizeof(netloc_edge_t *[nsubedges]));
        free(buff); buff = NULL;
        netloc_edge_t *subedge = NULL;
        for (size_t se = 0; se < xml_node_get_num_children(tmp); ++se) {
            subedge = netloc_edge_xml_load(xml_node_get_child(tmp, se),
                                           network, partition);
            edge->subnode_edges[se] = subedge;
            if (!subedge) {
                if (netloc__xml_verbose())
                    fprintf(stderr,
                            "WARN: cannot read subconnexion #%zu\n", se);
                continue;
            }
            total_gbits -= subedge->total_gbits;
            utarray_concat(&edge->node->physical_links, &subedge->physical_links);
            utarray_concat(&edge->physical_links, &subedge->physical_links);
        }
    } else {

        /* Read physical links from file */

        /* Check for <links> tag */
        if (!(2 < xml_node_get_num_children(it_edge)
              && xml_node_has_child_data(it_edge)
              && (tmp = xml_node_get_child(it_edge, 2))
              && 0 == strcmp("links", xml_node_get_name(tmp))
              && xml_node_get_num_children(tmp))) {
            if (netloc__xml_verbose())
                fprintf(stderr, "ERROR: no \"links\" tag.\n");
            goto ERROR;
        }
        netloc_physical_link_t *link = NULL;
        for (size_t l = 0; l < xml_node_get_num_children(tmp); ++l) {
            link = netloc_physical_link_xml_load(xml_node_get_child(tmp, l),
                                                 edge, partition);
            if (link) {
                total_gbits -= link->gbits;
                utarray_push_back(&edge->node->physical_links, &link);
                utarray_push_back(&edge->physical_links, &link);
                HASH_ADD(hh, network->physical_links, id,
                         sizeof(unsigned long long int), link);
            }
        }
    }
#ifdef NETLOC_DEBUG
    /* Check proper value for edge->total_gbits */
    if (0 != total_gbits) {
        if (netloc__xml_verbose())
            fprintf(stderr, "WARN: erroneous value read from file for edge "
                    "total bandwidth. \"%f\" instead of %f (calculated).\n",
                    edge->total_gbits, edge->total_gbits - total_gbits);
    }
    /* Check proper value for nlinks and utarray_len(edge->physical_links) */
    if (nlinks != utarray_len(&edge->physical_links)) {
        if (netloc__xml_verbose())
            fprintf(stderr, "WARN: erroneous value read from file for edge "
                    "count of physical links. %u requested, but only %u "
                    "found\n", nlinks, utarray_len(&edge->physical_links));
    }
#endif /* NETLOC_DEBUG */
    return edge;
 ERROR:
    netloc_edge_destruct(edge);
    return NULL;
}

static netloc_physical_link_t *
netloc_physical_link_xml_load(xml_node_ptr it_link, netloc_edge_t *edge,
                              netloc_partition_t *partition)
{
    char *buff = NULL, *tmp_buff = NULL;
    netloc_physical_link_t *link = netloc_physical_link_construct();
    /* set ports */
    int tmpport;
    buff = xml_node_attr_get(it_link, "srcport");
    if (!buff ||
        (!(tmpport = strtol(buff, &tmp_buff, 10)) && tmp_buff == buff)){
        if (netloc__xml_verbose())
            fprintf(stderr, "ERROR: cannot read physical link's source "
                    "port.\n");
        goto ERROR;
    }
    free(buff); buff = NULL;
    link->ports[0] = tmpport;
    buff = xml_node_attr_get(it_link, "destport");
    if (!buff ||
        (!(tmpport = strtol(buff, &tmp_buff, 10)) && tmp_buff == buff)){
        if (netloc__xml_verbose())
            fprintf(stderr, "ERROR: cannot read physical link's destination "
                    "port.\n");
        goto ERROR;
    }
    free(buff); buff = NULL;
    link->ports[1] = tmpport;
    /* set speed */
    buff = xml_node_attr_get(it_link, "speed");
    if (!buff || 0 >= strlen(buff)) {
        if (netloc__xml_verbose())
            fprintf(stderr, "ERROR: cannot read physical link's speed.\n");
        goto ERROR;
    }
    link->speed = buff; buff = NULL;
    /* set width */
    buff = xml_node_attr_get(it_link, "width");
    if (!buff || 0 >= strlen(buff)) {
        if (netloc__xml_verbose())
            fprintf(stderr, "ERROR: cannot read physical link's width.\n");
        goto ERROR;
    }
    link->width = buff; buff = NULL;
    /* set gbits */
    float gbits;
    buff = xml_node_attr_get(it_link, "bandwidth");
    if (!buff || 0 >= strlen(buff)
        || (!(gbits = strtof(buff, &tmp_buff)) && tmp_buff == buff)) {
        if (netloc__xml_verbose())
            fprintf(stderr, "ERROR: cannot read physical link's bandwidth.\n");
        goto ERROR;
    }
    free(buff); buff = NULL; tmp_buff = NULL;
    /* set id */
    unsigned long long int id_read;
    buff = xml_node_attr_get(it_link, "logical_id");
    if (!buff || 0 >= strlen(buff) || 1 > sscanf(buff, "%llu", &id_read)) {
        if (netloc__xml_verbose())
            fprintf(stderr, "ERROR: cannot read physical link's id.\n");
        goto ERROR;
    }
    link->id = id_read;
    free(buff); buff = NULL; tmp_buff = NULL;
    /* set other_way_id */
    buff = xml_node_attr_get(it_link, "reverse_logical_id");
    if (!buff || 0 >= strlen(buff) || 1 > sscanf(buff, "%llu", &id_read)) {
        if (netloc__xml_verbose())
            fprintf(stderr, "ERROR: cannot read reverse physical link's id.\n");
        goto ERROR;
    }
    link->other_way_id = id_read;
    free(buff); buff = NULL; tmp_buff = NULL;
    /* set description */
    if (xml_node_get_content(it_link)) {
        link->description = strdup(xml_node_get_content(it_link));
    }
    /* set src, dest, partition, edge */
    link->src   = edge->node;
    link->dest  = edge->dest;
    link->edge  = edge;
    link->gbits = gbits;
    if (partition)
        utarray_push_back(&link->partitions, &partition);

    return link;

 ERROR:
    netloc_physical_link_destruct(link);
    free(buff); buff = NULL;
    return NULL;
}
#endif
