# GAS 战斗桥接（GAS Combat Bridge）

把虚幻引擎的 **Gameplay Ability System（GAS）** 包装成**中文蓝图节点**，让不写 C++ 的战斗策划也能独立做出技能、连招、血条和 Buff。

![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.7-0E1128?logo=unrealengine&logoColor=white)
![License](https://img.shields.io/badge/license-MIT-3DA639)
![Nodes](https://img.shields.io/badge/blueprint%20nodes-77-1f6feb)
![Blueprint Only](https://img.shields.io/badge/blueprint--only-yes-8957e5)

界面全中文、术语按语义翻译（`GameplayEffect` 叫「效果」不叫「特效」，`GameplayCue` 叫「表现」）。
附带一份 2000 行的零基础手册、一个节点速查表、一份源码详解，以及一个运行时调试面板。

---

## 为什么需要它

GAS 是虚幻最强的战斗框架，但它的绝大多数能力**只暴露给 C++**。纯蓝图项目里你会反复撞到同一堵墙：

| 你想做的事 | 原生 GAS 的现状 | 这个插件 |
|---|---|---|
| 新建一套生命/护盾/攻击力属性 | **做不到**。引擎的下拉框会过滤掉蓝图生成的属性集，你的属性永远不出现在效果里 | 提供「通用战斗属性集」（11 项）+「连招属性集」（2 项），开箱可用 |
| 加 Buff、造伤害 | 效果能建，但数值要靠 `SetByCaller` 标签传递，键名对不上时**静默失效** | 「施加效果（动态数值）」直接传「标签 → 数值」映射 |
| 知道技能为什么没放出来 | **完全静默**。按了键没反应，日志里一个字都没有 | 「技能激活失败时」事件 + 7 条中文原因（消耗不足/冷却中/被状态标签阻止……） |
| 血条跟随属性变化 | 属性变化回调是**原生多播委托**，蓝图绑不到 | 「属性监听器」对象，存进变量就生效 |
| 读技能的动态标签 | `GetDynamicSpecSourceTags()` 不是 `BlueprintCallable` | 两个节点直接读写 |
| 在技能中途主动结束自己 | `EndAbility` 是 **protected**，蓝图节点只给蓝图用 | 「结束技能」节点，区分正常结束/被打断 |
| 看清角色当前状态 | 手打控制台命令、翻页看视口文字、**只能看不能改** | 可停靠的调试面板，能直接改属性、激活技能、移除效果 |

---

## 功能一览

### 77 个蓝图节点

| 分类 | 数量 | 说明 |
|---|---|---|
| `战斗\|初始化` | 12 | 挂 ASC、初始化技能系统、授予/回收技能集 |
| `战斗\|属性` | 10 | 读写当前值/基础值、批量快照、生命百分比、是否已死亡 |
| `战斗\|技能` | 17 | 授予、激活、打断、查冷却、动态标签、结束技能、由事件激活（异步） |
| `战斗\|效果` | 9 | 施加、动态数值、造成伤害、移除、层数、剩余时间 |
| `战斗\|标签` | 7 | 查标签、游离标签增删 |
| `战斗\|表现` | 5 | 播放表现、循环表现、发游戏事件、发状态树事件 |
| `战斗\|监听` | 7 | 属性监听器的创建与生命周期 |
| `战斗\|调试` | 7 | 战斗状态报告、打印、标签变化时刻 |
| `战斗\|技能集` | 3 | 一次性把技能/效果/属性打包授予或回收 |

### 事件与资产

- **8 个组件事件**：战斗就绪、技能激活/结束/激活失败、标签变化、被施加效果、受到伤害、死亡
- **2 个属性集事件**：受到伤害时、死亡时
- **2 个监听器事件**：数值变化时、归零时
- **技能集资产（`UGASCombatAbilitySet`）**：像配表格一样配置「挂哪些属性集、初始值多少、给哪些技能、带哪些常驻效果、贴哪些标签」，一次授予、逆序回收。换武器 = 回收旧技能集 + 授予新技能集，互不干扰
- **连招属性集**：`ComboIndex` / `MaxComboIndex`，内置「刻意不钳上界」的绕回契约（`3 + (-2) → 1`）

### 运行时调试面板

三种打开方式：

1. 菜单 **窗口 → 工具 → GAS 战斗调试**
2. 关卡编辑器顶部播放工具栏的 **`GAS 调试`** 按钮
3. 控制台 **`GASCombat.Debug`**

四个区：属性（**可直接改基础值**）、技能（**可直接激活/取消**）、效果（**可直接移除**）、标签。另有「忽略冷却 / 忽略消耗」两个开关，做连招验证时非常省事。

---

## 安装

### 方式一：免编译（推荐给策划）

把插件文件夹放到下面任一位置，重启编辑器即可：

| 作用范围 | 路径 |
|---|---|
| 所有工程 | `<引擎目录>/Engine/Plugins/Marketplace/GASCombatBridge/` |
| 单个工程 | `<工程目录>/Plugins/GASCombatBridge/` |

> 文件夹名不必和 `.uplugin` 同名 —— 引擎是按 `*.uplugin` 文件发现插件的。

### 方式二：从源码编译

需要 **Visual Studio 2022**（含 C++ 游戏开发工作负载）+ Windows SDK。

```powershell
# 1. 把仓库放到工程的 Plugins 目录
git clone https://github.com/as05104evergone-star/GAS-Combat-Bridge.git MyProject/Plugins/GASCombatBridge

# 2. 右键 .uproject → Generate Visual Studio project files
# 3. 打开生成的 .sln，配置选 Development Editor，生成
# 4. 启动编辑器（若提示模块缺失，点是即可热重编译）
```

也可以用 UAT 打包成可直接分发的版本：

```powershell
& "<引擎目录>\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin `
    -Plugin="<仓库目录>\GASCombatBridge.uplugin" `
    -Package="<输出目录>\GASCombatBridge" `
    -TargetPlatforms=Win64
```

> ⚠️ `BuildPlugin` 会**删掉 `.uplugin` 里的 `EnabledByDefault`**，导致插件「已发现但未启用」——
> 表现是节点全搜不到、`LogGASCombat` 一行不打、且**零报错**。
> 打包后请手动把这个键加回去。详见 `使用说明.md` §10.2。

### 环境要求

| 项 | 要求 |
|---|---|
| 引擎 | **5.7**（在 5.7.4 / Changelist 47537391 上验证） |
| 平台 | Windows / Win64 |
| 编译器 | VS2022（仅从源码编译时需要） |
| 依赖插件 | `GameplayAbilities`、`GameplayStateTree`（`.uplugin` 里已声明，会自动启用） |

其他 5.x 版本需要改 `.uplugin` 的 `EngineVersion` 后重新编译，**未测试**。

---

## 三分钟上手

1. 在角色蓝图里加 **`GAS 战斗桥接组件`**
2. 组件细节面板 → **挂载的属性集** → 加一项 `通用战斗属性集`
   （🔴 不填这一步，伤害不掉血且零报错）
3. 勾上 **开始时自动初始化**，并在 `BeginPlay` 后调一次 **`初始化技能系统`**
4. 用 **`授予技能`** 给技能，用 **`施加效果`** 或 **`对目标造成伤害`** 结算伤害

然后就能用 **`监听属性变化`** 做血条、用 **`技能激活失败时`** 查技能为什么没放出来。

完整流程见 [`新手入门手册.md`](新手入门手册.md) 第 3 章。

---

## 文档

| 文档 | 篇幅 | 给谁看 |
|---|---|---|
| [`新手入门手册.md`](新手入门手册.md) | 2100 行 | **没用过 GAS 的人**。从「GAS 是什么」讲到「三段连招 + 血条 + 换武器」三个完整实战，每个节点都标了会踩的坑 |
| [`使用说明.md`](使用说明.md) | 1300 行 | 查参数的人。按功能分类逐个节点讲引脚与用法，含排错章节与重编译指南 |
| [`代码实现详解.md`](代码实现详解.md) | 1150 行 | 想改源码的人。14 章讲清分层、启动流程、两套属性集、节点库实现范式、技能集顺序契约、编辑器面板与自动化测试 |

---

## 目录结构

```
GAS-Combat-Bridge/
├── GASCombatBridge.uplugin          # 插件描述（2 个模块 + 2 个依赖插件）
├── Config/
│   ├── DefaultGameplayTags.ini      # 参考用，不会被引擎自动加载（见下）
│   ├── DefaultGame.ini              # 参考用
│   └── FilterPlugin.ini
├── Source/
│   ├── GASCombatBridge/             # 运行时模块
│   │   ├── Public/
│   │   │   ├── GASCombatStatics.h           # 56 个节点声明
│   │   │   ├── GASCombatBridgeComponent.h   # 桥接组件（8 个事件）
│   │   │   ├── GASCombatCoreAttributeSet.h  # 通用战斗属性集（11 项）
│   │   │   ├── GASCombatComboAttributeSet.h # 连招属性集（2 项）
│   │   │   ├── GASCombatAbilitySet.h        # 技能集资产
│   │   │   ├── GASCombatAttributeWatcher.h  # 属性监听器
│   │   │   ├── GASCombatAbilityTaskLibrary.h# 异步节点工厂
│   │   │   └── GASCombatTypes.h             # 8 个蓝图结构体
│   │   └── Private/
│   │       ├── GASCombatStatics.cpp         # 节点实现（1380 行）
│   │       ├── GASCombatBridge.cpp          # 启动：标签注册 + 控制台命令
│   │       └── Tests/GASCombatComboTest.cpp # 3 个自动化测试
│   └── GASCombatBridgeEditor/       # 编辑器模块（不进游戏包）
│       ├── Private/SGASCombatDebugPanel.cpp # 调试面板
│       └── Private/GASCombatBridgeEditor.cpp# 页签 + 工具栏按钮
├── 新手入门手册.md
├── 使用说明.md
├── 代码实现详解.md
└── LICENSE
```

### 关于 `Config/*.ini`

这两个文件在插件里**不会被自动加载**，属于参考文档。

原因：`GameplayTags` 的设置类是**引擎模块**的类，它不会去翻插件的 `Config/` 目录。写在里面 = 100% 静默失效（v1.0.0 的真实缺陷）。

插件改为在模块启动时用 C++ 原生注册标签（日志会打「已注册 16 个自带标签」）。这两个 ini 保留下来，**供你想把这套标签并进自己工程时直接复制**。

---

## 已知限制

- **只在 UE 5.7.4 / Win64 上验证过**，其他版本需自行编译并可能微调
- **联机复制未做双端实测**。复制相关的写法按引擎源码推断（`REPNOTIFY_Always`、`IsOwnerActorAuthoritative`、`bShouldReplicate`）
- **状态树事件节点**未在真正带 StateTree 组件的工程里跑过
- 调试面板是编辑器功能，**不进游戏包**
- 属性集必须是 C++ 类 —— 这是引擎的硬限制（`GetAllAttributeProperties` 会过滤蓝图生成的类），所以新增属性仍然需要改 C++。插件已经把你最常用的 13 项都做好了

## 开发

3 个自动化测试（属性集管线 / 属性集是原生类 / 标签与失败原因），覆盖了 `SetByCaller → 修饰符 → 夹取` 全链路和连招绕回语义：

```
GASCombat.R1.ComboAttributeSetPipeline
GASCombat.R1.AttributeSetIsNative
GASCombat.R1.TagsAndActivateFailSettings
```

跑法：编辑器 → **工具 → 会话前端 → 自动化测试**，筛 `GASCombat`。命令行方式见 `使用说明.md` §10.5。

---

## 协议

[MIT](LICENSE)。可商用、可修改、可再分发，保留版权声明即可。

---

## 致谢

术语翻译与设计参考了以下开源项目的思路（未复制其代码）：

- [tranek/GASDocumentation](https://github.com/tranek/GASDocumentation) —— 目前最完整的 GAS 社区文档
- [DanMcGuire/UE5-GAS-Templates](https://github.com/DanMcGuire/UE5-GAS-Templates)
- [Variann/UE5-GAS-Shooter](https://github.com/Variann/UE5-GAS-Shooter)
