//=============================================================================
// tec_controller.cpp  -  TEC Control 4 SPAD (BeagleBone Black)
// QuNet LAb
//
//  MODES (arguments passed thorugh the command line):
//    (sin arg) / "open"  -> multiplexes 4 TEC (SHDN ON except commuting instance)
//                           and monitors temperatures.  <-- TO USE
//    "test"              -> MANUAL TEST MODE (keyboard inputed commands).
//
//  Hardware: 1x MAX1968 (CTLI fijo ~1,3 V) repartido entre 4 TEC por current
//  switches (4 GPIO) + On/Off (SHDN). NTC leidas por I2C (ADS1115).
//  Todo en el ARM (Linux): GPIO por sysfs + I2C por /dev/i2c-2.
//
//  NTC alimentadas desde REF (1,50 V) del MAX1968 (no desde 3,3 V).
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

// -------- consigna (para el modo regulate, futuro) --------
//  Setpoint fijo 18,0 C con histeresis +/-0,1 C (bang-bang):
//    refria si T > 18,1 ; para si T < 17,9
#define TEMP_SET    18.0
#define TEMP_HYST    0.1
#define TEMP_HIGH   (TEMP_SET + TEMP_HYST)   // 18.1 -> enciende (enfriar)
#define TEMP_LOW    (TEMP_SET - TEMP_HYST)   // 17.9 -> apaga
// -------- tiempos --------
#define SLOT_US      100000   // 100 ms por canal en lazo abierto (ciclo 400 ms).
                              // El paper CAPSat usa 10 ms/canal (40 ms de ciclo);
                              // ambos son irrelevantes frente a la constante
                              // termica (segundos). 100 ms es comodo para banco.
#define DEAD_US        2000   // 2 ms de tiempo muerto (SHDN=0) al conmutar

// -------- GPIO (numeros sysfs BBB) --------
//  SEL1..4 = current switches;  SHDN = On/Off del MAX1968
//  P8_12=44  P8_11=45  P8_16=46  P8_15=47 (SEL1..4)    P8_14=26 (SHDN)
static const int SEL_GPIO[4] = { 44, 45, 46, 47 };
static const int SHDN_GPIO   = 26;

// -------- ADS1115 --------
#define I2C_BUS        "/dev/i2c-2"
#define ADS1115_ADDR   0x48
#define ADS_REG_CONV   0x00
#define ADS_REG_CONFIG 0x01
#define ADS_LSB        (2.048 / 32768.0)   // PGA +/-2.048 V  (NTC desde REF 1,5V)

// -------- divisor NTC: REF(1,50 V) -> 10k -> nodo(V) -> NTC -> GND --------
#define VSUPPLY   1.50          // <-- NTC alimentadas desde REF, no desde 3,3 V
#define R_TOP     10000.0
#define R0_NTC    10000.0
#define T0_KELVIN 298.15
#define BETA      3950.0

static volatile sig_atomic_t g_run = 1;

// globals de I/O
static int g_sel_fd[4] = {-1,-1,-1,-1};
static int g_shdn_fd = -1;
static int g_i2c = -1;

// ---------- GPIO sysfs ----------
static void gpio_export(int n){ int fd=open("/sys/class/gpio/export",O_WRONLY);
    if(fd>=0){ char b[8]; int l=snprintf(b,sizeof b,"%d",n); if(write(fd,b,l)){} close(fd);} }
static void gpio_unexport(int n){ int fd=open("/sys/class/gpio/unexport",O_WRONLY);
    if(fd>=0){ char b[8]; int l=snprintf(b,sizeof b,"%d",n); if(write(fd,b,l)){} close(fd);} }
static void gpio_dir_out(int n){ char p[64]; snprintf(p,sizeof p,"/sys/class/gpio/gpio%d/direction",n);
    int fd=open(p,O_WRONLY); if(fd>=0){ if(write(fd,"out",3)){} close(fd);} }
static int  gpio_val_fd(int n){ char p[64]; snprintf(p,sizeof p,"/sys/class/gpio/gpio%d/value",n); return open(p,O_WRONLY); }
static void gpio_write(int fd,int v){ if(fd>=0){ lseek(fd,0,SEEK_SET); char c=v?'1':'0'; if(write(fd,&c,1)){} } }

// ---------- helpers de alto nivel ----------
static void select_channel(int ch){ for(int i=0;i<4;i++) gpio_write(g_sel_fd[i], i==ch?1:0); }
static void set_shdn(int v){ gpio_write(g_shdn_fd, v ? 0 : 1); }
static void switch_to(int ch){ set_shdn(0); usleep(DEAD_US); select_channel(ch); set_shdn(1); } // conmutacion a I=0

// ---------- ADS1115 ----------
static double ads_read_volts(int ch){
    // OS=1 | MUX=single AINch | PGA=2 (+/-2.048V) | MODE=1 single | DR=4 (128SPS) | COMP off
    uint16_t cfg = 0x8000 | ((0x4+ch)<<12) | (0x2<<9) | (0x1<<8) | (0x4<<5) | 0x03;
    uint8_t w[3]={ADS_REG_CONFIG,(uint8_t)(cfg>>8),(uint8_t)(cfg&0xFF)};
    if(write(g_i2c,w,3)!=3) return NAN;
    usleep(9000);
    uint8_t reg=ADS_REG_CONV; if(write(g_i2c,&reg,1)!=1) return NAN;
    uint8_t r[2]; if(read(g_i2c,r,2)!=2) return NAN;
    return (int16_t)((r[0]<<8)|r[1]) * ADS_LSB;
}
static double read_temp(int ch){
    double v=ads_read_volts(ch);
    if(v<=0.0 || v>=VSUPPLY) return NAN;
    double R=R_TOP*v/(VSUPPLY-v);
    return 1.0/(1.0/T0_KELVIN+(1.0/BETA)*log(R/R0_NTC))-273.15;
}

// ================= MODO LAZO ABIERTO (usar ahora) =================
bool enable_print = true; // Set to false to turn off printing

static void run_open_loop(void) {
    double temps[4] = {0};
    time_t last_print = 0;

    while (g_run) {
        // 1. Read all 4 channels
        for (int ch = 0; ch < 4 && g_run; ++ch) {
            switch_to(ch);
            temps[ch] = read_temp(ch);
            usleep(SLOT_US);
        }

        // 2. Print every 5 seconds if flag is true
        time_t now = time(NULL);
        if (enable_print && (now - last_print >= 3)) {
            printf("\033[H\033[J"); // Clears the screen
            
            for (int i = 0; i < 4; ++i) {
                printf("TEC%d active | NTC%d = %.1f C\n", i + 1, i + 1, temps[i]);
            }
            printf("Ctrl+C to stop the multiplexed TEC controller");
            fflush(stdout);

            last_print = now;
        }
    }
}
// ================= MODO PRUEBA MANUAL (banco) =================
static void run_test(){
    printf("\n=== MANUAL TEST MODE ===\n"
           " 1..4  : select channel (cierra ese current switch)\n"
           " o / f : SHDN On / Off (driver on/off)\n"
           " r     : read and display 4 NTC\n"
           " x     : all OFF (SHDN off, no channel)\n"
           " q     : exit\n"
           "ATENTION: 'o' provides current to the selected channel. Read in advance with little current.\n\n");
    int cur_ch=-1, shdn=0;
    char line[32];
    while(g_run){
        printf("[channel=%s SHDN=%s] > ", cur_ch<0?"-":(cur_ch==0?"1":cur_ch==1?"2":cur_ch==2?"3":"4"), shdn?"ON":"off");
        fflush(stdout);
        if(!fgets(line,sizeof line,stdin)) break;     // Ctrl+C o EOF
        char c=line[0];
        if(c=='q') break;
        else if(c>='1'&&c<='4'){ cur_ch=c-'1'; set_shdn(0); shdn=0; select_channel(cur_ch);
                                 printf(" -> channel %d selected (SHDN off for security)\n",cur_ch+1); }
        else if(c=='o'){ if(cur_ch<0) printf(" first select a channel (1..4)\n");
                         else { set_shdn(1); shdn=1; printf(" -> SHDN ON: current to channel %d\n",cur_ch+1); } }
        else if(c=='f'){ set_shdn(0); shdn=0; printf(" -> SHDN off\n"); }
        else if(c=='r'){ for(int ch=0;ch<4;ch++) printf("   NTC%d = %.2f C\n",ch+1,read_temp(ch)); }
        else if(c=='x'){ set_shdn(0); shdn=0; select_channel(-1); cur_ch=-1; printf(" -> all off\n"); }
    }
}

/// Errors handling
std::atomic<bool> signalReceivedFlag{false};
static void SignalINTHandler(int s) {
signalReceivedFlag.store(true);
cout << "Caught SIGINT" << endl;
}

static void SignalTERMHandler(int s) {
signalReceivedFlag.store(true);
cout << "Caught SIGTERM" << endl;
}

static void SignalPIPEHandler(int s) {
signalReceivedFlag.store(true);
cout << "Caught SIGPIPE" << endl;
}

int main(int argc, char** argv){
    /// Errors/actions handling
     signal(SIGINT, SignalINTHandler);// Interruption signal
     signal(SIGTERM, SignalTERMHandler); // kill, systemd stop

    // init GPIO
    for(int i=0;i<4;i++){ gpio_export(SEL_GPIO[i]); gpio_dir_out(SEL_GPIO[i]); g_sel_fd[i]=gpio_val_fd(SEL_GPIO[i]); }
    gpio_export(SHDN_GPIO); gpio_dir_out(SHDN_GPIO); g_shdn_fd=gpio_val_fd(SHDN_GPIO);
    set_shdn(0); select_channel(-1);
    // init I2C
    g_i2c=open(I2C_BUS,O_RDWR);
    if(g_i2c<0 || ioctl(g_i2c,I2C_SLAVE,ADS1115_ADDR)<0){ perror("I2C ADS1115 (only affects NTC reading)"); }

    const char* mode = (argc>1)? argv[1] : "open";
    if(!strcmp(mode,"test"))          run_test();
    else                              run_open_loop();

    // clean exit
    set_shdn(0); select_channel(-1);
    printf("\n[TEC] Stopped (driver OFF, channels OFF).\n");
    for(int i=0;i<4;i++) if(g_sel_fd[i]>=0) close(g_sel_fd[i]);
    if(g_shdn_fd>=0) close(g_shdn_fd);
    if(g_i2c>=0) close(g_i2c);
    // desexportar los GPIO para no dejarlos colgados (lección del P8_12 "muerto")
    for(int i=0;i<4;i++) gpio_unexport(SEL_GPIO[i]);
    gpio_unexport(SHDN_GPIO);

    
    return 0;
}
