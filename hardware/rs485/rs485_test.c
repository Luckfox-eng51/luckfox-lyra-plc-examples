#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <linux/serial.h>
#include <linux/gpio.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* Linux RS485 physical link tester. This private protocol is NOT Modbus RTU.
 * Wire: "R485", type:u8 (1=request, 2=response), pattern:u8,
 *       seq:u32le, payload_len:u16le, payload, CRC16-Modbus:u16le.
 * A response XORs every payload byte with 0xa5, so local echo cannot pass.
 * Half duplex: one outstanding request; responder waits before replying.
 */
#define HEADER 12
#define MAX_PAYLOAD 4096
#define MAX_FRAME (HEADER + MAX_PAYLOAD + 2)
static volatile sig_atomic_t stopped;
static int fd = -1, have_termios, changed_rs485, exclusive;
static int txen_fd = -1;
static unsigned txen_setup_us = 20;
static unsigned long txen_cycles;
static struct termios saved_termios;
static struct serial_rs485 saved_rs485;
static unsigned long crc_errors, discarded, unexpected;
struct stream { unsigned char data[MAX_FRAME * 2]; size_t used; };
struct frame { unsigned char data[MAX_FRAME]; size_t size; };

/* Return monotonic milliseconds for timeouts and round-trip timing. */
static int64_t now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
/* Request shutdown; leave cleanup to the main program. */
static void on_signal(int sig) { (void)sig; stopped = 1; }
/* Wait for ms milliseconds, or return early on shutdown. */
static void pause_ms(int ms) {
    struct timespec t = {ms / 1000, (ms % 1000) * 1000000L};
    while (!stopped && nanosleep(&t, &t) < 0 && errno == EINTR) {}
}
/* Set TXEN: 1 for transmit, 0 for receive. Return 0 on success, -1 on error. */
static int set_txen(int level) {
    struct gpiohandle_data value = {{0}};
    value.values[0] = (unsigned char)level;
    return ioctl(txen_fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &value);
}
/* Wait for us microseconds; system scheduling may extend the delay. */
static void pause_us(unsigned us) {
    struct timespec t = {us / 1000000U, (long)(us % 1000000U) * 1000L};
    while (!stopped && nanosleep(&t, &t) < 0 && errno == EINTR) {}
}
/* Return to receive mode, release GPIO, restore serial settings, and close. */
static void cleanup(void) {
    if (txen_fd >= 0) {
        if (set_txen(0) < 0) perror("release TXEN to receive");
        close(txen_fd); txen_fd = -1;
    }
    if (fd < 0) return;
    if (changed_rs485 && ioctl(fd, TIOCSRS485, &saved_rs485) < 0)
        perror("restore RS485");
    if (have_termios && tcsetattr(fd, TCSANOW, &saved_termios) < 0)
        perror("restore termios");
    if (exclusive) (void)ioctl(fd, TIOCNXCL);
    close(fd);
    fd = -1;
}
/* Print the system error and exit with status 2. */
static void die(const char *s) { perror(s); exit(2); }
/* Parse a decimal number in [min, max]; exit on invalid input. */
static unsigned number(const char *s, unsigned min, unsigned max) {
    char *end;
    errno = 0;
    unsigned long v = strtoul(s, &end, 10);
    if (errno || !*s || *end || *s == '-' || v < min || v > max) {
        fprintf(stderr, "Invalid number: %s (range %u..%u)\n", s, min, max);
        exit(2);
    }
    return (unsigned)v;
}
/* Convert a supported baud rate to its termios value. */
static speed_t baud_value(unsigned baud) {
    switch (baud) {
    case 1200: return B1200; case 2400: return B2400;
    case 4800: return B4800; case 9600: return B9600;
    case 19200: return B19200; case 38400: return B38400;
    case 57600: return B57600; case 115200: return B115200;
    case 230400: return B230400; case 460800: return B460800;
    case 921600: return B921600;
    default: fprintf(stderr, "Unsupported baud: %u\n", baud); exit(2);
    }
}
/* Read a 16-bit value from two bytes in little-endian order. */
static uint16_t get16(const unsigned char *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
/* Read a 32-bit value from four bytes in little-endian order. */
static uint32_t get32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
/* Write a 16-bit value as two bytes in little-endian order. */
static void put16(unsigned char *p, uint16_t x) { p[0] = x; p[1] = x >> 8; }
/* Write a 32-bit value as four bytes in little-endian order. */
static void put32(unsigned char *p, uint32_t x) {
    for (int i = 0; i < 4; i++) p[i] = (unsigned char)(x >> (8 * i));
}
/* Compute Modbus CRC16 with initial value 0xFFFF and polynomial 0xA001. */
static uint16_t crc16(const unsigned char *p, size_t n) {
    uint16_t c = 0xffff;
    while (n--) {
        c ^= *p++;
        for (int i = 0; i < 8; i++) c = (c & 1) ? (c >> 1) ^ 0xa001 : c >> 1;
    }
    return c;
}
/* Write the frame CRC into the reserved final two bytes. */
static void seal(struct frame *f) {
    put16(f->data + f->size - 2, crc16(f->data, f->size - 2));
}
/* Build a request with a sequence number, one of six payload patterns, and CRC. */
static void request(struct frame *f, uint32_t seq, unsigned len, unsigned pattern) {
    memcpy(f->data, "R485", 4);
    f->data[4] = 1; f->data[5] = (unsigned char)pattern;
    put32(f->data + 6, seq); put16(f->data + 10, (uint16_t)len);
    uint32_t rng = seq ^ 0x6d2b79f5U;
    if (!rng) rng = 1;
    for (unsigned i = 0; i < len; i++) {
        unsigned char b;
        switch (pattern) {
        case 0: b = 0x00; break; case 1: b = 0xff; break;
        case 2: b = 0x55; break; case 3: b = 0xaa; break;
        case 4: b = (unsigned char)i; break;
        default:
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            b = (unsigned char)rng; break;
        }
        f->data[HEADER + i] = b;
    }
    f->size = HEADER + len + 2;
    seal(f);
}
/* Build a reply using payload XOR 0xA5 and a new CRC to reject local echo. */
static void reply(struct frame *f) {
    f->data[4] = 2;
    for (size_t i = HEADER; i < f->size - 2; i++) f->data[i] ^= 0xa5;
    seal(f);
}
/* Wait for serial events until the monotonic deadline in milliseconds.
 * Return 1 if ready, 0 on timeout, or -1 on error or shutdown.
 */
static int ready(short events, int64_t deadline) {
    while (!stopped) {
        int64_t remain = deadline - now_ms();
        if (remain <= 0) return 0;
        struct pollfd p = {fd, events, 0};
        int r = poll(&p, 1, (int)remain);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (!r) return 0;
        if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) { errno = EIO; return -1; }
        if (p.revents & events) return 1;
    }
    errno = EINTR;
    return -1;
}
/* Send a full frame within the timeout in milliseconds.
 * With GPIO TXEN, wait for both the output queue and UART to empty,
 * then lower TXEN immediately to receive. Without GPIO, only queue the frame.
 * Return 0 on success or -1 on error.
 */
static int send_frame(const struct frame *f, unsigned timeout) {
    size_t done = 0;
    int64_t deadline = now_ms() + timeout;
    int result = -1, saved_errno;
    if (txen_fd >= 0) {
        if (set_txen(1) < 0) return -1;
        pause_us(txen_setup_us);
    }
    while (done < f->size) {
        int r = ready(POLLOUT, deadline);
        if (r != 1) { if (!r) errno = ETIMEDOUT; goto finished; }
        ssize_t n = write(fd, f->data + done, f->size - done);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            goto finished;
        }
        done += (size_t)n;
    }
    if (txen_fd >= 0) {
        while (!stopped) {
            int queued, lsr;
            if (ioctl(fd, TIOCOUTQ, &queued) < 0 || ioctl(fd, TIOCSERGETLSR, &lsr) < 0)
                goto finished;
            if (!queued && (lsr & TIOCSER_TEMT)) { result = 0; break; }
            if (now_ms() >= deadline) { errno = ETIMEDOUT; goto finished; }
            pause_us(20);
        }
        if (stopped) { errno = EINTR; goto finished; }
    } else result = 0;
finished:
    saved_errno = errno;
    if (txen_fd >= 0) {
        /* No printf, remote operation, or extra hold delay before returning to RX. */
        if (set_txen(0) < 0) return -1;
        if (!result) txen_cycles++;
    }
    errno = saved_errno;
    return result;
}
/* Remove n bytes from the receive buffer; n must not exceed s->used. */
static void drop(struct stream *s, size_t n) {
    memmove(s->data, s->data + n, s->used - n); s->used -= n;
}
/* Assemble a frame, check its CRC, and skip noise or damaged data.
 * Keep unused bytes for the next call.
 * Return 1 for a valid frame, 0 on timeout, or -1 on error or shutdown.
 */
static int receive(struct stream *s, struct frame *f, int64_t deadline) {
    while (!stopped) {
        while (s->used >= 4) {
            if (memcmp(s->data, "R485", 4)) { drop(s, 1); discarded++; continue; }
            if (s->used < HEADER) break;
            unsigned len = get16(s->data + 10);
            if (!len || len > MAX_PAYLOAD || s->data[4] < 1 || s->data[4] > 2 || s->data[5] > 5) {
                drop(s, 1); discarded++; continue;
            }
            size_t total = HEADER + len + 2;
            if (s->used < total) break;
            if (crc16(s->data, total - 2) != get16(s->data + total - 2)) {
                crc_errors++; drop(s, 1); discarded++; continue;
            }
            memcpy(f->data, s->data, total); f->size = total; drop(s, total);
            return 1;
        }
        int r = ready(POLLIN, deadline);
        if (r != 1) return r;
        ssize_t n = read(fd, s->data + s->used, sizeof(s->data) - s->used);
        if (n < 0) { if (errno == EINTR || errno == EAGAIN) continue; return -1; }
        if (!n) continue;
        s->used += (size_t)n;
    }
    errno = EINTR;
    return -1;
}
/* Save serial settings and configure raw 8N1 with no flow control.
 * Apply the requested kernel RS485 mode and clear old serial data.
 */
static void setup(const char *device, unsigned baud, const char *rs485) {
    speed_t speed = baud_value(baud);
    fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) die("open serial");
    if (ioctl(fd, TIOCEXCL) < 0) die("TIOCEXCL");
    exclusive = 1;
    if (tcgetattr(fd, &saved_termios) < 0) die("tcgetattr");
    have_termios = 1;
    struct termios t = saved_termios;
    cfmakeraw(&t);
    t.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS);
#ifdef CMSPAR
    t.c_cflag &= ~CMSPAR;
#endif
    t.c_cflag |= CS8 | CREAD | CLOCAL;
    t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;
    if (cfsetispeed(&t, speed) || cfsetospeed(&t, speed) || tcsetattr(fd, TCSANOW, &t))
        die("set 8N1 baud");
    struct termios actual;
    if (tcgetattr(fd, &actual) < 0) die("verify termios");
    if (cfgetispeed(&actual) != speed || cfgetospeed(&actual) != speed ||
        (actual.c_cflag & (CSIZE | PARENB | CSTOPB | CRTSCTS)) != CS8) {
        fprintf(stderr, "Driver did not retain requested serial settings\n"); exit(2);
    }
    if (ioctl(fd, TIOCGRS485, &saved_rs485) < 0) {
        printf("RS485_IOCTL unavailable: %s\n", strerror(errno));
        if (strcmp(rs485, "keep")) die("RS485 direction control unsupported");
    } else {
        printf("RS485_IOCTL original_flags=0x%x\n", saved_rs485.flags);
        if (strcmp(rs485, "keep")) {
            struct serial_rs485 cfg = {0}, applied;
            if (strcmp(rs485, "off")) {
                cfg.flags = SER_RS485_ENABLED;
                cfg.flags |= !strcmp(rs485, "high") ? SER_RS485_RTS_ON_SEND : SER_RS485_RTS_AFTER_SEND;
            }
            if (ioctl(fd, TIOCSRS485, &cfg) < 0) die("TIOCSRS485");
            changed_rs485 = 1;
            if (ioctl(fd, TIOCGRS485, &applied) < 0) die("verify RS485");
            if (applied.flags != cfg.flags) { fprintf(stderr, "Driver rejected RS485 flags\n"); exit(2); }
            printf("RS485_IOCTL applied_flags=0x%x\n", applied.flags);
        }
    }
    if (tcflush(fd, TCIOFLUSH) < 0) die("tcflush");
}
/* Request an active-high TXEN GPIO, initially low for receive.
 * The line number is an offset within the GPIO chip.
 */
static void setup_txen(const char *chip, unsigned line) {
    struct serial_rs485 cfg;
    if (ioctl(fd, TIOCGRS485, &cfg) == 0 && (cfg.flags & SER_RS485_ENABLED)) {
        fprintf(stderr, "Manual GPIO TXEN requires kernel RS485 mode disabled\n"); exit(2);
    }
    int lsr;
    if (ioctl(fd, TIOCSERGETLSR, &lsr) < 0)
        die("GPIO TXEN requires UART transmitter-empty (TIOCSERGETLSR) support");
    int chip_fd = open(chip, O_RDONLY | O_CLOEXEC);
    if (chip_fd < 0) die("open gpiochip");
    struct gpiohandle_request req = {0};
    req.lineoffsets[0] = line;
    req.flags = GPIOHANDLE_REQUEST_OUTPUT;
    req.default_values[0] = 0;
    req.lines = 1;
    snprintf(req.consumer_label, sizeof(req.consumer_label), "rs485-test-txen");
    int rc = ioctl(chip_fd, GPIO_GET_LINEHANDLE_IOCTL, &req);
    int err = errno;
    close(chip_fd); errno = err;
    if (rc < 0) die("request TXEN GPIO (check line number and owner)");
    txen_fd = req.fd;
    printf("TXEN gpiochip=%s line=%u active_high=1 idle=0 setup_us=%u drain=TIOCOUTQ+TIOCSER_TEMT\n",
           chip,line,txen_setup_us);
}
/* Print command options, defaults, and exit codes. */
static void usage(const char *name) {
    printf("Usage: %s --device /dev/ttyS1 --mode ping|reply [options]\n"
           "  -d, --device PATH    Linux serial device (required)\n"
           "  -m, --mode MODE      ping initiates; reply responds (required)\n"
           "  -b, --baud N         9600 default; 8N1, no flow control\n"
           "  -n, --count N        frame count, default 100 (1..1000000)\n"
           "  -l, --length N       ping payload bytes, default 256 (1..4096)\n"
           "  -t, --timeout MS     per-frame deadline; default 3000\n"
           "  -g, --gap MS         ping gap / reply turnaround, default 5\n"
           "  -r, --rs485 MODE     keep(default), high, low, off\n"
           "      --txen-line N    control active-high TXEN using GPIO line offset\n"
           "      --gpiochip PATH GPIO controller, default /dev/gpiochip0\n"
           "      --txen-setup-us N  delay before first byte, default 20 us\n"
           "  -v, --verbose        print every frame\n"
           "Exit: 0 all frames valid; 1 test failed; 2 setup/I/O/arguments; 130 interrupted\n",
           name);
}
/* Run the ping or reply test, validate frames, and print the final summary. */
int main(int argc, char **argv) {
    const char *device = NULL, *mode = NULL, *rs485 = "keep";
    unsigned baud = 9600, count = 100, len = 256, timeout = 3000, gap = 5;
    int verbose = 0, c, txen_line = -1;
    const char *gpiochip = "/dev/gpiochip0";
    static const struct option opts[] = {
        {"device",1,0,'d'}, {"mode",1,0,'m'}, {"baud",1,0,'b'},
        {"count",1,0,'n'}, {"length",1,0,'l'}, {"timeout",1,0,'t'},
        {"gap",1,0,'g'}, {"rs485",1,0,'r'}, {"verbose",0,0,'v'},
        {"txen-line",1,0,1000}, {"gpiochip",1,0,1001}, {"txen-setup-us",1,0,1002},
        {"help",0,0,'h'}, {0,0,0,0}
    };
    while ((c = getopt_long(argc, argv, "d:m:b:n:l:t:g:r:vh", opts, NULL)) != -1) {
        switch(c) {
        case 'd': device = optarg; break; case 'm': mode = optarg; break;
        case 'b': baud = number(optarg,1200,921600); break;
        case 'n': count = number(optarg,1,1000000); break;
        case 'l': len = number(optarg,1,MAX_PAYLOAD); break;
        case 't': timeout = number(optarg,100,600000); break;
        case 'g': gap = number(optarg,0,60000); break;
        case 'r': rs485 = optarg; break; case 'v': verbose = 1; break;
        case 1000: txen_line = (int)number(optarg,0,65535); break;
        case 1001: gpiochip = optarg; break;
        case 1002: txen_setup_us = number(optarg,0,1000000); break;
        case 'h': usage(argv[0]); return 0; default: return 2;
        }
    }
    if (!device || !mode || optind != argc ||
        (strcmp(mode,"ping") && strcmp(mode,"reply")) ||
        (strcmp(rs485,"keep") && strcmp(rs485,"high") && strcmp(rs485,"low") && strcmp(rs485,"off"))) {
        usage(argv[0]); return 2;
    }
    if (txen_line >= 0 && strcmp(rs485,"keep") && strcmp(rs485,"off")) {
        fprintf(stderr,"Do not combine manual TXEN with kernel RTS direction control\n"); return 2;
    }
    int is_ping = !strcmp(mode,"ping");
    if (is_ping && timeout < ((len + HEADER + 2) * 20U * 1000U / baud) + gap + 100U)
        fprintf(stderr,"WARNING: timeout may be shorter than round-trip wire time\n");
    setvbuf(stdout, NULL, _IOLBF, 0);
    atexit(cleanup);
    struct sigaction sa = {0}; sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,&sa,NULL); sigaction(SIGTERM,&sa,NULL);
    setup(device, baud, rs485);
    if (txen_line >= 0) setup_txen(gpiochip, (unsigned)txen_line);
    struct serial_icounter_struct before = {0}, after = {0};
    int have_counters = ioctl(fd, TIOCGICOUNT, &before) == 0;
    printf("READY device=%s mode=%s baud=%u format=8N1 count=%u length=%u timeout_ms=%u gap_ms=%u rs485=%s\n",
           device,mode,baud,count,len,timeout,gap,rs485);
    struct stream stream = {0};
    unsigned sent = 0, valid = 0, timeouts = 0, mismatch = 0;
    int io_failed = 0;
    int64_t started = now_ms(), rtt_sum = 0, rtt_min = INT64_MAX, rtt_max = 0;
    uint32_t base_seq = (uint32_t)started ^ ((uint32_t)getpid() << 16);
    for (unsigned i = 0; i < count && !stopped; i++) {
        struct frame expected, received;
        int64_t tick = now_ms(), deadline = tick + timeout;
        uint32_t seq = base_seq + i;
        if (is_ping) {
            request(&expected, seq, len, i % 6);
            if (send_frame(&expected, timeout) < 0) { perror("write"); io_failed=1; break; }
            sent++;
            reply(&expected);
        }
        int got = 0;
        while (!stopped) {
            int r = receive(&stream, &received, deadline);
            if (r < 0) { if (!stopped) perror("read/poll"); io_failed=1; break; }
            if (!r) break;
            if (received.data[4] != (is_ping ? 2 : 1)) { unexpected++; continue; }
            if (is_ping) {
                if (get32(received.data + 6) != seq) { unexpected++; continue; }
                if (received.size != expected.size || memcmp(received.data, expected.data, expected.size)) {
                    mismatch++; fprintf(stderr,"MISMATCH seq=%" PRIu32 "\n",seq); break;
                }
            } else {
                seq = get32(received.data + 6);
                pause_ms((int)gap);
                if (stopped) break;
                reply(&received);
                if (send_frame(&received, timeout) < 0) { perror("reply write"); io_failed=1; break; }
                sent++;
            }
            got = 1; valid++;
            int64_t rtt = now_ms() - tick;
            if (is_ping) {
                rtt_sum += rtt;
                if (rtt < rtt_min) rtt_min = rtt;
                if (rtt > rtt_max) rtt_max = rtt;
            }
            if (verbose || valid == 1 || valid % 100 == 0 || valid == count)
                printf("OK frame=%u seq=%" PRIu32 " payload=%u pattern=%u%s%" PRId64 "\n",
                       valid,seq,get16(received.data + 10),received.data[5],
                       is_ping ? " rtt_ms=" : " wait_and_reply_ms=",rtt);
            break;
        }
        if (io_failed || stopped) break;
        if (!got) {
            timeouts++;
            fprintf(stderr,"NO_VALID_REPLY_OR_REQUEST frame=%u partial_bytes=%zu\n",i+1,stream.used);
            /* A partial damaged frame must not block the next transaction. */
            discarded += stream.used; stream.used = 0;
            if (!is_ping) break;
        }
        if (is_ping) pause_ms((int)gap);
    }
    /* Bound final drain, especially needed when reply exits after its last write. */
    int queued = 0;
    int64_t drain_deadline = now_ms() + timeout;
    while (!stopped && ioctl(fd, TIOCOUTQ, &queued) == 0 && queued > 0) {
        if (now_ms() >= drain_deadline) { io_failed=1; fprintf(stderr,"Output drain timeout\n"); break; }
        pause_ms(1);
    }
    /* Some USB drivers report an empty host queue before the last wire byte. */
    if (!is_ping && sent && txen_fd < 0) pause_ms((int)((MAX_FRAME * 10U * 1000U + baud - 1) / baud + 5));
    if (have_counters && ioctl(fd,TIOCGICOUNT,&after) == 0)
        printf("UART_COUNTER_DELTA tx=%u rx=%u frame=%u parity=%u overrun=%u brk=%u buf_overrun=%u\n",
               (unsigned)after.tx-(unsigned)before.tx,(unsigned)after.rx-(unsigned)before.rx,
               (unsigned)after.frame-(unsigned)before.frame,(unsigned)after.parity-(unsigned)before.parity,
               (unsigned)after.overrun-(unsigned)before.overrun,(unsigned)after.brk-(unsigned)before.brk,
               (unsigned)after.buf_overrun-(unsigned)before.buf_overrun);
    int ok = !stopped && !io_failed && valid == count && !timeouts && !mismatch && !crc_errors && !discarded;
    printf("SUMMARY mode=%s sent=%u valid=%u expected=%u timeouts=%u mismatch=%u crc_errors=%lu discarded_bytes=%lu unexpected=%lu elapsed_ms=%" PRId64,
           mode,sent,valid,count,timeouts,mismatch,crc_errors,discarded,unexpected,now_ms()-started);
    if (is_ping && valid) printf(" rtt_min_ms=%" PRId64 " rtt_avg_ms=%.2f rtt_max_ms=%" PRId64,
                                 rtt_min,(double)rtt_sum/valid,rtt_max);
    printf(" txen_cycles=%lu result=%s\n",txen_cycles,ok ? "PASS" : "FAIL");
    return stopped ? 130 : io_failed ? 2 : ok ? 0 : 1;
}
