#ifndef PINS_H
#define PINS_H

// ---- Motor Driver (TB6612FNG) ----
#define AIN1    2
#define AIN2    3
#define PWMA    4
#define STBY    5

// ---- Battery Voltage Divider ----
#define BATTERY_ADC 0

// ---- Wake Button (BOOT button on ESP32‑C3 SuperMini, GPIO9) ----
#define WAKE_BUTTON_PIN 9

// ---- Motor timing ----
#define CURTAIN_TRAVEL_TIME 4000   // ms for full travel (will be auto‑calibrated later)
#define MAX_RUN_TIME         8000  // safety cutoff
#define BATTERY_CAL          2.08f // voltage divider factor
#define ADC_SAMPLES          16

// ---- Motor PWM (soft start / soft stop) ----
#define PWM_FREQ              20000  // 20kHz, above audible range
#define PWM_RESOLUTION_BITS   8      // duty range 0-255
#define PWM_MAX_DUTY          ((1 << PWM_RESOLUTION_BITS) - 1)
#define MOTOR_RAMP_UP_MS      300    // time to reach full speed from stop
#define MOTOR_RAMP_DOWN_MS    300    // time to decelerate before a scheduled stop
#define MOTOR_MIN_DUTY        60     // duty floor while ramping (below this the motor may stall, not spin)

// ---- Timezone (POSIX TZ string) ----
// Default: Europe/Helsinki (EET/EEST with automatic DST).
// Change this if you build VerhoBot outside Finland.
// Reference table: https://github.com/nayarsystems/posix_tz_db
#define TIMEZONE_STRING "EET-2EEST,M3.5.0/3,M10.5.0/4"

#endif
