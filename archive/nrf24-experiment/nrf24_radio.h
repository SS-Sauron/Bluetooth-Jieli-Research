#ifndef NRF24_RADIO_H
#define NRF24_RADIO_H

#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"

#ifdef CONFIG_SCANNER_NRF24

bool nrf24_radio_init(void);
void nrf24_spectrum_scan(uint8_t rssi[126]);
void nrf24_spectrum_task(void *arg);
void nrf24_jammer_start(void);
void nrf24_jammer_stop(void);

#else

static inline bool nrf24_radio_init(void)
{
    return false;
}

static inline void nrf24_spectrum_scan(uint8_t rssi[126])
{
    (void)rssi;
}

static inline void nrf24_spectrum_task(void *arg)
{
    (void)arg;
}

static inline void nrf24_jammer_start(void)
{
}

static inline void nrf24_jammer_stop(void)
{
}

#endif /* CONFIG_SCANNER_NRF24 */

#endif /* NRF24_RADIO_H */
