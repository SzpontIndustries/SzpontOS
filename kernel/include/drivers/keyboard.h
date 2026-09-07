/*
 * SzpontOS - Modern PS/2 & AT Keyboard Driver
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef SZPONTOS_DRIVERS_KEYBOARD_H
#define SZPONTOS_DRIVERS_KEYBOARD_H

#include <kernel/types.h>
#include <stdbool.h>

/* Keyboard Modifier Flags */
#define KBD_MOD_LSHIFT (1 << 0)
#define KBD_MOD_RSHIFT (1 << 1)
#define KBD_MOD_LCTRL (1 << 2)
#define KBD_MOD_RCTRL (1 << 3)
#define KBD_MOD_LALT (1 << 4)
#define KBD_MOD_RALT (1 << 5)
#define KBD_MOD_CAPSLOCK (1 << 6)
#define KBD_MOD_NUMLOCK (1 << 7)

/* AUX setup holds the same lock as IRQ and keyboard command processing. */
uint64_t keyboard_controller_acquire(void);
void keyboard_controller_release(uint64_t flags);
bool keyboard_aux_command(uint8_t cmd);
int keyboard_aux_read(uint32_t timeout_us);
bool keyboard_configure_aux(bool irq_enabled);

void keyboard_init(void);
void keyboard_poll_hardware(void);
void keyboard_relax(void);
void keyboard_drain_buffers(void);
bool keyboard_has_char(void);
char keyboard_getc(void);

void keyboard_push_char(char ch);
void keyboard_push_str(const char *str);
void keyboard_handle_incoming_byte(uint8_t scancode);

void keyboard_set_leds(bool numlock, bool capslock, bool scrolllock);
uint8_t keyboard_get_modifiers(void);
bool keyboard_is_caps_lock(void);
bool keyboard_is_num_lock(void);
void keyboard_force_set2_mode(void);

#endif /* SZPONTOS_DRIVERS_KEYBOARD_H */
