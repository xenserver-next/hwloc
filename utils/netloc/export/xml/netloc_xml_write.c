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

/*
 * This file provides general function to generate XML topology files.
 */

#define _GNU_SOURCE         /* See feature_test_macros(7) */

#include <netloc.h>
#include <private/netloc.h>
#include <private/autogen/config.h>
#include <private/utils/xml.h>

#include <netloc/uthash.h>
#include <netloc/utarray.h>

static char **hwlocpaths = NULL;
static const netloc_partition_t *crt_part = NULL;

static inline int insert_xml_link(xml_node_ptr links_node,
                                  const netloc_physical_link_t *link)
{
    xml_char *buff = NULL;
    char *strBuff = NULL;
    int strBuffSize = 0;
    xml_node_ptr crt_node;
    /* Add current connexion description */
    buff = xml_char_strdup(link->description);
    crt_node = xml_node_child_new(links_node, NULL, BAD_CAST "link", buff);
    xml_char_free(buff); buff = NULL;
    /* Set srcport */
    strBuffSize = asprintf(&strBuff, "%d", link->ports[0]);
    if (0 < strBuffSize) {
        xml_node_attr_cpy_add(crt_node, BAD_CAST "srcport", strBuff);
        free(strBuff);
    }
    strBuff = NULL;
    /* Set destport */
    strBuffSize = asprintf(&strBuff, "%d", link->ports[1]);
    if (0 < strBuffSize) {
        xml_node_attr_cpy_add(crt_node, BAD_CAST "destport", strBuff);
        free(strBuff);
    }
    strBuff = NULL;
    /* Set speed */
    xml_node_attr_cpy_add(crt_node, BAD_CAST "speed", link->speed);
    /* Set width */
    xml_node_attr_cpy_add(crt_node, BAD_CAST "width", link->width);
    /* Set bandwidth */
    strBuffSize = asprintf(&strBuff, "%f", link->gbits);
    if (0 < strBuffSize) {
        xml_node_attr_cpy_add(crt_node, BAD_CAST "bandwidth", strBuff);
        free(strBuff);
    }
    strBuff = NULL;
    /* Set logical_id */
    strBuffSize = asprintf(&strBuff, "%llu", link->id);
    if (0 < strBuffSize) {
        xml_node_attr_cpy_add(crt_node, BAD_CAST "logical_id", strBuff);
        free(strBuff);
    }
    strBuff = NULL;
    /* Set reverse physical_link->logical_id */
    if (0 < asprintf(&strBuff, "%llu", link->other_way_id)) {
        xml_node_attr_cpy_add(crt_node, BAD_CAST "reverse_logical_id", strBuff);
        free(strBuff);
    }
    strBuff = NULL;
    return NETLOC_SUCCESS;
}

static inline int insert_xml_edge(xml_node_ptr con_node,
                                  const netloc_edge_t *edge)
{
    xml_char *buff = NULL;
    char *strBuff = NULL;
    int strBuffSize = 0;
    xml_node_ptr links_node;
    /* Set bandwidth */
    strBuffSize = asprintf(&strBuff, "%f", edge->total_gbits);
    if (0 < strBuffSize) {
        xml_node_attr_cpy_add(con_node, BAD_CAST "bandwidth", strBuff);
        free(strBuff);
    }
    strBuff = NULL;
    /* Set nblinks */
    unsigned int num_links = utarray_len(&edge->physical_links);
    strBuffSize = asprintf(&strBuff, "%u", num_links);
    if (0 < strBuffSize) {
        xml_node_attr_cpy_add(con_node, BAD_CAST "nblinks", strBuff);
        free(strBuff);
    }
    strBuff = NULL;
    /* Add src */
    buff = xml_char_strdup(edge->node->physical_id);
    assert(buff);
    xml_node_child_new(con_node, NULL, BAD_CAST "src", buff);
    xml_char_free(buff); buff = NULL;
    /* Add dest */
    buff = xml_char_strdup(edge->dest->physical_id);
    assert(buff);
    xml_node_child_new(con_node, NULL, BAD_CAST "dest", buff);
    xml_char_free(buff); buff = NULL;
    if (netloc_edge_is_virtual(edge)) {
        xml_node_ptr subcons_node, subcon_node;
        /* Set virtual="yes" */
        xml_node_attr_add(con_node, BAD_CAST "virtual", BAD_CAST "yes");
        subcons_node = xml_node_child_new(con_node, NULL,
                                          BAD_CAST "subconnexions", NULL);
        /* Set size */
        strBuffSize = asprintf(&strBuff, "%u", edge->nsubedges);
        if (0 < strBuffSize) {
            xml_node_attr_cpy_add(subcons_node, BAD_CAST "size", strBuff);
            free(strBuff);
        }
        strBuff = NULL;
        /* Insert subedges */
        for (unsigned int se = 0; se < edge->nsubedges; ++se) {
            netloc_edge_t *subedge = edge->subnode_edges[se];
            subcon_node = xml_node_child_new(subcons_node, NULL,
                                             BAD_CAST "connexion", NULL);
            insert_xml_edge(subcon_node, subedge);
        }
    } else {
        /* Add links */
        links_node = xml_node_child_new(con_node, NULL, BAD_CAST "links", NULL);
        for (unsigned int l = 0; l < num_links; l++) {
            netloc_physical_link_t *link = *(netloc_physical_link_t **)
                utarray_eltptr(&edge->physical_links, l);
            insert_xml_link(links_node, link);
        }
    }
    return NETLOC_SUCCESS;
}

static inline int
insert_xml_node(xml_node_ptr crt_node, const netloc_node_t *node,
                const netloc_partition_t *part)
{
    /* Sanity check */
    if (NETLOC_NODE_TYPE_HOST == node->type && NULL == part) {
        fprintf(stderr, "Error: node \"%s\" should have a partition with it.\n",
                node->hostname);
        return NETLOC_ERROR;
    }
    xml_char *buff = NULL;
    /* Set mac_addr */
    if (node->physical_id && 0 < strlen(node->physical_id)) {
        xml_node_attr_cpy_add(crt_node, BAD_CAST "mac_addr", node->physical_id);
    }
    /* Set type */
    xml_node_attr_cpy_add(crt_node, BAD_CAST "type",
                          netloc_node_type_encode(node->type));
    /* Set name */
    if (node->hostname && 0 < strlen(node->hostname)) {
        xml_node_attr_cpy_add(crt_node, BAD_CAST "name", node->hostname);
    }
    /* Operations iif node is a host */
    if (NETLOC_NODE_TYPE_HOST == node->type) {
        /* topology hwloc */
        if (node->hwloc_topo_idx) {
            xml_node_attr_cpy_add(crt_node, BAD_CAST "hwloc_file",
                                  hwlocpaths[node->hwloc_topo_idx-1]);
        }
        /* topology index */
        if (NULL != part->topo) {
            netloc_arch_node_t *arch_node = NULL;
            HASH_FIND_STR(part->topo->nodes_by_name, node->hostname, arch_node);
            if (NULL != arch_node
                && 0 < asprintf(&buff, "%d", arch_node->idx_in_topo)) {
                xml_node_attr_cpy_add(crt_node, BAD_CAST "index", buff);
                free(buff);
            }
        } else fprintf(stderr,"WARN: topo->arch is empty\n");
    }
    /* Add description */
    if (node->description && 0 < strlen(node->description)) {
        buff = xml_char_strdup(node->description);
        if (buff) {
            xml_node_child_new(crt_node, NULL, BAD_CAST "description", buff);
            xml_char_free(buff);
        }
    }
    return NETLOC_SUCCESS;
}

static inline int
insert_xml_explicit(xml_node_ptr explicit_node,
                    const netloc_network_explicit_t *expl,
                    const netloc_partition_t *part)
{
    xml_node_ptr nodes_node = NULL, cons_node = NULL, crt_node;
    /* Add nodes */
    nodes_node = xml_node_child_new(explicit_node, NULL,
                                    BAD_CAST "nodes", NULL);
    cons_node = xml_node_child_new(explicit_node, NULL,
                                   BAD_CAST "connexions", NULL);
    netloc_iter_nodelist(&expl->nodes, pnode) {
        crt_node = xml_node_child_new(nodes_node, NULL, BAD_CAST "node", NULL);
        insert_xml_node(crt_node, *pnode, part);
        if (0 < netloc_node_get_num_subnodes(*pnode)) {
            /* VIRTUAL NODE */
            int strBuffSize = 0;
            char *strBuff = NULL;
            netloc_node_t *subnode;
            /* Set virtual */
            xml_node_attr_add(crt_node, BAD_CAST "virtual", BAD_CAST "yes");
            /* Set size */
            strBuffSize =
                asprintf(&strBuff, "%u", netloc_node_get_num_subnodes(*pnode));
            if (0 < strBuffSize) {
                xml_node_attr_cpy_add(crt_node, BAD_CAST "size", strBuff);
                free(strBuff);
            }
            strBuff = NULL;
            /* Add subnodes */
            xml_node_ptr subnodes_node =
                xml_node_child_new(crt_node, NULL, BAD_CAST "subnodes", NULL);
            xml_node_ptr subnode_node;
            netloc_node_iter_subnodes(*pnode, subnode) {
                subnode_node = xml_node_child_new(subnodes_node, NULL,
                                                  BAD_CAST "node", NULL);
                insert_xml_node(subnode_node, subnode, NULL);
            }
        }
        /* Add links and connexions */
        netloc_edge_t *edge, *edge_tmp;
        netloc_node_iter_edges(*pnode, edge, edge_tmp) {
            /* Check edge belongs to this partition... probably */
            /* if( edge_not_in_partition ? ) continue; */
            if (!netloc_edge_is_in_partition(edge, crt_part))
                continue;

            crt_node = xml_node_child_new(cons_node, NULL,
                                          BAD_CAST "connexion", NULL);
            insert_xml_edge(crt_node, edge);
        }
    }
    return NETLOC_SUCCESS;
}

static inline int
insert_extra(xml_node_ptr network_node, const netloc_network_t *network)
{
    unsigned int part_size = 0;
    xml_node_ptr part_node = NULL, crt_node = NULL, nodes_node = NULL,
        cons_node = NULL, explicit_node = NULL;
    part_node = xml_node_new(NULL, BAD_CAST "partition");
    explicit_node = xml_node_child_new(part_node, NULL,
                                       BAD_CAST "explicit", NULL);
    nodes_node = xml_node_child_new(explicit_node, NULL,
                                    BAD_CAST "nodes", NULL);
    cons_node = xml_node_child_new(explicit_node, NULL,
                                   BAD_CAST "connexions",NULL);
    /* Set name */
    xml_node_attr_add(part_node, BAD_CAST "name",
                      BAD_CAST "/extra+structural/");
    /* Add nodes */
    netloc_node_t *node, *node_tmp;
    netloc_network_iter_nodes(network, node, node_tmp) {
        /* node must not be abstracted */
        if (0 == netloc_node_get_num_subnodes(node)
            && NULL != node->virtual_node) {
            continue;
        }
        /* Check if node belongs to no partition */
        if (0 == node->partitions.i) {
            ++part_size;
            crt_node = xml_node_child_new(nodes_node, NULL,
                                          BAD_CAST "node", NULL);
            insert_xml_node(crt_node, node, NULL);
            if (0 < netloc_node_get_num_subnodes(node)) {
                /* VIRTUAL NODE */
                int strBuffSize = 0;
                char *strBuff = NULL;
                netloc_node_t *subnode;
                /* Set virtual */
                xml_node_attr_add(crt_node, BAD_CAST "virtual", BAD_CAST "yes");
                /* Set size */
                strBuffSize = asprintf(&strBuff, "%u",
                                       netloc_node_get_num_subnodes(node));
                if (0 < strBuffSize) {
                    xml_node_attr_cpy_add(crt_node, BAD_CAST "size", strBuff);
                    free(strBuff);
                }
                strBuff = NULL;
                /* Add subnodes */
                xml_node_ptr subnodes_node =
                    xml_node_child_new(crt_node, NULL,
                                       BAD_CAST "subnodes", NULL);
                xml_node_ptr subnode_node;
                netloc_node_iter_subnodes(node, subnode) {
                    subnode_node = xml_node_child_new(subnodes_node, NULL,
                                                      BAD_CAST "node", NULL);
                    insert_xml_node(subnode_node, subnode, NULL);
                }
            }
        }
        /* Add links and connexions */
        netloc_edge_t *edge, *edge_tmp;
        HASH_ITER(hh, node->edges, edge, edge_tmp) {
            /* Check if edge belongs to no partition */
            if (0 < edge->partitions.i)
                continue;
            /* Check if not to subnode */
            if (0 == netloc_node_get_num_subnodes(edge->dest) &&
                NULL != edge->dest->virtual_node)
                continue;
            crt_node = xml_node_child_new(cons_node, NULL,
                                          BAD_CAST "connexion", NULL);
            insert_xml_edge(crt_node, edge);
        }
    }
    /* Add to the xml iif not empty */
    if (!xml_node_has_child(nodes_node)
        && !xml_node_has_child(cons_node)) {
        /* No extra needed: remove it from the output */
        xml_node_free(part_node);
    } else {
        /* Set size */
        char *buff = NULL;
        unsigned int buffSize = 0;
        buffSize = asprintf(&buff, "%u", part_size);
        if (0 < buffSize) {
            xml_node_attr_cpy_add(explicit_node, BAD_CAST "size", buff);
            free(buff);
        }
        buff = NULL;
        xml_node_child_add(network_node, part_node);
    }
    return NETLOC_SUCCESS;
}

static inline int
insert_xml_arch(xml_node_ptr partition_node, const netloc_arch_t *arch)
{
    int buffSize = 0, buffSize2 = 0;
    char *buff = NULL;
    xml_node_ptr topology_node = xml_node_child_new(partition_node, NULL,
                                                    BAD_CAST "topology", NULL);
    /* TODO: do sub architectures with a while loop.*/
    xml_node_ptr topo_node = xml_node_child_new(topology_node, NULL,
                                                BAD_CAST "topo", NULL);
    /* TODO: translate from arch->type to proper type */
    xml_node_attr_add(topo_node, BAD_CAST "type", BAD_CAST "tree");
    /* TODO: check num_levels > 1 */
    NETLOC_int nlevels = arch->arch.node_tree->num_levels;
    buffSize = asprintf(&buff, "%" PRId64, nlevels + 1);
    xml_node_attr_cpy_add(topo_node, BAD_CAST "ndims", buff);
    free(buff); buffSize = 0;

    /* Compute a upper bound for the list size */
    NETLOC_int max_degree = arch->arch.node_tree->degrees[0];
    NETLOC_int max_cost = arch->arch.node_tree->costs[0];
    for (NETLOC_int level = 1; level < nlevels; ++level) {
        if (arch->arch.node_tree->degrees[level] > max_degree)
            max_degree = arch->arch.node_tree->degrees[level];
        if (arch->arch.node_tree->costs[level] > max_cost)
            max_cost = arch->arch.node_tree->costs[level];
    }
    int max_degree_list = 0 < max_degree
        ? max_degree * snprintf(NULL, 0, " %" PRId64, max_degree)
        : 1;
    int max_cost_list = 0 < max_cost
        ? max_cost * snprintf(NULL, 0, " %" PRId64, max_cost)
        : 1;
    buff = (char *) calloc(max_degree_list + max_cost_list + 2, sizeof(char));
    for (NETLOC_int level = 0; level < nlevels; ++level) {
        buffSize += sprintf(buff + buffSize, "%" PRId64 " ",
                               arch->arch.node_tree->degrees[level]);
        buffSize2 += sprintf(buff + max_degree_list + buffSize2 + 1,
                                "%" PRId64 " ",
                                arch->arch.node_tree->costs[level]);
    }
    if (0 < buffSize) {
        buff[buffSize - 1] = '\0'; /* End of "dims" attr */
        buff[max_degree_list + buffSize2] = '\0'; /* End of "costs" attr */
    }
    xml_node_attr_cpy_add(topo_node, BAD_CAST "dims", buff);
    xml_node_attr_cpy_add(topo_node, BAD_CAST "costs",
                          buff + max_degree_list + 1);
    free(buff); buffSize = 0; buffSize2 = 0;
    return NETLOC_SUCCESS;
}

static inline int
insert_xml_partition(xml_node_ptr network_node, const netloc_partition_t *part)
{
    xml_node_ptr part_node = NULL, explicit_node = NULL;
    part_node = xml_node_child_new(network_node, NULL,
                                   BAD_CAST "partition", NULL);
    /* Set crt_part */
    crt_part = part;
    /* Set name */
    if (part->name && 0 < strlen(part->name)) {
        xml_node_attr_cpy_add(part_node, BAD_CAST "name", part->name);
    }
    /* Add arch */
    if (part->topo) {
        insert_xml_arch(part_node, part->topo);
    }
    /* Add explicit */
    if (part->desc) {
        explicit_node = xml_node_child_new(part_node, NULL,
                                           BAD_CAST "explicit", NULL);
        insert_xml_explicit(explicit_node, part->desc, part);
        /* Set explicit size */
        char *strBuff = NULL;
        if (0 < asprintf(&strBuff, "%u", utarray_len(&part->desc->nodes))) {
            xml_node_attr_cpy_add(explicit_node, BAD_CAST "size", strBuff);
            free(strBuff);
        }
    }
    crt_part = NULL;
    return NETLOC_SUCCESS;
}

static inline int
insert_xml_network(xml_node_ptr parent, const netloc_network_t *network)
{
    xml_node_ptr network_node;
    /* Add a network tag */
    network_node = xml_node_child_new(parent, NULL, BAD_CAST "network", NULL);
    /* Set transport */
    xml_node_attr_add(network_node, BAD_CAST "transport", BAD_CAST
                      netloc_network_type_encode(network->transport_type));
    /* Add subnet node if any */
    if (NETLOC_NETWORK_TYPE_INFINIBAND == network->transport_type) {
        char *subnet = ((netloc_network_ib_t *)network)->subnet_id;
        if (subnet && 0 < strlen(subnet)) {
            char *buff = xml_char_strdup(subnet);
            if (buff) {
                xml_node_child_new(network_node, NULL, BAD_CAST "subnet", buff);
                xml_char_free(buff);
            }
        }
    }
    /* Add partitions */
    netloc_partition_t *part, *part_tmp;
    netloc_network_iter_partitions(network, part, part_tmp) {
        insert_xml_partition(network_node, part);
    }
    /* Add partition /extra+structural/ */
    insert_extra(network_node, network);
    return NETLOC_SUCCESS;
}

static inline int
insert_xml_machine(xml_doc_ptr doc, const netloc_machine_t *machine)
{
    /* Create a machine node */
    xml_node_ptr root_node = xml_node_new(NULL, BAD_CAST "machine");
    /* Set version */
    xml_node_attr_add(root_node, BAD_CAST "version",
                      BAD_CAST NETLOC_STR_VERS(NETLOCFILE_VERSION_2_0));
    /* Add hwloc_path node if it exists */
    if (machine->hwloc_dir_path && 0 < strlen(machine->hwloc_dir_path)) {
        char *buff = xml_char_strdup(machine->hwloc_dir_path);
        if (buff) {
            xml_node_child_new(root_node, NULL, BAD_CAST "hwloc_path", buff);
            xml_char_free(buff);
        }
    }
    /* Add network informations */
    hwlocpaths = machine->hwlocpaths;
    insert_xml_network(root_node, machine->network);
    hwlocpaths = NULL;
    /* Set machine node as the document root node */
    xml_doc_set_root_element(doc, root_node);
    return NETLOC_SUCCESS;
}

int netloc_write_xml_file(const netloc_machine_t *machine)
{
    xml_doc_ptr doc = NULL;        /* document pointer */
    int ret = NETLOC_ERROR;

    XML_LIB_CHECK_VERSION;

    /* Creates a new document */
    doc = xml_doc_new(BAD_CAST "1.0");
    ret = insert_xml_machine(doc, machine);

    /*
     * Dumping document to stdio or file
     */
    if (-1 == xml_doc_write(machine->topopath, doc, "UTF-8", 1))
        fprintf(stderr, "Error: Unable to write to file \"%s\"\n", machine->topopath);
    else
        ret = NETLOC_SUCCESS;

    /* Free the document */
    xml_doc_free(doc);

    /*
     * Free the global variables that may
     * have been allocated by the parser.
     */
    xml_parser_cleanup();

    return ret;
}
