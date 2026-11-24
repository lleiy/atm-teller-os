#define VGA ((volatile unsigned short*)0xB8000)
#define COLS 80
#define ROWS 25

static inline unsigned char inb(unsigned short p) {
    unsigned char r;
    __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(p));
    return r;
}
static inline void outb(unsigned short p, unsigned char v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(p));
}

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

static const char lower[] =
 "\0\033" "1234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
static const char upper[] =
 "\0\033" "!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";

static char getchar_blocking(void) {
    static int shift = 0;
    for (;;) {
        while (!(inb(0x64) & 1)) { }
        unsigned char sc = inb(0x60);
        if (sc & 0x80) {
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
static void error(const char *msg) { print_c(msg, 0x0C); putc('\n'); }

/* ===== PHASE 3: account store (Lerio's module) ===== */
#define MAX_ACC 10
static int acc_id[MAX_ACC], acc_bal[MAX_ACC], acc_used[MAX_ACC];

static int find_acc(int id) {
    for (int i = 0; i < MAX_ACC; i++)
        if (acc_used[i] && acc_id[i] == id) return i;
    return -1;
}

/* ===== PHASE 4: process management (Gerona's module) ===== */
#define MAX_PROC 32
enum { T_DEPOSIT, T_WITHDRAW, T_BALANCE };
enum { S_WAITING, S_DONE, S_FAILED };

struct pcb {
    int pid, type, acc, amount;
    int burst;          /* estimated service time, used by SJF */
    int state, result;
    const char *msg;
};
static struct pcb proc[MAX_PROC];
static int nproc = 0;
static int rq[MAX_PROC];     /* ready queue: indexes into proc[] */
static int rq_count = 0;
static int policy = 1;       /* 0 = FCFS, 1 = SJF */

static const char *type_name(int t) {
    return t == T_DEPOSIT ? "deposit " : t == T_WITHDRAW ? "withdraw" : "balance ";
}

static void submit(int type, int acc, int amount) {
    if (nproc >= MAX_PROC) { error("process table full"); return; }
    struct pcb *p = &proc[nproc];
    p->pid = nproc + 1; p->type = type; p->acc = acc; p->amount = amount;
    p->burst = (type == T_BALANCE) ? 1 : (type == T_DEPOSIT) ? 2 : 3;
    p->state = S_WAITING; p->result = 0; p->msg = "";
    rq[rq_count++] = nproc;
    nproc++;
    print("queued P"); print_int(p->pid); print(" ("); print(type_name(type));
    print(") position "); print_int(rq_count); putc('\n');
}

static int pick_next(void) {
    int best = 0;                          /* FCFS: front of queue */
    if (policy == 1)
        for (int i = 1; i < rq_count; i++)
            if (proc[rq[i]].burst < proc[rq[best]].burst) best = i;
    return best;
}

static void teller_serve(struct pcb *p) {
    int i = find_acc(p->acc);
    if (i < 0) { p->state = S_FAILED; p->msg = "account not found"; return; }
    if (p->type == T_DEPOSIT) acc_bal[i] += p->amount;
    else if (p->type == T_WITHDRAW) {
        if (p->amount > acc_bal[i]) { p->state = S_FAILED; p->msg = "insufficient funds"; return; }
        acc_bal[i] -= p->amount;
    }
    p->result = acc_bal[i];
    p->state = S_DONE;
}

static void print_proc_line(struct pcb *p) {
    print("  P"); print_int(p->pid); print(" ");
    print(type_name(p->type)); print(" acc "); print_int(p->acc);
    if (p->type != T_BALANCE) { print(" amt "); print_int(p->amount); }
}

static int serve_one(void) {
    if (rq_count == 0) return 0;
    int slot = pick_next();
    struct pcb *p = &proc[rq[slot]];
    for (int i = slot; i < rq_count - 1; i++) rq[i] = rq[i + 1];
    rq_count--;
    teller_serve(p);
    print("teller serving"); print_proc_line(p); print(" -> ");
    if (p->state == S_DONE) {
        print_c("DONE", 0x0A); print(" balance = "); print_int(p->result);
    } else {
        print_c("FAILED", 0x0C); print(" ("); print(p->msg); print(")");
    }
    putc('\n');
    return 1;
}

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
    submit(T_DEPOSIT, to_int(argv[1]), to_int(argv[2]));
}
static void cmd_withdraw(int argc, char **argv) {
    if (argc != 3 || !is_number(argv[1]) || !is_number(argv[2])) { error("usage: withdraw <id> <amount>"); return; }
    submit(T_WITHDRAW, to_int(argv[1]), to_int(argv[2]));
}
static void cmd_balance(int argc, char **argv) {
    if (argc != 2 || !is_number(argv[1])) { error("usage: balance <id>"); return; }
    submit(T_BALANCE, to_int(argv[1]), 0);
}
static void cmd_run(void) {
    if (rq_count == 0) { print("queue is empty\n"); return; }
    print(policy ? "scheduler: SJF\n" : "scheduler: FCFS\n");
    while (serve_one()) { }
}
static void cmd_next(void) {
    if (!serve_one()) print("queue is empty\n");
}
static void cmd_sched(int argc, char **argv) {
    if (argc == 1) { print("scheduler: "); print(policy ? "SJF\n" : "FCFS\n"); return; }
    if (streq(argv[1], "fcfs")) { policy = 0; print("scheduler set to FCFS\n"); }
    else if (streq(argv[1], "sjf")) { policy = 1; print("scheduler set to SJF\n"); }
    else error("usage: sched fcfs | sched sjf");
}
static void cmd_status(void) {
    print_c("ready queue (", 0x0E); print(policy ? "SJF" : "FCFS"); print_c("):\n", 0x0E);
    if (rq_count == 0) print("  empty\n");
    for (int i = 0; i < rq_count; i++) { print_proc_line(&proc[rq[i]]); print(" WAITING\n"); }
    print_c("finished:\n", 0x0E);
    int any = 0;
    for (int i = 0; i < nproc; i++) {
        struct pcb *p = &proc[i];
        if (p->state == S_WAITING) continue;
        any = 1;
        print_proc_line(p); print(p->state == S_DONE ? " DONE\n" : " FAILED\n");
    }
    if (!any) print("  none\n");
    print_c("accounts:\n", 0x0E);
    any = 0;
    for (int i = 0; i < MAX_ACC; i++)
        if (acc_used[i]) {
            any = 1;
            print("  acc "); print_int(acc_id[i]); print(" = "); print_int(acc_bal[i]); putc('\n');
        }
    if (!any) print("  none\n");
}
static void cmd_help(void) {
    print("open <id> <amount>      create account (instant)\n");
    print("deposit <id> <amount>   add to queue\n");
    print("withdraw <id> <amount>  add to queue\n");
    print("balance <id>            add to queue\n");
    print("run     teller serves the whole queue\n");
    print("next    teller serves one transaction\n");
    print("sched [fcfs|sjf]  show or change scheduler\n");
    print("status  queue, finished, accounts\n");
    print("clear   help\n");
}

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
    else if (streq(argv[0], "run"))      cmd_run();
    else if (streq(argv[0], "next"))     cmd_next();
    else if (streq(argv[0], "sched"))    cmd_sched(argc, argv);
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
