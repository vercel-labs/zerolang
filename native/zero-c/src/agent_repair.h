#ifndef ZERO_AGENT_REPAIR_H
#define ZERO_AGENT_REPAIR_H

#include "zero.h"

typedef struct {
  int line;
  char *old_line;
  char *new_line;
} ZAgentRepairEdit;

bool z_agent_repair_can_apply(const ZDiag *diag, const char *fix_safety);
bool z_agent_repair_find_edit(const char *source, const ZDiag *diag, ZAgentRepairEdit *edit);
char *z_agent_repair_apply_single_line_edit(const char *source, int target_line, const char *new_line);
void z_agent_repair_edit_free(ZAgentRepairEdit *edit);

#endif
