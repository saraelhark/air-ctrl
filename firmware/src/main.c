#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "air_ctrl_sensor.h"
#include "air_ctrl_bt.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#if IS_ENABLED(CONFIG_AIR_CTRL_USE_BSEC)
static void print_iaq_accuracy(uint8_t accuracy)
{
	const char *acc_str;
	switch (accuracy) {
	case 0: acc_str = "Stabilizing"; break;
	case 1: acc_str = "Low"; break;
	case 2: acc_str = "Medium"; break;
	case 3: acc_str = "High"; break;
	default: acc_str = "Unknown"; break;
	}
	LOG_INF("  IAQ Accuracy: %s (%d)", acc_str, accuracy);
}
#endif

int main(void)
{
	int err;
	air_ctrl_sensor_data_t sensor_data;

	err = air_ctrl_bt_init();
	if (err) {
		LOG_ERR("Bluetooth init failed: %d", err);
	}

	LOG_INF("Air-ctrl device starting...");

	err = air_ctrl_sensor_init();
	if (err != 0) {
		LOG_ERR("Sensor integration init failed: %d", err);
		return 0;
	}

	#if IS_ENABLED(CONFIG_AIR_CTRL_USE_BSEC)
	LOG_INF("BSEC initialized, starting sensor loop...");
	LOG_INF("Note: IAQ accuracy will improve over time (needs ~5 min warm-up)");
	#else
	LOG_INF("Raw sensor mode initialized, starting sensor loop...");
	#endif

	while (true) {
		if (air_ctrl_sensor_run(&sensor_data)) {
			#if IS_ENABLED(CONFIG_AIR_CTRL_USE_BSEC)
			LOG_INF("=== BME688 BSEC Data ===");
			
			LOG_INF("  Temperature: %.2f °C (raw: %.2f °C)",
				sensor_data.temperature,
				sensor_data.raw_temperature);
			LOG_INF("  Humidity: %.2f %%RH (raw: %.2f %%RH)",
				sensor_data.humidity,
				sensor_data.raw_humidity);
			
			LOG_INF("  Pressure: %.2f hPa",
				sensor_data.raw_pressure / 100.0);
			
			LOG_INF("  Gas Resistance: %.0f Ohm",
				sensor_data.raw_gas_resistance);
			
			LOG_INF("  IAQ: %.1f", sensor_data.iaq);
			print_iaq_accuracy(sensor_data.iaq_accuracy);
			LOG_INF("  Static IAQ: %.1f", sensor_data.static_iaq);
			
			LOG_INF("  CO2 Equivalent: %.0f ppm",
				sensor_data.co2_equivalent);
			LOG_INF("  Breath VOC: %.3f ppm",
				sensor_data.breath_voc_equivalent);
			LOG_INF("  Gas Percentage: %.1f %%",
				sensor_data.gas_percentage);
			
			LOG_INF("  Stabilization: %s, Run-in: %s",
				sensor_data.stabilization_status > 0.5f ? "Done" : "Ongoing",
				sensor_data.run_in_status > 0.5f ? "Done" : "Ongoing");
			#else
			LOG_INF("=== BME688 Raw Data ===");
			LOG_INF("  Temperature: %.2f °C", sensor_data.raw_temperature);
			LOG_INF("  Humidity: %.2f %%RH", sensor_data.raw_humidity);
			LOG_INF("  Pressure: %.2f hPa", sensor_data.raw_pressure / 100.0);
			LOG_INF("  Gas Resistance: %.0f Ohm", sensor_data.raw_gas_resistance);
			#endif
		}

		k_sleep(K_MSEC(100));
	}
}
