# JYDevices 模块说明

## 1. 模块职责

`IODevices/JYDevices` 负责对接几类井仪 PXIe 设备，并把“设备控制 + 原始数据采集 + 数据对齐 + 对外输出”统一成一套可复用接口。当前包含：

- `PXIe-5322`：高速电压采集
- `PXIe-5323`：高速电流采集
- `PXIe-5711`：波形输出
- `PXIe-8902`：万用表/低速多点采样

这个目录中的核心分层如下：

- `JYThreadManager`：总入口，负责创建 worker、绑定编排器和数据管线
- `JYDeviceOrchestrator`：负责多设备同步启动/停止
- `JYDeviceWorker`：每块设备一个工作线程，串行执行控制命令并持续取数
- `JYDeviceAdapter`：底层驱动适配层，直接调用厂商 SDK
- `JYDataPipeline`：原始包入口，做校验、原始转发和对齐转发
- `JYDataAligner`：把不同设备的数据按统一时间轴对齐输出

## 2. 数据采集到数据输出流程

### 2.1 总体流程

1. 上层创建 `JYThreadManager`
2. 通过 `create532xWorker / create5711Worker / create8902Worker` 创建所需设备线程
3. 上层通过 `orchestrator()` 获取 `JYDeviceOrchestrator`
4. 调用 `synchronizeStart(...)`
5. `JYDeviceOrchestrator` 依次执行：
   - `configureAll`
   - 等待所有目标设备进入 `Configured`
   - `startAll`
   - 等待进入 `Armed`
   - `triggerAll`
   - 等待进入 `Running`
6. 每个 `JYDeviceWorker` 在自己的 `std::thread` 中持续调用 `adapter->read(...)`
7. 采集到的 `JYDataPacket` 通过 `dataReady` 发给 `JYDataPipeline::ingest(...)`
8. `JYDataPipeline` 对外输出两类数据：
   - 原始数据：`packetReady(const JYDataPacket &packet)`
   - 对齐数据：`alignedBatchReady(const JYAlignedBatch &batch)`
9. 上层业务消费原始包或对齐批次
10. 停止时调用 `synchronizeStop(...)` 或 `shutdown()`

### 2.2 关键对象之间的关系

- `JYThreadManager` 持有一个 `JYDeviceOrchestrator`
- `JYThreadManager` 持有一个 `JYDataPipeline`
- 每个 `JYDeviceWorker` 持有一个 `JYDeviceAdapter`
- `JYDeviceWorker::dataReady` 连接到 `JYDataPipeline::ingest`
- `JYDataPipeline` 内部持有一个 `JYDataAligner`

### 2.3 输出数据的两种形态

#### 原始数据 `JYDataPacket`

原始数据保持设备自身节奏，不做跨设备对齐。适合：

- 实时显示某一块卡的原始波形
- 做底层驱动调试
- 需要保留设备原始采样率和包边界的场景

#### 对齐数据 `JYAlignedBatch`

对齐数据会把多个设备的数据重建到同一条时间轴。适合：

- 多传感器联合诊断
- 把电压、电流、DMM 数据放到统一窗口中分析
- 需要统一时间基准的算法输入

## 3. 数据对齐策略

`JYDataAligner` 的策略可以概括为：

### 3.1 输入前提

- 每个 `JYDataPacket` 必须至少提供：
  - `sampleRateHz`
  - `samplesPerChannel`
  - `startSampleIndex`
  - `timestampMs`

### 3.2 对齐步骤

1. 保存每种设备最新收到的一包数据到 `m_latest`
2. 如果设置了 `expectedKinds`，则必须等所有期望设备都到齐才尝试输出
3. 根据 `maxAgeMs` 丢弃过旧数据
4. 对每个包恢复其时间范围：
   - `packetStart = anchor + startSampleIndex / sampleRate`
   - `packetEnd = anchor + endSampleIndex / sampleRate`
5. 对所有设备时间范围求交集：
   - 起点取 `maxStart`
   - 终点取 `minEnd`
6. 以最高采样率 `refRate` 作为统一参考时间轴
7. 在 `maxStart ~ targetEnd` 范围内按 `1 / refRate` 生成 `targetTimes`
8. 每个设备、每个通道都对 `targetTimes` 做线性插值重采样
9. 重新打包为 `JYAlignedBatch`

### 3.3 参考采样率

- 使用参与对齐设备中的**最高采样率**作为 `refRate`
- 低采样率设备会向高采样率时间轴插值

### 3.4 对齐窗口

- `Settings::windowMs > 0` 时：
  - 输出窗口长度取 `windowMs`
- `Settings::windowMs <= 0` 时：
  - 使用所有设备公共交集的完整长度

### 3.5 锚点

- 如果外部调用了 `setSyncAnchorMs(anchorMs)`：
  - 使用外部给定锚点
- 如果未设置锚点：
  - 第一次对齐时会根据收到的包自动推导一个锚点

### 3.6 旧包淘汰

- 若某包 `timestampMs < newestTs - maxAgeMs`
- 则认为它已经过期，会被丢弃并发出 `packetDropped`

## 4. 主要数据结构

### 4.1 `JYDeviceKind`

- `PXIe5322`：电压采集卡
- `PXIe5323`：电流采集卡
- `PXIe5711`：波形输出卡
- `PXIe8902`：万用表

### 4.2 `JYDeviceState`

- `Closed`：已关闭
- `Configured`：已配置
- `Armed`：已启动但尚未真正触发
- `Running`：运行中
- `Faulted`：故障态

### 4.3 `JYDataPacket`

- `kind`：设备类型
- `channelCount`：包内交织通道数
- `samplesPerChannel`：每通道样本数
- `sampleRateHz`：单通道采样率
- `startSampleIndex`：该包第一个样本的全局样本序号
- `data`：交织样本数据
- `timestampMs`：该包结束时刻的时间戳

### 4.4 `JYAlignedBatch`

- `packets`：按设备种类索引的对齐后数据包
- `timestampMs`：对齐窗口起始时间戳

## 5. 对外 API 一览

下面只列这个目录头文件中可被外部直接使用的公开函数/方法。

---

## 5.1 `jythreadmanager.h`

### `JYThreadManager::JYThreadManager(QObject *parent = nullptr)`

- `parent`：Qt 父对象
- 作用：创建线程管理器，同时创建内部的 `JYDeviceOrchestrator` 和 `JYDataPipeline`

### `JYThreadManager::~JYThreadManager()`

- 作用：析构时自动调用 `shutdown()`

### `JYDeviceWorker *create532xWorker(JYDeviceKind kind)`

- `kind`：必须是 `PXIe5322` 或 `PXIe5323`
- 返回值：对应设备的 worker 指针
- 作用：创建并启动高速采集卡 worker；若已创建则直接返回已有实例

### `JYDeviceWorker *create5711Worker()`

- 返回值：5711 worker 指针
- 作用：创建并启动波形输出 worker

### `JYDeviceWorker *create8902Worker()`

- 返回值：8902 worker 指针
- 作用：创建并启动万用表 worker

### `JYDeviceOrchestrator *orchestrator() const`

- 返回值：统一编排器
- 作用：供外部执行同步 configure/start/stop

### `JYDataPipeline *pipeline() const`

- 返回值：数据管线对象
- 作用：供外部设置对齐参数并接收原始/对齐数据

### `bool isDeviceInitialized(JYDeviceKind kind) const`

- `kind`：目标设备类型
- 返回值：设备是否已经进入可工作状态

### `void shutdown()`

- 作用：关闭全部 worker、清空状态并回收线程资源

### 信号 `void deviceStatusChanged(JYDeviceKind kind, JYDeviceState state, const QString &message)`

- `kind`：设备种类
- `state`：当前状态
- `message`：附带消息
- 作用：对外统一广播设备状态变化

---

## 5.2 `jydeviceorchestrator.h`

### `void addWorker(JYDeviceWorker *worker)`

- `worker`：要纳入统一编排的 worker
- 作用：注册 worker

### `void clearWorkers()`

- 作用：清空已注册 worker

### `void configureAll(const JYDeviceConfig &config532x, const JYDeviceConfig &config5711, const JYDeviceConfig &config8902)`

- `config532x`：5322/5323 共享配置
- `config5711`：5711 配置
- `config8902`：8902 配置
- 作用：向启用设备广播 configure

### `void startAll()`

- 作用：向全部 worker 广播 start

### `void triggerAll()`

- 作用：向全部 worker 广播软触发

### `void stopAll()`

- 作用：向全部 worker 广播 stop

### `void closeAll()`

- 作用：向全部 worker 广播 close

### `bool synchronizeStart(const JYDeviceConfig &config532x, const JYDeviceConfig &config5711, const JYDeviceConfig &config8902, int timeoutMs, qint64 *barrierReleaseMs = nullptr)`

- `config532x`：5322/5323 共享配置
- `config5711`：5711 配置
- `config8902`：8902 配置
- `timeoutMs`：每个等待阶段的超时时间
- `barrierReleaseMs`：可选输出参数，记录统一触发时刻
- 返回值：同步启动是否成功
- 作用：执行完整同步启动流程

### `bool synchronizeStop(int timeoutMs)`

- `timeoutMs`：停止等待超时
- 返回值：同步停止是否成功
- 作用：停止最近一次启动的设备集合

### 信号 `void syncFailed(const QString &reason)`

- `reason`：失败原因

### 信号 `void syncSucceeded()`

- 作用：同步流程成功完成

---

## 5.3 `jydeviceworker.h`

### `JYDeviceWorker(std::unique_ptr<JYDeviceAdapter> adapter, QObject *parent = nullptr)`

- `adapter`：底层设备适配器
- `parent`：Qt 父对象

### `~JYDeviceWorker()`

- 作用：析构时停止工作线程

### `void start()`

- 作用：启动 worker 专属线程

### `void stop()`

- 作用：停止 worker 线程

### `void postConfigure(const JYDeviceConfig &config)`

- `config`：设备配置
- 作用：异步投递 configure

### `void postStart()`

- 作用：异步投递 start

### `void postTrigger()`

- 作用：异步投递 trigger

### `void postStop()`

- 作用：异步投递 stop

### `void postClose()`

- 作用：异步投递 close

### `JYDeviceKind kind() const`

- 返回值：当前 worker 对应设备类型

### `JYDeviceState state() const`

- 返回值：当前缓存状态

### 信号 `void statusChanged(JYDeviceKind kind, JYDeviceState state, const QString &message)`

- 作用：状态变化通知

### 信号 `void dataReady(const JYDataPacket &packet)`

- 作用：原始数据包输出

---

## 5.4 `jydatapipeline.h`

### `JYDataPipeline(QObject *parent = nullptr)`

- `parent`：Qt 父对象

### `void setExpectedKinds(const QSet<JYDeviceKind> &kinds)`

- `kinds`：期望参与对齐的设备集合
- 作用：限制哪些设备必须到齐后才能输出对齐批次

### `void setAlignSettings(const JYDataAligner::Settings &settings)`

- `settings`：对齐参数
- 作用：设置对齐窗口和旧包策略

### `void setSyncAnchorMs(qint64 anchorMs)`

- `anchorMs`：统一时间锚点，单位毫秒
- 作用：让不同设备 sampleIndex 共享同一绝对时间原点

### `void ingest(const JYDataPacket &packet)`

- `packet`：原始数据包
- 作用：校验并转发原始包，同时送入对齐器

### 信号 `void alignedBatchReady(const JYAlignedBatch &batch)`

- `batch`：对齐后的批次数据

### 信号 `void packetRejected(JYDeviceKind kind, const QString &reason)`

- `kind`：被拒绝的数据设备类型
- `reason`：拒绝原因

### 信号 `void packetIngested(JYDeviceKind kind, int channelCount, int dataSize, qint64 timestampMs)`

- `kind`：设备类型
- `channelCount`：通道数
- `dataSize`：数据点数
- `timestampMs`：包时间戳

### 信号 `void packetReady(const JYDataPacket &packet)`

- `packet`：原始数据包

---

## 5.5 `jydataaligner.h`

### `JYDataAligner::Settings`

#### `int windowMs`

- 含义：目标对齐窗口长度，单位毫秒

#### `int maxAgeMs`

- 含义：允许保留的最大旧包年龄，单位毫秒

### `JYDataAligner(QObject *parent = nullptr)`

- `parent`：Qt 父对象

### `void setSettings(const Settings &settings)`

- `settings`：新的对齐参数

### `void setExpectedKinds(const QSet<JYDeviceKind> &kinds)`

- `kinds`：期望设备集合

### `void setSyncAnchorMs(qint64 anchorMs)`

- `anchorMs`：统一时间锚点，单位毫秒

### `void ingest(const JYDataPacket &packet)`

- `packet`：输入原始包

### 信号 `void alignedReady(const JYAlignedBatch &batch)`

- `batch`：成功对齐后的批次

### 信号 `void packetDropped(JYDeviceKind kind, const QString &reason)`

- `kind`：被丢弃的数据设备类型
- `reason`：丢弃原因

---

## 5.6 `jydeviceadapter.h`

### `virtual JYDeviceKind kind() const = 0`

- 返回值：当前适配器对应设备类型

### `virtual bool configure(const JYDeviceConfig &config, QString *error) = 0`

- `config`：设备配置
- `error`：失败时返回错误文本
- 返回值：是否成功

### `virtual bool start(QString *error) = 0`

- `error`：失败信息
- 返回值：是否成功

### `virtual bool trigger(QString *error) = 0`

- `error`：失败信息
- 返回值：是否成功

### `virtual bool stop(QString *error) = 0`

- `error`：失败信息
- 返回值：是否成功

### `virtual bool close(QString *error) = 0`

- `error`：失败信息
- 返回值：是否成功

### `virtual bool read(JYDataPacket *out, QString *error) = 0`

- `out`：输出数据包
- `error`：失败信息
- 返回值：是否成功读到有效数据

### `std::unique_ptr<JYDeviceAdapter> createJY532xAdapter(JYDeviceKind kind)`

- `kind`：`PXIe5322` 或 `PXIe5323`
- 返回值：对应适配器实例

### `std::unique_ptr<JYDeviceAdapter> createJY5711Adapter()`

- 返回值：5711 适配器

### `std::unique_ptr<JYDeviceAdapter> createJY8902Adapter()`

- 返回值：8902 适配器

---

## 5.7 `jydeviceconfigutils.h`

### `QString jyDeviceStateText(JYDeviceState state, const QString &message)`

- `state`：设备状态
- `message`：附带错误或提示文本
- 返回值：界面可显示文本

### `JYDeviceConfig build532xInitConfig(JYDeviceKind kind)`

- `kind`：5322 或 5323
- 返回值：默认初始化配置

### `JYDeviceConfig build5711InitConfig()`

- 返回值：5711 默认初始化配置

### `JYDeviceConfig build8902InitConfig()`

- 返回值：8902 默认初始化配置

---

## 5.8 `jydevicetype.h`

### `inline JY5711WaveformConfig build5711WaveformConfig(int channel, const QString &waveformId, const QMap<QString, double> &params = {})`

- `channel`：输出通道号
- `waveformId`：波形 ID
- `params`：波形参数表
- 返回值：已补齐默认参数的波形配置

---

## 5.9 `5711waveformconfig.h`

### 波形生成基类

#### `virtual double Waveform::generate(int sampleIndex, int samplesRate) = 0`

- `sampleIndex`：当前样本序号
- `samplesRate`：采样率
- 返回值：该样本点输出值

### 波形注册与查询

#### `double PXIe5711_param_value(const QMap<QString, double> &values, const QString &key, double fallback = 0.0)`

- `values`：参数表
- `key`：参数名
- `fallback`：缺省值
- 返回值：参数值

#### `const QVector<PXIe5711WaveformDefinition> &PXIe5711_waveform_registry()`

- 返回值：所有内置波形定义

#### `const PXIe5711WaveformDefinition *PXIe5711_find_waveform(const QString &waveformId)`

- `waveformId`：波形 ID
- 返回值：波形定义指针；找不到返回 `nullptr`

#### `QString PXIe5711_default_waveform_id()`

- 返回值：默认波形 ID

#### `QString PXIe5711_resolve_waveform_id(const QString &token)`

- `token`：波形 ID 或显示名
- 返回值：标准波形 ID

#### `QString PXIe5711_waveform_display_name(const QString &waveformId)`

- `waveformId`：波形 ID
- 返回值：显示名称

#### `QVector<QString> PXIe5711_waveform_ids()`

- 返回值：所有波形 ID

#### `QVector<PXIe5711ParamSpec> PXIe5711_waveform_param_specs(const QString &waveformId)`

- `waveformId`：波形 ID
- 返回值：该波形所需参数规格

#### `QMap<QString, double> PXIe5711_default_param_map(const QString &waveformId)`

- `waveformId`：波形 ID
- 返回值：该波形默认参数表

#### `QMap<QString, double> PXIe5711_merge_params(const QString &waveformId, const QMap<QString, double> &values)`

- `waveformId`：波形 ID
- `values`：外部传入参数
- 返回值：补齐默认项后的完整参数表

#### `std::unique_ptr<Waveform> PXIe5711_create_waveform(const QString &waveformId, const QMap<QString, double> &params)`

- `waveformId`：波形 ID
- `params`：参数表
- 返回值：具体波形对象

#### `QMap<QString, double> PXIe5711_make_params(std::initializer_list<std::pair<const char *, double>> items)`

- `items`：便捷参数初始化列表
- 返回值：参数表

---

## 5.10 `jydatachannel.h`

### `void push(const T &value)`

- `value`：入队数据

### `bool tryPop(T *out)`

- `out`：输出最旧数据
- 返回值：是否成功

### `bool tryPopLatest(T *out)`

- `out`：输出最新数据
- 返回值：是否成功

### `void clear()`

- 作用：清空队列

### `size_t size() const`

- 返回值：当前队列长度

## 6. 推荐使用方式

### 6.1 只需要设备同步启动

- 用 `JYThreadManager` 创建 worker
- 用 `orchestrator()->synchronizeStart(...)`
- 用 `orchestrator()->synchronizeStop(...)`

### 6.2 需要原始数据

- 监听 `pipeline()->packetReady(...)`

### 6.3 需要对齐后的联合分析数据

- 先设置：
  - `pipeline()->setExpectedKinds(...)`
  - `pipeline()->setAlignSettings(...)`
  - `pipeline()->setSyncAnchorMs(...)`
- 再监听 `pipeline()->alignedBatchReady(...)`

## 7. 当前实现上的注意点

- `PXIe-5711` 是输出设备，`read()` 基本为空操作，不会产生有效采样包
- `PXIe-5322` 和 `PXIe-5323` 虽然共用配置结构，但采样率由代码强制区分
- `JYDataAligner` 当前采用**线性插值**，不是零阶保持
- `alignedReady` 成功发出后，`m_latest` 会被清空，下一批需要重新等包到齐
- `JYDataPipeline::validate()` 只做基础结构校验，更复杂的时间/采样率约束在对齐器中处理
