/* Host regression test: actual decoder and command demultiplexer, fake ports. */
#include <kernel/types.h>
#define SZPONTOS_ARCH_X86_64_IO_H
static uint8_t bytes[32], statuses[32];
static unsigned input_pos, input_len;
static uint8_t inb(uint16_t port) {
    if (port == 0x64) return input_pos < input_len ? statuses[input_pos] | 1 : 0;
    return input_pos < input_len ? bytes[input_pos++] : 0;
}
static uint8_t last_data;
static void outb(uint16_t p, uint8_t v) { if(p==0x60) last_data=v; }
static void io_wait(void) {}
static void udelay(uint64_t t) { (void)t; }
#include "kernel/src/drivers/keyboard.c"
extern int printf(const char *, ...);
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static int mouse_bytes;
void evdev_push_key(uint16_t code, bool pressed) { (void)code; (void)pressed; }
bool ps2_mouse_is_enabled(void) { return true; }
void ps2_mouse_handle_byte(uint8_t b) { (void)b; mouse_bytes++; }
static bool output(const char *s) {
    for (; *s; s++) {
        if (g_kb_read_ptr == g_kb_write_ptr || g_kb_buffer[g_kb_read_ptr] != *s) return false;
        g_kb_read_ptr = (g_kb_read_ptr+1) % KEYBOARD_BUFFER_SIZE;
    }
    return g_kb_read_ptr == g_kb_write_ptr;
}
static void feed(const uint8_t *p, size_t n) { for (size_t i=0;i<n;i++) process_scancode(p[i]); }
#define FEED(...) do { const uint8_t seq[]={__VA_ARGS__}; feed(seq,sizeof(seq)); } while (0)
int main(void) {
    FEED(0x1e,0x9e,0x2a,0x1e,0x9e,0xaa,0x1e,0x9e,0x1c,0x9c);
    CHECK(output("aAa\r"));
    FEED(0xe0,0x48,0xe0,0xc8);
    CHECK(output("\033[A"));
    FEED(0xe1,0x1d,0x45,0xe1,0x9d,0xc5,0x1e,0x9e);
    CHECK(output("a")); CHECK(!g_lctrl && g_num_lock);
    g_is_set2_mode=true;
    FEED(0x1c,0xf0,0x1c,0x12,0x1c,0xf0,0x1c,0xf0,0x12,0x5a,0xf0,0x5a);
    CHECK(output("aA\r")); CHECK(!g_lshift);
    FEED(0xe0,0x75,0xe0,0xf0,0x75,0xe0,0x14,0x21,0xf0,0x21,0xe0,0xf0,0x14);
    CHECK(output("\033[A\003")); CHECK(!g_rctrl);
    FEED(0xe1,0x14,0x77,0xe1,0xf0,0x14,0xf0,0x77,0x1c,0xf0,0x1c);
    CHECK(output("a")); CHECK(!g_lctrl && g_num_lock);
    FEED(0xfa,0xfe,0xee,0x1c,0xf0,0x1c); CHECK(output("a"));
    /* A mouse ACK must not finish a keyboard command. Interleaved keys survive. */
    g_is_set2_mode=false;
    statuses[0]=0x20;bytes[0]=0xfa;
    statuses[1]=0;bytes[1]=0x1e;
    statuses[2]=0;bytes[2]=0xfa;
    input_pos=0;input_len=3;
    CHECK(kbd_send_device_command(0xf4)==0 && input_pos==3);
    CHECK(output("a") && mouse_bytes==1);
    g_i8042_present=true;
    statuses[0]=0;bytes[0]=0x30;
    statuses[1]=0x20;bytes[1]=0xfa;
    input_pos=0;input_len=2;
    CHECK(keyboard_aux_command(0xff)); CHECK(output("b"));
    statuses[0]=0;bytes[0]=0x2e;
    statuses[1]=0x20;bytes[1]=0xaa;
    input_pos=0;input_len=2;
    CHECK(keyboard_aux_read(1000)==0xaa); CHECK(output("c"));
    kbd_deliver_byte(0x80,0x1e); CHECK(output(""));
    g_controller_config=0x04; /* controller refused translation */
    CHECK(keyboard_configure_aux(true)); CHECK(last_data==0x07);
    CHECK(keyboard_configure_aux(false)); CHECK(last_data==0x05);
    g_controller_config=0x44;
    CHECK(keyboard_configure_aux(true)); CHECK(last_data==0x47);
    CHECK(keyboard_configure_aux(false)); CHECK(last_data==0x45);
    printf("PASS: PS/2 Set 1/2, modifiers, arrows, Pause, ACK/AUX routing, parity\n");
    return 0;
}
