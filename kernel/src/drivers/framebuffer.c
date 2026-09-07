/*
 * SzpontOS - Higher-Half Framebuffer Console & ANSI/VT100 Terminal Driver
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <drivers/framebuffer.h>
#include <drivers/font8x16.h>
#include <kernel/string.h>
#include <kernel/kprint.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <arch/x86_64/io.h>
#include <drivers/keyboard.h>

static struct limine_framebuffer *g_fb = NULL;
static uint32_t *g_fb_ptr = NULL;
static uint32_t *g_backbuffer = NULL;
static size_t g_fb_width = 0;
static size_t g_fb_height = 0;
static size_t g_fb_pitch_pixels = 0;

/* Alternate Screen Buffer */
static uint32_t *g_alt_screen_buf = NULL;
static bool g_in_alt_screen = false;

/* Console Grid Geometry */
static size_t g_scale = 1;
static size_t g_char_w = 8;
static size_t g_char_h = 16;
static size_t g_cols = 0;
static size_t g_rows = 0;

/* Cursor Position & Attributes */
static size_t g_cursor_x = 0;
static size_t g_cursor_y = 0;
static size_t g_saved_cursor_x = 0;
static size_t g_saved_cursor_y = 0;
static bool g_cursor_visible = true;
static bool g_autowrap = true;

/* Scroll Region (DECSTBM) */
static size_t g_scroll_top = 0;
static size_t g_scroll_bottom = 0;

/* Color & Text Rendition State */
static uint32_t g_fg_color = FB_COLOR_WHITE;
static uint32_t g_bg_color = FB_COLOR_BG;
static uint32_t g_default_fg = FB_COLOR_WHITE;
static uint32_t g_default_bg = FB_COLOR_BG;
static bool g_bold = false;
static bool g_reverse = false;
static bool g_underline = false;
static int g_fg_code = -1;
static int g_bg_code = -1;

/* Character Set / Graphics State */
static bool g_dec_graphics_g0 = false;
static bool g_dec_graphics_g1 = false;
static bool g_use_g1 = false;

/* ANSI Parser State Machine */
enum ansi_state {
    ANSI_STATE_NORMAL = 0,
    ANSI_STATE_ESC,
    ANSI_STATE_CSI,
    ANSI_STATE_OSC,
    ANSI_STATE_SCS_G0,
    ANSI_STATE_SCS_G1
};

#define MAX_ANSI_PARAMS 16
static enum ansi_state g_ansi_state = ANSI_STATE_NORMAL;
static int g_ansi_params[MAX_ANSI_PARAMS];
static size_t g_ansi_param_count = 0;
static bool g_ansi_has_param = false;
static bool g_ansi_private = false;

/* UTF-8 Decoding State */
static uint32_t g_utf8_codepoint = 0;
static int g_utf8_bytes_needed = 0;

/* 16 Standard ANSI Colors (Normal & Bright) */
static const uint32_t g_ansi_colors_normal[8] = {
    0x00151828, /* 0: Black */
    0x00ff5370, /* 1: Red */
    0x00c3e88d, /* 2: Green */
    0x00ffcb6b, /* 3: Yellow */
    0x0082aaff, /* 4: Blue */
    0x00c792ea, /* 5: Magenta */
    0x0089ddff, /* 6: Cyan */
    0x00d0d0d0  /* 7: White / Light Gray */
};

static const uint32_t g_ansi_colors_bright[8] = {
    0x00717cb4, /* 0: Bright Black (Gray) */
    0x00ff6e8a, /* 1: Bright Red */
    0x00d5ff99, /* 2: Bright Green */
    0x00ffe585, /* 3: Bright Yellow */
    0x009ec4ff, /* 4: Bright Blue */
    0x00e1adff, /* 5: Bright Magenta */
    0x00a8eeff, /* 6: Bright Cyan */
    0x00ffffff  /* 7: Bright White */
};

/* 256-Color Lookup Helper */
static uint32_t ansi_256_to_rgb(int idx) {
    if (idx < 0)
        return FB_COLOR_WHITE;
    if (idx < 8)
        return g_ansi_colors_normal[idx];
    if (idx < 16)
        return g_ansi_colors_bright[idx - 8];
    if (idx >= 16 && idx <= 231) {
        /* 6x6x6 color cube */
        int code = idx - 16;
        int b = code % 6;
        int g = (code / 6) % 6;
        int r = code / 36;
        static const uint8_t levels[6] = {0x00, 0x5f, 0x87, 0xaf, 0xd7, 0xff};
        return ((uint32_t)levels[r] << 16) | ((uint32_t)levels[g] << 8) | (uint32_t)levels[b];
    }
    if (idx >= 232 && idx <= 255) {
        /* Grayscale ramp */
        uint8_t gray = (uint8_t)(8 + (idx - 232) * 10);
        return ((uint32_t)gray << 16) | ((uint32_t)gray << 8) | (uint32_t)gray;
    }
    return FB_COLOR_WHITE;
}

/* DEC Special Graphics Translation (CP437) */
static uint8_t map_dec_graphics(char c) {
    switch (c) {
    case '`':
        return 0x04; /* Diamond ◆ */
    case 'a':
        return 0xB1; /* Checkerboard ▒ */
    case 'f':
        return 0xF8; /* Degree ° */
    case 'g':
        return 0xF1; /* Plus/minus ± */
    case 'j':
        return 0xD9; /* Lower right corner ┘ */
    case 'k':
        return 0xBF; /* Upper right corner ┐ */
    case 'l':
        return 0xDA; /* Upper left corner ┌ */
    case 'm':
        return 0xC0; /* Lower left corner └ */
    case 'n':
        return 0xC5; /* Crossing lines ┼ */
    case 'o':
        return 0xC4; /* Horizontal line ─ */
    case 'p':
        return 0xC4; /* Horizontal line ─ */
    case 'q':
        return 0xC4; /* Horizontal line ─ */
    case 'r':
        return 0xC4; /* Horizontal line ─ */
    case 's':
        return 0x5F; /* Underscore _ */
    case 't':
        return 0xC3; /* Left tee ├ */
    case 'u':
        return 0xB4; /* Right tee ┤ */
    case 'v':
        return 0xC1; /* Bottom tee ┴ */
    case 'w':
        return 0xC2; /* Top tee ┬ */
    case 'x':
        return 0xB3; /* Vertical line │ */
    case 'y':
        return 0xF3; /* Less than or equal ≤ */
    case 'z':
        return 0xF2; /* Greater than or equal ≥ */
    case '{':
        return 0xE3; /* Pi π */
    case '|':
        return 0xD8; /* Not equal ≠ */
    case '}':
        return 0x9C; /* Pound £ */
    case '~':
        return 0xFA; /* Bullet · */
    default:
        return (uint8_t)c;
    }
}

/* Unicode Code Point to CP437 Mapping */
static uint8_t unicode_to_cp437(uint32_t cp) {
    if (cp <= 0x7F)
        return (uint8_t)cp;

    /* Unicode Box Drawing (0x2500 - 0x257F) */
    switch (cp) {
    case 0x2500:
    case 0x2501:
        return 0xC4; /* ─ ━ */
    case 0x2502:
    case 0x2503:
        return 0xB3; /* │ ┃ */
    case 0x250C:
    case 0x250F:
        return 0xDA; /* ┌ ┏ */
    case 0x2510:
    case 0x2513:
        return 0xBF; /* ┐ ┓ */
    case 0x2514:
    case 0x2517:
        return 0xC0; /* └ ┗ */
    case 0x2518:
    case 0x251B:
        return 0xD9; /* ┘ ┛ */
    case 0x251C:
    case 0x2523:
        return 0xC3; /* ├ ┣ */
    case 0x2524:
    case 0x252B:
        return 0xB4; /* ┤ ┫ */
    case 0x252C:
    case 0x2533:
        return 0xC2; /* ┬ ┳ */
    case 0x2534:
    case 0x253B:
        return 0xC1; /* ┴ ┻ */
    case 0x253C:
    case 0x254B:
        return 0xC5; /* ┼ ╋ */

    /* Double Box Drawing */
    case 0x2550:
        return 0xCD; /* ═ */
    case 0x2551:
        return 0xBA; /* ║ */
    case 0x2554:
        return 0xC9; /* ╔ */
    case 0x2557:
        return 0xBB; /* ╗ */
    case 0x255A:
        return 0xC8; /* ╚ */
    case 0x255D:
        return 0xBC; /* ╝ */
    case 0x2560:
        return 0xCC; /* ╠ */
    case 0x2563:
        return 0xB9; /* ╣ */
    case 0x2566:
        return 0xCB; /* ╦ */
    case 0x2569:
        return 0xCA; /* ╩ */
    case 0x256C:
        return 0xCE; /* ╬ */

    /* Unicode Block Elements (0x2580 - 0x259F) */
    case 0x2588:
        return 0xDB; /* █ Full block */
    case 0x2580:
        return 0xDF; /* ▀ Upper half */
    case 0x2584:
        return 0xDC; /* ▄ Lower half */
    case 0x258C:
        return 0xDD; /* ▌ Left half */
    case 0x2590:
        return 0xDE; /* ▐ Right half */
    case 0x2591:
        return 0xB0; /* ░ Light shade */
    case 0x2592:
        return 0xB1; /* ▒ Medium shade */
    case 0x2593:
        return 0xB2; /* ▓ Dark shade */
    case 0x25A0:
        return 0xFE; /* ■ Black square */

    /* Punctuation and Symbols */
    case 0x2022:
    case 0x00B7:
        return 0xFA; /* • · */
    case 0x00B0:
        return 0xF8; /* ° */
    case 0x00B1:
        return 0xF1; /* ± */
    case 0x2264:
        return 0xF3; /* ≤ */
    case 0x2265:
        return 0xF2; /* ≥ */
    case 0x2191:
        return 0x18; /* ↑ */
    case 0x2193:
        return 0x19; /* ↓ */
    case 0x2192:
        return 0x1A; /* → */
    case 0x2190:
        return 0x1B; /* ← */
    default:
        return (cp < 256) ? (uint8_t)cp : '?';
    }
}

void framebuffer_init(struct limine_framebuffer *fb) {
    if (!fb || !fb->address)
        return;

    g_fb = fb;
    g_fb_ptr = (uint32_t *)fb->address;
    g_fb_width = fb->width;
    g_fb_height = fb->height;
    g_fb_pitch_pixels = fb->pitch / (fb->bpp / 8);

    /* For high-resolution displays (>= 1280px), use 2x scaling to provide standard 80-column layout */
    g_scale = (g_fb_width >= 1280) ? 2 : 1;
    g_char_w = FONT_WIDTH * g_scale;
    g_char_h = FONT_HEIGHT * g_scale;

    g_cols = g_fb_width / g_char_w;
    g_rows = g_fb_height / g_char_h;

    g_cursor_x = 0;
    g_cursor_y = 0;
    g_saved_cursor_x = 0;
    g_saved_cursor_y = 0;
    g_cursor_visible = true;
    g_autowrap = true;

    g_scroll_top = 0;
    g_scroll_bottom = (g_rows > 0) ? g_rows - 1 : 0;

    g_default_fg = FB_COLOR_WHITE;
    g_default_bg = FB_COLOR_BG;
    g_fg_color = g_default_fg;
    g_bg_color = g_default_bg;
    g_bold = false;
    g_reverse = false;
    g_underline = false;
    g_fg_code = -1;
    g_bg_code = -1;

    g_dec_graphics_g0 = false;
    g_dec_graphics_g1 = false;
    g_use_g1 = false;

    g_ansi_state = ANSI_STATE_NORMAL;
    g_utf8_bytes_needed = 0;
    g_utf8_codepoint = 0;

    fb_clear(g_bg_color);
}

bool framebuffer_is_available(void) {
    return g_fb_ptr != NULL;
}

size_t fb_get_width(void) {
    return g_fb_width;
}

size_t fb_get_height(void) {
    return g_fb_height;
}

size_t fb_get_cols(void) {
    return g_cols;
}

size_t fb_get_rows(void) {
    return g_rows;
}

size_t fb_get_pitch(void) {
    return g_fb ? g_fb->pitch : (g_fb_pitch_pixels * sizeof(uint32_t));
}

uint32_t fb_get_bpp(void) {
    return g_fb ? (uint32_t)g_fb->bpp : 32;
}

uint32_t *fb_get_fb_ptr(void) {
    return g_fb_ptr;
}

uint32_t *fb_get_backbuffer_ptr(void) {
    return g_backbuffer;
}

struct limine_framebuffer *fb_get_limine(void) {
    return g_fb;
}

static bool g_graphics_mode = false;

void fb_set_graphics_mode(bool enabled) {
    g_graphics_mode = enabled;
}

bool fb_is_graphics_mode(void) {
    return g_graphics_mode;
}

void fb_blit_from_buffer(const uint32_t *src, size_t src_pitch_pixels, size_t dst_x, size_t dst_y, size_t w, size_t h) {
    if (!src || !g_fb_ptr)
        return;
    if (dst_x >= g_fb_width || dst_y >= g_fb_height)
        return;
    if (dst_x + w > g_fb_width)
        w = g_fb_width - dst_x;
    if (dst_y + h > g_fb_height)
        h = g_fb_height - dst_y;

    size_t copy_bytes = w * sizeof(uint32_t);
    for (size_t row = 0; row < h; row++) {
        const uint32_t *src_row = src + row * src_pitch_pixels;
        uint32_t *dst_row = g_fb_ptr + (dst_y + row) * g_fb_pitch_pixels + dst_x;
        memcpy(dst_row, src_row, copy_bytes);
    }
}

static bool g_fb_dirty = false;
static size_t g_dirty_min_x = 0;
static size_t g_dirty_min_y = 0;
static size_t g_dirty_max_x = 0;
static size_t g_dirty_max_y = 0;

static inline void fb_mark_dirty(size_t x, size_t y, size_t w, size_t h) {
    if (!g_fb_dirty) {
        g_dirty_min_x = x;
        g_dirty_min_y = y;
        g_dirty_max_x = x + w;
        g_dirty_max_y = y + h;
        g_fb_dirty = true;
    } else {
        if (x < g_dirty_min_x)
            g_dirty_min_x = x;
        if (y < g_dirty_min_y)
            g_dirty_min_y = y;
        if (x + w > g_dirty_max_x)
            g_dirty_max_x = x + w;
        if (y + h > g_dirty_max_y)
            g_dirty_max_y = y + h;
    }
}

static inline void fb_flush_rect(size_t x, size_t y, size_t w, size_t h) {
    if (!g_fb_ptr || !g_backbuffer)
        return;
    if (x >= g_fb_width || y >= g_fb_height)
        return;
    if (x + w > g_fb_width)
        w = g_fb_width - x;
    if (y + h > g_fb_height)
        h = g_fb_height - y;

    size_t bytes = w * sizeof(uint32_t);
    for (size_t row = 0; row < h; row++) {
        size_t offset = (y + row) * g_fb_pitch_pixels + x;
        memcpy(&g_fb_ptr[offset], &g_backbuffer[offset], bytes);
    }
}

void fb_flush(void) {
    if (g_graphics_mode)
        return;
    if (!g_fb_dirty || !g_fb_ptr || !g_backbuffer)
        return;
    if (g_dirty_max_x > g_fb_width)
        g_dirty_max_x = g_fb_width;
    if (g_dirty_max_y > g_fb_height)
        g_dirty_max_y = g_fb_height;
    if (g_dirty_max_x > g_dirty_min_x && g_dirty_max_y > g_dirty_min_y) {
        fb_flush_rect(g_dirty_min_x, g_dirty_min_y, g_dirty_max_x - g_dirty_min_x, g_dirty_max_y - g_dirty_min_y);
    }
    g_fb_dirty = false;
}

void fb_put_pixel(size_t x, size_t y, uint32_t color) {
    if (!g_fb_ptr || x >= g_fb_width || y >= g_fb_height)
        return;
    size_t offset = y * g_fb_pitch_pixels + x;
    if (g_backbuffer) {
        g_backbuffer[offset] = color;
        fb_mark_dirty(x, y, 1, 1);
    } else {
        g_fb_ptr[offset] = color;
    }
}

static inline void fb_fill_pixels_32(uint32_t *dst, uint32_t color, size_t count) {
    if (!dst || count == 0)
        return;
    uint64_t color64 = ((uint64_t)color << 32) | (uint64_t)color;
    size_t qwords = count >> 1;
    uint64_t *d64 = (uint64_t *)dst;
    for (size_t i = 0; i < qwords; i++) {
        d64[i] = color64;
    }
    if (count & 1) {
        dst[count - 1] = color;
    }
}

void fb_fill_rect(size_t x, size_t y, size_t w, size_t h, uint32_t color) {
    if (!g_fb_ptr || w == 0 || h == 0)
        return;
    if (x >= g_fb_width || y >= g_fb_height)
        return;
    if (x + w > g_fb_width)
        w = g_fb_width - x;
    if (y + h > g_fb_height)
        h = g_fb_height - y;

    if (g_backbuffer) {
        for (size_t row = 0; row < h; row++) {
            uint32_t *dst = &g_backbuffer[(y + row) * g_fb_pitch_pixels + x];
            fb_fill_pixels_32(dst, color, w);
        }
        fb_mark_dirty(x, y, w, h);
    } else {
        for (size_t row = 0; row < h; row++) {
            uint32_t *dst = &g_fb_ptr[(y + row) * g_fb_pitch_pixels + x];
            fb_fill_pixels_32(dst, color, w);
        }
    }
}

void fb_clear(uint32_t color) {
    if (!g_fb_ptr)
        return;
    size_t total_pixels = g_fb_height * g_fb_pitch_pixels;
    if (g_backbuffer) {
        fb_fill_pixels_32(g_backbuffer, color, total_pixels);
        fb_fill_pixels_32(g_fb_ptr, color, total_pixels);
    } else {
        fb_fill_pixels_32(g_fb_ptr, color, total_pixels);
    }
    g_fb_dirty = false;
    g_cursor_x = 0;
    g_cursor_y = 0;
}

static void fb_draw_char_raw(size_t col, size_t row, uint8_t uc, uint32_t fg, uint32_t bg, bool underline) {
    if (!g_fb_ptr || col >= g_cols || row >= g_rows)
        return;

    const uint8_t *glyph = g_font_8x16[uc];
    size_t px = col * g_char_w;
    size_t py = row * g_char_h;

    uint32_t *target = g_backbuffer ? g_backbuffer : g_fb_ptr;

    if (g_scale == 1) {
        for (size_t y = 0; y < FONT_HEIGHT; y++) {
            uint8_t line = glyph[y];
            if (underline && (y == FONT_HEIGHT - 2 || y == FONT_HEIGHT - 1)) {
                line = 0xFF;
            }
            uint32_t *dst = &target[(py + y) * g_fb_pitch_pixels + px];
            dst[0] = (line & 0x80) ? fg : bg;
            dst[1] = (line & 0x40) ? fg : bg;
            dst[2] = (line & 0x20) ? fg : bg;
            dst[3] = (line & 0x10) ? fg : bg;
            dst[4] = (line & 0x08) ? fg : bg;
            dst[5] = (line & 0x04) ? fg : bg;
            dst[6] = (line & 0x02) ? fg : bg;
            dst[7] = (line & 0x01) ? fg : bg;
        }
    } else if (g_scale == 2) {
        for (size_t y = 0; y < FONT_HEIGHT; y++) {
            uint8_t line = glyph[y];
            if (underline && (y == FONT_HEIGHT - 2 || y == FONT_HEIGHT - 1)) {
                line = 0xFF;
            }
            uint32_t *dst0 = &target[(py + y * 2) * g_fb_pitch_pixels + px];
            uint32_t *dst1 = &target[(py + y * 2 + 1) * g_fb_pitch_pixels + px];
            for (size_t x = 0; x < 8; x++) {
                uint32_t c = (line & (0x80 >> x)) ? fg : bg;
                dst0[x * 2] = c;
                dst0[x * 2 + 1] = c;
                dst1[x * 2] = c;
                dst1[x * 2 + 1] = c;
            }
        }
    } else {
        for (size_t y = 0; y < FONT_HEIGHT; y++) {
            uint8_t line = glyph[y];
            if (underline && (y == FONT_HEIGHT - 2 || y == FONT_HEIGHT - 1)) {
                line = 0xFF;
            }
            for (size_t sy = 0; sy < g_scale; sy++) {
                uint32_t *dst = &target[(py + y * g_scale + sy) * g_fb_pitch_pixels + px];
                for (size_t x = 0; x < FONT_WIDTH; x++) {
                    uint32_t color = ((line >> (7 - x)) & 1) ? fg : bg;
                    for (size_t sx = 0; sx < g_scale; sx++) {
                        dst[x * g_scale + sx] = color;
                    }
                }
            }
        }
    }

    if (g_backbuffer) {
        fb_mark_dirty(px, py, g_char_w, g_char_h);
        fb_flush_rect(px, py, g_char_w, g_char_h);
    }
}

static void fb_scroll_up_region(size_t top, size_t bottom) {
    if (!g_fb_ptr || top >= bottom || bottom >= g_rows)
        return;

    /* Flush pending drawing before scrolling */
    fb_flush();

    size_t line_height_px = g_char_h;
    size_t num_lines = bottom - top;
    size_t region_y = top * line_height_px;
    size_t region_h = (num_lines + 1) * line_height_px;

    if (g_backbuffer) {
        /* 1. Fast in-RAM line move */
        for (size_t row = 0; row < num_lines; row++) {
            size_t dst_y = (top + row) * line_height_px;
            size_t src_y = (top + row + 1) * line_height_px;
            for (size_t y = 0; y < line_height_px; y++) {
                uint32_t *dst = &g_backbuffer[(dst_y + y) * g_fb_pitch_pixels];
                uint32_t *src = &g_backbuffer[(src_y + y) * g_fb_pitch_pixels];
                memcpy(dst, src, g_fb_width * sizeof(uint32_t));
            }
        }

        /* 2. Clear bottom line in RAM */
        size_t bot_y = bottom * line_height_px;
        for (size_t y = 0; y < line_height_px; y++) {
            uint32_t *dst = &g_backbuffer[(bot_y + y) * g_fb_pitch_pixels];
            fb_fill_pixels_32(dst, g_bg_color, g_fb_width);
        }

        /* 3. Flush ONLY modified region to VRAM */
        fb_flush_rect(0, region_y, g_fb_width, region_h);
        g_fb_dirty = false;
    } else {
        /* Fallback if backbuffer not yet initialized */
        for (size_t row = 0; row < num_lines; row++) {
            size_t dst_y = (top + row) * line_height_px;
            size_t src_y = (top + row + 1) * line_height_px;
            for (size_t y = 0; y < line_height_px; y++) {
                uint32_t *dst = &g_fb_ptr[(dst_y + y) * g_fb_pitch_pixels];
                uint32_t *src = &g_fb_ptr[(src_y + y) * g_fb_pitch_pixels];
                memcpy(dst, src, g_fb_width * sizeof(uint32_t));
            }
        }
        fb_fill_rect(0, bottom * g_char_h, g_fb_width, g_char_h, g_bg_color);
    }
}

static void fb_scroll_down_region(size_t top, size_t bottom) {
    if (!g_fb_ptr || top >= bottom || bottom >= g_rows)
        return;

    /* Flush pending drawing before scrolling */
    fb_flush();

    size_t line_height_px = g_char_h;
    size_t num_lines = bottom - top;
    size_t region_y = top * line_height_px;
    size_t region_h = (num_lines + 1) * line_height_px;

    if (g_backbuffer) {
        /* 1. Fast in-RAM line move */
        for (size_t row = num_lines; row > 0; row--) {
            size_t dst_y = (top + row) * line_height_px;
            size_t src_y = (top + row - 1) * line_height_px;
            for (size_t y = 0; y < line_height_px; y++) {
                uint32_t *dst = &g_backbuffer[(dst_y + y) * g_fb_pitch_pixels];
                uint32_t *src = &g_backbuffer[(src_y + y) * g_fb_pitch_pixels];
                memcpy(dst, src, g_fb_width * sizeof(uint32_t));
            }
        }

        /* 2. Clear top line in RAM */
        size_t top_y = top * line_height_px;
        for (size_t y = 0; y < line_height_px; y++) {
            uint32_t *dst = &g_backbuffer[(top_y + y) * g_fb_pitch_pixels];
            fb_fill_pixels_32(dst, g_bg_color, g_fb_width);
        }

        /* 3. Flush ONLY modified region to VRAM */
        fb_flush_rect(0, region_y, g_fb_width, region_h);
        g_fb_dirty = false;
    } else {
        for (size_t row = num_lines; row > 0; row--) {
            size_t dst_y = (top + row) * line_height_px;
            size_t src_y = (top + row - 1) * line_height_px;
            for (size_t y = 0; y < line_height_px; y++) {
                uint32_t *dst = &g_fb_ptr[(dst_y + y) * g_fb_pitch_pixels];
                uint32_t *src = &g_fb_ptr[(src_y + y) * g_fb_pitch_pixels];
                memcpy(dst, src, g_fb_width * sizeof(uint32_t));
            }
        }
        fb_fill_rect(0, top * g_char_h, g_fb_width, g_char_h, g_bg_color);
    }
}

void fb_console_set_color(uint32_t fg, uint32_t bg) {
    g_fg_color = fg;
    g_bg_color = bg;
}

static void handle_csi_sgr(void) {
    if (g_ansi_param_count == 0) {
        g_fg_color = g_default_fg;
        g_bg_color = g_default_bg;
        g_bold = false;
        g_reverse = false;
        g_underline = false;
        g_fg_code = -1;
        g_bg_code = -1;
        return;
    }

    for (size_t i = 0; i < g_ansi_param_count; i++) {
        int code = g_ansi_params[i];

        if (code == 0) {
            /* Reset attributes */
            g_fg_color = g_default_fg;
            g_bg_color = g_default_bg;
            g_bold = false;
            g_reverse = false;
            g_underline = false;
            g_fg_code = -1;
            g_bg_code = -1;
        } else if (code == 1) {
            /* Bold / Bright */
            g_bold = true;
            if (g_fg_code >= 0 && g_fg_code < 8) {
                g_fg_color = g_ansi_colors_bright[g_fg_code];
            }
        } else if (code == 2 || code == 22) {
            /* Normal intensity */
            g_bold = false;
            if (g_fg_code >= 0 && g_fg_code < 8) {
                g_fg_color = g_ansi_colors_normal[g_fg_code];
            }
        } else if (code == 4) {
            /* Underline */
            g_underline = true;
        } else if (code == 24) {
            /* Underline off */
            g_underline = false;
        } else if (code == 7) {
            /* Reverse Video (Invert) */
            g_reverse = true;
        } else if (code == 27) {
            /* Reverse Video off */
            g_reverse = false;
        } else if (code >= 30 && code <= 37) {
            /* Standard Foreground */
            g_fg_code = code - 30;
            g_fg_color = g_bold ? g_ansi_colors_bright[g_fg_code] : g_ansi_colors_normal[g_fg_code];
        } else if (code == 38) {
            /* 256-color or 24-bit Truecolor Foreground */
            if (i + 2 < g_ansi_param_count && g_ansi_params[i + 1] == 5) {
                /* 38;5;n */
                g_fg_color = ansi_256_to_rgb(g_ansi_params[i + 2]);
                g_fg_code = -1;
                i += 2;
            } else if (i + 4 < g_ansi_param_count && g_ansi_params[i + 1] == 2) {
                /* 38;2;r;g;b */
                uint8_t r = (uint8_t)g_ansi_params[i + 2];
                uint8_t g = (uint8_t)g_ansi_params[i + 3];
                uint8_t b = (uint8_t)g_ansi_params[i + 4];
                g_fg_color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
                g_fg_code = -1;
                i += 4;
            }
        } else if (code == 39) {
            /* Default Foreground */
            g_fg_color = g_default_fg;
            g_fg_code = -1;
        } else if (code >= 40 && code <= 47) {
            /* Standard Background */
            g_bg_code = code - 40;
            g_bg_color = g_ansi_colors_normal[g_bg_code];
        } else if (code == 48) {
            /* 256-color or 24-bit Truecolor Background */
            if (i + 2 < g_ansi_param_count && g_ansi_params[i + 1] == 5) {
                /* 48;5;n */
                g_bg_color = ansi_256_to_rgb(g_ansi_params[i + 2]);
                g_bg_code = -1;
                i += 2;
            } else if (i + 4 < g_ansi_param_count && g_ansi_params[i + 1] == 2) {
                /* 48;2;r;g;b */
                uint8_t r = (uint8_t)g_ansi_params[i + 2];
                uint8_t g = (uint8_t)g_ansi_params[i + 3];
                uint8_t b = (uint8_t)g_ansi_params[i + 4];
                g_bg_color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
                g_bg_code = -1;
                i += 4;
            }
        } else if (code == 49) {
            /* Default Background */
            g_bg_color = g_default_bg;
            g_bg_code = -1;
        } else if (code >= 90 && code <= 97) {
            /* Bright Foreground */
            g_fg_code = code - 90;
            g_fg_color = g_ansi_colors_bright[g_fg_code];
        } else if (code >= 100 && code <= 107) {
            /* Bright Background */
            g_bg_code = code - 100;
            g_bg_color = g_ansi_colors_bright[g_bg_code];
        }
    }
}

static void handle_csi_command(char cmd) {
    if (g_ansi_has_param && g_ansi_param_count < MAX_ANSI_PARAMS) {
        g_ansi_param_count++;
    }

    if (g_ansi_private) {
        /* DEC Private Mode Commands */
        int mode = (g_ansi_param_count > 0) ? g_ansi_params[0] : 0;
        if (cmd == 'h') {
            /* Set Mode */
            if (mode == 25) {
                g_cursor_visible = true;
            } else if (mode == 7) {
                g_autowrap = true;
            } else if (mode == 1049 || mode == 47) {
                /* Switch to Alternate Screen Buffer */
                if (!g_in_alt_screen) {
                    if (!g_alt_screen_buf && g_fb_width > 0 && g_fb_height > 0) {
                        g_alt_screen_buf = (uint32_t *)kmalloc(g_fb_width * g_fb_height * sizeof(uint32_t));
                    }
                    if (g_alt_screen_buf) {
                        uint32_t *src_buf = g_backbuffer ? g_backbuffer : g_fb_ptr;
                        for (size_t y = 0; y < g_fb_height; y++) {
                            memcpy(&g_alt_screen_buf[y * g_fb_width], &src_buf[y * g_fb_pitch_pixels],
                                   g_fb_width * sizeof(uint32_t));
                        }
                    }
                    g_in_alt_screen = true;
                    fb_clear(g_bg_color);
                }
            }
        } else if (cmd == 'l') {
            /* Reset Mode */
            if (mode == 25) {
                g_cursor_visible = false;
            } else if (mode == 7) {
                g_autowrap = false;
            } else if (mode == 1049 || mode == 47) {
                /* Restore Main Screen Buffer */
                if (g_in_alt_screen) {
                    if (g_alt_screen_buf) {
                        uint32_t *dst_buf = g_backbuffer ? g_backbuffer : g_fb_ptr;
                        for (size_t y = 0; y < g_fb_height; y++) {
                            memcpy(&dst_buf[y * g_fb_pitch_pixels], &g_alt_screen_buf[y * g_fb_width],
                                   g_fb_width * sizeof(uint32_t));
                        }
                        if (g_backbuffer) {
                            fb_flush_rect(0, 0, g_fb_width, g_fb_height);
                        }
                    }
                    g_in_alt_screen = false;
                }
            }
        } else if (cmd == 'n') {
            if (mode == 6) {
                /* DECCPR (Cursor Position Report) */
                char cpr[32];
                ksnprintf(cpr, sizeof(cpr), "\033[%d;%dR", (int)g_cursor_y + 1, (int)g_cursor_x + 1);
                keyboard_push_str(cpr);
            } else if (mode == 5) {
                keyboard_push_str("\033[0n");
            }
        } else if (cmd == 'c') {
            /* Secondary Device Attributes (DA2) */
            keyboard_push_str("\033[>0;10;0c");
        }
        return;
    }

    switch (cmd) {
    case 'm':
        handle_csi_sgr();
        break;

    case 'H':
    case 'f': {
        /* Cursor Position: \033[row;colH (1-indexed) */
        int row = (g_ansi_param_count > 0 && g_ansi_params[0] > 0) ? g_ansi_params[0] - 1 : 0;
        int col = (g_ansi_param_count > 1 && g_ansi_params[1] > 0) ? g_ansi_params[1] - 1 : 0;
        g_cursor_y = (row < (int)g_rows) ? (size_t)row : g_rows - 1;
        g_cursor_x = (col < (int)g_cols) ? (size_t)col : g_cols - 1;
        break;
    }

    case 'A': {
        /* Cursor Up */
        int count = (g_ansi_param_count > 0 && g_ansi_params[0] > 0) ? g_ansi_params[0] : 1;
        g_cursor_y = (g_cursor_y >= (size_t)count) ? g_cursor_y - count : 0;
        break;
    }

    case 'B': {
        /* Cursor Down */
        int count = (g_ansi_param_count > 0 && g_ansi_params[0] > 0) ? g_ansi_params[0] : 1;
        g_cursor_y += count;
        if (g_cursor_y >= g_rows)
            g_cursor_y = g_rows - 1;
        break;
    }

    case 'C': {
        /* Cursor Forward */
        int count = (g_ansi_param_count > 0 && g_ansi_params[0] > 0) ? g_ansi_params[0] : 1;
        g_cursor_x += count;
        if (g_cursor_x >= g_cols)
            g_cursor_x = g_cols - 1;
        break;
    }

    case 'D': {
        /* Cursor Backward */
        int count = (g_ansi_param_count > 0 && g_ansi_params[0] > 0) ? g_ansi_params[0] : 1;
        g_cursor_x = (g_cursor_x >= (size_t)count) ? g_cursor_x - count : 0;
        break;
    }

    case 'E': {
        /* Cursor Next Line */
        int count = (g_ansi_param_count > 0 && g_ansi_params[0] > 0) ? g_ansi_params[0] : 1;
        g_cursor_x = 0;
        g_cursor_y += count;
        if (g_cursor_y >= g_rows)
            g_cursor_y = g_rows - 1;
        break;
    }

    case 'F': {
        /* Cursor Previous Line */
        int count = (g_ansi_param_count > 0 && g_ansi_params[0] > 0) ? g_ansi_params[0] : 1;
        g_cursor_x = 0;
        g_cursor_y = (g_cursor_y >= (size_t)count) ? g_cursor_y - count : 0;
        break;
    }

    case 'G':
    case '`': {
        /* Cursor Horizontal Absolute (Column) */
        int col = (g_ansi_param_count > 0 && g_ansi_params[0] > 0) ? g_ansi_params[0] - 1 : 0;
        g_cursor_x = (col < (int)g_cols) ? (size_t)col : g_cols - 1;
        break;
    }

    case 'd': {
        /* Vertical Position Absolute (Row) */
        int row = (g_ansi_param_count > 0 && g_ansi_params[0] > 0) ? g_ansi_params[0] - 1 : 0;
        g_cursor_y = (row < (int)g_rows) ? (size_t)row : g_rows - 1;
        break;
    }

    case 'J': {
        /* Erase in Display */
        int mode = (g_ansi_param_count > 0) ? g_ansi_params[0] : 0;
        if (mode == 2 || mode == 3) {
            /* Clear entire screen */
            fb_clear(g_bg_color);
            g_cursor_x = 0;
            g_cursor_y = 0;
        } else if (mode == 0) {
            /* Clear from cursor to end of screen */
            if (g_cursor_x < g_cols) {
                fb_fill_rect(g_cursor_x * g_char_w, g_cursor_y * g_char_h, (g_cols - g_cursor_x) * g_char_w, g_char_h,
                             g_bg_color);
            }
            if (g_cursor_y + 1 < g_rows) {
                fb_fill_rect(0, (g_cursor_y + 1) * g_char_h, g_fb_width, (g_rows - g_cursor_y - 1) * g_char_h,
                             g_bg_color);
            }
        } else if (mode == 1) {
            /* Clear from start of screen to cursor */
            if (g_cursor_y > 0) {
                fb_fill_rect(0, 0, g_fb_width, g_cursor_y * g_char_h, g_bg_color);
            }
            fb_fill_rect(0, g_cursor_y * g_char_h, (g_cursor_x + 1) * g_char_w, g_char_h, g_bg_color);
        }
        break;
    }

    case 'K': {
        /* Erase in Line */
        int mode = (g_ansi_param_count > 0) ? g_ansi_params[0] : 0;
        if (mode == 0) {
            /* Clear cursor to end of line */
            if (g_cursor_x < g_cols) {
                fb_fill_rect(g_cursor_x * g_char_w, g_cursor_y * g_char_h, (g_cols - g_cursor_x) * g_char_w, g_char_h,
                             g_bg_color);
            }
        } else if (mode == 1) {
            /* Clear start of line to cursor */
            fb_fill_rect(0, g_cursor_y * g_char_h, (g_cursor_x + 1) * g_char_w, g_char_h, g_bg_color);
        } else if (mode == 2) {
            /* Clear entire line */
            fb_fill_rect(0, g_cursor_y * g_char_h, g_fb_width, g_char_h, g_bg_color);
        }
        break;
    }

    case 'X': {
        /* Erase Characters */
        int count = (g_ansi_param_count > 0 && g_ansi_params[0] > 0) ? g_ansi_params[0] : 1;
        size_t w = (g_cursor_x + count <= g_cols) ? (size_t)count : (g_cols - g_cursor_x);
        fb_fill_rect(g_cursor_x * g_char_w, g_cursor_y * g_char_h, w * g_char_w, g_char_h, g_bg_color);
        break;
    }

    case 'L': {
        /* Insert Line (IL) */
        int count = (g_ansi_param_count > 0 && g_ansi_params[0] > 0) ? g_ansi_params[0] : 1;
        if (g_cursor_y >= g_scroll_top && g_cursor_y <= g_scroll_bottom) {
            for (int i = 0; i < count; i++) {
                fb_scroll_down_region(g_cursor_y, g_scroll_bottom);
            }
        }
        break;
    }

    case 'M': {
        /* Delete Line (DL) */
        int count = (g_ansi_param_count > 0 && g_ansi_params[0] > 0) ? g_ansi_params[0] : 1;
        if (g_cursor_y >= g_scroll_top && g_cursor_y <= g_scroll_bottom) {
            for (int i = 0; i < count; i++) {
                fb_scroll_up_region(g_cursor_y, g_scroll_bottom);
            }
        }
        break;
    }

    case '@': {
        /* Insert Characters (ICH) */
        int count = (g_ansi_param_count > 0 && g_ansi_params[0] > 0) ? g_ansi_params[0] : 1;
        if (g_cursor_x < g_cols && count > 0) {
            size_t shift = (size_t)count;
            if (g_cursor_x + shift < g_cols) {
                for (size_t x = g_cols - 1; x >= g_cursor_x + shift; x--) {
                    /* Copy pixel column */
                    for (size_t y = 0; y < g_char_h; y++) {
                        uint32_t *row = &g_fb_ptr[(g_cursor_y * g_char_h + y) * g_fb_pitch_pixels];
                        for (size_t sx = 0; sx < g_char_w; sx++) {
                            row[x * g_char_w + sx] = row[(x - shift) * g_char_w + sx];
                        }
                    }
                }
            }
            fb_fill_rect(g_cursor_x * g_char_w, g_cursor_y * g_char_h, shift * g_char_w, g_char_h, g_bg_color);
        }
        break;
    }

    case 'P': {
        /* Delete Characters (DCH) */
        int count = (g_ansi_param_count > 0 && g_ansi_params[0] > 0) ? g_ansi_params[0] : 1;
        if (g_cursor_x < g_cols && count > 0) {
            size_t shift = (size_t)count;
            if (g_cursor_x + shift < g_cols) {
                for (size_t x = g_cursor_x; x + shift < g_cols; x++) {
                    for (size_t y = 0; y < g_char_h; y++) {
                        uint32_t *row = &g_fb_ptr[(g_cursor_y * g_char_h + y) * g_fb_pitch_pixels];
                        for (size_t sx = 0; sx < g_char_w; sx++) {
                            row[x * g_char_w + sx] = row[(x + shift) * g_char_w + sx];
                        }
                    }
                }
                fb_fill_rect((g_cols - shift) * g_char_w, g_cursor_y * g_char_h, shift * g_char_w, g_char_h,
                             g_bg_color);
            } else {
                fb_fill_rect(g_cursor_x * g_char_w, g_cursor_y * g_char_h, (g_cols - g_cursor_x) * g_char_w, g_char_h,
                             g_bg_color);
            }
        }
        break;
    }

    case 'r': {
        /* Set Top and Bottom Margins (DECSTBM) */
        int top = (g_ansi_param_count > 0 && g_ansi_params[0] > 0) ? g_ansi_params[0] - 1 : 0;
        int bottom = (g_ansi_param_count > 1 && g_ansi_params[1] > 0) ? g_ansi_params[1] - 1 : (int)(g_rows - 1);
        if (top >= 0 && top < (int)g_rows && bottom > top && bottom < (int)g_rows) {
            g_scroll_top = (size_t)top;
            g_scroll_bottom = (size_t)bottom;
        } else {
            g_scroll_top = 0;
            g_scroll_bottom = (g_rows > 0) ? g_rows - 1 : 0;
        }
        g_cursor_x = 0;
        g_cursor_y = g_scroll_top;
        break;
    }

    case 's': {
        /* Save Cursor Position */
        g_saved_cursor_x = g_cursor_x;
        g_saved_cursor_y = g_cursor_y;
        break;
    }

    case 'u': {
        /* Restore Cursor Position */
        g_cursor_x = (g_saved_cursor_x < g_cols) ? g_saved_cursor_x : g_cols - 1;
        g_cursor_y = (g_saved_cursor_y < g_rows) ? g_saved_cursor_y : g_rows - 1;
        break;
    }

    case 'n': {
        int mode = (g_ansi_param_count > 0) ? g_ansi_params[0] : 0;
        if (mode == 6) {
            /* CPR (Cursor Position Report): \033[<row>;<col>R (1-indexed) */
            char cpr[32];
            ksnprintf(cpr, sizeof(cpr), "\033[%d;%dR", (int)g_cursor_y + 1, (int)g_cursor_x + 1);
            keyboard_push_str(cpr);
        } else if (mode == 5) {
            /* Status report: OK -> \033[0n */
            keyboard_push_str("\033[0n");
        }
        break;
    }

    case 'c': {
        /* Primary Device Attributes (DA1): \033[?1;2c (VT100 with AVO) */
        keyboard_push_str("\033[?1;2c");
        break;
    }

    case 't': {
        /* Window manipulation / size query */
        int mode = (g_ansi_param_count > 0) ? g_ansi_params[0] : 0;
        if (mode == 18) {
            /* Report size in characters: \033[8;<rows>;<cols>t */
            char buf[32];
            ksnprintf(buf, sizeof(buf), "\033[8;%d;%dt", (int)g_rows, (int)g_cols);
            keyboard_push_str(buf);
        } else if (mode == 14) {
            /* Report size in pixels: \033[4;<height>;<width>t */
            char buf[32];
            ksnprintf(buf, sizeof(buf), "\033[4;%d;%dt", (int)g_fb_height, (int)g_fb_width);
            keyboard_push_str(buf);
        }
        break;
    }

    default:
        break;
    }
}

static void render_glyph(uint8_t glyph_idx) {
    uint32_t fg = g_reverse ? g_bg_color : g_fg_color;
    uint32_t bg = g_reverse ? g_fg_color : g_bg_color;

    fb_draw_char_raw(g_cursor_x, g_cursor_y, glyph_idx, fg, bg, g_underline);
    g_cursor_x++;

    if (g_cursor_x >= g_cols) {
        if (g_autowrap) {
            g_cursor_x = 0;
            g_cursor_y++;
            if (g_cursor_y > g_scroll_bottom) {
                fb_scroll_up_region(g_scroll_top, g_scroll_bottom);
                g_cursor_y = g_scroll_bottom;
            }
        } else {
            g_cursor_x = g_cols - 1;
        }
    }
}

static void fb_console_putc_internal(char c) {
    if (!g_fb_ptr || g_graphics_mode)
        return;

    /* UTF-8 Multi-byte Decoding */
    uint8_t byte = (uint8_t)c;
    if (g_utf8_bytes_needed > 0) {
        if ((byte & 0xC0) == 0x80) {
            g_utf8_codepoint = (g_utf8_codepoint << 6) | (byte & 0x3F);
            g_utf8_bytes_needed--;
            if (g_utf8_bytes_needed == 0) {
                uint8_t glyph = unicode_to_cp437(g_utf8_codepoint);
                render_glyph(glyph);
            }
            return;
        } else {
            /* Invalid continuation byte, reset */
            g_utf8_bytes_needed = 0;
        }
    } else if (byte >= 0xC0 && byte <= 0xFD) {
        if ((byte & 0xE0) == 0xC0) {
            g_utf8_codepoint = byte & 0x1F;
            g_utf8_bytes_needed = 1;
        } else if ((byte & 0xF0) == 0xE0) {
            g_utf8_codepoint = byte & 0x0F;
            g_utf8_bytes_needed = 2;
        } else if ((byte & 0xF8) == 0xF0) {
            g_utf8_codepoint = byte & 0x07;
            g_utf8_bytes_needed = 3;
        }
        return;
    }

    /* ANSI Escape Sequence State Machine */
    if (g_ansi_state == ANSI_STATE_NORMAL) {
        if (c == 0x1B || c == '\033') {
            g_ansi_state = ANSI_STATE_ESC;
            return;
        } else if (c == 0x0E) {
            /* Shift Out: Select G1 Character Set */
            g_use_g1 = true;
            return;
        } else if (c == 0x0F) {
            /* Shift In: Select G0 Character Set */
            g_use_g1 = false;
            return;
        }
    } else if (g_ansi_state == ANSI_STATE_ESC) {
        if (c == '[') {
            g_ansi_state = ANSI_STATE_CSI;
            g_ansi_param_count = 0;
            g_ansi_has_param = false;
            g_ansi_private = false;
            memset(g_ansi_params, 0, sizeof(g_ansi_params));
            return;
        } else if (c == ']') {
            g_ansi_state = ANSI_STATE_OSC;
            return;
        } else if (c == '(') {
            g_ansi_state = ANSI_STATE_SCS_G0;
            return;
        } else if (c == ')') {
            g_ansi_state = ANSI_STATE_SCS_G1;
            return;
        } else if (c == '7') {
            /* DECSC: Save Cursor */
            g_saved_cursor_x = g_cursor_x;
            g_saved_cursor_y = g_cursor_y;
            g_ansi_state = ANSI_STATE_NORMAL;
            return;
        } else if (c == '8') {
            /* DECRC: Restore Cursor */
            g_cursor_x = (g_saved_cursor_x < g_cols) ? g_saved_cursor_x : g_cols - 1;
            g_cursor_y = (g_saved_cursor_y < g_rows) ? g_saved_cursor_y : g_rows - 1;
            g_ansi_state = ANSI_STATE_NORMAL;
            return;
        } else if (c == 'M') {
            /* Reverse Index (RI) */
            if (g_cursor_y <= g_scroll_top) {
                fb_scroll_down_region(g_scroll_top, g_scroll_bottom);
            } else {
                g_cursor_y--;
            }
            g_ansi_state = ANSI_STATE_NORMAL;
            return;
        } else if (c == 'E') {
            /* Next Line (NEL) */
            g_cursor_x = 0;
            g_cursor_y++;
            if (g_cursor_y > g_scroll_bottom) {
                fb_scroll_up_region(g_scroll_top, g_scroll_bottom);
                g_cursor_y = g_scroll_bottom;
            }
            g_ansi_state = ANSI_STATE_NORMAL;
            return;
        } else if (c == 'D') {
            /* Index (IND) */
            g_cursor_y++;
            if (g_cursor_y > g_scroll_bottom) {
                fb_scroll_up_region(g_scroll_top, g_scroll_bottom);
                g_cursor_y = g_scroll_bottom;
            }
            g_ansi_state = ANSI_STATE_NORMAL;
            return;
        } else if (c == 'c') {
            /* RIS: Reset Initial State */
            fb_clear(g_default_bg);
            g_scroll_top = 0;
            g_scroll_bottom = (g_rows > 0) ? g_rows - 1 : 0;
            g_fg_color = g_default_fg;
            g_bg_color = g_default_bg;
            g_bold = false;
            g_reverse = false;
            g_underline = false;
            g_ansi_state = ANSI_STATE_NORMAL;
            return;
        } else {
            g_ansi_state = ANSI_STATE_NORMAL;
            return;
        }
    } else if (g_ansi_state == ANSI_STATE_SCS_G0) {
        g_dec_graphics_g0 = (c == '0');
        g_ansi_state = ANSI_STATE_NORMAL;
        return;
    } else if (g_ansi_state == ANSI_STATE_SCS_G1) {
        g_dec_graphics_g1 = (c == '0');
        g_ansi_state = ANSI_STATE_NORMAL;
        return;
    } else if (g_ansi_state == ANSI_STATE_OSC) {
        /* Consume OSC sequence until BEL (0x07) or ST (\033\) */
        if (c == 0x07 || c == 0x1B) {
            g_ansi_state = ANSI_STATE_NORMAL;
        }
        return;
    } else if (g_ansi_state == ANSI_STATE_CSI) {
        if (c == '?' || c == '>') {
            g_ansi_private = true;
            return;
        } else if (c >= '0' && c <= '9') {
            g_ansi_params[g_ansi_param_count] = g_ansi_params[g_ansi_param_count] * 10 + (c - '0');
            g_ansi_has_param = true;
            return;
        } else if (c == ';') {
            if (g_ansi_param_count < MAX_ANSI_PARAMS - 1) {
                g_ansi_param_count++;
            }
            g_ansi_params[g_ansi_param_count] = 0;
            g_ansi_has_param = true;
            return;
        } else {
            /* Final character of CSI sequence */
            handle_csi_command(c);
            g_ansi_state = ANSI_STATE_NORMAL;
            return;
        }
    }

    /* Standard character processing */
    if (c == '\r') {
        g_cursor_x = 0;
        return;
    }

    if (c == '\n') {
        g_cursor_x = 0;
        g_cursor_y++;
        if (g_cursor_y > g_scroll_bottom) {
            fb_scroll_up_region(g_scroll_top, g_scroll_bottom);
            g_cursor_y = g_scroll_bottom;
        }
        return;
    }

    if (c == '\t') {
        size_t next_tab = (g_cursor_x + 8) & ~7;
        while (g_cursor_x < next_tab && g_cursor_x < g_cols) {
            render_glyph(' ');
        }
        return;
    }

    if (c == '\b' || c == 127) {
        if (g_cursor_x > 0) {
            g_cursor_x--;
            fb_draw_char_raw(g_cursor_x, g_cursor_y, ' ', g_fg_color, g_bg_color, false);
        }
        return;
    }

    if (c == 0x07) {
        /* Bell / Alert - ignore silently */
        return;
    }

    /* Render character with DEC graphics support if active */
    uint8_t glyph = (uint8_t)c;
    bool dec_mode = g_use_g1 ? g_dec_graphics_g1 : g_dec_graphics_g0;
    if (dec_mode) {
        glyph = map_dec_graphics(c);
    }

    render_glyph(glyph);
}

void fb_console_putc(char c) {
    if (g_graphics_mode)
        return;
    fb_console_putc_internal(c);
    fb_flush();
}

void fb_console_puts(const char *str) {
    if (!str || g_graphics_mode)
        return;
    while (*str) {
        fb_console_putc_internal(*str++);
    }
    fb_flush();
}

void fb_console_write(const char *buf, size_t len) {
    if (!buf || len == 0)
        return;
    if (g_graphics_mode)
        return;
    for (size_t i = 0; i < len; i++) {
        fb_console_putc_internal(buf[i]);
    }
    fb_flush();
}

void fb_console_clear(void) {
    fb_clear(g_bg_color);
}

void framebuffer_init_backbuffer(void) {
    if (!g_fb || !g_fb->address || g_backbuffer)
        return;

    size_t total_bytes = g_fb_height * g_fb_pitch_pixels * sizeof(uint32_t);
    size_t pages_needed = (total_bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    /* =========================================================================
     * CRITICAL FIX: Remap framebuffer VRAM pages with Write-Combining (WC)
     *
     * Limine maps the framebuffer through HHDM with default Write-Back (WB)
     * caching attributes. On QEMU this works because "VRAM" is just RAM.
     * On real GPUs (NVIDIA Quadro, Intel HD Graphics, etc.), VRAM is an MMIO
     * region on the PCIe bus. The GPU display controller reads VRAM directly
     * from the graphics card memory, NOT from CPU cache. If we write to VRAM
     * with WB caching, the data stays in CPU L1/L2 cache and NEVER reaches
     * the display controller — resulting in a frozen/blank screen.
     *
     * We remap every page of the VRAM region with PWT+PCD flags (bits 3+4),
     * which gives us Uncacheable (UC) behavior. This guarantees that every
     * store to g_fb_ptr immediately reaches VRAM through the PCIe bus.
     * ========================================================================= */
    uintptr_t fb_virt = (uintptr_t)g_fb->address;
    uintptr_t fb_phys = VIRT_TO_PHYS(fb_virt);
    size_t fb_total_bytes = g_fb_height * g_fb->pitch;
    size_t fb_pages = (fb_total_bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    for (size_t i = 0; i < fb_pages; i++) {
        uintptr_t vaddr = fb_virt + i * PAGE_SIZE;
        uintptr_t paddr = fb_phys + i * PAGE_SIZE;
        /* Remap with PWT (bit 3) + PCD (bit 4) = Uncacheable, bypasses CPU cache entirely */
        vmm_map_page(&g_kernel_pagemap, vaddr, paddr,
                     VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_WRITE_THROUGH | VMM_FLAG_CACHE_DISABLE);
    }

    /* Flush TLB to activate new cache attributes */
    write_cr3(read_cr3());

    klog_info("FB: VRAM remapped as Uncacheable (%zu pages, phys 0x%lx, virt 0x%lx)", fb_pages, (unsigned long)fb_phys,
              (unsigned long)fb_virt);

    /* Allocate in-RAM shadow backbuffer (this one stays Write-Back for fast rendering) */
    uintptr_t phys = pmm_alloc_pages(pages_needed);
    if (!phys) {
        klog_warn("FB: Failed to allocate %zu pages for backbuffer!", pages_needed);
        return;
    }

    g_backbuffer = (uint32_t *)PHYS_TO_VIRT(phys);
    memset(g_backbuffer, 0, total_bytes);

    /* Copy existing screen content into backbuffer */
    if (g_fb_ptr) {
        memcpy(g_backbuffer, g_fb_ptr, total_bytes);
    }

    klog_info("FB: Backbuffer enabled (%zu KiB in RAM, Shadow Buffer active)", total_bytes / 1024);
}

#include <drivers/panic_image.h>

typedef struct {
    uint8_t r, g, b, a;
} fb_qoi_rgba_t;

void fb_draw_panic_image(void) {
    if (!g_fb_ptr)
        return;

    /* 1. Clear backbuffer and screen to dark panic background */
    uint32_t bg_color = 0x00120202; /* Deep dark crimson */
    fb_clear(bg_color);
    fb_console_set_color(FB_COLOR_WHITE, bg_color);

    const uint8_t *data = g_panic_image_qoi;
    size_t size = g_panic_image_qoi_size;

    if (size < 14 || data[0] != 'q' || data[1] != 'o' || data[2] != 'i' || data[3] != 'f')
        return;

    uint32_t width = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) | ((uint32_t)data[6] << 8) | data[7];
    uint32_t height = ((uint32_t)data[8] << 24) | ((uint32_t)data[9] << 16) | ((uint32_t)data[10] << 8) | data[11];

    /* Center image horizontally */
    size_t dest_x = (g_fb_width > width) ? (g_fb_width - width) / 2 : 0;
    size_t dest_y = 12;

    fb_qoi_rgba_t index[64];
    memset(index, 0, sizeof(index));

    fb_qoi_rgba_t px = {0, 0, 0, 255};
    size_t p = 14;
    size_t total_pixels = width * height;
    size_t px_pos = 0;

    uint32_t *target_buf = g_backbuffer ? g_backbuffer : g_fb_ptr;

    while (p < size && px_pos < total_pixels) {
        uint8_t b1 = data[p++];

        if (b1 == 0xFE) { /* QOI_OP_RGB */
            px.r = data[p++];
            px.g = data[p++];
            px.b = data[p++];
        } else if (b1 == 0xFF) { /* QOI_OP_RGBA */
            px.r = data[p++];
            px.g = data[p++];
            px.b = data[p++];
            px.a = data[p++];
        } else if ((b1 & 0xC0) == 0x00) { /* QOI_OP_INDEX */
            px = index[b1 & 0x3F];
        } else if ((b1 & 0xC0) == 0x40) { /* QOI_OP_DIFF */
            px.r += ((b1 >> 4) & 0x03) - 2;
            px.g += ((b1 >> 2) & 0x03) - 2;
            px.b += (b1 & 0x03) - 2;
        } else if ((b1 & 0xC0) == 0x80) { /* QOI_OP_LUMA */
            uint8_t b2 = data[p++];
            int vg = (b1 & 0x3F) - 32;
            px.r += vg - 8 + ((b2 >> 4) & 0x0F);
            px.g += vg;
            px.b += vg - 8 + (b2 & 0x0F);
        } else if ((b1 & 0xC0) == 0xC0) { /* QOI_OP_RUN */
            int run = (b1 & 0x3F) + 1;
            uint32_t col = ((uint32_t)px.r << 16) | ((uint32_t)px.g << 8) | px.b;
            for (int r = 0; r < run && px_pos < total_pixels; r++) {
                size_t x = dest_x + (px_pos % width);
                size_t y = dest_y + (px_pos / width);
                if (x < g_fb_width && y < g_fb_height) {
                    target_buf[y * g_fb_pitch_pixels + x] = col;
                }
                px_pos++;
            }
            continue;
        }

        uint8_t idx_pos = (px.r * 3 + px.g * 5 + px.b * 7 + px.a * 11) % 64;
        index[idx_pos] = px;

        size_t x = dest_x + (px_pos % width);
        size_t y = dest_y + (px_pos / width);
        uint32_t col = ((uint32_t)px.r << 16) | ((uint32_t)px.g << 8) | px.b;
        if (x < g_fb_width && y < g_fb_height) {
            target_buf[y * g_fb_pitch_pixels + x] = col;
        }
        px_pos++;
    }

    /* Flush backbuffer to VRAM */
    if (g_backbuffer && g_fb_ptr) {
        size_t total_bytes = g_fb_height * g_fb_pitch_pixels * sizeof(uint32_t);
        memcpy(g_fb_ptr, g_backbuffer, total_bytes);
    }

    /* Position console cursor nicely below the artwork */
    g_cursor_x = 2;
    g_cursor_y = (dest_y + height + 15) / g_char_h;
    if (g_cursor_y >= g_rows)
        g_cursor_y = g_rows - 1;
}
