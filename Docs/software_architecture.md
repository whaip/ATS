# 软件架构

## 总体结构

当前 ATS 程序采用“主窗口导航 + 专项功能页面 + 设备层适配 + 故障诊断编排”的结构。主界面负责页面组织和全局入口，不同业务模块分别在独立目录中实现，模块之间通过接口、运行时上下文和数据文件协同。

## 1. 主界面与页面导航

主界面由 [mainwindow.cpp](/d:/FaultDetect/Program/FaultDetect/ATS/mainwindow.cpp) 和 [mainwindow.ui](/d:/FaultDetect/Program/FaultDetect/ATS/mainwindow.ui) 负责，核心职责包括：

- 管理首页、板卡管理、项目信息、错误日志、统计结果等页面入口。
- 承载设备初始化、菜单动作和页面切换。
- 汇总运行日志并展示到日志表格。
- 将业务页面按需创建并加入 `pagesStack`。

## 2. 板卡管理与识别

板卡管理模块位于 `BoardManager` 目录，核心职责包括：

- 板卡模板图像采集、保存、删除与数据库维护。
- 板卡 ROI 提取与识别模型调用。
- `board_db/boards.json`、`board_db/images`、`embeddings_database.yml` 等数据资产管理。
- 训练脚本触发、模型更新与模板库重建。

当前识别链路已经由 ORB 迁移为 `ROI-Embedding` 检索，核心文件包括：

- [boardmanager.cpp](/d:/FaultDetect/Program/FaultDetect/ATS/BoardManager/boardmanager.cpp)
- [roiembeddingmatcher.cpp](/d:/FaultDetect/Program/FaultDetect/ATS/BoardManager/roiembeddingmatcher.cpp)
- [train_board_embedding.py](/d:/FaultDetect/Program/FaultDetect/ATS/BoardManager/EmbeddingTraining/train_board_embedding.py)

## 3. 视觉与温度采集

视觉与温度相关模块分为两部分：

- `HDCamera`：高清相机采集、预览、参数控制、图像输出。
- `IRCamera`：红外温度采集、温度帧处理、温度统计和温度显示。

这两部分提供板卡识别输入和温度监测输入，为板卡识别和故障诊断提供现场数据基础。

## 4. JY 设备控制层

硬件控制核心位于 `IODevices/JYDevices`，采用分层设计：

- `Adapter`：直接封装厂商接口，负责设备打开、配置、启动、停止、关闭。
- `Worker`：负责线程内设备操作调度。
- `Orchestrator`：组织完整测试时序，例如 `configure -> start -> trigger -> stop`。
- `ThreadManager`：统一管理不同设备的 worker 生命周期。

同时程序中还提供：

- `DataGenerateCard`：用于输出设备配置与界面控制。
- `DataCaptureCard`：用于采集设备配置、波形采样与显示。

## 5. 故障诊断架构

故障诊断模块位于 `FaultDiagnostic` 目录，整体按职责分为：

- `TPS`：定义测试策略、接线要求、设备计划和策略参数。
- `Diagnostics`：处理采样结果、提取特征并输出诊断结论。
- `Runtime`：统一编排任务执行流程、资源绑定、测试运行和状态流转。
- `Workflow`：承载接线引导、任务上下文和流程配套逻辑。
- `UI`：承载测试页面、配置页面和运行交互。

这套结构把“怎么测”和“怎么判”明确拆开，便于后续增加新的元件 TPS 插件和诊断插件。

## 6. 日志、统计与对外接口

测试结果记录和对外发送由两部分组成：

- `TaskLogging`：负责将测试结果写入 SQLite，并提供统计页面。
- `TaskTransport`：负责把统计结果通过 `HTTP / TCP / UDP` 实时发送给外部系统。

相关模块包括：

- [testtasklogservice.cpp](/d:/FaultDetect/Program/FaultDetect/ATS/FaultDiagnostic/TaskLogging/testtasklogservice.cpp)
- [tasklogstatisticspage.cpp](/d:/FaultDetect/Program/FaultDetect/ATS/FaultDiagnostic/TaskLogging/tasklogstatisticspage.cpp)
- [tasklogtransportservice.cpp](/d:/FaultDetect/Program/FaultDetect/ATS/FaultDiagnostic/TaskTransport/tasklogtransportservice.cpp)
- [tasklogtransportwidget.cpp](/d:/FaultDetect/Program/FaultDetect/ATS/FaultDiagnostic/TaskTransport/tasklogtransportwidget.cpp)

## 7. 典型运行流程

程序当前的典型运行链如下：

1. 相机采集板卡图像并提取 ROI。
2. 板卡管理模块识别板卡类型并装载对应配置。
3. 用户配置测试任务或从配置库读取任务参数。
4. 故障诊断运行时调用 TPS 插件生成设备计划并驱动 JY 设备执行测试。
5. Diagnostics 插件对采样结果进行分析并生成诊断结果。
6. 日志模块写入 SQLite，统计模块刷新页面，对外发送模块发布统计结果。

## 8. 当前架构特点

当前程序架构具备以下特点：

- 页面级功能模块划分明确。
- 设备控制与业务逻辑分层较清晰。
- TPS 与 Diagnostics 已完成职责拆分。
- 板卡识别、任务日志和统计发送都具备独立扩展点。
- 新增板卡、测试策略、诊断插件和接口协议时，不需要整体重构主程序。

这套结构将继续沿着“模块化、插件化、可替换算法”的方向迭代。
