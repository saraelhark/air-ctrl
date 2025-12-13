#ifndef AIR_CTRL_SENSOR_H_
#define AIR_CTRL_SENSOR_H_

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int64_t timestamp_ns;

    float raw_temperature;
    float raw_humidity;
    float raw_pressure;
    float raw_gas_resistance;

    float temperature;
    float humidity;

    float iaq;
    uint8_t iaq_accuracy;
    float static_iaq;
    float co2_equivalent;
    float breath_voc_equivalent;
    float gas_percentage;

    float stabilization_status;
    float run_in_status;
} air_ctrl_sensor_data_t;

int air_ctrl_sensor_init(void);

bool air_ctrl_sensor_run(air_ctrl_sensor_data_t *output);

int64_t air_ctrl_sensor_get_next_call_ns(void);

int64_t air_ctrl_sensor_get_timestamp_ns(void);

#endif /* AIR_CTRL_SENSOR_H_ */
