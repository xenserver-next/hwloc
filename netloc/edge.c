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

static int cur_uid = 0;

netloc_edge_t * netloc_edge_construct()
{
    netloc_edge_t *edge = NULL;

    edge = (netloc_edge_t*)malloc(sizeof(netloc_edge_t));
    if( NULL == edge ) {
        return NULL;
    }

    edge->id = cur_uid;
    cur_uid++;

    edge->dest = NULL;
    edge->node = NULL;
    edge->other_way = NULL;

    utarray_new(edge->partitions, &ut_ptr_icd);
    utarray_new(edge->physical_links, &ut_ptr_icd);

    edge->total_gbits = 0;

    edge->nsubedges = 0;
    edge->subnode_edges = NULL;

    edge->userdata = NULL;

    return edge;
}

char * netloc_edge_pretty_print(netloc_edge_t* edge)
{
    char * str = NULL;

    asprintf(&str, " [%23s]/[%d] -- [%23s]/[%d] (%f gbits, %d links)",
             edge->node->physical_id, edge->node->logical_id,
             edge->dest->physical_id, edge->dest->logical_id,
             edge->total_gbits, utarray_len(edge->physical_links));

    return str;
}

int netloc_edge_destruct(netloc_edge_t * edge)
{
    utarray_free(edge->physical_links);
    utarray_free(edge->partitions);

    if (edge->nsubedges)
        free(edge->subnode_edges);
    free(edge);
    return NETLOC_SUCCESS;
}

void netloc_edge_reset_uid(void)
{
    cur_uid = 0;
}

#if defined(HWLOC_HAVE_LIBXML2)

netloc_edge_t *
netloc_edge_xml_load(xmlNode *it_edge, netloc_topology_t *topology, netloc_partition_t *partition)
{
    xmlNode *tmp, *crt_node;
    xmlChar *buff = NULL;
    char *strBuff = NULL;
    size_t strBuffSize;
    netloc_edge_t *edge = netloc_edge_construct();
    /* set total_gbits */
    float total_gbits;
    buff = xmlGetProp(it_edge, BAD_CAST "bandwidth");
    if (buff && 0 < strlen((char *)buff)) {
        total_gbits = strtof((char *)buff, &strBuff);
        if (0 == total_gbits && strBuff == (char *)buff)
            fprintf(stderr, "WARN: cannot read connexion's bandwidth.\n");
        xmlFree(buff); buff = NULL; strBuff = NULL;
        edge->total_gbits = total_gbits;
    }
    /* get number of links */
    unsigned int nlinks;
    buff = xmlGetProp(it_edge, BAD_CAST "nblinks");
    if (!buff || (1 > sscanf((char *)buff, "%u", &nlinks))) {
        fprintf(stderr, "WARN: cannot read connexion's physical links.\n");
    } else if (0 >= nlinks) {
        fprintf(stderr, "WARN: less than 1 connexion's physical link required (%u).\n",
                nlinks);
    }
    xmlFree(buff); buff = NULL; strBuff = NULL;
    /* Move tmp to the proper xml node and set src node */
    if (it_edge->children
        && ((XML_TEXT_NODE == it_edge->children->type && (tmp = it_edge->children->next))
            || (tmp = it_edge->children))
        && XML_ELEMENT_NODE == tmp->type && 0 == strcmp("src", (char *)tmp->name)
        && tmp->children && tmp->children->content) {
        netloc_topology_find_node(topology, (char *)tmp->children->content, edge->node);
    } else {
        fprintf(stderr, "ERROR: cannot find connexion's source node.\n");
        goto ERROR;
    }
    /* Move tmp to the proper xml node and set dest node */
    if (tmp->next
        && ((XML_TEXT_NODE == tmp->next->type && (tmp = tmp->next->next)) || (tmp = tmp->next))
        && tmp && XML_ELEMENT_NODE == tmp->type && 0 == strcmp("dest", (char *)tmp->name)
        && tmp->children && tmp->children->content) {
        netloc_topology_find_node(topology, (char *)tmp->children->content, edge->dest);
    } else {
        fprintf(stderr, "ERROR: cannot find connexion's destination node.\n");
        goto ERROR;
    }
    if (partition)
        utarray_push_back(edge->partitions, &partition);
    HASH_ADD_PTR(edge->node->edges, dest, edge);

    if ((buff = xmlGetProp(it_edge, BAD_CAST "virtual"))
        && 0 < (strBuffSize = strlen((char *)buff))
        && 0 == strncmp("yes", (char *)buff, 3)) {
        /*** VIRTUAL EDGE ***/
        xmlFree(buff); buff = NULL;
        /* Move tmp to the proper xml node */
        unsigned int nsubedges;
        if (!tmp->next
            || !((XML_TEXT_NODE == tmp->next->type && (tmp = tmp->next->next))
                 || (tmp = tmp->next)) || !tmp || XML_ELEMENT_NODE != tmp->type
            || 0 != strcmp("subconnexions", (char *)tmp->name) || !tmp->children) {
            fprintf(stderr, "ERROR: cannot find the subedges. Failing to read this connexion\n");
            goto ERROR;
        }
        buff = xmlGetProp(tmp, BAD_CAST "size");
        if (!buff || (1 > sscanf((char *)buff, "%u", &nsubedges))) {
            fprintf(stderr, "ERROR: cannot read virtual edge subnodes.\n");
            goto ERROR;
        } else if (1 >= nsubedges) {
            fprintf(stderr, "WARN: less than 2 edges (%u).\n", nsubedges);
        }
        edge->nsubedges = nsubedges;
        edge->subnode_edges = (netloc_edge_t **)malloc(sizeof(netloc_edge_t *[nsubedges]));
        xmlFree(buff); buff = NULL; strBuff = NULL;
        unsigned int se = 0;
        for(xmlNode *it_subedge = tmp->children && XML_ELEMENT_NODE != tmp->children->type
                    ? tmp->children->next : tmp->children;
                it_subedge;
                it_subedge = it_subedge->next && XML_ELEMENT_NODE != it_subedge->next->type
                    ? it_subedge->next->next : it_subedge->next) {
            netloc_edge_t *subedge = netloc_edge_xml_load(it_subedge, topology, partition);
            edge->subnode_edges[se++] = subedge;
            if (!subedge) {
                fprintf(stderr, "WARN: cannot read subconnexion #%u\n", se);
                continue;
            }
            total_gbits -= subedge->total_gbits;
            utarray_concat(edge->node->physical_links, subedge->physical_links);
            utarray_concat(edge->physical_links, subedge->physical_links);
        }
    } else {

        /* Read physical links from file */

        /* Check for <links> tag */
        if (!tmp->next
            || !((XML_TEXT_NODE == tmp->next->type && (crt_node = tmp->next->next))
                 || (crt_node = tmp->next))
            || XML_ELEMENT_NODE != crt_node->type
            || 0 != strcmp("links", (char *)crt_node->name) || !crt_node->children) {
            fprintf(stderr, "ERROR: No \"links\" tag.\n");
            goto ERROR;
        }
        xmlNode *it_link;
        netloc_physical_link_t *link = NULL;
        for (it_link = crt_node->children && XML_ELEMENT_NODE != crt_node->children->type
                 ? crt_node->children->next : crt_node->children;
             it_link;
             it_link = it_link->next && XML_ELEMENT_NODE != it_link->next->type
                 ? it_link->next->next : it_link->next) {
            link = netloc_physical_link_xml_load(it_link, edge, partition);
            if (link) {
                total_gbits -= link->gbits;
                utarray_push_back(edge->node->physical_links, &link);
                utarray_push_back(edge->physical_links, &link);
                HASH_ADD(hh, topology->physical_links, id, sizeof(unsigned long long int), link);
            }
        }
    }
#ifdef NETLOC_DEBUG
    /* Check proper value for edge->total_gbits */
    if (0 != total_gbits) {
        fprintf(stderr, "WARN: Erroneous value read from file for edge total bandwidth. "
                "\"%f\" instead of %f (calculated).\n",
                edge->total_gbits, edge->total_gbits - total_gbits);
    }
    /* Check proper value for nlinks and utarray_len(edge->physical_links) */
    if (nlinks != utarray_len(edge->physical_links)) {
        fprintf(stderr, "WARN: Erroneous value read from file for edge count of physical links. "
                "%u requested, but only %u found\n", nlinks, utarray_len(edge->physical_links));
    }
#endif /* NETLOC_DEBUG */
    return edge;
 ERROR:
    netloc_edge_destruct(edge);
    return NULL;
}

#else
netloc_edge_t *
netloc_edge_xml_load(void *it_edge                 __netloc_attribute_unused,
                     netloc_topology_t *topology   __netloc_attribute_unused,
                     netloc_partition_t *partition __netloc_attribute_unused)
{
    /* nothing to do here */
    return NULL;
}
#endif /* defined(HWLOC_HAVE_LIBXML2) */
