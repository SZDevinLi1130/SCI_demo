# shorter_conn_intervals_test 修改版本记录

本文档用于记录本工程在本轮协作中的每一版修改内容，并同时记录对应的用户要求。

说明：

- 本文档偏重版本演进记录。
- [modify.rst](d:/workspace/26_work/shorter_conn_intervals_test/modify.rst) 偏重某次提交的代码前后对比说明。
- 本文档最后一版包含当前尚未提交到 git 的修复内容。

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