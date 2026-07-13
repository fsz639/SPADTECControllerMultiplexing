// --- SCRIPT MULTICANAL PRU0 ---
// Cuenta flancos en 4 pines PRU → Host

.origin 0
.entrypoint INIT

#include "PRUassLecture.hp"

#define PIN0   0      // P9_31 → r31 bit 0
#define PIN1   1      // P9_29 → r31 bit 1
#define PIN2   2      // P9_30 → r31 bit 2
#define PIN3   3      // P9_28 → r31 bit 3

// r31 = input register
// r30 = output register

INIT:
    // Enable OCP
    LBCO    r0, CONST_PRUCFG, 4, 4
    CLR     r0, r0, 4
    SBCO    r0, CONST_PRUCFG, 4, 4

    // Shared RAM base
    MOV     r14, 0x10000     // offset 0x10000 → Shared RAM

MAIN_LOOP:
    // Esperar evento del Host (bit 30)
    QBBC    MAIN_LOOP, r31, 30

    // Limpiar interrupción Host
    MOV     r0, 0
    SBCO    r0, C0, 0x24, 1

    // Inicializar contadores
    MOV     r8,  0      // contador canal 0
    MOV     r9,  0      // contador canal 1
    MOV     r10, 0      // contador canal 2
    MOV     r11, 0      // contador canal 3

    // Estados anteriores
    MOV     r4, 0
    MOV     r5, 0
    MOV     r6, 0
    MOV     r7, 0

    // Ventana de medición (20M ciclos)
    MOV     r3, 20000000

COUNT_LOOP:
    // Leer r31 una sola vez (OPTIMIZACIÓN)
    MOV     r12, r31

    //-------------------------------------------------------------------
    // CANAL 0
    QBBC    C0_LOW,  r12, PIN0
C0_HIGH:
    QBEQ    C0_NOEDGE, r4, 1
    ADD     r8, r8, 1
    MOV     r4, 1
    JMP     C1_CHECK
C0_LOW:
    MOV     r4, 0
C0_NOEDGE:

    //-------------------------------------------------------------------
    // CANAL 1
C1_CHECK:
    QBBC    C1_LOW,  r12, PIN1
C1_HIGH:
    QBEQ    C1_NOEDGE, r5, 1
    ADD     r9, r9, 1
    MOV     r5, 1
    JMP     C2_CHECK
C1_LOW:
    MOV     r5, 0
C1_NOEDGE:

    //-------------------------------------------------------------------
    // CANAL 2
C2_CHECK:
    QBBC    C2_LOW,  r12, PIN2
C2_HIGH:
    QBEQ    C2_NOEDGE, r6, 1
    ADD     r10, r10, 1
    MOV     r6, 1
    JMP     C3_CHECK
C2_LOW:
    MOV     r6, 0
C2_NOEDGE:

    //-------------------------------------------------------------------
    // CANAL 3
C3_CHECK:
    QBBC    C3_LOW,  r12, PIN3
C3_HIGH:
    QBEQ    C3_NOEDGE, r7, 1
    ADD     r11, r11, 1
    MOV     r7, 1
    JMP     LOOP_END
C3_LOW:
    MOV     r7, 0
C3_NOEDGE:

LOOP_END:
    // Decrementar timer
    SUB     r3, r3, 1
    QBNE    COUNT_LOOP, r3, 0

    //-------------------------------------------------------------------
    // Guardar los 4 contadores consecutivos en Shared RAM
    SBBO    r8,  r14, 0, 4     // canal 0
    SBBO    r9,  r14, 4, 4     // canal 1
    SBBO    r10, r14, 8, 4     // canal 2
    SBBO    r11, r14, 12,4     // canal 3

    // Interrupción al Host
    MOV     r31.b0, PRU0_ARM_INTERRUPT + 16

    JMP     MAIN_LOOP
EXIT:
	HALT
ERR:	// Signal error
	//LED_ON
//	JMP INITIATIONS
//	JMP ERR
	HALT
