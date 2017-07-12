/*
 * Copyright © 2013-2014 University of Wisconsin-La Crosse.
 *                         All rights reserved.
 * Copyright © 2013 Cisco Systems, Inc.  All rights reserved.
 * Copyright © 2015-2017 Inria.  All rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 * See COPYING in top-level directory.
 *
 * $HEADER$
 */

#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <stdlib.h>

#include <private/autogen/config.h>
#include <private/netloc.h>
#include <netloc.h>

netloc_node_t * netloc_node_construct()
{
    netloc_node_t *node = NULL;

    node = (netloc_node_t*)malloc(sizeof(netloc_node_t));
    if (NULL == node) {
        return NULL;
    }
    memset(node, 0, sizeof(netloc_node_t));
    node->physical_id[0]  = '\0';
    node->logical_id      = -1;
    node->type         = NETLOC_NODE_TYPE_INVALID;
    utarray_new(node->physical_links, &ut_ptr_icd);
    node->description  = NULL;
    node->userdata     = NULL;
    node->edges        = NULL;
    utarray_new(node->partitions, &ut_ptr_icd);
    node->paths        = NULL;
    node->hostname     = NULL;
    node->nsubnodes    = 0;
    node->subnodes     = NULL;

    return node;
}

int netloc_node_destruct(netloc_node_t * node)
{
    utarray_free(node->physical_links);
    utarray_free(node->partitions);

    /* Description */
    if (node->description)
        free(node->description);

    /* Edges */
    netloc_edge_t *edge, *edge_tmp;
    HASH_ITER(hh, node->edges, edge, edge_tmp) {
        HASH_DEL(node->edges, edge);  /* delete; edge advances to next */
        netloc_edge_destruct(edge);
    }

    /* Subnodes */
    /* WARNING: The subnodes are to be removed from the hashtable PRIOR TO THIS CALL */
    if (0 < node->nsubnodes)
        free(node->subnodes);

    /* Paths */
    netloc_path_t *path, *path_tmp;
    HASH_ITER(hh, node->paths, path, path_tmp) {
        HASH_DEL(node->paths, path);  /* delete; path advances to next */
        netloc_path_destruct(path);
    }

    /* Hostname */
    if (node->hostname)
        free(node->hostname);

    /* hwlocTopo: nothing to do beacause the pointer is stored also in the topology */

    free(node);

    return NETLOC_SUCCESS;
}

char *netloc_node_pretty_print(netloc_node_t* node)
{
    char * str = NULL;

    asprintf(&str, " [%23s]/[%d] -- %s (%d links)",
             node->physical_id,
             node->logical_id,
             node->description,
             utarray_len(node->physical_links));

    return str;
}

#if defined(HWLOC_HAVE_LIBXML2)

/** Read with libxml */

netloc_node_t * netloc_node_xml_load(xmlNode *it_node, char *hwlocpath,
                                     netloc_hwloc_topology_t **hwloc_topos) {
    xmlNode *tmp = NULL, *crt_node;
    xmlChar *buff = NULL;
    char *strBuff = NULL;
    size_t strBuffSize, buffSize;
    netloc_node_t *node = netloc_node_construct();

    /* read hostname */
    buff = xmlGetProp(it_node, BAD_CAST "name");
    if (buff && 0 < strlen((char *)buff)) {
        strBuff = strdup((char *)buff);
    } else {
        strBuff = strdup("");
    }
    xmlFree(buff); buff = NULL;
    /* set physical_id */
    buff = xmlGetProp(it_node, BAD_CAST "mac_addr");
    if (buff && 0 < strlen((char *)buff)) {
        strncpy(node->physical_id, (char *)buff, 20);
        /* Should be shorter than 20 */
        node->physical_id[19] = '\0'; /* If a problem occurs */
        xmlFree(buff); buff = NULL;
    } else {
        fprintf(stderr, "Error: node \"%s\" has no physical address and "
                "is not added to the topology.\n",
                node->hostname && strlen(node->hostname) ? node->hostname : "(no name)");
        xmlFree(buff); buff = NULL;
        netloc_node_destruct(node);
        return NULL;
    }
    /* set hostname */
    node->hostname = strBuff;
    /* set type (i.e., host or switch) */
    buff = xmlGetProp(it_node, BAD_CAST "type");
    if (buff)
        node->type = netloc_node_type_decode((char *)buff);
    xmlFree(buff); buff = NULL;
    /* Set description */
    if (it_node->children
        && ((XML_TEXT_NODE == it_node->children->type && (tmp = it_node->children->next))
            || (tmp = it_node->children))
        && XML_ELEMENT_NODE == tmp->type && 0 == strcmp("description", (char *)tmp->name)
        && tmp->children && tmp->children->content) {
        node->description = strdup((char *)tmp->children->content);
    }
    /* set hwloc topology field iif node is a host */
    if (NETLOC_NODE_TYPE_HOST == node->type
        && (buff = xmlGetProp(it_node, BAD_CAST "hwloc_file"))
        && 0 < (buffSize = strlen((char *)buff))) {
        netloc_hwloc_topology_t *hwloc_topo = NULL;
        char *refname;
        strBuffSize = asprintf(&strBuff, "%s/%s", hwlocpath, (char *)buff);
        if (5 > strBuffSize) { /* bad return from asprintf() */
            fprintf(stderr, "WARN: Invalid topology file \"%s/%s\", or "
                    "memory exhaustion\n", hwlocpath, (char *)buff);
        } else if (9 < strBuffSize && 0 == strcmp(".diff.xml", &strBuff[strBuffSize-9])) {
            /* hwloc_file point to a diff file */
            hwloc_topology_diff_t diff;
            if (0 <= hwloc_topology_diff_load_xml(strBuff, &diff, &refname) && refname) {
                /* Load diff file to check on refname */
                hwloc_topology_diff_destroy(diff);
                free(strBuff);
                strBuffSize = asprintf(&strBuff, "%s/%s", hwlocpath, refname);
                free(refname);
            } else {
                fprintf(stderr, "WARN: no refname for topology file \"%s/%s\"\n",
                        hwlocpath, (char *)buff);
            }
        }
        HASH_FIND(hh, *hwloc_topos, strBuff, (unsigned)strBuffSize, hwloc_topo);
        if (!hwloc_topo) { /* The topology cannot be retrieved in the hashtable */
            size_t hwloc_topo_idx = (size_t)HASH_COUNT(*hwloc_topos);
            hwloc_topo = malloc(sizeof(netloc_hwloc_topology_t));
            hwloc_topo->path = strdup(strBuff);
            hwloc_topo->hwloc_topo_idx = hwloc_topo_idx;
            node->hwloc_topo_idx = hwloc_topo_idx;
            ++hwloc_topo_idx;
            HASH_ADD_STR(*hwloc_topos, path, hwloc_topo);
        } else {
            node->hwloc_topo_idx = hwloc_topo->hwloc_topo_idx;
        }
        free(strBuff); strBuff = NULL;
        xmlFree(buff); buff = NULL;
    } else if (NETLOC_NODE_TYPE_SWITCH == node->type
               && (buff = xmlGetProp(it_node, BAD_CAST "virtual"))
               && 0 < (buffSize = strlen((char *)buff))
               && 0 == strncmp("yes", (char *)buff, 3)) {
        /* Virtual node */
        xmlFree(buff); buff = NULL;
        buff = xmlGetProp(it_node, BAD_CAST "size");
        if (1 != sscanf((const char *)buff, "%u", &node->nsubnodes)) {
            fprintf(stderr, "WARN: Cannot read how many subnodes are included into the "
                    "virtual node \"%s\"\n", node->physical_id);
        }
        xmlFree(buff); buff = NULL;
        /* Add subnodes */
        if (!node->description) {
            tmp = (it_node->children && XML_ELEMENT_NODE != it_node->children->type
                   ? it_node->children->next : it_node->children);
            node->description = strndup(node->physical_id, 20);
        } else {
            tmp = tmp->next && XML_ELEMENT_NODE != tmp->next->type ? tmp->next->next : tmp->next;
        }
        if (0 != strcmp("subnodes", (char *)tmp->name)) {
            fprintf(stderr, "WARN: virtual node \"%s\" is empty, and thus, ignored\n",
                    node->physical_id);
            netloc_node_destruct(node);
            return NULL;
        }
        /* Allocate subnodes array */
        node->subnodes = malloc(sizeof(netloc_node_t *[node->nsubnodes]));
        if (!node->subnodes) {
            fprintf(stderr, "WARN: unable to allocate the memory for virtual node \"%s\" "
                    "subnodes. This node is thus ignored\n", node->physical_id);
            netloc_node_destruct(node);
            return NULL;
        }
        int subnode_id = 0;
        for (xmlNode *it_subnode = (tmp->children
                                    && XML_ELEMENT_NODE != tmp->children->type
                                    ? tmp->children->next : tmp->children);
             it_subnode;
             it_subnode = it_subnode->next && XML_ELEMENT_NODE != it_subnode->next->type
                 ? it_subnode->next->next : it_subnode->next) {
            netloc_node_t *subnode = netloc_node_xml_load(it_subnode, hwlocpath, hwloc_topos);
            subnode->virtual_node        = node;
            node->subnodes[subnode_id++] = subnode;
        }
        /* Check we found all the subnodes */
        if (subnode_id != node->nsubnodes) {
            fprintf(stderr, "WARN: expecting %u subnodes, but %u found\n",
                    node->nsubnodes, subnode_id);
        }
    }

    return node;
}

#else
netloc_edge_t *
netloc_node_xml_load(void *it_node                         __netloc_attribute_unused,
                     char *hwlocpath                       __netloc_attribute_unused,
                     netloc_hwloc_topology_t **hwloc_topos __netloc_attribute_unused)
{
    /* nothing to do here */
    return NULL;
}
#endif /* defined(HWLOC_HAVE_LIBXML2) */
