#ifndef SRC_INCLUDE_APP_SENSOR_H_
#define SRC_INCLUDE_APP_SENSOR_H_



// Sensor coefficients
typedef struct {
    uint32_t current;
    uint32_t voltage;
    uint32_t power;
    uint32_t energy;
#if USE_BL0942
    uint32_t freq;
#endif
} sensor_pwr_coef_t;

extern sensor_pwr_coef_t sensor_pwr_coef;

int32_t app_monitoringCb(void *arg);
int32_t energy_timerCb(void *args);
void monitoring_handler(void);
int energy_restore(void);
void energy_remove(void);

void app_sensor_init(void);
nv_sts_t load_config_sensor(void);
nv_sts_t save_config_sensor(void);

#endif /* SRC_INCLUDE_APP_SENSOR_H_ */
