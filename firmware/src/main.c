#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "air_ctrl_sensor.h"
#include "air_ctrl_bt.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

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
			LOG_INF(
				"ts_ns,temp_raw_c,temp_comp_c,hum_raw_rh,hum_comp_rh,press_raw_pa,gas_raw_ohm,iaq,iaq_acc,static_iaq,co2_eq_ppm,breath_voc_eq_ppm,gas_pct,stabilized,run_in"
			);
			LOG_INF(
				"%lld,%.2f,%.2f,%.2f,%.2f,%.0f,%.0f,%.1f,%u,%.1f,%.0f,%.3f,%.1f,%u,%u",
				(long long)sensor_data.timestamp_ns,
				sensor_data.raw_temperature,
				sensor_data.temperature,
				sensor_data.raw_humidity,
				sensor_data.humidity,
				sensor_data.raw_pressure,
				sensor_data.raw_gas_resistance,
				sensor_data.iaq,
				sensor_data.iaq_accuracy,
				sensor_data.static_iaq,
				sensor_data.co2_equivalent,
				sensor_data.breath_voc_equivalent,
				sensor_data.gas_percentage,
				sensor_data.stabilization_status > 0.5f ? 1U : 0U,
				sensor_data.run_in_status > 0.5f ? 1U : 0U
			);
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
