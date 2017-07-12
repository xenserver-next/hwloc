/*
 * Copyright © 2013-2014 University of Wisconsin-La Crosse.
 *                         All rights reserved.
 * Copyright © 2016-2017 Inria.  All rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 * See COPYING in top-level directory.
 *
 * $HEADER$
 */

#define _GNU_SOURCE              /* for asprintf() */

#include <private/netloc.h>
#include <netloc.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <netloc/uthash.h>
#include <netloc/utarray.h>
#include <dirent.h>
#include <libgen.h> // for dirname

char *netloc_line_get_next_token(char **string, char c)
{
    char *field;
    char *string_end;

    if (!*string)
        return NULL;

    string_end = strchr(*string, c);

    if (string_end) {
        string_end[0] = '\0';
        field = *string;
        *string = string_end+1;
    } else {
        field = *string;
        *string = NULL;
    }

    return field;
}

ssize_t netloc_line_get(char **lineptr, size_t *n, FILE *stream)
{
    ssize_t read = getline(lineptr, n, stream);
    if (read == -1)
        return -1;

    /* Remove last \n character */
    char *line = *lineptr;
    size_t lastpos = strlen(line)-1;
    if (line[lastpos] == '\n') {
        line[lastpos] = '\0';
        read--;
    }
    return read;
}

/** Read with libxml */

#if defined(HWLOC_HAVE_LIBXML2)

xmlDoc *netloc_xml_reader_init(char *path)
{
    int ret = NETLOC_SUCCESS;
    xmlDoc *doc = NULL;

    /*
     * This initialize the library and check potential ABI mismatches
     * between the version it was compiled for and the actual shared
     * library used.
     */
    LIBXML_TEST_VERSION;

    doc = xmlReadFile(path, "UTF-8", 0);
    if (!doc) {
        fprintf(stderr, "Cannot open topology file %s\n", path);
        perror("xmlReadFile");
    }

    return doc;
}

int netloc_xml_reader_clean_and_out(xmlDoc *doc)
{
    /* Free the document */
    xmlFreeDoc(doc);

    /*
     * Free the global variables that may
     * have been allocated by the parser.
     */
    xmlCleanupParser();

    return NETLOC_SUCCESS;
}

#else
void *netloc_xml_reader_init(char *path __netloc_attribute_unused);
{
    /* nothing to do here */
    return NULL;
}
void netloc_xml_reader_clean_and_out(void *doc __netloc_attribute_unused)
{
    /* nothing to do here */
    return;
}
#endif
