# 交互与三维编辑实施记录

2026-09-08。本记录对应首批 M1，后续物理组件映射、跨层路由、自动布局尚未交付。

## 实现分工

- Claude CLI：实际启动模型为 `claude-opus-5`，负责 React 交互状态、器件支持标注、快捷键与行为回归。
- Codex：负责 Three.js 预览与剖切、拾取一致性、目录默认属性、集成审查和真实浏览器回归。
- 交互状态测试不代替浏览器实操；浏览器使用受控内核夹具的部分不代替真实 C++ 服务联调。

## 三维接口

`CircuitViewport.setPlacement(name, properties)` 复用场景方块的简化绘制代码生成透明预览。中继器、比较器的箭头表示输出方向；侦测器的蓝色箭头表示检测面；其他有 facing 的器件箭头表示朝向。现有模型仍是简化模型，不声明等同原版材质或碰撞模型。

默认属性由内核器件目录新增的可选 `defaultProperties` 提供，来自同一注册表。旧服务未提供时可使用已接收的默认状态；仍未知则保留默认属性提示，不猜测模型。真实放置仍由内核验证，预览不证明支撑、接线或物理正确性。

`setSection(axis, maximum)` 仅显示所选轴坐标不超过 maximum 的区域；none 关闭。它和编辑层剖切共同约束显示、选择框与拾取，但不删除方块、不改动工程坐标或仿真状态。空处放置使用编辑 Y 层；命中方块时在表面相邻格放置，同样受剖切与高度范围限制。

## 验证方法

前端基础回归：

```sh
npm --prefix simulator/apps/web ci
npm --prefix simulator/apps/web run build
node simulator/tests/webTests.mjs
node simulator/tests/interactionTests.mjs
```

浏览器回归要求 Playwright 可从 Node 环境解析（也可由 NODE_PATH 提供），以及已安装 Chrome。先在另一终端启动开发服务器：

```sh
npm --prefix simulator/apps/web run dev -- --port 5179 --strictPort
node simulator/tests/viewportTests.mjs
node simulator/tests/interactionBrowserTests.mjs
```

可用 VERIMC_TEST_URL 改测试地址，用 VERIMC_BROWSER 指定 Playwright 浏览器 channel。截图写入忽略的 simulator/testResults/。

原生回归在 WSL GCC 11.4、Release、CMake 3.30.5、nlohmann/json 3.12.0、Zstandard 1.5.7、OpenSSL 3.0.2 下执行，simulatorBuildServer=OFF。构建时修复了已有的 ProjectSource 不完整类型导致 GCC/JSON 重载解析失败的问题。

```sh
cmake -S simulator -B simulator/buildInteraction -DsimulatorBuildServer=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build simulator/buildInteraction -j 6
ctest --test-dir simulator/buildInteraction --output-on-failure
```

若依赖安装在非系统位置，配置时通过 CMAKE_PREFIX_PATH 指向依赖安装前缀。

## 已观测结果

- 原生 CTest：3/3 通过，核心用例 84 项和 VMCB 用例 11 项通过，另有加法器工程测试。
- 现有前端回归：8/8 通过。
- 视口浏览器回归：5/5 通过，包含负坐标、同源几何、静止指针切层、方向、三轴剖切不修改世界与截图。
- 新增交互与真实 JSX 渲染回归：28/28 通过。
- 整页 Chrome 浏览器回归：4/4 通过，使用受控内核夹具；已检查整页和放置预览截图。
- TypeScript 检查和 Vite 生产构建通过；仍有现有大 bundle 警告，未进行性能优化或帧率验收。

本轮没有运行 Minecraft 原版差分测试，也未验证完整 Windows 原生构建或 C++ 服务与浏览器的端到端文件传输。

## Minecraft 风格输入调整（2026-09-08）

默认使用放置工具：左键拆除，右键点击放置；右键拖动保留视角旋转。以整个按下期间是否移动达到 5 像素区分点击和拖动，拖出去再回到起点也不放置。选择、探针与操作工具保留左键点选，右键仍可放置。空处落在编辑层，命中方块时选取表面法线对应的相邻格，不覆盖已有方块；放置仍交由 C++ 内核校验支撑与器件状态。

画布获得焦点后，WASD 按镜头水平朝向前后左右移动，Space 上升、左右 Shift 均可下降；连续移动为 12 格/秒，合成方向归一化，俯仰不改变水平移动高度。失焦、切换页面可见状态或取消指针操作时清理输入，Ctrl/Cmd/Alt 组合键不移动镜头。中键平移与滚轮缩放保留。

Enter（含数字键盘 Enter）运行/暂停，长按不反复切换。文本控件、菜单和帮助弹窗隔离运行快捷键，按钮保留原生 Enter 激活。快捷键读取连接当前状态，避免 React 下一次渲染前使用旧运行状态。补上首次画布尺寸初始化和拾取前世界矩阵同步，场景修改后立即刷新放置预览。

验证：36 项前端测试（含 28 项交互/JSX 测试）、12 项 Chrome 浏览器测试（7 项视口、5 项整页）全部通过；TypeScript 与 Vite 生产构建通过。检查了整页截图，既有 bundle 体积提示仍存在。本轮浏览器测试使用受控内核夹具，验证输入路由与相机行为，没有改动 C++ 器件机制，也未新增 Minecraft 差分验证。
