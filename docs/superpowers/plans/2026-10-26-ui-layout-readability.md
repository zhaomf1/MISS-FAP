# UI 布局可读性重构实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 重构主界面布局代码，让绘制区域、状态映射和按键焦点清晰可读，并消除操作图标重叠。

**Architecture:** 保留单个 `ui_layout.c` 和 `ui.h` 公共入口，在文件内部用具名区域/焦点和区域级创建、刷新函数拆分职责。保持现有屏幕布局、文案和命令语义，图标按焦点和可用状态控制显示。

**Tech Stack:** C、LVGL、Keil ARMCC 5、PowerShell 静态检查。

**Spec:** `docs/superpowers/specs/2026-10-26-ui-layout-readability-design.md`

## Global Constraints
- 屏幕保持 320x240，既有分区、文案和命令含义不变。
- 保持 `ui_layout_init()`、`ui_layout_ready()`、`ui_layout_key()`、`ui_layout_process()` 签名不变。
- 不改温控、电机、按键硬件行为及 Keil 工程源文件清单。
- 中文注释说明页面绘制、交互和各运行状态的显示映射。

## Review Focus
- 初始化失败时不得继续操作空控件；审阅 `s_ok` 检查边界。
- 编辑态切换焦点或状态失效时，编辑光标和焦点框需同步更新。
- 开始、停止、舱门、消音图标必须不重叠且切换时不残留。
- 状态文字、颜色和进度条保持旧行为；对照重构前逻辑。
- Keil ARMCC 5 兼容性；运行 UI 静态检查及可用的编译检查。

---

### Task 1: 布局常量、页面创建和图标
**Files:** Modify `Core/Src/app/ui_layout.c`; Test `tests/verify_ui_layout_structure.ps1`
- [ ] 先添加静态回归检查：要求具名焦点、区域创建函数和独立图标位置，并确认图标槽互不覆盖；运行并观察预期失败。
- [ ] 定义具名区域与七个焦点编号，替换布局/焦点裸索引和坐标。
- [ ] 拆分框架、顶部栏、温度区、计时区、延迟降温区和右侧操作区创建函数。
- [ ] 图标槽位独立放置，加入中文几何注释。

### Task 2: 输入和状态刷新拆分
**Files:** Modify `Core/Src/app/ui_layout.c`
- [ ] 将按键事件路由到温度、时间、延迟控件各自的编辑入口；开始、停止、舱门和消音各自提交应用命令。
- [ ] 将显示刷新拆成温度设定/温度值、舱门状态、阶段状态、孵育时间、延迟降温和四个操作卡片的独立函数。
- [ ] 用中文注释说明空闲、运行、延迟、降温、保存、完成、错误、报警的显示映射和优先级。
- [ ] 保证所有 `render_*` 函数只更新 UI，所有命令提交仅出现在控件交互入口中。
- [ ] 图标按当前焦点和可操作状态显示；停止符号为直角空心方形。

### Task 3: 验证
**Files:** Verify `Core/Src/app/ui_layout.c`, `Core/Inc/app/ui.h`, `MDK-ARM/mini_evol/mini_evol.uvprojx`, `tests/verify_ui_layout_structure.ps1`
- [ ] 运行新静态回归检查和已有 UI 静态检查。
- [ ] 运行可用的主机 C 语法检查或 Keil 工程构建；工具缺失时记录。
- [ ] 对照公共签名和工程源文件清单，检查最终差异仅涉及本次目标文件。
