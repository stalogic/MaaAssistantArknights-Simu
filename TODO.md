# MAA-Simu TODO

## 肉鸽模式 RL/VLA 模拟器改造

### 当前架构

MAA 肉鸽模式的核心循环天然适合 RL：**截图 → 感知 → 决策 → 执行 → 循环**

| 层次 | 现有实现 | 文件位置 |
|------|---------|---------|
| 截图 | `Controller::get_image()` → 1280x720 BGR | `src/MaaCore/Controller/` |
| 感知 | 模板匹配 (`Matcher`) + PP-OCRv3 (`OCRer`) | `src/MaaCore/Vision/` |
| 决策 | 硬编码优先级 + JSON 配置 | `src/MaaCore/Task/Roguelike/*Plugin.cpp` |
| 执行 | ADB/Minitouch/Win32 多后端 | `src/MaaCore/Controller/` |
| 状态 | `RoguelikeConfig::m_status` (干员/藏品/希望) | `src/MaaCore/Task/Roguelike/RoguelikeConfig.h` |
| 编排 | `ProcessTask` 状态机 (task.json next 链) | `src/MaaCore/Task/ProcessTask.cpp` |
| API | C API → C# P/Invoke | `include/AsstCaller.h`, `src/MaaWpfGui/Services/MaaService.cs` |

---

## 方案对比

### 方案一：HTTP/gRPC 外部桥接（推荐起步）

**思路**：保持 MAA Core 不变，在外部构建 Python AI 推理服务，通过 HTTP/gRPC 桥接。MAA 截图→发给 AI→AI 返回动作坐标→MAA 执行。

```
┌─────────────────────────────────┐
│  MAA-Simu (C++ Core)            │
│  ┌─────────────────────────┐    │
│  │ AiBridge (HTTP Client)   │    │
│  │ - POST screenshot+state  │    │
│  │ - GET action {x,y,type}  │    │
│  └──────────┬──────────────┘    │
└─────────────┼───────────────────┘
              │ HTTP/gRPC
┌─────────────┼───────────────────┐
│  Python AI Service              │
│  ┌─────────────────────────┐    │
│  │ FastAPI Server           │    │
│  │ - /observe → state       │    │
│  │ - /act → action          │    │
│  │ - RL/VLA Model Inference │    │
│  └──────────────────────────┘    │
└──────────────────────────────────┘
```

**需要新增**：
- `src/MaaCore/AiBridge/` — libcurl HTTP 客户端
- Python FastAPI 推理服务
- `SimulatorView.xaml` — AI 控制面板 UI
- C API 扩展：`AsstSetAiEndpoint`, `AsstGetRoguelikeState`, `AsstGetScreenshotB64`

**需要改动**：每个决策插件的 `_run()` 方法，将硬编码替换为 `AiBridge::query()` 调用

| 维度 | 评分 | 说明 |
|------|------|------|
| 开发难度 | ★★☆ | Python 服务端开发快，C++ HTTP 客户端简单 |
| 模型兼容性 | ★★★ | 任何 Python 框架 (PyTorch/JAX/TensorFlow) |
| 推理延迟 | ★☆☆ | HTTP 往返 ~10-50ms，gRPC 可优化到 ~5ms |
| 训练友好性 | ★★★ | Python 端直接记录 trajectory，训练零摩擦 |
| 部署复杂度 | ★★☆ | 需要同时运行 MAA + Python 服务 |
| 调试便利性 | ★★★ | Python 端可独立调试、A/B 测试、回放 |
| 语言灵活性 | ★★★ | VLA/RL 模型全部用 Python，生态最丰富 |

---

### 方案二：直接 C++ 集成本地推理

**思路**：将 ONNX/OpenVINO/LibTorch 模型直接集成到 MaaCore 中，在原 `_run()` 方法内调用推理引擎，无需外部服务。

```
┌─────────────────────────────────────┐
│  MAA-Simu (C++ Core)                │
│  ┌────────────────────────────────┐ │
│  │ RoguelikeRecruitTaskPlugin     │ │
│  │  _run() {                      │ │
│  │    img = ctrler()->get_image() │ │
│  │    state = m_config->status()  │ │
│  │    action = model->predict()   │ │  ← 本地 ONNX 推理
│  │    ctrler()->click(action)     │ │
│  │  }                             │ │
│  └────────────────────────────────┘ │
└─────────────────────────────────────┘
```

**需要新增**：
- ONNX Runtime 推理封装（已有 `OnnxSessions`，可复用）
- VLA 模型 ONNX 导出 pipeline
- 状态序列化 → 模型输入 tensor

**需要改动**：每个决策插件直接调用本地推理

| 维度 | 评分 | 说明 |
|------|------|------|
| 开发难度 | ★★★ | C++ 模型推理、预处理、后处理工作量大 |
| 模型兼容性 | ★☆☆ | 仅支持 ONNX/OpenVINO 导出格式，VLA 多模态模型导出复杂 |
| 推理延迟 | ★★★ | 本地推理 ~1-10ms，无网络开销 |
| 训练友好性 | ★☆☆ | 需要 Python 训练 → 导出 ONNX → 部署 C++，迭代慢 |
| 部署复杂度 | ★★★ | 单进程运行，无需外部依赖 |
| 调试便利性 | ★☆☆ | C++ 调试困难，无法热更新模型 |
| 语言灵活性 | ★☆☆ | 模型必须在训练框架中导出到 ONNX，受限 |

---

## 推荐实施路径

### 第一阶段：方案一最小可行原型
1. 写一个 Python FastAPI 推理服务（先用随机策略验证链路）
2. 在 `AiBridge` 中用 libcurl 实现 HTTP POST
3. 在 `RoguelikeRecruitTaskPlugin` 中接入 AI 决策
4. 在 Simulator 标签页添加 AI 控制开关
5. 验证端到端：截图 → Python AI → 招募动作

### 第二阶段：完整接入
1. 扩展 AI 桥接到所有决策插件（战斗/购物/遭遇/路线）
2. `RoguelikeConfig::m_status` 完整 JSON 序列化
3. 替换真实 RL/VLA 模型

### 第三阶段：训练循环
1. Episode 管理（自动重置/loop）
2. Trajectory 记录（state, action, reward, next_state, done）
3. 接入离线 RL 训练 pipeline

---

## VLA 模型接口设计（草案）

### 输入 (Observation)
```json
{
  "screenshot": "<base64 png>",
  "state": {
    "theme": "Sarkaz",
    "floor": 3,
    "hope": 5,
    "hp": 4,
    "operators": [{"name": "W", "elite": 2, "level": 80}],
    "collections": ["Golden Chalice"],
    "current_task": "Roguelike@ChooseOper"
  }
}
```

### 输出 (Action)
```json
{
  "task_type": "recruit",
  "action": "click_oper",
  "target": "W",
  "x": 640, "y": 360,
  "confidence": 0.92
}
```

---

## VLA 全 AI 控制模式（待实施）

### 架构

VLA 推理循环替代 MAA ProcessTask 的硬编码决策：

```
截图 → VLA（Python server）→ 文本指令 → 解析器 → 校验 → 执行 / 回退默认规则
```

### 实现步骤

| # | 类型 | 文件 | 说明 |
|---|------|------|------|
| 1 | 新增 | `AiBridge/VlaInstruction.h` | 指令数据结构 + 解析函数声明 |
| 2 | 新增 | `AiBridge/VlaInstruction.cpp` | 文本解析 + 校验逻辑 |
| 3 | 修改 | `AiBridge.h/cpp` | 新增 `query_vla()` 方法 |
| 4 | 修改 | `RoguelikeRecruitTaskPlugin.cpp` | 补全 VLA 招募执行 |
| 5 | 修改 | `RoguelikeBattleTaskPlugin.cpp` | 补全 VLA 战斗部署/撤退/技能 |
| 6 | 修改 | `RoguelikeShoppingTaskPlugin.cpp` | 补全 VLA 购物执行 |
| 7 | 修改 | `RoguelikeStageEncounterTaskPlugin.cpp` | 补全 VLA 遭遇选择执行 |
| 8 | 修改 | `RoguelikeRoutingTaskPlugin.cpp` | 补全 VLA 路线选择执行 |
| 9 | 修改 | `tools/ai_service/server.py` | 新增 `/api/vla` 端点 |
| 10 | 修改 | `SimulatorView.xaml` + `SimulatorViewModel.cs` | VLA Full Control 开关 |

### 文本指令格式

| 指令 | 示例 | MAA 动作 |
|------|------|---------|
| `recruit <name>` | `recruit 斑点` | recruit_appointed_char |
| `deploy <name> x=<int> y=<int> dir=<int>` | `deploy 斑点 x=3 y=4 dir=3` | deploy_oper |
| `retreat x=<int> y=<int>` | `retreat x=3 y=4` | retreat_oper |
| `skill <name>` | `skill 斑点` | 点击技能按钮 |
| `buy <goods>` | `buy Golden Chalice` | 点击商品 |
| `encounter choice=<int>` | `encounter choice=0` | 点击选项 |
| `route node=<int>` | `route node=2` | 点击地图节点 |
| `click x=<int> y=<int>` | `click x=640 y=360` | ctrler()->click |
| `swipe x1 y1 x2 y2` | `swipe 100 500 100 200` | ctrler()->swipe |

### 非法指令处理

1. **校验层**: 解析后检查合法性（干员名是否在候选列表、坐标是否越界等）
2. **回退默认规则**: 非法指令走 MAA 原有 max_element / ProcessTask 逻辑
3. **死循环保护**: 同一 step 连续 3 次非法 → 强制回退默认规则
