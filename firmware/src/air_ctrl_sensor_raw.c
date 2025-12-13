#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>

#include <errno.h>
#include <string.h>

#include "air_ctrl_sensor.h"

LOG_MODULE_REGISTER(air_ctrl_sensor, LOG_LEVEL_DBG);

#define RAW_SAMPLE_PERIOD_NS (3LL * 1000000000LL)

static const struct device *const bme = DEVICE_DT_GET_ONE(bosch_bme680);

static int64_t next_call_ns;

int64_t air_ctrl_sensor_get_timestamp_ns(void)
{
	return k_ticks_to_ns_near64(k_uptime_ticks());
}

int air_ctrl_sensor_init(void)
{
	if (!device_is_ready(bme)) {
		LOG_ERR("BME680 not ready");
		return -ENODEV;
	}

	next_call_ns = 0;
	LOG_INF("Raw BME688 mode initialized");
	return 0;
}

int64_t air_ctrl_sensor_get_next_call_ns(void)
{
	return next_call_ns;
}

bool air_ctrl_sensor_run(air_ctrl_sensor_data_t *output)
{
	struct sensor_value temp;
	struct sensor_value pressure;
	struct sensor_value humidity;
	struct sensor_value gas;
	int64_t timestamp_ns;

	if (output == NULL) {
		return false;
	}

	timestamp_ns = air_ctrl_sensor_get_timestamp_ns();
	if (timestamp_ns < next_call_ns) {
		return false;
	}

	if (sensor_sample_fetch(bme) < 0) {
		LOG_ERR("BME680 sample fetch failed");
		next_call_ns = timestamp_ns + RAW_SAMPLE_PERIOD_NS;
		return false;
	}

	if (sensor_channel_get(bme, SENSOR_CHAN_AMBIENT_TEMP, &temp) ||
		sensor_channel_get(bme, SENSOR_CHAN_PRESS, &pressure) ||
		sensor_channel_get(bme, SENSOR_CHAN_HUMIDITY, &humidity) ||
		sensor_channel_get(bme, SENSOR_CHAN_GAS_RES, &gas)) {
		LOG_ERR("BME680 channel read failed");
		next_call_ns = timestamp_ns + RAW_SAMPLE_PERIOD_NS;
		return false;
	}

	memset(output, 0, sizeof(*output));

	output->timestamp_ns = timestamp_ns;
	output->raw_temperature = (float)sensor_value_to_double(&temp);
	output->raw_humidity = (float)sensor_value_to_double(&humidity);
	output->raw_pressure = (float)(sensor_value_to_double(&pressure) * 1000.0);
	output->raw_gas_resistance = (float)gas.val1;

	output->temperature = output->raw_temperature;
	output->humidity = output->raw_humidity;

	next_call_ns = timestamp_ns + RAW_SAMPLE_PERIOD_NS;
	return true;
}
