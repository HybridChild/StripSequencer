/*
 * Copyright (c) 2026 Esben Dueholm Nørgaard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The adapter between the library's neutral pixel buffer and a Zephyr
 * led_strip driver: the one piece of the platform side every application
 * would otherwise rewrite from the devicetree.
 *
 *   static constexpr stripseq::zephyr::LedStrip out =
 *       STRIPSEQ_LED_STRIP_DT_GET(DT_ALIAS(led_strip));
 *   ...
 *   strip.render(now);
 *   stripseq::zephyr::update(out, buffer);
 *
 * Only the hand-off, never the frame loop. When to render, on which
 * workqueue, at what frame rate and whether finished segments are dropped are
 * the application's choices, and two applications rarely make them alike.
 *
 * It ships with the Zephyr module rather than the core, which never sees an
 * operating system. Header-only, so an application that never includes it
 * pays nothing, and one that does needs CONFIG_LED_STRIP for the driver API.
 */

#ifndef STRIPSEQ_ZEPHYR_LED_STRIP_H_
#define STRIPSEQ_ZEPHYR_LED_STRIP_H_

#include <cstddef>
#include <cstdint>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/dt-bindings/led/led.h>

#include <stripseq/strip.h>

#ifndef CONFIG_LED_STRIP
#error "stripseq/zephyr/led_strip.h drives a led_strip device: set CONFIG_LED_STRIP=y"
#endif

namespace stripseq::zephyr {

/* A strip to send frames to: the device, and on the RGBW path the order its
 * chip takes the four channels in. Built by STRIPSEQ_LED_STRIP_DT_GET, so it
 * is a constant that lives in flash. */
struct LedStrip {
    const struct device* dev;
#if STRIPSEQ_WHITE_CHANNEL
    /* Byte j of a pixel on the wire is byte from[j] of the neutral R,G,B,W
     * pixel. */
    uint8_t from[4];
#endif
};

#if STRIPSEQ_WHITE_CHANNEL

/* led_strip_update_channels() takes bytes already in the chip's own order and
 * consults color-mapping nowhere, so the order is read from the devicetree
 * here instead of being written down a second time.
 *
 * Declared, never defined, never constexpr, as invalid_sequence's are: called
 * from the consteval function below, the name is the compile error. */
namespace invalid_color_mapping {
void color_mapping_must_name_red_green_blue_and_white_once_each();
}

/* What STRIPSEQ_LED_STRIP_DT_GET calls with the node's color-mapping, which
 * arrives as a braced list of LED_COLOR_ID values. consteval, so a mapping
 * that is not a permutation of the four channels fails the build at the line
 * that names the node, not on the bench as the wrong color. */
template <size_t N>
consteval LedStrip led_strip(const struct device* dev, const uint8_t (&mapping)[N]) {
    static_assert(N == Strip::bytes_per_pixel,
                  "color-mapping must name exactly as many channels as an RGBW pixel has");
    LedStrip out{dev, {}};
    bool seen[4] = {};
    for (size_t j = 0; j < N; ++j) {
        const uint8_t id = mapping[j];
        /* Where this channel sits in the neutral pixel. */
        uint8_t at = 0;
        if (id == LED_COLOR_ID_WHITE)
            at = 3;
        else if (id >= LED_COLOR_ID_RED && id <= LED_COLOR_ID_BLUE)
            at = static_cast<uint8_t>(id - LED_COLOR_ID_RED);
        else
            invalid_color_mapping::color_mapping_must_name_red_green_blue_and_white_once_each();
        if (seen[at])
            invalid_color_mapping::color_mapping_must_name_red_green_blue_and_white_once_each();
        seen[at] = true;
        out.from[j] = at;
    }
    return out;
}

#define STRIPSEQ_LED_STRIP_DT_GET(node_id) \
    ::stripseq::zephyr::led_strip(DEVICE_DT_GET(node_id), DT_PROP(node_id, color_mapping))

#else

/* The RGB path hands the buffer to led_strip_update_rgb() as it stands, since
 * the neutral layout is exactly struct led_rgb and the driver applies
 * color-mapping itself. The scratch byte some drivers ask for breaks that. */
#ifdef CONFIG_LED_STRIP_RGB_SCRATCH
#error "CONFIG_LED_STRIP_RGB_SCRATCH prefixes struct led_rgb with a byte; the buffer cannot be passed through"
#endif
static_assert(sizeof(struct led_rgb) == Strip::bytes_per_pixel,
              "struct led_rgb must match the library's neutral RGB pixel");

#define STRIPSEQ_LED_STRIP_DT_GET(node_id) \
    (::stripseq::zephyr::LedStrip{DEVICE_DT_GET(node_id)})

#endif

/* Sends the frame the library has just rendered into buffer, and returns what
 * the driver returned. The buffer's size is deduced, as Strip's constructor
 * deduces it, so the pixel count cannot drift from the array.
 *
 * On the RGBW path the bytes are permuted in place, within each pixel, so no
 * second buffer is needed; they are left in the chip's order afterwards, and
 * the next render overwrites them in full. */
template <size_t B>
int update(const LedStrip& out, uint8_t (&buffer)[B]) {
    static_assert(B % Strip::bytes_per_pixel == 0,
                  "Buffer size is not a whole number of pixels -- "
                  "check it against Strip::bytes_per_pixel.");
#if STRIPSEQ_WHITE_CHANNEL
    for (size_t p = 0; p < B; p += 4) {
        uint8_t* px = buffer + p;
        const uint8_t neutral[4] = {px[0], px[1], px[2], px[3]};
        for (size_t j = 0; j < 4; ++j)
            px[j] = neutral[out.from[j]];
    }
    return led_strip_update_channels(out.dev, buffer, B);
#else
    return led_strip_update_rgb(out.dev, reinterpret_cast<struct led_rgb*>(buffer),
                                B / Strip::bytes_per_pixel);
#endif
}

}  // namespace stripseq::zephyr

#endif  /* STRIPSEQ_ZEPHYR_LED_STRIP_H_ */
