/*
 * Copyright (c) 2026 Esben Dueholm Nørgaard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The adapter between the library's neutral buffer and a Zephyr led_strip
 * driver, shared by every sample that drives the strip.
 *
 * The strip is the devicetree's led-strip alias. send_frame() hands it one
 * rendered frame, on whichever path the channel setting selects: an in-place
 * permute into the chip's order and led_strip_update_channels(), or the buffer
 * as it stands to led_strip_update_rgb().
 */

#pragma once

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/dt-bindings/led/led.h>

#include <stripseq/stripseq.h>

#define STRIP_ADAPTER_NODE DT_ALIAS(led_strip)

#if STRIPSEQ_WHITE_CHANNEL

/* led_strip_update_channels() takes bytes already in the chip's own order and
 * consults color-mapping nowhere, so the order is read from the devicetree
 * here instead of being written down a second time. */
inline constexpr uint8_t kWireOrder[] = DT_PROP(STRIP_ADAPTER_NODE, color_mapping);

/* Where each LED_COLOR_ID sits in the library's neutral R,G,B,W pixel. */
constexpr uint8_t neutral_index(uint8_t id) {
    return id == LED_COLOR_ID_WHITE ? 3 : static_cast<uint8_t>(id - LED_COLOR_ID_RED);
}

constexpr bool is_permutation_of_rgbw(const uint8_t (&order)[4]) {
    bool seen[4] = {};
    for (uint8_t id : order) {
        if (id != LED_COLOR_ID_WHITE && (id < LED_COLOR_ID_RED || id > LED_COLOR_ID_BLUE))
            return false;
        if (seen[neutral_index(id)])
            return false;
        seen[neutral_index(id)] = true;
    }
    return true;
}

static_assert(sizeof(kWireOrder) == stripseq::Strip::bytes_per_pixel,
              "color-mapping must name exactly as many channels as an RGBW pixel has");
static_assert(is_permutation_of_rgbw(kWireOrder),
              "color-mapping must name red, green, blue and white once each");

/* Within each pixel, so the one buffer serves both orders and the library's
 * next render overwrites the permuted bytes in full. */
inline void permute_in_place(uint8_t* pixels, uint16_t count) {
    for (uint16_t p = 0; p < count; ++p) {
        uint8_t* px = pixels + p * stripseq::Strip::bytes_per_pixel;
        const uint8_t neutral[4] = {px[0], px[1], px[2], px[3]};
        for (uint8_t j = 0; j < 4; ++j)
            px[j] = neutral[neutral_index(kWireOrder[j])];
    }
}

#else

/* The RGB path hands the buffer to led_strip_update_rgb() as it stands, since
 * the neutral layout is exactly struct led_rgb and the driver applies
 * color-mapping itself. The scratch byte some drivers ask for breaks that. */
#ifdef CONFIG_LED_STRIP_RGB_SCRATCH
#error "CONFIG_LED_STRIP_RGB_SCRATCH prefixes struct led_rgb with a byte; the buffer cannot be passed through"
#endif
static_assert(sizeof(struct led_rgb) == stripseq::Strip::bytes_per_pixel,
              "struct led_rgb must match the library's neutral RGB pixel");

#endif

/* Sends a frame the library has just rendered into buffer. On the RGBW path
 * the bytes are left in the chip's order, which the next render overwrites. */
inline int send_frame(const struct device* dev, uint8_t* buffer, uint16_t pixels) {
#if STRIPSEQ_WHITE_CHANNEL
    permute_in_place(buffer, pixels);
    return led_strip_update_channels(dev, buffer, size_t{pixels} * stripseq::Strip::bytes_per_pixel);
#else
    return led_strip_update_rgb(dev, reinterpret_cast<struct led_rgb*>(buffer), pixels);
#endif
}
