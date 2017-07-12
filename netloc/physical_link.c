/*
 * Copyright © 2016-2017 Inria.  All rights reserved.
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

netloc_physical_link_t * netloc_physical_link_construct(void)
{
    netloc_physical_link_t *physical_link = NULL;

    physical_link = (netloc_physical_link_t*)
        malloc(sizeof(netloc_physical_link_t));
    if( NULL == physical_link ) {
        return NULL;
    }

    physical_link->id = cur_uid;
    cur_uid++;

    physical_link->src = NULL;
    physical_link->dest = NULL;

    physical_link->ports[0] = -1;
    physical_link->ports[1] = -1;

    physical_link->width = NULL;
    physical_link->speed = NULL;

    physical_link->edge = NULL;
    physical_link->other_way = NULL;

    utarray_new(physical_link->partitions, &ut_ptr_icd);

    physical_link->gbits = -1;

    physical_link->description = NULL;

    return physical_link;
}

netloc_physical_link_t * netloc_physical_link_deep_copy(netloc_physical_link_t *origin)
{
    netloc_physical_link_t *physical_link = NULL;

    if (NULL == origin) {
        return NULL;
    }

    physical_link = (netloc_physical_link_t*)
        malloc(sizeof(netloc_physical_link_t));
    if( NULL == physical_link ) {
        return NULL;
    }

    physical_link->id = cur_uid;
    cur_uid++;

    physical_link->src = origin->src;
    physical_link->dest = origin->dest;

    physical_link->ports[0] = origin->ports[0];
    physical_link->ports[1] = origin->ports[1];

    physical_link->width = strdup(origin->width);
    physical_link->speed = strdup(origin->speed);

    physical_link->edge = origin->edge;
    physical_link->other_way = origin->other_way;

    utarray_new(physical_link->partitions, &ut_ptr_icd);
    utarray_concat(physical_link->partitions, origin->partitions);

    physical_link->gbits = origin->gbits;

    physical_link->description = strdup(origin->description);

    return physical_link;
}

int netloc_physical_link_destruct(netloc_physical_link_t *link)
{
    free(link->width);
    free(link->description);
    free(link->speed);
    utarray_free(link->partitions);
    free(link);
    return NETLOC_SUCCESS;
}

char * netloc_link_pretty_print(netloc_physical_link_t* link)
{
    char * str = NULL;
    const char * tmp_src_str = NULL;
    const char * tmp_dest_str = NULL;

    tmp_src_str = netloc_node_type_encode(link->src->type);
    tmp_dest_str = netloc_node_type_encode(link->dest->type);

    asprintf(&str, "%3d (%s) [%23s] %d [<- %s / %s (%f) ->] (%s) [%23s] %d",
             link->id,
             tmp_src_str,
             link->src->physical_id,
             link->ports[0],
             link->speed,
             link->width,
             link->gbits,
             tmp_dest_str,
             link->dest->physical_id,
             link->ports[1]);

    return str;
}

#if defined(HWLOC_HAVE_LIBXML2)

netloc_physical_link_t *
netloc_physical_link_xml_load(xmlNode *it_link, netloc_edge_t *edge,
                              netloc_partition_t *partition)
{
    xmlNode *tmp, *crt_node;
    xmlChar *buff = NULL;
    char *strBuff = NULL;
    size_t strBuffSize, buffSize;
    netloc_physical_link_t *link = netloc_physical_link_construct();
    /* set ports */
    int tmpport;
    buff = xmlGetProp(it_link, BAD_CAST "srcport");
    if (!buff ||
        (!(tmpport = strtol((char *)buff, &strBuff, 10)) && strBuff == (char *)buff)){
        fprintf(stderr, "ERROR: cannot read physical link's source port.\n");
        goto ERROR;
    }
    xmlFree(buff); buff = NULL;
    link->ports[0] = tmpport;
    buff = xmlGetProp(it_link, BAD_CAST "destport");
    if (!buff ||
        (!(tmpport = strtol((char *)buff, &strBuff, 10)) && strBuff == (char *)buff)){
        fprintf(stderr, "ERROR: cannot read physical link's destination port.\n");
        goto ERROR;
    }
    xmlFree(buff); buff = NULL;
    link->ports[1] = tmpport;
    /* set speed */
    buff = xmlGetProp(it_link, BAD_CAST "speed");
    if (!buff || 0 >= strlen((char *)buff)) {
        fprintf(stderr, "ERROR: cannot read physical link's speed.\n");
        goto ERROR;
    }
    link->speed = strdup((char *)buff);
    xmlFree(buff); buff = NULL;
    /* set width */
    buff = xmlGetProp(it_link, BAD_CAST "width");
    if (!buff || 0 >= strlen((char *)buff)) {
        fprintf(stderr, "ERROR: cannot read physical link's width.\n");
        goto ERROR;
    }
    link->width = strdup((char *)buff);
    xmlFree(buff); buff = NULL;
    /* set gbits */
    float gbits;
    buff = xmlGetProp(it_link, BAD_CAST "bandwidth");
    if (!buff || 0 >= strlen((char *)buff)
        || (!(gbits = strtof((char *)buff, &strBuff)) && strBuff == (char *)buff)) {
        fprintf(stderr, "ERROR: cannot read physical link's bandwidth.\n");
        goto ERROR;
    }
    xmlFree(buff); buff = NULL; strBuff = NULL;
    /* set id */
    unsigned long long int id_read;
    buff = xmlGetProp(it_link, BAD_CAST "logical_id");
    if (!buff || 0 >= strlen((char *)buff) || 1 > sscanf((char *)buff, "%llu", &id_read)) {
        fprintf(stderr, "ERROR: cannot read physical link's id.\n");
        goto ERROR;
    }
    link->id = id_read;
    xmlFree(buff); buff = NULL; strBuff = NULL;
    /* set other_way_id */
    buff = xmlGetProp(it_link, BAD_CAST "reverse_logical_id");
    if (!buff || 0 >= strlen((char *)buff) || 1 > sscanf((char *)buff, "%llu", &id_read)) {
        fprintf(stderr, "ERROR: cannot read reverse physical link's id.\n");
        goto ERROR;
    }
    link->other_way_id = id_read;
    xmlFree(buff); buff = NULL; strBuff = NULL;
    /* set description */
    if (it_link->children
        && (XML_TEXT_NODE == it_link->children->type && (tmp = it_link->children))) {
        link->description = strdup((char *)tmp->content);
    }
    /* set src, dest, partition, edge */
    link->src   = edge->node;
    link->dest  = edge->dest;
    link->edge  = edge;
    link->gbits = gbits;
    if (partition)
        utarray_push_back(link->partitions, &partition);

    return link;

 ERROR:
    netloc_physical_link_destruct(link);
    xmlFree(buff); buff = NULL;
    return NULL;
}

#else
netloc_physical_link_t *
netloc_physical_link_xml_load(void *it_link                 __netloc_attribute_unused,
                              netloc_edge_t *edge           __netloc_attribute_unused,
                              netloc_partition_t *partition __netloc_attribute_unused)
{
    /* nothing to do here */
    return NULL;
}
#endif /* defined(HWLOC_HAVE_LIBXML2) */
