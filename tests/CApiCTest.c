/* The C API from C (#203): hydroid_res.h compiles as C, and a C program links the library and
 * reads a value with it. The argument is the golden table/res; CApiTest.cpp checks the rest. */

#include <stdio.h>
#include <string.h>

#include "hydroid_res.h"

static int failures = 0;

static void check(int ok, const char* what) {
  if (!ok) {
    fprintf(stderr, "failed: %s\n", what);
    failures++;
  }
}

int main(int argc, char** argv) {
  const char* package = "com.itechnika.hydroid.table";
  const char* locales[] = {"ko-KR"};
  hydroid_res_config device;
  hydroid_res_value value;
  hydroid_res_table* table;
  hydroid_res_resources* resources;
  uint32_t id;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <table/res>\n", argv[0]);
    return 2;
  }
  check(strcmp(hydroid_res_version(), HYDROID_RES_VERSION) == 0, "the version");
  table = hydroid_res_table_create();
  if (!table) return 1;
  if (!hydroid_res_table_add_package(table, argv[1], package, 0x7f)) {
    fprintf(stderr, "%s", hydroid_res_table_error(table));
    hydroid_res_table_destroy(table);
    return 1;
  }
  resources = hydroid_res_resources_create(table);
  if (!resources) return 1;

  memset(&device, 0, sizeof device);
  device.orientation = 2; /* land */
  check(hydroid_res_resources_set_configuration(resources, &device, locales, 1) == 1,
        "set_configuration");
  id = hydroid_res_table_find(table, "@string/config_order", package);
  check(id != 0, "find");
  check(hydroid_res_resources_value(resources, id, &value, NULL) == 1, "value");
  check(value.kind == HYDROID_RES_VALUE_STRING, "kind");
  check(value.string != NULL && strcmp(value.string, "ko-rKR") == 0, "string");
  check(value.string_length == 6, "string_length");
  check(value.source == NULL && value.source_length == 0, "no source");

  /* A file, and where it was read. */
  id = hydroid_res_table_find(table, "@raw/data", package);
  check(id != 0, "find a file");
  check(hydroid_res_resources_value(resources, id, &value, NULL) == 1, "value of a file");
  check(value.kind == HYDROID_RES_VALUE_FILE, "kind of a file");
  check(value.source != NULL && strlen(value.source) == value.source_length, "source");
  /* .../raw/data.bin, or ...\raw\data.bin on Windows. */
  check(value.source_length > 12 &&
            strcmp(value.source + value.source_length - 8, "data.bin") == 0 &&
            strncmp(value.source + value.source_length - 12, "raw", 3) == 0,
        "source of raw/data.bin");

  hydroid_res_resources_destroy(resources);
  hydroid_res_table_destroy(table);
  return failures == 0 ? 0 : 1;
}
