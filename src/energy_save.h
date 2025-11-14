/*
 * energy_save.h
 *
 *  Created on: 13 нояб. 2025 г.
 *      Author: pvvx
 */

#ifndef SRC_ENERGY_SAVE_H_
#define SRC_ENERGY_SAVE_H_

// Save energy count in Flash
typedef struct {
    uint64_t energy;
    uint64_t xor_energy;
} save_data_t;

extern save_data_t save_data; // Save energy count in Flash
extern bool new_save_data;    // flag

int energy_restore(void); // Start initialize (Save Energy)
void energy_remove(void); // Clear all USER_DATA (Save Energy)
int32_t energy_timerCb(void *args); // Step 1 minutes (Save Energy)

#endif /* SRC_ENERGY_SAVE_H_ */
