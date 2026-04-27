# shorter_conn_intervals_test 修改版本记录

本文档用于记录本工程在本轮协作中的每一版修改内容，并同时记录对应的用户要求。

说明：

- 本文档偏重版本演进记录。
- [modify.rst](d:/workspace/26_work/shorter_conn_intervals_test/modify.rst) 偏重某次提交的代码前后对比说明。
- 本文档记录当前最新修复内容，提交状态以各版本条目为准。

## 版本总览

| 版本 | 用户要求 | 实际修改 | 提交状态 |
| --- | --- | --- | --- |
| V1 | 当主机和从机之间蓝牙连接断开后立即重新连接，并且把连接间隔更新到 750 us | 增加断开后立即重连逻辑；在重连后请求更短连接间隔；将初始连接区间保留在 10 ms，目标短间隔设为 750 us | 已进入历史版本 |
| V2 | 这份代码基于 NCS 3.2.3，在 nrf54l15 dk 上运行 | 排查并修复 sysbuild 子镜像解析到错误 SDK 路径的问题；修复构建环境继承逻辑，使 clean build 能在 NCS 3.2.3 下通过 | 已提交到 git 历史 |
| V3 | 在没有连接成功，或者断开连接了后 LED0、LED1 全部关闭，连接成功后 LED0 点亮 | 统一 LED 状态控制；断开或未连接时关闭 LED0/LED1；连接成功时点亮 LED0 | 已进入历史版本 |
| V4 | LED1 按照蓝牙通信的迟延进行闪烁，当通信断开时 LED1 熄灭 | 恢复 LED1 时延脉冲行为；请求时点亮 LED1，收到响应或超时后关闭；断连时强制熄灭 | 已进入历史版本 |
| V5 | 修改到按 BUTTON0 时和输入 p 功能相同，按 BUTTON1 时和输入 c 功能相同 | 初步加入按键选主从逻辑 | 后续被 V6 覆盖 |
| V6 | 保留原来的输入 p 和 c 选择主机或从机功能，同时增加 BUTTON0 和 BUTTON1 来选择主机或从机 | 增加按钮中断和角色选择同步逻辑；BUTTON0 等效 peripheral，BUTTON1 等效 central；保留串口 p/c 输入；先到先得 | 已进入历史版本 |
| V7 | 优化代码，使蓝牙传输延时的抖动性更小，保持稳定性和延时一致性和延时尽可能小 | 做过一轮更激进的低抖动改造，包括事件驱动测量和角色职责调整 | 后续按用户要求撤销 |
| V8 | 上面这一次修改请取消，退回到修改前的状态 | 撤销上一轮未确认的低抖动优化，保留此前已经确认的功能 | 已进入历史版本 |
| V9 | 把蓝牙物理层改为 1 Mbps 模式，优化代码使蓝牙传输延时抖动变小 | 将 PHY 从 2M 调整到 1M；FSU 目标 PHY 改为 1M；时延测量改为信号量驱动；按批统计 avg/min/max/jitter；central 作为主动发起侧 | 已提交：7421737 |
| V10 | 继续修改，连接间隔最大不能超过 1 ms，在 1 Mbps 下优化传输延时抖动 | 将目标间隔限制为 1000 us；控制器 event length 调整为 1000 us；关闭连接事件自动扩展；生成对比文档并整理为 rst | 已提交：7421737、b4a8ce3 |
| V11 | 代码运行时报 HCI 0x11 / BT_HCI_ERR_UNSUPP_FEATURE_PARAM_VAL，要求修复 | 将精确的 1000 us 单点 SCI 请求改为有界区间请求，让控制器在 common_min_interval_us 到 1000 us 之间选择可接受值，避免 1M PHY 下精确请求被拒绝 | 已修复并完成编译验证 |
| V12 | 两块板联调时 central 侧仍报 opcode 0x20a1 status 0x11，要求继续修复 | 将 app 和 ipc_radio 的控制器保留 ACL event length 及 central ACL event spacing 从 1000 us 下调到 875 us，为 1M PHY 下的 SCI 请求留出调度余量，避免 `Connection_Interval_Max < connIntervalRequired` | 已修复并完成编译验证；待双板回归 |
| V13 | 调整到 875 us 后双板联调仍报 opcode 0x20a1 status 0x11，要求继续修复 | 将 `LE Connection Rate Request` 里的 `max_ce_len_125us` 从规范最大值收紧到当前 interval 上限与控制器预留 event length 的较小值，避免 1M PHY 下向控制器申明过大的连接事件长度 | 已修复并完成编译验证；待双板回归 |
| V14 | 双板联调仍报 opcode 0x20a1 status 0x11，要求继续修复 | 确认当前 build 的 controller 默认 `CONFIG_BT_CTLR_DATA_LENGTH_MAX=69`，将其在 app 和 ipc_radio 配置中显式下调到 `35`，让 1M PHY 下的加密 ACL 包对仍可落进 `<= 1 ms` 的 SCI 目标窗口 | 已修复并完成编译验证；待双板回归 |
| V15 | 双板联调仍报 opcode 0x20a1 status 0x11，要求继续修复 | 使用 `bt_conn_le_read_min_conn_interval_groups()` 读取 local controller 支持的 SCI interval groups，请求前从 group 中挑选本地明确支持的 `<= 1 ms` interval；若当前 controller 在该范围内无合法值，则停止发送必失败的 `0x20a1` 请求并打印明确日志 | 已修复并完成编译验证；待双板回归 |
| V16 | group 选择后仍报 opcode 0x20a1 status 0x11，要求继续修复 | 根据官方 sample 的时序将 SCI request 移回 `1M PHY` 切换之前，先在当前更宽松的 PHY 条件下完成 connection rate update，再切到 `1M` 并做 `1M FSU`，避免 controller 在 `1M` 条件下计算出更高的 `connIntervalRequired` | 已修复并完成编译验证；待双板回归 |
| V17 | request 已在 1M 切换前发送，但仍报 opcode 0x20a1 status 0x11，要求继续修复 | 对照官方 sample 发现 request 前还缺少 `2M FSU`；将 `select_lowest_frame_space()` 参数化，并在 SCI request 前先做 `2M` frame space negotiation，SCI 成功后再切 `1M` 并做 `1M FSU` | 已修复并完成编译验证；待双板回归 |
| V18 | request 已成功发出但 50 ms 内未等到回调，要求继续修复 | 为 SCI procedure 增加独立的 `CONN_RATE_CHANGE_TIMEOUT_MS=1000`，不再复用 latency 的 50 ms；同时记录 `conn_rate_changed` 的实际 status，避免“事件晚到/失败”被误判成同一种错误 | 已修复并完成编译验证；待双板回归 |
| V19 | SCI 已成功切到 750 us，但后续切 1M PHY 失败，要求继续修复 | 记录当前生效 interval；当 controller 已接受 `<= 1 ms` 的最短 SCI interval 时，不再强制切 `1M PHY`，而是保留 `2M PHY` 继续运行，避免 `opcode 0x2032 status 0x09` 的重复失败 | 已修复并完成编译验证；待双板回归 |
| V20 | 从双板日志看 1250 us 放宽后切 1M PHY 已运行 OK，要求提交这一版本 | 在 SCI 成功后先把 interval 放宽到 `> 1 ms`，确认 `1250 us -> 1M PHY` 过渡路径可稳定运行，并以该版本作为当前稳定版本提交 | 已完成双板回归验证；本次提交 |
| V21 | 增加一个 Button3，在 `750 us + 2M PHY` 和 `1250 us + 1M PHY` 之间切换 | 新增 `BUTTON3` 运行时切换逻辑，在 central 侧 latency 循环中安全执行 profile 切换，并复用已有 SCI / PHY / FSU helper 在两种已验证 profile 间切换 | 已完成编译验证；本次提交 |
| V22 | 优化 `jitter` 大但 `backoff` 仍为 0 ms 的场景，并让主从 LED 状态更一致 | 将 central 侧 latency request 改为基于当前 connection interval 的固定节拍发送，避免“响应后立即重发”导致的 connection event 相位漂移；同时为 peripheral 增加 latency service 回调，用同一笔 latency 事务驱动 LED1 脉冲 | 已完成编译验证；本次提交 |
| V23 | 修复 V22 引入的 latency 偏大与 profile 切换首窗混样问题 | 用 controller anchor 对齐的 radio notification prepare 回调替代“任意相位固定节拍”，让 central 在真实 connection event 前发送 request；同时在 profile 切换成功后重置 latency 统计窗口，避免旧 profile 样本混入新 profile 日志 | 已完成编译验证；本次提交 |
| V24 | 修正 anchor-slot 方案中的固定 latency 偏置 | 将 latency payload 的时间戳基准从“request API 调用时刻”改成“prepare 回调预测的 event 起点时刻”，消除 prepare 预留时间被算进 latency 的固定偏置 | 已完成编译验证；本次提交 |
| V25 | 修复 `Unhandled vendor-specific event 0x82` | 发现 `bt_hci_register_vnd_evt_cb()` 只有一个全局回调，QoS 回调覆盖了 anchor-point 回调；改为在应用侧使用单一 VS 分发回调，同时处理 QoS report 和 anchor-point report，消除 `0x82` 未处理告警 | 已完成编译验证；本次提交 |
| V26 | 继续收敛 profile 运行时的大幅 jitter | 根据双板日志确认离群值约等于“一整个 connection interval 再除以 2”，说明 request 偶发滑到了下一个 connection event；将固定 `300 us` prepare 提前量改成随当前 interval 自适应的更晚 slot，给上一笔 response 返回和主线程重新入队留出更多时间 | 已完成编译验证；本次提交 |
| V27 | 简化 latency 日志并让主从都能用 Button3 切换 profile | 将 latency 统计日志收敛为仅输出平均值与 jitter；移除 Button3 的 central-only 限制，让 central 和 peripheral 都进入同一条 runtime profile 控制路径，谁按下 Button3 就由谁发起切换 | 已完成编译验证；本次提交 |
| V28 | 修复从机侧 latency slot 超时与 750 us 切换失败 | 放开 peripheral 侧的 anchor-slot 放行条件，避免进入 runtime loop 后长期等不到 slot；同时将 peripheral 发起的 `750 us + 2M PHY` 切换改为有界区间请求，避免严格 `750 us` 单点导致 `opcode 0x20a1 status 0x11` | 已完成编译验证；本次提交 |
| V29 | 恢复为单边输出传输 latency，消除主从显示不同步 | 回退为当时“只输出 Transmission Latency”的方式：仅 initiator 侧继续发送 latency request 并打印传输 latency；另一侧仅保留 Button3 的 runtime profile 切换能力，不再参与 latency 发送与统计 | 已完成编译验证；本次提交 |
| V30 | 去掉从机侧 Button3 的 PHY 切换能力 | 将 Button3 入口重新收紧为 initiator-only，只保留 central 侧切换 `750 us + 2M PHY` / `1250 us + 1M PHY` 的能力；peripheral 不再响应本地 Button3 触发的 PHY/profile 切换 | 已完成编译验证；本次提交 |

## 详细版本记录

## V1 断连立即重连并尝试 750 us

### 用户要求

当主机和从机之间蓝牙连接断开后立即重新连接，并且把连接间隔更新到 750 us。

### 实际修改

- 在连接断开回调中加入立即重连逻辑。
- central 断连后重新扫描，peripheral 断连后重新广播。
- 将短连接目标区间设置为 750 us。
- 调整连接参数更新流程，避免继续沿用原始区间轮询逻辑。

### 结果

- 功能逻辑完成。
- 后续在实际 NCS 3.2.3 环境下继续演进。

## V2 NCS 3.2.3 构建环境修复

### 用户要求

这份代码是基于 NCS 3.2.3 版本，在 nrf54l15 dk 上运行。

### 实际修改

- 发现工程中子镜像和缓存有混用旧 SDK 路径的问题。
- 修复 [CMakeLists.txt](d:/workspace/26_work/shorter_conn_intervals_test/CMakeLists.txt) 中 sysbuild 子镜像继承 `Zephyr_DIR` 和 `ZEPHYR_BASE` 的逻辑。
- 使用 NCS 3.2.3 工具链完成 clean build 验证。

### 结果

- 构建环境稳定到 NCS 3.2.3。
- 后续所有功能修改都以该环境为基础。

## V3 LED 连接状态指示

### 用户要求

在没有连接成功，或者断开连接了后 LED0，LED1 全部都设置关闭，连接成功后 LED0 点亮。

### 实际修改

- 增加统一的 LED 状态设置逻辑。
- 未连接或断开时同时关闭 LED0 和 LED1。
- 建立连接后点亮 LED0。

### 结果

- LED0 成为连接状态灯。
- LED1 为后续时延脉冲逻辑保留。

## V4 LED1 时延闪烁

### 用户要求

LED1 按照蓝牙通信的迟延进行闪烁，当通信断开时 LED1 熄灭。

### 实际修改

- latency request 发出时点亮 LED1。
- latency response 收到后关闭 LED1。
- 超时、失败和断开时都强制关闭 LED1。

### 结果

- LED1 成为链路时延活动指示灯。

## V5 按钮版主从选择

### 用户要求

修改到按 BUTTON0 时和输入 p 功能相同，按 BUTTON1 时和输入 c 功能相同，即由 button 设置主机或从机。

### 实际修改

- 增加按键中断。
- BUTTON0 映射为 peripheral。
- BUTTON1 映射为 central。

### 结果

- 初步实现按键选角色。
- 下一版根据用户补充要求保留串口输入能力。

## V6 保留 p/c 并增加按钮

### 用户要求

在保留原来的输入 p 和 c 选择主机或从机功能的同时，再增加 BUTTON0 和 BUTTON1 来选择主机或从机。

### 实际修改

- 保留串口 `p/c` 选择角色。
- 增加 BUTTON0 和 BUTTON1 选择角色。
- 增加角色选择同步机制，避免按键与串口同时输入导致状态冲突。
- 以首个有效输入为准。

### 结果

- 串口和按键两种选择方式同时生效。

## V7 第一轮低抖动优化尝试

### 用户要求

功能都实现 OK 了，再帮我优化一下代码，使蓝牙传输延时的抖动性更小，保持主机和从机之间传输的稳定性和延时的一致性和延时尽可能小。

### 实际修改

- 做过一轮更激进的低抖动设计调整。
- 主要方向包括事件驱动等待、角色职责强化和统计方式调整。

### 结果

- 该轮方案未最终保留。
- 下一版按用户要求撤销。

## V8 撤销上一轮未确认优化

### 用户要求

上面这一次修改请取消，退回到修改前的状态。

### 实际修改

- 撤销上一轮抖动优化改造。
- 保留此前已确认的重连、LED、角色选择功能。

### 结果

- 回到更稳定的功能基线。

## V9 1 Mbps PHY 与事件驱动时延测量

### 用户要求

请修改 shorter_conn_intervals_test 程序，把蓝牙物理层改为 1 Mbps 模式，优化代码使蓝牙传输延时抖动变小。

### 实际修改

- [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c) 中将 PHY 更新由 2M 改为 1M。
- 将 FSU 目标 PHY 从 `BT_HCI_LE_FRAME_SPACE_UPDATE_PHY_2M_MASK` 改为 `BT_HCI_LE_FRAME_SPACE_UPDATE_PHY_1M_MASK`。
- 将目标 interval 改为 1000 us。
- 增加 `conn_rate_changed_sem` 和 `latency_response_sem`，将 latency 测量改为事件驱动。
- 增加 `latency_stats_reset()` 和 `latency_stats_add()`，将逐次打印改为按批统计。
- 将 central 明确为主动发起连接参数更新的一侧。

### 结果

- 对应主要提交为 `7421737`。

## V10 1 Mbps 下限制最大连接间隔不超过 1 ms

### 用户要求

继续修改，连接间隔最大不能超过 1 ms，在 1 Mbps 下优化传输延时抖动。

### 实际修改

- 将目标 interval 固定为 1000 us。
- 在 [prj.conf](d:/workspace/26_work/shorter_conn_intervals_test/prj.conf) 中设置 `CONFIG_BT_CTLR_SDC_MAX_CONN_EVENT_LEN_DEFAULT=1000`。
- 在 [sysbuild/ipc_radio/prj.conf](d:/workspace/26_work/shorter_conn_intervals_test/sysbuild/ipc_radio/prj.conf) 中同步设置 `CONFIG_BT_CTLR_SDC_MAX_CONN_EVENT_LEN_DEFAULT=1000`。
- 在应用侧和 radio 子镜像侧都关闭 `CONFIG_BT_CTLR_SDC_CONN_EVENT_EXTEND_DEFAULT`。
- 生成提交对比文档，并整理为 [modify.rst](d:/workspace/26_work/shorter_conn_intervals_test/modify.rst)。

### 结果

- 相关提交仍在 `7421737` 中。
- 文档提交为 `b4a8ce3`。

## V11 运行时修复：避免精确 1000 us 请求被控制器拒绝

### 用户要求

代码运行时 log 提示错误如下：

- `Opcode 0x20 status 0x11 BT HCI ERR UNSUPP FEATURE PARAM VAL`
- `Connection rate request failed (err -5)`

### 问题分析

- 当前代码在 1M PHY 下将 SCI 请求写成精确的 `1000 us -> 1000 us`。
- 但 `bt_conn_le_read_min_conn_interval()` 只能告诉我们最小支持值是 750 us，不能保证 1M PHY 下一定支持精确 1000 us 这个单点。
- 因此控制器在接收单点精确请求时返回 `0x11`，即 Unsupported Feature or Parameter Value。

### 实际修改

- 在 [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c) 的 `test_run()` 中，将 `conn_rate_request(requested_interval_us, requested_interval_us)` 改成区间请求。
- 如果 `common_min_interval_us` 小于 `requested_interval_us`，则请求 `common_min_interval_us .. requested_interval_us`。
- 保证区间上限仍然是 1000 us，不超过用户要求的 1 ms。
- 增加新的日志输出：如果是区间请求，打印 `Requesting new connection interval range: min us to max us`。

### 结果

- 当前版本已编译验证通过。
- 本版修复将与 [modify.md](d:/workspace/26_work/shorter_conn_intervals_test/modify.md) 一并提交到 git。

## V12 双板联调修复：为 1M PHY 下的 SCI 请求留出调度余量

### 用户要求

两块板测试时，主机端运行日志仍报以下错误，要求继续修复：

- `opcode 0x20a1 status 0x11 BT_HCI_ERR_UNSUPP_FEATURE_PARAM_VAL`
- `Connection rate request failed (err -5)`

### 问题分析

- `0x20a1` 对应 `LE Connection Rate Request`。
- 根据控制器命令定义，`0x11` 在这里对应 `Connection_Interval_Max < connIntervalRequired`，说明控制器认为当前请求上限 `1000 us` 小于它在当前调度配置下所需的最小 interval。
- 当前工程在 app 和 radio 子镜像里都把 `CONFIG_BT_CTLR_SDC_MAX_CONN_EVENT_LEN_DEFAULT` 固定为 `1000`，而 central ACL event spacing 也随之为 `1000 us`。
- 在 1M PHY、加密链路和最低 frame space 组合下，控制器需要额外调度余量，因此精确卡在 `1000 us` 的保留窗口会把 `connIntervalRequired` 顶到大于 `1000 us`，导致区间请求 `750 us .. 1000 us` 被拒绝。

### 实际修改

- 将 [prj.conf](d:/workspace/26_work/shorter_conn_intervals_test/prj.conf) 中 `CONFIG_BT_CTLR_SDC_MAX_CONN_EVENT_LEN_DEFAULT` 从 `1000` 下调到 `875`。
- 在 [prj.conf](d:/workspace/26_work/shorter_conn_intervals_test/prj.conf) 中显式增加 `CONFIG_BT_CTLR_SDC_CENTRAL_ACL_EVENT_SPACING_DEFAULT=875`。
- 将 [prj.conf](d:/workspace/26_work/shorter_conn_intervals_test/sysbuild/ipc_radio/prj.conf) 中对应的 radio 子镜像配置同步下调到同样的 `875 us`。
- 保持连接间隔请求上限仍然不超过 `1 ms`，只减少控制器预留窗口，给 SCI 调度留下余量。

### 结果

- 已使用现有 `build_1` 完成重编译验证，通过。
- 重编译后的 `.config` 已确认：
	- `CONFIG_BT_CTLR_SDC_MAX_CONN_EVENT_LEN_DEFAULT=875`
	- `CONFIG_BT_CTLR_SDC_CENTRAL_ACL_EVENT_SPACING_DEFAULT=875`
- 当前版本待重新进行双板回归验证，确认 `LE Connection Rate Request` 不再被控制器拒绝。

## V13 双板联调继续修复：收紧 LE Connection Rate Request 的 CE length 参数

### 用户要求

在将控制器保留 ACL event length 和 central ACL spacing 下调到 `875 us` 后，两块板测试日志仍报以下错误，要求继续修复：

- `opcode 0x20a1 status 0x11 BT_HCI_ERR_UNSUPP_FEATURE_PARAM_VAL`
- `Connection rate request failed (err -5)`

### 问题分析

- 当前 build 中 `875 us` 配置已经生效，说明问题不再只是控制器默认预留窗口过大。
- `LE Connection Rate Request` 中的 `min_ce_len_125us` / `max_ce_len_125us` 仍沿用规范宏：
	- `BT_HCI_LE_SCI_CE_LEN_MIN_125US = 0x0001`
	- `BT_HCI_LE_SCI_CE_LEN_MAX_125US = 0x3E7F`
- 在 2M PHY 的官方 sample 中使用规范最大值通常没有问题，但在当前 `1M PHY + <=1 ms interval ceiling + 已收紧 controller event length` 场景下，继续向控制器声明一个极大的 `max_ce_len_125us` 很可能会使该请求在调度上不再自洽。
- 因此本轮改为将 `max_ce_len_125us` 收紧到“当前 interval 上限”和“控制器预留 event length”中的较小值，避免请求里附带一个远大于当前窗口的最大连接事件长度。

### 实际修改

- 在 [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c) 中新增 `conn_rate_max_ce_len_125us()` 辅助函数。
- 该函数将 `max_ce_len_125us` 计算为：
	- `interval_max_us / 125`
	- `CONFIG_BT_CTLR_SDC_MAX_CONN_EVENT_LEN_DEFAULT / 125`
	两者中的较小值，并保证不低于 `BT_HCI_LE_SCI_CE_LEN_MIN_125US`。
- 在 `set_conn_rate_defaults()` 中不再使用 `BT_HCI_LE_SCI_CE_LEN_MAX_125US`，改为使用上述收紧后的值。
- 在 `conn_rate_request()` 中同样改为使用收紧后的 `max_ce_len_125us`。

### 结果

- 已使用现有 `build_1` 完成重编译验证，通过。
- 当前版本待重新进行双板回归验证，确认 `LE Connection Rate Request` 是否不再返回 `opcode 0x20a1 status 0x11`。

## V14 双板联调继续修复：降低 controller 的 ACL data length 上限

### 用户要求

在前两轮收紧 controller event length 和 SCI `max_ce_len_125us` 参数后，central 侧日志仍报以下错误，要求继续修复：

- `opcode 0x20a1 status 0x11 BT_HCI_ERR_UNSUPP_FEATURE_PARAM_VAL`
- `Connection rate request failed (err -5)`

### 问题分析

- 重新检查当前 build 的生成配置后，发现 controller 实际仍在使用默认的 `CONFIG_BT_CTLR_DATA_LENGTH_MAX=69`。
- 官方 [README.rst](d:/workspace/26_work/shorter_conn_intervals_test/README.rst) 对应的 Shorter Connection Intervals sample 默认走 `LE 2M PHY` 路径，而当前工程已被修改为 `LE 1M PHY`。
- 在 `1M PHY + 加密链路 + <=1 ms SCI 目标` 下，69-byte 的 ACL data length 上限会显著抬高运行时 `connIntervalRequired`，从而触发 `Connection_Interval_Max < connIntervalRequired`，即 `opcode 0x20a1 status 0x11`。
- 相比前两轮只收紧 event window 和 `max_ce_len_125us`，`CONFIG_BT_CTLR_DATA_LENGTH_MAX` 更直接决定 controller 为链路保留的包时长，因此这一项更接近根因。

### 实际修改

- 在 [prj.conf](d:/workspace/26_work/shorter_conn_intervals_test/prj.conf) 中显式增加 `CONFIG_BT_CTLR_DATA_LENGTH_MAX=35`。
- 在 [prj.conf](d:/workspace/26_work/shorter_conn_intervals_test/sysbuild/ipc_radio/prj.conf) 中同步增加 `CONFIG_BT_CTLR_DATA_LENGTH_MAX=35`。
- 选择 `35` 而不是直接回落到 `27`，是为了在保持 `1M PHY` 下 `<=1 ms` 可调度性的同时，保留比最小值更高的 ACL 吞吐余量。
- 保留前两轮已经加入的 `875 us` controller event length / central ACL spacing，以及收紧后的 SCI `max_ce_len_125us` 逻辑。

### 结果

- 已使用现有 `build_1` 完成重编译验证，通过。
- 重编译后的 `.config` 已确认：
	- `CONFIG_BT_CTLR_DATA_LENGTH_MAX=35`
	- `CONFIG_BT_CTLR_SDC_MAX_CONN_EVENT_LEN_DEFAULT=875`
	- `CONFIG_BT_CTLR_SDC_CENTRAL_ACL_EVENT_SPACING_DEFAULT=875`
- 当前版本待重新进行双板回归验证，确认 `LE Connection Rate Request` 不再返回 `opcode 0x20a1 status 0x11`。

## V15 双板联调继续修复：按 controller 支持的 SCI interval groups 选择请求值

### 用户要求

在前几轮已经收紧 controller event length、SCI `max_ce_len_125us` 和 ACL data length 上限后，central 侧日志仍报以下错误，要求继续修复：

- `opcode 0x20a1 status 0x11 BT_HCI_ERR_UNSUPP_FEATURE_PARAM_VAL`
- `Connection rate request failed (err -5)`

### 问题分析

- 检查 Zephyr host 实现后，`bt_conn_le_conn_rate_request()` 本身只是把参数透传给 `BT_HCI_OP_LE_CONNECTION_RATE_REQUEST`，不会替应用自动筛选 interval。
- 官方 Shorter Connection Intervals sample 的成功路径是 `LE 2M PHY + 2M FSU + exact request`；当前工程已偏离成 `LE 1M PHY + 1M FSU + <= 1 ms ceiling`，因此不能再假设 `750..1000 us` 这一整段对 controller 都合法。
- Zephyr 提供了 `bt_conn_le_read_min_conn_interval_groups()`，可以直接读取 local controller 支持的 SCI interval groups。
- 继续盲发 `750..1000 us` 的 range request 只会重复触发 `0x20a1`；更稳妥的做法是先读取 local group，再从中选出一个 controller 明确支持且位于目标范围内的 interval。

### 实际修改

- 在 [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c) 中增加 `local_min_interval_info` 和 `local_min_interval_groups_valid`，保存 local controller 返回的 SCI group 信息。
- 启动时优先调用 `bt_conn_le_read_min_conn_interval_groups()`，成功后打印每个本地支持 group 的区间和步进；若该 API 不可用，再回退到原来的 `bt_conn_le_read_min_conn_interval()`。
- 新增 `first_group_interval_in_range_us()` 与 `select_local_supported_interval_us()`，在请求发出前从 local groups 中选出一个位于请求范围内的最小合法 interval。
- 如果当前 local controller 在 `request_min_us .. request_max_us` 范围内根本没有合法 SCI interval，则不再发送 `0x20a1`，而是直接打印：
	- `No local SCI interval supported in range ...`
- 若找到合法值，则把 range request 收敛为 exact request，并打印：
	- `Selected local supported SCI interval: ... us`

### 结果

- 已使用现有 `build_1` 完成重编译验证，通过。
- 当前版本待重新进行双板回归验证。新的关键日志分叉是：
	- 如果看到 `Selected local supported SCI interval: ... us`，说明 controller group 里存在 `<= 1 ms` 的合法值，接下来观察 `0x20a1` 是否消失。
	- 如果看到 `No local SCI interval supported in range 750 us to 1000 us`，则可基本确认在当前 `1M PHY` 路径下，local controller 本身不支持 `<= 1 ms` 的 SCI 请求，需要在“回到 2M PHY”与“放宽上限大于 1 ms”之间二选一。

## V16 双板联调继续修复：先做 SCI，再切 1M PHY

### 用户要求

在已经打印出 local SCI groups 且明确选择 `750 us` 后，central 侧依旧报以下错误，要求继续修复：

- `Selected local supported SCI interval: 750 us`
- `opcode 0x20a1 status 0x11 BT_HCI_ERR_UNSUPP_FEATURE_PARAM_VAL`

### 问题分析

- 新日志已经证明 `750 us` 本身位于 local controller 声明支持的 SCI group 内，不是 interval 值非法。
- 对照官方 Shorter Connection Intervals sample，成功路径是先在更宽松的 PHY 条件下完成 SCI，再去做后续 PHY / FSU 变化；注释中也明确写到最低 interval 需要在 `2M PHY` 条件下达成。
- 当前工程的失败路径则是：在发 `LE Connection Rate Request` 之前，已经先切到了 `1M PHY` 并完成了 `1M FSU`。
- 因此更合理的根因是：`0x20a1 status 0x11` 不是因为 `750 us` 值不在支持集合，而是 controller 在当前 `1M PHY` 链路条件下计算出的 `connIntervalRequired` 高于 `750 us`。

### 实际修改

- 调整 [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c) 中 `test_run()` 的时序。
- 现在先完成：
	- remote min interval 读取
	- local group 过滤
	- `conn_rate_request()`
	- 等待 `conn_rate_changed_sem`
- 只有在 SCI procedure 完成之后，才继续：
	- `update_to_1m_phy()`
	- `select_lowest_frame_space()`
- 这样可以避免 controller 在已经切到 `1M PHY` 的链路条件下拒绝 `750 us` 的 SCI request。

### 结果

- 已使用现有 `build_1` 完成重编译验证，通过。
- 当前版本待重新进行双板回归验证。
- 这轮回归最关键的观察点是：
	- `Requesting new connection interval: 750 us` 后是否出现 `Connection rate changed: interval 750 us ...`
	- 随后 `LE PHY updated: TX PHY LE 1M, RX PHY LE 1M` 和 `Frame space updated: ... PHYs: 0x01` 是否还能继续成功

## V17 双板联调继续修复：先做 2M FSU，再发 SCI request

### 用户要求

在将 SCI request 保持在 `1M PHY` 切换之前后，central 侧日志仍报以下错误，要求继续修复：

- `LE PHY updated: TX PHY LE 2M, RX PHY LE 2M`
- `Selected local supported SCI interval: 750 us`
- `Requesting new connection interval: 750 us`
- `opcode 0x20a1 status 0x11 BT_HCI_ERR_UNSUPP_FEATURE_PARAM_VAL`

### 问题分析

- 新日志已经证明失败发生在链路仍处于 `2M PHY` 时，因此 V16 中“已经切到 1M 再发 request”的假设不再成立。
- 继续对照官方 Shorter Connection Intervals sample，发现 sample 在发 SCI request 之前不仅处于 `2M PHY`，还会先完成一次 `2M` 的 Frame Space Update。
- 当前工程此时虽然已经是 `2M PHY`，但日志里在 request 前没有出现 `Frame space updated: ... PHYs: 0x02`，说明 controller 在评估 SCI request 前并未拿到降低后的 `2M` frame space。
- 因此新的局部根因是：`750 us` 并非不在支持 group 中，而是 request 前缺少 sample 要求的 `2M FSU`，controller 仍以更保守的 frame space 计算 `connIntervalRequired`，最终返回 `0x11`。

### 实际修改

- 将 [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c) 中的 `select_lowest_frame_space()` 改为接收 `phys_mask` 参数。
- 在 `test_run()` 中，进入 SCI request 流程前先执行：
	- `select_lowest_frame_space(BT_HCI_LE_FRAME_SPACE_UPDATE_PHY_2M_MASK)`
- 若 `2M FSU` 失败，则直接打印 `2M frame space update failed` 并停止当前轮 request。
- 保留 SCI request 成功后的 `1M` 路径：
	- `update_to_1m_phy()`
	- `select_lowest_frame_space(BT_HCI_LE_FRAME_SPACE_UPDATE_PHY_1M_MASK)`
- 这样 request 前的链路时序变为：
	- `2M PHY`
	- `2M FSU`
	- `SCI request`
	- `1M PHY`
	- `1M FSU`

### 结果

- 已使用现有 `build_1` 完成重编译验证，通过。
- 当前版本待重新进行双板回归验证。
- 下一轮日志最关键的观察点是：
	- request 前是否先出现 `Frame space updated: ... PHYs: 0x02`
	- `Requesting new connection interval: 750 us` 后是否转为 `Connection rate changed: interval 750 us ...`
	- SCI 成功后，`LE PHY updated: TX PHY LE 1M, RX PHY LE 1M` 与 `Frame space updated: ... PHYs: 0x01` 是否继续成功

## V18 双板联调继续修复：把 SCI procedure 的等待窗口与 status 处理独立出来

### 用户要求

在 request 前已经完成 `2M FSU` 后，新的 central 日志不再出现 `0x11` 拒绝，但出现了新的异常，要求继续修复：

- `Requesting new connection interval: 750 us`
- `Timed out waiting for connection rate change`
- 随后又异步出现 `Connection rate changed: interval 750 us, ...`

### 问题分析

- 新日志说明 `LE Connection Rate Request` 已经成功发出，controller 也最终完成了 procedure，因此上一轮关于 request 参数/时序的修复方向是对的。
- 当前真正的问题是应用侧等待窗口仍复用了 `LATENCY_RESPONSE_TIMEOUT_MS=50`，这对一次完整的 SCI procedure 明显过短。
- 因为 `conn_rate_changed` 事件是在 timeout 之后才到达，当前代码会先打印失败并提前返回；而真正的成功回调随后才异步到达，导致“应用误判失败，但 controller 实际成功”的状态不一致。
- 此外，`conn_rate_changed()` 之前只负责打印日志和 `k_sem_give()`，不会把实际 `status` 反馈给 `test_run()`，因此即使未来回调带失败状态，主流程也无法区分“成功完成”和“带失败状态的完成”。

### 实际修改

- 在 [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c) 中新增 `CONN_RATE_CHANGE_TIMEOUT_MS=1000`，将 SCI procedure 的等待窗口与 latency response 的 50 ms 超时分离。
- 增加 `conn_rate_change_status` 变量，在 `conn_rate_changed()` 回调中记录 controller 返回的实际 status。
- 发起 `conn_rate_request()` 前先把 `conn_rate_change_status` 重置为 `BT_HCI_ERR_UNSPECIFIED`。
- 将 `k_sem_take(&conn_rate_changed_sem, ...)` 的等待从 `LATENCY_RESPONSE_TIMEOUT_MS` 改为 `CONN_RATE_CHANGE_TIMEOUT_MS`。
- 等待结束后，显式检查 `conn_rate_change_status`：
	- 若不是 `BT_HCI_ERR_SUCCESS`，则打印明确的 procedure status 并停止后续 `1M PHY` 路径。
	- 只有 status 真正成功时，才继续后面的 `1M PHY` 与 `1M FSU`。

### 结果

- 已使用现有 `build_1` 完成重编译验证，通过。
- 当前版本待重新进行双板回归验证。
- 下一轮日志最关键的观察点是：
	- `Requesting new connection interval: 750 us` 后，是否在 1000 ms 内直接出现 `Connection rate changed: interval 750 us ...`
	- 不应再出现先 timeout、后 success 的顺序错位日志
	- 成功后是否继续出现 `LE PHY updated: TX PHY LE 1M, RX PHY LE 1M` 与 `Frame space updated: ... PHYs: 0x01`

## V19 双板联调继续修复：最短 SCI interval 下保留 2M PHY

### 用户要求

在 `LE Connection Rate Request` 已成功切到 `750 us` 后，新的 central 日志继续出现后续错误，要求继续修复：

- `Connection rate changed: interval 750 us, ...`
- `opcode 0x2032 status 0x09 BT_HCI_ERR_CONN_LIMIT_EXCEEDED`
- `PHY update failed: -111`

### 问题分析

- 这轮日志已经证明前面的 SCI procedure 链路是成功的：
	- `2M FSU` 已成功
	- `conn_rate_request(750 us)` 已成功
	- `conn_rate_changed` 已按预期返回 success
- 新失败点已经完全后移到 SCI 成功之后的 `bt_conn_le_phy_update(... 1M ...)`。
- 结合官方 sample 注释“最低 connection intervals 只能在 2M PHY 达成”和当前 controller 行为，可以确定：
	- 当前 controller 可以在 `2M PHY` 下接受 `750 us`
	- 但在已经处于该最短 SCI interval 时，不接受再切换到 `1M PHY`
- 因此这里的根因不再是 request 参数、FSU 时序或 timeout，而是“最短 SCI interval 与 1M PHY 组合本身不被该 controller 接受”。继续强制切 `1M` 只会稳定触发 `opcode 0x2032 status 0x09`。

### 实际修改

- 在 [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c) 中新增 `active_conn_interval_us`，跟踪当前连接真实生效的 interval。
- 在 `connected()` 中用 `conn_info.le.interval_us` 初始化该值。
- 在 `conn_rate_changed()` 成功回调中，用 `params->interval_us` 更新该值。
- 在 `disconnected()` 中将该值恢复为初始 `INTERVAL_INITIAL_US`。
- 在 SCI procedure 成功之后、准备执行 `update_to_1m_phy()` 之前增加判断：
	- 若 `active_conn_interval_us <= INTERVAL_TARGET_US`，则打印
	  `Skipping 1M PHY update at ... us; controller accepted the shortest SCI interval only on 2M PHY`
	  并保留 `2M PHY` 继续运行
	- 仅当当前 interval 大于 `1 ms` 时，才继续尝试 `1M PHY` 与 `1M FSU`

### 结果

- 已使用现有 `build_1` 完成重编译验证，通过。
- 当前版本待重新进行双板回归验证。
- 下一轮日志最关键的观察点是：
	- `Connection rate changed: interval 750 us ...` 之后，不应再出现 `opcode 0x2032 status 0x09`
	- 应改为出现 `Skipping 1M PHY update at 750 us; ...`
	- 随后 latency 测量应直接在 `2M PHY + 750 us SCI` 组合下继续运行

## V20 双板联调继续修复：先把 interval 放宽到大于 1 ms，再尝试切 1M PHY

### 用户要求

新的 central 日志已经证明 `750 us + 2M PHY` 组合稳定，用户要求继续验证新的过渡路径：

- `Connection rate changed: interval 750 us, ...`
- `Skipping 1M PHY update at 750 us; controller accepted the shortest SCI interval only on 2M PHY`
- latency 日志持续稳定

在此基础上，要求在 SCI 成功后先把 interval 放宽到 `> 1 ms`，再尝试切换到 `1M PHY`。

### 问题分析

- 最新日志说明上一轮的保护逻辑是有效的：当前 controller 的确可以稳定运行在 `750 us + 2M PHY`。
- 之前失败的直接原因，是在最短 `750 us` interval 仍然生效时立即调用 `bt_conn_le_phy_update(... 1M ...)`，从而触发 `opcode 0x2032 status 0x09 BT_HCI_ERR_CONN_LIMIT_EXCEEDED`。
- 这说明“最短 SCI interval 下直接切 1M”不可行，但并不排除“先把 interval 放宽，再切 1M”这个两步过渡路径。
- 因此本轮采用新的局部假设：
	- 先保留已经验证通过的 `750 us + 2M PHY` 作为 SCI 达成路径
	- 在 SCI 成功后，请求一个本地 controller 明确支持、且大于 `1 ms` 的 interval
	- 只有当这个放宽后的 interval 生效后，才再次尝试 `1M PHY`

### 实际修改

- 在 [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c) 中新增 `request_conn_interval_update()`，统一处理 interval request、等待 `conn_rate_changed` 和 status 检查。
- 保留原有的最短 SCI request 流程，仍然先请求最短本地支持的 `750 us`。
- 当 SCI 成功后，如果当前 `active_conn_interval_us <= 1000 us`：
	- 通过本地 interval groups 选择一个 `>= 1250 us` 的支持值
	- 先请求该更宽的 interval
	- 只有该 interval request 成功后，才继续执行 `update_to_1m_phy()`
- 如果没有可用的 `> 1 ms` interval，或者放宽 interval 的 request 失败，则保留当前 PHY 并继续运行 latency 测试，不再中断整个流程。
- 如果后续 `1M PHY` update 或 `1M` 的 frame space update 仍失败，也只记录 warning，并保留当前 PHY 继续运行，避免再次把验证流程直接打断。

### 结果

- 已使用现有 `build_1` 完成重编译验证，通过。
- 已完成双板回归验证，central 侧日志已确认 `Requesting wider connection interval before 1M PHY switch: 1250 us`、`Connection rate changed: interval 1250 us ...` 和 `LE PHY updated: TX PHY LE 1M, RX PHY LE 1M` 路径成立。
- 当前版本确认为稳定版本，本次按该版本提交到 git。

## V21 增加 Button3：在 `750 us + 2M PHY` 和 `1250 us + 1M PHY` 间运行时切换

### 用户要求

增加一个 `Button3`，通过这个按键在 `750 us + 2M PHY` 和 `1250 us + 1M PHY` 两个已验证 profile 之间切换。

### 问题分析

- 当前 demo 在 central 侧已经有两条已验证的链路 profile：
	- `750 us + 2M PHY`
	- `1250 us + 1M PHY`
- 这类切换不能直接在 GPIO ISR 里调用 BLE API，否则会把按钮中断上下文和连接参数更新流程混在一起。
- 因此本轮采用的实现方式是：
	- `BUTTON3` 中断只设置一个待处理切换请求
	- 真正的 interval / PHY / FSU 切换仍放在已有 latency 循环里执行
	- 这样可以复用现有的 SCI、PHY update 和 frame space update 逻辑，同时保持运行时切换路径可控

### 实际修改

- 在 [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c) 中新增 `BUTTON3`，使用 `DT_ALIAS(sw3)` 作为第三个物理按键。
- 新增 `enum link_profile`、`requested_link_profile`、`active_link_profile` 和切换 pending / enable 状态，用来跟踪当前和目标 profile。
- 新增 `switch_to_750us_2m_profile()` 与 `switch_to_1250us_1m_profile()`，分别封装两条切换路径：
	- `750 us + 2M PHY`
	- `1250 us + 1M PHY`
- 在 central 侧 latency 循环中增加 `process_pending_link_profile_switch()`，让 `BUTTON3` 的切换请求在非中断上下文里执行。
- 启动日志中增加提示，明确 `BUTTON3` 用于在两条 profile 间切换。

### 影响文件

- [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c)

### 结果

- 验证：已使用现有 `build_1` 完成编译验证，通过。
- 提交：本次提交。
- 运行时回归时建议重点观察：按下 `BUTTON3` 后，日志是否按预期打印 profile 切换以及对应的 interval / PHY 更新结果。

## V22 优化大抖动窗口并统一主从 LED1 的事务语义

### 用户要求

优化代码，解决 `jitter` 很大但 `backoff` 仍然是 `0 ms`、更像是 connection event 调度或时序相位带来的波动的问题；同时让主机和从机之间的 LED 状态保持一致。

### 问题分析

- 当前 central 侧 latency loop 在收到上一笔 latency response 后，基本立刻就发下一笔 request。
- 当 `qos_tx_backoff_ms = 0` 时，这种“响应驱动重发”会让发送相位跟着上一笔 response 到达时间漂移，而不是锁定在当前 connection interval 的固定节拍上。
- 一旦该相位逐渐漂移到 connection event 边界附近，就会出现某一笔 request 跨到额外的 event 才完成，从而把该统计窗口里的 `latency_max_us` 拉高，表现为 `jitter` 突然明显增大。
- 当前 LED1 只在 central 侧 request / response 路径里驱动，peripheral 侧并没有用同一笔 latency 事务去更新 LED1，因此主从两侧 LED1 的语义并不一致。

### 实际修改

- 在 [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c) 中新增 latency request 固定节拍状态：
	- `latency_request_pacing_interval_us`
	- `latency_request_deadline_cycle`
	- `latency_request_pacing_valid`
- 新增 `latency_request_pacing_reset()` 和 `wait_for_next_latency_request_slot()`，把 central 侧的 request 发送改成“按当前 `active_conn_interval_us` 的绝对节拍发送”，不再让下一笔 request 直接跟着上一笔 response 漂移。
- 在连接建立、连接断开和 profile 切换成功后，重置 latency pacing 状态，避免旧 profile 的相位继续影响新 profile。
- 为 latency service 增加 `bt_latency_cb.latency_request` 回调，在 peripheral 收到 latency write request 时点亮 LED1，并用 delayable work 在一个 connection interval 后关闭 LED1。
- 在 central 侧 response handler 中取消 LED1 的延时关灯 work 并立即灭灯，保证一笔 latency 事务结束后两侧都回到空闲 LED 状态。

### 影响文件

- [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c)
- [modify.md](d:/workspace/26_work/shorter_conn_intervals_test/modify.md)

### 结果

- 验证：已使用现有 `build_1` 执行 `ninja -C build_1`，编译通过。
- 提交：本次提交。
- 运行时回归时建议重点观察：
	- `backoff 0 ms` 时，`latency max` 是否仍会周期性出现跨 event 的大跳变。
	- central 与 peripheral 的 LED1 是否都围绕同一笔 latency 事务同步亮灭。
	- 切换 `BUTTON3` 后，新的 profile 是否会在 1 到 2 个 connection interval 内稳定到新的节拍。

## V23 用 anchor 对齐槽位修复 V22 的 latency 偏大问题

### 用户要求

在 `1250 us -> 1M PHY` 和 `750 us -> 2M PHY` 的运行日志中，发现 V22 后 latency 数值本身变大，而且 profile 切换后的第一条统计窗口混入了旧 profile 的样本，要求继续优化，使 latency 显示恢复正常。

### 问题分析

- V22 的 fixed pacing 只保证“每隔一个 interval 发送一次 request”，但它的初始相位来自应用当前时刻，而不是实际 connection anchor。
- 这会让 central 稳定地落在一个错误的相位上：
	- `1250 us + 1M PHY` 稳定打印约 `1411 us`
	- `750 us + 2M PHY` 稳定打印约 `927 us`
- 这说明 request 的发送时机虽然稳定了，但没有对齐真实 connection event，导致每一笔 latency 都额外包含了一段固定等待时间。
- 同时，profile 切换成功后没有清空 latency 统计窗口，因此切换后的第一条日志会把旧 profile 和新 profile 的样本混在一起，表现为 `min/max/jitter` 明显异常。

### 实际修改

- 在 [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c) 中移除 V22 的“任意相位 fixed pacing”实现。
- 改为使用 NCS 提供的 `bt_radio_notification_conn_cb_register()`，在每个 connection event 开始前 `300 us` 触发 `prepare` 回调。
- central 侧 latency loop 不再自己推算绝对节拍，而是等待下一次由 controller anchor 对齐的 request slot，再立即调用 `bt_latency_request()`。
- 在 profile 切换成功后新增 `latency_stats_reset()`，避免切换前后的样本混入同一个统计窗口。
- 为支持上述 feature，同步在 app 和 `sysbuild/ipc_radio` 侧补齐 anchor-point report 相关配置。

### 影响文件

- [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c)
- [prj.conf](d:/workspace/26_work/shorter_conn_intervals_test/prj.conf)
- [sysbuild/ipc_radio/prj.conf](d:/workspace/26_work/shorter_conn_intervals_test/sysbuild/ipc_radio/prj.conf)
- [modify.md](d:/workspace/26_work/shorter_conn_intervals_test/modify.md)

### 结果

- 验证：已使用现有 `build_1` 完成编译验证，通过。
- 提交：本次提交。
- 运行时回归时建议重点观察：
	- `1250 us + 1M PHY` 是否回落到更接近真实链路事务时间的稳定值，而不是固定在约 `1411 us`。
	- `750 us + 2M PHY` 是否不再固定在约 `927 us`，同时保持低抖动。
	- `BUTTON3` 切换后的第一条 latency 日志是否已经不再混入前一 profile 的 `min/max`。

## V24 将 latency 时间戳改为 event 对齐参考时间

### 用户要求

继续修复 `1250 us + 1M PHY` 与 `750 us + 2M PHY` 下 latency 数值整体偏大的问题，使打印出来的 latency 更接近真实链路事务时间。

### 问题分析

- V23 已经把 request 发送节拍对齐到 controller anchor，但 latency payload 里仍然写入“调用 `bt_latency_request()` 的当前时刻”。
- 由于 prepare 回调本来就是在 connection event 开始前一段时间触发，当前时刻会天然早于真正的 on-air 发送时刻。
- 这样虽然 jitter 很小，但会给所有样本都叠加一个近似固定的正偏置，表现为：
	- `1250 us + 1M PHY` 稳定打印在约 `1411 us`
	- `750 us + 2M PHY` 稳定打印在约 `927 us`
- 这更像“时间戳基准偏早”而不是链路真的慢了这么多。

### 实际修改

- 在 [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c) 中新增 `latency_request_reference_cycle`。
- `latency_request_prepare_handler()` 在给 central 侧 request slot 放行前，先记录一个“预测的 event 起点时刻”：`k_cycle_get_32() + LATENCY_PREPARE_DISTANCE_US`。
- central latency loop 在发送 payload 时，优先使用这个 event 对齐的参考时间，而不再直接使用 `bt_latency_request()` 调用时刻。
- 在连接建立、连接断开、profile 切换和每次等待新 slot 前，把该参考时间清零，避免误用旧 slot 的时间戳。

### 影响文件

- [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c)
- [modify.md](d:/workspace/26_work/shorter_conn_intervals_test/modify.md)

### 结果

- 验证：已使用现有 `build_1` 完成编译验证，通过。
- 提交：本次提交。
- 运行时回归时建议重点观察：
	- `1250 us + 1M PHY` 的 latency 是否从约 `1411 us` 回落到更接近真实链路事务时间的稳定值。
	- `750 us + 2M PHY` 的 latency 是否从约 `927 us` 回落，同时保持低 jitter。
	- 两个 profile 下的平均值是否不再整体抬高，而只保留少量正常调度波动。

## V25 修复 `Unhandled vendor-specific event 0x82`

### 用户要求

运行时日志持续打印：

- `Unhandled vendor-specific event 0x82 len 12: ...`

要求继续定位并修复该问题。

### 问题分析

- `0x82` 对应的是 controller 的 connection anchor point update report。
- 当前工程在应用启动时先通过 anchor-slot 逻辑注册了一次 vendor-specific event callback，随后又在 QoS enable 路径里再次调用 `bt_hci_register_vnd_evt_cb()` 注册 QoS 回调。
- 检查 Zephyr host 实现后确认：`bt_hci_register_vnd_evt_cb()` 只有一个全局回调槽，后注册的回调会覆盖先注册的回调。
- 结果就是：
	- QoS report 还能被处理；
	- anchor-point report `0x82` 已经没有对应处理函数，于是被 host 打印成 `Unhandled vendor-specific event`。

### 实际修改

- 在 [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c) 中移除对 `bt_radio_notification_conn_cb_register()` 的依赖。
- 改为在应用侧注册一个统一的 `vs_evt_handler()`，把 vendor-specific event 分发给：
	- QoS report 处理路径
	- anchor-point report 处理路径
- 为 anchor-point report 新增本地 timer 调度逻辑，沿用 controller 提供的 `anchor_point_us` 和当前 connection interval，在应用侧生成下一个 request slot。
- 保留现有 QoS backoff 逻辑不变，只修复 VS event 分发根因。

### 影响文件

- [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c)
- [modify.md](d:/workspace/26_work/shorter_conn_intervals_test/modify.md)

### 结果

- 验证：已使用现有 `build_1` 完成编译验证，通过。
- 提交：本次提交。
- 运行时回归时建议重点观察：
	- 连接建立后，`Unhandled vendor-specific event 0x82` 是否已经消失。
	- QoS 统计是否仍然持续更新。
	- anchor-slot 驱动下的 latency 日志是否继续稳定输出。

## V26 收敛 profile 运行时的大幅 jitter

### 用户要求

双板日志显示：

- `1250 us + 1M PHY` 下多数窗口稳定在 `883 us / jitter 17 us`，但会间歇出现 `1498~1521 us` 的离群最大值；
- `750 us + 2M PHY` 下也会周期性出现 `939 us` 或更高的离群值；
- QoS 一直是 `crc err 0, backoff 0 ms`，要求继续收敛 jitter。

### 问题分析

- 当前 latency 统计值本质上是 round-trip latency 的一半。
- 对照日志可见：
	- `1499 - 879 ~= 620 us`，乘以 2 后约等于一个 `1250 us` connection interval；
	- `939 - 578 ~= 361 us`，乘以 2 后约等于一个 `750 us` connection interval。
- 这说明当前异常并不是链路重传或 QoS backoff，而是 request 偶发错过了目标 connection event，滑到下一个 event 才上空口。
- 现有实现把 anchor-slot 的 prepare 提前量固定在 `300 us`。对当前 request/response 闭环来说，这个 slot 偏早，尤其在 `750 us + 2M PHY` 下，上一笔 response 返回后，主线程到下一笔 slot 的剩余时间过小，容易出现“还没重新入队，slot 已经过去”的整 interval 滑移。

### 实际修改

- 在 [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c) 中移除固定的 `LATENCY_PREPARE_DISTANCE_US=300`。
- 改为根据当前 `active_conn_interval_us` 动态计算 prepare 提前量：
	- 以 `interval / 8` 为基准；
	- 并限制在 `125 us` 到 `200 us` 之间。
- central 侧写入 latency 时间戳时，继续以“预测的 event 起点时刻”为基准，只是把 slot 放得更靠后，给 response 返回后的主线程重新挂起和下一笔 request 重新入队留出更多裕量。

### 影响文件

- [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c)
- [modify.md](d:/workspace/26_work/shorter_conn_intervals_test/modify.md)

### 结果

- 验证：已使用现有 `build_1` 完成编译验证，通过。
- 提交：本次提交。
- 运行时回归时建议重点观察：
	- `1250 us + 1M PHY` 下是否不再周期性出现 `1498~1521 us` 的离群值；
	- `750 us + 2M PHY` 下 `939 us` 一类的整 interval 滑移是否显著减少；
	- QoS `crc err` 和 `backoff` 是否继续保持为 0。

## V27 简化 latency 日志并让主从都能用 Button3 切换 profile

### 用户要求

继续修改：

- 主机和从机都只打印平均传输 latency 与 jitter 两项；
- 主机和从机都可以通过 `BUTTON3` 在 `750 us + 2M PHY` 与 `1250 us + 1M PHY` 之间切换，谁按下 `BUTTON3` 就以谁发起的切换为准。

### 问题分析

- 当前日志窗口除了 `avg/jitter` 之外还打印 `min/max/QoS/backoff`，不符合这轮简化输出的要求。
- 当前 `BUTTON3` 逻辑只允许 central 侧发起切换；peripheral 即便完成了服务发现，也会因为 `test_run()` 中的早退而完全不进入 runtime latency/profile 控制路径。
- 对照 Nordic 原始 sample，latency request 循环本来就不要求只在 initiator 一侧运行；真正需要限制到 initiator 一侧的只是启动阶段的初始 SCI/profile 建链过程。

### 实际修改

- 在 [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c) 中将 latency 统计日志改为只输出：
	- `Latency avg ... us`
	- `jitter ... us`
- 移除 `BUTTON3` 的 central-only 判断，让 central 与 peripheral 只要进入 runtime loop 后都可以提交 profile 切换请求。
- 调整 `test_run()` 控制流：
	- 初始的 `2M FSU` 与自动切到 `1250 us + 1M PHY` 仍只由 initiator 侧负责；
	- 但两侧都会进入 latency request / runtime profile loop，因此两侧都能打印 latency 统计，也都能消费本地 `BUTTON3` 请求。
- 新增本地 profile 同步逻辑，根据当前 connection interval 与 PHY 状态推断 `active_link_profile`，避免另一侧第一次按 `BUTTON3` 时因本地 profile 状态未知而切错方向。

### 影响文件

- [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c)
- [modify.md](d:/workspace/26_work/shorter_conn_intervals_test/modify.md)

### 结果

- 验证：已完成编译验证。
- 提交：本次提交。
- 运行时回归时建议重点观察：
	- central 和 peripheral 是否都只再打印 `avg/jitter` 两项；
	- 在任一侧按下 `BUTTON3` 后，两侧链路是否都切到对应 profile；
	- 两侧同时具备 runtime 切换能力后，latency 抖动是否仍保持在当前可接受范围内。

## V28 修复从机侧 latency slot 超时与 750 us 切换失败

### 用户要求

从机侧运行日志显示：

- 持续打印 `Timed out waiting for the next latency request slot`；
- 从机按下 `BUTTON3` 切到 `750 us + 2M PHY` 时，出现 `opcode 0x20a1 status 0x11 BT_HCI_ERR_UNSUPP_FEATURE_PARAM_VAL`。

要求继续修复这两个运行时错误。

### 问题分析

- V27 让 peripheral 也进入了 runtime latency/profile loop，但 anchor-slot 的实际放行函数 `latency_request_prepare_handler()` 里仍然保留了 `!is_central` 的早退条件。
- 结果就是：
	- peripheral 进入 latency loop 后会等待 `latency_request_slot_sem`；
	- 但本地 timer 即使触发，也不会真正给这个 semaphore 放行；
	- 因而从机侧会持续打印 `Timed out waiting for the next latency request slot`。
- 同时，peripheral 发起 `750 us + 2M PHY` 切换时沿用了 central 侧的“严格 `750 us` 单点 request”路径。
- 对 peripheral 侧的 SCI 请求，这种严格单点更容易被 central/controller 拒绝；之前官方 sample 的注释也提示，peripheral 更适合请求一个有界区间，让 central 在区间内选取它能接受的最短 interval。

### 实际修改

- 在 [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c) 中移除了 `latency_request_prepare_handler()` 里的 central-only 早退，让 peripheral 侧在进入 runtime loop 后也能真正收到 anchor-slot 放行。
- 保留现有 anchor-point report 和本地 timer 机制不变，只修正错误的角色限制。
- 将 `switch_to_750us_2m_profile()` 中 peripheral 发起的 connection rate request 改为有界区间请求：
	- 最小值仍为目标 `750 us`；
	- 最大值放宽到当前 active interval；
	- 让 central/controller 在该区间内选择它能接受的最短值，避免严格单点 `750 us` 直接触发 `0x20a1 status 0x11`。

### 影响文件

- [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c)
- [modify.md](d:/workspace/26_work/shorter_conn_intervals_test/modify.md)

### 结果

- 验证：已完成编译验证。
- 提交：本次提交。
- 运行时回归时建议重点观察：
	- peripheral 侧是否不再持续打印 `Timed out waiting for the next latency request slot`；
	- peripheral 按下 `BUTTON3` 切到 `750 us + 2M PHY` 时，是否不再报 `opcode 0x20a1 status 0x11`；
	- 两侧在 `750 us + 2M PHY` 和 `1250 us + 1M PHY` 间切换后，latency 日志是否继续只输出 `avg/jitter`。

## V29 恢复为单边输出传输 latency，消除主从显示不同步

### 用户要求

主/从机之间的 latency 显示不同步，要求改回到之前“只输出传输 latency”的方式。

### 问题分析

- 当前主从两侧都会进入 latency 主循环，并各自发起 `bt_latency_request()`。
- 因此两边实际上测量的是不同的 latency 事务，日志天然不会同步。
- 用户这里要恢复的是更早那种“只输出 `Transmission Latency`”的工作方式，本质上也就是恢复为单边发起测量、单边打印。
- 在当前工程里，真正需要保留在两侧的只是 `BUTTON3` profile 切换能力；latency request 本身不需要两边同时发起。

### 实际修改

- 在 [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c) 中将 latency 输出恢复为：
	- `Transmission Latency: %u us`
- 恢复 initiator-only 的 latency slot 放行条件，避免非 initiator 侧继续参与 latency request 调度。
- 在 runtime loop 中让非 initiator 侧只做两件事：
	- 处理本地 `BUTTON3` 发起的 profile 切换
	- 保持连接存活等待
- initiator 侧继续负责 latency request 的发送和 `Transmission Latency` 日志输出。

### 影响文件

- [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c)
- [modify.md](d:/workspace/26_work/shorter_conn_intervals_test/modify.md)

### 结果

- 验证：已完成编译验证。
- 提交：本次提交。
- 运行时回归时建议重点观察：
	- 是否只剩 initiator 一侧打印 `Transmission Latency: ... us`；
	- 另一侧是否不再输出不同步的 latency 数值；
	- 即使只保留单边 latency 输出，主从两侧的 `BUTTON3` profile 切换能力是否仍保持可用。

## V30 去掉从机侧 Button3 的 PHY 切换能力

### 用户要求

去掉从机端的 `BUTTON3` 切换 `1M / 2M PHY` 功能，只保留主机端切换 PHY。

### 问题分析

- 在 V29 之后，latency 已经恢复为 initiator-only 输出，但 `BUTTON3` 的入口仍对主从两侧开放。
- 这意味着 peripheral 侧虽然不再发送 latency request，仍然可能通过本地 `BUTTON3` 发起 profile 切换。
- 本轮要求很明确：只保留主机端切换 PHY，因此最小且安全的修复就是把 `BUTTON3` 的入口重新收紧到 initiator 侧。

### 实际修改

- 在 [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c) 中为 `request_link_profile_toggle()` 重新增加 initiator-only 条件。
- 现在只有 `initiate_conn_rate_update == true` 的一侧，也就是 central / 主机侧，才能通过 `BUTTON3` 发起 `750 us + 2M PHY` 和 `1250 us + 1M PHY` 之间的切换。
- peripheral / 从机侧按下 `BUTTON3` 将不再触发 PHY/profile 切换。

### 影响文件

- [src/main.c](d:/workspace/26_work/shorter_conn_intervals_test/src/main.c)
- [modify.md](d:/workspace/26_work/shorter_conn_intervals_test/modify.md)

### 结果

- 验证：已完成编译验证。
- 提交：本次提交。
- 运行时回归时建议重点观察：
	- central 侧 `BUTTON3` 是否仍可正常在两个 profile 间切换；
	- peripheral 侧按下 `BUTTON3` 后是否不再打印切换日志；
	- 连接和单边 `Transmission Latency` 输出路径是否保持不变。

## 当前 git 状态

- 当前分支：`master`
- 当前远端：`origin`
- V11 修复完成后，应以最新 `git log` 结果为准查看提交状态。

## 后续建议

如果后续你还继续修改，建议每次按下面格式追加在本文档末尾：

1. 用户要求
2. 实际修改
3. 影响范围
4. 是否已编译验证
5. 是否已提交到 git
