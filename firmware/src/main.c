#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/printk.h>
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

const struct device *const bme = DEVICE_DT_GET_ONE(bosch_bme680);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed err 0x%02x %s\n", err, bt_hci_err_to_str(err));
	} else {
		printk("Connected\n");
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected, reason 0x%02x %s\n", reason, bt_hci_err_to_str(reason));
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected
};

static void bt_ready(void)
{
	int err;

	printk("Bluetooth initialized\n");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);

	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
	} else {
		printk("Advertising successfully started\n");
	}
}

int main(void)
{
	int err;

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	bt_ready();

	/* Check BME688 sensor */
	if (!device_is_ready(bme)) {
		LOG_ERR("BME688 not ready");
		return 0;
	}

	LOG_INF("Air-ctrl device starting...");

    while (true) {
        struct sensor_value temp, pressure, humidity, gas;

        if (sensor_sample_fetch(bme) < 0) {
            LOG_ERR("Sample fetch failed");
        } else if (
            sensor_channel_get(bme, SENSOR_CHAN_AMBIENT_TEMP, &temp) ||
            sensor_channel_get(bme, SENSOR_CHAN_PRESS, &pressure) ||
            sensor_channel_get(bme, SENSOR_CHAN_HUMIDITY, &humidity) ||
            sensor_channel_get(bme, SENSOR_CHAN_GAS_RES, &gas)) {
            LOG_ERR("Channel read failed");
        } else {
            LOG_INF("T=%.2f°C P=%.2fkPa RH=%.1f%% Gas=%dΩ",
                    sensor_value_to_double(&temp),
                    sensor_value_to_double(&pressure) / 1000.0,
                    sensor_value_to_double(&humidity),
                    gas.val1);
        }

        k_sleep(K_SECONDS(2));
    }
}
