/*
 * accel.h - accelerometer abstraction for DWM3001C and pyro boxes
 *
 * The public API returns acceleration in mg. The selected CMake preset decides
 * whether the firmware targets legacy LIS2DH12, GenA BMI323, or explicit auto.
 */

#ifndef ACCEL_H
#define ACCEL_H

#include <stdint.h>
#include <stdbool.h>

/* Accelerometer data in milligravity (mg). */
typedef struct {
    int16_t x;  /* mg */
    int16_t y;  /* mg */
    int16_t z;  /* mg */
} accel_data_t;

#define ACCEL_BACKEND_CODE_NONE    0u
#define ACCEL_BACKEND_CODE_LIS2DH12 1u
#define ACCEL_BACKEND_CODE_BMI323   2u

/* Initialize the preset-selected accelerometer backend. */
bool accel_init(void);

/* Read XYZ acceleration in mg. */
bool accel_read(accel_data_t *data);

/* Human-readable active backend name for boot/debug logs. */
const char *accel_backend_name(void);

/* Compact active backend values for serial/UWB metadata. */
const char *accel_backend_label(void);
uint8_t accel_backend_code(void);
bool accel_backend_is_bmi323(void);

#endif /* ACCEL_H */
