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

#ifndef _UTILS_XML_H_
#define _UTILS_XML_H_

#include <private/netloc.h>
#include <netloc.h>

#ifdef HWLOC_HAVE_LIBXML2

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

typedef xmlChar    xml_char;
#define xml_char_free   xmlFree
#define xml_char_strdup xmlCharStrdup

typedef xmlNodePtr xml_node_ptr;
#define xml_node_new       xmlNewNode
#define xml_node_free      xmlFree
#define xml_node_attr_add  xmlNewProp
extern void xml_node_attr_cpy_add(xml_node_ptr pnode, const xml_char *name,
                                  const char *value);
#define xml_node_child_add xmlAddChild
#define xml_node_child_new xmlNewChild
#define xml_node_has_child(node) (NULL != node->children)

typedef xmlDocPtr  xml_doc_ptr;
#define xml_doc_new              xmlNewDoc
#define xml_doc_free             xmlFreeDoc
#define xml_doc_set_root_element xmlDocSetRootElement
#define xml_doc_write            xmlSaveFormatFileEnc

#define xml_parser_cleanup()                    \
    do {                                        \
        if (getenv("HWLOC_LIBXML_CLEANUP"))     \
            xmlCleanupParser();                 \
    } while (0)

#define XML_LIB_CHECK_VERSION LIBXML_TEST_VERSION

#else

/* XML writing is made with utils/netloc/xml/netloc_xml_write.c */

typedef void * xml_ns_ptr;

typedef char xml_char;
#define xml_char_free   free
#define xml_char_strdup strdup
#define BAD_CAST (xml_char *)

struct xml_node_t;
typedef struct xml_node_t * xml_node_ptr;

xml_node_ptr xml_node_new(xml_ns_ptr ns __netloc_attribute_unused,
                          char *type);

void xml_node_free(xml_node_ptr node);

#define xml_node_attr_cpy_add xml_node_attr_add

void xml_node_attr_add(xml_node_ptr node, const char *name,
                       const char *value);

void xml_node_child_add(xml_node_ptr node, xml_node_ptr child);

xml_node_ptr xml_node_child_new(xml_node_ptr parent,
                                xml_ns_ptr namespace __netloc_attribute_unused,
                                xml_char *type, xml_char *content);

int xml_node_has_child(xml_node_ptr node);


struct xml_doc_t;
typedef struct xml_doc_t * xml_doc_ptr;

xml_doc_ptr xml_doc_new(const xml_char *version);

void xml_doc_free(xml_doc_ptr doc);

void xml_doc_set_root_element(xml_doc_ptr doc, xml_node_ptr node);

int xml_doc_write(char *outpath, xml_doc_ptr doc, const char *enc,
                  int format __netloc_attribute_unused);

#define xml_parser_cleanup()  do { /* do nothing */ } while (0)
#define XML_LIB_CHECK_VERSION do { /* do nothing */ } while (0)

#endif /* not defined( HWLOC_HAVE_LIBXLM2 ) */

#endif /* _UTILS_XML_H_ */
