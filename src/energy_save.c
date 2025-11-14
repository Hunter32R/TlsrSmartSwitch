/*
 * energy_save.c
 *
 *  Created on: 13 нояб. 2025 г.
 *      Author: pvvx
 */
#include "app_main.h"
#include "energy_save.h"
#include "battery.h"

// Address save energy count to Flash
typedef struct {
    uint32_t flash_addr_save;          /* flash page address saved */
    uint32_t flash_addr_start;         /* flash page address start */
    uint32_t flash_addr_end;           /* flash page address end   */
} energy_cons_t;

static energy_cons_t energy_cons; // Address save energy count to Flash

bool new_save_data = false;      // flag

// Save energy count in Flash
save_data_t save_data;

// Clearing USER_DATA
static void clear_user_data(void) {

    uint32_t flash_addr = energy_cons.flash_addr_start;
    while(flash_addr < energy_cons.flash_addr_end) {
        flash_erase_sector(flash_addr);
        flash_addr += FLASH_SECTOR_SIZE;
    }
}

// Saving the energy meter
static void save_dataCb(void *args) {

	battery_detect(0);

    if (save_data.xor_energy != ~save_data.energy) {

        save_data.xor_energy = ~save_data.energy;

        light_blink_start(1, 250, 250);

        if (energy_cons.flash_addr_save == energy_cons.flash_addr_end) {
            energy_cons.flash_addr_save = energy_cons.flash_addr_start;
        }
        if ((energy_cons.flash_addr_save & (FLASH_SECTOR_SIZE-1)) == 0) {
            flash_erase(energy_cons.flash_addr_save);
        }

        flash_write(energy_cons.flash_addr_save, sizeof(save_data), (uint8_t*)&save_data);

        energy_cons.flash_addr_save += sizeof(save_data);
    }
}

// Initializing USER_DATA storage addresses in Flash memory
static void init_save_addr_drv(void) {

	u32 mid = flash_read_mid();
	mid >>= 16;
	mid &= 0xff;
	if(mid >= FLASH_SIZE_1M) {
        energy_cons.flash_addr_start = BEGIN_USER_DATA_F1M;
        energy_cons.flash_addr_end = END_USER_DATA_F1M;
    } else {
        energy_cons.flash_addr_start = BEGIN_USER_DATA_F512K;
        energy_cons.flash_addr_end = END_USER_DATA_F512K;
    }
    energy_cons.flash_addr_save = energy_cons.flash_addr_start;
    save_data.energy = 0;
    save_data.xor_energy = -1;
    g_zcl_seAttrs.cur_sum_delivered = 0;
}


// Read & check valid blk (Save Energy)
static int check_saved_blk(uint32_t flash_addr, save_data_t * pdata) {
    if(flash_addr >= energy_cons.flash_addr_end)
        flash_addr = energy_cons.flash_addr_start;
    flash_read_page(flash_addr, sizeof(save_data_t), (uint8_t*)pdata);
    if(pdata->energy == -1 && pdata->xor_energy == -1) {
        return -1;
    } else if(pdata->energy == ~pdata->xor_energy) {
        return 0;
    }
    return 1;
}

// Start initialize (Save Energy)
int energy_restore(void) {
    int ret;
    uint32_t flash_addr;

    save_data_t tst_data;

    init_save_addr_drv();

    flash_addr = energy_cons.flash_addr_start;

    while(flash_addr < energy_cons.flash_addr_end) {
        flash_addr &= ~(FLASH_SECTOR_SIZE-1);
        ret = check_saved_blk(flash_addr, &tst_data);
        if(ret < 0) {
            flash_addr += FLASH_SECTOR_SIZE;
            continue;
        }
        if(ret == 0) {
            memcpy(&save_data, &tst_data, sizeof(save_data)); // save_data = tst_data;
            if((check_saved_blk(flash_addr + FLASH_SECTOR_SIZE, &tst_data) == 0)
            && tst_data.energy > save_data.energy) {
                flash_addr += FLASH_SECTOR_SIZE;
                continue;
            }
        }
        flash_addr += sizeof(tst_data);
        while(flash_addr < energy_cons.flash_addr_end) {
            ret = check_saved_blk(flash_addr, &tst_data);
            if(ret == 0) {
                if(tst_data.energy > save_data.energy) {
                    memcpy(&save_data, &tst_data, sizeof(save_data)); // save_data = tst_data;
                } else {
                    flash_addr &= ~(FLASH_SECTOR_SIZE-1);
                    energy_cons.flash_addr_save = flash_addr;
                    g_zcl_seAttrs.cur_sum_delivered = save_data.energy;
                    return 0;
                }
            } else if(ret < 0) {
                energy_cons.flash_addr_save = flash_addr;
                g_zcl_seAttrs.cur_sum_delivered = save_data.energy;
                return 0;
            }
            flash_addr += sizeof(tst_data);
        }
    }
    return 1;
}

// Clear all USER_DATA (Save Energy)
void energy_remove(void) {
    init_save_addr_drv();
    clear_user_data();
}

// Step 1 minutes (Save Energy)
int32_t energy_timerCb(void *args) {

    if (new_save_data) {
        new_save_data = false;
        TL_SCHEDULE_TASK(save_dataCb, NULL);
    }
    return 0;
}


