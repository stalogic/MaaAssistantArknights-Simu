# VLA 训练数据优化计划

## 当前状态评估

基于 392 步 Phantom 肉鸽轨迹分析，当前数据**无法直接训练 VLA 模型**。以下列出所有问题及修复方案。

---

## Phase 1: 致命问题修复（阻塞训练）

### 1.1 Reward 信号

**现状**: `"reward": 0` 硬编码，全轨迹无效。

**方案**: 在 `log_generic` 调用处传入 reward。reward 来源：

| 信号源 | 触发条件 | reward 值 |
|--------|---------|-----------|
| 楼层晋升 | floor > prev_floor | +5 |
| HP 降低 | hp < prev_hp | -1 每点 |
| HP 恢复 | hp > prev_hp | +0.5 每点 |
| 招募高星 | chosen priority > threshold | +1 |
| 通关/失败 | GamePass | +10 / -5 |
| 进入商店 | task contains "Trader" | +0.5 |

**实现**: `TrajectoryLogger` 新增成员 `int m_prev_hp`, `int m_prev_floor`，在 `log_generic/log_recruit` 中根据 `extra_params` 与上一状态对比自动计算 reward。

**改动文件**: `TrajectoryLogger.h/cpp`

---

### 1.2 全局状态填充 (extra_params)

**现状**: 83.7% 的记录 (`action`/`wait`/`stop`) extra_params 为空 `{}`。

**方案**: 在 `ProcessTask::run_action` 中注入状态。`ProcessTask` 需要能访问 `RoguelikeConfig`。

**实现**: 在 `RoguelikeControlTaskPlugin` 或 `RoguelikeResetTaskPlugin` 中将 `m_config` 指针注册到 `TrajectoryLogger`，使其在任何调用点都能读取当前肉鸽状态。

```cpp
TrajectoryLogger::instance().set_state_provider([&]() {
    json::value state{
        {"theme", m_config->get_theme()},
        {"floor", m_config->status().floor},
        {"hope", m_config->status().hope},
        {"hp", m_config->status().hp},
        {"difficulty", m_config->get_difficulty()},
        {"mode", static_cast<int>(m_config->get_mode())},
        {"squad", m_config->get_squad()},
        {"formation_limit", m_config->status().formation_upper_limit},
    };
    return state.to_string();
});
```

**改动文件**: `TrajectoryLogger.h/cpp`, `RoguelikeResetTaskPlugin.cpp`, `RoguelikeRecruitTaskPlugin.cpp` (去掉冗余的手动传参), `RoguelikeBattleTaskPlugin.cpp` 等

---

### 1.3 点击坐标记录

**现状**: `ClickSelf`/`ClickRect`/`Swipe` 只记录 task 名，无实际像素坐标。

**方案**: 在 `ProcessTask::run_action` 中从 `hits.rect` / `task->specific_rect` 提取坐标写入 action。

```cpp
// ClickSelf 记录 hits.rect 坐标
action["x"] = hits.rect.x;
action["y"] = hits.rect.y;
action["w"] = hits.rect.width;
action["h"] = hits.rect.height;

// ClickRect 记录 specific_rect 坐标
action["x"] = task->specific_rect.x;
action["y"] = task->specific_rect.y;

// Swipe 记录始末坐标
action["x1"] = task->specific_rect.x;
action["y1"] = task->specific_rect.y;
action["x2"] = task->rect_move.x;
action["y2"] = task->rect_move.y;
```

**改动文件**: `ProcessTask.cpp`

---

### 1.4 Episode 终止标记 (done)

**现状**: `done: true` 从未触发（Stop 未到达 logger 或逻辑有误）。

**问题定位**: 检查 `ProcessTaskAction::Stop` 是否在 `SubTaskStart` 之后进入 `run_action`。可能 Stop 在 `run_task` 层面被处理，不走 `run_action`。

**方案**: 除 ProcessTask Stop 外，在肉鸽结算插件 (`RoguelikeSettlementTaskPlugin`) 中也显式调用 `end_session()`，在末尾加一条 `done: true` 记录。

**改动文件**: `RoguelikeSettlementTaskPlugin.cpp`, `ProcessTask.cpp`

---

## Phase 2: 严重问题修复（影响训练质量）

### 2.1 AI 决策影子日志

**现状**: 全轨迹 `ai_used: false, ai_chosen: ""`。

**方案**: 在每个决策点，无论 AI 是否启用，都调一次 `AiBridge::query_xxx()` 获取 AI 建议，记录在 `ai_chosen` 字段。

```cpp
// recruit 示例：无论 AI 是否启用，都获取建议
std::string ai_suggestion = "";
if (AiBridge::instance().is_enabled()) {
    ai_suggestion = AiBridge::instance().query_recruit_decision(...);
}
// 如果 AI 未启用但想记录对比，可加:
// ai_suggestion = get_or_query_ai_suggestion("recruit", ...);
```

**影响**: 提供 (默认规则, AI 建议) 对比对，用于训练。

**改动文件**: 5 个决策插件

---

### 2.2 冗余动作去噪

**现状**: `deploy → BattleCancelSelection` 成对出现，39 对占 9.9%。

**方案 A (预处理)**: 训练 pipeline 中过滤，不修改 C++ 代码。

**方案 B (日志标记)**: 给 `BattleCancelSelection` 加 `is_noise: true` 标记，方便下游过滤。

**推荐**: 方案 A。在 Python 预处理脚本中实现，不需要改 C++。

---

### 2.3 空/损坏截图检测

**现状**: 5 张 wait 帧仅 3.9 KB（正常 700KB+）。

**方案**: 在 `save_screenshot` 前添加空图检测。

```cpp
if (img.empty() || img.rows == 0 || img.cols == 0) return "INVALID";
```

**改动文件**: `TrajectoryLogger.cpp`

---

## Phase 3: 中等问题（后处理解决）

### 3.1 Episode 元数据文件

在每个 session 目录写入 `metadata.json`:

```json
{
  "episode_id": "20260529_222035_Phantom",
  "theme": "Phantom",
  "mode": "Exp",
  "difficulty": 3,
  "squad": "指挥分队",
  "total_steps": 392,
  "outcome": "unknown"
}
```

**改动文件**: `TrajectoryLogger.cpp`

---

### 3.2 训练前预处理 Pipeline

Python 脚本做数据清洗：过滤空图、统一 action schema、去除冗余动作、对齐观察到标准维度。

**新增文件**: `tools/ai_service/preprocess_trajectory.py`

---

## 实施顺序

| 优先级 | 编号 | 内容 | 预估工作量 |
|--------|------|------|-----------|
| P0 | 1.3 | 点击坐标记录 | 1h |
| P0 | 1.2 | 全局状态填充 | 2h |
| P0 | 1.1 | Reward 信号 | 1h |
| P0 | 1.4 | Episode done 标记 | 1h |
| P1 | 2.1 | AI 影子日志 | 2h |
| P1 | 2.3 | 空图检测 | 10min |
| P2 | 3.1 | Session metadata | 30min |
| P3 | 2.2 | 去噪(预处理) | 独立 |
| P3 | 3.2 | 预处理 pipeline | 独立 |

**总计 P0-P1**: 约 7h C++ 改动。
