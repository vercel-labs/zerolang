# zerolang

zerolang 是一个实验性的 graph-first 编程系统。人类仍然编写和审查可读的 `.0` 源码，AI Agent 通过编译器导出的 ProgramGraph、结构化命令契约、proof receipt policy 和 rollback/proof ledger 来理解、修改、修复、重构、构建程序。

[English README](README.md)

源码仍然是事实来源；ProgramGraph 是从源码派生出来的、由编译器拥有的语义接口。目标不是再造一门普通语言，而是把编译器变成 Agent 编程协议：每一步都能被校验、审计和回滚，让 Agent 修改 zerolang 项目时比修改普通 TypeScript/Rust/C 文本项目更可靠、更省 token、更少 hallucination，也更容易自动修复。

> **安全状态**
>
> zerolang 还不是生产可用系统。请预期存在安全问题，不要用于敏感数据、可信基础设施或生产环境。开发和试验时应在隔离、可丢弃的环境中运行。

## 项目定位

当前系统围绕 Agent-native 可验证构建流程组织：

- 人类写稳定、可读、可审查的 `.0` 源码。
- 编译器从源码生成带类型、效果、能力、模块、节点 ID、节点 hash 和图 hash 的 ProgramGraph。
- Agent 使用图查询、诊断、修复计划和命令契约收集上下文，而不是整文件猜测。
- Agent 提交带 graph hash、node ID 和 `expect` 前置条件的语义编辑，让编译器验证后再写回源码或派生 artifact。
- 构建、测试、发布、清理、回滚等步骤通过结构化 JSON 输出暴露 `agentCommand`、read/write policy、verification commands、proof ledger 和 rollback 字段。
- 本地 gate 和 completion audit 会把这些能力映射到目标级 requirements，形成可重复验证的证据链。

## ProgramGraph

普通文本 patch 对 Agent 来说信息太弱：它需要猜测符号引用是否相关、上下文是否过期、调用是否解析到目标函数，以及编辑是否保持类型、能力、导入和目标平台约束。

ProgramGraph 把这些事实变成编译器输出：

```bash
zero graph dump examples/hello.0
```

图中包含稳定节点 ID、`graphHash`、节点类型、解析后的符号、边、能力事实、helper 使用和模块关系。Agent 可以从诊断、符号、调用点、能力或节点 ID 开始，只请求附近的语义切片，而不是把大量源码塞进上下文。

## Agent 原生工作流

常用 Agent 入口都提供 `--json` 结构化输出：

```bash
zero agent protocol --json
zero skills list --json
zero skills get language --json
zero check --json examples/hello.0
zero parse --json examples/hello.0
zero graph --json examples/systems-package
zero graph find --json --symbol main examples/hello.0
zero graph slice --json --node <node-id> examples/hello.0
zero graph patch --json examples/hello.0 <patch-file>
zero build --json examples/hello.0
zero test --json examples/hello.0
zero size --json examples/hello.0
zero doctor --json
```

这些命令的重点不是漂亮输出，而是给 Agent 稳定字段：

- `agentCommand.command.argv`：可重放的规范 argv。
- `agentCommand.readPolicy` / `writePolicy`：说明命令是否读源码、写源码、写 artifact、是否需要验证和回滚。
- `agentCommand.verificationCommands[]`：必须重放的证明命令。
- `proofResultFields[purpose]`：协议 manifest 按证明目的声明应记录的结果字段。
- `proofReceiptPolicy.requiredFields`：每条 proof receipt 必须记录 purpose、argv、退出状态、声明字段、实际观察字段和重放时间。
- `agentCommand.recommendedNextCommands[]`：失败、诊断、构建后或图 artifact 后的结构化下一步命令。
- `graphHash`、`nodeHash`、`moduleIdentity`、`saved.path`、`artifactHash`、`artifactBytes`：用于审计和 stale-context 检测的身份字段。
- `agentTransaction.proofLedger` 和 `rollback`：语义编辑、修复、保存、验证、回滚的阶段化证据。

## Checked Graph Edit

支持的语义编辑通过 `zero graph patch` 执行。Agent 不直接改行号，而是给出图 hash、节点 ID、字段和预期旧值：

```bash
zero graph patch examples/hello.0 \
  --expect-graph-hash graph:b8a019041020df03 \
  --op 'set node="#610c78bf" field="value" expect="hello from zero\n" value="hello graph\n"'
```

编译器会拒绝过期 graph hash 和不匹配的 `expect` 值。成功路径会验证、保存、格式化、重读并检查结果；JSON 事务里包含 proof ledger、verification commands 和 rollback 信息。

当前语义编辑面覆盖 17 个操作、5 类编辑，包括 rename、callee replacement、import edit、function/param edit 和 type change。失败路径会返回结构化 retry commands，而不是让 Agent 从错误文本里猜恢复步骤。

## 当前 Agent 协议能力

当前仓库已经把多条 Agent 编程链路接入本地 gate：

- `zero check --json` 诊断包含结构化修复计划和 graph-inspect 后续命令。
- `zero tokens --json`、`zero parse --json`、`zero graph find/impact/slice --json` 支持逐级升级上下文。
- `zero graph find/impact/slice --json` 提供 token-bounded 图查询，并保留 target/profile 上下文。
- `zero graph patch --json` 支持带图 hash、节点 ID、`expect` 和 proof ledger 的 checked graph edit。
- `zero fix --plan/--patch/--apply --json` 暴露 typed repair plan、compiler patch、事务证明和回滚边界。
- `zero build/test/size/ship --json` 暴露 artifact 身份、验证命令、安全事实、缓存事实和生产就绪风险。
- `zero graph import/view/roundtrip/validate/check/size/build/ship --json` 覆盖 ProgramGraph artifact 的导入、查看、验证、构建、测试、发布链路。
- `zero agent protocol --json` 暴露 35 个 proof purpose、`proofResultFields` 目录、`proofReceiptPolicy`、`permissionModel`、`objectiveContract` 和本地 gate。

## 本地验证

从干净 checkout 开始开发时，需要先构建本地 native compiler：

```bash
make -C native/zero-c
```

常用验证命令：

```bash
pnpm run docs:test
pnpm run conformance
pnpm run native:test
pnpm run command-contracts
pnpm run agent:contracts
```

Agent 协议完整 gate 可以直接运行：

```bash
node --experimental-strip-types --disable-warning=ExperimentalWarning scripts/agent-contracts-gate.mts --summary-only
```

目标级 completion audit 可以运行：

```bash
node --experimental-strip-types --disable-warning=ExperimentalWarning scripts/agent-completion-audit.mts
```

它会读取编译器 manifest 中的 `objectiveContract`，重放本地 gate，并逐项报告 12 个目标 requirement 对应的 gate summary 证据。当前目标审计覆盖：

- 可读源码
- 语义图理解
- 编译器介导修改
- 编译器介导修复
- 重构操作面
- 可验证构建和测试
- 可审计 proof receipts
- 已证明回滚
- token-efficient 图协议
- hallucination resistance
- 自动修复到 build
- 协议表面，而不只是一门语言

## 快速开始

安装发布版本：

```bash
curl -fsSL https://zerolang.ai/install.sh | bash
export PATH="$HOME/.zero/bin:$PATH"
zero --version
```

检查程序：

```bash
zero check examples/hello.0
```

运行小程序：

```bash
zero run examples/add.0
```

如果需要机器可读的运行前审计，优先使用：

```bash
zero build --json examples/add.0
```

`zero run --json` 目前会返回结构化诊断，并给出需要走 artifact validation 的推荐后续命令。

## 兼容性策略

zerolang 仍然处于实验阶段，并且会优先选择一个当前语法和一个规范格式，而不是维护兼容层。语言、标准库、诊断、图 API 和 Agent inspection surface 都可能为了简化 Agent 使用而发生破坏性变化。
