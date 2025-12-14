#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/settings/settings.h>

#include <errno.h>
#include <string.h>

#include "air_ctrl_sensor.h"
#include "bsec_interface.h"
#include "bsec_datatypes.h"
#include "bsec_selectivity.h"

LOG_MODULE_REGISTER(air_ctrl_sensor_bsec, LOG_LEVEL_DBG);

#define BSEC_INSTANCE_SIZE 3272

#define BSEC_CHECK_INPUT(x, shift) (x & (1 << ((shift) - 1)))

static bsec_bme_settings_t bme_settings;

static float temp_offset = 0.0f;

static const struct device *const bme = DEVICE_DT_GET_ONE(bosch_bme680);

static uint8_t bsec_mem[BSEC_INSTANCE_SIZE] __aligned(4);
static void *bsec_instance = NULL;

static uint8_t bsec_work_buffer[BSEC_MAX_WORKBUFFER_SIZE] __aligned(4);

#define BSEC_STATE_SAVE_INTERVAL_NS (5LL * 60LL * 1000000000LL)

static uint8_t bsec_state_blob[BSEC_MAX_STATE_BLOB_SIZE] __aligned(4);
static size_t bsec_state_blob_len;
static bool bsec_state_blob_valid;
static int64_t bsec_last_state_save_ns;

static int bsec_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	if (strcmp(name, "state") == 0) {
		if (len > sizeof(bsec_state_blob)) {
			return -EINVAL;
		}

		ssize_t rc = read_cb(cb_arg, bsec_state_blob, len);
		if (rc < 0) {
			return (int)rc;
		}

		bsec_state_blob_len = (size_t)rc;
		bsec_state_blob_valid = true;
		return 0;
	}

	return -ENOENT;
}

static struct settings_handler bsec_settings_handler = {
	.name = "air_ctrl/bsec",
	.h_set = bsec_settings_set,
};

static void bsec_state_save_if_needed(int64_t timestamp_ns)
{
	if (!IS_ENABLED(CONFIG_SETTINGS)) {
		return;
	}

	if (timestamp_ns < (bsec_last_state_save_ns + BSEC_STATE_SAVE_INTERVAL_NS)) {
		return;
	}

	uint32_t state_len = 0;
	memset(bsec_work_buffer, 0, sizeof(bsec_work_buffer));
	bsec_library_return_t bsec_status = bsec_get_state(bsec_instance, 0, bsec_state_blob,
						   sizeof(bsec_state_blob), bsec_work_buffer,
						   sizeof(bsec_work_buffer), &state_len);
	if (bsec_status != BSEC_OK) {
		LOG_ERR("bsec_get_state failed: %d", bsec_status);
		return;
	}

	int err = settings_save_one("air_ctrl/bsec/state", bsec_state_blob, state_len);
	if (err) {
		LOG_ERR("Failed to save BSEC state (err %d)", err);
		return;
	}

	bsec_last_state_save_ns = timestamp_ns;
	LOG_INF("Saved BSEC state (%u bytes)", state_len);
}

static bsec_sensor_configuration_t virtual_sensors[] = {
	{BSEC_SAMPLE_RATE_LP, BSEC_OUTPUT_IAQ},
	{BSEC_SAMPLE_RATE_LP, BSEC_OUTPUT_STATIC_IAQ},
	{BSEC_SAMPLE_RATE_LP, BSEC_OUTPUT_CO2_EQUIVALENT},
	{BSEC_SAMPLE_RATE_LP, BSEC_OUTPUT_BREATH_VOC_EQUIVALENT},
	{BSEC_SAMPLE_RATE_LP, BSEC_OUTPUT_RAW_TEMPERATURE},
	{BSEC_SAMPLE_RATE_LP, BSEC_OUTPUT_RAW_PRESSURE},
	{BSEC_SAMPLE_RATE_LP, BSEC_OUTPUT_RAW_HUMIDITY},
	{BSEC_SAMPLE_RATE_LP, BSEC_OUTPUT_RAW_GAS},
	{BSEC_SAMPLE_RATE_LP, BSEC_OUTPUT_STABILIZATION_STATUS},
	{BSEC_SAMPLE_RATE_LP, BSEC_OUTPUT_RUN_IN_STATUS},
	{BSEC_SAMPLE_RATE_LP, BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE},
	{BSEC_SAMPLE_RATE_LP, BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY},
	{BSEC_SAMPLE_RATE_LP, BSEC_OUTPUT_GAS_PERCENTAGE},
};

#define NUM_VIRTUAL_SENSORS (sizeof(virtual_sensors) / sizeof(virtual_sensors[0]))

int64_t air_ctrl_sensor_get_timestamp_ns(void)
{
	return k_ticks_to_ns_near64(k_uptime_ticks());
}

static bool process_data(float temperature_c, float humidity_percent, float pressure_pa,
			 float gas_ohm, int64_t timestamp_ns, air_ctrl_sensor_data_t *output)
{
	bsec_input_t inputs[BSEC_MAX_PHYSICAL_SENSOR];
	bsec_output_t outputs[BSEC_NUMBER_OUTPUTS];
	uint8_t n_inputs = 0;
	uint8_t n_outputs = BSEC_NUMBER_OUTPUTS;
	bsec_library_return_t status;

	if (!bme_settings.trigger_measurement || bme_settings.op_mode == 0) {
		return false;
	}

	if (BSEC_CHECK_INPUT(bme_settings.process_data, BSEC_INPUT_TEMPERATURE)) {
		inputs[n_inputs].sensor_id = BSEC_INPUT_HEATSOURCE;
		inputs[n_inputs].signal = temp_offset;
		inputs[n_inputs].time_stamp = timestamp_ns;
		n_inputs++;

		inputs[n_inputs].sensor_id = BSEC_INPUT_TEMPERATURE;
		inputs[n_inputs].signal = temperature_c;
		inputs[n_inputs].time_stamp = timestamp_ns;
		n_inputs++;
	}

	if (BSEC_CHECK_INPUT(bme_settings.process_data, BSEC_INPUT_HUMIDITY)) {
		inputs[n_inputs].sensor_id = BSEC_INPUT_HUMIDITY;
		inputs[n_inputs].signal = humidity_percent;
		inputs[n_inputs].time_stamp = timestamp_ns;
		n_inputs++;
	}

	if (BSEC_CHECK_INPUT(bme_settings.process_data, BSEC_INPUT_PRESSURE)) {
		inputs[n_inputs].sensor_id = BSEC_INPUT_PRESSURE;
		inputs[n_inputs].signal = pressure_pa;
		inputs[n_inputs].time_stamp = timestamp_ns;
		n_inputs++;
	}

	if (BSEC_CHECK_INPUT(bme_settings.process_data, BSEC_INPUT_GASRESISTOR)) {
		inputs[n_inputs].sensor_id = BSEC_INPUT_GASRESISTOR;
		inputs[n_inputs].signal = gas_ohm;
		inputs[n_inputs].time_stamp = timestamp_ns;
		n_inputs++;
	}

	if (BSEC_CHECK_INPUT(bme_settings.process_data, BSEC_INPUT_PROFILE_PART)) {
		inputs[n_inputs].sensor_id = BSEC_INPUT_PROFILE_PART;
		inputs[n_inputs].signal = 0.0f;
		inputs[n_inputs].time_stamp = timestamp_ns;
		n_inputs++;
	}

	if (BSEC_CHECK_INPUT(bme_settings.process_data, BSEC_INPUT_DISABLE_BASELINE_TRACKER)) {
		inputs[n_inputs].sensor_id = BSEC_INPUT_DISABLE_BASELINE_TRACKER;
		inputs[n_inputs].signal = 0.0f;
		inputs[n_inputs].time_stamp = timestamp_ns;
		n_inputs++;
	}

	if (n_inputs == 0) {
		return false;
	}

	LOG_DBG("BSEC inputs: n=%d T=%.2f H=%.2f P=%.0f Gas=%.0f", n_inputs, (double)temperature_c,
		(double)humidity_percent, (double)pressure_pa, (double)gas_ohm);

	memset(outputs, 0, sizeof(outputs));
	status = bsec_do_steps(bsec_instance, inputs, n_inputs, outputs, &n_outputs);

	if (status != BSEC_OK) {
		LOG_ERR("bsec_do_steps failed: %d", status);
		return false;
	}

	LOG_DBG("bsec_do_steps: n_outputs=%d", n_outputs);

	memset(output, 0, sizeof(air_ctrl_sensor_data_t));

	for (uint8_t i = 0; i < n_outputs; i++) {
		switch (outputs[i].sensor_id) {
		case BSEC_OUTPUT_RAW_TEMPERATURE:
			output->raw_temperature = outputs[i].signal;
			break;
		case BSEC_OUTPUT_RAW_HUMIDITY:
			output->raw_humidity = outputs[i].signal;
			break;
		case BSEC_OUTPUT_RAW_PRESSURE:
			output->raw_pressure = outputs[i].signal;
			break;
		case BSEC_OUTPUT_RAW_GAS:
			output->raw_gas_resistance = outputs[i].signal;
			break;
		case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE:
			output->temperature = outputs[i].signal;
			break;
		case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY:
			output->humidity = outputs[i].signal;
			break;
		case BSEC_OUTPUT_IAQ:
			output->iaq = outputs[i].signal;
			output->iaq_accuracy = outputs[i].accuracy;
			break;
		case BSEC_OUTPUT_STATIC_IAQ:
			output->static_iaq = outputs[i].signal;
			break;
		case BSEC_OUTPUT_CO2_EQUIVALENT:
			output->co2_equivalent = outputs[i].signal;
			break;
		case BSEC_OUTPUT_BREATH_VOC_EQUIVALENT:
			output->breath_voc_equivalent = outputs[i].signal;
			break;
		case BSEC_OUTPUT_GAS_PERCENTAGE:
			output->gas_percentage = outputs[i].signal;
			break;
		case BSEC_OUTPUT_STABILIZATION_STATUS:
			output->stabilization_status = outputs[i].signal;
			break;
		case BSEC_OUTPUT_RUN_IN_STATUS:
			output->run_in_status = outputs[i].signal;
			break;
		default:
			break;
		}
		output->timestamp_ns = outputs[i].time_stamp;
	}

	return (n_outputs > 0);
}

static bool measure_and_process(air_ctrl_sensor_data_t *output)
{
	struct sensor_value temp;
	struct sensor_value pressure;
	struct sensor_value humidity;
	struct sensor_value gas;
	bool need_gas = BSEC_CHECK_INPUT(bme_settings.process_data, BSEC_INPUT_GASRESISTOR);

	if (sensor_sample_fetch(bme) < 0) {
		LOG_ERR("BME680 sample fetch failed");
		return false;
	}

	if (sensor_channel_get(bme, SENSOR_CHAN_AMBIENT_TEMP, &temp) ||
	    sensor_channel_get(bme, SENSOR_CHAN_PRESS, &pressure) ||
	    sensor_channel_get(bme, SENSOR_CHAN_HUMIDITY, &humidity)) {
		LOG_ERR("BME680 channel read failed");
		return false;
	}

	if (need_gas) {
		if (sensor_channel_get(bme, SENSOR_CHAN_GAS_RES, &gas)) {
			LOG_ERR("BME680 gas channel read failed");
			return false;
		}
	} else {
		gas.val1 = 0;
		gas.val2 = 0;
	}

	const int64_t timestamp_ns = air_ctrl_sensor_get_timestamp_ns();

	const float temperature_c = (float)sensor_value_to_double(&temp);
	const float humidity_percent = (float)sensor_value_to_double(&humidity);
	const float pressure_pa = (float)(sensor_value_to_double(&pressure) * 1000.0);
	const float gas_ohm = (float)gas.val1;

	return process_data(temperature_c, humidity_percent, pressure_pa, gas_ohm, timestamp_ns, output);
}

int air_ctrl_sensor_init(void)
{
	bsec_library_return_t bsec_status;
	bsec_version_t version;
	bsec_sensor_configuration_t required_sensors[BSEC_MAX_PHYSICAL_SENSOR];
	uint8_t n_required = BSEC_MAX_PHYSICAL_SENSOR;
	int err;

	LOG_INF("Initializing BSEC integration...");

	bsec_instance = (void *)bsec_mem;

	if (!device_is_ready(bme)) {
		LOG_ERR("BME680 not ready");
		return -ENODEV;
	}

	bsec_get_version(bsec_instance, &version);
	LOG_INF("BSEC version: %d.%d.%d.%d", version.major, version.minor, version.major_bugfix,
		version.minor_bugfix);

	bsec_status = bsec_init(bsec_instance);
	if (bsec_status != BSEC_OK) {
		LOG_ERR("bsec_init failed: %d", bsec_status);
		return -EIO;
	}
	LOG_INF("BSEC initialized");

	memset(bsec_work_buffer, 0, sizeof(bsec_work_buffer));
	bsec_status = bsec_set_configuration(bsec_instance, bsec_config_selectivity, BSEC_MAX_PROPERTY_BLOB_SIZE,
					 bsec_work_buffer, BSEC_MAX_WORKBUFFER_SIZE);
	if (bsec_status != BSEC_OK) {
		LOG_ERR("bsec_set_configuration failed: %d", bsec_status);
		return -EIO;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		err = settings_register(&bsec_settings_handler);
		if (err) {
			LOG_ERR("settings_register failed (err %d)", err);
		} else {
			bsec_state_blob_valid = false;
			bsec_state_blob_len = 0;
			err = settings_load_subtree("air_ctrl/bsec");
			if (err) {
				LOG_ERR("settings_load_subtree failed (err %d)", err);
			}

			if (bsec_state_blob_valid && bsec_state_blob_len > 0) {
				memset(bsec_work_buffer, 0, sizeof(bsec_work_buffer));
				bsec_status = bsec_set_state(bsec_instance, bsec_state_blob, bsec_state_blob_len,
							     bsec_work_buffer, sizeof(bsec_work_buffer));
				if (bsec_status != BSEC_OK) {
					LOG_ERR("bsec_set_state failed: %d", bsec_status);
				} else {
					LOG_INF("Restored BSEC state (%u bytes)", (uint32_t)bsec_state_blob_len);
				}
			}
		}
	}

	bsec_status = bsec_update_subscription(bsec_instance, virtual_sensors, NUM_VIRTUAL_SENSORS,
					 required_sensors, &n_required);
	if (bsec_status != BSEC_OK) {
		LOG_ERR("bsec_update_subscription failed: %d", bsec_status);
		return -EIO;
	}
	LOG_INF("BSEC subscribed to %d virtual sensors, requires %d physical sensors", NUM_VIRTUAL_SENSORS,
		n_required);

	memset(&bme_settings, 0, sizeof(bme_settings));
	bsec_last_state_save_ns = air_ctrl_sensor_get_timestamp_ns();

	LOG_INF("BSEC integration initialized successfully");

	return 0;
}

int64_t air_ctrl_sensor_get_next_call_ns(void)
{
	return bme_settings.next_call;
}

bool air_ctrl_sensor_run(air_ctrl_sensor_data_t *output)
{
	int64_t timestamp_ns;
	bsec_library_return_t bsec_status;

	if (output == NULL) {
		return false;
	}

	timestamp_ns = air_ctrl_sensor_get_timestamp_ns();

	if (timestamp_ns < bme_settings.next_call) {
		return false;
	}

	bsec_status = bsec_sensor_control(bsec_instance, timestamp_ns, &bme_settings);
	if (bsec_status != BSEC_OK) {
		LOG_ERR("bsec_sensor_control failed: %d", bsec_status);
		return false;
	}

	if (bme_settings.op_mode == 0) {
		return false;
	}

	if (bme_settings.trigger_measurement) {
		bool ok = measure_and_process(output);
		if (ok) {
			bsec_state_save_if_needed(timestamp_ns);
		}
		return ok;
	}

	return false;
}
