#include "zero.h"

#include <stdio.h>
#include <stdlib.h>

void z_backend_blocker_set(ZBackendBlocker *blocker,
                           const char *target,
                           const char *object_format,
                           const char *backend,
                           const char *stage,
                           const char *unsupported_feature) {
  if (!blocker) return;
  *blocker = (ZBackendBlocker){0};
  blocker->present = true;
  snprintf(blocker->target, sizeof(blocker->target), "%s", target ? target : "");
  snprintf(blocker->object_format, sizeof(blocker->object_format), "%s", object_format ? object_format : "");
  snprintf(blocker->backend, sizeof(blocker->backend), "%s", backend ? backend : "");
  snprintf(blocker->stage, sizeof(blocker->stage), "%s", stage ? stage : "");
  snprintf(blocker->unsupported_feature, sizeof(blocker->unsupported_feature), "%s", unsupported_feature ? unsupported_feature : "");
}

bool z_write_binary_file(const char *path, const unsigned char *data, size_t len, ZDiag *diag) {
  FILE *file = fopen(path, "wb");
  if (!file) {
    if (diag) snprintf(diag->message, sizeof(diag->message), "failed to write binary test file");
    return false;
  }
  size_t wrote = fwrite(data ? data : (const unsigned char *)"", 1, len, file);
  fclose(file);
  return wrote == len;
}

void z_free_source(SourceInput *input) {
  if (!input) return;
  free(input->source_file);
  free(input->source);
  for (size_t i = 0; i < input->source_file_count; i++) free(input->source_files[i]);
  free(input->source_files);
  for (size_t i = 0; i < input->module_count; i++) {
    free(input->module_names[i]);
    free(input->module_paths[i]);
  }
  free(input->module_names);
  free(input->module_paths);
  for (size_t i = 0; i < input->symbol_count; i++) {
    free(input->symbol_modules[i]);
    free(input->symbol_kinds[i]);
    free(input->symbol_names[i]);
  }
  free(input->symbol_modules);
  free(input->symbol_kinds);
  free(input->symbol_names);
  *input = (SourceInput){0};
}
