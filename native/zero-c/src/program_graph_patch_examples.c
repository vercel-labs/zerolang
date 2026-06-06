#include "program_graph_patch.h"

static const char *const graph_patch_operation_examples[] = {
  "expect graphHash \"graph:a7f7e6899a73f3b4\"",
  "set node=\"#id\" field=\"value\" expect=\"old\" value=\"new\"",
  "insert node=\"#id\" kind=\"Literal\" parent=\"#parent\" edge=\"arg\" order=\"0\" type=\"String\" value=\"text\"",
  "insertEdge from=\"#from\" to=\"#to\" edge=\"arg\" target=\"node\" order=\"0\"",
  "replace node=\"#id\" expect=\"nodehash:abc123\" kind=\"Literal\" type=\"String\" value=\"text\"",
  "delete node=\"#id\" expect=\"nodehash:abc123\"",
  "delete node=\"#id\"",
  "rename node=\"#id\" expect=\"old\" value=\"new\"",
  "addMain",
  "addCheckWrite fn=\"main\" text=\"hello\\n\"",
  "addFunction name=\"add\" ret=\"i32\"",
  "addParam fn=\"add\" name=\"left\" type=\"i32\"",
  "addReturnBinary fn=\"add\" name=\"+\" left=\"left\" right=\"right\" type=\"i32\"",
  "addLetLiteral fn=\"main\" name=\"count\" type=\"u32\" value=\"0\"",
  "addLetBinary fn=\"add\" name=\"sum\" type=\"i32\" operator=\"+\" left=\"left\" right=\"right\"",
  "addReturnValue fn=\"identity\" value=\"input\" type=\"i32\"",
  "addCheckWriteValue fn=\"main\" value=\"message\" type=\"String\"",
  "addTest name=\"addition works\" call=\"add\" arg0=\"40\" arg1=\"2\" expect=\"42\" type=\"i32\"",
  "setMainArgsAddCli fn=\"add_u32\"",
  NULL,
};

const char *const *z_program_graph_patch_operation_examples(void) {
  return graph_patch_operation_examples;
}
