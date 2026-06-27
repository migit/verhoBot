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

#endif
