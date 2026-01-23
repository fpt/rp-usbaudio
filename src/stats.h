// SPDX-License-Identifier: MIT
// Statistics module for buffer/DMA monitoring via CDC

#ifndef _STATS_H_
#define _STATS_H_

#include <stdint.h>

// Initialize statistics module
void stats_init(void);

// Statistics task - call from main loop
// Outputs stats via CDC every 1 second
void stats_task(void);

// Record an underrun event
void stats_record_underrun(void);

// Record samples written
void stats_record_samples_written(uint32_t count);

#endif /* _STATS_H_ */
