#include "program_graph_report.h"

#include "program_graph_format.h"
#include "program_graph_resolve.h"
#include "program_graph_semantics.h"
#include "std_sig.h"

#include <string.h>

static void graph_report_capability_set(CapabilitySummary *caps, const char *capability) {
  if (!caps || !capability) return;
  if (strcmp(capability, "args") == 0) caps->args = true;
  else if (strcmp(capability, "env") == 0) caps->env = true;
  else if (strcmp(capability, "fs") == 0) caps->fs = true;
  else if (strcmp(capability, "memory") == 0) caps->memory = true;
  else if (strcmp(capability, "alloc") == 0) {
    caps->alloc = true;
    caps->memory = true;
  } else if (strcmp(capability, "path") == 0) caps->path = true;
  else if (strcmp(capability, "codec") == 0) caps->codec = true;
  else if (strcmp(capability, "parse") == 0) caps->parse = true;
  else if (strcmp(capability, "time") == 0) caps->time = true;
  else if (strcmp(capability, "rand") == 0) caps->rand = true;
  else if (strcmp(capability, "net") == 0) caps->net = true;
  else if (strcmp(capability, "proc") == 0) caps->proc = true;
  else if (strcmp(capability, "web") == 0) caps->web = true;
  else if (strcmp(capability, "world") == 0) caps->world = true;
}

static void graph_report_collect_capabilities_from_std_name(const char *name, CapabilitySummary *caps) {
  if (!name || !caps) return;
  const ZStdHelperInfo *helper = z_std_helper_find(name);
  if (helper) {
    graph_report_capability_set(caps, helper->capability);
    return;
  }
  if (strncmp(name, "std.args.", strlen("std.args.")) == 0) caps->args = true;
  else if (strncmp(name, "std.env.", strlen("std.env.")) == 0) caps->env = true;
  else if (strncmp(name, "std.fs.", strlen("std.fs.")) == 0) caps->fs = true;
  else if (strncmp(name, "std.path.", strlen("std.path.")) == 0) caps->path = true;
  else if (strncmp(name, "std.codec.", strlen("std.codec.")) == 0) caps->codec = true;
  else if (strncmp(name, "std.parse.", strlen("std.parse.")) == 0) caps->parse = true;
  else if (strncmp(name, "std.json.", strlen("std.json.")) == 0) caps->parse = true;
  else if (strncmp(name, "std.time.", strlen("std.time.")) == 0) caps->time = true;
  else if (strncmp(name, "std.rand.", strlen("std.rand.")) == 0) caps->rand = true;
  else if (strncmp(name, "std.proc.", strlen("std.proc.")) == 0) caps->proc = true;
  else if (strncmp(name, "std.crypto.secureRandom", strlen("std.crypto.secureRandom")) == 0) caps->rand = true;
  else if (strncmp(name, "std.crypto.", strlen("std.crypto.")) == 0) caps->codec = true;
  else if (strncmp(name, "std.net.", strlen("std.net.")) == 0) caps->net = true;
  else if (strncmp(name, "std.http.", strlen("std.http.")) == 0) caps->net = true;
  else if (strncmp(name, "std.mem.", strlen("std.mem.")) == 0) {
    caps->memory = true;
    if (strcmp(name, "std.mem.nullAlloc") == 0 ||
        strcmp(name, "std.mem.fixedBufAlloc") == 0 ||
        strcmp(name, "std.mem.arena") == 0 ||
        strcmp(name, "std.mem.allocBytes") == 0 ||
        strcmp(name, "std.mem.byteBuf") == 0 ||
        strcmp(name, "std.mem.reset") == 0 ||
        strcmp(name, "std.mem.capacity") == 0) caps->alloc = true;
  }
}

static bool graph_report_is_call_ref(const ZProgramGraphResolutionReference *ref) {
  return ref && strcmp(ref->kind ? ref->kind : "", "call") == 0;
}

CapabilitySummary z_program_graph_report_capabilities(const ZProgramGraph *graph) {
  CapabilitySummary caps = {0};
  ZProgramGraphResolutionFacts resolution;
  z_program_graph_resolution_facts_init(&resolution);
  if (!graph || !z_program_graph_collect_resolution_facts(graph, &resolution)) return caps;

  ZProgramGraphCapabilitySummary graph_caps = {0};
  z_program_graph_collect_capabilities(graph, &resolution, &graph_caps);
  caps.args = graph_caps.args;
  caps.env = graph_caps.env;
  caps.fs = graph_caps.fs;
  caps.memory = graph_caps.memory;
  caps.alloc = graph_caps.alloc;
  caps.path = graph_caps.path;
  caps.codec = graph_caps.codec;
  caps.parse = graph_caps.parse;
  caps.time = graph_caps.time;
  caps.rand = graph_caps.rand;
  caps.net = graph_caps.net;
  caps.proc = graph_caps.proc;
  caps.web = graph_caps.web;
  caps.world = graph_caps.world;

  for (size_t i = 0; i < resolution.reference_len; i++) {
    if (graph_report_is_call_ref(&resolution.references[i])) graph_report_collect_capabilities_from_std_name(resolution.references[i].qualified_name, &caps);
  }
  z_program_graph_resolution_facts_free(&resolution);
  return caps;
}

void z_program_graph_report_mark_used_std_helpers(const ZProgramGraph *graph, bool *used, size_t used_len) {
  if (!graph || !used || used_len == 0) return;
  ZProgramGraphResolutionFacts resolution;
  z_program_graph_resolution_facts_init(&resolution);
  if (!z_program_graph_collect_resolution_facts(graph, &resolution)) return;
  for (size_t i = 0; i < resolution.reference_len; i++) {
    if (!graph_report_is_call_ref(&resolution.references[i])) continue;
    int index = z_std_helper_index(resolution.references[i].qualified_name, used_len);
    if (index >= 0) used[index] = true;
  }
  z_program_graph_resolution_facts_free(&resolution);
}

const ZProgramGraph *z_program_graph_report_load_source(const char *artifact, bool repository_store, ZProgramGraphReportLoad *loaded) {
  if (loaded) *loaded = (ZProgramGraphReportLoad){0};
  if (!artifact || !artifact[0] || !loaded) return NULL;
  ZDiag diag = {0};
  if (repository_store) {
    if (!z_program_graph_store_load_path(artifact, &loaded->store, &diag)) return NULL;
    loaded->store_loaded = true;
    return &loaded->store.graph;
  }
  if (!z_program_graph_load(artifact, &loaded->graph, &diag)) return NULL;
  loaded->graph_loaded = true;
  return &loaded->graph;
}

void z_program_graph_report_load_free(ZProgramGraphReportLoad *loaded) {
  if (!loaded) return;
  if (loaded->store_loaded) z_program_graph_store_free(&loaded->store);
  if (loaded->graph_loaded) z_program_graph_free(&loaded->graph);
  *loaded = (ZProgramGraphReportLoad){0};
}
