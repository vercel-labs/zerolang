#include "package.h"
#include "keys.h"
#include "ledger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* cli_lib_text = "pub fun greeting_code() -> i32 {\n"
                                  "    return 42\n"
                                  "}\n";

static const char* cli_main_text = "use lib\n\n"
                                   "pub fun main(world: World) -> Void raises {\n"
                                   "    if greeting_code() == 42 {\n"
                                   "        check world.out.write(\"hello from zero\\n\")\n"
                                   "    }\n"
                                   "}\n\n"
                                   "test \"greeting is stable\" {\n"
                                   "    expect(greeting_code() == 42)\n"
                                   "}\n";

static const char* cli_readme_text =
    "# Zero CLI\n\n"
    "This project was created with `zero new cli`.\n\n"
    "Try:\n\n"
    "```sh\n"
    "zero check .\n"
    "zero test .\n"
    "zero run .\n"
    "zero dev --json .\n"
    "zero build --target linux-musl-x64 --out .zero/out/app .\n"
    "zero ship --target linux-musl-x64 --out .zero/ship/app .\n"
    "```\n\n"
    "The entry point receives `World` explicitly, so I/O is visible in the function signature. The generated output is "
    "deterministic and the manifest records the default release target.\n";

static const char* lib_lib_text = "pub fun add_one(value: i32) -> i32 {\n"
                                  "    return value + 1\n"
                                  "}\n\n"
                                  "test \"public api works\" {\n"
                                  "    expect(add_one(41) == 42)\n"
                                  "}\n";

static const char* lib_readme_text =
    "# Zero Library\n\n"
    "This small package exposes one public function, docs metadata in `zero.json`, and an inline test.\n\n"
    "Try:\n\n"
    "```sh\n"
    "zero check .\n"
    "zero test .\n"
    "zero dev --json .\n"
    "zero graph --json .\n"
    "zero doc --json .\n"
    "```\n";

static const char* pkg_model_text = "pub shape Point {\n"
                                    "    value: i32,\n"
                                    "}\n";

static const char* pkg_math_text = "fun base(value: i32) -> i32 {\n"
                                   "    return value\n"
                                   "}\n\n"
                                   "pub fun add_one(value: i32) -> i32 {\n"
                                   "    return base(value) + 1\n"
                                   "}\n";

static const char* pkg_main_text = "use math\n"
                                   "use model\n\n"
                                   "pub fun main(world: World) -> Void raises {\n"
                                   "    let point = Point { value: add_one(41) }\n"
                                   "    if point.value == 42 {\n"
                                   "        check world.out.write(\"package ok\\n\")\n"
                                   "    }\n"
                                   "}\n\n"
                                   "test \"package import works\" {\n"
                                   "    let point = Point { value: add_one(41) }\n"
                                   "    expect(point.value == 42)\n"
                                   "}\n";

static const char* pkg_readme_text =
    "# Zero Package\n\n"
    "This template shows package-local imports, one public symbol, and one private helper.\n\n"
    "Try:\n\n"
    "```sh\n"
    "zero check .\n"
    "zero test .\n"
    "zero run .\n"
    "zero dev --json .\n"
    "zero build --target linux-musl-x64 --out .zero/out/app .\n"
    "zero ship --target linux-musl-x64 --out .zero/ship/app .\n"
    "zero graph --json .\n"
    "```\n";

static const char* gitignore_text = ".zero/\n";

static bool package_file_write(const char* root, const char* relative, const char* text, ZDiag* diag) {
  char* path = z_path_join(root, relative);
  bool ok = z_write_file(path, text, diag);
  free(path);
  return ok;
}

static bool package_buf_write(const char* root, const char* relative, ZBuf* buf, ZDiag* diag) {
  bool ok = package_file_write(root, relative, buf->data ? buf->data : "", diag);
  zbuf_free(buf);
  return ok;
}

static bool package_manifest_append(ZBuf* buf, const char* name, const char* main_path) {
  zbuf_append(buf, "{\n");
  zbuf_append(buf, "  \"package\": {\n");
  zbuf_append(buf, "    \"name\": ");
  z_json_string_append(buf, name);
  zbuf_append(buf, ",\n    \"version\": \"0.1.0\",\n    \"license\": \"MIT\"\n");
  zbuf_append(buf, "  },\n");
  zbuf_append(buf, "  \"targets\": {\n");
  zbuf_append(buf, "    \"cli\": {\n");
  zbuf_append(buf, "      \"kind\": \"exe\",\n");
  zbuf_append(buf, "      \"main\": ");
  z_json_string_append(buf, main_path);
  zbuf_append(buf, ",\n      \"defaultTarget\": \"linux-musl-x64\",\n      \"devTarget\": \"host\",\n      "
                   "\"releaseProfile\": \"release-small\"\n    }\n");
  zbuf_append(buf, "  },\n");
  zbuf_append(buf, "  \"deps\": {},\n");
  zbuf_append(buf, "  \"profiles\": {\n");
  zbuf_append(buf, "    \"dev\": { \"inherits\": \"dev\" },\n");
  zbuf_append(buf, "    \"release-small\": { \"inherits\": \"release-small\" }\n");
  zbuf_append(buf, "  },\n");
  zbuf_append(buf, "  \"docs\": {\n");
  zbuf_append(buf, "    \"readme\": \"README.md\",\n");
  zbuf_append(buf, "    \"examples\": [");
  z_json_string_append(buf, main_path);
  zbuf_append(buf, "]\n");
  zbuf_append(buf, "  }\n");
  zbuf_append(buf, "}\n");
  return true;
}

static bool package_scaffold_cli(const char* root, const char* name, ZDiag* diag) {
  ZBuf manifest;
  zbuf_init(&manifest);
  package_manifest_append(&manifest, name, "src/main.0");
  if (!package_buf_write(root, "zero.json", &manifest, diag)) return false;
  if (!package_file_write(root, "src/lib.0", cli_lib_text, diag)) return false;
  if (!package_file_write(root, "src/main.0", cli_main_text, diag)) return false;
  if (!package_file_write(root, "README.md", cli_readme_text, diag)) return false;
  return package_file_write(root, ".gitignore", gitignore_text, diag);
}

static bool package_scaffold_lib(const char* root, const char* name, ZDiag* diag) {
  ZBuf manifest;
  zbuf_init(&manifest);
  package_manifest_append(&manifest, name, "src/lib.0");
  if (!package_buf_write(root, "zero.json", &manifest, diag)) return false;
  if (!package_file_write(root, "src/lib.0", lib_lib_text, diag)) return false;
  if (!package_file_write(root, "README.md", lib_readme_text, diag)) return false;
  return package_file_write(root, ".gitignore", gitignore_text, diag);
}

static bool package_scaffold_app(const char* root, const char* name, ZDiag* diag) {
  ZBuf manifest;
  zbuf_init(&manifest);
  package_manifest_append(&manifest, name, "src/main.0");
  if (!package_buf_write(root, "zero.json", &manifest, diag)) return false;
  if (!package_file_write(root, "src/model.0", pkg_model_text, diag)) return false;
  if (!package_file_write(root, "src/math.0", pkg_math_text, diag)) return false;
  if (!package_file_write(root, "src/main.0", pkg_main_text, diag)) return false;
  if (!package_file_write(root, "README.md", pkg_readme_text, diag)) return false;
  return package_file_write(root, ".gitignore", gitignore_text, diag);
}

static bool package_scaffold_template(const char* root, const char* name, PackageTemplate template_kind, ZDiag* diag) {
  if (template_kind == PACKAGE_TEMPLATE_CLI) return package_scaffold_cli(root, name, diag);
  if (template_kind == PACKAGE_TEMPLATE_LIB) return package_scaffold_lib(root, name, diag);
  if (template_kind == PACKAGE_TEMPLATE_APP) return package_scaffold_app(root, name, diag);
  return false;
}

static bool package_diag(ZDiag* diag, const char* message) {
  diag->code = 9002;
  diag->line = 1;
  diag->column = 1;
  snprintf(diag->message, sizeof(diag->message), "%s", message);
  snprintf(diag->help, sizeof(diag->help), "check ledger creation");
  return false;
}

static bool ledger_keys_from_use(LedgerKeys* keys, const KeyUse* use, ZDiag* diag) {
  keys->threshold = 1;
  keys->publisher_count = 1;
  keys->publishers = calloc(1, sizeof(LedgerPublisher));
  if (!keys->publishers) return package_diag(diag, "ledger keys allocation failed");
  keys->publishers[0].key_id = z_strdup(use->id_hex ? use->id_hex : "");
  keys->publishers[0].public_key = z_strdup(use->public_hex ? use->public_hex : "");
  keys->publishers[0].label = z_strdup(use->label ? use->label : "");
  keys->publishers[0].name = z_strdup(use->name ? use->name : "");
  keys->publishers[0].email = z_strdup(use->email ? use->email : "");
  return true;
}

bool package_create(const char* root, const char* name, PackageTemplate template_kind, PackageKeyNote* note,
                    ZDiag* diag) {
  if (!package_scaffold_template(root, name, template_kind, diag)) return false;
  KeyUse use = {0};
  if (!keys_active_or_create(&use, diag)) return false;
  if (!z_trusted_keys_allow_key(&use, diag)) {
    keys_use_free(&use);
    return false;
  }
  LedgerKeys keys = {0};
  if (!ledger_keys_from_use(&keys, &use, diag)) {
    keys_use_free(&use);
    return false;
  }
  bool ok = ledger_write_new(root, name, &keys, &use, diag);
  ledger_keys_free(&keys);
  if (!ok) {
    keys_use_free(&use);
    return false;
  }
  if (note) {
    note->key_id = z_strdup(use.id_hex ? use.id_hex : "");
    note->key_dir = z_strdup(use.dir ? use.dir : "");
    note->public_key = z_strdup(use.public_hex ? use.public_hex : "");
    note->key_created = use.created;
  }
  keys_use_free(&use);
  return true;
}

void package_key_note_free(PackageKeyNote* note) {
  if (!note) return;
  free(note->key_id);
  free(note->key_dir);
  free(note->public_key);
  note->key_id = NULL;
  note->key_dir = NULL;
  note->public_key = NULL;
  note->key_created = false;
}
