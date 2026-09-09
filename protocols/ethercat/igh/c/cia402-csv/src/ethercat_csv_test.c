#define _GNU_SOURCE

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>

#include <ecrt.h>

#define VENDOR_ID     0x00000009
#define PRODUCT_CODE  0x26483052
#define REVISION_NO   0x00010211
#define PERIOD_NS     2000000L

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t shutdown_in_progress = 0;

struct status_snapshot {
    atomic_uint slave_count;
    atomic_uint al_states;
    atomic_uint link_up;
    atomic_uint working_counter;
    atomic_uint wc_state;
    atomic_uint status_word;
    atomic_uint control_word;
    atomic_int target_velocity;
    atomic_int actual_position;
};

static struct status_snapshot snapshot;

static unsigned int off_control_word;
static unsigned int off_target_velocity;
static unsigned int off_status_word;
static unsigned int off_actual_position;

static ec_pdo_entry_info_t pdo_entries[] = {
    {0x6040, 0x00, 16},
    {0x60ff, 0x00, 32},
    {0x6041, 0x00, 16},
    {0x6064, 0x00, 32},
};

static ec_pdo_info_t pdos[] = {
    {0x1602, 2, pdo_entries + 0},
    {0x1a02, 2, pdo_entries + 2},
};

static ec_sync_info_t syncs[] = {
    {0, EC_DIR_OUTPUT, 0, NULL, EC_WD_DISABLE},
    {1, EC_DIR_INPUT,  0, NULL, EC_WD_DISABLE},
    {2, EC_DIR_OUTPUT, 1, pdos + 0, EC_WD_ENABLE},
    {3, EC_DIR_INPUT,  1, pdos + 1, EC_WD_DISABLE},
    {0xff, EC_DIR_INVALID, 0, NULL, EC_WD_DEFAULT}
};

static ec_pdo_entry_reg_t domain_regs[] = {
    {0, 0, VENDOR_ID, PRODUCT_CODE, 0x6040, 0, &off_control_word, NULL},
    {0, 0, VENDOR_ID, PRODUCT_CODE, 0x60ff, 0, &off_target_velocity, NULL},
    {0, 0, VENDOR_ID, PRODUCT_CODE, 0x6041, 0, &off_status_word, NULL},
    {0, 0, VENDOR_ID, PRODUCT_CODE, 0x6064, 0, &off_actual_position, NULL},
    {0}
};

enum control_strategy {
    STRATEGY_STANDARD,
    STRATEGY_FFFF,
};

static void on_signal(int signo)
{
    (void) signo;
    if (shutdown_in_progress)
        return; /* Ignore repeated signals during shutdown. */
    running = 0;
}

static void add_ns(struct timespec *t, long ns)
{
    t->tv_nsec += ns;
    while (t->tv_nsec >= 1000000000L) {
        t->tv_nsec -= 1000000000L;
        ++t->tv_sec;
    }
}

static uint64_t application_time_ns(void)
{
    /* ecrt application time is nanoseconds since 2000-01-01. */
    const uint64_t epoch_2000 = 946684800ULL;
    struct timespec now;

    clock_gettime(CLOCK_REALTIME, &now);
    return ((uint64_t) now.tv_sec - epoch_2000) * 1000000000ULL +
           (uint64_t) now.tv_nsec;
}

static void *logger_thread(void *unused)
{
    (void) unused;
    while (running) {
        sleep(1);
        if (!running)
            break;
        printf("slave=%u al=0x%02x link=%u wc=%u/%s "
               "status=0x%04x control=0x%04x target_velocity=%d actual_position=%d\n",
               atomic_load(&snapshot.slave_count),
               atomic_load(&snapshot.al_states),
               atomic_load(&snapshot.link_up),
               atomic_load(&snapshot.working_counter),
               atomic_load(&snapshot.wc_state) == EC_WC_COMPLETE ? "complete" : "incomplete",
               atomic_load(&snapshot.status_word),
               atomic_load(&snapshot.control_word),
               atomic_load(&snapshot.target_velocity),
               atomic_load(&snapshot.actual_position));
        fflush(stdout);
    }
    return NULL;
}

static uint16_t standard_control_word(uint16_t status, int *operation_enabled)
{
    uint16_t state = status & 0x006f;
    static uint16_t last_state = 0xffff;

    *operation_enabled = 0;
    if (status & 0x0008) {
        if (last_state != 0x0008)
            printf("CiA402: Fault detected (0x%04x), sending fault reset.\n", status);
        last_state = 0x0008;
        return 0x0080; /* Fault reset. */
    }

    if (state != last_state) {
        const char *name = "Unknown";
        switch (state) {
        case 0x0000: name = "Not ready to switch on"; break;
        case 0x0040: name = "Switch on disabled"; break;
        case 0x0021: name = "Ready to switch on"; break;
        case 0x0023: name = "Switched on"; break;
        case 0x0027: name = "Operation enabled"; break;
        case 0x0007: name = "Quick stop active"; break;
        case 0x000f: name = "Fault reaction active"; break;
        }
        printf("CiA402: State → %s (0x%04x)\n", name, state);
        last_state = state;
    }

    switch (state) {
    case 0x0021: /* Ready to switch on. */
        return 0x0007;
    case 0x0023: /* Switched on. */
        return 0x000f;
    case 0x0027: /* Operation enabled. */
        *operation_enabled = 1;
        return 0x000f;
    default:
        return 0x0006;
    }
}

static void usage(const char *name)
{
    fprintf(stderr,
            "Usage: %s [--enable] [--velocity N] [--strategy standard|ffff] [--cpu N]\n"
            "  Default: monitor PDO communication with zero output.\n"
            "  --enable is required before a non-zero velocity is sent.\n"
            "  --cpu binds the process to a specific CPU core (default: 2).\n",
            name);
}

int main(int argc, char **argv)
{
    ec_master_t *master = NULL;
    ec_domain_t *domain = NULL;
    ec_slave_config_t *sc = NULL;
    uint8_t *domain_pd = NULL;
    ec_domain_state_t domain_state = {0};
    ec_master_state_t master_state = {0};
    enum control_strategy strategy = STRATEGY_STANDARD;
    int enable = 0;
    int32_t requested_velocity = 50;
    int cpu_core = 2;
    uint64_t cycles = 0;
    pthread_t logger;
    int logger_started = 0;
    int rc = EXIT_FAILURE;
    int option;
    static const struct option options[] = {
        {"enable",   no_argument,       NULL, 'e'},
        {"velocity", required_argument, NULL, 'v'},
        {"strategy", required_argument, NULL, 's'},
        {"cpu",      required_argument, NULL, 'c'},
        {"help",     no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    while ((option = getopt_long(argc, argv, "ev:s:c:h", options, NULL)) != -1) {
        switch (option) {
        case 'e':
            enable = 1;
            break;
        case 'v': {
            char *end = NULL;
            long value = strtol(optarg, &end, 0);
            if (!end || *end || value < INT32_MIN || value > INT32_MAX) {
                fprintf(stderr, "Invalid velocity: %s\n", optarg);
                return EXIT_FAILURE;
            }
            requested_velocity = (int32_t) value;
            break;
        }
        case 's':
            if (!strcmp(optarg, "standard"))
                strategy = STRATEGY_STANDARD;
            else if (!strcmp(optarg, "ffff"))
                strategy = STRATEGY_FFFF;
            else {
                fprintf(stderr, "Invalid strategy: %s\n", optarg);
                return EXIT_FAILURE;
            }
            break;
        case 'c': {
            char *end = NULL;
            long value = strtol(optarg, &end, 0);
            if (!end || *end || value < 0 || value > 3) {
                fprintf(stderr, "Invalid CPU core: %s (expected 0-3)\n", optarg);
                return EXIT_FAILURE;
            }
            cpu_core = (int) value;
            break;
        }
        default:
            usage(argv[0]);
            return option == 'h' ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }

    if (!enable && requested_velocity != 50)
        fprintf(stderr, "Notice: --velocity is ignored without --enable.\n");

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    (void) mlockall(MCL_CURRENT | MCL_FUTURE);

    /* Bind to CPU core for real-time consistency. */
    {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_core, &cpuset);
        if (sched_setaffinity(0, sizeof(cpuset), &cpuset))
            fprintf(stderr, "Warning: cannot set CPU affinity: %s\n", strerror(errno));
        else
            printf("CPU affinity: bound to core %d.\n", cpu_core);
    }

    master = ecrt_request_master(0);
    if (!master) {
        fprintf(stderr, "ecrt_request_master(0) failed\n");
        goto out;
    }

    ec_slave_info_t slave_info;
    if (ecrt_master_get_slave(master, 0, &slave_info)) {
        fprintf(stderr, "Cannot read identity of slave 0\n");
        goto out;
    }
    if (slave_info.vendor_id != VENDOR_ID ||
        slave_info.product_code != PRODUCT_CODE ||
        slave_info.revision_number != REVISION_NO) {
        fprintf(stderr,
                "Slave 0 identity mismatch: expected %08x:%08x:%08x, "
                "found %08x:%08x:%08x\n",
                (unsigned int) VENDOR_ID,
                (unsigned int) PRODUCT_CODE,
                (unsigned int) REVISION_NO,
                (unsigned int) slave_info.vendor_id,
                (unsigned int) slave_info.product_code,
                (unsigned int) slave_info.revision_number);
        goto out;
    }
    printf("Slave 0: %s, identity=%08x:%08x:%08x\n",
           slave_info.name,
           (unsigned int) slave_info.vendor_id,
           (unsigned int) slave_info.product_code,
           (unsigned int) slave_info.revision_number);

    domain = ecrt_master_create_domain(master);
    if (!domain) {
        fprintf(stderr, "ecrt_master_create_domain failed\n");
        goto out;
    }
    sc = ecrt_master_slave_config(master, 0, 0, VENDOR_ID, PRODUCT_CODE);
    if (!sc) {
        fprintf(stderr, "Slave 0 identity did not match\n");
        goto out;
    }
    if (ecrt_slave_config_pdos(sc, EC_END, syncs)) {
        fprintf(stderr, "ecrt_slave_config_pdos failed\n");
        goto out;
    }
    if (ecrt_slave_config_sdo8(sc, 0x6060, 0, 9)) {
        fprintf(stderr, "Failed to queue CSV mode SDO 0x6060:0 = 9\n");
        goto out;
    }
    if (ecrt_domain_reg_pdo_entry_list(domain, domain_regs)) {
        fprintf(stderr, "ecrt_domain_reg_pdo_entry_list failed\n");
        goto out;
    }
    if (ecrt_master_activate(master)) {
        fprintf(stderr, "ecrt_master_activate failed\n");
        goto out;
    }
    domain_pd = ecrt_domain_data(domain);
    if (!domain_pd) {
        fprintf(stderr, "ecrt_domain_data failed\n");
        goto out;
    }

    printf("Started: 2 ms cycle, strategy=%s, enable=%s, requested_velocity=%d\n",
           strategy == STRATEGY_STANDARD ? "standard" : "ffff",
           enable ? "yes" : "no", requested_velocity);
    printf("Press Ctrl-C to command zero velocity and disable.\n");

    if (pthread_create(&logger, NULL, logger_thread, NULL)) {
        perror("pthread_create");
        goto out;
    }
    logger_started = 1;

    struct sched_param sched = {.sched_priority = 80};
    if (sched_setscheduler(0, SCHED_FIFO, &sched))
        fprintf(stderr, "Warning: cannot enable SCHED_FIFO: %s\n", strerror(errno));

    struct timespec wakeup;
    clock_gettime(CLOCK_MONOTONIC, &wakeup);

    while (running) {
        uint16_t status;
        uint16_t control = 0;
        int32_t target = 0;
        int32_t position;
        int operation_enabled = 0;

        add_ns(&wakeup, PERIOD_NS);
        int sleep_rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
                                       &wakeup, NULL);
        if (sleep_rc && sleep_rc != EINTR) {
            fprintf(stderr, "clock_nanosleep: %s\n", strerror(sleep_rc));
            running = 0;
            break;
        }
        if (!running)
            break;

        ecrt_master_receive(master);
        ecrt_domain_process(domain);

        status = EC_READ_U16(domain_pd + off_status_word);
        position = EC_READ_S32(domain_pd + off_actual_position);

        if (enable && cycles >= 1000) { /* Two seconds of zero PDO output first. */
            if (strategy == STRATEGY_STANDARD) {
                control = standard_control_word(status, &operation_enabled);
                if (operation_enabled)
                    target = requested_velocity;
            } else {
                control = 0xffff;
                target = requested_velocity;
            }
        }

        EC_WRITE_U16(domain_pd + off_control_word, control);
        EC_WRITE_S32(domain_pd + off_target_velocity, target);

        ecrt_master_application_time(master, application_time_ns());
        ecrt_domain_queue(domain);
        ecrt_master_send(master);
        ++cycles;

        if (cycles % 500 == 0) {
            ecrt_domain_state(domain, &domain_state);
            ecrt_master_state(master, &master_state);
            atomic_store(&snapshot.slave_count, master_state.slaves_responding);
            atomic_store(&snapshot.al_states, master_state.al_states);
            atomic_store(&snapshot.link_up, master_state.link_up);
            atomic_store(&snapshot.working_counter, domain_state.working_counter);
            atomic_store(&snapshot.wc_state, domain_state.wc_state);
            atomic_store(&snapshot.status_word, status);
            atomic_store(&snapshot.control_word, control);
            atomic_store(&snapshot.target_velocity, target);
            atomic_store(&snapshot.actual_position, position);
        }
    }

    /* CiA 402 shutdown: send disable command for 500 ms before releasing. */
    printf("Shutting down: sending disable sequence...\n");
    shutdown_in_progress = 1;
    for (int i = 0; i < 250; ++i) {
        add_ns(&wakeup, PERIOD_NS);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wakeup, NULL);
        ecrt_master_receive(master);
        ecrt_domain_process(domain);
        /* CiA 402: 0x0000 transitions to "Switch on disabled" */
        EC_WRITE_U16(domain_pd + off_control_word, 0x0000);
        EC_WRITE_S32(domain_pd + off_target_velocity, 0);
        ecrt_domain_queue(domain);
        ecrt_master_send(master);
    }
    rc = EXIT_SUCCESS;

out:
    running = 0;
    if (logger_started)
        pthread_join(logger, NULL);
    if (master)
        ecrt_release_master(master);
    return rc;
}
