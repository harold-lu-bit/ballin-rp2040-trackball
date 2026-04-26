#ifndef SCROLL_H
#define SCROLL_H

#include <stdint.h>

#include "tusb.h"

#define ENABLE_HIRES_WHEEL 1

typedef struct __attribute__((packed))
{
    uint8_t buttons;
    int16_t dx;
    int16_t dy;
    int8_t wheel;
    int8_t pan;
} hid_report_t;

void scroll_init(void);
void scroll_reset(void);
void scroll_fill_report(int16_t dx, int16_t dy, hid_report_t *report);
uint16_t scroll_get_feature_report(hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen);
void scroll_set_feature_report(hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize);

#endif
