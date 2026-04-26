#include "scroll.h"

#include <stdlib.h>

#include "pico/time.h"

#define SCROLL_BASE_BALL_UNITS_PER_DETENT 96.0f
#define SCROLL_DEFAULT_MULTIPLIER 1
#define SCROLL_MIN_MULTIPLIER 1
#define SCROLL_MAX_MULTIPLIER 16
#define SCROLL_HORIZONTAL_DIVISOR 128
#define SCROLL_DIRECTION_RESET_FACTOR 0.25f
#define SCROLL_IDLE_RESET_MS 120
#define SCROLL_MAX_REPORT_WHEEL 6
#define SCROLL_MAX_FEATURE_VALUE 15
#define INVERT_VERTICAL_SCROLL 1

static float vertical_scroll_accumulator;
static int32_t horizontal_scroll_accumulator;
static int8_t last_scroll_dir;
static uint32_t last_scroll_time_ms;
static uint8_t resolution_multiplier_value;
static uint8_t effective_scroll_multiplier;

static int8_t clamp_scroll_steps(int32_t steps)
{
    if (steps > 127)
    {
        return 127;
    }

    if (steps < -127)
    {
        return -127;
    }

    return (int8_t)steps;
}

static uint32_t scroll_time_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

static void reset_vertical_scroll_state(void)
{
    vertical_scroll_accumulator = 0.0f;
    last_scroll_dir = 0;
    last_scroll_time_ms = 0;
}

static float scroll_gain_from_speed(int abs_dy)
{
    if (abs_dy <= 1)
    {
        return 0.85f;
    }

    if (abs_dy <= 12)
    {
        return 1.0f;
    }

    if (abs_dy <= 28)
    {
        return 1.1f;
    }

    return 1.25f;
}

static uint8_t sanitized_multiplier(void)
{
    if (effective_scroll_multiplier < SCROLL_MIN_MULTIPLIER || effective_scroll_multiplier > SCROLL_MAX_MULTIPLIER)
    {
        return SCROLL_DEFAULT_MULTIPLIER;
    }

    return effective_scroll_multiplier;
}

static void set_multiplier_from_feature_value(uint8_t value)
{
    if (value > SCROLL_MAX_FEATURE_VALUE)
    {
        resolution_multiplier_value = SCROLL_DEFAULT_MULTIPLIER - 1;
        effective_scroll_multiplier = SCROLL_DEFAULT_MULTIPLIER;
        return;
    }

    resolution_multiplier_value = value;
    effective_scroll_multiplier = value + 1;
}

void scroll_init(void)
{
    horizontal_scroll_accumulator = 0;
    set_multiplier_from_feature_value(SCROLL_DEFAULT_MULTIPLIER - 1);
    reset_vertical_scroll_state();
}

void scroll_reset(void)
{
    horizontal_scroll_accumulator = 0;
    reset_vertical_scroll_state();
}

void scroll_fill_report(int16_t dx, int16_t dy, hid_report_t *report)
{
    int32_t horizontal_steps = 0;
    int raw_vertical;

    report->dx = 0;
    report->dy = 0;
    report->wheel = 0;
    report->pan = 0;

    if (abs(dx) > abs(dy))
    {
        horizontal_scroll_accumulator += dx;
        horizontal_steps = horizontal_scroll_accumulator / SCROLL_HORIZONTAL_DIVISOR;
        horizontal_scroll_accumulator -= horizontal_steps * SCROLL_HORIZONTAL_DIVISOR;
    }
    else
    {
        horizontal_scroll_accumulator = 0;
    }

    report->pan = clamp_scroll_steps(horizontal_steps);

#if INVERT_VERTICAL_SCROLL
    raw_vertical = dy;
#else
    raw_vertical = -dy;
#endif

    if (raw_vertical == 0)
    {
        return;
    }

    if (last_scroll_time_ms != 0)
    {
        uint32_t now_ms = scroll_time_ms();

        if ((now_ms - last_scroll_time_ms) > SCROLL_IDLE_RESET_MS)
        {
            reset_vertical_scroll_state();
        }

        last_scroll_time_ms = now_ms;
    }
    else
    {
        last_scroll_time_ms = scroll_time_ms();
    }

#if ENABLE_HIRES_WHEEL
    {
        int abs_vertical = abs(raw_vertical);
        int8_t dir = raw_vertical > 0 ? 1 : -1;
        int8_t wheel_out = 0;
        float subunit;

        if (last_scroll_dir != 0 && dir != last_scroll_dir)
        {
            vertical_scroll_accumulator *= SCROLL_DIRECTION_RESET_FACTOR;
        }

        last_scroll_dir = dir;

        subunit = SCROLL_BASE_BALL_UNITS_PER_DETENT / (float)sanitized_multiplier();
        vertical_scroll_accumulator += (float)raw_vertical * scroll_gain_from_speed(abs_vertical);

        while (vertical_scroll_accumulator >= subunit && wheel_out < SCROLL_MAX_REPORT_WHEEL)
        {
            wheel_out += 1;
            vertical_scroll_accumulator -= subunit;
        }

        while (vertical_scroll_accumulator <= -subunit && wheel_out > -SCROLL_MAX_REPORT_WHEEL)
        {
            wheel_out -= 1;
            vertical_scroll_accumulator += subunit;
        }

        report->wheel = wheel_out;
    }
#else
    vertical_scroll_accumulator += (float)raw_vertical;
    report->wheel = clamp_scroll_steps((int32_t)(vertical_scroll_accumulator / SCROLL_BASE_BALL_UNITS_PER_DETENT));
    vertical_scroll_accumulator -= (float)report->wheel * SCROLL_BASE_BALL_UNITS_PER_DETENT;
#endif
}

uint16_t scroll_get_feature_report(hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
#if ENABLE_HIRES_WHEEL
    if (report_type != HID_REPORT_TYPE_FEATURE || reqlen < 1)
    {
        return 0;
    }

    buffer[0] = resolution_multiplier_value & 0x0F;
    return 1;
#else
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
#endif
}

void scroll_set_feature_report(hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
#if ENABLE_HIRES_WHEEL
    uint8_t value;

    if (report_type != HID_REPORT_TYPE_FEATURE || bufsize < 1)
    {
        return;
    }

    value = buffer[0] & 0x0F;

    if (value <= SCROLL_MAX_FEATURE_VALUE)
    {
        set_multiplier_from_feature_value(value);
    }
#else
    (void)report_type;
    (void)buffer;
    (void)bufsize;
#endif
}
