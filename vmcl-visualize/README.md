# VMCL Visualize

VeriMC 逻辑图查看器，独立的纯前端网页。选择或拖入编译器输出的 `.vmcl`，即可探索展开后的节点、端口和连接。

文件读取、检查和自动布局均在浏览器内完成，文件不会上传。运行不需要 C++ 编译器、simulator 或业务 API。开发时用 Vite 提供静态文件；构建后可放到普通静态服务器。

## 启动

需要 Node.js 20.11+（或 22 LTS）及 npm。已在 Node.js 20.11.1 / npm 10.2.4 验证。

```sh
cd vmcl-visualize
npm ci
npm run dev
```

浏览器打开 <http://127.0.0.1:5174/>。默认加载同步计数器，也可以点击左侧其他示例。

```sh
npm test
npm run build
npm run preview
```

`build` 包含 TypeScript 检查，输出到 `dist/`；预览地址为 <http://127.0.0.1:4174/>。发布时完整复制 `dist/`，包括 assets 和 examples。资源使用相对地址，支持子目录托管。请通过 HTTP(S) 打开，不直接双击 `index.html`：浏览器的模块与 Worker 需要同源资源加载。无需配置 SPA 路由回退。

## 使用

- **打开文件**：右上角选择 `.vmcl`，或把单个文件拖入网页。重新选择同一个文件也可刷新其内容。
- **探索结构**：拖动画布、滚轮平移、使用缩放按钮或触控板捏合；节点可拖动。`F` 适应全部节点。
- **定位**：按 `/` 搜索名称、操作、类型、源文件或 `#ID`；点击展开层级聚焦对应实例和 generate 作用域。
- **查看连接**：点击节点，检查上游/下游一跳、寄存器 D/CLK/RST、常量、类型和源码位置；点击连线检查驱动范围。`Esc` 清除聚焦。
- **调整显示**：切换位宽标签、时钟/复位线；“重新自动布局”恢复原始自动布局并清除手动位置。
- **导出**：下载完整图的 SVG，包含自动布局的所有节点和连线。导出不包含临时聚焦、隐藏控制线或手动拖动；不会修改或回写 `.vmcl`。

左侧层级用于聚焦，当前没有将子模块收起为单个黑盒的功能。顶层与子模块端口均保留为实际 IR 节点。

## 数据边界

| 层 | 文件 | 职责 |
| --- | --- | --- |
| 逻辑模型 | `src/model.ts` | 类型化的 LogicGraph、节点、连接、端口、实例与来源；不含屏幕坐标或 JSON 对象。整数常量使用 BigInt。 |
| 文件适配器 | `src/vmclReader.ts` | 严格读取 `.vmcl` v1，转换为逻辑模型；格式和规模错误显式报告。 |
| 布局适配器 | `src/layout.ts` | 将逻辑模型转换成独立 GraphLayout；ELK 负责分层布局和正交走线。 |
| 后台计算 | `src/graphWorker.ts` | 在 Worker 里解析，再启动 ELK Worker 布局，避免阻塞页面交互。取消、更换文件和超时都会终止任务。 |
| 显示/导出 | `src/graphView.tsx`、`src/app.tsx`、`src/exportSvg.ts` | React Flow 交互画布、属性面板和 SVG 输出。 |

浏览器模型是 C++ [共享逻辑 IR](../verimc/docs/logicIr.md) 的显示用投影，独立于文件编码；不是把 C++ 编译到 WebAssembly。文件协议以 [.vmcl 规范](../verimc/docs/vmclFormat.md) 为准。后续文件格式变化只应进入读取适配器。

图中的连线同时来自 `node.inputs` 和显式 `connections`。寄存器反馈保留为有向环；ELK 为画图选择层次，不改变数据方向或同步状态语义。数组与部分驱动保留各自的目标位段，不合并成一条可能误导的总线。

## 当前覆盖与限制

- 支持当前编译器 `.vmcl` v1 / 语言 0.1 的全部操作和类型，包括枚举、数组、寄存器和带符号/大整数常量。
- 检查 JSON 语法、重复/未知字段、协议版本、节点引用、操作签名、类型/位宽、端口、源码区间以及信号驱动完整性和重叠。
- **完整的按位组合环和时钟域检查仍由 `verimcCli validate file.vmcl` 完成。** 网页载入成功不表示已经执行了完整编译器验证。源码哈希仅显示，不会读取用户的源码文件重新核验。
- 当前是逻辑结构查看器，不执行电路仿真、源文件中的测试或物理验证，不生成 Minecraft 方块布局。
- 显示预算为 16 MiB 文件、3,000 节点、15,000 条总连线、1,000 万逻辑位；JSON 最深 128 层，单次处理最长 30 秒。超出时明确拒绝，不静默裁剪。这些是保护预算，不代表最大规模已有性能保证。
- 导入失败保留原图；关闭或刷新页面不会保存图的位置和选择状态。

## 示例来源

四个文件由本仓库 `verimcCli` 编译真实源文件生成，不是手工画出的展示数据。以下命令在仓库根目录运行（更新已有示例时明确使用 `--force`）：

```sh
verimc/build/verimcCli compile verimc/examples/valid/counter.vmc --top Counter -o vmcl-visualize/public/examples/counter.vmcl --force
verimc/build/verimcCli compile verimc/examples/valid/adder.vmc --top Adder -o vmcl-visualize/public/examples/adder.vmcl --force
verimc/build/verimcCli compile verimc/examples/valid/rippleAdder.vmc --top RippleAdder -o vmcl-visualize/public/examples/rippleAdder.vmcl --force
verimc/build/verimcCli compile verimc/examples/valid/valueOperations.vmc --top SignalArray -o vmcl-visualize/public/examples/signalArray.vmcl --force
```

测试说明见 [验证记录](docs/verification.md)。主要第三方依赖：React / React Flow（MIT）、ELK.js（EPL-2.0）、jsonc-parser（MIT）、Lucide（ISC）。依赖版本锁定于 `package-lock.json`；构建时收集生产依赖的原始许可到 `public/thirdPartyNotices.txt`，随静态资源一起发布。工具说明见 [React Flow 文档](https://reactflow.dev/learn) 和 [ELK.js 源码与许可](https://github.com/kieler/elkjs)。
