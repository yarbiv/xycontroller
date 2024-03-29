#include <stdio.h>
#include <main.h>
#include <msp430.h>
#include <driverlib/driverlib.h>
#include <hal_LCD.h>
#include <string.h>

#include <motor.h> /* our code */
#include <graph.h>


/* 
    MACROS
*/

/* Logic high and low */
#define LO             0x00
#define HI             0x01

/* # of calls to rotateMotor needed for one full rotation (4096 steps) */
#define FULL_ROTATION 512

/* Macros to control the 3 SELECT signals of the mux */
#define MUX_S0(x) (x==HI) ? (P2OUT |= BIT5) : (P2OUT &= ~BIT5)
#define MUX_S1(x) (x==HI) ? (P8OUT |= BIT2) : (P8OUT &= ~BIT2)
#define MUX_S2(x) (x==HI) ? (P8OUT |= BIT3) : (P8OUT &= ~BIT3)


/* 
    GLOBAL motor and positional state
*/
int x_degrees = 0, y_degrees = 0;
int xpos = 0, ypos = 0;
unsigned int current_target = 0;
int x_targets[6] = {0, 1, 2, 3, 4, 5};
int y_targets[6] = {0, 1, 2, 3, 4, 5};
int x_steps = 0, y_steps = 0;

typedef enum {config, idle, running, blocked} appState;
appState state = config, prev_state = config;


/* LUT for the keypad digits (row, column)*/
static int keypadLUT[4][3] = { { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 }, {0, 0, 0} };

/* Read and return a single value from the ADC */
int16_t readADC() {
    ADC_startConversion(ADC_BASE, ADC_SINGLECHANNEL);           // Start an ADC read
    while (ADC_isBusy(ADC_BASE));                               // Wait for ADC to finish
    ADC_disableConversions(ADC_BASE, ADC_COMPLETECONVERSION);
    return ADC_getResults(ADC_BASE);
}

int checkGPIO(void) {
    int reading;
    // 8.3 8.2 2.5
    //   1   0   0    ROW1
    //   1   1   0    ROW2
    //   1   1   1    ROW3
    //   1   0   1    ROW4

    // Always high
    GPIO_setOutputHighOnPin(GPIO_PORT_P8, GPIO_PIN3);

    // Row 1
    GPIO_setOutputLowOnPin(GPIO_PORT_P8, GPIO_PIN2);
    GPIO_setOutputLowOnPin(GPIO_PORT_P2, GPIO_PIN5);
    // Read pin 8.0, if high, return 1
    reading = readADC();
    if (reading > 100) return 1;

    // Row 2
    GPIO_setOutputHighOnPin(GPIO_PORT_P8, GPIO_PIN2);
    GPIO_setOutputLowOnPin(GPIO_PORT_P2, GPIO_PIN5);
    // Read pin 8.0, if high, return 2
    reading = readADC();
    if (reading > 100) return 2;

    // Row 3
    GPIO_setOutputHighOnPin(GPIO_PORT_P8, GPIO_PIN2);
    GPIO_setOutputHighOnPin(GPIO_PORT_P2, GPIO_PIN5);
    // Read pin 8.0, if high, return 3
    reading = readADC();
    if (reading > 100) return 3;

    // Row 4
    GPIO_setOutputLowOnPin(GPIO_PORT_P8, GPIO_PIN2);
    GPIO_setOutputHighOnPin(GPIO_PORT_P2, GPIO_PIN5);
    reading = readADC();
    if (reading > 100) return 4;
    // Read pin 8.0, if high, return 4

    return 0;
}


int readLightSensor() {
    MUX_S0(LO);                                           // Configure select bits
    MUX_S1(LO);
    MUX_S2(LO);

    int16_t reading;
    char coords[10] = {0};

    while (1) {
        reading = readADC();
        memset(coords, 0, 10);
        sprintf(coords, "%d", reading);
        displayScrollText(coords);
    }
}


int getKeypress(void) {

    while (1) {
        int row;
        char buf[50];

        // Drive column 1
        GPIO_setOutputHighOnPin(GPIO_PORT_P8, GPIO_PIN1);
        GPIO_setOutputLowOnPin(GPIO_PORT_P1, GPIO_PIN1);
        GPIO_setOutputLowOnPin(GPIO_PORT_P1, GPIO_PIN0);

        row = checkGPIO();
        if (row != 0) {
            return(keypadLUT[row-1][0]);
        }

        // Drive column 2
        GPIO_setOutputLowOnPin(GPIO_PORT_P8, GPIO_PIN1);
        GPIO_setOutputHighOnPin(GPIO_PORT_P1, GPIO_PIN1);
        GPIO_setOutputLowOnPin(GPIO_PORT_P1, GPIO_PIN0);

        row = checkGPIO();
        if (row != 0) {
            return(keypadLUT[row-1][1]);
        }

        // Drive column 3
        GPIO_setOutputLowOnPin(GPIO_PORT_P8, GPIO_PIN1);
        GPIO_setOutputLowOnPin(GPIO_PORT_P1, GPIO_PIN1);
        GPIO_setOutputHighOnPin(GPIO_PORT_P1, GPIO_PIN0);

        row = checkGPIO();
        if (row != 0) {
            return(keypadLUT[row-1][2]);
        }
    }
}

char ADCState = 0; //Busy state of the ADC
int16_t ADCResult = 0; //Storage for the ADC conversion result

int obstructed() {
    int16_t reading;

    // Try sensor RIGHT
    MUX_S0(HI);
    MUX_S1(LO);
    MUX_S2(LO);

    if ((reading = readADC()) < 200) {
        return 1;
    }

    // Try sensor LEFT
    MUX_S0(LO);
    MUX_S1(HI);
    MUX_S2(LO);

    if ((reading = readADC()) < 200) {
        return 2;
    }

    // Try sensor UP
    MUX_S0(LO);
    MUX_S1(LO);
    MUX_S2(LO);

    if ((reading = readADC()) < 200) {
        return 3;
    }

    // Try sensor DOWN
    MUX_S0(HI);
    MUX_S1(HI);
    MUX_S2(LO);

    if ((reading = readADC()) < 200) {
        return 4;
    }

    return 0;
}


void main(void)
{
    char buttonState = 0; // Current button press state (to allow edge detection)

    /*
     * Functions with two underscores in front are called compiler intrinsics.
     * They are documented in the compiler user guide, not the IDE or MCU guides.
     * They are a shortcut to insert some assembly code that is not really
     * expressible in plain C/C++. Google "MSP430 Optimizing C/C++ Compiler
     * v18.12.0.LTS" and search for the word "intrinsic" if you want to know
     * more.
     * */

    //Turn off interrupts during initialization
    __disable_interrupt();

    //Stop watchdog timer unless you plan on using it
    WDT_A_hold(WDT_A_BASE);

    // Initializations - see functions for more detail
    Init_GPIO();    //Sets all pins to output low as a default
    Init_ADC();     //Sets up the ADC to sample
    Init_Clock();   //Sets up the necessary system clocks
    Init_LCD();     //Sets up the LaunchPad LCD display

     /*
     * The MSP430 MCUs have a variety of low power modes. They can be almost
     * completely off and turn back on only when an interrupt occurs. You can
     * look up the power modes in the Family User Guide under the Power Management
     * Module (PMM) section. You can see the available API calls in the DriverLib
     * user guide, or see "pmm.h" in the driverlib directory. Unless you
     * purposefully want to play with the power modes, just leave this command in.
     */
    PMM_unlockLPM5(); //Disable the GPIO power-on default high-impedance mode to activate previously configured port settings

    //All done initializations - turn interrupts back on.
    __enable_interrupt();

    /* set the home coordinates */
    current_target = 1;

    int app_running = 1;

    int blocked_sensor = 0;

    while (app_running) {
        switch (state) {

        case blocked:
            while ((blocked_sensor = obstructed()) != 0) {
                switch (blocked_sensor) {
                case 1:
                    displayScrollText("RIGHT BLOCKED");
                    break;
                case 2:
                    displayScrollText("LEFT BLOCKED");
                    break;
                case 3:
                    displayScrollText("UP BLOCKED");
                    break;
                case 4:
                    displayScrollText("DOWN BLOCKED");
                    break;
                default:
                    displayScrollText("BLOCKED");
                    break;
                }
            }
            state = prev_state;                             // Reset state to running

            break;

        case idle:
            while (P1IN & BIT2) {
                displayScrollText("IDLE");
                __delay_cycles(1000);
            }
            prev_state = state;
            state = running;

            break;

        case running:
            while (1) {
                if (obstructed()) {                         // Stop moving if we are obstructed
                    prev_state = state;
                    state = blocked;                        // Set state to blocked
                    break;
                }

                /* Rotate motor X if target isn't reached */
                if (xpos < x_targets[current_target]) {
                    rotateMotorXForward();
                    x_steps++;

                    if (x_steps == FULL_ROTATION) {
                        ++xpos;
                        x_steps = 0;
                    }
                } else if (xpos > x_targets[current_target]) {
                    rotateMotorXBackward();
                    x_steps++;

                    if (x_steps == FULL_ROTATION) {
                        --xpos;
                        x_steps = 0;
                    }
                }


                if (ypos < y_targets[current_target]) {
                    rotateMotorYForward();
                    y_steps++;

                    if (y_steps == FULL_ROTATION) {
                        ++ypos;
                        y_steps = 0;
                    }
                } else if (ypos > y_targets[current_target]) {
                    rotateMotorYBackward();
                    y_steps++;

                    if (y_steps == FULL_ROTATION) {
                        --ypos;
                        y_steps = 0;
                    }
                }

                graph((float) xpos / 18.0, (float) ypos / 18.0);

                if (xpos == x_targets[current_target] && ypos == y_targets[current_target]) {
                    if (current_target == 0) {
                        state = config;
                        break;
                    }

                    __delay_cycles(100000);
                    current_target = (current_target + 1) % 6;
                }
            }


            break;

        case config:
            __delay_cycles(1);
            unsigned int pair = 0;
            int target_x = 0, target_y = 0;
            displayScrollText("ENTER COORDINATES");
            char coords[10] = {0};
            for (pair = 1; pair < 6; ++pair) {  // Enter 5 coordinate pairs, start at 1 because target[0] = 0 (origin)
                target_x = getKeypress();
                memset(coords, 0, 10);
                sprintf(coords, "XPOS %d", target_x);
                displayScrollText(coords);

                memset(coords, 0, 10);
                target_y = getKeypress();
                sprintf(coords, "YPOS %d", target_y);
                displayScrollText(coords);

                x_targets[pair] = target_x;
                y_targets[pair] = target_y;

                memset(coords, 0, 10);
                sprintf(coords, "%d %d", target_x, target_y);
                displayScrollText(coords);

            }

            state = running;  // Transition to running
            break;

        default:
            break;
        }
    }

    /*
     * You can use the following code if you plan on only using interrupts
     * to handle all your system events since you don't need any infinite loop of code.
     *
     * //Enter LPM0 - interrupts only
     * __bis_SR_register(LPM0_bits);
     * //For debugger to let it know that you meant for there to be no more code
     * __no_operation();
    */

}

void Init_GPIO(void)
{
    // Set all GPIO pins to output low to prevent floating input and reduce power consumption
    GPIO_setOutputLowOnPin(GPIO_PORT_P1, GPIO_PIN0|GPIO_PIN1|GPIO_PIN2|GPIO_PIN3|GPIO_PIN4|GPIO_PIN5|GPIO_PIN6|GPIO_PIN7);
    GPIO_setOutputLowOnPin(GPIO_PORT_P2, GPIO_PIN0|GPIO_PIN1|GPIO_PIN2|GPIO_PIN3|GPIO_PIN4|GPIO_PIN5|GPIO_PIN6|GPIO_PIN7);
    GPIO_setOutputLowOnPin(GPIO_PORT_P3, GPIO_PIN0|GPIO_PIN1|GPIO_PIN2|GPIO_PIN3|GPIO_PIN4|GPIO_PIN5|GPIO_PIN6|GPIO_PIN7);
    GPIO_setOutputLowOnPin(GPIO_PORT_P4, GPIO_PIN0|GPIO_PIN1|GPIO_PIN2|GPIO_PIN3|GPIO_PIN4|GPIO_PIN5|GPIO_PIN6|GPIO_PIN7);
    GPIO_setOutputLowOnPin(GPIO_PORT_P5, GPIO_PIN0|GPIO_PIN1|GPIO_PIN2|GPIO_PIN3|GPIO_PIN4|GPIO_PIN5|GPIO_PIN6|GPIO_PIN7);
    GPIO_setOutputLowOnPin(GPIO_PORT_P6, GPIO_PIN0|GPIO_PIN1|GPIO_PIN2|GPIO_PIN3|GPIO_PIN4|GPIO_PIN5|GPIO_PIN6|GPIO_PIN7);
    GPIO_setOutputLowOnPin(GPIO_PORT_P7, GPIO_PIN0|GPIO_PIN1|GPIO_PIN2|GPIO_PIN3|GPIO_PIN4|GPIO_PIN5|GPIO_PIN6|GPIO_PIN7);
    GPIO_setOutputLowOnPin(GPIO_PORT_P8, GPIO_PIN0|GPIO_PIN1|GPIO_PIN2|GPIO_PIN3|GPIO_PIN4|GPIO_PIN5|GPIO_PIN6|GPIO_PIN7);

    GPIO_setAsOutputPin(GPIO_PORT_P1, GPIO_PIN0|GPIO_PIN1|GPIO_PIN2|GPIO_PIN3|GPIO_PIN4|GPIO_PIN5|GPIO_PIN6|GPIO_PIN7);
    GPIO_setAsOutputPin(GPIO_PORT_P2, GPIO_PIN0|GPIO_PIN1|GPIO_PIN2|GPIO_PIN3|GPIO_PIN4|GPIO_PIN5|GPIO_PIN6|GPIO_PIN7);
    GPIO_setAsOutputPin(GPIO_PORT_P3, GPIO_PIN0|GPIO_PIN1|GPIO_PIN2|GPIO_PIN3|GPIO_PIN4|GPIO_PIN5|GPIO_PIN6|GPIO_PIN7);
    GPIO_setAsOutputPin(GPIO_PORT_P4, GPIO_PIN0|GPIO_PIN1|GPIO_PIN2|GPIO_PIN3|GPIO_PIN4|GPIO_PIN5|GPIO_PIN6|GPIO_PIN7);
    GPIO_setAsOutputPin(GPIO_PORT_P5, GPIO_PIN0|GPIO_PIN1|GPIO_PIN2|GPIO_PIN3|GPIO_PIN4|GPIO_PIN5|GPIO_PIN6|GPIO_PIN7);
    GPIO_setAsOutputPin(GPIO_PORT_P6, GPIO_PIN0|GPIO_PIN1|GPIO_PIN2|GPIO_PIN3|GPIO_PIN4|GPIO_PIN5|GPIO_PIN6|GPIO_PIN7);
    GPIO_setAsOutputPin(GPIO_PORT_P7, GPIO_PIN0|GPIO_PIN1|GPIO_PIN2|GPIO_PIN3|GPIO_PIN4|GPIO_PIN5|GPIO_PIN6|GPIO_PIN7);
    GPIO_setAsOutputPin(GPIO_PORT_P8, GPIO_PIN0|GPIO_PIN1|GPIO_PIN2|GPIO_PIN3|GPIO_PIN4|GPIO_PIN5|GPIO_PIN6|GPIO_PIN7);

    //Set LaunchPad switches as inputs - they are active low, meaning '1' until pressed
    GPIO_setAsInputPinWithPullUpResistor(SW1_PORT, SW1_PIN);
    GPIO_setAsInputPinWithPullUpResistor(SW2_PORT, SW2_PIN);

    //Set LED1 and LED2 as outputs
//    GPIO_setAsOutputPin(LED1_PORT, LED1_PIN); //Comment if using UART
//    GPIO_setAsOutputPin(LED2_PORT, LED2_PIN);
}

/* Clock System Initialization */
void Init_Clock(void)
{
    /*
     * The MSP430 has a number of different on-chip clocks. You can read about it in
     * the section of the Family User Guide regarding the Clock System ('cs.h' in the
     * driverlib).
     */

    /*
     * On the LaunchPad, there is a 32.768 kHz crystal oscillator used as a
     * Real Time Clock (RTC). It is a quartz crystal connected to a circuit that
     * resonates it. Since the frequency is a power of two, you can use the signal
     * to drive a counter, and you know that the bits represent binary fractions
     * of one second. You can then have the RTC module throw an interrupt based
     * on a 'real time'. E.g., you could have your system sleep until every
     * 100 ms when it wakes up and checks the status of a sensor. Or, you could
     * sample the ADC once per second.
     */
    //Set P4.1 and P4.2 as Primary Module Function Input, XT_LF
    GPIO_setAsPeripheralModuleFunctionInputPin(GPIO_PORT_P4, GPIO_PIN1 + GPIO_PIN2, GPIO_PRIMARY_MODULE_FUNCTION);

    // Set external clock frequency to 32.768 KHz
    CS_setExternalClockSource(32768);
    // Set ACLK = XT1
    CS_initClockSignal(CS_ACLK, CS_XT1CLK_SELECT, CS_CLOCK_DIVIDER_1);
    // Initializes the XT1 crystal oscillator
    CS_turnOnXT1LF(CS_XT1_DRIVE_1);
    // Set SMCLK = DCO with frequency divider of 1
    CS_initClockSignal(CS_SMCLK, CS_DCOCLKDIV_SELECT, CS_CLOCK_DIVIDER_1);
    // Set MCLK = DCO with frequency divider of 1
    CS_initClockSignal(CS_MCLK, CS_DCOCLKDIV_SELECT, CS_CLOCK_DIVIDER_1);
}

/* UART Initialization */
void Init_UART(void)
{
    /* UART: It configures P1.0 and P1.1 to be connected internally to the
     * eSCSI module, which is a serial communications module, and places it
     * in UART mode. This let's you communicate with the PC via a software
     * COM port over the USB cable. You can use a console program, like PuTTY,
     * to type to your LaunchPad. The code in this sample just echos back
     * whatever character was received.
     */

    //Configure UART pins, which maps them to a COM port over the USB cable
    //Set P1.0 and P1.1 as Secondary Module Function Input.
    GPIO_setAsPeripheralModuleFunctionInputPin(GPIO_PORT_P1, GPIO_PIN1, GPIO_PRIMARY_MODULE_FUNCTION);
    GPIO_setAsPeripheralModuleFunctionOutputPin(GPIO_PORT_P1, GPIO_PIN0, GPIO_PRIMARY_MODULE_FUNCTION);

    /*
     * UART Configuration Parameter. These are the configuration parameters to
     * make the eUSCI A UART module to operate with a 9600 baud rate. These
     * values were calculated using the online calculator that TI provides at:
     * http://software-dl.ti.com/msp430/msp430_public_sw/mcu/msp430/MSP430BaudRateConverter/index.html
     */

    //SMCLK = 1MHz, Baudrate = 9600
    //UCBRx = 6, UCBRFx = 8, UCBRSx = 17, UCOS16 = 1
    EUSCI_A_UART_initParam param = {0};
        param.selectClockSource = EUSCI_A_UART_CLOCKSOURCE_SMCLK;
        param.clockPrescalar    = 6;
        param.firstModReg       = 8;
        param.secondModReg      = 17;
        param.parity            = EUSCI_A_UART_NO_PARITY;
        param.msborLsbFirst     = EUSCI_A_UART_LSB_FIRST;
        param.numberofStopBits  = EUSCI_A_UART_ONE_STOP_BIT;
        param.uartMode          = EUSCI_A_UART_MODE;
        param.overSampling      = 1;

    if(STATUS_FAIL == EUSCI_A_UART_init(EUSCI_A0_BASE, &param))
    {
        return;
    }

    EUSCI_A_UART_enable(EUSCI_A0_BASE);

    EUSCI_A_UART_clearInterrupt(EUSCI_A0_BASE, EUSCI_A_UART_RECEIVE_INTERRUPT);

    // Enable EUSCI_A0 RX interrupt
    EUSCI_A_UART_enableInterrupt(EUSCI_A0_BASE, EUSCI_A_UART_RECEIVE_INTERRUPT);
}

/* EUSCI A0 UART ISR - Echoes data back to PC host */
#pragma vector=USCI_A0_VECTOR
__interrupt
void EUSCIA0_ISR(void)
{
    uint8_t RxStatus = EUSCI_A_UART_getInterruptStatus(EUSCI_A0_BASE, EUSCI_A_UART_RECEIVE_INTERRUPT_FLAG);

    EUSCI_A_UART_clearInterrupt(EUSCI_A0_BASE, RxStatus);

    if (RxStatus)
    {
        EUSCI_A_UART_transmitData(EUSCI_A0_BASE, EUSCI_A_UART_receiveData(EUSCI_A0_BASE));
    }
}

/* PWM Initialization */
void Init_PWM(void)
{
    /*
     * The internal timers (TIMER_A) can auto-generate a PWM signal without needing to
     * flip an output bit every cycle in software. The catch is that it limits which
     * pins you can use to output the signal, whereas manually flipping an output bit
     * means it can be on any GPIO. This function populates a data structure that tells
     * the API to use the timer as a hardware-generated PWM source.
     *
     */
    //Generate PWM - Timer runs in Up-Down mode
    param.clockSource           = TIMER_A_CLOCKSOURCE_SMCLK;
    param.clockSourceDivider    = TIMER_A_CLOCKSOURCE_DIVIDER_1;
    param.timerPeriod           = TIMER_A_PERIOD; //Defined in main.h
    param.compareRegister       = TIMER_A_CAPTURECOMPARE_REGISTER_1;
    param.compareOutputMode     = TIMER_A_OUTPUTMODE_RESET_SET;
    param.dutyCycle             = HIGH_COUNT; //Defined in main.h

    //PWM_PORT PWM_PIN (defined in main.h) as PWM output
    GPIO_setAsPeripheralModuleFunctionOutputPin(PWM_PORT, PWM_PIN, GPIO_PRIMARY_MODULE_FUNCTION);
}

void Init_ADC(void)
{
    /*
     * To use the ADC, you need to tell a physical pin to be an analog input instead
     * of a GPIO, then you need to tell the ADC to use that analog input. Defined
     * these in main.h for A9 on P8.1.
     */

    //Set ADC_IN to input direction
    GPIO_setAsPeripheralModuleFunctionInputPin(ADC_IN_PORT, ADC_IN_PIN, GPIO_PRIMARY_MODULE_FUNCTION);

    //Initialize the ADC Module
    /*
     * Base Address for the ADC Module
     * Use internal ADC bit as sample/hold signal to start conversion
     * USE MODOSC 5MHZ Digital Oscillator as clock source
     * Use default clock divider of 1
     */
    ADC_init(ADC_BASE,
             ADC_SAMPLEHOLDSOURCE_SC,
             ADC_CLOCKSOURCE_ADCOSC,
             ADC_CLOCKDIVIDER_1);

    ADC_enable(ADC_BASE);

    /*
     * Base Address for the ADC Module
     * Sample/hold for 16 clock cycles
     * Do not enable Multiple Sampling
     */
    ADC_setupSamplingTimer(ADC_BASE,
                           ADC_CYCLEHOLD_16_CYCLES,
                           ADC_MULTIPLESAMPLESDISABLE);

    //Configure Memory Buffer
    /*
     * Base Address for the ADC Module
     * Use input ADC_IN_CHANNEL
     * Use positive reference of AVcc
     * Use negative reference of AVss
     */
    ADC_configureMemory(ADC_BASE,
                        ADC_IN_CHANNEL,
                        ADC_VREFPOS_AVCC,
                        ADC_VREFNEG_AVSS);

    ADC_clearInterrupt(ADC_BASE,
                       ADC_COMPLETED_INTERRUPT);

    //Enable Memory Buffer interrupt
    ADC_enableInterrupt(ADC_BASE,
                        ADC_COMPLETED_INTERRUPT);
}

//ADC interrupt service routine
#pragma vector=ADC_VECTOR
__interrupt
void ADC_ISR(void)
{
    uint8_t ADCStatus = ADC_getInterruptStatus(ADC_BASE, ADC_COMPLETED_INTERRUPT_FLAG);

    ADC_clearInterrupt(ADC_BASE, ADCStatus);

    if (ADCStatus)
    {
        ADCState = 0; //Not busy anymore
        ADCResult = ADC_getResults(ADC_BASE);
    }
}
