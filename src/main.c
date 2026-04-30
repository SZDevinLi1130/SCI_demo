/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/console/console.h>
#include <string.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/drivers/timer/nrf_grtc_timer.h>
#include <bluetooth/hci_vs_sdc.h>
#include <bluetooth/services/latency.h>
#include <bluetooth/services/latency_client.h>
#include <bluetooth/scan.h>
#include <bluetooth/gatt_dm.h>
#include <sdc_hci_cmd_controller_baseband.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/random/random.h>

LOG_MODULE_REGISTER(app_main, LOG_LEVEL_INF);

#define DEVICE_NAME	CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define INTERVAL_INITIAL	  0x8	 /* 8 units, 10 ms */
#define INTERVAL_INITIAL_US	  10000 /* 10 ms */
#define INTERVAL_TARGET_US	  1000
#define INTERVAL_2M_PHY_MODE_US 750
#define INTERVAL_1M_PHY_MIN_US 1250
#define LATENCY_RESPONSE_TIMEOUT_MS 50
#define CONN_RATE_CHANGE_TIMEOUT_MS 1000
#define LATENCY_REPORT_INTERVAL 200//64
#define ACL_AUTO_FLUSH_TIMEOUT_625US 4U
#define QOS_BACKOFF_MAX_MS 4U
#define LATENCY_PREPARE_DISTANCE_MIN_US 125U
#define LATENCY_PREPARE_DISTANCE_MAX_US 200U
#define LATENCY_PREPARE_DISTANCE_DIVISOR 8U
#define STABILITY_BACKOFF_THRESHOLD_US 500
#define STABILITY_BACKOFF_INTERVAL_MS 2
#define RECONNECT_DELAY_MIN_MS 100
#define RECONNECT_DELAY_MAX_MS 300

static struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static struct gpio_dt_spec button0 = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_dt_spec button1 = GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios);
static struct gpio_dt_spec button3 = GPIO_DT_SPEC_GET(DT_ALIAS(sw3), gpios);

enum role_selection {
	ROLE_SELECTION_NONE,
	ROLE_SELECTION_PERIPHERAL,
	ROLE_SELECTION_CENTRAL,
};

enum link_profile {
	LINK_PROFILE_UNKNOWN,
	LINK_PROFILE_750US_2M,
	LINK_PROFILE_1250US_1M,
};

#define ROLE_INPUT_THREAD_STACK_SIZE 1024
#define ROLE_INPUT_THREAD_PRIORITY 7

static struct gpio_callback button0_cb_data;
static struct gpio_callback button1_cb_data;
static struct gpio_callback button3_cb_data;
static volatile enum role_selection selected_role = ROLE_SELECTION_NONE;
K_THREAD_STACK_DEFINE(role_input_thread_stack, ROLE_INPUT_THREAD_STACK_SIZE);
static struct k_thread role_input_thread_data;

static void set_connection_leds(bool connected)
{
	gpio_pin_set_dt(&led0, connected ? 1 : 0);
	gpio_pin_set_dt(&led1, 0);
}

// static uint32_t test_intervals[] = {
// 	0,    /* Will be replaced with minimum supported interval */
// 	1000, /* 1 ms */
// 	1250, /* 1.25 ms */
// 	2000, /* 2 ms */
// 	4000  /* 4 ms */
// };

static uint32_t requested_interval_us = INTERVAL_TARGET_US;

/** @brief UUID of the SCI Min Interval Service. **/
#define BT_UUID_VS_SCI_MIN_INTERVAL_SERVICE_VAL                                                    \
	BT_UUID_128_ENCODE(0x1c840001, 0x49ac, 0x4905, 0x9702, 0x6e836da4cadd)

#define BT_UUID_VS_SCI_MIN_INTERVAL_SERVICE                                                        \
	BT_UUID_DECLARE_128(BT_UUID_VS_SCI_MIN_INTERVAL_SERVICE_VAL)

/** @brief UUID of the SCI Min Interval Characteristic. **/
#define BT_UUID_VS_SCI_MIN_INTERVAL_CHAR_VAL                                                       \
	BT_UUID_128_ENCODE(0x1c840002, 0x49ac, 0x4905, 0x9702, 0x6e836da4cadd)

#define BT_UUID_VS_SCI_MIN_INTERVAL_CHAR BT_UUID_DECLARE_128(BT_UUID_VS_SCI_MIN_INTERVAL_CHAR_VAL)

static K_SEM_DEFINE(phy_updated, 0, 1);
static K_SEM_DEFINE(min_interval_read_sem, 0, 1);
static K_SEM_DEFINE(frame_space_updated_sem, 0, 1);
static K_SEM_DEFINE(discovery_complete_sem, 0, 1);
static K_SEM_DEFINE(role_selected_sem, 0, 1);
static K_SEM_DEFINE(conn_rate_changed_sem, 0, 1);
static K_SEM_DEFINE(latency_response_sem, 0, 1);
static K_SEM_DEFINE(latency_request_slot_sem, 0, 1);

static bool test_ready;
static bool initiate_conn_rate_update = true;
static bool conn_rate_update_pending = true;
static bool is_central;

static uint32_t latency_response;
static uint32_t latency_sum_us;
static uint32_t latency_min_us;
static uint32_t latency_max_us;
static uint32_t latency_sample_count;
static atomic_t qos_crc_ok_count;
static atomic_t qos_crc_error_count;
static atomic_t qos_tx_backoff_ms;
static atomic_t requested_link_profile;
static atomic_t link_profile_switch_pending;
static atomic_t link_profile_switch_enabled;
static atomic_t latency_request_reference_cycle;

static uint16_t local_min_interval_us;
static uint16_t remote_min_interval_us;
static uint16_t common_min_interval_us;
static uint16_t remote_min_interval_handle;
static uint32_t active_conn_interval_us = INTERVAL_INITIAL_US;
static uint8_t conn_rate_change_status = BT_HCI_ERR_UNSPECIFIED;
static struct bt_conn_le_min_conn_interval_info local_min_interval_info;
static bool local_min_interval_groups_valid;
static enum link_profile active_link_profile = LINK_PROFILE_UNKNOWN;
static uint8_t active_tx_phy;
static uint8_t active_rx_phy;

static struct bt_conn *default_conn;
static struct bt_latency latency;
static struct bt_latency_client latency_client;
static struct bt_le_conn_param *conn_param =
	BT_LE_CONN_PARAM(INTERVAL_INITIAL, INTERVAL_INITIAL, 0, 400);
static struct bt_conn_info conn_info = {0};

static void latency_led_off_work_handler(struct k_work *work);
static void latency_request_slot_timer_handler(struct k_timer *timer);
static K_WORK_DELAYABLE_DEFINE(latency_led_off_work, latency_led_off_work_handler);
static K_TIMER_DEFINE(latency_request_slot_timer, latency_request_slot_timer_handler, NULL);

static uint32_t latency_prepare_distance_us_get(uint32_t conn_interval_us)
{
	uint32_t prepare_distance_us;

	if (conn_interval_us <= 1U) {
		return 0U;
	}

	prepare_distance_us = conn_interval_us / LATENCY_PREPARE_DISTANCE_DIVISOR;
	prepare_distance_us = CLAMP(prepare_distance_us, LATENCY_PREPARE_DISTANCE_MIN_US,
		LATENCY_PREPARE_DISTANCE_MAX_US);

	return MIN(prepare_distance_us, conn_interval_us - 1U);
}

static void latency_request_prepare_handler(struct bt_conn *conn)
{
	uint32_t prepare_distance_us;

	if (!initiate_conn_rate_update || !default_conn ||
	    (conn != default_conn) ||
	    (atomic_get(&link_profile_switch_enabled) == 0)) {
		return;
	}

	prepare_distance_us = latency_prepare_distance_us_get(active_conn_interval_us);
	if (prepare_distance_us == 0U) {
		return;
	}

	atomic_set(&latency_request_reference_cycle,
		(atomic_val_t)(k_cycle_get_32() + k_us_to_cyc_ceil32(prepare_distance_us)));
	k_sem_give(&latency_request_slot_sem);
}

static void latency_request_slot_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	latency_request_prepare_handler(default_conn);
}

static void latency_request_received_handler(const void *buf, uint16_t len)
{
	ARG_UNUSED(buf);
	ARG_UNUSED(len);

	if (is_central || !default_conn) {
		return;
	}

	gpio_pin_set_dt(&led1, 1);
	k_work_reschedule(&latency_led_off_work, K_USEC(active_conn_interval_us));
}

static const struct bt_latency_cb latency_service_cb = {
	.latency_request = latency_request_received_handler,
};

static void latency_led_off_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	gpio_pin_set_dt(&led1, 0);
}

static bool anchor_point_vs_evt_handler(struct net_buf_simple *buf)
{
	sdc_hci_subevent_vs_conn_anchor_point_update_report_t *evt;
	struct bt_conn *conn;
	uint32_t conn_interval_us;
	uint32_t prepare_distance_us;
	uint64_t timer_trigger_us;

	(void)net_buf_simple_pull_u8(buf);
	evt = (void *)buf->data;

	conn = bt_hci_conn_lookup_handle(evt->conn_handle);
	if (!conn) {
		return true;
	}

	if (conn != default_conn) {
		bt_conn_unref(conn);
		return true;
	}

	conn_interval_us = active_conn_interval_us;
	bt_conn_unref(conn);

	prepare_distance_us = latency_prepare_distance_us_get(conn_interval_us);
	if (prepare_distance_us == 0U) {
		return true;
	}

	timer_trigger_us = evt->anchor_point_us + conn_interval_us - prepare_distance_us;
	timer_trigger_us -= z_nrf_grtc_timer_startup_value_get();

	k_timer_start(&latency_request_slot_timer, K_TIMEOUT_ABS_US(timer_trigger_us),
		K_USEC(conn_interval_us));

	return true;
}

static void qos_stats_reset(void)
{
	atomic_set(&qos_crc_ok_count, 0);
	atomic_set(&qos_crc_error_count, 0);
}

static void latency_stats_reset(void)
{
	latency_sum_us = 0;
	latency_min_us = 0;
	latency_max_us = 0;
	latency_sample_count = 0;
	qos_stats_reset();
}

static void latency_stats_add(uint32_t latency_us)
{
	LOG_INF("Transmission Latency: %u us", latency_us);

	latency_sum_us += latency_us;
	if (latency_min_us == 0U || latency_us < latency_min_us) {
		latency_min_us = latency_us;
	}
	if (latency_max_us == 0U || latency_us > latency_max_us) {
		latency_max_us = latency_us;
	}
	latency_sample_count++;

	if (latency_sample_count >= LATENCY_REPORT_INTERVAL) {
		uint32_t avg_us = latency_sum_us / latency_sample_count;
		uint32_t jitter_us = latency_max_us - latency_min_us;
		uint32_t crc_ok = (uint32_t)atomic_get(&qos_crc_ok_count);
		uint32_t crc_err = (uint32_t)atomic_get(&qos_crc_error_count);
		uint32_t backoff_ms = (uint32_t)atomic_get(&qos_tx_backoff_ms);

		LOG_INF("Window[%u]: avg %u us, jitter %u us, QoS(crc ok %u, err %u, backoff %u ms)",
			(uint32_t)LATENCY_REPORT_INTERVAL, avg_us, jitter_us,
			crc_ok, crc_err, backoff_ms);

		/* Proactive stability backoff: if jitter exceeds threshold and no CRC backoff active,
		 * apply a small spacing to let the connection event schedule settle.
		 */
		if (jitter_us > STABILITY_BACKOFF_THRESHOLD_US && backoff_ms == 0U) {
			atomic_set(&qos_tx_backoff_ms, (atomic_val_t)STABILITY_BACKOFF_INTERVAL_MS);
		}

		latency_sum_us = 0;
		latency_min_us = 0;
		latency_max_us = 0;
		latency_sample_count = 0;
		qos_stats_reset();
	}
}

static int set_acl_auto_flush_timeout(struct bt_conn *conn, uint16_t flush_timeout_625us)
{
	struct net_buf *buf;
	struct net_buf *rsp = NULL;
	sdc_hci_cmd_cb_write_automatic_flush_timeout_t *cmd;
	uint16_t conn_handle;
	int err;

	err = bt_hci_get_conn_handle(conn, &conn_handle);
	if (err) {
		LOG_WRN("Failed to get connection handle for flush timeout (err %d)", err);
		return err;
	}

	buf = bt_hci_cmd_alloc(K_MSEC(LATENCY_RESPONSE_TIMEOUT_MS));
	if (!buf) {
		LOG_WRN("Failed to allocate HCI command buffer for flush timeout");
		return -ENOMEM;
	}

	cmd = net_buf_add(buf, sizeof(*cmd));
	cmd->conn_handle = sys_cpu_to_le16(conn_handle);
	cmd->flush_timeout = sys_cpu_to_le16(flush_timeout_625us);

	err = bt_hci_cmd_send_sync(SDC_HCI_OPCODE_CMD_CB_WRITE_AUTOMATIC_FLUSH_TIMEOUT, buf, &rsp);
	if (rsp) {
		net_buf_unref(rsp);
	}

	if (err) {
		LOG_WRN("Failed to write ACL auto flush timeout (err %d)", err);
		return err;
	}

	LOG_INF("ACL auto flush timeout set to %u us", (uint32_t)flush_timeout_625us * 625U);
	return 0;
}

static bool qos_vs_evt_handler(struct net_buf_simple *buf)
{
	uint8_t subevent_code;
	sdc_hci_subevent_vs_qos_conn_event_report_t *evt;
	uint32_t current_backoff_ms;

	subevent_code = net_buf_simple_pull_u8(buf);
	if (subevent_code != SDC_HCI_SUBEVENT_VS_QOS_CONN_EVENT_REPORT) {
		return false;
	}

	evt = (void *)buf->data;
	atomic_add(&qos_crc_ok_count, evt->crc_ok_count);
	atomic_add(&qos_crc_error_count, evt->crc_error_count);

	current_backoff_ms = (uint32_t)atomic_get(&qos_tx_backoff_ms);
	if (evt->crc_error_count != 0U) {
		uint32_t next_backoff_ms = MIN(QOS_BACKOFF_MAX_MS,
			current_backoff_ms + (uint32_t)evt->crc_error_count);

		atomic_set(&qos_tx_backoff_ms, (atomic_val_t)next_backoff_ms);
	} else if ((evt->crc_ok_count != 0U) && (current_backoff_ms != 0U)) {
		atomic_set(&qos_tx_backoff_ms, (atomic_val_t)(current_backoff_ms - 1U));
	}

	return true;
}

static bool vs_evt_handler(struct net_buf_simple *buf)
{
	uint8_t subevent_code;

	if (buf->len == 0U) {
		return false;
	}

	subevent_code = buf->data[0];

	if (subevent_code == SDC_HCI_SUBEVENT_VS_QOS_CONN_EVENT_REPORT) {
		return qos_vs_evt_handler(buf);
	}

	if (subevent_code == SDC_HCI_SUBEVENT_VS_CONN_ANCHOR_POINT_UPDATE_REPORT) {
		return anchor_point_vs_evt_handler(buf);
	}

	return false;
}

static int enable_vs_event_reporting(void)
{
	sdc_hci_cmd_vs_qos_conn_event_report_enable_t cmd_enable = {
		.enable = true,
	};
	sdc_hci_cmd_vs_conn_anchor_point_update_event_report_enable_t anchor_cmd_enable = {
		.enable = true,
	};
	int err;

	err = bt_hci_register_vnd_evt_cb(vs_evt_handler);
	if (err) {
		LOG_WRN("Failed to register VS callback (err %d)", err);
		return err;
	}

	err = hci_vs_sdc_qos_conn_event_report_enable(&cmd_enable);
	if (err) {
		LOG_WRN("Failed to enable QoS connection event reports (err %d)", err);
		return err;
	}

	err = hci_vs_sdc_conn_anchor_point_update_event_report_enable(&anchor_cmd_enable);
	if (err) {
		LOG_WRN("Failed to enable connection anchor point update reports (err %d)", err);
		return err;
	}

	LOG_INF("Bluetooth LE QoS and anchor-point event reporting enabled");
	return 0;
}

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_VS_SCI_MIN_INTERVAL_SERVICE_VAL),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const char *link_profile_to_str(enum link_profile profile)
{
	switch (profile) {
	case LINK_PROFILE_750US_2M:
		return "750 us + 2M PHY";
	case LINK_PROFILE_1250US_1M:
		return "1250 us + 1M PHY";
	default:
		return "Unknown";
	}
}

static void sync_active_link_profile(void)
{
	enum link_profile profile = LINK_PROFILE_UNKNOWN;

	if ((active_conn_interval_us == INTERVAL_2M_PHY_MODE_US) &&
	    (active_tx_phy == BT_GAP_LE_PHY_2M) &&
	    (active_rx_phy == BT_GAP_LE_PHY_2M)) {
		profile = LINK_PROFILE_750US_2M;
	} else if ((active_conn_interval_us == INTERVAL_1M_PHY_MIN_US) &&
		   (active_tx_phy == BT_GAP_LE_PHY_1M) &&
		   (active_rx_phy == BT_GAP_LE_PHY_1M)) {
		profile = LINK_PROFILE_1250US_1M;
	}

	active_link_profile = profile;

	if (profile != LINK_PROFILE_UNKNOWN) {
		atomic_set(&requested_link_profile, (atomic_val_t)profile);
	}
}

/* Example GATT service for exchanging minimum supported connection interval. */
static ssize_t read_min_interval(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
				 uint16_t len, uint16_t offset)
{
	uint16_t value = sys_cpu_to_le16(local_min_interval_us);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &value, sizeof(value));
}

BT_GATT_SERVICE_DEFINE(sci_min_interval_svc,
		       BT_GATT_PRIMARY_SERVICE(BT_UUID_VS_SCI_MIN_INTERVAL_SERVICE),
		       BT_GATT_CHARACTERISTIC(BT_UUID_VS_SCI_MIN_INTERVAL_CHAR, BT_GATT_CHRC_READ,
					      BT_GATT_PERM_READ, read_min_interval, NULL, NULL));

static const char *phy_to_str(uint8_t phy)
{
	switch (phy) {
	case 0:
		return "Unknown";
	case BT_GAP_LE_PHY_1M:
		return "LE 1M";
	case BT_GAP_LE_PHY_2M:
		return "LE 2M";
	case BT_GAP_LE_PHY_CODED:
		return "LE Coded";
	default:
		return "Unknown";
	}
}

static const char *fsu_initiator_to_str(enum bt_conn_le_frame_space_update_initiator initiator)
{
	switch (initiator) {
	case BT_CONN_LE_FRAME_SPACE_UPDATE_INITIATOR_LOCAL_HOST:
		return "Local Host";
	case BT_CONN_LE_FRAME_SPACE_UPDATE_INITIATOR_LOCAL_CONTROLLER:
		return "Local Controller";
	case BT_CONN_LE_FRAME_SPACE_UPDATE_INITIATOR_PEER:
		return "Peer";
	default:
		return "Unknown";
	}
}

static void scan_filter_match(struct bt_scan_device_info *device_info,
			      struct bt_scan_filter_match *filter_match, bool connectable)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));

	LOG_INF("Filters matched. Address: %s connectable: %d", addr, connectable);
}

static void scan_filter_no_match(struct bt_scan_device_info *device_info, bool connectable)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));

	// LOG_INF("Filter does not match. Address: %s connectable: %d", addr, connectable);
}

static void scan_connecting_error(struct bt_scan_device_info *device_info)
{
	LOG_WRN("Connecting failed");
}

BT_SCAN_CB_INIT(scan_cb, scan_filter_match, scan_filter_no_match, scan_connecting_error, NULL);

static void scan_init(void)
{
	int err;
	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_PASSIVE,
		.options = BT_LE_SCAN_OPT_FILTER_DUPLICATE,
		.interval = 0x0010,
		.window = 0x0010,
	};

	struct bt_scan_init_param scan_init = {
		.connect_if_match = true, .scan_param = &scan_param, .conn_param = conn_param};

	bt_scan_init(&scan_init);
	bt_scan_cb_register(&scan_cb);

	err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_UUID, BT_UUID_VS_SCI_MIN_INTERVAL_SERVICE);
	if (err) {
		LOG_WRN("Scanning filters cannot be set (err %d)", err);
		return;
	}

	err = bt_scan_filter_enable(BT_SCAN_UUID_FILTER, false);
	if (err) {
		LOG_WRN("Filters cannot be turned on (err %d)", err);
	}
}

static bool try_select_role(enum role_selection role)
{
	if (selected_role != ROLE_SELECTION_NONE) {
		return false;
	}

	selected_role = role;
	k_sem_give(&role_selected_sem);

	return true;
}

static void request_link_profile_toggle(void)
{
	enum link_profile current_profile;
	enum link_profile next_profile;

	if (!initiate_conn_rate_update ||
	    atomic_get(&link_profile_switch_enabled) == 0) {
		return;
	}

	current_profile = active_link_profile;
	if (current_profile == LINK_PROFILE_UNKNOWN) {
		current_profile = (enum link_profile)atomic_get(&requested_link_profile);
	}

	if (current_profile == LINK_PROFILE_750US_2M) {
		next_profile = LINK_PROFILE_1250US_1M;
	} else {
		next_profile = LINK_PROFILE_750US_2M;
	}

	atomic_set(&requested_link_profile, (atomic_val_t)next_profile);
	atomic_set(&link_profile_switch_pending, 1);
}

static void role_button_pressed(const struct device *port, struct gpio_callback *cb,
				uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);

	if (pins & BIT(button0.pin)) {
		try_select_role(ROLE_SELECTION_PERIPHERAL);
	} else if (pins & BIT(button1.pin)) {
		try_select_role(ROLE_SELECTION_CENTRAL);
	} else if (pins & BIT(button3.pin)) {
		request_link_profile_toggle();
	}
}

static void role_input_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		char input_char = console_getchar();

		if (selected_role != ROLE_SELECTION_NONE) {
			return;
		}

		if ((input_char == 'p') || (input_char == 'P')) {
			if (try_select_role(ROLE_SELECTION_PERIPHERAL)) {
				LOG_INF("Peripheral selected from console");
				return;
			}
		} else if ((input_char == 'c') || (input_char == 'C')) {
			if (try_select_role(ROLE_SELECTION_CENTRAL)) {
				LOG_INF("Central selected from console");
				return;
			}
		} else {
			LOG_INF("Invalid role, use p/c or BUTTON0/BUTTON1");
		}
	}
}

static int buttons_init(void)
{
	int err;

	if (!gpio_is_ready_dt(&button0) || !gpio_is_ready_dt(&button1) ||
	    !gpio_is_ready_dt(&button3)) {
		LOG_ERR("Button device not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&button0, GPIO_INPUT);
	if (err) {
		LOG_ERR("Failed to configure BUTTON0 (err %d)", err);
		return err;
	}

	err = gpio_pin_configure_dt(&button1, GPIO_INPUT);
	if (err) {
		LOG_ERR("Failed to configure BUTTON1 (err %d)", err);
		return err;
	}

	err = gpio_pin_configure_dt(&button3, GPIO_INPUT);
	if (err) {
		LOG_ERR("Failed to configure BUTTON3 (err %d)", err);
		return err;
	}

	gpio_init_callback(&button0_cb_data, role_button_pressed, BIT(button0.pin));
	err = gpio_add_callback(button0.port, &button0_cb_data);
	if (err) {
		LOG_ERR("Failed to add BUTTON0 callback (err %d)", err);
		return err;
	}

	gpio_init_callback(&button1_cb_data, role_button_pressed, BIT(button1.pin));
	err = gpio_add_callback(button1.port, &button1_cb_data);
	if (err) {
		LOG_ERR("Failed to add BUTTON1 callback (err %d)", err);
		return err;
	}

	gpio_init_callback(&button3_cb_data, role_button_pressed, BIT(button3.pin));
	err = gpio_add_callback(button3.port, &button3_cb_data);
	if (err) {
		LOG_ERR("Failed to add BUTTON3 callback (err %d)", err);
		return err;
	}

	err = gpio_pin_interrupt_configure_dt(&button0, GPIO_INT_EDGE_TO_ACTIVE);
	if (err) {
		LOG_ERR("Failed to enable BUTTON0 interrupt (err %d)", err);
		return err;
	}

	err = gpio_pin_interrupt_configure_dt(&button1, GPIO_INT_EDGE_TO_ACTIVE);
	if (err) {
		LOG_ERR("Failed to enable BUTTON1 interrupt (err %d)", err);
		return err;
	}

	err = gpio_pin_interrupt_configure_dt(&button3, GPIO_INT_EDGE_TO_ACTIVE);
	if (err) {
		LOG_ERR("Failed to enable BUTTON3 interrupt (err %d)", err);
		return err;
	}

	return 0;
}

static uint8_t read_min_interval_cb(struct bt_conn *conn, uint8_t err,
				    struct bt_gatt_read_params *params, const void *data,
				    uint16_t length)
{
	if (err) {
		LOG_WRN("Failed to read remote min interval (err %u)", err);
		k_sem_give(&min_interval_read_sem);
		return BT_GATT_ITER_STOP;
	}

	if (data && length == sizeof(uint16_t)) {
		remote_min_interval_us = sys_get_le16(data);
	}

	k_sem_give(&min_interval_read_sem);

	return BT_GATT_ITER_STOP;
}

static int read_remote_min_interval(void)
{
	static struct bt_gatt_read_params read_params;
	int err;

	read_params.func = read_min_interval_cb;
	read_params.handle_count = 1;
	read_params.single.handle = remote_min_interval_handle;
	read_params.single.offset = 0;

	err = bt_gatt_read(default_conn, &read_params);
	if (err) {
		LOG_WRN("Failed to initiate read (err %d)", err);
		return err;
	}

	return 0;
}

static void sci_discovery_complete(struct bt_gatt_dm *dm, void *context)
{
	const struct bt_gatt_dm_attr *gatt_chrc;
	const struct bt_gatt_dm_attr *gatt_value;

	LOG_INF("SCI service discovery completed");

	gatt_chrc = bt_gatt_dm_char_by_uuid(dm, BT_UUID_VS_SCI_MIN_INTERVAL_CHAR);
	if (gatt_chrc) {
		gatt_value = bt_gatt_dm_attr_next(dm, gatt_chrc);
		if (gatt_value) {
			remote_min_interval_handle = gatt_value->handle;
			LOG_INF("Found SCI min interval characteristic, handle: 0x%04x",
				remote_min_interval_handle);
		} else {
			LOG_WRN("Failed to get characteristic value attribute");
			return;
		}
	} else {
		LOG_WRN("SCI min interval characteristic not found");
		return;
	}

	bt_gatt_dm_data_release(dm);

	test_ready = true;
	k_sem_give(&discovery_complete_sem);
}

static void sci_discovery_service_not_found(struct bt_conn *conn, void *context)
{
	LOG_INF("SCI service not found");
}

static void sci_discovery_error(struct bt_conn *conn, int err, void *context)
{
	LOG_INF("Error while discovering SCI service: (err %d)", err);
}

static struct bt_gatt_dm_cb sci_discovery_cb = {
	.completed = &sci_discovery_complete,
	.service_not_found = &sci_discovery_service_not_found,
	.error_found = &sci_discovery_error,
};

static void latency_discovery_complete(struct bt_gatt_dm *dm, void *context)
{
	struct bt_latency_client *latency_client_ctx = context;
	int err;

	LOG_INF("Latency service discovery completed");

	bt_gatt_dm_data_print(dm);
	bt_latency_handles_assign(dm, latency_client_ctx);

	bt_gatt_dm_data_release(dm);

	/* Now discover the SCI min interval service */
	err = bt_gatt_dm_start(default_conn, BT_UUID_VS_SCI_MIN_INTERVAL_SERVICE, &sci_discovery_cb,
			       NULL);
	if (err) {
		LOG_WRN("SCI service discovery failed (err %d)", err);
	}
}

static void latency_discovery_service_not_found(struct bt_conn *conn, void *context)
{
	LOG_WRN("Latency service not found");
}

static void latency_discovery_error(struct bt_conn *conn, int err, void *context)
{
	LOG_WRN("Error while discovering GATT database: (err %d)", err);
}

static struct bt_gatt_dm_cb latency_discovery_cb = {
	.completed = &latency_discovery_complete,
	.service_not_found = &latency_discovery_service_not_found,
	.error_found = &latency_discovery_error,
};

static void adv_start(void)
{
	int err;

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		LOG_INF("Advertising failed to start (err %d)", err);
		return;
	}

	LOG_INF("Advertising successfully started");
}

static void scan_start(void)
{
	int err;

	err = bt_scan_start(BT_SCAN_TYPE_SCAN_PASSIVE);
	if (err) {
		LOG_WRN("Starting scanning failed (err %d)", err);
		return;
	}

	LOG_INF("Scanning successfully started");
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_WRN("Connection failed, err 0x%02x %s", err, bt_hci_err_to_str(err));
		set_connection_leds(false);
		if (is_central) {
			scan_start();
		} else {
			adv_start();
		}
		return;
	}

	conn_rate_update_pending = true;
	remote_min_interval_us = 0;
	common_min_interval_us = 0;
	remote_min_interval_handle = 0;
	requested_interval_us = MAX((uint32_t)local_min_interval_us, (uint32_t)INTERVAL_TARGET_US);
	latency_response = 0;
	latency_stats_reset();
	k_timer_stop(&latency_request_slot_timer);
	k_sem_reset(&latency_request_slot_sem);
	atomic_set(&latency_request_reference_cycle, 0);
	atomic_set(&qos_tx_backoff_ms, 0);
	atomic_set(&requested_link_profile, (atomic_val_t)LINK_PROFILE_1250US_1M);
	atomic_set(&link_profile_switch_pending, 0);
	atomic_set(&link_profile_switch_enabled, 0);
	k_work_cancel_delayable(&latency_led_off_work);
	k_sem_reset(&conn_rate_changed_sem);
	k_sem_reset(&latency_response_sem);
	active_link_profile = LINK_PROFILE_UNKNOWN;

	default_conn = bt_conn_ref(conn);
	err = bt_conn_get_info(default_conn, &conn_info);
	if (err) {
		LOG_WRN("Getting conn info failed (err %d)", err);
		return;
	}

	/* make sure we're not scanning or advertising */
	if (conn_info.role == BT_CONN_ROLE_CENTRAL) {
		bt_scan_stop();
	} else {
		bt_le_adv_stop();
	}

	LOG_INF("Connected as %s",
		conn_info.role == BT_CONN_ROLE_CENTRAL ? "central" : "peripheral");
	LOG_INF("Conn. interval is %u us", conn_info.le.interval_us);
	active_conn_interval_us = conn_info.le.interval_us;
	active_tx_phy = BT_GAP_LE_PHY_1M;
	active_rx_phy = BT_GAP_LE_PHY_1M;
	sync_active_link_profile();

	err = set_acl_auto_flush_timeout(default_conn, ACL_AUTO_FLUSH_TIMEOUT_625US);
	if (err) {
		LOG_WRN("LE Flushable ACL Data remains at controller default timeout");
	}

#if defined(CONFIG_BT_SMP)
	if (conn_info.role == BT_CONN_ROLE_PERIPHERAL) {
		err = bt_conn_set_security(conn, BT_SECURITY_L2);
		if (err) {
			LOG_WRN("Failed to set security: %d", err);
		}
	}
#else
	/* Start discovery immediately if encryption is not enabled */
	err = bt_gatt_dm_start(default_conn, BT_UUID_LATENCY, &latency_discovery_cb,
			       &latency_client);
	if (err) {
		LOG_WRN("Discover failed (err %d)", err);
	}
#endif /* CONFIG_BT_SMP */
	set_connection_leds(true);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected, reason 0x%02x %s", reason, bt_hci_err_to_str(reason));

	test_ready = false;
	conn_rate_update_pending = true;
	latency_response = 0;
	latency_stats_reset();
	k_timer_stop(&latency_request_slot_timer);
	k_sem_reset(&latency_request_slot_sem);
	atomic_set(&latency_request_reference_cycle, 0);
	atomic_set(&qos_tx_backoff_ms, 0);
	active_conn_interval_us = INTERVAL_INITIAL_US;
	atomic_set(&link_profile_switch_pending, 0);
	atomic_set(&link_profile_switch_enabled, 0);
	active_tx_phy = 0U;
	active_rx_phy = 0U;
	k_work_cancel_delayable(&latency_led_off_work);
	k_sem_reset(&conn_rate_changed_sem);
	k_sem_reset(&latency_response_sem);
	active_link_profile = LINK_PROFILE_UNKNOWN;

	if (default_conn) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
	}

	/* Add a small randomized delay before reconnecting to reduce the
	 * chance of simultaneous reconnect between both sides.
	 */
	{
		uint32_t reconnect_delay_ms = RECONNECT_DELAY_MIN_MS +
			(sys_rand32_get() % (RECONNECT_DELAY_MAX_MS - RECONNECT_DELAY_MIN_MS + 1));

		k_sleep(K_MSEC(reconnect_delay_ms));
	}

	/* Restart scanning or advertising based on configured role */
	if (is_central) {
		LOG_INF("Reconnecting as central");
		scan_start();
	} else {
		LOG_INF("Reconnecting as peripheral");
		adv_start();
	}

	set_connection_leds(false);
}

#if defined(CONFIG_BT_SMP)
void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	LOG_INF("Security changed: level %i, err: %i %s", level, err, bt_security_err_to_str(err));

	if (err != 0) {
		LOG_WRN("Failed to encrypt link");
		bt_conn_disconnect(conn, BT_HCI_ERR_PAIRING_NOT_SUPPORTED);
		return;
	}

	/* Start service discovery when link is encrypted */
	int gatt_err = bt_gatt_dm_start(default_conn, BT_UUID_LATENCY, &latency_discovery_cb,
					&latency_client);
	if (gatt_err) {
		LOG_WRN("Discover failed (err %d)", gatt_err);
	}
}
#endif /* CONFIG_BT_SMP */

static bool le_param_req(struct bt_conn *conn, struct bt_le_conn_param *param)
{
	/* Ignore peer parameter preferences. */
	return false;
}

static void log_local_min_interval_groups(void)
{
	if (!local_min_interval_groups_valid) {
		return;
	}

	if (local_min_interval_info.num_groups == 0U) {
		LOG_INF("Local SCI interval groups unavailable; controller reports rounded values only");
		return;
	}

	for (uint8_t index = 0U; index < local_min_interval_info.num_groups; index++) {
		const struct bt_conn_le_min_conn_interval_group *group =
			&local_min_interval_info.groups[index];

		LOG_INF("Local SCI group %u: %u us to %u us step %u us", index,
			group->min_125us * 125U, group->max_125us * 125U,
			group->stride_125us * 125U);
	}
}

static uint32_t first_group_interval_in_range_us(
	const struct bt_conn_le_min_conn_interval_group *group, uint32_t range_min_us,
	uint32_t range_max_us)
{
	uint32_t group_min_us = group->min_125us * 125U;
	uint32_t group_max_us = group->max_125us * 125U;
	uint32_t stride_us = group->stride_125us * 125U;
	uint32_t interval_us;

	if ((range_max_us < group_min_us) || (range_min_us > group_max_us)) {
		return 0U;
	}

	interval_us = MAX(range_min_us, group_min_us);
	if (stride_us > 0U) {
		uint32_t offset_us = interval_us - group_min_us;
		uint32_t remainder_us = offset_us % stride_us;

		if (remainder_us != 0U) {
			interval_us += stride_us - remainder_us;
		}
	}

	if ((interval_us < range_min_us) || (interval_us > range_max_us) ||
	    (interval_us > group_max_us)) {
		return 0U;
	}

	return interval_us;
}

static uint32_t select_local_supported_interval_us(uint32_t range_min_us, uint32_t range_max_us)
{
	uint32_t selected_interval_us = 0U;

	if (!local_min_interval_groups_valid || (local_min_interval_info.num_groups == 0U)) {
		return 0U;
	}

	for (uint8_t index = 0U; index < local_min_interval_info.num_groups; index++) {
		uint32_t candidate_interval_us = first_group_interval_in_range_us(
			&local_min_interval_info.groups[index], range_min_us, range_max_us);

		if ((candidate_interval_us != 0U) &&
		    ((selected_interval_us == 0U) || (candidate_interval_us < selected_interval_us))) {
			selected_interval_us = candidate_interval_us;
		}
	}

	return selected_interval_us;
}

static uint16_t conn_rate_max_ce_len_125us(uint32_t interval_max_us)
{
	uint32_t interval_ce_len_125us = interval_max_us / 125U;
	uint32_t controller_ce_len_125us = CONFIG_BT_CTLR_SDC_MAX_CONN_EVENT_LEN_DEFAULT / 125U;
	uint32_t max_ce_len_125us = MIN(interval_ce_len_125us, controller_ce_len_125us);

	return (uint16_t)MAX((uint32_t)BT_HCI_LE_SCI_CE_LEN_MIN_125US, max_ce_len_125us);
}

static int set_conn_rate_defaults(uint32_t interval_min_us, uint32_t interval_max_us)
{
	const struct bt_conn_le_conn_rate_param params = {
		.interval_min_125us = interval_min_us / 125,
		.interval_max_125us = interval_max_us / 125,
		.subrate_min = 1,
		.subrate_max = 1,
		.max_latency = 0,
		.continuation_number = 0,
		.supervision_timeout_10ms = 400,
		.min_ce_len_125us = BT_HCI_LE_SCI_CE_LEN_MIN_125US,
		.max_ce_len_125us = conn_rate_max_ce_len_125us(interval_max_us),
	};

	int err = bt_conn_le_conn_rate_set_defaults(&params);

	if (err) {
		LOG_WRN("Set default rate parameters failed (err %d)", err);
		return err;
	}

	LOG_INF("SCI default connection rate parameters set (min=%u us, max=%u us)",
		interval_min_us, interval_max_us);
	return 0;
}

static int select_lowest_frame_space(uint8_t phys_mask)
{
	const struct bt_conn_le_frame_space_update_param params = {
		.phys = phys_mask,
		.spacing_types = BT_CONN_LE_FRAME_SPACE_TYPES_MASK_ACL_IFS,
		.frame_space_min = 0,
		.frame_space_max = 150,
	};

	int err = bt_conn_le_frame_space_update(default_conn, &params);

	if (err) {
		LOG_WRN("Frame space update request failed (err %d)", err);
		return err;
	}

	/* Wait for frame space update to complete */
	k_sem_take(&frame_space_updated_sem, K_FOREVER);
	return 0;
}

static int conn_rate_request(uint32_t interval_min_us, uint32_t interval_max_us)
{
	const struct bt_conn_le_conn_rate_param params = {
		.interval_min_125us = interval_min_us / 125,
		.interval_max_125us = interval_max_us / 125,
		.subrate_min = 1,
		.subrate_max = 1,
		.max_latency = 0,
		.continuation_number = 0,
		.supervision_timeout_10ms = 400,
		.min_ce_len_125us = BT_HCI_LE_SCI_CE_LEN_MIN_125US,
		.max_ce_len_125us = conn_rate_max_ce_len_125us(interval_max_us),
	};

	int err = bt_conn_le_conn_rate_request(default_conn, &params);

	if (err) {
		LOG_WRN("Connection rate request failed (err %d)", err);
		return err;
	}

	return 0;
}

static int request_conn_interval_update(uint32_t interval_min_us, uint32_t interval_max_us)
{
	int err;

	if (interval_min_us == interval_max_us) {
		LOG_INF("Requesting new connection interval: %u us", interval_max_us);
	} else {
		LOG_INF("Requesting new connection interval range: %u us to %u us",
			interval_min_us, interval_max_us);
	}

	k_sem_reset(&conn_rate_changed_sem);
	conn_rate_change_status = BT_HCI_ERR_UNSPECIFIED;

	err = conn_rate_request(interval_min_us, interval_max_us);
	if (err) {
		return err;
	}

	err = k_sem_take(&conn_rate_changed_sem, K_MSEC(CONN_RATE_CHANGE_TIMEOUT_MS));
	if (err) {
		LOG_WRN("Timed out waiting for connection rate change after %u ms",
			CONN_RATE_CHANGE_TIMEOUT_MS);
		return err;
	}

	if (conn_rate_change_status != BT_HCI_ERR_SUCCESS) {
		LOG_WRN("Connection rate procedure completed with status 0x%02x %s",
			conn_rate_change_status, bt_hci_err_to_str(conn_rate_change_status));
		return -EIO;
	}

	return 0;
}

static int resolve_link_profile_interval_us(enum link_profile profile, uint32_t *interval_us)
{
	uint32_t target_interval_us;

	if (profile == LINK_PROFILE_750US_2M) {
		target_interval_us = MAX((uint32_t)common_min_interval_us,
			(uint32_t)INTERVAL_2M_PHY_MODE_US);
	} else if (profile == LINK_PROFILE_1250US_1M) {
		target_interval_us = INTERVAL_1M_PHY_MIN_US;
	} else {
		return -EINVAL;
	}

	if (local_min_interval_groups_valid) {
		uint32_t supported_interval_us =
			select_local_supported_interval_us(target_interval_us, target_interval_us);

		if (supported_interval_us == 0U) {
			LOG_WRN("Requested link profile interval %u us is not in the local SCI groups",
				target_interval_us);
			return -ENOTSUP;
		}

		target_interval_us = supported_interval_us;
	}

	*interval_us = target_interval_us;
	return 0;
}

static void conn_rate_changed(struct bt_conn *conn, uint8_t status,
			      const struct bt_conn_le_conn_rate_changed *params)
{
	conn_rate_change_status = status;

	if (status == BT_HCI_ERR_SUCCESS) {
		active_conn_interval_us = params->interval_us;
		sync_active_link_profile();
		LOG_INF("Connection rate changed: "
			"interval %u us, "
			"subrate factor %d, "
			"peripheral latency %d, "
			"continuation number %d, "
			"supervision timeout %d ms",
			params->interval_us, params->subrate_factor, params->peripheral_latency,
			params->continuation_number, params->supervision_timeout_10ms * 10);
	} else {
		LOG_WRN("Connection rate change failed (HCI status 0x%02x %s)", status,
			bt_hci_err_to_str(status));
	}

	k_sem_give(&conn_rate_changed_sem);
}

static int update_to_1m_phy(void)
{
	struct bt_conn_le_phy_param phy;

	phy.options = BT_CONN_LE_PHY_OPT_NONE;
	phy.pref_rx_phy = BT_GAP_LE_PHY_1M;
	phy.pref_tx_phy = BT_GAP_LE_PHY_1M;

	int err = bt_conn_le_phy_update(default_conn, &phy);

	if (err) {
		LOG_WRN("PHY update failed: %d", err);
		return err;
	}

	k_sem_take(&phy_updated, K_FOREVER);
	return 0;
}

static int update_to_2m_phy(void)
{
	struct bt_conn_le_phy_param phy;

	phy.options = BT_CONN_LE_PHY_OPT_NONE;
	phy.pref_rx_phy = BT_GAP_LE_PHY_2M;
	phy.pref_tx_phy = BT_GAP_LE_PHY_2M;

	int err = bt_conn_le_phy_update(default_conn, &phy);

	if (err) {
		LOG_WRN("PHY update failed: %d", err);
		return err;
	}

	k_sem_take(&phy_updated, K_FOREVER);
	return 0;
}

static int switch_to_750us_2m_profile(void)
{
	uint32_t target_interval_us;
	uint32_t request_max_interval_us;
	int err;

	err = resolve_link_profile_interval_us(LINK_PROFILE_750US_2M, &target_interval_us);
	if (err) {
		return err;
	}

	err = update_to_2m_phy();
	if (err) {
		return err;
	}

	err = select_lowest_frame_space(BT_HCI_LE_FRAME_SPACE_UPDATE_PHY_2M_MASK);
	if (err) {
		return err;
	}

	request_max_interval_us = target_interval_us;
	if (!initiate_conn_rate_update && active_conn_interval_us > target_interval_us) {
		/* As a peripheral, request a bounded range so the central can pick the
		 * lowest interval it can support instead of rejecting a strict 750 us point.
		 */
		request_max_interval_us = active_conn_interval_us;
	}

	err = request_conn_interval_update(target_interval_us, request_max_interval_us);
	if (err) {
		return err;
	}

	requested_interval_us = target_interval_us;
	active_link_profile = LINK_PROFILE_750US_2M;
	return 0;
}

static int switch_to_1250us_1m_profile(void)
{
	uint32_t target_interval_us;
	int err;

	err = resolve_link_profile_interval_us(LINK_PROFILE_1250US_1M, &target_interval_us);
	if (err) {
		return err;
	}

	err = request_conn_interval_update(target_interval_us, target_interval_us);
	if (err) {
		return err;
	}

	err = update_to_1m_phy();
	if (err) {
		return err;
	}

	err = select_lowest_frame_space(BT_HCI_LE_FRAME_SPACE_UPDATE_PHY_1M_MASK);
	if (err) {
		return err;
	}

	requested_interval_us = target_interval_us;
	active_link_profile = LINK_PROFILE_1250US_1M;
	return 0;
}

static void process_pending_link_profile_switch(void)
{
	enum link_profile target_profile;
	int err;

	if (atomic_get(&link_profile_switch_pending) == 0) {
		return;
	}

	atomic_set(&link_profile_switch_pending, 0);
	target_profile = (enum link_profile)atomic_get(&requested_link_profile);
	if (target_profile == active_link_profile) {
		return;
	}

	LOG_INF("Switching link profile to %s", link_profile_to_str(target_profile));
	if (target_profile == LINK_PROFILE_750US_2M) {
		err = switch_to_750us_2m_profile();
	} else if (target_profile == LINK_PROFILE_1250US_1M) {
		err = switch_to_1250us_1m_profile();
	} else {
		err = -EINVAL;
	}

	if (err) {
		LOG_WRN("Failed to switch link profile to %s (err %d); keeping %s",
			link_profile_to_str(target_profile), err,
			link_profile_to_str(active_link_profile));
		atomic_set(&requested_link_profile, (atomic_val_t)active_link_profile);
		return;
	}

	atomic_set(&requested_link_profile, (atomic_val_t)active_link_profile);
	k_timer_stop(&latency_request_slot_timer);
	latency_stats_reset();
	k_sem_reset(&latency_request_slot_sem);
	atomic_set(&latency_request_reference_cycle, 0);
	LOG_INF("Link profile active: %s", link_profile_to_str(active_link_profile));
}

static void le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *param)
{
	active_tx_phy = param->tx_phy;
	active_rx_phy = param->rx_phy;
	sync_active_link_profile();
	LOG_INF("LE PHY updated: TX PHY %s, RX PHY %s", phy_to_str(param->tx_phy),
		phy_to_str(param->rx_phy));
	k_sem_give(&phy_updated);
}

static void frame_space_updated(struct bt_conn *conn,
				const struct bt_conn_le_frame_space_updated *params)
{
	if (params->status == BT_HCI_ERR_SUCCESS) {
		LOG_INF("Frame space updated: %u us, PHYs: 0x%02x, spacing types: 0x%04x, "
			"initiator: %s",
			params->frame_space, params->phys, params->spacing_types,
			fsu_initiator_to_str(params->initiator));
	} else {
		LOG_WRN("Frame space update failed (HCI status 0x%02x %s)", params->status,
			bt_hci_err_to_str(params->status));
	}

	k_sem_give(&frame_space_updated_sem);
}

static void latency_response_handler(const void *buf, uint16_t len)
{
	uint32_t latency_time;
	k_work_cancel_delayable(&latency_led_off_work);
	gpio_pin_set_dt(&led1, 0);

	if (len == sizeof(latency_time)) {
		/* compute how much time was spent */
		latency_time = *((uint32_t *)buf);
		uint32_t cycles_spent = k_cycle_get_32() - latency_time;

		latency_response = (uint32_t)k_cyc_to_ns_floor64(cycles_spent) / 2000;
	}

	k_sem_give(&latency_response_sem);
}

static const struct bt_latency_client_cb latency_client_cb = {.latency_response =
								      latency_response_handler};

static void test_run(void)
{
	int err;

	if (!test_ready) {
		/* disconnected while blocking inside _getchar() */
		return;
	}

	test_ready = false;
	latency_stats_reset();

	/* SCI needs the reduced frame space before the controller evaluates the
	 * request. Do that on the 2M path first, then switch to 1M after SCI succeeds.
	 */
	if (initiate_conn_rate_update) {
		err = select_lowest_frame_space(BT_HCI_LE_FRAME_SPACE_UPDATE_PHY_2M_MASK);
		if (err) {
			LOG_WRN("2M frame space update failed (err %d)", err);
			return;
		}
	}

	/* Read remote min interval if characteristic was found */
	if (remote_min_interval_handle != 0) {
		err = read_remote_min_interval();
		if (err == 0) {
			k_sem_take(&min_interval_read_sem, K_FOREVER);

			/* Select the shortest interval supported by both devices. */
			common_min_interval_us = MAX(local_min_interval_us, remote_min_interval_us);
			requested_interval_us =
				MAX((uint32_t)common_min_interval_us, (uint32_t)INTERVAL_TARGET_US);

			LOG_INF("Minimum connection intervals: Local: %u us, Peer: %u us, "
				"Common: %u us",
				local_min_interval_us, remote_min_interval_us,
				common_min_interval_us);
		}
	}

	if (initiate_conn_rate_update && conn_rate_update_pending) {
		uint32_t request_min_us = requested_interval_us;
		uint32_t request_max_us = requested_interval_us;

		/* On 1M PHY, the controller may not support every 125 us step up to the
		 * target interval. Request a bounded range so it can select the best value
		 * that stays at or below the 1 ms ceiling.
		 */
		if (common_min_interval_us != 0U && common_min_interval_us < requested_interval_us) {
			request_min_us = common_min_interval_us;
		}

		if (local_min_interval_groups_valid) {
			uint32_t supported_interval_us =
				select_local_supported_interval_us(request_min_us, request_max_us);

			if (supported_interval_us == 0U) {
				LOG_WRN("No local SCI interval supported in range %u us to %u us",
					request_min_us, request_max_us);
				return;
			}

			request_min_us = supported_interval_us;
			request_max_us = supported_interval_us;
			LOG_INF("Selected local supported SCI interval: %u us", supported_interval_us);
		}

		err = request_conn_interval_update(request_min_us, request_max_us);
		if (err) {
			LOG_WRN("Connection rate update failed (err %d)", err);
			return;
		}

		conn_rate_update_pending = false;
		active_link_profile = LINK_PROFILE_750US_2M;
		atomic_set(&requested_link_profile, (atomic_val_t)active_link_profile);
	}

	if (initiate_conn_rate_update && active_conn_interval_us <= INTERVAL_TARGET_US) {
		LOG_INF("Requesting wider connection interval before 1M PHY switch: %u us",
			INTERVAL_1M_PHY_MIN_US);
		err = switch_to_1250us_1m_profile();
		if (err) {
			LOG_WRN("Keeping current PHY after 1250 us + 1M transition failure (err %d)",
				err);
			goto latency_loop;
		}

}
latency_loop:
	atomic_set(&link_profile_switch_enabled, 1);
	k_timer_stop(&latency_request_slot_timer);
	k_sem_reset(&latency_request_slot_sem);
	atomic_set(&latency_request_reference_cycle, 0);

	/* Start sending timestamps to the peer device */
	while (default_conn) {
		uint32_t qos_backoff_ms = (uint32_t)atomic_get(&qos_tx_backoff_ms);
		uint32_t time;

		process_pending_link_profile_switch();

		if (!initiate_conn_rate_update) {
			k_sleep(K_MSEC(10));
			continue;
		}

		if (qos_backoff_ms != 0U) {
			k_sleep(K_MSEC(qos_backoff_ms));
		}

		atomic_set(&latency_request_reference_cycle, 0);
		k_sem_reset(&latency_request_slot_sem);
		err = k_sem_take(&latency_request_slot_sem, K_MSEC(LATENCY_RESPONSE_TIMEOUT_MS));
		if (err) {
			LOG_WRN("Timed out waiting for the next latency request slot");
			continue;
		}

		k_sem_reset(&latency_response_sem);
		latency_response = 0;
		time = (uint32_t)atomic_get(&latency_request_reference_cycle);
		if (time == 0U) {
			time = k_cycle_get_32();
		}
		gpio_pin_set_dt(&led1, 1);
		err = bt_latency_request(&latency_client, &time, sizeof(time));
		if (err && err != -EALREADY) {
			LOG_WRN("Latency failed (err %d)", err);
			gpio_pin_set_dt(&led1, 0);
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
			LOG_WRN("Did not receive a latency response");
			gpio_pin_set_dt(&led1, 0);
		}

		latency_response = 0;
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.le_phy_updated = le_phy_updated,
	.le_param_req = le_param_req,
	.conn_rate_changed = conn_rate_changed,
	.frame_space_updated = frame_space_updated,
#if defined(CONFIG_BT_SMP)
	.security_changed = security_changed,
#endif /* CONFIG_BT_SMP */
};

int main(void)
{
	int err;

	console_init();

	if (!gpio_is_ready_dt(&led0)) {
		LOG_ERR("LED device not ready");
		return 0;
	}
	gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
	set_connection_leds(false);

	err = buttons_init();
	if (err) {
		return 0;
	}
	
	LOG_INF("Starting Bluetooth Shorter Connection Intervals sample");

	err = bt_enable(NULL);
	if (err) {
		LOG_WRN("Bluetooth init failed (err %d)", err);
		return 0;
	}

	LOG_INF("Bluetooth initialized");

	err = enable_vs_event_reporting();
	if (err) {
		LOG_WRN("Bluetooth LE VS event reporting unavailable (err %d)", err);
	}

	err = bt_conn_le_read_min_conn_interval_groups(&local_min_interval_info);
	if (err) {
		LOG_WRN("Failed to read local SCI interval groups (err %d)", err);

		/* Fall back to the minimum-only query when interval groups are unavailable. */
		err = bt_conn_le_read_min_conn_interval(&local_min_interval_us);
		if (err) {
			LOG_WRN("Failed to read min conn interval (err %d)", err);
			return 0;
		}
	} else {
		local_min_interval_us = local_min_interval_info.min_supported_conn_interval_us;
		local_min_interval_groups_valid = true;
		log_local_min_interval_groups();
	}
	LOG_INF("Local minimum connection interval: %u us", local_min_interval_us);

	/* Set the initial allowed range of parameters that can be requested by the peripheral.
	 * They will be overridden by any calls to bt_conn_le_conn_rate_request().
	 */
	err = set_conn_rate_defaults(local_min_interval_us, INTERVAL_INITIAL_US);
	if (err) {
		LOG_WRN("Failed to set conn rate defaults");
		return 0;
	}

	err = bt_latency_init(&latency, &latency_service_cb);
	if (err) {
		LOG_WRN("Latency service initialization failed (err %d)", err);
		return 0;
	}

	err = bt_latency_client_init(&latency_client, &latency_client_cb);
	if (err) {
		LOG_WRN("Latency client initialization failed (err %d)", err);
		return 0;
	}

#if 0
	while (true) {
		LOG_INF("Should this device initiate connection interval updates?\n"
			"Type y (yes, this device will initiate) or n (no, peer will initiate): ");

		char input_char = console_getchar();

		LOG_INF("");

		if (input_char == 'y') {
			initiate_conn_rate_update = true;
			LOG_INF("This device will initiate connection interval updates");
			break;
		} else if (input_char == 'n') {
			initiate_conn_rate_update = false;
			LOG_INF("The peer device will initiate connection interval updates");
			break;
		}

		LOG_INF("Invalid input");
	}
#endif

	k_thread_create(&role_input_thread_data, role_input_thread_stack,
			K_THREAD_STACK_SIZEOF(role_input_thread_stack), role_input_thread,
			NULL, NULL, NULL, ROLE_INPUT_THREAD_PRIORITY, 0, K_NO_WAIT);

	LOG_INF("Press BUTTON0 for peripheral or BUTTON1 for central, or type p/c");
	k_sem_take(&role_selected_sem, K_FOREVER);
	gpio_pin_interrupt_configure_dt(&button0, GPIO_INT_DISABLE);
	gpio_pin_interrupt_configure_dt(&button1, GPIO_INT_DISABLE);

	if (selected_role == ROLE_SELECTION_CENTRAL) {
		is_central = true;
		initiate_conn_rate_update = true;
		LOG_INF("Central. Starting scanning");
		LOG_INF("Press BUTTON3 to toggle between 750 us + 2M PHY and 1250 us + 1M PHY");
		scan_init();
		scan_start();
	} else {
		is_central = false;
		initiate_conn_rate_update = false;
		LOG_INF("Peripheral. Starting advertising");
		adv_start();
	}

	for (;;) {
		/* Wait for service discovery to complete */
		k_sem_take(&discovery_complete_sem, K_FOREVER);

		test_run();
	}
}

