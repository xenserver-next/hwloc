#include <libgen.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

#include <netloc.h>
#include <private/netloc.h>

#define NETLOC_FILE_VERSION "3.0"

static int read_topology(xmlNode *topology_node, netloc_topology_t *topology);
static xmlDoc *netloc_xml_reader_init(const char *path);
int read_partitions(xmlNode *partitions_node, netloc_machine_t *machine);
int read_explicit(xmlNode *explicit_node, netloc_machine_t *machine);
int is_positive(long int value);
int is_strictly_positive(long int value);
int read_long_int(char *str, long int *pvalue, int (*check)(long int value));
int read_int(char *str, int *pvalue, int (*check)(long int value));
int read_partition(xmlNode *partition_node, netloc_partition_t *partition);
int read_long_int_array(char *str, int *psize, long int **pvalues,
        int (*check)(long int value));
int read_int_array(char *str, int *psize, int **pvalues,
        int (*check)(long int value));
int read_int_2d_array(char *str, int *psize, int **psubsizes, int ***pvalues,
        int (*check)(long int value));
int read_nodes(xmlNode *nodes_node, netloc_explicit_t *explicit);
int read_node(xmlNode *node_node, netloc_explicit_t *explicit);
int read_restriction(xmlNode *restriction_node, int *num_nodes,
        char ***node_names);
int read_link(xmlNode *link_node);
int read_connections(xmlNode *connections_node);
int read_connection(xmlNode *connection_node);
int str_count_char(char *str, char refchar);

static xmlDoc *netloc_xml_reader_init(const char *path)
{
    xmlDoc *doc;

    /*
     * This initialize the library and check potential ABI mismatches
     * between the version it was compiled for and the actual shared
     * library used.
     */
    LIBXML_TEST_VERSION;

    doc = xmlReadFile(path, "UTF-8", XML_PARSE_NOBLANKS);
    if (!doc) {
        fprintf(stderr, "Cannot open topology file %s\n", path);
        perror("xmlReadFile");
    }

    return doc;
}

int netloc_read_xml(netloc_machine_t **pmachine, char *path)
{
    xmlNode *machine_node=NULL;
    xmlChar *buff = NULL;
    *pmachine = NULL;
    netloc_machine_t *machine = netloc_machine_construct(dirname(strdup(path)));
    machine->topopath = strdup(path);

    xmlDoc *doc = netloc_xml_reader_init(path);
    if (NULL == doc) {
        return NETLOC_ERROR_NOENT;
    }

    machine_node = xmlDocGetRootElement(doc);
    if (machine_node && !strcmp("machine", (char *) machine_node->name)) {
        /* Check netloc file version */
        buff = xmlGetProp(machine_node, BAD_CAST "version");
        if (!buff ||
            0 != strcmp(NETLOC_FILE_VERSION,(char *)buff)) {
            fprintf(stderr, "Incorrect version number (\"%s\"), "
                    "please generate your input file again.\n", (char *)buff);
            xmlFree(buff); buff = NULL;
            return NETLOC_ERROR;
        }
        xmlFree(buff); buff = NULL;
    } else {
        fprintf(stderr, "Cannot read the machine.\n");
        return NETLOC_ERROR;
    }

    int num_allocated;
    char **allocated_names;
    for (xmlNode *cur_node = machine_node->children;
            cur_node;
            cur_node = cur_node->next) {
        if (!strcmp("partitions", (char *)cur_node->name)) {
            read_partitions(cur_node, machine);

        } else if (!strcmp("explicit", (char *)cur_node->name)) {
            read_explicit(cur_node, machine);

        } else if (!strcmp("restriction", (char *)cur_node->name)) {
            read_restriction(cur_node, &num_allocated, &allocated_names);

        } else {
            exit(-1); // XXX TODO
        }
    }

    /* Handle restriction */
    if (machine->explicit) {
        netloc_node_t **node_list = malloc(num_allocated*sizeof(*node_list));
        for (int n = 0; n < num_allocated; n++) {
            char *name = allocated_names[n];
            int ret = netloc_node_find(machine, name, node_list+n);
            if (ret != NETLOC_SUCCESS) {
                return ret; // TODO_free memory
            }
            ret = netloc_restriction_set_node_list(machine, num_allocated,
                    node_list);
            if (ret != NETLOC_SUCCESS) {
                return ret; // TODO_free memory
            }
        }
    } else {
        assert(0); // TODO
    }

    *pmachine = machine;
    return 0;
}

int read_partitions(xmlNode *partitions_node, netloc_machine_t *machine)
{
    if (!partitions_node->children)
        exit(-1); // XXX TODO

    int npartitions = xmlChildElementCount(partitions_node);
    netloc_partition_t *partitions;
    partitions = (netloc_partition_t *)malloc(sizeof(netloc_partition_t[npartitions]));

    /* Retreive partitions */
    int pidx = 0;
    for (xmlNode *partition_node = partitions_node->children;
            partition_node;
            partition_node = partition_node->next) {
        if (strcmp((char *)partition_node->name, "partition")) {
            exit(-1); // XXX TODO
        }
        read_partition(partition_node, partitions+pidx);
        pidx++;
    }

    netloc_machine_add_partitions(machine, npartitions, partitions);

    return 0;
}

int read_explicit(xmlNode *explicit_node, netloc_machine_t *machine)
{
    netloc_machine_add_explicit(machine);
    for (xmlNode *cur_node = explicit_node->children;
            cur_node;
            cur_node = cur_node->next) {
        if (!strcmp("nodes", (char *)cur_node->name)) {
            read_nodes(cur_node, machine->explicit);
        }
    }

    return 0;
}

int read_restriction(xmlNode *restriction_node, int *pnum_nodes, char ***pnode_names)
{
    xmlChar *buff;
    assert(restriction_node->children);

    int num_nodes = xmlChildElementCount(restriction_node);
    *pnum_nodes = num_nodes;

    char **node_names;
    node_names = malloc(num_nodes*sizeof(*node_names));
    *pnode_names = node_names;

    /* Retreive partitions */
    int n = 0;
    for (xmlNode *cur_node = restriction_node->children; cur_node; cur_node = cur_node->next) {
        if (!strcmp((char *)cur_node->name, "node")) {
            buff = xmlGetProp(cur_node, BAD_CAST "name");
            node_names[n] = (char *)buff;
        } else {
            exit(-1); // XXX TODO
        }
        n++;
    }

    return NETLOC_SUCCESS;
}


int read_nodes(xmlNode *nodes_node, netloc_explicit_t *explicit)
{
    for (xmlNode *cur_node = nodes_node->children;
            cur_node;
            cur_node = cur_node->next) {
        if (!strcmp("node", (char *)cur_node->name)) {
            read_node(cur_node, explicit);
        }
    }

    return 0;
}


int read_node(xmlNode *node_node, netloc_explicit_t *explicit)
{
    xmlChar *buff;
    netloc_node_t *node = netloc_node_construct();

    buff = xmlGetProp(node_node, BAD_CAST "mac_addr");
    strcpy(node->physical_id, (char *)buff);

    buff = xmlGetProp(node_node, BAD_CAST "type");
    node->type = netloc_node_type_decode((char *)buff);
    free(buff);

    buff = xmlGetProp(node_node, BAD_CAST "name");
    node->hostname = (char *)buff;

    buff = xmlGetProp(node_node, BAD_CAST "hwloc_file");
    // TODO

    buff = xmlGetProp(node_node, BAD_CAST "partitions");
    int nparts;
    int *parts;
    if (read_int_array((char *)buff, &nparts, &parts, is_positive) != 0) {
        exit(-1);
    }
    node->nparts = nparts;
    node->partitions = parts;

    if (node->type == NETLOC_NODE_TYPE_HOST) {
        int size1, size2;
        buff = xmlGetProp(node_node, BAD_CAST "index");
        int read_int_array(char *str, int *psize, int **pvalues,
                int (*check)(long int value));
        int *idx;
        if (read_int_array((char *)buff, &size1, &idx, is_positive) != 0) {
            exit(-1);
        }
        node->topo_positions = (netloc_position_t *)malloc(sizeof(netloc_position_t[size1]));
        for (int p = 0; p < size1; p++) {
            node->topo_positions[p].idx = idx[p];
        }
        free(idx);

        buff = xmlGetProp(node_node, BAD_CAST "coords");
        int *subsizes;
        int **coords;
        if (strlen(buff)) {
            if (read_int_2d_array((char *)buff, &size2, &subsizes, &coords, is_positive) != 0) {
                free(subsizes);
                free(coords);
                exit(-1);
            } else {
                assert(size1 == size2);
                for (int p = 0; p < size2; p++) {
                    node->topo_positions[p].coords = coords[p];
                }
                free(subsizes);
                free(coords);
            }
        }

    } else if (node->type != NETLOC_NODE_TYPE_SWITCH) {
        exit(-1);
    }

    buff = xmlGetProp(node_node, BAD_CAST "description");
    node->description = (char *)buff;


    if (!node_node->children)
        exit(-1); // XXX TODO

    /* Retreive partitions */
    for (xmlNode *cur_node = node_node->children;
            cur_node;
            cur_node = cur_node->next) {
        if (!strcmp((char *)cur_node->name, "connections")) {
            read_connections(cur_node);

        /* Virtual node */
        } else if (!strcmp((char *)cur_node->name, "node")) {
            read_node(cur_node, explicit);
        } else {
            exit(-1); // XXX TODO
        }
    }

    netloc_explicit_add_node(explicit, node);

    return 0;
}

int read_connections(xmlNode *connections_node)
{
    //TODO add parameter to get connextion
    for (xmlNode *cur_node = connections_node->children;
            cur_node;
            cur_node = cur_node->next) {
        if (!strcmp("connection", (char *)cur_node->name)) {
            read_connection(cur_node);
        }
    }

    return 0;
}

int read_connection(xmlNode *connection_node)
{
    // TODO
    xmlChar *buff;

    buff = xmlGetProp(connection_node, BAD_CAST "bandwidth");
    buff = xmlGetProp(connection_node, BAD_CAST "dest");

    if (!connection_node->children)
        exit(-1); // XXX TODO

    /* Retreive partitions */
    for (xmlNode *cur_node = connection_node->children;
            cur_node;
            cur_node = cur_node->next) {
        if (!strcmp((char *)cur_node->name, "link")) {
            read_link(cur_node);
        } else {
            exit(-1); // XXX TODO
        }
    }

    return 0;
}

int read_link(xmlNode *link_node)
{
    // TODO
    xmlChar *buff;

    buff = xmlGetProp(link_node, BAD_CAST "mac_addr");
    buff = xmlGetProp(link_node, BAD_CAST "srcport");
    buff = xmlGetProp(link_node, BAD_CAST "destport");
    buff = xmlGetProp(link_node, BAD_CAST "speed");
    buff = xmlGetProp(link_node, BAD_CAST "width");
    buff = xmlGetProp(link_node, BAD_CAST "bandwidth");
    buff = xmlGetProp(link_node, BAD_CAST "id");
    buff = xmlGetProp(link_node, BAD_CAST "reverse_id");
    buff = xmlGetProp(link_node, BAD_CAST "description");
    buff = xmlGetProp(link_node, BAD_CAST "partitions");

    return 0;
}

int is_positive(long int value)
{
    return value >= 0;
}

int is_strictly_positive(long int value)
{
    return value > 0;
}

int read_long_int(char *str, long int *pvalue, int (*check)(long int value))
{
    char *endStr = NULL;
    long int value;

    if (!str || (check(value = strtol(str, &endStr, 10))
                && endStr == str)){
        return -1; // XXX TODO
    }
    *pvalue = value;
    return 0;
}


int read_partition(xmlNode *partition_node, netloc_partition_t *partition)
{
    xmlChar *buff;

    /* Read properties */
    /* Read index */
    long int idx;
    buff = xmlGetProp(partition_node, BAD_CAST "idx");
    if (read_long_int((char *)buff, &idx, is_positive) == -1)
    {
        fprintf(stderr, "WARN: cannot read partition index.\n");
        exit(-1); // XXX TODO
    } else {
        partition->idx = idx;
    }

    /* Read transport type */
    buff = xmlGetProp(partition_node, BAD_CAST "transport");
    partition->transport_type = netloc_network_type_decode((char *)buff);
    free(buff);

    /* Read subnet */
    buff = xmlGetProp(partition_node, BAD_CAST "subnet");
    partition->subnet = (char *)buff;

    /* Read name */
    buff = xmlGetProp(partition_node, BAD_CAST "name");
    partition->partition_name = (char *)buff;

    /* Read contents */
    xmlNode *cur_node = partition_node->children;
    if (strcmp("topology", (char *)cur_node->name)) {
        exit(-1);
    } else {
        netloc_topology_t *topology = netloc_topology_construct();
        partition->topology = topology;
        read_topology(cur_node, topology);
    }

    return 0;
}

int read_long_int_array(char *str, int *psize, long int **pvalues,
        int (*check)(long int value))
{
    assert(str);
    int size = str_count_char(str, ' ')+1;
    long int *values = (long int *)malloc(sizeof(long int[size]));

    *pvalues = NULL;

    char *endStr;
    for (int i = 0; i < size; i++) {
        assert(strlen(str));
        long int value = strtol(str, &endStr, 10);
        if (!check(value)) {
            exit(-1);
        }

        /* Check we have space character or null character */
        if (*endStr == ' ') {
            str++;
        } else if (*endStr != '\0') {
            exit(-1);
        }
        values[i] = value;
    }

    *pvalues = values;
    *psize = size;

    return 0;
}

int str_count_char(char *str, char refchar)
{
    assert(str);
    int count = 0;
    for (char *s = str; *s; s++) {
        if (*s == refchar)
            count++;
    }

    return count;
}

int read_int(char *str, int *pvalue, int (*check)(long int value))
{
    assert(str);
    char *endStr;
    long int value = strtol(str, &endStr, 10);
    if (!check(value)) {
        exit(-1);
    }
    int ivalue = (int)value;
    assert((long int)ivalue == value);

    *pvalue = ivalue;

    return 0;
}


int read_int_array(char *str, int *psize, int **pvalues,
        int (*check)(long int value))
{
    assert(str);
    int size = str_count_char(str, ' ')+1;
    int *values = (int *)malloc(sizeof(int[size]));

    *pvalues = NULL;

    char *endStr;
    for (int i = 0; i < size; i++) {
        assert(strlen(str));
        long int value = strtol(str, &endStr, 10);
        if (!check(value)) {
            exit(-1);
        }

        /* Check we have space character or null character */
        if (*endStr == ' ') {
            str++;
        } else if (*endStr != '\0') {
            exit(-1);
        }
        values[i] = (int)value;
    }

    *pvalues = values;
    *psize = size;

    return 0;
}

int read_int_2d_array(char *str, int *psize, int **psubsizes, int ***pvalues,
        int (*check)(long int value))
{
    assert(str);
    int size = str_count_char(str, ';')+1;
    int **values = (int **)malloc(sizeof(int *[size]));
    int *subsizes = (int *)malloc(sizeof(int[size]));

    int nfields = 0;
    char *token;
    while (str && (token = strsep(&str, ";"))) {

        int subsize = str_count_char(token, ' ')+1;
        int *subvalues = (int *)malloc(sizeof(int[subsize]));

        char *endStr;
        for (int i = 0; i < subsize; i++) {
            assert(strlen(token));
            long int value = strtol(token, &endStr, 10);
            if (!check(value)) {
                exit(-1);
            }

            /* Check we have space character or null character */
            if (*endStr == ' ') {
                token++;
            } else if (*endStr != '\0') {
                exit(-1);
            }
            subvalues[i] = (int)value;
        }

        values[nfields] = subvalues;
        subsizes[nfields] = subsize;
        nfields++;
    }
    assert(nfields == size);
    *psize = size;
    *psubsizes = subsizes;
    *pvalues = values;

    return 0;
}

int read_topology(xmlNode *topology_node, netloc_topology_t *topology)
{
    xmlChar *buff;
    /* Read properties */
    /* type */
    buff = xmlGetProp(topology_node, BAD_CAST "type");
    int type;
    if (read_int((char *)buff, &type, is_positive) == -1)
    {
        fprintf(stderr, "WARN: cannot read topology type.\n");
        exit(-1); // XXX TODO
    } else {
        topology->type = type;
    }

    /* ndims */
    buff = xmlGetProp(topology_node, BAD_CAST "ndims");
    int ndims;
    if (read_int((char *)buff, &ndims, is_positive) == -1)
    {
        fprintf(stderr, "WARN: cannot read topology ndims.\n");
        exit(-1); // XXX TODO
    } else {
        topology->ndims = ndims;
    }

    if (ndims) {
        /* dims */
        buff = xmlGetProp(topology_node, BAD_CAST "dims");
        int size;
        int *dims;
        if (read_int_array((char *)buff, &size, &dims,
                    is_strictly_positive) != 0
                || ndims != size)
        {
            fprintf(stderr, "WARN: cannot read topology dims.\n");
            exit(-1); // XXX TODO
        } else {
            topology->dimensions = dims;
        }

        buff = xmlGetProp(topology_node, BAD_CAST "costs");
        NETLOC_INT *costs;
        if (sizeof(NETLOC_INT) == sizeof(int)) {
            if (read_int_array((char *)buff, &size, (int **)&costs, is_positive) != 0
                    || ndims != size)
            {
                fprintf(stderr, "WARN: cannot read topology dims.\n");
                exit(-1); // XXX TODO
            }
        } else {
            if (read_long_int_array((char *)buff, &size, (long int **)&costs,
                        is_positive) != 0
                    || ndims != size)
            {
                fprintf(stderr, "WARN: cannot read topology dims.\n");
                exit(-1); // XXX TODO
            }
        }
        topology->costs = costs;
    }

    if (topology_node->children) { /* sub-toopology */
        if (xmlChildElementCount(topology_node) != 1) {
            exit(-1);
        };
        xmlNode *cur_node = topology_node->children;
        if (!strcmp("topology", (char *)cur_node->name)) {
            topology->subtopo =  netloc_topology_construct();
            read_topology(cur_node, topology->subtopo);

        } else {
            exit(-1); // XXX TODO
        }
    }


    return 0;
}

// TODO check partition value, check dims, et coords (< max_coord)
// check: if ndims == 0 then coords = NULL
