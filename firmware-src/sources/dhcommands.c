/*
 * dhcommands.cpp
 *
 * Copyright 2015 DeviceHive
 *
 * Author: Nikolay Khabarov
 *
 * Description: Module for executing server command
 *
 */

#include <ets_sys.h>
#include <osapi.h>
#include <os_type.h>
#include "dhcommands.h"
#include "dhsender_queue.h"
#include "dhgpio.h"
#include "dhadc.h"
#include "dhnotification.h"
#include "snprintf.h"
#include "dhcommand_parser.h"
#include "dhterminal.h"
#include "dhuart.h"
#include "dhi2c.h"
#include "dhspi.h"
#include "dhonewire.h"
#include "dhsender.h"
#include "dhsender_enums.h"

#define GPIONOTIFICATION_MIN_TIMEOUT_MS 50
#define ADCNOTIFICATION_MIN_TIMEOUT_MS 250

LOCAL void ICACHE_FLASH_ATTR responce_ok(unsigned int id) {
	dhsender_response(id, DHSTATUS_OK, RDT_CONST_STRING, "");
}

LOCAL const char * ICACHE_FLASH_ATTR responce_error(unsigned int id, const char *str) {
	if(str)
		dhsender_response(id, DHSTATUS_ERROR, RDT_CONST_STRING, str);
	return str;
}

LOCAL int ICACHE_FLASH_ATTR onewire_init(unsigned int id, ALLOWED_FIELDS fields, gpio_command_params *parse_pins) {
	if(fields & AF_PIN) {
		if(dhonewire_set_pin(parse_pins->pin) == 0) {
			responce_error(id, "Wrong onewire pin");
			return 1;
		}
	}
	return 0;
}

LOCAL int ICACHE_FLASH_ATTR spi_init(unsigned int id, ALLOWED_FIELDS fields, gpio_command_params *parse_pins) {
	if(fields & AF_CS) {
		if(dhspi_set_cs_pin(parse_pins->CS) == 0) {
			responce_error(id, "Wrong CS pin");
			return 1;
		}
	}
	if(fields & AF_SPIMODE) {
		if(dhspi_set_mode(parse_pins->spi_mode) == 0) {
			responce_error(id, "Wrong SPI mode");
			return 1;
		}
	}
	return 0;
}

LOCAL char *ICACHE_FLASH_ATTR i2c_status_tochar(DHI2C_STATUS status) {
	switch(status) {
		case DHI2C_NOACK:
			return "ACK response not detected";
		case DHI2C_WRONG_PARAMETERS:
			return "Wrong I2C parameters";
		case DHI2C_TIMEOUT:
			return "Can not set bus";
	}
	return 0;
}

LOCAL int ICACHE_FLASH_ATTR i2c_init(unsigned int id, ALLOWED_FIELDS fields, gpio_command_params *parse_pins) {
	if((fields & AF_ADDRESS) == 0) {
		responce_error(id, "Address not specified");
		return 1;
	}
	int init = ((fields & AF_SDA) ? 1 : 0) + ((fields & AF_SCL) ? 1 : 0);
	if(init == 2) {
		char *res = i2c_status_tochar(dhi2c_init(parse_pins->SDA, parse_pins->SCL));
		if(responce_error(id, res))
			return 1;
	} else if(init == 1) {
		responce_error(id, "Only one pin specified");
		return 1;
	} else {
		dhi2c_reinit();
	}
	return 0;
}

LOCAL int ICACHE_FLASH_ATTR uart_init(unsigned int id, ALLOWED_FIELDS fields, gpio_command_params *parse_pins, unsigned int isint) {
	if(fields & AF_UARTMODE) {
		if(parse_pins->uart_speed == 0 && isint) {
			dhuart_enable_buf_interrupt(0);
			responce_ok(id);
			return 1;
		} else if(!dhuart_init(parse_pins->uart_speed, parse_pins->uart_bits, parse_pins->uart_partity, parse_pins->uart_stopbits)) {
			responce_error(id, "Wrong UART mode");
			return 1;
		}
	}
	return 0;
}

void ICACHE_FLASH_ATTR dhcommands_do(unsigned int id, const char *command, const char *params, unsigned int paramslen) {
	gpio_command_params parse_pins;
	ALLOWED_FIELDS fields = 0;
	char *parse_res;
	unsigned int check;
	if( os_strcmp(command, "gpio/write") == 0 ) {
		parse_res = parse_params_pins_set(params, paramslen, &parse_pins, DHGPIO_SUITABLE_PINS, 0, AF_SET | AF_CLEAR, &fields);
		if (parse_res)
			responce_error(id, parse_res);
		else if ( (fields & (AF_SET | AF_CLEAR)) == 0)
			responce_error(id, "Dummy request");
		else if(dhgpio_write(parse_pins.pins_to_set, parse_pins.pins_to_clear))
			responce_ok(id);
		else
			responce_error(id, "Unsuitable pin");
	} else if( os_strcmp(command, "gpio/read") == 0 ) {
		int init = 1;
		if(paramslen) {
			parse_res = parse_params_pins_set(params, paramslen, &parse_pins, DHGPIO_SUITABLE_PINS, 0, AF_INIT | AF_PULLUP | AF_NOPULLUP, &fields);
			if (parse_res) {
				responce_error(id, parse_res);
				init = 0;
				return;
			} else {
				init = dhgpio_initialize(parse_pins.pins_to_init, parse_pins.pins_to_pullup, parse_pins.pins_to_nopull);
			}
		}
		if (init) {
			dhsender_response(id, DHSTATUS_OK, RDT_GPIO, 0, dhgpio_read(), system_get_time());
		} else {
			responce_error(id, "Wrong initialization parameters");
		}
	} else if( os_strcmp(command, "gpio/int") == 0 ) {
		parse_res = parse_params_pins_set(params, paramslen, &parse_pins, DHGPIO_SUITABLE_PINS, dhgpio_get_timeout(), AF_DISABLE | AF_RISING | AF_FALLING | AF_BOTH | AF_TIMEOUT, &fields);
		if (parse_res)
			responce_error(id, parse_res);
		else if (fields == 0)
			responce_error(id, "Wrong action");
		else if(parse_pins.timeout < GPIONOTIFICATION_MIN_TIMEOUT_MS || parse_pins.timeout > 0x7fffff)
			responce_error(id, "Timeout is wrong");
		else if(dhgpio_int(parse_pins.pins_to_disable, parse_pins.pins_to_rising, parse_pins.pins_to_falling, \
				parse_pins.pins_to_both, parse_pins.timeout))
			responce_ok(id);
		else
			responce_error(id, "Unsuitable pin");
	} else if( os_strcmp(command, "adc/read") == 0) {
		if(paramslen) {
			parse_res = parse_params_pins_set(params, paramslen, &parse_pins, DHADC_SUITABLE_PINS, 0, AF_READ, &fields);
			if (parse_res) {
				responce_error(id, parse_res);
				return;
			} else if (parse_pins.pins_to_read != DHADC_SUITABLE_PINS) {
				responce_error(id, "Unknown ADC channel");
				return;
			}
		}
		dhsender_response(id, DHSTATUS_OK, RDT_FLOAT, dhadc_get_value());
	} else if( os_strcmp(command, "adc/int") == 0) {
		if(paramslen) {
			parse_res = parse_params_pins_set(params, paramslen, &parse_pins, DHADC_SUITABLE_PINS, 0, AF_VALUES, &fields);
			if (responce_error(id, parse_res)) {
				return;
			} else if(parse_pins.pin_value_readed != 0x1) {
				responce_error(id, "Wrong adc channel");
				return;
			} else if ((parse_pins.pin_value[0] < ADCNOTIFICATION_MIN_TIMEOUT_MS && parse_pins.pin_value[0] != 0) || parse_pins.pin_value[0] > 0x7fffff) {
				responce_error(id, "Wrong period");
				return;
			} else {
				dhadc_loop(parse_pins.pin_value[0]);
				responce_ok(id);
				return;
			}
		}
		responce_error(id, "Wrong parameters");
	} else if( os_strcmp(command, "pwm/control") == 0 ) {
		parse_res = parse_params_pins_set(params, paramslen, &parse_pins, DHADC_SUITABLE_PINS, 0, AF_VALUES | AF_PERIOD | AF_COUNT, &fields);
		if (responce_error(id, parse_res))
			return;
		if(dhpwm_set_pwm(&parse_pins.pin_value, parse_pins.pin_value_readed, (fields & AF_PERIOD) ? parse_pins.periodus : dhpwm_get_period_us(),  parse_pins.count))
			responce_ok(id);
		else
			responce_error(id, "Wrong parameters");
	} else if( os_strcmp(command, "uart/write") == 0 ) {
		parse_res = parse_params_pins_set(params, paramslen, &parse_pins, DHADC_SUITABLE_PINS, 0, AF_UARTMODE | AF_DATA, &fields);
		if (responce_error(id, parse_res))
			return;
		if(uart_init(id, fields, &parse_pins, 0))
			return;
		dhuart_set_mode(DUM_PER_BUF, dhuart_get_timeout());
		dhuart_send_buf(parse_pins.data, parse_pins.data_len);
		responce_ok(id);
	} else if( os_strcmp(command, "uart/int") == 0 ) {
		if(paramslen) {
			parse_res = parse_params_pins_set(params, paramslen, &parse_pins, DHADC_SUITABLE_PINS, dhuart_get_timeout(), AF_UARTMODE | AF_TIMEOUT, &fields);
			if (responce_error(id, parse_res))
				return;
			if(uart_init(id, fields, &parse_pins, 1))
				return;
		}
		dhuart_set_mode(DUM_PER_BUF, (fields & AF_TIMEOUT) ? parse_pins.timeout : dhuart_get_timeout());
		dhuart_enable_buf_interrupt(1);
		responce_ok(id);
	} else if( os_strcmp(command, "uart/terminal") == 0 ) {
		if(paramslen) {
			responce_error(id, "Command does not have parameters");
			return;
		}
		dhterminal_init();
		responce_ok(id);
	} else if( os_strcmp(command, "i2c/master/read") == 0 ) {
		parse_res = parse_params_pins_set(params, paramslen, &parse_pins, DHADC_SUITABLE_PINS, 0, AF_SDA | AF_SCL | AF_DATA | AF_ADDRESS | AF_COUNT, &fields);
		if (responce_error(id, parse_res))
			return;
		if((fields & AF_COUNT) == 0)
			parse_pins.count = 2;
		if(parse_pins.count == 0 || parse_pins.count > INTERFACES_BUF_SIZE) {
			responce_error(id, "Wrong read size");
			return;
		}
		if(i2c_init(id, fields, &parse_pins))
			return;
		char *res;
		if(fields & AF_DATA) {
			res = i2c_status_tochar(dhi2c_write(parse_pins.address, parse_pins.data, parse_pins.data_len, 0));
			if(res) {
				responce_error(id, res);
				return;
			}
		}
		res = i2c_status_tochar(dhi2c_read(parse_pins.address, parse_pins.data, parse_pins.count));
		if (res) {
			responce_error(id, res);
			return;
		}
		dhsender_response(id, DHSTATUS_OK, RDT_DATA_WITH_LEN, parse_pins.data, parse_pins.count);
	} else if( os_strcmp(command, "i2c/master/write") == 0 ) {
		parse_res = parse_params_pins_set(params, paramslen, &parse_pins, DHADC_SUITABLE_PINS, 0, AF_SDA | AF_SCL | AF_DATA | AF_ADDRESS, &fields);
		if (responce_error(id, parse_res))
			return;
		if((fields & AF_DATA) == 0) {
			responce_error(id, "Data not specified");
			return;
		}
		if(i2c_init(id, fields, &parse_pins))
			return;
		char *res = i2c_status_tochar(dhi2c_write(parse_pins.address, parse_pins.data, parse_pins.data_len, 1));
		if(responce_error(id, res) == 0)
			responce_ok(id);
	} else if( os_strcmp(command, "spi/master/read") == 0 ) {
		parse_res = parse_params_pins_set(params, paramslen, &parse_pins, DHADC_SUITABLE_PINS, 0, AF_CS | AF_SPIMODE | AF_DATA | AF_COUNT, &fields);
		if (responce_error(id, parse_res))
			return;
		if((fields & AF_COUNT) == 0)
			parse_pins.count = 2;
		if(parse_pins.count == 0 || parse_pins.count > INTERFACES_BUF_SIZE) {
			responce_error(id, "Wrong read size");
			return;
		}
		if(spi_init(id, fields, &parse_pins))
			return;
		if(fields & AF_DATA)
			dhspi_write(parse_pins.data, parse_pins.data_len, 0);
		dhspi_read(parse_pins.data, parse_pins.count);
		dhsender_response(id, DHSTATUS_OK, RDT_DATA_WITH_LEN, parse_pins.data, parse_pins.count);
	} else if( os_strcmp(command, "spi/master/write") == 0 ) {
		parse_res = parse_params_pins_set(params, paramslen, &parse_pins, DHADC_SUITABLE_PINS, 0, AF_CS | AF_SPIMODE | AF_DATA, &fields);
		if (responce_error(id, parse_res))
			return;
		if(spi_init(id, fields, &parse_pins))
			return;
		if((fields & AF_DATA) == 0) {
			responce_error(id, "Data not specified");
			return;
		}
		dhspi_write(parse_pins.data, parse_pins.data_len, 1);
		responce_ok(id);
	} else if( os_strcmp(command, "onewire/master/read") == 0 ) {
		parse_res = parse_params_pins_set(params, paramslen, &parse_pins, DHADC_SUITABLE_PINS, 0, AF_PIN | AF_DATA | AF_COUNT, &fields);
		if (responce_error(id, parse_res))
			return;
		if((fields & AF_COUNT) == 0 || parse_pins.count == 0 || parse_pins.count > INTERFACES_BUF_SIZE) {
			responce_error(id, "Wrong read size");
			return;
		}
		if((fields & AF_DATA) == 0) {
			responce_error(id, "Command for reading is not specified");
			return;
		}
		if(onewire_init(id, fields, &parse_pins))
			return;
		if(dhonewire_write(parse_pins.data, parse_pins.data_len) == 0) {
			responce_error(id, "No response");
			return;
		}
		dhonewire_read(parse_pins.data, parse_pins.count);
		dhsender_response(id, DHSTATUS_OK, RDT_DATA_WITH_LEN, parse_pins.data, parse_pins.count);
	} else if( os_strcmp(command, "onewire/master/write") == 0 ) {
		parse_res = parse_params_pins_set(params, paramslen, &parse_pins, DHADC_SUITABLE_PINS, 0, AF_PIN | AF_DATA, &fields);
		if (responce_error(id, parse_res))
			return;
		if(onewire_init(id, fields, &parse_pins))
			return;
		if(dhonewire_write(parse_pins.data, parse_pins.data_len) == 0) {
			responce_error(id, "No response");
			return;
		}
		responce_ok(id);
	} else if( (check = os_strcmp(command, "onewire/master/search")) == 0 || os_strcmp(command, "onewire/master/alarm") == 0) {
		if(paramslen) {
			parse_res = parse_params_pins_set(params, paramslen, &parse_pins, DHADC_SUITABLE_PINS, 0, AF_PIN, &fields);
			if (responce_error(id, parse_res))
				return;
			if(onewire_init(id, fields, &parse_pins))
				return;
		}
		parse_pins.data_len = sizeof(parse_pins.data) ;
		if(dhonewire_search(parse_pins.data, &parse_pins.data_len, (check == 0) ? 0xF0 : 0xEC, dhonewire_get_pin()))
			dhsender_response(id, DHSTATUS_OK, RDT_SEARCH64, dhonewire_get_pin(), parse_pins.data, parse_pins.data_len);
		else
			responce_error(id, "Error during search");
	} else if( os_strcmp(command, "onewire/master/int") == 0 ) {
		parse_res = parse_params_pins_set(params, paramslen, &parse_pins, DHGPIO_SUITABLE_PINS, dhgpio_get_timeout(), AF_DISABLE | AF_PRESENCE, &fields);
		if (parse_res)
			responce_error(id, parse_res);
		else if (fields == 0)
			responce_error(id, "Wrong action");
		else if(dhonewire_int(parse_pins.pins_to_presence, parse_pins.pins_to_disable))
			responce_ok(id);
		else
			responce_error(id, "Unsuitable pin");
	} else if( os_strcmp(command, "onewire/dht/read") == 0 ) {
		if(paramslen) {
			parse_res = parse_params_pins_set(params, paramslen, &parse_pins, DHADC_SUITABLE_PINS, 0, AF_PIN, &fields);
			if (responce_error(id, parse_res))
				return;
			if(onewire_init(id, fields, &parse_pins))
				return;
		}
		parse_pins.count = dhonewire_dht_read(parse_pins.data, sizeof(parse_pins.data));
		if(parse_pins.count)
			dhsender_response(id, DHSTATUS_OK, RDT_DATA_WITH_LEN, parse_pins.data, parse_pins.count);
		else
			responce_error(id, "No response");
	} else {
		responce_error(id, "Unknown command");
	}
}
