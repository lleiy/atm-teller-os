#define VGA ((volatile unsigned short*)0xB8000)
#define COLS 80
#define ROWS 25

static int row = 0, col = 0;

static void clear(void) {
    for (int i = 0; i < COLS * ROWS; i++) VGA[i] = (0x07 << 8) | ' ';
    row = col = 0;
}

static void putc(char c, unsigned char color) {
    if (c == '\n') { col = 0; row++; return; }
    VGA[row * COLS + col] = (color << 8) | (unsigned char)c;
    if (++col >= COLS) { col = 0; row++; }
}

static void print(const char *s, unsigned char color) {
    while (*s) putc(*s++, color);
}

void kernel_main(void) {
    clear();
    print("ATM Teller OS\n", 0x0A);
    print("Phase 1 complete: kernel booted!\n", 0x07);
    print("bank> ", 0x0E);
}
