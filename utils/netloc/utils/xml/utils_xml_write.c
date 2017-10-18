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
 * This file provides general function to manage the writing of XML
 * files, in case LIBXML2 is not available.
 */

#define _GNU_SOURCE         /* See feature_test_macros(7) */

#include <private/netloc.h>
#include <netloc.h>
#include <private/utils/xml.h>

#ifndef HWLOC_HAVE_LIBXML2

/******************************************************************************/
/* Function to handle XML tree */
/******************************************************************************/
typedef struct {
    size_t num;
    size_t allocated;
    void **data;
} contents_t;

static void contents_init(contents_t *contents, size_t allocated)
{
    contents->data = (void **) (allocated ?
                                malloc(sizeof(void *[allocated]))
                                : NULL);
    contents->allocated = allocated;
    contents->num = 0;
}

static void contents_add(contents_t *contents, void *data)
{
    if (contents->num == contents->allocated) {
        if (contents->allocated)
            {
                void **new_data = (void **)
                    realloc(contents->data,
                            sizeof(void *[2*contents->allocated]));
                if (!new_data)
                    return;
                contents->data = new_data;
                contents->allocated *= 2;
            } else {
            contents->data = (void **) malloc(sizeof(void *[1]));
            if (!contents->data)
                return;
            contents->allocated = 1;
        }
    }
    contents->data[contents->num] = data;
    contents->num++;
}

static void contents_end(contents_t *contents)
{
    free(contents->data);
}

/******************************************************************************/
struct xml_node_t {
    xml_char *name;
    xml_char *content;
    contents_t attributes;
    contents_t children;
};

struct xml_doc_t {
    struct xml_node_t *root;
    xml_char *xml_version;
    xml_char *doctype;
};

static int xml_node_write(FILE *out, xml_node_ptr node, unsigned int depth);

xml_node_ptr xml_node_new(xml_ns_ptr ns __netloc_attribute_unused,
                          const xml_char *type)
{
    xml_node_ptr node = (xml_node_ptr)malloc(sizeof(struct xml_node_t));
    if (!node) {
        fprintf(stderr, "ERROR: Unable to allocate node.\n");
        return NULL;
    }
    node->name = strdup(type);
    contents_init(&node->attributes, 0);
    contents_init(&node->children, 0);
    node->content = NULL;
    return node;
}

void xml_node_free(xml_node_ptr node)
{
    size_t i;
    free(node->name);
    free(node->content);
    for (i = 0; i < node->attributes.num; ++i)
        free(node->attributes.data[i]);
    contents_end(&node->attributes);
    for (i = 0; i < node->children.num; ++i)
        xml_node_free((xml_node_ptr)node->children.data[i]);
    contents_end(&node->children);
    free(node);
}

void xml_node_attr_add(xml_node_ptr node, const xml_char *name,
                       const char *value)
{
    int ret;
    char *attr = NULL;
    ret = asprintf(&attr, " %s=\"%s\"", name, value);
    if (0 > ret) {
        fprintf(stderr, "WARN: unable to add attribute named \"%s\"\n", name);
        return;
    }
    contents_add(&node->attributes, attr);
}

void xml_node_child_add(xml_node_ptr node, xml_node_ptr child)
{
    contents_add(&node->children, child);
}

xml_node_ptr xml_node_child_new(xml_node_ptr parent, xml_ns_ptr ns,
                                const xml_char *type, const xml_char *content)
{
    xml_node_ptr child = xml_node_new(ns, type);
    if (child) {
        if (content) child->content = strdup(content);
        xml_node_child_add(parent, child);
    }
    return child;
}

int xml_node_has_child(xml_node_ptr node)
{
    return 0 != node->children.num;
}

static int xml_node_write(FILE *out, xml_node_ptr node, unsigned int depth)
{
    int ret = 0;
    size_t i;
    ret += fprintf(out, "%*s<%s", depth * 2, "", node->name);
    for (i = 0; i < node->attributes.num; ++i)
        ret += fprintf(out, "%s", (char *)node->attributes.data[i]);
    if (0 == node->children.num && !node->content)
        ret += fprintf(out, "/>\n");
    else if (node->content) {
        ret += fprintf(out, ">%s</%s>\n", node->content, node->name);
    } else { /* Cannot have both content and children: because. */
        ret += fprintf(out, ">\n");
        for (i = 0; i < node->children.num; ++i)
            ret += xml_node_write(out, node->children.data[i], depth + 1);
        ret += fprintf(out, "%*s</%s>\n", depth * 2, "", node->name);
    }
    return ret;
}

xml_doc_ptr xml_doc_new(const xml_char *version) {
    xml_doc_ptr ret = (xml_doc_ptr)malloc(sizeof(struct xml_doc_t));
    ret->xml_version = strdup(version);
    ret->doctype = NULL;
    return ret;
}

void xml_doc_free(xml_doc_ptr doc) {
    xml_node_free(doc->root);
    free(doc->xml_version);
    free(doc->doctype);
    free(doc);
}

xml_node_ptr xml_doc_set_root_element(xml_doc_ptr doc, xml_node_ptr node) {
    xml_node_ptr old = doc->root;
    doc->root = node;
    return old;
}

int xml_doc_write(const char *outpath, xml_doc_ptr doc, const char *enc,
                  int format __netloc_attribute_unused)
{
    FILE *out = fopen(outpath, "w");
    if (!out)
        return (fclose(out), -1);
    int ret = fprintf(out, "<?xml version=\"%s\" encoding=\"%s\"?>\n%s",
                      doc->xml_version, enc, doc->doctype ? doc->doctype : "");
    ret += xml_node_write(out, doc->root, 0);
    /* Close file */
    fclose(out);
    return ret;
}

void xml_dtd_subset_create(xml_doc_ptr doc, const xml_char *name,
                           const xml_char *externalid __netloc_attribute_unused,
                           const xml_char *systemid)
{
    int ret;
    if (doc->doctype) {
        fprintf(stderr, "WARN: Overwriting previous DTD: \"%s\"\n",
                doc->doctype);
        free(doc->doctype);
    }
    ret = asprintf(&doc->doctype, "<!DOCTYPE %s SYSTEM \"%s\">\n",
                       name, systemid);
    if (0 > ret)
        doc->doctype = NULL;
}

#else /* if defined( HWLOC_HAVE_LIBXML2 ) */

void xml_node_attr_cpy_add(xml_node_ptr pnode, const xml_char *name,
                           const char *value)
{
    xml_char *buff = xml_char_strdup(value);
    if (buff)
        xml_node_attr_add(pnode, name, buff);
    xml_char_free(buff);
}

#endif
