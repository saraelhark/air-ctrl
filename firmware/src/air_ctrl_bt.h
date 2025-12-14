#ifndef AIR_CTRL_BT_H
#define AIR_CTRL_BT_H

#include <stdbool.h>

#include "air_ctrl_sensor.h"

int air_ctrl_bt_init(void);

bool air_ctrl_bt_is_connected(void);

int air_ctrl_bt_notify_sensor_data(const air_ctrl_sensor_data_t *data);

#endif /* AIR_CTRL_BT_H */
