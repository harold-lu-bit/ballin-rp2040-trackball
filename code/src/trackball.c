/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Jacek Fedorynski
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bsp/board.h"
#include "tusb.h"

#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pico/time.h"

#include "hardware/gpio.h"

#include "pmw3360.h"
#include "scroll.h"

#define TOP_LEFT 16
#define TOP_RIGHT 28
#define BOTTOM_LEFT 17
#define BOTTOM_RIGHT 26
#define SIDE_MIDDLE 18
#define SIDE_LEFT 19
#define SIDE_RIGHT 20

#define BUTTON_LEFT 0x01
#define BUTTON_RIGHT 0x02
#define BUTTON_MIDDLE 0x04
#define BUTTON_BACK 0x08
#define BUTTON_FORWARD 0x10

#define CHORD_MS 60
#define BOOTSEL_HOLD_THRESHOLD_MS 2000
#define BASE_POINTER_SCALE 1.0f
#define POINTER_PRECISION_SPEED_LOW 1.0f
#define POINTER_PRECISION_SPEED_HIGH 4.0f
#define POINTER_PRECISION_SCALE_LOW 0.45f
#define POINTER_PRECISION_SCALE_HIGH 1.0f

typedef enum
{
    PRIMARY_IDLE = 0,
    PRIMARY_PENDING_L,
    PRIMARY_PENDING_R,
    PRIMARY_NORMAL_L,
    PRIMARY_NORMAL_R,
    PRIMARY_SCROLL,
    PRIMARY_LOCKOUT
} primary_button_state_t;

static const unsigned int cpi_levels[] = {800, 1600};

// These IDs are bogus. If you want to distribute any hardware using this,
// you will have to get real ones.
#define USB_VID 0xCAFE
#define USB_PID 0xBABA

tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,

    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x00,

    .bNumConfigurations = 0x01};

uint8_t const desc_hid_report[] = {
    0x05, 0x01,       // USAGE_PAGE (Generic Desktop)
    0x09, 0x02,       // USAGE (Mouse)
    0xA1, 0x01,       // COLLECTION (Application)
    0x09, 0x01,       //   USAGE (Pointer)
    0xA1, 0x00,       //   COLLECTION (Physical)
    0x05, 0x09,       //     USAGE_PAGE (Button)
    0x19, 0x01,       //     USAGE_MINIMUM (1)
    0x29, 0x05,       //     USAGE_MAXIMUM (5)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x25, 0x01,       //     LOGICAL_MAXIMUM (1)
    0x95, 0x05,       //     REPORT_COUNT (5)
    0x75, 0x01,       //     REPORT_SIZE (1)
    0x81, 0x02,       //     INPUT (Data,Var,Abs)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x75, 0x03,       //     REPORT_SIZE (3)
    0x81, 0x03,       //     INPUT (Const,Var,Abs)
    0x05, 0x01,       //     USAGE_PAGE (Generic Desktop)
    0x09, 0x30,       //     USAGE (X)
    0x09, 0x31,       //     USAGE (Y)
    0x16, 0x00, 0x80, //     LOGICAL_MINIMUM(-32768)
    0x26, 0xff, 0x7f, //     LOGICAL_MAXIMUM(32767)
    0x75, 0x10,       //     REPORT_SIZE (16)
    0x95, 0x02,       //     REPORT_COUNT (2)
    0x81, 0x06,       //     INPUT (Data,Var,Rel)
#if ENABLE_HIRES_WHEEL
    // This descriptor has no Report ID. The Resolution Multiplier feature report is one byte.
    0xA1, 0x02,       //     COLLECTION (Logical)
    0x05, 0x01,       //       USAGE_PAGE (Generic Desktop)
    0x09, 0x48,       //       USAGE (Resolution Multiplier)
    0x15, 0x00,       //       LOGICAL_MINIMUM (0)
    0x25, 0x0F,       //       LOGICAL_MAXIMUM (15)
    0x35, 0x01,       //       PHYSICAL_MINIMUM (1)
    0x45, 0x10,       //       PHYSICAL_MAXIMUM (16)
    0x75, 0x08,       //       REPORT_SIZE (8)
    0x95, 0x01,       //       REPORT_COUNT (1)
    0xB1, 0x02,       //       FEATURE (Data,Var,Abs)
    0x35, 0x00,       //       PHYSICAL_MINIMUM (0)
    0x45, 0x00,       //       PHYSICAL_MAXIMUM (0)
    0x05, 0x01,       //       USAGE_PAGE (Generic Desktop)
    0x09, 0x38,       //     USAGE (Wheel)
#else
    0x09, 0x38,       //     USAGE (Wheel)
#endif
    0x15, 0x81,       //     LOGICAL_MINIMUM (-127)
    0x25, 0x7F,       //     LOGICAL_MAXIMUM (127)
    0x75, 0x08,       //     REPORT_SIZE (8)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x81, 0x06,       //     INPUT (Data,Var,Rel)
#if ENABLE_HIRES_WHEEL
    0xC0,             //     END_COLLECTION
#endif
    0x05, 0x0C,       //     USAGE_PAGE (Consumer)
    0x0A, 0x38, 0x02, //     USAGE (AC Pan)
    0x15, 0x81,       //     LOGICAL_MINIMUM (-127)
    0x25, 0x7F,       //     LOGICAL_MAXIMUM (127)
    0x75, 0x08,       //     REPORT_SIZE (8)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x81, 0x06,       //     INPUT (Data,Var,Rel)
    0xC0,             //   END_COLLECTION
    0xC0              // END COLLECTION
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
#define EPNUM_HID 0x81

uint8_t const desc_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN, 0, 100),

    // Interface number, string index, protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report), EPNUM_HID, CFG_TUD_HID_EP_BUFSIZE, 1)
};

char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04}, // 0: is supported language is English (0x0409)
    "Trackball",                // 1: Manufacturer
    "Trackball",                // 2: Product
};

hid_report_t report;
primary_button_state_t primary_button_state;
absolute_time_t primary_pressed_at;
absolute_time_t bootsel_pressed_at;
bool bootsel_armed;
bool dpi_down_was_pressed;
bool dpi_up_was_pressed;
size_t current_cpi_index;
uint8_t primary_click_pulse_mask;
float pointer_x_accumulator;
float pointer_y_accumulator;

static size_t cpi_level_count(void)
{
    return sizeof(cpi_levels) / sizeof(cpi_levels[0]);
}

static unsigned int current_cpi(void)
{
    return cpi_levels[current_cpi_index];
}

static bool primary_state_is_scroll_locked(void)
{
    return primary_button_state == PRIMARY_SCROLL || primary_button_state == PRIMARY_LOCKOUT;
}

static void enter_scroll_mode(void)
{
    primary_button_state = PRIMARY_SCROLL;
    scroll_reset();
}

static void exit_scroll_mode(void)
{
    primary_button_state = PRIMARY_LOCKOUT;
    scroll_reset();
}

static void maybe_enter_bootsel(bool bootsel_combo_pressed)
{
    if (primary_state_is_scroll_locked())
    {
        bootsel_armed = false;
        return;
    }

    if (!bootsel_combo_pressed)
    {
        bootsel_armed = false;
        return;
    }

    if (!bootsel_armed)
    {
        bootsel_armed = true;
        bootsel_pressed_at = get_absolute_time();
        return;
    }

    if (absolute_time_diff_us(bootsel_pressed_at, get_absolute_time()) >=
        (BOOTSEL_HOLD_THRESHOLD_MS * 1000))
    {
        reset_usb_boot(0, 0);
    }
}

static void update_primary_button_state(bool left_pressed, bool right_pressed)
{
    primary_click_pulse_mask = 0;

    switch (primary_button_state)
    {
    case PRIMARY_IDLE:
        if (left_pressed && right_pressed)
        {
            enter_scroll_mode();
        }
        else if (left_pressed)
        {
            primary_button_state = PRIMARY_PENDING_L;
            primary_pressed_at = get_absolute_time();
        }
        else if (right_pressed)
        {
            primary_button_state = PRIMARY_PENDING_R;
            primary_pressed_at = get_absolute_time();
        }
        break;

    case PRIMARY_PENDING_L:
        if (!left_pressed)
        {
            primary_button_state = PRIMARY_IDLE;
            primary_click_pulse_mask = BUTTON_LEFT;
        }
        else if (right_pressed &&
                 absolute_time_diff_us(primary_pressed_at, get_absolute_time()) <= (CHORD_MS * 1000))
        {
            enter_scroll_mode();
        }
        else if (absolute_time_diff_us(primary_pressed_at, get_absolute_time()) >= (CHORD_MS * 1000))
        {
            primary_button_state = PRIMARY_NORMAL_L;
        }
        break;

    case PRIMARY_PENDING_R:
        if (!right_pressed)
        {
            primary_button_state = PRIMARY_IDLE;
            primary_click_pulse_mask = BUTTON_RIGHT;
        }
        else if (left_pressed &&
                 absolute_time_diff_us(primary_pressed_at, get_absolute_time()) <= (CHORD_MS * 1000))
        {
            enter_scroll_mode();
        }
        else if (absolute_time_diff_us(primary_pressed_at, get_absolute_time()) >= (CHORD_MS * 1000))
        {
            primary_button_state = PRIMARY_NORMAL_R;
        }
        break;

    case PRIMARY_NORMAL_L:
        if (!left_pressed)
        {
            primary_button_state = right_pressed ? PRIMARY_NORMAL_R : PRIMARY_IDLE;
        }
        break;

    case PRIMARY_NORMAL_R:
        if (!right_pressed)
        {
            primary_button_state = left_pressed ? PRIMARY_NORMAL_L : PRIMARY_IDLE;
        }
        break;

    case PRIMARY_SCROLL:
        if (!left_pressed || !right_pressed)
        {
            exit_scroll_mode();
        }
        break;

    case PRIMARY_LOCKOUT:
        if (!left_pressed && !right_pressed)
        {
            primary_button_state = PRIMARY_IDLE;
        }
        break;
    }
}

static uint8_t primary_buttons_report(bool left_pressed, bool right_pressed)
{
    uint8_t buttons = primary_click_pulse_mask;

    switch (primary_button_state)
    {
    case PRIMARY_NORMAL_L:
        buttons |= BUTTON_LEFT;

        if (right_pressed)
        {
            buttons |= BUTTON_RIGHT;
        }
        break;

    case PRIMARY_NORMAL_R:
        buttons |= BUTTON_RIGHT;

        if (left_pressed)
        {
            buttons |= BUTTON_LEFT;
        }
        break;

    default:
        break;
    }

    return buttons;
}

static void apply_cpi_index(size_t new_index)
{
    if (new_index == current_cpi_index)
    {
        return;
    }

    current_cpi_index = new_index;
    pmw3360_set_cpi(current_cpi());
}

static void update_cpi_buttons(bool dpi_down_pressed, bool dpi_up_pressed)
{
    size_t last_index = cpi_level_count() - 1;

    if (dpi_down_pressed && !dpi_down_was_pressed && current_cpi_index > 0)
    {
        apply_cpi_index(current_cpi_index - 1);
    }

    if (dpi_up_pressed && !dpi_up_was_pressed && current_cpi_index < last_index)
    {
        apply_cpi_index(current_cpi_index + 1);
    }

    dpi_down_was_pressed = dpi_down_pressed;
    dpi_up_was_pressed = dpi_up_pressed;
}

static void pointer_reset(void)
{
    pointer_x_accumulator = 0.0f;
    pointer_y_accumulator = 0.0f;
}

static float pointer_precision_scale(int speed)
{
    if (speed <= POINTER_PRECISION_SPEED_LOW)
    {
        return POINTER_PRECISION_SCALE_LOW;
    }

    if (speed >= POINTER_PRECISION_SPEED_HIGH)
    {
        return POINTER_PRECISION_SCALE_HIGH;
    }

    float t = ((float)speed - POINTER_PRECISION_SPEED_LOW) /
              (POINTER_PRECISION_SPEED_HIGH - POINTER_PRECISION_SPEED_LOW);

    return POINTER_PRECISION_SCALE_LOW +
           t * (POINTER_PRECISION_SCALE_HIGH - POINTER_PRECISION_SCALE_LOW);
}

static int16_t pointer_report_delta(float *accumulator, float scaled_delta)
{
    int16_t report_delta;

    *accumulator += scaled_delta;
    report_delta = (int16_t)*accumulator;
    *accumulator -= (float)report_delta;

    return report_delta;
}

static void fill_pointer_report(int16_t dx, int16_t dy)
{
    int speed = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    float scale = BASE_POINTER_SCALE * pointer_precision_scale(speed);

    report.dx = pointer_report_delta(&pointer_x_accumulator, (float)dx * scale);
    report.dy = -pointer_report_delta(&pointer_y_accumulator, (float)dy * scale);
    report.wheel = 0;
    report.pan = 0;
}

void hid_task(void)
{
    int16_t dx;
    int16_t dy;
    bool top_left_pressed;
    bool top_right_pressed;
    bool bottom_left_pressed;
    bool bottom_right_pressed;
    bool side_middle_pressed;
    bool side_left_pressed;
    bool side_right_pressed;
    bool bootsel_combo_pressed;

    if (!tud_hid_ready())
    {
        return;
    }

    pmw3360_get_deltas(&dx, &dy);

    report.buttons = 0;
    report.dx = 0;
    report.dy = 0;
    report.wheel = 0;
    report.pan = 0;

    top_left_pressed = !gpio_get(TOP_LEFT);
    top_right_pressed = !gpio_get(TOP_RIGHT);
    bottom_left_pressed = !gpio_get(BOTTOM_LEFT);
    bottom_right_pressed = !gpio_get(BOTTOM_RIGHT);
    side_middle_pressed = gpio_get(SIDE_MIDDLE);
    side_left_pressed = gpio_get(SIDE_LEFT);
    side_right_pressed = gpio_get(SIDE_RIGHT);
    bootsel_combo_pressed = bottom_left_pressed && bottom_right_pressed;

    update_primary_button_state(side_middle_pressed, side_right_pressed);
    maybe_enter_bootsel(bootsel_combo_pressed);
    update_cpi_buttons(top_left_pressed, top_right_pressed);

    report.buttons |= primary_buttons_report(side_middle_pressed, side_right_pressed);

    if (side_left_pressed)
        report.buttons |= BUTTON_MIDDLE;
    if (bottom_left_pressed && !bootsel_combo_pressed)
        report.buttons |= BUTTON_FORWARD;
    if (bottom_right_pressed && !bootsel_combo_pressed)
        report.buttons |= BUTTON_BACK;

    if (primary_button_state == PRIMARY_SCROLL)
    {
        pointer_reset();
        scroll_fill_report(dx, dy, &report);
    }
    else
    {
        fill_pointer_report(dx, dy);
    }

    tud_hid_report(0, &report, sizeof(report));
}

void pin_init(uint pin)
{
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
}

void pin_init_pulldown(uint pin)
{
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_down(pin);
}

void pins_init(void)
{
    pin_init(TOP_LEFT);
    pin_init(TOP_RIGHT);
    pin_init(BOTTOM_LEFT);
    pin_init(BOTTOM_RIGHT);
    pin_init_pulldown(SIDE_MIDDLE);
    pin_init_pulldown(SIDE_LEFT);
    pin_init_pulldown(SIDE_RIGHT);
}

void report_init(void)
{
    memset(&report, 0, sizeof(report));
    primary_button_state = PRIMARY_IDLE;
    bootsel_armed = false;
    dpi_down_was_pressed = false;
    dpi_up_was_pressed = false;
    current_cpi_index = 0;
    primary_click_pulse_mask = 0;
    pointer_reset();
    scroll_init();
}

int main(void)
{
    stdio_init_all();
    board_init();
    pins_init();
    pmw3360_init();
    report_init();
    tusb_init();

    pmw3360_set_cpi(current_cpi());

    while (1)
    {
        tud_task(); // tinyusb device task
        hid_task();
    }

    return 0;
}

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

// Invoked when received GET HID REPORT DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const *tud_hid_descriptor_report_cb(uint8_t itf)
{
    return desc_hid_report;
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    (void)itf;
    return scroll_get_feature_report(report_id, report_type, buffer, reqlen);
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
    (void)itf;
    scroll_set_feature_report(report_id, report_type, buffer, bufsize);
}

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    return desc_configuration;
}

static uint16_t _desc_str[32];

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    uint8_t chr_count;

    if (index == 0)
    {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    }
    else
    {
        // Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
        // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors

        if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))
            return NULL;

        const char *str = string_desc_arr[index];

        // Cap at max char
        chr_count = strlen(str);
        if (chr_count > 31)
            chr_count = 31;

        // Convert ASCII string into UTF-16
        for (uint8_t i = 0; i < chr_count; i++)
        {
            _desc_str[1 + i] = str[i];
        }
    }

    // first byte is length (including header), second byte is string type
    _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);

    return _desc_str;
}
