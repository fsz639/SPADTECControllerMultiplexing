//=============================================================================
// tec_controller.cpp  -  TEC Control 4 SPAD (BeagleBone Black)
// QuNet Lab
// Miquel Alquézar Duran, Ferran Saigi, Marc Jofre
//=============================================================================
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <iostream>
#include <atomic>

using namespace std;

#define TEMP_SET    18.0
#define TEMP_HYST    0.1
#define TEMP_HIGH   (TEMP_SET + TEMP_HYST)
#define TEMP_LOW    (TEMP_SET - TEMP_HYST)

#define SLOT_US      100000   // 100 ms per channel
#define DEAD_US        2000   // 2 ms dead time (SHDN=0)

static const int SEL_GPIO[4] = { 66, 67, 69, 68 }; // In this order in order to activate the physical BBB pins in order (P8.7, P8.8, P8.9, and P8.10)
static const int SHDN_GPIO   = 45; // BBB pin P8.11

//  I2C-2 configuration: ADS1115 (P9.19 / P9.20)
#define I2C_BUS        "/dev/i2c-2"
#define ADS1115_ADDR   0x48
#define ADS_REG_CONV   0x00
#define ADS_REG_CONFIG 0x01
#define ADS_LSB        (2.048 / 32768.0)

//  I2C-1 configuration: DAC MCP4725 (P9.17 / P9.18)
#define I2C_BUS_DAC     "/dev/i2c-1"
#define MCP4725_ADDR    0x60     // Direccion habitual (o 0x61)
#define DAC_VDD         5.00     // Tensión real de alimentacion VDD del DAC (5.0V)
#define MAX_SETPOINT_V  1.50     // Limite absoluto seguro para la entrada REF del MAX1968
#define DAC_MAX_RAW     4095.0   // Resolucion de 12 bits (2^12 - 1)

#define VSUPPLY   1.50
#define R_TOP     10000.0
#define R0_NTC    10000.0
#define T0_KELVIN 298.15
#define BETA      3950.0

static volatile sig_atomic_t g_run = 1;

static int g_sel_fd[4] = {-1,-1,-1,-1};
static int g_shdn_fd = -1;
static int g_i2c = -1;

// ---------- Robust GPIO sysfs Helpers ----------
static void gpio_export(int n) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d", n);
    // Skip export if directory already exists
    if (access(path, F_OK) == 0) return;

    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd >= 0) {
        char b[8];
        int l = snprintf(b, sizeof(b), "%d", n);
        write(fd, b, l);
        close(fd);
    }
}

static void gpio_unexport(int n) {
    int fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (fd >= 0) {
        char b[8];
        int l = snprintf(b, sizeof(b), "%d", n);
        write(fd, b, l);
        close(fd);
    }
}

// Configures direction to output AND forces initial state to 0
static bool gpio_dir_out(int n) {
    char p[64];
    snprintf(p, sizeof(p), "/sys/class/gpio/gpio%d/direction", n);

    // Retry opening direction node for up to 2 seconds (handles udev delay on cold boot)
    int fd = -1;
    for (int retry = 0; retry < 20; ++retry) {
        fd = open(p, O_WRONLY);
        if (fd >= 0) break;
        usleep(100000); // 100ms
    }

    if (fd < 0) {
        fprintf(stderr, "[!] ERROR: Could not open direction node for GPIO %d\n", n);
        return false;
    }

    // Writing "low" sets direction to OUT and initial value to 0 atomically
    ssize_t ret = write(fd, "low", 3);
    close(fd);

    if (ret != 3) {
        fprintf(stderr, "[!] ERROR: Failed setting direction 'low' for GPIO %d\n", n);
        return false;
    }
    return true;
}

static int gpio_val_fd(int n) {
    char p[64];
    snprintf(p, sizeof(p), "/sys/class/gpio/gpio%d/value", n);
    return open(p, O_WRONLY);
}

static void gpio_write(int fd, int v) {
    if (fd >= 0) {
              lseek(fd, 0, SEEK_SET);
        char c = v ? '1' : '0';
        if (write(fd, &c, 1) < 0) {
            // Ignore unused return warning
        }
    }
}

// ---------- Hardware Control ----------
static void select_channel(int ch) {
    for (int i = 0; i < 4; i++) {
        gpio_write(g_sel_fd[i], (i == ch) ? 1 : 0);
    }
}

static void set_shdn(int v) {
    gpio_write(g_shdn_fd, v ? 0 : 1);
}

static void switch_to(int ch) {
    set_shdn(0);
    usleep(DEAD_US);
    select_channel(ch);
    set_shdn(1);
}

// ---------- DAC MCP4725 ----------
static bool dac_set_voltage(double volts) {
    if (g_i2c_dac < 0) return false;

    // Set maximum setpoint to 1.5V
    if (volts < 0.0) volts = 0.0;
    if (volts > MAX_SETPOINT_V) volts = MAX_SETPOINT_V;

    // Volts proportional to VCC (5V)
    uint16_t raw = (uint16_t)((volts / DAC_VDD) * DAC_MAX_RAW);

    // Write data
    uint8_t data[2];
    data[0] = (uint8_t)((raw >> 8) & 0x0F);
    data[1] = (uint8_t)(raw & 0xFF);

    if (write(g_i2c_dac, data, 2) != 2) {
        fprintf(stderr, "[!] Error when writing in DAC MCP4725\n");
        return false;
    }
    return true;
}

// ---------- ADS1115 ----------
static double ads_read_volts(int ch) {
    uint16_t cfg = 0x8000 | ((0x4 + ch) << 12) | (0x2 << 9) | (0x1 << 8) | (0x4 << 5) | 0x03;
    uint8_t w[3] = {ADS_REG_CONFIG, (uint8_t)(cfg >> 8), (uint8_t)(cfg & 0xFF)};
    if (write(g_i2c, w, 3) != 3) return NAN;
    usleep(9000);
    uint8_t reg = ADS_REG_CONV;
    if (write(g_i2c, &reg, 1) != 1) return NAN;
    uint8_t r[2];
    if (read(g_i2c, r, 2) != 2) return NAN;
    return (int16_t)((r[0] << 8) | r[1]) * ADS_LSB;
}

static double read_temp(int ch) {
    double v = ads_read_volts(ch);
    if (v <= 0.0 || v >= VSUPPLY) return NAN;
    double R = R_TOP * v / (VSUPPLY - v);
    return 1.0 / (1.0 / T0_KELVIN + (1.0 / BETA) * log(R / R0_NTC)) - 273.15;
}

// ---------- Open Loop Mode ----------
bool enable_print = true;

static void run_open_loop(void) {
    double temps[4] = {0};
    time_t last_print = 0;

    while (g_run) {
        for (int ch = 0; ch < 4 && g_run; ++ch) {
            switch_to(ch);
            temps[ch] = read_temp(ch);
            usleep(SLOT_US);
        }

        time_t now = time(NULL);
        if (enable_print && (now - last_print >= 3)) {
            printf("\033[H\033[J");
            for (int i = 0; i < 4; ++i) {
                printf("TEC%d active | NTC%d = %.1f C\n", i + 1, i + 1, temps[i]);
            }
            printf("Ctrl+C to stop the multiplexed TEC controller\n");
            fflush(stdout);
            last_print = now;
        }
    }
}

// ---------- Manual Test Mode ----------
static void run_test() {
    printf("\n=== MANUAL TEST MODE ===\n"
           " 1..4  : select channel\n"
           " o / f : SHDN On / Off\n"
           " r     : read 4 NTC\n"
           " x     : all OFF\n"
           " q     : exit\n\n");
    int cur_ch = -1, shdn = 0;
    char line[32];
    while (g_run) {
        printf("[channel=%s SHDN=%s] > ", cur_ch < 0 ? "-" : (cur_ch == 0 ? "1" : cur_ch == 1 ? "2" : cur_ch == 2 ? "3" : "4"), shdn ? "ON" : "off");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) break;
        char c = line[0];
        if (c == 'q') break;
        else if (c >= '1' && c <= '4') {
            cur_ch = c - '1';
            set_shdn(0);
            shdn = 0;
            select_channel(cur_ch);
            printf(" -> channel %d selected\n", cur_ch + 1);
        } else if (c == 'o') {
            if (cur_ch < 0) printf(" select channel first\n");
            else { set_shdn(1); shdn = 1; printf(" -> SHDN ON\n"); }
        } else if (c == 'f') { set_shdn(0); shdn = 0; printf(" -> SHDN off\n"); }
        else if (c == 'r') { for (int ch = 0; ch < 4; ch++) printf("   NTC%d = %.2f C\n", ch + 1, read_temp(ch)); }
        else if (c == 'x') { set_shdn(0); shdn = 0; select_channel(-1); cur_ch = -1; printf(" -> all off\n"); }
    }
}

// ------------ Closed loop mode ------------

static void run_closed_loop(void) {
    double temp_setpoint = TEMP_SET;
    double Kp = 0.05; // Proportional gain
    double temps[4] = {0};
    double current_dac_v = 0.75; // Initial center point voltage (25ºC)
    time_t last_print = 0;

    printf("[CLOSED LOOP] Initiating temperature control (Target: %.1f C)...\n", temp_setpoint);

    while (g_run) {
        for (int ch = 0; ch < 4 && g_run; ++ch) {
            switch_to(ch);
            temps[ch] = read_temp(ch);

            if (!isnan(temps[ch])) {
                // Basic control algorithm
                double error = temps[ch] - temp_setpoint;
                current_dac_v = 0.75 + (Kp * error);
                
                // Apply correction to DAC in every cycle
                dac_set_voltage(current_dac_v);
            }
            usleep(SLOT_US);
        }

        time_t now = time(NULL);
        if (enable_print && (now - last_print >= 3)) {
            printf("\033[H\033[J");
            printf("=== LOOP MODE ABORTED (Target: %.1f C) ===\n", temp_setpoint);
            for (int i = 0; i < 4; ++i) {
                printf("TEC%d active | NTC%d = %.2f C | DAC Setpoint = %.3f V\n", 
                       i + 1, i + 1, temps[i], current_dac_v);
            }
            printf("\nCtrl+C to stop controller.\n");
            fflush(stdout);
            last_print = now;
        }
    }
}

// ---------- Signal Handlers ----------
std::atomic<bool> signalReceivedFlag{false};

static void SignalINTHandler(int s) {
    signalReceivedFlag.store(true);
    g_run = 0;
}

static void SignalTERMHandler(int s) {
    signalReceivedFlag.store(true);
    g_run = 0;
}

// ---------- Main ----------
int main(int argc, char** argv) {
    signal(SIGINT, SignalINTHandler);
    signal(SIGTERM, SignalTERMHandler);

    // 1. Export GPIOs
    for (int i = 0; i < 4; i++) {
        gpio_export(SEL_GPIO[i]);
    }
    gpio_export(SHDN_GPIO);

    // 2. Set Direction to Output (with retry loop)
    for (int i = 0; i < 4; i++) {
        if (!gpio_dir_out(SEL_GPIO[i])) {
            fprintf(stderr, "[!] CRITICAL: Failed setting direction for SEL GPIO %d!\n", SEL_GPIO[i]);
        }
        g_sel_fd[i] = gpio_val_fd(SEL_GPIO[i]);
        if (g_sel_fd[i] < 0) {
            fprintf(stderr, "[!] CRITICAL: Failed opening value fd for SEL GPIO %d!\n", SEL_GPIO[i]);
        }
    }

    if (!gpio_dir_out(SHDN_GPIO)) {
        fprintf(stderr, "[!] CRITICAL: Failed setting direction for SHDN GPIO %d!\n", SHDN_GPIO);
    }
    g_shdn_fd = gpio_val_fd(SHDN_GPIO);
    if (g_shdn_fd < 0) {
        fprintf(stderr, "[!] CRITICAL: Failed opening value fd for SHDN GPIO %d!\n", SHDN_GPIO);
    }

    set_shdn(0);
    select_channel(-1);

    // 3. Init I2C - I2C 2 for temperature reading
    g_i2c = open(I2C_BUS, O_RDWR);
    if (g_i2c < 0 || ioctl(g_i2c, I2C_SLAVE, ADS1115_ADDR) < 0) {
        perror("I2C ADS1115");
    }

    // 4. Init I2C - I2C 1 for temperature setpoint DAQ - TODO

const char* mode = (argc > 1) ? argv[1] : "closed";

    if (!strcmp(mode, "test")) {
        run_test();
    } else if (!strcmp(mode, "open")) {
        double v_init = (argc > 2) ? atof(argv[2]) : 0.75;
        run_open_loop(v_init);
    } else {
        run_closed_loop();
    }


    // 4. Cleanup
    set_shdn(0);
    select_channel(-1);
    printf("\n[TEC] Stopped (driver OFF, channels OFF).\n");

    for (int i = 0; i < 4; i++) {
        if (g_sel_fd[i] >= 0) close(g_sel_fd[i]);
        gpio_unexport(SEL_GPIO[i]);
    }
    if (g_shdn_fd >= 0) close(g_shdn_fd);
    gpio_unexport(SHDN_GPIO);
    if (g_i2c >= 0) close(g_i2c);

    return 0;
}
