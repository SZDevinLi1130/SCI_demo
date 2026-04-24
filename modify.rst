Commit Compare: 8576082 -> 7421737
##################################

.. contents::
   :local:
   :depth: 2

本文件整理了本次提交相对上一个提交的关键改动，并将关键代码片段按“改前代码 / 改后代码 / 说明”展示。

本次提交的主线目标是：

* 将链路目标从 ``750 us / 2M PHY`` 调整为更稳定的 ``1000 us / 1M PHY``
* 将时延测试从固定 ``sleep`` 改为事件驱动等待
* 在应用侧和 radio 子镜像侧同时收紧控制器事件长度配置

1. 目标连接间隔与时延超时参数
*********************************

改前代码
========

.. code-block:: c

   #define INTERVAL_INITIAL          0x8    /* 8 units, 10 ms */
   #define INTERVAL_INITIAL_US       10000 /* 10 ms */
   #define INTERVAL_TARGET_US        750

改后代码
========

.. code-block:: c

   #define INTERVAL_INITIAL          0x8    /* 8 units, 10 ms */
   #define INTERVAL_INITIAL_US       10000 /* 10 ms */
   #define INTERVAL_TARGET_US        1000
   #define LATENCY_RESPONSE_TIMEOUT_MS 50
   #define LATENCY_REPORT_INTERVAL 64

说明
====

目标连接间隔从 750 us 调整为 1000 us，并新增时延响应超时和统计周期常量。

2. 时延测量状态与统计辅助函数
********************************

2.1 新增信号量与统计变量
=========================

改前代码
========

.. code-block:: c

   static K_SEM_DEFINE(phy_updated, 0, 1);
   static K_SEM_DEFINE(min_interval_read_sem, 0, 1);
   static K_SEM_DEFINE(frame_space_updated_sem, 0, 1);
   static K_SEM_DEFINE(discovery_complete_sem, 0, 1);
   static K_SEM_DEFINE(role_selected_sem, 0, 1);

   static bool test_ready;
   static bool initiate_conn_rate_update = true;
   static bool conn_rate_update_pending = true;
   static bool is_central;

   static uint32_t latency_response;

改后代码
========

.. code-block:: c

   static K_SEM_DEFINE(phy_updated, 0, 1);
   static K_SEM_DEFINE(min_interval_read_sem, 0, 1);
   static K_SEM_DEFINE(frame_space_updated_sem, 0, 1);
   static K_SEM_DEFINE(discovery_complete_sem, 0, 1);
   static K_SEM_DEFINE(role_selected_sem, 0, 1);
   static K_SEM_DEFINE(conn_rate_changed_sem, 0, 1);
   static K_SEM_DEFINE(latency_response_sem, 0, 1);

   static bool test_ready;
   static bool initiate_conn_rate_update = true;
   static bool conn_rate_update_pending = true;
   static bool is_central;

   static uint32_t latency_response;
   static uint32_t latency_sum_us;
   static uint32_t latency_min_us;
   static uint32_t latency_max_us;
   static uint32_t latency_sample_count;

说明
====

新增连接速率变化等待信号量、时延响应等待信号量，以及 avg/min/max/jitter 统计所需状态变量。

2.2 新增时延统计函数
=====================

改前代码
========

旧版本无此统计辅助函数。

改后代码
========

.. code-block:: c

   static void latency_stats_reset(void)
   {
           latency_sum_us = 0;
           latency_min_us = 0;
           latency_max_us = 0;
           latency_sample_count = 0;
   }

   static void latency_stats_add(uint32_t latency_us)
   {
           if (latency_sample_count == 0U) {
                   latency_min_us = latency_us;
                   latency_max_us = latency_us;
           } else {
                   latency_min_us = MIN(latency_min_us, latency_us);
                   latency_max_us = MAX(latency_max_us, latency_us);
           }

           latency_sum_us += latency_us;
           latency_sample_count++;

           if (latency_sample_count >= LATENCY_REPORT_INTERVAL) {
                   LOG_INF("Latency avg %u us, min %u us, max %u us, jitter %u us",
                           latency_sum_us / latency_sample_count, latency_min_us,
                           latency_max_us, latency_max_us - latency_min_us);
                   latency_stats_reset();
           }
   }

说明
====

旧版本逐次打印时延；新版本改为累计统计并周期性输出，减少日志本身对时延测量的干扰。

3. 连接建立与断开时的状态重置
********************************

改前代码
========

.. code-block:: c

   remote_min_interval_us = 0;
   common_min_interval_us = 0;
   remote_min_interval_handle = 0;
   requested_interval_us = MAX((uint32_t)local_min_interval_us, (uint32_t)INTERVAL_TARGET_US);

   default_conn = bt_conn_ref(conn);
   ...

   test_ready = false;
   conn_rate_update_pending = true;

   if (default_conn) {
           bt_conn_unref(default_conn);
           default_conn = NULL;
   }

改后代码
========

.. code-block:: c

   remote_min_interval_us = 0;
   common_min_interval_us = 0;
   remote_min_interval_handle = 0;
   requested_interval_us = MAX((uint32_t)local_min_interval_us, (uint32_t)INTERVAL_TARGET_US);
   latency_response = 0;
   latency_stats_reset();
   k_sem_reset(&conn_rate_changed_sem);
   k_sem_reset(&latency_response_sem);

   default_conn = bt_conn_ref(conn);
   ...

   test_ready = false;
   conn_rate_update_pending = true;
   latency_response = 0;
   latency_stats_reset();
   k_sem_reset(&conn_rate_changed_sem);
   k_sem_reset(&latency_response_sem);

   if (default_conn) {
           bt_conn_unref(default_conn);
           default_conn = NULL;
   }

说明
====

连接建立和断开时新增对时延状态和等待信号量的清理，避免重连后沿用旧状态。

4. SCI 默认参数、PHY 与 Frame Space 策略
*****************************************

4.1 默认 SCI 参数与 Frame Space 目标 PHY
========================================

改前代码
========

.. code-block:: c

   const struct bt_conn_le_conn_rate_param params = {
           .interval_min_125us = interval_min_us / 125,
           .interval_max_125us = interval_max_us / 125,
           .subrate_min = 1,
           .subrate_max = 1,
           .max_latency = 5,
           .continuation_number = 0,
           .supervision_timeout_10ms = 400,
           .min_ce_len_125us = BT_HCI_LE_SCI_CE_LEN_MIN_125US,
           .max_ce_len_125us = BT_HCI_LE_SCI_CE_LEN_MAX_125US,
   };

   const struct bt_conn_le_frame_space_update_param params = {
           .phys = BT_HCI_LE_FRAME_SPACE_UPDATE_PHY_2M_MASK,
           .spacing_types = BT_CONN_LE_FRAME_SPACE_TYPES_MASK_ACL_IFS,
           .frame_space_min = 0,
           .frame_space_max = 150,
   };

改后代码
========

.. code-block:: c

   const struct bt_conn_le_conn_rate_param params = {
           .interval_min_125us = interval_min_us / 125,
           .interval_max_125us = interval_max_us / 125,
           .subrate_min = 1,
           .subrate_max = 1,
           .max_latency = 0,
           .continuation_number = 0,
           .supervision_timeout_10ms = 400,
           .min_ce_len_125us = BT_HCI_LE_SCI_CE_LEN_MIN_125US,
           .max_ce_len_125us = BT_HCI_LE_SCI_CE_LEN_MAX_125US,
   };

   const struct bt_conn_le_frame_space_update_param params = {
           .phys = BT_HCI_LE_FRAME_SPACE_UPDATE_PHY_1M_MASK,
           .spacing_types = BT_CONN_LE_FRAME_SPACE_TYPES_MASK_ACL_IFS,
           .frame_space_min = 0,
           .frame_space_max = 150,
   };

说明
====

默认 ``max_latency`` 从 5 降到 0，同时 Frame Space Update 的目标 PHY 从 2M 改为 1M。

4.2 PHY 更新函数与回调同步
============================

改前代码
========

.. code-block:: c

   static void conn_rate_changed(struct bt_conn *conn, uint8_t status,
                                 const struct bt_conn_le_conn_rate_changed *params)
   {
           if (status == BT_HCI_ERR_SUCCESS) {
                   ...
           } else {
                   ...
           }
   }

   static int update_to_2m_phy(void)
   {
           struct bt_conn_le_phy_param phy;

           phy.options = BT_CONN_LE_PHY_OPT_NONE;
           phy.pref_rx_phy = BT_GAP_LE_PHY_2M;
           phy.pref_tx_phy = BT_GAP_LE_PHY_2M;
           ...
   }

   static void latency_response_handler(const void *buf, uint16_t len)
   {
           ...
           if (len == sizeof(latency_time)) {
                   latency_response = (uint32_t)k_cyc_to_ns_floor64(cycles_spent) / 2000;
           }
   }

改后代码
========

.. code-block:: c

   static void conn_rate_changed(struct bt_conn *conn, uint8_t status,
                                 const struct bt_conn_le_conn_rate_changed *params)
   {
           if (status == BT_HCI_ERR_SUCCESS) {
                   ...
           } else {
                   ...
           }

           k_sem_give(&conn_rate_changed_sem);
   }

   static int update_to_1m_phy(void)
   {
           struct bt_conn_le_phy_param phy;

           phy.options = BT_CONN_LE_PHY_OPT_NONE;
           phy.pref_rx_phy = BT_GAP_LE_PHY_1M;
           phy.pref_tx_phy = BT_GAP_LE_PHY_1M;
           ...
   }

   static void latency_response_handler(const void *buf, uint16_t len)
   {
           ...
           if (len == sizeof(latency_time)) {
                   latency_response = (uint32_t)k_cyc_to_ns_floor64(cycles_spent) / 2000;
           }

           k_sem_give(&latency_response_sem);
   }

说明
====

连接参数变化回调和时延响应回调都变成了可同步事件；PHY 更新函数由 2M 改为 1M。

5. test_run() 从固定等待改为事件驱动
***************************************

改前代码
========

.. code-block:: c

   test_ready = false;

   /* Update link parameters to satisfy minimum supported connection interval requirements. */
   if (initiate_conn_rate_update) {
           /* The lowest connection intervals can only be achieved on the 2M PHY. */
           err = update_to_2m_phy();
           ...

           /* A smaller frame space is required. */
           err = select_lowest_frame_space();
           ...
   }

   if (initiate_conn_rate_update && conn_rate_update_pending) {
           LOG_INF("Requesting new connection interval: %u us", requested_interval_us);

           err = conn_rate_request(requested_interval_us, requested_interval_us);
           if (err) {
                   ...
           }

           conn_rate_update_pending = false;
   }

   while (default_conn) {
           uint32_t time = k_cycle_get_32();

           gpio_pin_set_dt(&led1, 1);
           err = bt_latency_request(&latency_client, &time, sizeof(time));
           if (err && err != -EALREADY) {
                   ...
                   k_sleep(K_MSEC(200));
                   continue;
           }

           k_sleep(K_MSEC(200)); /* wait for latency response */

           if (latency_response) {
                   LOG_INF("Transmission Latency: %u us", latency_response);
           } else {
                   ...
           }
   }

改后代码
========

.. code-block:: c

   test_ready = false;
   latency_stats_reset();

   if (!initiate_conn_rate_update) {
           return;
   }

   /* Update link parameters to satisfy minimum supported connection interval requirements. */
   if (initiate_conn_rate_update) {
           err = update_to_1m_phy();
           ...

           /* Negotiate the smallest supported frame space for the selected PHY. */
           err = select_lowest_frame_space();
           ...
   }

   if (initiate_conn_rate_update && conn_rate_update_pending) {
           LOG_INF("Requesting new connection interval: %u us", requested_interval_us);
           k_sem_reset(&conn_rate_changed_sem);

           err = conn_rate_request(requested_interval_us, requested_interval_us);
           if (err) {
                   ...
           }

           err = k_sem_take(&conn_rate_changed_sem, K_MSEC(LATENCY_RESPONSE_TIMEOUT_MS));
           if (err) {
                   LOG_WRN("Timed out waiting for connection rate change");
                   return;
           }

           conn_rate_update_pending = false;
   }

   while (default_conn) {
           uint32_t time = k_cycle_get_32();

           k_sem_reset(&latency_response_sem);
           latency_response = 0;
           gpio_pin_set_dt(&led1, 1);
           err = bt_latency_request(&latency_client, &time, sizeof(time));
           if (err && err != -EALREADY) {
                   ...
                   continue;
           }

           err = k_sem_take(&latency_response_sem, K_MSEC(LATENCY_RESPONSE_TIMEOUT_MS));
           if (err) {
                   gpio_pin_set_dt(&led1, 0);
                   LOG_WRN("Did not receive a latency response");
                   continue;
           }

           if (latency_response) {
                   latency_stats_add(latency_response);
           } else {
                   ...
           }

           latency_response = 0;
   }

说明
====

旧版本依赖固定 ``sleep``；新版本改为等待连接速率变化事件和 latency 响应事件，明显减少了线程调度与日志对测量结果的污染。

6. main() 中的 central / peripheral 职责划分
*********************************************

改前代码
========

.. code-block:: c

   if (selected_role == ROLE_SELECTION_CENTRAL) {
           is_central = true;
           LOG_INF("Central. Starting scanning");
           scan_init();
           scan_start();
   } else {
           is_central = false;
           LOG_INF("Peripheral. Starting advertising");
           adv_start();
   }

改后代码
========

.. code-block:: c

   if (selected_role == ROLE_SELECTION_CENTRAL) {
           is_central = true;
           initiate_conn_rate_update = true;
           LOG_INF("Central. Starting scanning");
           scan_init();
           scan_start();
   } else {
           is_central = false;
           initiate_conn_rate_update = false;
           LOG_INF("Peripheral. Starting advertising");
           adv_start();
   }

说明
====

明确由 central 侧主动发起连接参数更新，peripheral 侧仅配合响应，减少双方同时修改参数带来的不确定性。

7. prj.conf 中的控制器参数变化
********************************

改前代码
========

.. code-block:: ini

   CONFIG_BT_FRAME_SPACE_UPDATE=y
   CONFIG_BT_SHORTER_CONNECTION_INTERVALS=y

   # Ensure the default event length is shorter than the minimum interval
   CONFIG_BT_CTLR_SDC_MAX_CONN_EVENT_LEN_DEFAULT=750

   CONFIG_BT_SCAN=y
   ...
   CONFIG_BT_HCI_ERR_TO_STR=y

   CONFIG_GPIO=y

改后代码
========

.. code-block:: ini

   CONFIG_BT_FRAME_SPACE_UPDATE=y
   CONFIG_BT_SHORTER_CONNECTION_INTERVALS=y

   # Keep the reserved ACL event length aligned with the 1 ms target interval.
   CONFIG_BT_CTLR_SDC_MAX_CONN_EVENT_LEN_DEFAULT=1000
   CONFIG_BT_CTLR_SDC_CONN_EVENT_EXTEND_DEFAULT=n

   CONFIG_BT_SCAN=y
   ...
   CONFIG_BT_HCI_ERR_TO_STR=y

   CONFIG_GPIO=y

   CONFIG_BT_CTLR_TX_PWR_PLUS_8=y
   CONFIG_BT_CTLR_TX_PWR_DYNAMIC_CONTROL=y

说明
====

应用侧控制器配置从 750 us 调整到 1000 us，并关闭连接事件扩展；同时新增发射功率相关配置。

8. sysbuild/ipc_radio/prj.conf 中的 radio 子镜像参数变化
**********************************************************

改前代码
========

.. code-block:: ini

   CONFIG_BT_FRAME_SPACE_UPDATE=y
   CONFIG_BT_SHORTER_CONNECTION_INTERVALS=y

   # Ensure the default event length is shorter than the minimum interval
   CONFIG_BT_CTLR_SDC_MAX_CONN_EVENT_LEN_DEFAULT=750

改后代码
========

.. code-block:: ini

   CONFIG_BT_FRAME_SPACE_UPDATE=y
   CONFIG_BT_SHORTER_CONNECTION_INTERVALS=y

   # Keep the reserved ACL event length aligned with the 1 ms target interval.
   CONFIG_BT_CTLR_SDC_MAX_CONN_EVENT_LEN_DEFAULT=1000
   CONFIG_BT_CTLR_SDC_CONN_EVENT_EXTEND_DEFAULT=n

说明
====

radio 子镜像侧与应用侧保持一致，确保 sysbuild 下 controller 行为同步收敛到 1 ms 目标。

总结
****

这次提交相对上一个提交的本质变化，不是单纯“改成 1M PHY”，而是将整个链路和测量路径一起收敛为：

* ``1 Mbps PHY``
* ``1 ms`` 目标连接间隔
* 更严格的 ``max_latency = 0``
* 连接事件长度固定到 ``1000 us``
* 禁用连接事件自动扩展
* latency 测量改为事件驱动等待
* latency 输出改为统计汇总而不是逐次打印

从工程效果上看，这些变化的目标是让测得的 transmission latency 更稳定、抖动更小，并减少应用层观测逻辑对结果本身的干扰。

9. 为什么这些改动会降低 jitter
**********************************

这一节不再从代码 diff 角度看，而是从链路调度和测量方法角度看这些改动为什么有效。

9.1 从 750 us 调整到 1000 us
=============================

* ``750 us`` 更接近该样例的激进极限配置，对 PHY、Frame Space、控制器调度和包时序的要求更高。
* ``1000 us`` 给控制器调度、空口切换和 GATT 往返留出了更大的稳定裕量。
* 这通常会让最小可达时延略微上升，但会换来更窄的抖动分布。

9.2 从 2M PHY 切换到 1M PHY 后同步收紧策略
===========================================

* 旧版本里更偏向 ``2M PHY + 更短 interval`` 的思路。
* 这次提交明确改成 ``1M PHY``，同时不再继续保留 ``750 us`` 的目标，而是把 interval 调整到 ``1 ms``。
* 这样做的意义是避免 PHY 已经变慢，但调度窗口仍然按更激进目标去压缩，导致空口交换时序更容易波动。

9.3 ``max_latency = 0`` 的作用
==============================

* ``max_latency = 0`` 表示从设备不允许通过 slave latency 跳过连接事件。
* 这样每个连接事件都更规律，主从两边的交互节奏更固定。
* 在低时延测试里，这比允许一定 latency 更容易获得稳定的一致性结果。

9.4 固定连接事件长度并关闭 event extend
==========================================

* ``CONFIG_BT_CTLR_SDC_MAX_CONN_EVENT_LEN_DEFAULT=1000`` 让控制器预留的 ACL 连接事件长度与 ``1 ms`` 目标对齐。
* ``CONFIG_BT_CTLR_SDC_CONN_EVENT_EXTEND_DEFAULT=n`` 则避免控制器在运行中自动扩展连接事件窗口。
* 这两个配置组合起来，等价于尽量把每个连接事件的时间预算固定住，减少事件长度漂移造成的时延抖动。

9.5 从固定 ``sleep`` 改成事件驱动等待
=======================================

* 旧版本中 latency 请求发出后，主要靠固定 ``k_sleep()`` 等待响应。
* 固定 sleep 会把线程调度、tick 粒度、日志输出时机等软件层噪声混入测量值。
* 新版本通过 ``conn_rate_changed_sem`` 和 ``latency_response_sem`` 等待真实事件，测量更接近链路本身，而不是应用线程运行时机。

9.6 从逐次打印改成统计输出
============================

* 逐次打印 latency 会带来更频繁的串口输出和日志处理。
* 在高频请求场景下，日志本身就会影响调度节奏，进而反过来影响时延测试。
* 改成按批次统计 ``avg/min/max/jitter``，可以明显减少观测逻辑对被观测对象的干扰。

9.7 只让 central 主动发起参数更新
===================================

* 如果 central 和 peripheral 都可能主动触发 connection rate update，就会增加参数协商时序的不确定性。
* 本次修改后，central 明确为主动方，peripheral 只负责配合。
* 这能减少双方并发改参数导致的协商抖动，让链路更快进入稳定态。

9.8 结论
========

把这些改动合在一起看，本次提交的核心不是追求单次测量里的最小数值，而是通过减少不确定性来缩小时延分布宽度。也就是说，这次优化更偏向：

* 更稳定的连接事件节奏
* 更确定的控制器时间预算
* 更少的软件层测量噪声
* 更明确的主从职责分工

因此，最终看到的效果通常不是“平均值大幅下降”，而是“最大值被压低、最小值更接近平均值、整体 jitter 变小”。