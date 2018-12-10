#define _GNU_SOURCE	   /* See feature_test_macros(7) */
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#include "/usr/include/libxml2/libxml/parser.h"
#include "/usr/include/libxml2/libxml/tree.h"
#include "/usr/include/libxml2/libxml/xmlmemory.h"

#include "include/netloc-utils.h"
#include "include/netloc-wip.h"
#include "include/netloc-datatypes.h"

void add_netloc_node(xmlNodePtr parent_node, netloc_node_t *node,
        netloc_machine_t *machine);

char *str_join(int size, char **values) {
    /* Compute total length */
    int total_len = 0;
    for (int i = 0; i < size; i++) {
        total_len += strlen(values[i]);
    }
    if (size) {
        total_len += size-1; /* space chars */
    }

    char *total_str = (char *)malloc(sizeof(char[total_len+1]));

    for (int i = 0, offset = 0; i < size; i++) {
        int len = strlen(values[i]);
        memcpy(total_str+offset, values[i], len);
        offset += len;
        total_str[offset] = ' ';
        offset++;
    }
    total_str[total_len] = '\0';

    return total_str;
}

char *int_join(int size, int *values)
{
    /* Convert each int into char* */
    char **str_ints = (char **)malloc(sizeof(char *[size]));
    for (int i = 0; i < size; i++) {
        asprintf(str_ints+i, "%d", values[i]);
    }

    char *ret_char = str_join(size, str_ints);

    for (int i = 0; i < size; i++) {
        free(str_ints[i]);
    }
    free(str_ints);

    return ret_char;
}

char *float_join(int size, float *values)
{
    /* Convert each float into char* */
    char **str_floats = (char **)malloc(sizeof(char *[size]));
    for (int i = 0; i < size; i++) {
        asprintf(str_floats+i, "%f", values[i]);
    }

    char *ret_char = str_join(size, str_floats);

    for (int i = 0; i < size; i++) {
        free(str_floats[i]);
    }
    free(str_floats);

    return ret_char;
}

int netloc_machine_to_xml(netloc_machine_t *machine)
{
    /* Creates a new document, a node and set it as a root node */
    xmlDocPtr doc = NULL;       /* document pointer */
    doc = xmlNewDoc(BAD_CAST "1.0");

    /* Create a machine node */
    xmlNodePtr root_node = xmlNewNode(NULL, BAD_CAST "machine");
    xmlDocSetRootElement(doc, root_node);

    /* Set version */
    xmlNewProp(root_node, BAD_CAST "version", BAD_CAST "3.0");

    /* Add hwloc_path node if it exists */
    if (machine->hwloc_dir_path && 0 < strlen(machine->hwloc_dir_path)) {
        xmlChar *buff = xmlCharStrdup((const char *)machine->hwloc_dir_path);
        if (buff) {
            xmlNewProp(root_node, BAD_CAST "hwloc_path", BAD_CAST buff);
            xmlFree(buff);
        }
    }

    /** Hwloc topology List */
    //char *hwloc_dir_path;
    //char **hwlocpaths;
    //unsigned int nb_hwloc_topos;
    //hwloc_topology_t *hwloc_topos; // TODO

    xmlNodePtr partitions_node = xmlNewNode(NULL, BAD_CAST "partitions");
    xmlAddChild(root_node, partitions_node);
    for (int p = 0; p < machine->npartitions; p++) {
        netloc_partition_t *partition = machine->partitions+p;
        xmlNodePtr partition_node = xmlNewNode(NULL, BAD_CAST "partition");
        xmlAddChild(partitions_node, partition_node);

        /* idx */
        char *idx;
        asprintf(&idx, "%d", p);
        xmlNewProp(partition_node, BAD_CAST "idx", BAD_CAST idx);
        free(idx);

        xmlNewProp(partition_node, BAD_CAST "transport", BAD_CAST "IB");

        xmlNewProp(partition_node, BAD_CAST "subnet",
                BAD_CAST "fe80:0000:0000:0000");

        xmlNewProp(partition_node, BAD_CAST "name",
                BAD_CAST partition->partition_name);


        netloc_topology_t *topology = partition->topology;
        xmlNodePtr parent_node = partition_node;
        while (topology) {
            xmlNodePtr topo_node = xmlNewNode(NULL, BAD_CAST "topology");
            xmlAddChild(parent_node, topo_node);

            /* Type */
            char *topo_type;
            asprintf(&topo_type, "%d", topology->type);
            xmlNewProp(topo_node, BAD_CAST "type", BAD_CAST topo_type);
            free(topo_type);

            /* Ndims */
            char *topo_ndims;
            asprintf(&topo_ndims, "%d", topology->ndims);
            xmlNewProp(topo_node, BAD_CAST "ndims", BAD_CAST topo_ndims);
            free(topo_ndims);

            /* Dims */
            char *topo_dims = int_join(topology->ndims, topology->dimensions);
            xmlNewProp(topo_node, BAD_CAST "dims", BAD_CAST topo_dims);
            free(topo_dims);

            /* Costs */
            char *topo_costs = int_join(topology->ndims, topology->costs);
            xmlNewProp(topo_node, BAD_CAST "costs", BAD_CAST topo_costs);
            free(topo_costs);

            topology = topology->subtopo;
            parent_node = topo_node;
        }
    }


    /* Explicit */
    netloc_explicit_t *explicit = machine->explicit;
    xmlNodePtr explicit_node = xmlNewNode(NULL, BAD_CAST "explicit");
    xmlAddChild(root_node, explicit_node);

    /* Nodes */
    xmlNodePtr nodes_node = xmlNewNode(NULL, BAD_CAST "nodes");
    xmlNewProp(nodes_node, BAD_CAST "hwloc_path", BAD_CAST machine->hwloc_dir_path);
    xmlAddChild(explicit_node, nodes_node);

    netloc_node_t *node, *node_tmp;
    HASH_ITER(hh, explicit->nodes, node, node_tmp) {
        /* Skip if belongs to a virtual node */
        if (!node->nsubnodes && node->virtual_node) {
            continue;
        }
        add_netloc_node(nodes_node, node, machine);
    }

    // Add links at the same level as nodes (in XML) ??? TODO XXX */

    // XXX TODO find name
    char *xml_path;
    asprintf(&xml_path, "%s/%s", machine->topopath, "topo.xml");

    DIR* dir = opendir(machine->topopath);
    if (dir)
    {
        /* Directory exists. */
        closedir(dir);
    }
    else if (ENOENT == errno)
    {
        /* Directory does not exist. */
        int ret = mkdir(machine->topopath, 0755);
        if (ret == -1) {
            perror("mkdir");
        }
    } else if (ENOTDIR == errno) {
        perror("opendir");
    }

    xmlSaveFormatFileEnc(xml_path, doc, "UTF-8", 1);

    return 0;
}

void add_netloc_node(xmlNodePtr parent_node, netloc_node_t *node,
        netloc_machine_t *machine)
{
    char *prop;
    xmlNodePtr node_node = xmlNewNode(NULL, BAD_CAST "node");
    xmlAddChild(parent_node, node_node);

    xmlNewProp(node_node, BAD_CAST "mac_addr", BAD_CAST node->physical_id);

    if (node->type == NETLOC_NODE_TYPE_HOST) {
        xmlNewProp(node_node, BAD_CAST "type", BAD_CAST "CA");
        xmlNewProp(node_node, BAD_CAST "name", BAD_CAST node->hostname);
    } else {
        xmlNewProp(node_node, BAD_CAST "type", BAD_CAST "SW");
        asprintf(&prop, "%d", node->nsubnodes);
        xmlNewProp(node_node, BAD_CAST "size", BAD_CAST prop);
        free(prop);
        xmlNewProp(node_node, BAD_CAST "name", BAD_CAST "");
    }

    xmlNewProp(node_node, BAD_CAST "hwloc_file", BAD_CAST "XXX"); // XXX TODO

    if (node->newtopo_positions) {
        int *all_idx;
        char **all_coords;
        all_idx = (int *)malloc(sizeof(int[machine->npartitions]));
        all_coords = (char **)malloc(sizeof(char *[machine->npartitions]));

        for (int p = 0; p < node->nparts; p++) {
            int partition = node->newpartitions[p];
            all_idx[p] = node->newtopo_positions[p].idx;

            all_coords[p] =
                int_join(machine->partitions[partition].topology->ndims,
                        node->newtopo_positions[p].coords);
        }

        char *all_idx_str, *all_coords_str;
        all_idx_str = int_join(node->nparts, all_idx);
        all_coords_str = str_join(node->nparts, all_coords);

        /* Add ';' between partitions */
        for (int p = 0, offset=0; p < node->nparts-1; p++) {
            offset += strlen(all_coords[p]);
            all_coords_str[offset] = ';';
            offset++;
        }

        free(all_idx);
        free(all_coords);
        for (int p = 0; p < node->nparts-1; p++) {
            free(all_coords[p]);
        }

        xmlNewProp(node_node, BAD_CAST "index", BAD_CAST all_idx_str);
        xmlNewProp(node_node, BAD_CAST "coords", BAD_CAST all_coords_str);
        free(all_idx_str);
        free(all_coords_str);
    }

    xmlNewProp(node_node, BAD_CAST "description", BAD_CAST node->description);

    char *partition_str = int_join(node->nparts, node->newpartitions);
    xmlNewProp(node_node, BAD_CAST "partitions", BAD_CAST partition_str);
    free(partition_str);

    /* Sub nodes if virtual */
    for (int n = 0; n < node->nsubnodes; n++) {
        add_netloc_node(node_node, node->subnodes[n], machine);
    }

    /* Connexions */
    xmlNodePtr connexions_node = xmlNewNode(NULL, BAD_CAST "connexions");
    xmlAddChild(node_node, connexions_node);

    /* Sub edges */
    // TODO

    /* Iter on edges */
    netloc_edge_t *edge, *edge_tmp;
    netloc_node_iter_edges(node, edge, edge_tmp) {
        xmlNodePtr connexion_node = xmlNewNode(NULL, BAD_CAST "connexion");
        xmlAddChild(connexions_node, connexion_node);

        asprintf(&prop, "%f", edge->total_gbits);
        xmlNewProp(connexion_node, BAD_CAST "bandwidth", BAD_CAST prop);
        free(prop);

        xmlNewProp(connexion_node, BAD_CAST "dest", BAD_CAST edge->dest->physical_id);

        /* Links */
        for (int l = 0; l < utarray_len(&edge->physical_links); l++) {
            netloc_physical_link_t *link = *(netloc_physical_link_t **)
                utarray_eltptr(&edge->physical_links, l);

            xmlNodePtr link_node = xmlNewNode(NULL, BAD_CAST "link");
            xmlAddChild(connexion_node, link_node);

            asprintf(&prop, "%d", link->ports[0]);
            xmlNewProp(link_node, BAD_CAST "srcport", BAD_CAST prop);
            free(prop);

            asprintf(&prop, "%d", link->ports[1]);
            xmlNewProp(link_node, BAD_CAST "destport", BAD_CAST prop);
            free(prop);

            xmlNewProp(link_node, BAD_CAST "speed", BAD_CAST link->speed);
            xmlNewProp(link_node, BAD_CAST "width", BAD_CAST link->width);

            asprintf(&prop, "%f", link->gbits);
            xmlNewProp(link_node, BAD_CAST "bandwidth", BAD_CAST prop);
            free(prop);

            asprintf(&prop, "%llu", link->id);
            xmlNewProp(link_node, BAD_CAST "id", BAD_CAST prop);
            free(prop);

            asprintf(&prop, "%llu", link->other_way_id);
            xmlNewProp(link_node, BAD_CAST "reverse_id", BAD_CAST prop);
            free(prop);

            xmlNewProp(link_node, BAD_CAST "description",
                    BAD_CAST link->description);

            if (link->newpartitions) {
                char *partition_str = int_join(link->nparts, link->newpartitions);
                xmlNewProp(link_node, BAD_CAST "partitions", BAD_CAST partition_str);
                free(partition_str);
            }
        }

    }
}
