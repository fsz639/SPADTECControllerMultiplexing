//=============================================================================
// tec_controller.cpp  -  Control TEC de los 4 SPAD (BeagleBone Black)
// Quantum Labs
//
//  MODOS (argumento en linea de comandos):
//    (sin arg) / "open"  -> LAZO ABIERTO: corriente fija, multiplexa los 4 TEC
//                           (SHDN siempre ON salvo el instante de conmutacion)
//                           y monitoriza las temperaturas.  <-- USAR AHORA
//    "regulate"          -> LAZO CERRADO (termostato/histeresis 15-18 C).  <-- FUTURO
//    "test"              -> MODO PRUEBA MANUAL de banco (comandos por teclado).
//
//  Hardware: 1x MAX1968 (CTLI fijo ~1,3 V) repartido entre 4 TEC por current
//  switches (4 GPIO) + On/Off (SHDN). NTC leidas por I2C (ADS1115).
//  Todo en el ARM (Linux): GPIO por sysfs + I2C por /dev/i2c-2.
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

// -------- consigna (para el modo regulate, futuro) --------
#define TEMP_LOW    15.0
#define TEMP_HIGH   18.0
// -------- tiempos --------
#define SLOT_US      100000   // 100 ms por canal en lazo abierto
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
#define ADS_LSB        (4.096 / 32768.0)   // PGA +/-4.096 V

// -------- divisor NTC: +3V3 -> 10k -> nodo(V) -> NTC -> GND --------
#define VSUPPLY   3.30
#define R_TOP     10000.0
#define R0_NTC    10000.0
#define T0_KELVIN 298.15
#define BETA      3950.0

static volatile sig_atomic_t g_run = 1;
static void on_sigint(int){ g_run = 0; }

// globals de I/O
static int g_sel_fd[4] = {-1,-1,-1,-1};
static int g_shdn_fd = -1;
static int g_i2c = -1;

// ---------- GPIO sysfs ----------
static void gpio_export(int n){ int fd=open("/sys/class/gpio/export",O_WRONLY);
    if(fd>=0){ char b[8]; int l=snprintf(b,sizeof b,"%d",n); if(write(fd,b,l)){} close(fd);} }
static void gpio_dir_out(int n){ char p[64]; snprintf(p,sizeof p,"/sys/class/gpio/gpio%d/direction",n);
    int fd=open(p,O_WRONLY); if(fd>=0){ if(write(fd,"out",3)){} close(fd);} }
static int  gpio_val_fd(int n){ char p[64]; snprintf(p,sizeof p,"/sys/class/gpio/gpio%d/value",n); return open(p,O_WRONLY); }
static void gpio_write(int fd,int v){ if(fd>=0){ lseek(fd,0,SEEK_SET); char c=v?'1':'0'; if(write(fd,&c,1)){} } }

// ---------- helpers de alto nivel ----------
static void select_channel(int ch){ for(int i=0;i<4;i++) gpio_write(g_sel_fd[i], i==ch?1:0); }
static void set_shdn(int v){ gpio_write(g_shdn_fd, v); }
static void switch_to(int ch){ set_shdn(0); usleep(DEAD_US); select_channel(ch); set_shdn(1); } // conmutacion a I=0

// ---------- ADS1115 ----------
static double ads_read_volts(int ch){
    uint16_t cfg = 0x8000 | ((0x4+ch)<<12) | (0x1<<9) | (0x1<<8) | (0x4<<5) | 0x03;
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
static void run_open_loop(){
    printf("[TEC] LAZO ABIERTO: corriente fija, multiplexado + monitorizacion.\n");
    while(g_run){
        for(int ch=0; ch<4 && g_run; ++ch){
            switch_to(ch);                       // enfria este canal (driver ON)
            double t=read_temp(ch);
            printf("TEC%d activo | NTC%d = %.1f C\n", ch+1, ch+1, t);
            fflush(stdout);
            usleep(SLOT_US);
        }
    }
}

// ================= MODO REGULACION (futuro) =================
static void run_regulate(){
    printf("[TEC] LAZO CERRADO (termostato %.0f-%.0f C).\n",(double)TEMP_LOW,(double)TEMP_HIGH);
    bool cooling[4]={false,false,false,false};
    while(g_run){
        for(int ch=0; ch<4 && g_run; ++ch){
            set_shdn(0); usleep(DEAD_US); select_channel(ch);
            double t=read_temp(ch);
            if(!std::isnan(t)){ if(t>TEMP_HIGH) cooling[ch]=true; else if(t<TEMP_LOW) cooling[ch]=false; }
            set_shdn(cooling[ch]?1:0);
            printf("TEC%d: %.1f C %s\n", ch+1, t, cooling[ch]?"[enfriando]":"[reposo]");
            fflush(stdout);
            usleep(cooling[ch]?300000:20000);
        }
    }
}

// ================= MODO PRUEBA MANUAL (banco) =================
static void run_test(){
    printf("\n=== MODO PRUEBA MANUAL (banco) ===\n"
           " 1..4  : seleccionar canal (cierra ese current switch)\n"
           " o / f : SHDN On / oFf (driver encendido/apagado)\n"
           " r     : leer y mostrar las 4 NTC\n"
           " x     : todo a reposo (SHDN off, ningun canal)\n"
           " q     : salir\n"
           "ATENCION: 'o' da corriente al canal seleccionado. Mide antes con poca corriente.\n\n");
    int cur_ch=-1, shdn=0;
    char line[32];
    while(g_run){
        printf("[canal=%s SHDN=%s] > ", cur_ch<0?"-":(cur_ch==0?"1":cur_ch==1?"2":cur_ch==2?"3":"4"), shdn?"ON":"off");
        fflush(stdout);
        if(!fgets(line,sizeof line,stdin)) break;     // Ctrl+C o EOF
        char c=line[0];
        if(c=='q') break;
        else if(c>='1'&&c<='4'){ cur_ch=c-'1'; set_shdn(0); shdn=0; select_channel(cur_ch);
                                 printf(" -> canal %d seleccionado (SHDN puesto a off por seguridad)\n",cur_ch+1); }
        else if(c=='o'){ if(cur_ch<0) printf(" selecciona antes un canal (1..4)\n");
                         else { set_shdn(1); shdn=1; printf(" -> SHDN ON: corriente por el canal %d\n",cur_ch+1); } }
        else if(c=='f'){ set_shdn(0); shdn=0; printf(" -> SHDN off\n"); }
        else if(c=='r'){ for(int ch=0;ch<4;ch++) printf("   NTC%d = %.2f C\n",ch+1,read_temp(ch)); }
        else if(c=='x'){ set_shdn(0); shdn=0; select_channel(-1); cur_ch=-1; printf(" -> todo a reposo\n"); }
    }
}

int main(int argc, char** argv){
    signal(SIGINT,on_sigint);
    // init GPIO
    for(int i=0;i<4;i++){ gpio_export(SEL_GPIO[i]); gpio_dir_out(SEL_GPIO[i]); g_sel_fd[i]=gpio_val_fd(SEL_GPIO[i]); }
    gpio_export(SHDN_GPIO); gpio_dir_out(SHDN_GPIO); g_shdn_fd=gpio_val_fd(SHDN_GPIO);
    set_shdn(0); select_channel(-1);
    // init I2C
    g_i2c=open(I2C_BUS,O_RDWR);
    if(g_i2c<0 || ioctl(g_i2c,I2C_SLAVE,ADS1115_ADDR)<0){ perror("I2C ADS1115 (solo afecta a la lectura de NTC)"); }

    const char* mode = (argc>1)? argv[1] : "open";
    if(!strcmp(mode,"test"))          run_test();
    else if(!strcmp(mode,"regulate")) run_regulate();
    else                              run_open_loop();

    // salida segura
    set_shdn(0); select_channel(-1);
    printf("\n[TEC] Parado (driver OFF, canales en reposo).\n");
    for(int i=0;i<4;i++) if(g_sel_fd[i]>=0) close(g_sel_fd[i]);
    if(g_shdn_fd>=0) close(g_shdn_fd);
    if(g_i2c>=0) close(g_i2c);
    return 0;
}
