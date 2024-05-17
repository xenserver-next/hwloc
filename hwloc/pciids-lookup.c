/*
 * Copyright © 2024 Cloud Software Group
 * See COPYING in top-level directory.
 *
 * Lookup of PCI device vendor and device strings in /usr/share/hwdata/pci.ids
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define LINE_LENGTH 256

// Trim leading and trailing whitespace
static char *
hwloc_trim_str(char *str)
{
  char *end;

  while (*str == ' ')
    str++;

  if (*str == 0)
    return str;

  end = str + strlen(str) - 1; // Trim trailing space
  while (end > str && (*end == ' ' || *end == '\n' || *end == '\r'))
    end--;
  end[1] = '\0';

  return str;
}

// Lookup of PCI device vendor and device strings in /usr/share/hwdata/pci.ids
void
hwloc_pciids_lookup_device(int vendor_id, int device_id, char *buf, int bufsz)
{
  char line[LINE_LENGTH];
  int current_vendor_id, current_device_id, vendor_found = 0, written = 0;

  FILE *file = fopen("/usr/share/hwdata/pci.ids", "r");
  if (!file) {
    perror("Error opening pci.ids");
    return;
  }

  while (fgets(line, sizeof(line), file)) {
    char *trimmed_line = hwloc_trim_str(line);

    if (trimmed_line[0] == '#' || trimmed_line[0] == '\0') {
      continue;
    }

    if (!vendor_found && line[0] != '\t') {
      sscanf(trimmed_line, "%04x", &current_vendor_id);
      if (current_vendor_id == vendor_id) {
        char *space = index(trimmed_line + 6, ' ');

        if (space)
          *space = '\0';
        written += snprintf(buf, bufsz, "%s ", trimmed_line + 6);
        buf += written;
        bufsz -= written;
        vendor_found = 1;
      }
    } else if (vendor_found && line[0] == '\t' && line[1] != '\t') {
      sscanf(trimmed_line, "%04x", &current_device_id);
      if (current_device_id == device_id) {
        written += snprintf(buf, bufsz, "%s", trimmed_line + 7);
        buf += written;
        bufsz -= written;
        fclose(file);
        return;
      }
    } else if (vendor_found && line[0] != '\t')
      break;
  }
  fclose(file);
}
