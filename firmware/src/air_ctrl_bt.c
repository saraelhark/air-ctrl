#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <errno.h>
#include <limits.h>
#include <string.h>

#include "air_ctrl_bt.h"

LOG_MODULE_REGISTER(air_ctrl_bt, LOG_LEVEL_INF);

struct __packed air_ctrl_ble_sample_v1 {
	uint8_t version;
	uint8_t flags;
	uint16_t seq;
	uint32_t timestamp_ms;
	int16_t temp_c_x100;
	uint16_t hum_rh_x100;
	uint32_t gas_ohm;
	uint16_t iaq_x10;
	uint8_t iaq_acc;
	uint16_t co2_eq_ppm;
	uint16_t breath_voc_eq_ppb;
};

static struct bt_conn *default_conn;
static bool notify_enabled;
static uint16_t sample_seq;

static uint8_t last_sample[sizeof(struct air_ctrl_ble_sample_v1)];
static size_t last_sample_len;

#define BT_UUID_AIR_CTRL_SERVICE BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x7f5f2cc2, 0x5d7a, 0x4a34, 0xa8c8, 0x1c7e3c01a7e1))

#define BT_UUID_AIR_CTRL_SAMPLE BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x7f5f2cc3, 0x5d7a, 0x4a34, 0xa8c8, 0x1c7e3c01a7e1))

static void ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	notify_enabled = (value == BT_GATT_CCC_NOTIFY);
}

static ssize_t read_sample(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			   uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, last_sample, last_sample_len);
}

BT_GATT_SERVICE_DEFINE(air_ctrl_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_AIR_CTRL_SERVICE),
	BT_GATT_CHARACTERISTIC(BT_UUID_AIR_CTRL_SAMPLE,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_sample, NULL, NULL),
	BT_GATT_CCC(ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("Connection failed err 0x%02x %s", err, bt_hci_err_to_str(err));
	} else {
		if (default_conn == NULL) {
			default_conn = bt_conn_ref(conn);
		}
		LOG_INF("Connected");
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	if (default_conn != NULL) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
	}

	notify_enabled = false;
	LOG_INF("Disconnected, reason 0x%02x %s", reason, bt_hci_err_to_str(reason));
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static void bt_ready(void)
{
	int err;

	LOG_INF("Bluetooth initialized");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);

	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
	} else {
		LOG_INF("Advertising successfully started");
	}
}

int air_ctrl_bt_init(void)
{
	int err;

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return err;
	}

	bt_ready();
	return 0;
}

bool air_ctrl_bt_is_connected(void)
{
	return default_conn != NULL;
}

static uint32_t ns_to_ms_u32(int64_t timestamp_ns)
{
	if (timestamp_ns <= 0) {
		return 0U;
	}

	return (uint32_t)(timestamp_ns / 1000000LL);
}

int air_ctrl_bt_notify_sensor_data(const air_ctrl_sensor_data_t *data)
{
	struct air_ctrl_ble_sample_v1 sample;
	uint16_t iaq_x10;
	uint32_t gas_ohm;
	uint16_t hum_x100;
	int16_t temp_x100;
	uint16_t co2_eq_ppm;
	uint16_t breath_voc_eq_ppb;

	if (data == NULL) {
		return -EINVAL;
	}

	if (!air_ctrl_bt_is_connected()) {
		return -ENOTCONN;
	}

	if (!notify_enabled) {
		return -EACCES;
	}

	gas_ohm = (data->raw_gas_resistance <= 0.0f) ? 0U : (uint32_t)data->raw_gas_resistance;

	temp_x100 = (int16_t)CLAMP((int32_t)(data->raw_temperature * 100.0f), INT16_MIN, INT16_MAX);
	hum_x100 = (uint16_t)CLAMP((int32_t)(data->raw_humidity * 100.0f), 0, UINT16_MAX);

	iaq_x10 = (data->iaq <= 0.0f) ? 0U : (uint16_t)CLAMP((int32_t)(data->iaq * 10.0f), 0, UINT16_MAX);
	co2_eq_ppm = (data->co2_equivalent <= 0.0f) ? 0U :
		(uint16_t)CLAMP((int32_t)(data->co2_equivalent + 0.5f), 0, UINT16_MAX);
	breath_voc_eq_ppb = (data->breath_voc_equivalent <= 0.0f) ? 0U :
		(uint16_t)CLAMP((int32_t)(data->breath_voc_equivalent * 1000.0f + 0.5f), 0, UINT16_MAX);

	sample.version = 2U;
	sample.flags = 0U;
	sample.seq = sys_cpu_to_le16(sample_seq++);
	sample.timestamp_ms = sys_cpu_to_le32(ns_to_ms_u32(data->timestamp_ns));
	sample.temp_c_x100 = (int16_t)sys_cpu_to_le16((uint16_t)temp_x100);
	sample.hum_rh_x100 = sys_cpu_to_le16(hum_x100);
	sample.gas_ohm = sys_cpu_to_le32(gas_ohm);
	sample.iaq_x10 = sys_cpu_to_le16(iaq_x10);
	sample.iaq_acc = data->iaq_accuracy;
	sample.co2_eq_ppm = sys_cpu_to_le16(co2_eq_ppm);
	sample.breath_voc_eq_ppb = sys_cpu_to_le16(breath_voc_eq_ppb);

	memcpy(last_sample, &sample, sizeof(sample));
	last_sample_len = sizeof(sample);

	return bt_gatt_notify(default_conn, &air_ctrl_svc.attrs[2], last_sample, last_sample_len);
}
