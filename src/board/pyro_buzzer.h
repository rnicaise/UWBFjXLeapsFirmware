#ifndef PYRO_BUZZER_H
#define PYRO_BUZZER_H

#include <stdint.h>
#include <nrf_gpio.h>

#ifndef PYRO_BUZZER_PIN
#define PYRO_BUZZER_PIN NRF_GPIO_PIN_MAP(1, 5)
#endif

void pyro_buzzer_beep_three_times(void);

#endif /* PYRO_BUZZER_H */