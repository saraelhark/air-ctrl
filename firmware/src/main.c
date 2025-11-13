#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <errno.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

const struct device *const bme = DEVICE_DT_GET_ONE(bosch_bme680);

#define DISPLAY_NODE DT_NODELABEL(st7789v)
#define BACKLIGHT_NODE DT_NODELABEL(backlight)

BUILD_ASSERT(DT_NODE_HAS_STATUS(DISPLAY_NODE, okay), "Display node not enabled");
BUILD_ASSERT(DT_NODE_HAS_STATUS(BACKLIGHT_NODE, okay), "Backlight node not enabled");

static const struct device *const display = DEVICE_DT_GET(DISPLAY_NODE);
static const struct gpio_dt_spec backlight = GPIO_DT_SPEC_GET(BACKLIGHT_NODE, gpios);

static int display_test_fill(void)
{
    int err;

    /* Check if display device is ready */
    if (!device_is_ready(display)) {
        LOG_ERR("Display device not ready");
        return -ENODEV;
    }

    /* Turn on backlight - set to ACTIVE */
    if (device_is_ready(backlight.port)) {
        err = gpio_pin_configure_dt(&backlight, GPIO_OUTPUT_ACTIVE);
        if (err) {
            LOG_ERR("Back-light GPIO config failed (%d)", err);
            return err;
        }
    } else {
        LOG_WRN("Backlight GPIO not ready");
    }

    /* Get display capabilities */
    struct display_capabilities caps;
    display_get_capabilities(display, &caps);
    
    LOG_INF("Display: %dx%d, pixel format: 0x%x", 
            caps.x_resolution, caps.y_resolution, 
            caps.current_pixel_format);

    /* Verify RGB565 support */
    if (!(caps.supported_pixel_formats & PIXEL_FORMAT_RGB_565)) {
        LOG_ERR("Display lacks RGB565 support (mask 0x%08x)",
                caps.supported_pixel_formats);
        return -ENOTSUP;
    }

    /* Turn off blanking to make display visible */
    err = display_blanking_off(display);
    if (err) {
        LOG_ERR("Failed to turn off blanking (%d)", err);
        return err;
    }

    /* Fill entire screen with GREEN */
    static uint16_t line_buf[240];
    const uint16_t green = 0x07E0; /* RGB565 green: 5 bits red (0), 6 bits green (all set), 5 bits blue (0) */
    
    /* Fill line buffer with green */
    for (int i = 0; i < 240; i++) {
        line_buf[i] = green;
    }
    
    struct display_buffer_descriptor desc = {
        .width = caps.x_resolution,
        .height = 1,
        .pitch = caps.x_resolution,
        .buf_size = caps.x_resolution * sizeof(uint16_t),
    };
    
    LOG_INF("Filling entire %dx%d display with GREEN...", caps.x_resolution, caps.y_resolution);
    
    /* Fill entire screen line by line */
    for (uint16_t y = 0; y < caps.y_resolution; y++) {
        err = display_write(display, 0, y, &desc, (uint8_t *)line_buf);
        if (err) {
            LOG_ERR("Display write failed at row %u (%d)", y, err);
            return err;
        }
    }
    
    LOG_INF("Screen should be completely GREEN now!");
    return 0;
}

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("Connection failed err 0x%02x %s", err, bt_hci_err_to_str(err));
	} else {
		LOG_INF("Connected");
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected, reason 0x%02x %s", reason, bt_hci_err_to_str(reason));
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected
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

int main(void)
{
	int err;

	// err = bt_enable(NULL);
	// if (err) {
	// 	LOG_ERR("Bluetooth init failed (err %d)", err);
	// 	return 0;
	// }

	// bt_ready();

	/* Check BME688 sensor */
	if (!device_is_ready(bme)) {
		LOG_ERR("BME688 not ready");
		return 0;
	}

    k_sleep(K_SECONDS(10));

	err = display_test_fill();
	if (err) {
		LOG_ERR("Display test failed (%d)", err);
	} else {
		LOG_INF("Display filled with test color");
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
