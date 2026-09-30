#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <getopt.h>
#include <inttypes.h>
#include <linux/gpio.h>
#include <linux/serial.h>
#include <math.h>
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

/* Standalone Modbus RTU sensor reader.
 * Read FC03/FC04 registers, then decode using command-line parameters.
 */
struct options {
    const char *device, *chip, *type, *order, *name, *label_unit;
    unsigned baud, unit, function, start, quantity, count, interval, timeout, stopbits, line;
    double scale, offset;
    char parity;
    int gpio, verbose, raw, precision;
};
static struct options opt = {
    .device="/dev/ttyS2", .chip="/dev/gpiochip0", .type="u16", .order="ABCD",
    .name="", .label_unit="", .baud=9600, .unit=1, .function=3, .start=0,
    .quantity=1, .count=1, .interval=1000, .timeout=1000, .stopbits=1, .line=15,
    .scale=1, .offset=0, .parity='N', .gpio=-1, .precision=-1
};
static int serial_fd=-1, gpio_fd=-1, saved_attrs_ok, rs485_changed, exclusive;
static int gpio_was_input;
static struct termios saved_attrs;
static struct serial_rs485 saved_rs485;
static volatile sig_atomic_t stopped;
static unsigned long bad_crc, ignored_bytes;

/* Read monotonic time in milliseconds. */
static int64_t milliseconds(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
/* Request a clean exit on a termination signal. */
static void interrupt_handler(int signal_number) {
    (void)signal_number;
    stopped=1;
}
/* Wait for the requested delay or an interrupt. */
static void delay_us(unsigned microseconds) {
    struct timespec t = {microseconds / 1000000, (long)(microseconds % 1000000) * 1000};
    while (!stopped && nanosleep(&t, &t) < 0 && errno == EINTR) {}
}
/* Set TXEN high to send or low to receive. */
static int direction(int high) {
    struct gpiohandle_data value = {{0}};
    value.values[0]=(unsigned char)high;
    return ioctl(gpio_fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &value);
}
/* Release GPIO and restore the serial settings. */
static void cleanup(void) {
    if (gpio_fd >= 0) {
        if (direction(0) < 0) perror("TXEN receive");
        if (gpio_was_input) {
            struct gpiohandle_config cfg={0};
            cfg.flags=GPIOHANDLE_REQUEST_INPUT;
            if (ioctl(gpio_fd, GPIOHANDLE_SET_CONFIG_IOCTL, &cfg) < 0)
                perror("restore TXEN input");
        }
        close(gpio_fd);
        gpio_fd=-1;
    }
    if (serial_fd < 0) return;
    if (rs485_changed && ioctl(serial_fd, TIOCSRS485, &saved_rs485) < 0)
        perror("restore kernel RS485 mode");
    if (saved_attrs_ok && tcsetattr(serial_fd, TCSANOW, &saved_attrs) < 0)
        perror("restore serial settings");
    if (exclusive && ioctl(serial_fd, TIOCNXCL) < 0) perror("release serial port");
    close(serial_fd);
    serial_fd=-1;
}
/* Print a system error and exit. */
static void fatal(const char *message) {
    perror(message);
    exit(2);
}
/* Parse an integer within the allowed range. */
static unsigned integer(const char *text, unsigned low, unsigned high) {
    char *end;
    errno=0;
    unsigned long n=strtoul(text, &end, (text[0]=='0' && (text[1]=='x' || text[1]=='X')) ? 16 : 10);
    if (errno || !*text || *end || *text=='-' || n<low || n>high) {
        fprintf(stderr, "Invalid argument: %s (range %u..%u)\n", text, low, high);
        exit(2);
    }
    return (unsigned)n;
}
/* Parse a finite numeric value. */
static double numeric(const char *text) {
    char *end;
    errno=0;
    double n=strtod(text, &end);
    if (errno || !*text || *end || !isfinite(n)) {
        fprintf(stderr, "Invalid numeric parameter: %s\n", text);
        exit(2);
    }
    return n;
}
/* Convert a baud rate to a termios constant. */
static speed_t baud_speed(unsigned baud) {
    switch (baud) {
    case 1200: return B1200; case 2400: return B2400;
    case 4800: return B4800; case 9600: return B9600;
    case 19200: return B19200; case 38400: return B38400;
    case 57600: return B57600; case 115200: return B115200;
    default: fprintf(stderr, "Unsupported baud rate: %u\n", baud); exit(2);
    }
}
/* Open the serial port and configure optional TXEN control. */
static void open_devices(void) {
    speed_t speed=baud_speed(opt.baud);
    serial_fd=open(opt.device, O_RDWR|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);
    if (serial_fd<0) fatal("open serial");
    if (ioctl(serial_fd, TIOCEXCL)<0) fatal("exclusive serial access");
    exclusive=1;
    if (tcgetattr(serial_fd, &saved_attrs)<0) fatal("read serial settings");
    saved_attrs_ok=1;
    struct termios t=saved_attrs;
    cfmakeraw(&t);
    t.c_cflag &= ~(CSIZE|PARENB|PARODD|CSTOPB|CRTSCTS);
#ifdef CMSPAR
    t.c_cflag &= ~CMSPAR;
#endif
    t.c_cflag |= CS8|CREAD|CLOCAL;
    if (opt.parity!='N') {
        t.c_cflag |= PARENB;
        t.c_iflag |= INPCK;
        if (opt.parity=='O') t.c_cflag |= PARODD;
    }
    if (opt.stopbits==2) t.c_cflag |= CSTOPB;
    t.c_cc[VMIN]=0;
    t.c_cc[VTIME]=0;
    if (cfsetispeed(&t, speed)<0 || cfsetospeed(&t, speed)<0 ||
        tcsetattr(serial_fd, TCSANOW, &t)<0) fatal("configure serial");
    struct termios actual;
    if (tcgetattr(serial_fd, &actual)<0) fatal("verify serial");
    tcflag_t mask=CSIZE|PARENB|PARODD|CSTOPB|CRTSCTS;
    if ((actual.c_cflag & mask)!=(t.c_cflag & mask) ||
        cfgetispeed(&actual)!=speed || cfgetospeed(&actual)!=speed) {
        fprintf(stderr, "The serial driver did not retain the requested baud rate or framing settings.\n");
        exit(2);
    }
    if (opt.gpio) {
        if (ioctl(serial_fd, TIOCGRS485, &saved_rs485)<0) fatal("read kernel RS485 mode");
        struct serial_rs485 disabled={0};
        if (ioctl(serial_fd, TIOCSRS485, &disabled)<0) fatal("disable kernel RTS control");
        rs485_changed=1;
        if (ioctl(serial_fd, TIOCGRS485, &disabled)<0) fatal("verify kernel RS485 mode");
        if (disabled.flags & SER_RS485_ENABLED) {
            fprintf(stderr, "Kernel RTS direction control is still enabled; cannot use a separate TXEN GPIO.\n");
            exit(2);
        }
        int lsr;
        if (ioctl(serial_fd, TIOCSERGETLSR, &lsr)<0) fatal("UART transmitter-empty support");
        int chip=open(opt.chip, O_RDONLY|O_CLOEXEC);
        if (chip<0) fatal("open GPIO controller");
        struct gpioline_info info={0};
        info.line_offset=opt.line;
        if (ioctl(chip, GPIO_GET_LINEINFO_IOCTL, &info)<0) {
            int error=errno; close(chip); errno=error; fatal("read GPIO state");
        }
        gpio_was_input=!(info.flags & GPIOLINE_FLAG_IS_OUT);
        struct gpiohandle_request request={0};
        request.lineoffsets[0]=opt.line;
        request.flags=GPIOHANDLE_REQUEST_OUTPUT;
        request.lines=1;
        snprintf(request.consumer_label, sizeof(request.consumer_label), "sensor-read");
        int rc=ioctl(chip, GPIO_GET_LINEHANDLE_IOCTL, &request);
        int error=errno;
        close(chip);
        errno=error;
        if (rc<0) fatal("request TXEN GPIO (check other programs)");
        gpio_fd=request.fd;
    }
    if (tcflush(serial_fd, TCIOFLUSH)<0) fatal("clear serial buffers");
}
/* Wait for serial readiness until the deadline. */
static int wait_for(short events, int64_t deadline) {
    while (!stopped) {
        int64_t remaining=deadline-milliseconds();
        if (remaining<=0) return 0;
        struct pollfd p={serial_fd, events, 0};
        int rc=poll(&p, 1, (int)remaining);
        if (rc<0) { if (errno==EINTR) continue; return -1; }
        if (rc==0) return 0;
        if (p.revents & (POLLERR|POLLHUP|POLLNVAL)) { errno=EIO; return -1; }
        if (p.revents & events) return 1;
    }
    errno=EINTR;
    return -1;
}
/* Send a request and switch to receive after the UART is empty. */
static int send_request(const unsigned char *request, size_t length) {
    int64_t deadline=milliseconds()+opt.timeout;
    int result=-1;
    if (gpio_fd>=0) {
        if (direction(1)<0) return -1;
        delay_us(20);
    }
    size_t sent=0;
    while (!stopped && sent<length) {
        int ready=wait_for(POLLOUT, deadline);
        if (ready<=0) { if (!ready) errno=ETIMEDOUT; goto done; }
        ssize_t n=write(serial_fd, request+sent, length-sent);
        if (n<0) {
            if (errno==EINTR || errno==EAGAIN) continue;
            goto done;
        }
        sent+=(size_t)n;
    }
    if (stopped) { errno=EINTR; goto done; }
    if (gpio_fd>=0) {
        while (!stopped) {
            int queued, lsr;
            if (ioctl(serial_fd, TIOCOUTQ, &queued)<0 ||
                ioctl(serial_fd, TIOCSERGETLSR, &lsr)<0) goto done;
            if (!queued && (lsr & TIOCSER_TEMT)) { result=0; break; }
            if (milliseconds()>=deadline) { errno=ETIMEDOUT; goto done; }
            delay_us(20);
        }
        if (stopped) { result=-1; errno=EINTR; }
    } else result=0;
done:
    {
        int error=errno;
        if (gpio_fd>=0 && direction(0)<0) return -1;
        errno=error;
    }
    return result;
}
/* Calculate the Modbus RTU CRC. */
static uint16_t crc16(const unsigned char *data, size_t length) {
    uint16_t crc=0xffff;
    while (length--) {
        crc ^= *data++;
        for (unsigned i=0; i<8; i++)
            crc=(crc & 1) ? (crc >> 1)^0xa001 : crc >> 1;
    }
    return crc;
}
/* Print a frame when verbose output is enabled. */
static void dump(const char *label, const unsigned char *data, size_t length) {
    if (!opt.verbose) return;
    fprintf(stderr, "%s", label);
    for (size_t i=0; i<length; i++) fprintf(stderr, " %02x", data[i]);
    fputc('\n', stderr);
}
/* 0=data, 1=timeout, 2=Modbus exception, -1=I/O error. */
/* Read and validate a Modbus register response. */
static int read_registers(unsigned address, unsigned quantity, uint16_t *values,
                          unsigned *exception_code) {
    unsigned char request[8]={
        (unsigned char)opt.unit, (unsigned char)opt.function,
        (unsigned char)(address>>8), (unsigned char)address,
        0, (unsigned char)quantity, 0, 0
    };
    uint16_t crc=crc16(request, 6);
    request[6]=(unsigned char)crc;
    request[7]=(unsigned char)(crc>>8);
    /* Exceeds 3.5 character times for all supported baud rates. */
    delay_us(40000);
    if (stopped) { errno=EINTR; return -1; }
    if (tcflush(serial_fd, TCIFLUSH)<0) return -1;
    dump("TX", request, sizeof(request));
    if (send_request(request, sizeof(request))<0) return -1;
    unsigned char buffer[256];
    size_t used=0;
    int64_t deadline=milliseconds()+opt.timeout;
    while (!stopped) {
        size_t offset=0;
        while (offset<used) {
            unsigned char *p=buffer+offset;
            size_t remaining=used-offset;
            size_t prefix=remaining<8 ? remaining : 8;
            if (!memcmp(p, request, prefix)) {
                if (remaining<8) break;
                offset+=8;
                ignored_bytes+=8;
                continue;
            }
            if (p[0]!=opt.unit) { offset++; ignored_bytes++; continue; }
            if (remaining<2) break;
            int exception=p[1]==(opt.function|0x80);
            if (!exception && p[1]!=opt.function) { offset++; ignored_bytes++; continue; }
            if (remaining<3) break;
            if (!exception && p[2]!=quantity*2) { offset++; ignored_bytes++; continue; }
            size_t length=exception ? 5 : 5+quantity*2;
            if (remaining<length) break;
            uint16_t received=(uint16_t)(p[length-2]|((uint16_t)p[length-1]<<8));
            if (crc16(p, length-2)!=received) {
                bad_crc++; offset++; ignored_bytes++; continue;
            }
            dump("RX", p, length);
            if (exception) { *exception_code=p[2]; return 2; }
            for (unsigned i=0; i<quantity; i++)
                values[i]=(uint16_t)(((uint16_t)p[3+i*2]<<8)|p[4+i*2]);
            return 0;
        }
        if (offset) { memmove(buffer, buffer+offset, used-offset); used-=offset; }
        int ready=wait_for(POLLIN, deadline);
        if (ready<=0) return ready<0 ? -1 : 1;
        ssize_t n=read(serial_fd, buffer+used, sizeof(buffer)-used);
        if (n<0) {
            if (errno==EINTR || errno==EAGAIN) continue;
            return -1;
        }
        used+=(size_t)n;
    }
    errno=EINTR;
    return -1;
}

/* Return the register count used by one decoded value. */
static unsigned value_width(void) {
    if (!strcmp(opt.type,"u16") || !strcmp(opt.type,"i16") ||
        !strcmp(opt.type,"signmag16")) return 1;
    if (!strcmp(opt.type,"u32") || !strcmp(opt.type,"i32") ||
        !strcmp(opt.type,"f32")) return 2;
    fprintf(stderr,"Unknown type: %s. Use u16, i16, signmag16, u32, i32 or f32.\n",opt.type);
    exit(2);
}
/* Decode registers and apply the scale and offset. */
static double decode_value(const uint16_t *registers) {
    double value;
    if (value_width()==1) {
        int32_t n=registers[0];
        if (!strcmp(opt.type,"i16") && n>=32768) n-=65536;
        if (!strcmp(opt.type,"signmag16") && (n & 0x8000)) n=-(n & 0x7fff);
        value=n;
    } else {
        unsigned char bytes[4]={
            (unsigned char)(registers[0]>>8), (unsigned char)registers[0],
            (unsigned char)(registers[1]>>8), (unsigned char)registers[1]
        };
        uint32_t bits=0;
        for (unsigned i=0; i<4; i++)
            bits=(bits<<8)|bytes[opt.order[i]-'A'];
        if (!strcmp(opt.type,"f32")) {
            float f;
            _Static_assert(sizeof(float)==4 && FLT_RADIX==2 &&
                           FLT_MANT_DIG==24 && FLT_MAX_EXP==128,
                           "f32 requires IEEE 754 binary32 float");
            memcpy(&f,&bits,sizeof(f));
            value=f;
        } else if (!strcmp(opt.type,"i32") && (bits & UINT32_C(0x80000000))) {
            value=(double)((int64_t)bits-INT64_C(4294967296));
        } else value=bits;
    }
    return value*opt.scale+opt.offset;
}
/* Print command-line options and defaults. */
static void usage(const char *program) {
    printf("Usage: %s [options]\n"
           "Read-only Modbus RTU sensor reader; no sensor-specific register mapping.\n"
           "Defaults: /dev/ttyS2, 9600 8N1, unit 1, FC03, R0, one u16 value, read once.\n"
           "  -d, --device PATH     Serial port; USB adapters use automatic direction\n"
           "  -b, --baud N          Baud rate, 1200..115200 (standard rates)\n"
           "  -p, --parity N|E|O    Parity (default: N)\n"
           "      --stopbits 1|2    Stop bits (default: 1)\n"
           "  -a, --address N       Modbus unit address, 1..247 (default: 1)\n"
           "  -f, --function 3|4    Read holding/input registers (default: 3)\n"
           "  -s, --start N         Protocol register address, decimal or 0x hex (default: 0)\n"
           "  -q, --quantity N      Register count, 1..16 (default: 1)\n"
           "      --type TYPE       u16, i16, signmag16, u32, i32, f32 (default: u16)\n"
           "      --scale N         Multiply decoded values by N (default: 1)\n"
           "      --offset N        Add N after scaling (default: 0)\n"
           "      --name TEXT       Optional display name\n"
           "      --unit TEXT       Optional display unit, e.g. C, %%RH, kPa\n"
           "      --order ORDER     32-bit byte order: ABCD, BADC, CDAB, DCBA (default: ABCD)\n"
           "      --precision N     Fixed decimal places, 0..6 (default: automatic)\n"
           "      --raw             Also show original register values\n"
           "  -n, --count N         Request count; 0=continuous (default: 1)\n"
           "  -i, --interval MS     Minimum interval between read starts (default: 1000)\n"
           "  -t, --timeout MS      Response timeout (default: 1000)\n"
           "      --gpiochip PATH   Enable manual TXEN using this GPIO chip\n"
           "      --txen-line N     Enable manual TXEN using this line offset\n"
           "      --auto-direction  Disable GPIO control for an automatic adapter\n"
           "  -v, --verbose         Show TX/RX frames and diagnostics\n"
           "  -h, --help            Show this help\n"
           "On /dev/ttyS2, TXEN defaults to /dev/gpiochip0 line 15 (high=TX, low=RX).\n"
           "32-bit types consume two registers each; -q must be even.\n"
           "One type/scale/offset/unit applies to all values in a request.\n"
           "Exit: 0=all reads succeeded, 1=read/decode failure, 2=setup error, 130=interrupted.\n",
           program);
}
/* Parse options and run the sensor read loop. */
int main(int argc, char **argv) {
    static const struct option options[]={
        {"device",1,0,'d'}, {"baud",1,0,'b'}, {"parity",1,0,'p'},
        {"address",1,0,'a'}, {"function",1,0,'f'}, {"start",1,0,'s'},
        {"quantity",1,0,'q'}, {"count",1,0,'n'}, {"interval",1,0,'i'},
        {"timeout",1,0,'t'}, {"verbose",0,0,'v'}, {"help",0,0,'h'},
        {"type",1,0,1000}, {"scale",1,0,1001}, {"offset",1,0,1002},
        {"name",1,0,1003}, {"unit",1,0,1004}, {"order",1,0,1005},
        {"precision",1,0,1006}, {"raw",0,0,1007}, {"stopbits",1,0,1008},
        {"gpiochip",1,0,1009}, {"txen-line",1,0,1010},
        {"auto-direction",0,0,1011}, {0,0,0,0}
    };
    int c;
    while ((c=getopt_long(argc,argv,"d:b:p:a:f:s:q:n:i:t:vh",options,NULL))!=-1) {
        switch (c) {
        case 'd': opt.device=optarg; break;
        case 'b': opt.baud=integer(optarg,1200,115200); break;
        case 'p':
            if (strlen(optarg)!=1 || !strchr("NEO",optarg[0])) {
                fprintf(stderr,"Parity must be N, E or O.\n"); return 2;
            }
            opt.parity=optarg[0]; break;
        case 'a': opt.unit=integer(optarg,1,247); break;
        case 'f': opt.function=integer(optarg,3,4); break;
        case 's': opt.start=integer(optarg,0,65535); break;
        case 'q': opt.quantity=integer(optarg,1,16); break;
        case 'n': opt.count=integer(optarg,0,1000000); break;
        case 'i': opt.interval=integer(optarg,50,60000); break;
        case 't': opt.timeout=integer(optarg,100,10000); break;
        case 'v': opt.verbose=1; break;
        case 'h': usage(argv[0]); return 0;
        case 1000: opt.type=optarg; break;
        case 1001: opt.scale=numeric(optarg); break;
        case 1002: opt.offset=numeric(optarg); break;
        case 1003: opt.name=optarg; break;
        case 1004: opt.label_unit=optarg; break;
        case 1005: opt.order=optarg; break;
        case 1006: opt.precision=(int)integer(optarg,0,6); break;
        case 1007: opt.raw=1; break;
        case 1008: opt.stopbits=integer(optarg,1,2); break;
        case 1009: opt.chip=optarg; opt.gpio=1; break;
        case 1010: opt.line=integer(optarg,0,65535); opt.gpio=1; break;
        case 1011: opt.gpio=0; break;
        default: return 2;
        }
    }
    unsigned width=value_width();
    if (optind!=argc || opt.start+opt.quantity>65536 || opt.quantity%width ||
        (strcmp(opt.order,"ABCD") && strcmp(opt.order,"BADC") &&
         strcmp(opt.order,"CDAB") && strcmp(opt.order,"DCBA")) ||
        (width==1 && strcmp(opt.order,"ABCD"))) {
        fprintf(stderr,"Invalid register range or byte order; 32-bit values require an even register count. See --help.\n");
        return 2;
    }
    if (opt.gpio<0) opt.gpio=!strcmp(opt.device,"/dev/ttyS2");
    setvbuf(stdout,NULL,_IOLBF,0);
    atexit(cleanup);
    signal(SIGINT,interrupt_handler);
    signal(SIGTERM,interrupt_handler);
    open_devices();
    printf("Sensor reader: %s, %u 8%c%u, unit %u, FC%02u, start=%u, registers=%u\n",
           opt.device,opt.baud,opt.parity,opt.stopbits,opt.unit,opt.function,opt.start,opt.quantity);
    printf("Decode: %s, order=%s, value=decoded * %.10g + %.10g\n",
           opt.type,opt.order,opt.scale,opt.offset);
    if (!opt.count) puts("Press Ctrl+C to stop.");
    uint64_t attempts=0, good=0, failed=0;
    while (!stopped && (!opt.count || attempts<opt.count)) {
        int64_t started=milliseconds();
        uint16_t registers[16]={0};
        double values[16]={0};
        unsigned exception=0;
        int rc=read_registers(opt.start,opt.quantity,registers,&exception);
        if (stopped) break;
        attempts++;
        if (rc) {
            failed++;
            if (rc==1) fprintf(stderr,"Read failed: timeout. Check wiring, serial settings, unit and register address.\n");
            else if (rc==2) fprintf(stderr,"Read failed: Modbus exception 0x%02x. Check function and register range.\n",exception);
            else { perror("Read failed: serial I/O"); break; }
        } else {
            int valid=1;
            for (unsigned i=0; i<opt.quantity; i+=width) {
                values[i]=decode_value(registers+i);
                if (!isfinite(values[i])) valid=0;
            }
            if (!valid) {
                failed++;
                fprintf(stderr,"Decode failed: non-finite value. Check data type, byte order and scale.\n");
            } else {
                good++;
                time_t now=time(NULL);
                struct tm tm;
                char stamp[32];
                localtime_r(&now,&tm);
                strftime(stamp,sizeof(stamp),"%Y-%m-%d %H:%M:%S",&tm);
                for (unsigned i=0; i<opt.quantity; i+=width) {
                    printf("[%s] ",stamp);
                    if (*opt.name) printf("%s (R%u): ",opt.name,opt.start+i);
                    else printf("R%u: ",opt.start+i);
                    if (opt.precision>=0) printf("%.*f",opt.precision,values[i]);
                    else printf("%.10g",values[i]);
                    if (*opt.label_unit) printf(" %s",opt.label_unit);
                    if (opt.raw) {
                        printf(" [raw:");
                        for (unsigned j=0; j<width; j++)
                            printf(" R%u=0x%04x(%u)",opt.start+i+j,registers[i+j],registers[i+j]);
                        putchar(']');
                    }
                    putchar('\n');
                }
            }
        }
        if (opt.count && attempts>=opt.count) break;
        int64_t remaining=started+opt.interval-milliseconds();
        if (remaining>0) delay_us((unsigned)remaining*1000);
    }
    printf("Summary: successful=%" PRIu64 ", failed=%" PRIu64 ".\n",good,failed);
    if (opt.verbose) fprintf(stderr,"Diagnostics: CRC errors=%lu, discarded bytes=%lu\n",bad_crc,ignored_bytes);
    if (stopped) return 130;
    return failed || good==0 ? 1 : 0;
}
