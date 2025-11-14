#include "app_main.h"
#if USE_BL0942
#include "battery.h"
#include "energy_save.h"

extern u64 mul32x32_64(u32 a, u32 b); // hard function (in div_mod.S)

//--------- Work Data --------

#define HEAD            0x55

// rx buffer BL0942
typedef struct __attribute__((packed)) {
    uint8_t     head;
    uint32_t    i_rms       :24;
    uint32_t    v_rms       :24;
    uint32_t    i_fast_rms  :24;
    int32_t     watt        :24;
    uint32_t    cf_cnt      :24;
    uint16_t    freq;
    uint8_t     resv1;
    uint8_t     status;
    uint8_t     resv2;
    uint8_t     resv3;
    uint8_t     crc;
} app_monitoring_t;

// UART RX DMA buffer
typedef struct __attribute__((packed)) {
    volatile u32 dma_len;
    u8 data[32];
} dma_uart_rx_buf_t;

dma_uart_rx_buf_t urxb; // UART RX DMA buffer

static uint16_t tik_max_current; // count max current, step 8 sec
static uint8_t first_start = true;     // flag

//--------- Data for calculating BL0942 --------

// This REF get from https://github.com/esphome/esphome/blob/dev/esphome/components/bl0942/bl0942.h
#define BL0942_POWER_REF        28149691 // 2pow24/0.596  // x1000: x1000: 0..327.67W, x100 32.767..327.67W, x10: 327.67..3276.7W
#define BL0942_VOLTAGE_REF      26927694 // 2pow32/159.5 // 158.7335944299 // x100: 0..655.35V
#define BL0942_CURRENT_REF      17096883 // 2pow32/251.21346469622 // 305978/1.218, x1000: 0..65.535A
#define BL0942_ENERGY_REF       129968911 // 2pow32/33.0461127328 // x100
#define BL0942_FREQ_REF         100000000 // x100

const sensor_pwr_coef_t sensor_pwr_coef_def = {
	    .current = BL0942_CURRENT_REF,
	    .voltage = BL0942_VOLTAGE_REF,
	    .power = BL0942_POWER_REF,
	    .energy = BL0942_ENERGY_REF,
	    .freq = BL0942_FREQ_REF
};

sensor_pwr_coef_t sensor_pwr_coef;
sensor_pwr_coef_t sensor_pwr_coef_saved;

// Remainder from the previous division
typedef struct {
    uint32_t current;
    uint32_t voltage;
    uint32_t power;
    uint32_t energy;
    uint32_t old_energy; // for calculating the energy meter
} old_fract_t;

static old_fract_t old_fract; // Remainder from the previous division

//--------- Loading/Saving Sensor Coefficients --------

nv_sts_t load_config_sensor(void) {
#if NV_ENABLE
	nv_sts_t ret = nv_flashReadNew(1, NV_MODULE_APP,  NV_ITEM_APP_CFG_SENSOR, sizeof(sensor_pwr_coef), (uint8_t*)&sensor_pwr_coef);
	if(ret !=  NV_SUCC) {
		memcpy(&sensor_pwr_coef, &sensor_pwr_coef_def, sizeof(sensor_pwr_coef));
	}
	memcpy(&sensor_pwr_coef_saved, &sensor_pwr_coef, sizeof(sensor_pwr_coef));
	return ret;
#else
    return NV_ENABLE_PROTECT_ERROR;
#endif
}

nv_sts_t save_config_sensor(void) {
#if NV_ENABLE
	nv_sts_t ret = NV_SUCC;
	if(memcmp(&sensor_pwr_coef_saved, &sensor_pwr_coef, sizeof(sensor_pwr_coef))) {
		memcpy(&sensor_pwr_coef_saved, &sensor_pwr_coef, sizeof(sensor_pwr_coef));
		ret = nv_flashWriteNew(1, NV_MODULE_APP,  NV_ITEM_APP_CFG_SENSOR, sizeof(sensor_pwr_coef), (uint8_t*)&sensor_pwr_coef);
	}
    return ret;
#else
    return NV_ENABLE_PROTECT_ERROR;
#endif
}

//--------------------------------

// Chk BL0942
static uint8_t checksum(uint8_t *data, uint16_t length) {
    uint8_t crc8 = 0;
    for(uint8_t i = 0; i < (length - 1); i++) {
        crc8 += data[i];
    }
    crc8 += 0x58;
    return ~crc8;
}

//-----------------------
// UART 4800 BAUD
#define UART_BAUD 4800 // bps
#if CLOCK_SYS_CLOCK_HZ == 16000000
#define uartCLKdiv 475 // 16000000/(6+1)/(475+1) = 4801.92
#define bwpc 6
#elif CLOCK_SYS_CLOCK_HZ == 24000000
#define uartCLKdiv 999 // 24000000/(4+1)/(999+1) = 4800
#define bwpc 4
#elif CLOCK_SYS_CLOCK_HZ == 32000000
#define uartCLKdiv 832 // 32000000/(7+1)/(832+1) = 4796.163
#define bwpc 7
#elif CLOCK_SYS_CLOCK_HZ == 48000000
#define uartCLKdiv 999 // 48000000/(9+1)/(999+1) = 4800
#define bwpc 9
#endif

// Initializing UART for BL0942
void app_sensor_init(void) {

    drv_uart_pin_set(GPIO_UART_TX, GPIO_UART_RX);

    reg_clk_en0 |= FLD_CLK0_UART_EN; // Enable CLK UART

    // reg_clk_en1 |= FLD_CLK1_DMA_EN;

    uart_reset();

    uart_recbuff_init((unsigned char *)&urxb, sizeof(urxb.data));

    reg_dma0_addr = (uint16_t)((uint32_t)(&urxb)); //set receive buffer address
    reg_dma0_size = sizeof(urxb.data)>>4; //set receive buffer size
    reg_dma0_mode = FLD_DMA_WR_MEM;   //set DMA 0 mode to 0x01 for receive
    reg_dma0_addrHi = 0x04;

    // set reg_uart_clk_div/reg_uart_ctrl0
    REG_ADDR32(0x094) = MASK_VAL(FLD_UART_CLK_DIV, uartCLKdiv, FLD_UART_CLK_DIV_EN, 1)
        | ((MASK_VAL(FLD_UART_BPWC, bwpc) | (FLD_UART_RX_DMA_EN)) << 16) // set bit width, enable UART RX DMA mode
        | ((MASK_VAL(FLD_UART_CTRL1_STOP_BIT, 0)) << 24) // 00: 1 bit, 01: 1.5bit 1x: 2bits;
        ;
    reg_dma_chn_en |= FLD_DMA_CHN_UART_RX;

    TL_ZB_TIMER_SCHEDULE(app_monitoringCb, NULL, TIMEOUT_1SEC);
    TL_ZB_TIMER_SCHEDULE(energy_timerCb, NULL, TIMEOUT_1MIN);
}


// Task BL0942
void monitoring_handler(void) {

    int32_t  power;
    uint32_t current, voltage, energy, freq;
    uint64_t tmp;

    app_monitoring_t *pkt = (app_monitoring_t*)urxb.data;

    if(reg_dma_rx_rdy0 & FLD_DMA_IRQ_UART_RX) { // new data ?
        reg_uart_status0 |= FLD_UART_CLEAR_RX_FLAG | FLD_UART_RX_ERR_FLAG;
        if(urxb.dma_len == sizeof(app_monitoring_t)
          && pkt->head == HEAD
          && checksum((uint8_t *)pkt, sizeof(app_monitoring_t)) == pkt->crc) {

        	if (first_start) {
                first_start = false;
                reg_dma_rx_rdy0 = FLD_DMA_IRQ_UART_RX;
            } else {

                current = pkt->i_rms;
                voltage = pkt->v_rms;
                power = pkt->watt;
                freq = pkt->freq;
                energy = pkt->cf_cnt;

                reg_dma_rx_rdy0 = FLD_DMA_IRQ_UART_RX;

                tmp = mul32x32_64(current, sensor_pwr_coef.current);
                tmp += old_fract.current;
                old_fract.current = tmp & 0xffffffff;
                current = tmp >> 32;
                g_zcl_msAttrs.current = (uint16_t)current;

                tmp = mul32x32_64(voltage, sensor_pwr_coef.voltage);
                tmp += old_fract.voltage;
                old_fract.voltage = tmp & 0xffffffff;
                voltage = tmp >> 32;
                g_zcl_msAttrs.voltage = (uint16_t)voltage;

                // power x1000 0..3276.750W
                if(power < 0)
                    power =  -power;

                tmp = mul32x32_64(power, sensor_pwr_coef.power);
                tmp += old_fract.power;
                old_fract.power = tmp & 0xffffff;
                power = tmp >> 24;

                if(power > 327670) {
                    // x10: 327.6..3276.7W
                    power += 50;
                    power /= 100;
                    g_zcl_msAttrs.power_divisor = 10;
                } else if(power > 32767) {
                    // x100 32.767..327.67W
                    power += 5;
                    power /= 10;
                    g_zcl_msAttrs.power_divisor = 100;
                } else {
                    // x1000: x1000: 0..32.767W
                    g_zcl_msAttrs.power_divisor = 1000;
                }
                g_zcl_msAttrs.power = (int16_t)power;

                freq = (sensor_pwr_coef.freq + (freq >> 1))/ freq;

                g_zcl_msAttrs.freq = (uint16_t)freq;

                freq =  energy;
            	energy -= old_fract.old_energy;
                old_fract.old_energy = freq;

                if(energy) {

                	tmp = mul32x32_64(energy, sensor_pwr_coef.energy);
                    tmp += old_fract.energy;
                    old_fract.energy = tmp & 0xffffffff;
                    energy = tmp >> 32;

                	if(energy) {
                		save_data.energy += energy;
                		g_zcl_seAttrs.cur_sum_delivered = save_data.energy;
                		new_save_data = true; // energy_save();
                	}
                }

                if(config_min_max.min_voltage
                && voltage < config_min_max.min_voltage) {
            		cmdOnOff_off(APP_ENDPOINT1);
                	return;
                }
                if(config_min_max.max_voltage
                      && voltage > config_min_max.max_voltage) {
            		cmdOnOff_off(APP_ENDPOINT1);
                	return;
                }
                if(config_min_max.max_current && config_min_max.time_max_current) {
                    if(power > config_min_max.max_current) {
                    	tik_max_current += 8;
                    	if(tik_max_current >= config_min_max.time_max_current) {
                    		tik_max_current = 0xffff;
                    		cmdOnOff_off(APP_ENDPOINT1);
                        	return;
                    	}
                    } else {
                    	tik_max_current = 0;
                    }
                }
            }
        }
    } else {
        reg_dma_rx_rdy0 = FLD_DMA_IRQ_UART_RX;
    }
}

// Step 1 sec
int32_t app_monitoringCb(void *arg) {

    REG_ADDR16(0x90) = 0xAA58; // Send cmd: "Read full packet"

    return 0;
}


#endif // USE_BL0942
