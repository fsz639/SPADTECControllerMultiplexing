// This signals scripts outputs iteratively channel 1, then channel 2, channel 3, channel 4 and then a time off (also to allow for some management); copied for two output signals . Outputing at maxium 100 MHz.
// PRU-ICSS program to control realtime GPIO pins
// But only if the Pinmux Mode has been set correctly with a device  
 // tree overlay!  
 //  
 // Assemble in BBB with:  
 // pasm -b PRUassTrigSigScriptHist4Sig.p
 // https://www.ofitselfso.com/BBBCSIO/Help/BBBCSIOHelp_PRUPinInOutExamplePASMCode.html
 
.origin 0				// start of program in PRU memory
.entrypoint INITIATIONS			// program entry point (for debbuger)

#define GPIO0_BANK 0x44E07000
#define GPIO1_BANK 0x4804c000 // this is the address of the BB GPIO1 Bank Register for PR0
#define GPIO2_BANK 0x481ac000 // this is the address of the BBB GPIO2 Bank Register for PRU1. We set bits in special locations in offsets here to put a GPIO high or low.
#define GPIO3_BANK 0x481AE000

#define GPIO_SETDATAOUToffset 0x194 // at this offset various GPIOs are associated with a bit position. Writing a 32 bit value to this offset enables them (sets them high) if there is a 1 in a corresponding bit. A zero in a bit position here is ignored - it does NOT turn the associated GPIO off.

#define GPIO_CLEARDATAOUToffset 0x190 //We set a GPIO low by writing to this offset. In the 32 bit value we write, if a bit is 1 the 
// GPIO goes low. If a bit is 0 it is ignored.


// Refer to this mapping in the file - pruss_intc_mapping.h
#define PRU0_PRU1_INTERRUPT     17
#define PRU1_PRU0_INTERRUPT     18
#define PRU0_ARM_INTERRUPT      19
#define PRU1_ARM_INTERRUPT      20
#define ARM_PRU0_INTERRUPT      21
#define ARM_PRU1_INTERRUPT      22


// The constant table registers are common for both PRU (so they share the same values)
#define CONST_PRUCFG         C4
#define CONST_PRUDRAM        C24 // allow the PRU to map portions of the system's memory into its own address space. In particular we will map its own data RAM
#define CONST_IETREG	     C26 //

#define OWN_RAM              0x00000000 // current PRU data RAM
#define OWN_RAMoffset	     0x00000200 // Offset from Base OWN_RAM to avoid collision with some data tht PRU might store
#define PRU1_CTRL            0x240

// Beaglebone Black has 32 bit registers (for instance Beaglebone AI has 64 bits and more than 2 PRU)

// *** LED routines, so that LED USR0 can be used for some simple debugging
// *** Affects: r28, r29. Each PRU has its of 32 registers
.macro LED_OFF
	MOV		r28, 1<<21
	MOV		r29, GPIO2_BANK | GPIO_CLEARDATAOUToffset
	SBBO	r28, r29, 0, 4
.endm

.macro LED_ON
	MOV		r28, 1<<21
	MOV		r29, GPIO2_BANK | GPIO_SETDATAOUToffset
	SBBO	r28, r29, 0, 4
.endm
// r0 is arbitrary used for operations
// r4 reserved for zeroing registers
// r10 is arbitrary used for operations
// r14 is reserved for ON state time
// r15 is reserved for OFF state time
// r28 is mainly used for LED indicators operations
// r29 is mainly used for LED indicators operations
// r30 is reserved for output pins
INITIATIONS:
	SET     r30.t11	// enable the data bus for initiating the OCP master. it may be necessary to disable the bus to one peripheral while another is in use to prevent conflicts or manage bandwidth.
	LBCO	r0, CONST_PRUCFG, 4, 4 // Enable OCP master port
	// OCP master port is the protocol to enable communication between the PRUs and the host processor
	CLR		r0, r0, 4         // Clear SYSCFG[STANDBY_INIT] to enable OCP master port
	SBCO	r0, CONST_PRUCFG, 4, 4

	// Configure the programmable pointer register for PRU by setting c24_pointer // related to pru data RAM. Where the commands will be found
	// This will make C24 point to 0x00000000 (PRU data RAM).
	MOV	r0, OWN_RAM | OWN_RAMoffset// | OWN_RAMoffset
	MOV	r10, 0x24000+0x20// | C24add//CONST_PRUDRAM
	SBBO	r0, r10, 0, 4//SBCO	r0, CONST_PRUDRAM, 0, 4  // Load the base address of PRU0 Data RAM into C
	
	MOV	r30, 0
	MOV	r14, 2 // ON state
	MOV	r15, 2 // OFF state
	MOV	r11, 0x02220111
	MOV	r12, 0x08880444
	LDI	r4, 0 // zeroing
	
//	LED_ON	// just for signaling initiations
//	LED_OFF	// just for signaling initiations
CMDLOOP:
	QBBC	CMDLOOP, r31, 31//COMPARAR INTERRUPT HOST 22 Y PRU 31
	SBCO	r4.b0, C0, 0x24, 1 // Reset host interrupt
CMDLOOP2:// Double verification of host sending start command
	LBCO	r0.b0, CONST_PRUDRAM, 0, 1 // Load to r0 the content of CONST_PRUDRAM with offset 4, and 1 bytes
	QBEQ	CMDLOOP, r0.b0, 0 // loop until we get an instruction
	SBCO	r4.b0, CONST_PRUDRAM, 0, 1
	//LED_ON
// Define native PASM macros
.macro PIN_DELAY
    LDI        r4, 0
.endm

.macro PIN_DELAYOFF
.endm

.macro JMP_DELAY
    LDI        r4, 0
.endm

.macro JMP_DELAYOFF
.endm

FASTLOOP:
    MOV        r30, 0x00000001 
    PIN_DELAY
    
    MOV        r30, 0x00000000
    LDI        r4, 0

    MOV        r30, 0x00000002 
    PIN_DELAY
    
    MOV        r30, 0x00000000
    LDI        r4, 0

    MOV        r30, 0x00000004 
    PIN_DELAY
    
    MOV        r30, 0x00000000
    LDI        r4, 0

    MOV        r30, 0x00000008
    PIN_DELAY

    MOV        r30, 0x00000000

JMPLOOP:
    JMP        FASTLOOP
EXIT:
	SET     r30.t11	// enable the data bus. it may be necessary to disable the bus to one peripheral while another is in use to prevent conflicts or manage bandwidth.
	HALT
ERR:	// Signal error
	SET     r30.t11	// enable the data bus. it may be necessary to disable the bus to one peripheral while another is in use to prevent conflicts or manage bandwidth.
	//LED_ON
	JMP INITIATIONS
	JMP ERR
	HALT

//SIGNALON1:	// The odd signals actually carry the signal (so it is half of the period, adjusting the on time); while the even signals are the half period alway off
//	MOV		r30, 0x00000001 // Double channels 1. write to magic r30 output byte 0. Half word bytes= 7,6,5,4,3,2,1,0 bits
//	MOV		r5, r14
//SIGNALON1DEL:
//	SUB		r5, r5, 1
//	QBNE	SIGNALON1DEL, r5, 0
////	LDI		r4, 0 // Controlled intentional delay to account for the fact that QBNE takes one extra count when it does not go through the barrier
//SIGNALOFF1: // Make use of this dead time to instantly correct for intra relative frequency sifference
//	MOV		r30, 0x00000000 // All off
//	MOV		r5, r15
//	LDI		r4, 0 // Intentionally controlled delay to adjust all sequences (in particular to the last one)
//SIGNALOFF1DEL:
//	SUB		r5, r5, 1
//	QBNE	SIGNALOFF1DEL, r5, 0
//SIGNALON2:	// The odd signals actually carry the signal (so it is half of the period, adjusting the on time); while the even signals are the half period alway off
//	MOV		r30, 0x00000002 // Double channels 2. write to magic r30 output byte 0. Half word bytes= 7,6,5,4,3,2,1,0 bits
//	MOV		r5, r14
//SIGNALON2DEL:
//	SUB		r5, r5, 1
//	QBNE	SIGNALON2DEL, r5, 0
////	LDI		r4, 0 // Controlled intentional delay to account for the fact that QBNE takes one extra count when it does not go through the barrier
//SIGNALOFF2: // Make use of this dead time to instantly correct for intra relative frequency sifference
//	MOV		r30, 0x00000000 // All off
//	MOV		r5, r15
//	LDI		r4, 0 // Intentionally controlled delay to adjust all sequences (in particular to the last one)
//SIGNALOFF2DEL:
//	SUB		r5, r5, 1
//	QBNE	SIGNALOFF2DEL, r5, 0
//SIGNALON3:	// The odd signals actually carry the signal (so it is half of the period, adjusting the on time); while the even signals are the half period alway off
//	MOV		r30, 0x00000004 // Double channels 2. write to magic r30 output byte 0. Half word bytes= 7,6,5,4,3,2,1,0 bits
//	MOV		r5, r14
//SIGNALON3DEL:
//	SUB		r5, r5, 1
//	QBNE	SIGNALON3DEL, r5, 0
////	LDI		r4, 0 // Controlled intentional delay to account for the fact that QBNE takes one extra count when it does not go through the barrier
//SIGNALOFF3: // Make use of this dead time to instantly correct for intra relative frequency sifference
//	MOV		r30, 0x00000000 // All off
//	MOV		r5, r15
//	LDI		r4, 0 // Intentionally controlled delay to adjust all sequences (in particular to the last one)
//SIGNALOFF3DEL:
//	SUB		r5, r5, 1
//	QBNE	SIGNALOFF3DEL, r5, 0
//SIGNALON4:	// The odd signals actually carry the signal (so it is half of the period, adjusting the on time); while the even signals are the half period alway off
//	MOV		r30, 0x00000008 // Double channels 2. write to magic r30 output byte 0. Half word bytes= 7,6,5,4,3,2,1,0 bits
//	MOV		r5, r14
//SIGNALON4DEL:
//	SUB		r5, r5, 1
//	QBNE	SIGNALON4DEL, r5, 0
////	LDI		r4, 0 // Controlled intentional delay to account for the fact that QBNE takes one extra count when it does not go through the barrier
//SIGNALOFF4: // Make use of this dead time to instantly correct for intra relative frequency sifference
//	MOV		r30, 0x00000000 // All off
//	MOV		r5, r15
//	LDI		r4, 0 // Intentionally controlled delay to adjust all sequences (in particular to the last one)
//SIGNALOFF4DEL:
//	SUB		r5, r5, 1
//	QBNE	SIGNALOFF4DEL, r5, 0
//JMPLOOP:
//	JMP	SIGNALON1//	LDI		r4, 0 // Controlled intentional delay to account for the fact that QBNE takes one extra count when it does not go through the barrier
//EXIT:
//	SET     r30.t11	// enable the data bus. it may be necessary to disable the bus to one peripheral while another is in use to prevent conflicts or manage bandwidth.
//	HALT
//ERR:	// Signal error
//	SET     r30.t11	// enable the data bus. it may be necessary to disable the bus to one peripheral while another is in use to prevent conflicts or manage bandwidth.
//	//LED_ON
////	JMP INITIATIONS
////	JMP ERR
//	HALT
