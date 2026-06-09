#include "pyro_buzzer.h"

#include <nrf_delay.h>

#define PYRO_BUZZER_BEEP_COUNT       3u
#define PYRO_BUZZER_TONE_HALF_US   250u
#define PYRO_BUZZER_BEEP_MS        120u
#define PYRO_BUZZER_PAUSE_MS       120u

static void pyro_buzzer_tone_ms(uint32_t duration_ms)
{
    uint32_t cycles = (duration_ms * 1000u) / (PYRO_BUZZER_TONE_HALF_US * 2u);

    while (cycles-- > 0u)
    {
        nrf_gpio_pin_set(PYRO_BUZZER_PIN);
        nrf_delay_us(PYRO_BUZZER_TONE_HALF_US);
        nrf_gpio_pin_clear(PYRO_BUZZER_PIN);
        nrf_delay_us(PYRO_BUZZER_TONE_HALF_US);
    }
}

void pyro_buzzer_beep_three_times(void)
{
    nrf_gpio_cfg_output(PYRO_BUZZER_PIN);
    nrf_gpio_pin_clear(PYRO_BUZZER_PIN);

    for (uint8_t beep = 0u; beep < PYRO_BUZZER_BEEP_COUNT; beep++)
    {
        pyro_buzzer_tone_ms(PYRO_BUZZER_BEEP_MS);
        nrf_gpio_pin_clear(PYRO_BUZZER_PIN);
        nrf_delay_ms(PYRO_BUZZER_PAUSE_MS);
    }
}