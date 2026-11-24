#define VGA ((volatile unsigned short*)0xB8000)
#define COLS 80
#define ROWS 25

/* ---------- port I/O ---------- */
static inline unsigned char inb(unsigned short p) {
    unsigned char r;
    __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(p));
    return r;
}
static inline void outb(unsigned short p, unsigned char v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(p));
}

/* ---------- screen ---------- */
static int row = 0, col = 0;
static unsigned char color = 0x07;

static void set_cursor(void) {
    unsigned short pos = row * COLS + col;
    outb(0x3D4, 14); outb(0x3D5, pos >> 8);
    outb(0x3D4, 15); outb(0x3D5, pos & 0xFF);
}

static void clear(void) {
    for (int i = 0; i < COLS * ROWS; i++) VGA[i] = (0x07 << 8) | ' ';
    row = col = 0;
    set_cursor();
}

static void scroll(void) {
    for (int i = 0; i < COLS * (ROWS - 1); i++) VGA[i] = VGA[i + COLS];
    for (int i = COLS * (ROWS - 1); i < COLS * ROWS; i++) VGA[i] = (0x07 << 8) | ' ';
    row = ROWS - 1;
}

static void putc(char c) {
    if (c == '\n') { col = 0; row++; }
    else if (c == '\b') {
        if (col > 0) { col--; VGA[row * COLS + col] = (0x07 << 8) | ' '; }
    } else {
        VGA[row * COLS + col] = (color << 8) | (unsigned char)c;
        if (++col >= COLS) { col = 0; row++; }
    }
    if (row >= ROWS) scroll();
    set_cursor();
}

static void print_c(const char *s, unsigned char c) {
    unsigned char old = color; color = c;
    while (*s) putc(*s++);
    color = old;
}
static void print(const char *s) { print_c(s, 0x07); }

static void print_int(int n) {
    char buf[12]; int i = 0;
    if (n == 0) { putc('0'); return; }
    if (n < 0) { putc('-'); n = -n; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) putc(buf[--i]);
}

/* ---------- keyboard (polling, scancode set 1) ---------- */
static const char lower[] =
 "\0\033" "1234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
static const char upper[] =
 "\0\033" "!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";

static char getchar_blocking(void) {
    static int shift = 0;
    for (;;) {
        while (!(inb(0x64) & 1)) { }
        unsigned char sc = inb(0x60);
        if (sc & 0x80) {                         /* key released */
            sc &= 0x7F;
            if (sc == 0x2A || sc == 0x36) shift = 0;
            continue;
        }
        if (sc == 0x2A || sc == 0x36) { shift = 1; continue; }
        if (sc < 58) {
            char c = shift ? upper[sc] : lower[sc];
            if (c) return c;
        }
    }
}

static void readline(char *buf, int max) {
    int n = 0;
    for (;;) {
        char c = getchar_blocking();
        if (c == '\n') { putc('\n'); buf[n] = 0; return; }
        if (c == '\b') { if (n > 0) { n--; putc('\b'); } continue; }
        if (c >= 32 && n < max - 1) { buf[n++] = c; putc(c); }
    }
}

/* ---------- tiny string helpers ---------- */
static int streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static int is_number(const char *s) {
    if (!*s) return 0;
    while (*s) { if (*s < '0' || *s > '9') return 0; s++; }
    return 1;
}
static int to_int(const char *s) {
    int n = 0;
    while (*s) n = n * 10 + (*s++ - '0');
    return n;
}

/* ---------- temporary account store (Lerio replaces this in Phase 3) ---------- */
#define MAX_ACC 10
static int acc_id[MAX_ACC], acc_bal[MAX_ACC], acc_used[MAX_ACC];

static int find_acc(int id) {
    for (int i = 0; i < MAX_ACC; i++)
        if (acc_used[i] && acc_id[i] == id) return i;
    return -1;
}

static void error(const char *msg) { print_c(msg, 0x0C); putc('\n'); }

/* ---------- command handlers ---------- */
static void cmd_open(int argc, char **argv) {
    if (argc != 3 || !is_number(argv[1]) || !is_number(argv[2])) { error("usage: open <id> <amount>"); return; }
    int id = to_int(argv[1]);
    if (find_acc(id) >= 0) { error("account already exists"); return; }
    for (int i = 0; i < MAX_ACC; i++) {
        if (!acc_used[i]) {
            acc_used[i] = 1; acc_id[i] = id; acc_bal[i] = to_int(argv[2]);
            print("account "); print_int(id); print(" opened\n");
            return;
        }
    }
    error("no free account slots");
}

static void cmd_deposit(int argc, char **argv) {
    if (argc != 3 || !is_number(argv[1]) || !is_number(argv[2])) { error("usage: deposit <id> <amount>"); return; }
    int i = find_acc(to_int(argv[1]));
    if (i < 0) { error("account not found"); return; }
    acc_bal[i] += to_int(argv[2]);
    print("DONE. balance "); print_int(acc_id[i]); print(" = "); print_int(acc_bal[i]); putc('\n');
}

static void cmd_withdraw(int argc, char **argv) {
    if (argc != 3 || !is_number(argv[1]) || !is_number(argv[2])) { error("usage: withdraw <id> <amount>"); return; }
    int i = find_acc(to_int(argv[1]));
    if (i < 0) { error("account not found"); return; }
    int amt = to_int(argv[2]);
    if (amt > acc_bal[i]) { error("insufficient funds"); return; }
    acc_bal[i] -= amt;
    print("DONE. balance "); print_int(acc_id[i]); print(" = "); print_int(acc_bal[i]); putc('\n');
}

static void cmd_balance(int argc, char **argv) {
    if (argc != 2 || !is_number(argv[1])) { error("usage: balance <id>"); return; }
    int i = find_acc(to_int(argv[1]));
    if (i < 0) { error("account not found"); return; }
    print("balance "); print_int(acc_id[i]); print(" = "); print_int(acc_bal[i]); putc('\n');
}

static void cmd_status(void) {
    int any = 0;
    for (int i = 0; i < MAX_ACC; i++)
        if (acc_used[i]) {
            any = 1;
            print("  account "); print_int(acc_id[i]); print(" = "); print_int(acc_bal[i]); putc('\n');
        }
    if (!any) print("  no accounts yet\n");
}

static void cmd_help(void) {
    print("commands:\n");
    print("  open <id> <amount>\n  deposit <id> <amount>\n  withdraw <id> <amount>\n");
    print("  balance <id>\n  status\n  clear\n  help\n");
}

/* ---------- shell ---------- */
static int tokenize(char *line, char **argv, int max) {
    int argc = 0;
    while (*line) {
        while (*line == ' ') *line++ = 0;
        if (!*line) break;
        if (argc < max) argv[argc++] = line;
        while (*line && *line != ' ') line++;
    }
    return argc;
}

static void run_command(char *line) {
    char *argv[4];
    int argc = tokenize(line, argv, 4);
    if (argc == 0) return;
    if      (streq(argv[0], "open"))     cmd_open(argc, argv);
    else if (streq(argv[0], "deposit"))  cmd_deposit(argc, argv);
    else if (streq(argv[0], "withdraw")) cmd_withdraw(argc, argv);
    else if (streq(argv[0], "balance"))  cmd_balance(argc, argv);
    else if (streq(argv[0], "status"))   cmd_status();
    else if (streq(argv[0], "clear"))    clear();
    else if (streq(argv[0], "help"))     cmd_help();
    else error("unknown command (type help)");
}

void kernel_main(void) {
    char line[80];
    clear();
    print_c("ATM Teller OS\n", 0x0A);
    print("Type help for commands.\n\n");
    for (;;) {
        print_c("bank> ", 0x0E);
        readline(line, sizeof(line));
        run_command(line);
    }
}
