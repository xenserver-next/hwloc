/*
 * Copyright © 2017      Inria.  All rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 * See COPYING in top-level directory.
 *
 * $HEADER$
 */

/* 
 * This file provides general function and global variables to
 * generate XML topology files.
 */

#define _GNU_SOURCE         /* See feature_test_macros(7) */

#include <private/netloc.h>
#include <netloc.h>

#include <private/utils/xml.h>
#include <netloc/uthash.h>
#include <netloc/utarray.h>
#include <dirent.h>
#include <libgen.h>

node_t *nodes = NULL;
UT_array *partitions = NULL;
route_source_t *routes = NULL;
path_source_t *paths = NULL;

static int get_virtual_id(char *id)
{
    static unsigned int virtual_id = 0;
    sprintf(id, "virtual%012u", ++virtual_id);
    return 0;
}

static inline int node_is_virtual(node_t *node)
{
    return (NULL != node->subnodes);
}

static inline int edge_is_virtual(edge_t *edge)
{
    return (NULL != edge->subedges);
}

static int sort_by_dest(edge_t *a, edge_t *b)
{
    return strncmp(a->dest, b->dest, MAX_STR);
}

static inline void edge_merge_into(node_t *virtual_node, edge_t *virtual_edge,
                                   node_t *node, edge_t *edge)
{
    unsigned int npartitions = utarray_len(partitions);
    /* Change corresponding edge in reverse */
    node_t *dest_node;
    edge_t *dest_edge, *reverse_virtual_edge;
    HASH_FIND_STR(nodes, edge->dest, dest_node);
    assert(dest_node);
    HASH_FIND_STR(dest_node->edges, node->physical_id, dest_edge);
    assert(dest_edge);
    /* Check if the virtual_reverse_node is already defined */
    HASH_FIND_STR(dest_node->edges, virtual_node->physical_id, reverse_virtual_edge);
    if (!reverse_virtual_edge) { /* No previous reverse_virtual_edge */
        reverse_virtual_edge = (edge_t *)malloc(sizeof(edge_t));
        strncpy(reverse_virtual_edge->dest, virtual_node->physical_id, MAX_STR);
        reverse_virtual_edge->total_gbits = 0;
        reverse_virtual_edge->reverse_edge = virtual_edge;
        reverse_virtual_edge->partitions = calloc(npartitions, sizeof(int));
        utarray_new(reverse_virtual_edge->physical_link_idx, &ut_int_icd);
        utarray_new(reverse_virtual_edge->subedges, &ut_ptr_icd);
        HASH_ADD_STR(dest_node->edges, dest, reverse_virtual_edge);
        virtual_edge->reverse_edge = reverse_virtual_edge;
    }
    /* Merge into already defined reverse_virtual_edge */
    utarray_concat(reverse_virtual_edge->physical_link_idx, dest_edge->physical_link_idx);
    reverse_virtual_edge->total_gbits += dest_edge->total_gbits;
    HASH_DEL(dest_node->edges, dest_edge);
    utarray_push_back(reverse_virtual_edge->subedges, &dest_edge);
    /* Merge into virtual_edge */
    virtual_edge->total_gbits += edge->total_gbits;
    HASH_DEL(node->edges, edge);
    utarray_push_back(virtual_edge->subedges, &edge);
    /* Set partitions */
    for (unsigned int p = 0; p < npartitions; ++p) {
        virtual_edge->partitions[p] |= edge->partitions[p];
        reverse_virtual_edge->partitions[p] |= dest_edge->partitions[p];
    }
    /* Add offset to each physical_link index */
    unsigned int offset =
        utarray_len(virtual_node->physical_links) - utarray_len(node->physical_links);
    unsigned int num_links = utarray_len(edge->physical_link_idx);
    for (unsigned int l = 0; l < num_links; l++) {
        unsigned int link_idx = *(unsigned int *)
            utarray_eltptr(edge->physical_link_idx, l);
        link_idx += offset;
        utarray_push_back(virtual_edge->physical_link_idx, &link_idx);
#ifdef NETLOC_DEBUG
        physical_link_t *link = (physical_link_t *)
            utarray_eltptr(virtual_node->physical_links, link_idx);
        assert(0 != *(int*)link);
#endif /* NETLOC_DEBUG */
    }
}

static int find_similar_nodes(void)
{
    int ret;
    /* Build edge lists by node */
    int num_nodes = HASH_COUNT(nodes);
    node_t **switch_nodes = (node_t **)malloc(sizeof(node_t *[num_nodes]));
    node_t ***edgedest_by_node = (node_t ***)malloc(sizeof(node_t **[num_nodes]));
    unsigned int *num_edges_by_node = (unsigned int *)malloc(sizeof(unsigned int [num_nodes]));
    node_t *node, *node_tmp;
    int idx = -1;
    HASH_ITER(hh, nodes, node, node_tmp) {
        idx++;
        if (NETLOC_NODE_TYPE_HOST == node->type) {
            switch_nodes[idx] = NULL;
            num_edges_by_node[idx] = 0;
            edgedest_by_node[idx] = NULL;
            continue;
        }
        unsigned int num_edges = HASH_COUNT(node->edges);
        switch_nodes[idx] = node;
        num_edges_by_node[idx] = num_edges;
        edgedest_by_node[idx] = (node_t **)malloc(sizeof(node_t *[num_edges]));
        edge_t *edge, *edge_tmp;
        int edge_idx = 0;
        HASH_SORT(node->edges, sort_by_dest);
        netloc_node_iter_edges(node, edge, edge_tmp) {
            HASH_FIND_STR(nodes, edge->dest, edgedest_by_node[idx][edge_idx]);
            assert(edgedest_by_node[idx][edge_idx]);
            edge_idx++;
        }
    }

    /* We compare the edge lists to find similar nodes */
    for (int nodeIdx = 0; nodeIdx < num_nodes - 1; nodeIdx++) {
        node_t *node1 = switch_nodes[nodeIdx];
        node_t *virtual_node = NULL;

        for (int nodeCmpIdx = nodeIdx + 1; node1 && nodeCmpIdx < num_nodes; nodeCmpIdx++) {
            node_t *node2 = switch_nodes[nodeCmpIdx];

            int equal = node2 && num_edges_by_node[nodeIdx] == num_edges_by_node[nodeCmpIdx];

            /* Check if the destinations are the same */
            for (int i = 0; equal && i < num_edges_by_node[nodeIdx]; i++) {
                if (edgedest_by_node[nodeIdx][i] != edgedest_by_node[nodeCmpIdx][i])
                    equal = 0;
            }

            /* If we have similar nodes */
            if (equal) {
                /* We create a new virtual node to contain all of them */
                if (!virtual_node) {
                    /* virtual_node = netloc_node_construct(); */
                    virtual_node = (node_t *)calloc(1, sizeof(node_t));
                    get_virtual_id(virtual_node->physical_id);
                    virtual_node->description = strdup(virtual_node->physical_id);

                    unsigned int npartitions = utarray_len(partitions);
                    virtual_node->partitions = calloc(npartitions, sizeof(int));
                    for (unsigned int p = 0; p < npartitions; ++p)
                        virtual_node->partitions[p] |= node1->partitions[p];

                    virtual_node->type = node1->type;
                    virtual_node->subnodes = NULL;
                    utarray_new(virtual_node->physical_links, &physical_link_icd);

                    /* add physical_links */
                    utarray_concat(virtual_node->physical_links, node1->physical_links);

                    HASH_DEL(nodes, node1);
                    HASH_ADD_STR(virtual_node->subnodes, physical_id, node1);
                    HASH_ADD_STR(nodes, physical_id, virtual_node);

                    /* Initialize destination for virtual edge */
                    for (int i = 0; i < num_edges_by_node[nodeIdx]; i++) {
                        edge_t *edge1, *virtual_edge = (edge_t *)malloc(sizeof(edge_t));
                        strncpy(virtual_edge->dest, edgedest_by_node[nodeIdx][i]->physical_id,
                                MAX_STR);
                        virtual_edge->partitions = calloc(npartitions, sizeof(int));
                        virtual_edge->total_gbits = 0;
                        utarray_new(virtual_edge->subedges, &ut_ptr_icd);

                        utarray_new(virtual_edge->physical_link_idx, &ut_int_icd);
                        HASH_FIND_STR(node1->edges, virtual_edge->dest, edge1);
                        assert(edge1);
                        edge_merge_into(virtual_node, virtual_edge, node1, edge1);
                        HASH_ADD_STR(virtual_node->edges, dest, virtual_edge);
                    }
                }

                /* add physical_links */
                utarray_concat(virtual_node->physical_links, node2->physical_links);

                for (int i = 0; i < num_edges_by_node[nodeCmpIdx]; i++) {
                    edge_t *edge2, *virtual_edge;
                    HASH_FIND_STR(virtual_node->edges,
                                  edgedest_by_node[nodeCmpIdx][i]->physical_id, virtual_edge);
                    assert(virtual_edge);
                    HASH_FIND_STR(node2->edges, virtual_edge->dest, edge2);
                    assert(edge2);
                    edge_merge_into(virtual_node, virtual_edge, node2, edge2);
                }

                HASH_DEL(nodes, node2);
                HASH_ADD_STR(virtual_node->subnodes, physical_id, node2);
                switch_nodes[nodeCmpIdx] = NULL;

                    /* // TODO paths */

                    /* /\* Set edges *\/ */
                    /* netloc_edge_t *edge1, *edge_tmp1; */
                    /* netloc_node_iter_edges(node1, edge1, edge_tmp1) { */
                    /*     netloc_edge_t *virtual_edge = netloc_edge_construct(); */
                    /*     if (!first_virtual_edge) */
                    /*         first_virtual_edge = virtual_edge; */
                    /*     virtual_edge->node = virtual_node; */
                    /*     virtual_edge->dest = edge1->dest; */
                    /*     ret = edge_merge_into(virtual_edge, edge1, 0); */
                    /*     if (ret != NETLOC_SUCCESS) { */
                    /*         netloc_edge_destruct(virtual_edge); */
                    /*         goto ERROR; */
                    /*     } */
                    /*     HASH_ADD_PTR(virtual_node->edges, dest, virtual_edge); */

                    /*     /\* Change the reverse edge of the neighbours (reverse nodes) *\/ */
                    /*     netloc_node_t *reverse_node = edge1->dest; */
                    /*     netloc_edge_t *reverse_edge = edge1->other_way; */

                    /*     netloc_edge_t *reverse_virtual_edge = */
                    /*         netloc_edge_construct(); */
                    /*     reverse_virtual_edge->dest = virtual_node; */
                    /*     reverse_virtual_edge->node = reverse_node; */
                    /*     reverse_virtual_edge->other_way = virtual_edge; */
                    /*     virtual_edge->other_way = reverse_virtual_edge; */
                    /*     HASH_ADD_PTR(reverse_node->edges, dest, reverse_virtual_edge); */
                    /*     ret = edge_merge_into(reverse_virtual_edge, reverse_edge, 1); */
                    /*     if (ret != NETLOC_SUCCESS) { */
                    /*         goto ERROR; */
                    /*     } */
                    /*     HASH_DEL(reverse_node->edges, reverse_edge); */
                    /* } */

                    /* /\* We remove the node from the list of nodes *\/ */
                    /* HASH_DEL(topology->nodes, node1); */
                    /* HASH_ADD_STR(topology->nodes, physical_id, virtual_node); */
                    /* printf("First node found: %s (%s)\n", node1->description, node1->physical_id); */
            }

                /* utarray_concat(virtual_node->physical_links, node2->physical_links); */
                /* utarray_push_back(virtual_node->subnodes, &node2); */
                /* utarray_concat(virtual_node->partitions, node2->partitions); */

                /* /\* Set edges *\/ */
                /* netloc_edge_t *edge2, *edge_tmp2; */
                /* netloc_edge_t *virtual_edge = first_virtual_edge; */
                /* netloc_node_iter_edges(node2, edge2, edge_tmp2) { */
                /*     /\* Merge the edges from the physical node into the virtual node *\/ */
                /*     ret = edge_merge_into(virtual_edge, edge2, 0); */
                /*     if (ret != NETLOC_SUCCESS) { */
                /*         goto ERROR; */
                /*     } */

                /*     /\* Change the reverse edge of the neighbours (reverse nodes) *\/ */
                /*     netloc_node_t *reverse_node = edge2->dest; */
                /*     netloc_edge_t *reverse_edge = edge2->other_way; */

                /*     netloc_edge_t *reverse_virtual_edge; */
                /*     HASH_FIND_PTR(reverse_node->edges, &virtual_node, */
                /*             reverse_virtual_edge); */
                /*     ret = edge_merge_into(reverse_virtual_edge, reverse_edge, 1); */
                /*     if (ret != NETLOC_SUCCESS) { */
                /*         goto ERROR; */
                /*     } */
                /*     HASH_DEL(reverse_node->edges, reverse_edge); */

                /*     /\* Get the next edge *\/ */
                /*     virtual_edge = virtual_edge->hh.next; */
                /* } */

                /* /\* We remove the node from the list of nodes *\/ */
                /* HASH_DEL(topology->nodes, node2); */
                /* printf("\t node found: %s (%s)\n", node2->description, node2->physical_id); */

                /* nodes[idx2] = NULL; */
            /* } */
        }
    }

    ret = NETLOC_SUCCESS;
ERROR:
    free(switch_nodes);
    for (int idx = 0; idx < num_nodes; idx++) {
        if (edgedest_by_node[idx])
            free(edgedest_by_node[idx]);
    }
    free(edgedest_by_node);
    free(num_edges_by_node);
    return ret;
}

static inline void set_reverse_edges()
{
    node_t *node, *node_tmp, *dest_node;
    edge_t *edge, *edge_tmp, *reverse_edge;
    HASH_ITER(hh, nodes, node, node_tmp) {
        HASH_ITER(hh, node->edges, edge, edge_tmp) {
            HASH_FIND_STR(nodes, edge->dest, dest_node);
            assert(dest_node);
            HASH_FIND_STR(dest_node->edges, node->physical_id, edge->reverse_edge); 
            assert(edge->reverse_edge);
        }
    }
}

#ifdef HWLOC_HAVE_LIBXML2

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

static inline void set_xml_prop(xmlNodePtr pnode, const xmlChar *name, const char *value)
{
    xmlChar *buff = xmlCharStrdup(value);
    if(buff)
        xmlNewProp(pnode, name, buff);
    xmlFree(buff);
}

static inline void insert_xml_link(xmlNodePtr links_node, physical_link_t *link)
{
    xmlChar *buff = NULL;
    char *strBuff = NULL;
    int strBuffSize = 0;
    xmlNodePtr crt_node;
    /* Add current connexion description */
    buff = xmlCharStrdup(link->description);
    crt_node = xmlNewChild(links_node, NULL, BAD_CAST "link", buff);
    xmlFree(buff); buff = NULL;
    /* Set srcport */
    strBuffSize = asprintf(&strBuff, "%d", link->ports[0]);
    if (0 < strBuffSize)
        set_xml_prop(crt_node, BAD_CAST "srcport", strBuff);
    free(strBuff); strBuff = NULL;
    /* Set destport */
    strBuffSize = asprintf(&strBuff, "%d", link->ports[1]);
    if (0 < strBuffSize)
        set_xml_prop(crt_node, BAD_CAST "destport", strBuff);
    free(strBuff); strBuff = NULL;
    /* Set speed */
    set_xml_prop(crt_node, BAD_CAST "speed", link->speed);
    /* Set width */
    set_xml_prop(crt_node, BAD_CAST "width", link->width);
    /* Set bandwidth */
    strBuffSize = asprintf(&strBuff, "%f", link->gbits);
    if (0 < strBuffSize)
        set_xml_prop(crt_node, BAD_CAST "bandwidth", strBuff);
    free(strBuff); strBuff = NULL;
    /* Set logical_id */
    strBuffSize = asprintf(&strBuff, "%llu", link->int_id);
    if (0 < strBuffSize)
        set_xml_prop(crt_node, BAD_CAST "logical_id", strBuff);
    free(strBuff); strBuff = NULL;
    /* Set reverse physical_link->logical_id */
    if (link->other_link && 0 < asprintf(&strBuff, "%llu", link->other_link->int_id))
        set_xml_prop(crt_node, BAD_CAST "reverse_logical_id", strBuff);
    free(strBuff); strBuff = NULL;
}

static inline void insert_xml_edge(xmlNodePtr con_node, edge_t *edge, node_t *node)
{
    xmlChar *buff = NULL;
    char *strBuff = NULL;
    int strBuffSize = 0;
    xmlNodePtr crt_node, links_node;
    /* Set bandwidth */
    strBuffSize = asprintf(&strBuff, "%f", edge->total_gbits);
    if (0 < strBuffSize)
        set_xml_prop(con_node, BAD_CAST "bandwidth", strBuff);
    free(strBuff); strBuff = NULL;
    /* Set nblinks */
    unsigned int num_links = utarray_len(edge->physical_link_idx);
    strBuffSize = asprintf(&strBuff, "%u", num_links);
    if (0 < strBuffSize)
        set_xml_prop(con_node, BAD_CAST "nblinks", strBuff);
    free(strBuff); strBuff = NULL;
    /* Add src */
    buff = xmlCharStrdup(node->physical_id);
    assert(buff);
    crt_node = xmlNewChild(con_node, NULL, BAD_CAST "src", buff);
    xmlFree(buff); buff = NULL;
    /* Add dest */
    buff = xmlCharStrdup(edge->dest);
    assert(buff);
    crt_node = xmlNewChild(con_node, NULL, BAD_CAST "dest", buff);
    xmlFree(buff); buff = NULL;
    if (edge_is_virtual(edge)) {
        xmlNodePtr subcons_node, subcon_node;
        /* Set virtual="yes" */
        xmlNewProp(con_node, BAD_CAST "virtual", BAD_CAST "yes");
        subcons_node = xmlNewChild(con_node, NULL, BAD_CAST "subconnexions", NULL);
        /* Set size */
        unsigned int num_subedges = utarray_len(edge->subedges);
        strBuffSize = asprintf(&strBuff, "%u", num_subedges);
        if (0 < strBuffSize)
            set_xml_prop(subcons_node, BAD_CAST "size", strBuff);
        free(strBuff); strBuff = NULL;
        /* Insert subedges */
        node_t *real_node = NULL;
        for (unsigned int se = 0; se < num_subedges; ++se) {
            edge_t *subedge = *(edge_t **)utarray_eltptr(edge->subedges, se);
            if (node_is_virtual(node))
                HASH_FIND_STR(node->subnodes, subedge->reverse_edge->dest, real_node);
            else
                HASH_FIND_STR(nodes, subedge->reverse_edge->dest, real_node);
            assert(real_node);
            subcon_node = xmlNewChild(subcons_node, NULL, BAD_CAST "connexion", NULL);
            insert_xml_edge(subcon_node, subedge, real_node);
        }
    } else {
        /* Add links */
        links_node = xmlNewChild(con_node, NULL, BAD_CAST "links", NULL);
        for (unsigned int l = 0; l < num_links; l++) {
            unsigned int link_idx = *(unsigned int *)
                utarray_eltptr(edge->physical_link_idx, l);
            physical_link_t *link = (physical_link_t *)
                utarray_eltptr(node->physical_links, link_idx);
            insert_xml_link(links_node, link);
        }
    }
}

static inline void insert_xml_node(xmlNodePtr crt_node, node_t *node, char *hwloc_path)
{
    xmlChar *buff = NULL;
    char *strBuff = NULL;
    int strBuffSize = 0;
    /* Set mac_addr */
    if (node->physical_id && 0 < node->physical_id) {
        set_xml_prop(crt_node, BAD_CAST "mac_addr", node->physical_id);
    }
    /* Set type */
    set_xml_prop(crt_node, BAD_CAST "type", netloc_node_type_encode(node->type));
    if (node->hostname && 0 < strlen(node->hostname)) {
        /* Set name */
        set_xml_prop(crt_node, BAD_CAST "name", node->hostname);
        /* Set hwloc_path iif node is a host */
        if (NETLOC_NODE_TYPE_HOST == node->type) {
            FILE *fxml;
            strBuffSize = asprintf(&strBuff, "%s/%s.diff.xml",
                                   hwloc_path, node->hostname);
            if (!(fxml = fopen(strBuff, "r"))) {
                strBuffSize -= 8;
                strcpy(&strBuff[strBuffSize], "xml");
                if (!(fxml = fopen(strBuff, "r"))) {
                    fprintf(stderr, "Hwloc file absent: %s\n", strBuff);
                    strBuffSize = 0;
                } else
                    fclose(fxml);
            } else
                fclose(fxml);
            if (0 < strBuffSize) {
                set_xml_prop(crt_node, BAD_CAST "hwloc_file", 1+strrchr(strBuff, '/'));
            }
            free(strBuff); strBuff = NULL;
        }
    } else if (NETLOC_NODE_TYPE_HOST == node->type) {
        fprintf(stderr, "WARN: Host node with address %s has no hostname\n",
                node->physical_id);
    }
    /* Add description */
    if (node->description && 0 < strlen(node->description)) {
        buff = xmlCharStrdup(node->description);
        if(buff) {
            xmlNewChild(crt_node, NULL, BAD_CAST "description", buff);
        }
        xmlFree(buff);
    }
}

static int node_belongs_to_a_partition(node_t *node) {
    for(int p = 0; node->partitions && p < utarray_len(partitions); ++p)
        if (node->partitions[p]) return 1;
    return 0;
}

static int edge_belongs_to_a_partition(edge_t *edge) {
    for(int p = 0; edge->partitions && p < utarray_len(partitions); ++p)
        if (edge->partitions[p]) return 1;
    return 0;
}

static inline int insert_extra(xmlNodePtr root_node, char *full_hwloc_path)
{
    char *strBuff;
    unsigned int part_size = 0, strBuffSize;
    xmlNodePtr part_node = NULL, crt_node = NULL, nodes_node = NULL, cons_node = NULL;
    part_node = xmlNewChild(root_node, NULL, BAD_CAST "partition", NULL);
    nodes_node = xmlNewChild(part_node, NULL, BAD_CAST "nodes", NULL);
    cons_node = xmlNewChild(part_node, NULL, BAD_CAST "connexions", NULL);
    /* Set name */
    xmlNewProp(part_node, BAD_CAST "name", BAD_CAST "/extra+structural/");
    /* Add nodes */
    node_t *node, *node_tmp;
    HASH_ITER(hh, nodes, node, node_tmp) {
        /* Check if node belongs to no partition */
        if (!node_belongs_to_a_partition(node)) {
            ++part_size;
            crt_node = xmlNewChild(nodes_node, NULL, BAD_CAST "node", NULL);
            insert_xml_node(crt_node, node, full_hwloc_path);
            if (node->subnodes) {
                /* VIRTUAL NODE */
                node_t *subnode, *subnode_tmp;
                /* Set virtual */
                xmlNewProp(crt_node, BAD_CAST "virtual", BAD_CAST "yes");
                /* Set size */
                strBuffSize = asprintf(&strBuff, "%u", HASH_COUNT(node->subnodes));
                if (0 < strBuffSize) {
                    set_xml_prop(crt_node, BAD_CAST "size", strBuff);
                }
                free(strBuff); strBuff = NULL;
                /* Add subnodes */
                xmlNodePtr subnodes_node = xmlNewChild(crt_node, NULL, BAD_CAST "subnodes", NULL);
                xmlNodePtr subnode_node;
                HASH_ITER(hh, node->subnodes, subnode, subnode_tmp) {
                    subnode_node = xmlNewChild(subnodes_node, NULL, BAD_CAST "node", NULL);
                    insert_xml_node(subnode_node, subnode, full_hwloc_path);
                }
            }
        }
        /* Add links and connexions */
        edge_t *edge, *edge_tmp;
        HASH_ITER(hh, node->edges, edge, edge_tmp) {
            /* Check if edge belongs to no partition */
            if (edge_belongs_to_a_partition(edge))
                continue;
            crt_node = xmlNewChild(cons_node, NULL, BAD_CAST "connexion", NULL);
            insert_xml_edge(crt_node, edge, node);
        }
    }
    /* Set size */
    strBuffSize = asprintf(&strBuff, "%u", part_size);
    if (0 < strBuffSize)
        set_xml_prop(part_node, BAD_CAST "size", strBuff);
    free(strBuff); strBuff = NULL;
    return NETLOC_SUCCESS;
}

int netloc_write_into_xml_file(const char *subnet, const char *path, const char *hwlocpath,
                               const netloc_network_type_t transportType)
{
    xmlDocPtr doc = NULL;        /* document pointer */
    xmlNodePtr root_node = NULL; /* root pointer */
    xmlChar *buff = NULL;
    char *strBuff = NULL, *full_hwloc_path = NULL;
    int strBuffSize = 0;

    set_reverse_edges();
    find_similar_nodes();

    LIBXML_TEST_VERSION;

    /*
     * Add topology definition tag
     */
    /* Creates a new document, a node and set it as a root node. */
    doc = xmlNewDoc(BAD_CAST "1.0");
    root_node = xmlNewNode(NULL, BAD_CAST "topology");
    xmlDocSetRootElement(doc, root_node);
    /* Set version */
    xmlNewProp(root_node, BAD_CAST "version", BAD_CAST NETLOC_STR_VERS(NETLOCFILE_VERSION_2_0));
    /* Set transport */
    set_xml_prop(root_node, BAD_CAST "transport", netloc_network_type_encode(transportType));
    /* Add subnet node */
    if (subnet && 0 < strlen(subnet)) {
        buff = xmlCharStrdup(subnet);
        if (buff)
            xmlNewChild(root_node, NULL, BAD_CAST "subnet", buff);
        xmlFree(buff); buff = NULL;
    }
    /* Add hwloc_path node */
    DIR* dir;
    if (hwlocpath && 0 < strlen(hwlocpath)) {
        if ('/' != hwlocpath[0]) {
            strBuff = strdup(path);
            asprintf(&full_hwloc_path, "%s/%s", dirname(strBuff), hwlocpath);
            free(strBuff); strBuff = NULL;
        } else {
            full_hwloc_path = strdup(hwlocpath);
        }
    }
    if (full_hwloc_path && 0 < strlen(full_hwloc_path) && (dir = opendir(full_hwloc_path))) {
        buff = xmlCharStrdup(hwlocpath);
        if (buff)
            xmlNewChild(root_node, NULL, BAD_CAST "hwloc_path", buff);
        xmlFree(buff); buff = NULL;
        closedir(dir);
    }
    /* Add partitions */
    int npartitions = utarray_len(partitions);
    char **ppartition = (char **)utarray_front(partitions);
    for (int p = 0; p < npartitions; ++p) {
        unsigned int part_size = 0;
        xmlNodePtr part_node = NULL, crt_node = NULL, nodes_node = NULL, cons_node = NULL;
        part_node = xmlNewChild(root_node, NULL, BAD_CAST "partition", NULL);
        /* Set name */
        if (ppartition && 0 < strlen(*ppartition)) {
            set_xml_prop(part_node, BAD_CAST "name", *ppartition);
        }
        /* Add nodes */
        nodes_node = xmlNewChild(part_node, NULL, BAD_CAST "nodes", NULL);
        cons_node = xmlNewChild(part_node, NULL, BAD_CAST "connexions", NULL);
        node_t *node, *node_tmp;
        HASH_ITER(hh, nodes, node, node_tmp) {
            /* Check node belongs to the current partition */
            if (!node->partitions || !node->partitions[p])
                continue;
            else
                ++part_size;
            crt_node = xmlNewChild(nodes_node, NULL, BAD_CAST "node", NULL);
            insert_xml_node(crt_node, node, full_hwloc_path);
            if (node->subnodes) {
                /* VIRTUAL NODE */
                node_t *subnode, *subnode_tmp;
                /* Set virtual */
                xmlNewProp(crt_node, BAD_CAST "virtual", BAD_CAST "yes");
                /* Set size */
                strBuffSize = asprintf(&strBuff, "%u", HASH_COUNT(node->subnodes));
                if (0 < strBuffSize) {
                    set_xml_prop(crt_node, BAD_CAST "size", strBuff);
                }
                free(strBuff); strBuff = NULL;
                /* Add subnodes */
                xmlNodePtr subnodes_node = xmlNewChild(crt_node, NULL, BAD_CAST "subnodes", NULL);
                xmlNodePtr subnode_node;
                HASH_ITER(hh, node->subnodes, subnode, subnode_tmp) {
                    subnode_node = xmlNewChild(subnodes_node, NULL, BAD_CAST "node", NULL);
                    insert_xml_node(subnode_node, subnode, full_hwloc_path);
                }
            }
            /* Add links and connexions */
            edge_t *edge, *edge_tmp;
            HASH_ITER(hh, node->edges, edge, edge_tmp) {
                /* Check edge belongs to this partition */
                if(!edge->partitions || !edge->partitions[p])
                    continue;
                crt_node = xmlNewChild(cons_node, NULL, BAD_CAST "connexion", NULL);
                insert_xml_edge(crt_node, edge, node);
            }
        }
        /* Set size */
        strBuffSize = asprintf(&strBuff, "%u", part_size);
        if (0 < strBuffSize)
            set_xml_prop(part_node, BAD_CAST "size", strBuff);
        free(strBuff); strBuff = NULL;
        /* Get next partition */
        ppartition = (char **)utarray_next(partitions, ppartition);
    }
    /*
     * Add structural/extra edges
     */
    insert_extra(root_node, full_hwloc_path);
    /* 
     * Dumping document to stdio or file
     */
    char *output_path;
    asprintf(&output_path, "%s/IB-%s-nodes.xml", path, subnet);
    xmlSaveFormatFileEnc(output_path, doc, "UTF-8", 1);
    free(output_path); free(full_hwloc_path);
    /* Free the document */
    xmlFreeDoc(doc);
    /*
     * Free the global variables that may
     * have been allocated by the parser.
     */
    xmlCleanupParser();
    /*
     * This is to debug memory for regression tests
     */
    xmlMemoryDump();
    /* Untangle similar nodes so the virtualization is transparent */
    node_t *node, *node_tmp;
    HASH_ITER(hh, nodes, node, node_tmp) {
        if (node->subnodes) {
            node_t *subnode, *subnode_tmp;
            HASH_DEL(nodes, node);
            HASH_ITER(hh, node->subnodes, subnode, subnode_tmp) {
                HASH_DEL(node->subnodes, subnode);
                HASH_ADD_STR(nodes, physical_id, subnode);
            }
            free(node);
        }
    }
    return NETLOC_SUCCESS;
}

#else

#include <assert.h>
#warning No support available for netloc without libxml2

int write_into_xml_file(const char *subnet    __netloc_attribute_unused,
                        const char *path      __netloc_attribute_unused,
                        const char *hwlocpath __netloc_attribute_unused,
                        const netloc_network_type_t transportType __netloc_attribute_unused)
{
    /* nothing to do here */
    assert(0); return NETLOC_ERROR;
}

#endif
