# DIY 轨迹球鼠标：基于 TinyUSB 的高分辨率平滑滚动实现说明

## 目标

当前滚动逻辑大致是：

```c
ball_scroll_acc += trackball_delta_y;

if (ball_scroll_acc >= 96) {
    send_wheel(+1);
    ball_scroll_acc -= 96;
}

if (ball_scroll_acc <= -96) {
    send_wheel(-1);
    ball_scroll_acc += 96;
}
```

这个方案的页面滚动距离是合适的，但体感有顿挫，因为屏幕要等轨迹球累计到 96 才滚动一次。

请实现：

1. 保持“约 96 个轨迹球 delta 对应传统滚轮 1 格”的整体比例；
2. 将这 1 格拆成多个高分辨率小步；
3. 使用 HID `Resolution Multiplier` 暴露高分辨率滚轮能力；
4. 加入速度曲线，使慢速精细、快速高效；
5. 保留余量，不丢失轨迹球输入；
6. 不改变普通鼠标移动逻辑，只影响“按住自定义按键 + 滚球”的滚动模式。

USB HID 标准中，`Resolution Multiplier` 可与 `Wheel` 放在同一个 Logical Collection 内，使 Wheel 的有效分辨率从普通 1 count/detent 提高到多 count/detent；标准示例中 multiplier 可让 Wheel 变成每 detent 8 counts 或更多。([USB-IF][1])

---

## 基准参数

当前用户确认：

```c
96 trackball_delta ≈ 1 traditional wheel detent
```

不要简单把 96 改小，因为那会改变整体滚动距离。目标是：

```text
整体滚动距离仍接近原来：
96 ball delta -> 1 traditional wheel detent

但输出变细：
96 ball delta -> 8 个 high-res wheel count
```

推荐初始配置：

```c
#define SCROLL_BASE_BALL_UNITS_PER_DETENT 96.0f
#define SCROLL_RESOLUTION_MULTIPLIER       8
#define SCROLL_SUBUNIT                     (SCROLL_BASE_BALL_UNITS_PER_DETENT / SCROLL_RESOLUTION_MULTIPLIER)
// 初始即 12.0f
```

也就是：

```text
12 ball delta -> 1 high-res wheel count
8 high-res wheel counts -> 1 traditional wheel detent
96 ball delta -> 1 traditional wheel detent
```

如果 OS / 应用正确处理 HID Resolution Multiplier，滚动距离应接近旧方案，但启动距离从 96 降到 12，顿挫感会明显降低。

---

## HID Report Descriptor 要求

请在当前 TinyUSB HID mouse descriptor 中定位 wheel 字段。

如果当前 descriptor 只有普通 wheel，例如：

```text
Usage Page (Generic Desktop)
Usage (Wheel)
Logical Minimum (-127)
Logical Maximum (127)
Report Size (8)
Report Count (1)
Input (Data, Var, Rel)
```

请新增 `Resolution Multiplier` feature，并让它只作用于 wheel。

目标结构概念如下，具体字节写法按项目现有 descriptor 风格实现：

```text
Collection (Logical)
    Usage Page (Generic Desktop)
    Usage (Resolution Multiplier)
    Logical Minimum (0)
    Logical Maximum (15)
    Physical Minimum (1)
    Physical Maximum (16)
    Report Size (4)
    Report Count (1)
    Feature (Data, Var, Abs)

    Usage Page (Generic Desktop)
    Usage (Wheel)
    Logical Minimum (-127)
    Logical Maximum (127)
    Report Size (8)
    Report Count (1)
    Input (Data, Var, Rel)
End Collection
```

推荐让默认 effective multiplier 为 8：

```c
resolution_multiplier_feature_value = SCROLL_RESOLUTION_MULTIPLIER - 1;
// 如果 descriptor 使用 Logical 0..15 + Physical 1..16，则 value 7 对应 effective multiplier 8
```

注意事项：

1. 需要实现 Feature Report 的 get/set 处理。
2. Host 可能会读取或写入 Resolution Multiplier。
3. 如果 host 写入合法范围内的 multiplier，应更新运行时 multiplier。
4. 如果 host 不写入，固件默认使用 8。
5. 如果项目当前使用 TinyUSB 的标准 mouse helper 发送固定格式 report，而该 helper 不匹配新 descriptor，需要改为发送自定义 HID report struct。
6. 如果改 descriptor 后枚举失败，优先检查 Report ID、Report Size、Report Count、padding、Feature report 长度是否与 TinyUSB 回调返回的数据一致。

TinyUSB 的 HID 支持代码和 HID 相关定义位于其 HID class 实现中；本地工程中应优先查找当前 descriptor、mouse report struct、`tud_hid_report` 或同类发送路径。([GitHub][2])

---

## Report 结构建议

不要假设现有 report 格式。请根据项目当前 descriptor 调整。

概念上 mouse input report 应至少包含：

```c
typedef struct {
    uint8_t buttons;
    int8_t  x;
    int8_t  y;
    int8_t  wheel;   // high-res vertical wheel count
    // optional: int8_t pan; // horizontal scroll, 如果现有项目已有
} mouse_report_t;
```

滚动模式下：

```text
x/y 鼠标移动应为 0，避免滚动时同时移动指针。
wheel 发送 high-res count。
buttons 保持当前按键状态。
```

非滚动模式下：

```text
x/y 正常发送轨迹球移动。
wheel = 0。
```

---

## 滚动算法

### 状态变量

```c
static float scroll_acc = 0.0f;
static int8_t last_scroll_dir = 0;
static uint32_t last_scroll_time_ms = 0;
static uint8_t effective_scroll_multiplier = 8;
```

### 参数

```c
#define SCROLL_BASE_BALL_UNITS_PER_DETENT 96.0f
#define SCROLL_DEFAULT_MULTIPLIER          8
#define SCROLL_MIN_MULTIPLIER              1
#define SCROLL_MAX_MULTIPLIER              16

#define SCROLL_DIRECTION_RESET_FACTOR      0.25f
#define SCROLL_IDLE_RESET_MS               120
#define SCROLL_NOISE_DEADZONE              0
#define SCROLL_MAX_REPORT_WHEEL            6
```

`SCROLL_NOISE_DEADZONE` 初始建议为 `0`，因为旧方案已经验证 96 的总体比例合适。若实测轻触有误滚，再改成 `1`。

`SCROLL_MAX_REPORT_WHEEL` 用来限制单个 USB report 里的 wheel 值，避免快速甩球时一次发太大导致应用侧表现粗糙。推荐 `4~8`，初始用 `6`。

---

## 速度曲线

因为要保持 96 的总体比例，速度曲线的平均增益应围绕 `1.0`，不要整体放大太多。

推荐初始曲线：

```c
static float scroll_gain_from_speed(int abs_dy) {
    if (abs_dy <= 1) {
        return 0.70f;   // 极慢：更细，减少误滚
    } else if (abs_dy <= 4) {
        return 0.90f;   // 慢速：接近线性
    } else if (abs_dy <= 12) {
        return 1.00f;   // 常用速度：保持 96 基准
    } else if (abs_dy <= 28) {
        return 1.20f;   // 较快：轻微加速
    } else {
        return 1.45f;   // 甩球：明显加速，但不要太夸张
    }
}
```

如果用户觉得“滚动距离已经适配得好，只是顿挫”，那第一版甚至可以先使用更保守的曲线：

```c
static float scroll_gain_from_speed(int abs_dy) {
    if (abs_dy <= 1) return 0.85f;
    if (abs_dy <= 12) return 1.00f;
    if (abs_dy <= 28) return 1.10f;
    return 1.25f;
}
```

优先使用保守曲线。

---

## 核心伪代码

```c
void handle_trackball_scroll_mode(int16_t raw_dy, uint32_t now_ms) {
    if (raw_dy == 0) {
        maybe_send_mouse_report_with_wheel(0);
        return;
    }

    if ((now_ms - last_scroll_time_ms) > SCROLL_IDLE_RESET_MS) {
        scroll_acc = 0.0f;
        last_scroll_dir = 0;
    }

    last_scroll_time_ms = now_ms;

    int8_t dir = (raw_dy > 0) ? 1 : -1;
    int abs_dy = raw_dy > 0 ? raw_dy : -raw_dy;

    if (abs_dy <= SCROLL_NOISE_DEADZONE) {
        return;
    }

    if (last_scroll_dir != 0 && dir != last_scroll_dir) {
        scroll_acc *= SCROLL_DIRECTION_RESET_FACTOR;
    }

    last_scroll_dir = dir;

    uint8_t m = effective_scroll_multiplier;
    if (m < SCROLL_MIN_MULTIPLIER) m = SCROLL_DEFAULT_MULTIPLIER;
    if (m > SCROLL_MAX_MULTIPLIER) m = SCROLL_DEFAULT_MULTIPLIER;

    float subunit = SCROLL_BASE_BALL_UNITS_PER_DETENT / (float)m;
    float gain = scroll_gain_from_speed(abs_dy);

    scroll_acc += (float)raw_dy * gain;

    int8_t wheel_out = 0;

    while (scroll_acc >= subunit && wheel_out < SCROLL_MAX_REPORT_WHEEL) {
        wheel_out += 1;
        scroll_acc -= subunit;
    }

    while (scroll_acc <= -subunit && wheel_out > -SCROLL_MAX_REPORT_WHEEL) {
        wheel_out -= 1;
        scroll_acc += subunit;
    }

    if (wheel_out != 0) {
        send_mouse_report(/* x */ 0, /* y */ 0, /* wheel */ wheel_out);
    }
}
```

如果方向反了，改其中一个地方即可：

```c
raw_dy = -raw_dy;
```

不要同时在多个层级反向。

---

## Feature Report 行为

实现 Resolution Multiplier 的 Feature Report 时，建议：

```c
static uint8_t resolution_multiplier_value = SCROLL_DEFAULT_MULTIPLIER - 1;
static uint8_t effective_scroll_multiplier = SCROLL_DEFAULT_MULTIPLIER;
```

当 host get feature：

```c
return resolution_multiplier_value;
```

当 host set feature：

```c
if (value <= 15) {
    resolution_multiplier_value = value;
    effective_scroll_multiplier = value + 1;
} else {
    ignore or clamp;
}
```

如果 descriptor 选择的 Logical/Physical 范围不是 `0..15 -> 1..16`，则按实际 descriptor 计算 effective multiplier。推荐使用 `0..15 -> 1..16`，因为它和 USB HID Usage Tables 示例一致。([USB-IF][1])

---

## 兼容性 fallback

需要提供一个编译期开关或运行时配置：

```c
#define ENABLE_HIRES_WHEEL 1
```

当 `ENABLE_HIRES_WHEEL == 0` 时，恢复旧逻辑：

```c
96 ball delta -> wheel +/-1
```

当 `ENABLE_HIRES_WHEEL == 1` 但发现目标系统滚动距离明显变大，说明 host 或应用可能没有按预期处理 Resolution Multiplier。此时提供 fallback：

```c
effective_scroll_multiplier = 1;
subunit = 96.0f;
```

或者保留 high-res 算法但降低发送值：

```c
// compatibility mode
SCROLL_DEFAULT_MULTIPLIER = 1
```

建议在代码中保留两个 profile：

```c
PROFILE_HIRES:
    multiplier = 8
    subunit = 12

PROFILE_COMPAT:
    multiplier = 1
    subunit = 96
```
