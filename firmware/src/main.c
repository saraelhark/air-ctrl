#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

const struct device *const bme = DEVICE_DT_GET_ONE(bosch_bme680);

int main(void)
{
	    if (!device_is_ready(bme)) {
        LOG_ERR("BME688 not ready");
        return 0;
    }

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
