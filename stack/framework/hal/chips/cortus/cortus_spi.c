/* * OSS-7 - An opensource implementation of the DASH7 Alliance Protocol for ultra
 * lowpower wireless sensor communication
 *
 * Copyright 2015 University of Antwerp
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*! \file cortus_spi.c
 *
 *  \author philippe.nunes@cortus.com
 *
 */


#include <stdbool.h>
#include <stdint.h>

#include "debug.h"
#include "hwspi.h"
#include "hwgpio.h"

#include "cortus_gpio.h"

#include "machine/spi.h"
#include "machine/gpio.h"

#if defined(FRAMEWORK_LOG_ENABLED)
#include <stdio.h>
#endif

#define MAX_SPI_SLAVE_HANDLES 2

struct spi_slave_handle {
  spi_handle_t* spi;
  pin_id_t      cs;
  bool          cs_is_active_low;
  bool          selected;
};

struct spi_handle {
  SPI* spi_sfradr;
  spi_slave_handle_t *slave;
  bool active;
};

static spi_handle_t handle[SPI_COUNT];
spi_slave_handle_t slave_handle[MAX_SPI_SLAVE_HANDLES];
uint8_t next_spi_slave_handle = 0;

#define TIMING_ADJ


spi_handle_t* spi_init(uint8_t idx, uint32_t baudrate, uint8_t databits, bool msbf)
{

    // assert what is supported by cortus
    assert(databits == 8);
    assert(idx < SPI_COUNT);

    // configure new handle
    if (idx == 0)
        handle[idx].spi_sfradr = ((SPI *)SFRADR_SPI);
    else
        handle[idx].spi_sfradr = ((SPI *)SFRADR_SPI2);

    handle[idx].spi_sfradr->divider = baudrate;
    handle[idx].spi_sfradr->master = 1;
    handle[idx].spi_sfradr->config = 0x0 | ((0x1&(~msbf)) << 2);
    handle[idx].spi_sfradr->selclk = 0; // 0:50MHz, 1:25MHz, 2:12.5MHZ, 3:3.125MHz

   return &handle[idx];
}

static void ensure_slaves_deselected(spi_handle_t* spi) {
  // make sure CS line is high for active low slave and vice versa
    if(spi->slave->cs_is_active_low)
      hw_gpio_set(spi->slave->cs);
    else
      hw_gpio_clr(spi->slave->cs);
}

void spi_enable(spi_handle_t* spi) {
    // already active?
    if(spi->active)
        return;

    // bringing SPI bus up
    ensure_slaves_deselected(spi);

    spi->spi_sfradr->clk_en = 1;
    spi->active = true;
}

void spi_disable(spi_handle_t* spi) {

    while ((spi->spi_sfradr->bus_active & 0x1)) {}
    spi->spi_sfradr->clk_en = 0;

    ensure_slaves_deselected(spi);
    spi->active = false;
}

spi_slave_handle_t* spi_init_slave(spi_handle_t* spi, pin_id_t cs_pin, bool cs_is_active_low) {

    assert(next_spi_slave_handle < MAX_SPI_SLAVE_HANDLES);
    bool initial_level = spi->active > 0 && cs_is_active_low;
    hw_gpio_configure_pin(cs_pin, false, gpioModePushPull, initial_level);

    slave_handle[next_spi_slave_handle] = (spi_slave_handle_t){
          .spi              = spi,
          .cs               = cs_pin,
          .cs_is_active_low = cs_is_active_low,
          .selected         = false
    };

    // add slave to spi for back-reference
    spi->slave = &slave_handle[next_spi_slave_handle];
    next_spi_slave_handle++;
    return spi->slave;
}

void spi_select(spi_slave_handle_t* slave) {

    spi_enable(slave->spi);

    if(slave->cs_is_active_low)
        hw_gpio_clr(slave->cs);
    else
        hw_gpio_set(slave->cs);
}

void spi_deselect(spi_slave_handle_t* slave) {

#ifdef TIMING_ADJ
  volatile uint32_t i = 0;
  while (i!=7)   i++;
#endif

    if(slave->cs_is_active_low)
        hw_gpio_set(slave->cs);
    else
        hw_gpio_clr(slave->cs);

    spi_disable(slave->spi);

#ifdef TIMING_ADJ
  i = 0;
  while (i!=100)   i++;
#endif
}

unsigned char spi_exchange_byte(spi_slave_handle_t* slave, unsigned char data) {
#ifdef TIMING_ADJ
  volatile uint32_t i = 0;
  while (i!=3)   i++;
#endif
    SPI* spi = slave->spi->spi_sfradr;

    while ((spi->tx_status & 0x1) != 1) {} // wait until tx fifo is available
    spi->tx_data = data;

    while ((spi->rx_status & 0x1) != 1) {} // wait until rx fifo is available
    return (uint8_t)spi->rx_data;
}

void spi_send_byte_with_control(spi_slave_handle_t* slave, uint16_t data) {
   // 9-bit transmission is not supported.
#if defined(FRAMEWORK_LOG_ENABLED)
   DPRINT("CORTUS: spi_send_byte_with_control is called but not supported.\n");
#endif
}

void spi_exchange_bytes(spi_slave_handle_t* slave,
                        uint8_t* TxData, uint8_t* RxData, size_t length)
{
    uint16_t i = 0;
    if( RxData != NULL && TxData != NULL ) {           // two way transmition
        while( i < length ) {
            RxData[i] = spi_exchange_byte(slave, TxData[i]);
            i++;
        }
    } else if( RxData == NULL && TxData != NULL ) {    // send only
        while( i < length ) {
            spi_exchange_byte(slave, TxData[i]);
            i++;
        }
    } else if( RxData != NULL && TxData == NULL ) {   // receive only
        while( i < length ) {
            RxData[i] = spi_exchange_byte(slave, 0);
            i++;
        }
    }
}
