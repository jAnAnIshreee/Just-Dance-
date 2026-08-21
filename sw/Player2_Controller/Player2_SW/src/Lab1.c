// ============================================================
// Lab1.c
// Just Dance IMU framework for TM4C123 + LSM6DSOX
//
// ============================================================
// HOW TO ADD A NEW MOVE  (read this, josh)
// ============================================================
// Everything about a move now lives in ONE place: the
// gMoveInfo[] table near the bottom of the "move registry"
// section. Each entry bundles:
//
//   - the Move_t enum value
//   - display name
//   - score function pointer
//   - per-move segment-engine tuning (thresholds, sample caps)
//
// Choreography is ONLY the ordered list in gChoreo[]. Total
// choreography period is computed automatically from the sum
// of step durations -- you do NOT edit a #define for that.
//
// Steps to add a brand-new move:
//   1. Add MOVE_YOURMOVE to the Move_t enum.
//   2. Write int32_t ScoreYourMove(const Segment_t *seg){...}
//      (forward-declare it above gMoveInfo[]).
//   3. Add one line to gMoveInfo[]:
//        { MOVE_YOURMOVE, "YOUR MOVE", ScoreYourMove, {..tuning..} }
//      Use DEFAULT_SEGMENT_TUNING if you want the global defaults.
//   4. Drop it into gChoreo[] wherever it should dance.
//
// To reorder existing moves: edit ONLY gChoreo[].
// To retune segmentation for one move: edit ONLY its row in
//   gMoveInfo[]. No more hunting through UpdateGestureEngine.
//
// ============================================================
// Original notes (preserved):
// Uses real hardware time from Timer0A at 16 MHz.
// Choreography timing is no longer affected by UART/I2C/processing delays.
//
// Timing behavior:
// - calibration sampling spans exactly 5000 ms
// - dance timeline starts exactly at calibration_start + 5000 ms
// - first move begins 10000 ms later
// - total from calibration start to first move = 15000 ms
//
// Logical mapping kept as:
//      IRL X = sensor Y
//      IRL Y = sensor Z
//      IRL Z = sensor X
// ----------------------------------------------------------------------------
#include <stdint.h>
#include <stdbool.h>
#include "PLL.h"
#include "../inc/tm4c123gh6pm.h"

#define CTRL3_C_SW_RESET   0x01
#define CTRL3_C_IF_INC     0x04
#define CTRL3_C_BDU        0x40
/* ============================================================
   Test / runtime mode
   Change only these lines for quick testing
   ============================================================ */
#define TEST_MODE_ENABLE                0
#define TEST_MODE_MOVE                MOVE_RIGHT_ARM_BEAT

#define ARC_ML_DATASET_MODE             0  //TURN ON TO f(s->energy >= startThresh DATA
#define ARC_ML_INFERENCE_ENABLE         1


#define PRINT_SEGMENT_SCORES   0   // set to 0 to disable

#define PRINT_INDIVIDUAL_SEGMENT_SCORES   0   // easy to comment out //ADD THIS BACK


#define TEST_STREAM_PRINT_ENABLE        0
#define CHOREO_STREAM_PRINT_ENABLE      0
#define STREAM_PRINT_DIVIDER            20

/* ARC ML detect confidence display only.
   scoreOut for ARC ML is still "logit * 1000" */
#define ARC_ML_STRONG_LOGIT_MILLI       1200

/* ============================================================
   LSM6DSOX registers
   ============================================================ */
#define LSM6DSOX_ADDR       0x6A

#define WHO_AM_I            0x0F
#define CTRL1_XL            0x10
#define CTRL2_G             0x11
#define CTRL3_C             0x12
#define STATUS_REG          0x1E

#define OUTX_L_G            0x22
#define OUTX_L_A            0x28

/* ============================================================
   LED definitions
   ============================================================ */
#define LED_RED   0x02
#define LED_BLUE  0x04
#define LED_GREEN 0x08

/* ============================================================
   Real timing
   ============================================================ */
#define TIMER_TICKS_PER_MS              16000U   // 16 MHz

#define CALIBRATION_MS                  5000U
#define CALIBRATION_SAMPLE_PERIOD_MS    10U
#define MAIN_SAMPLE_PERIOD_MS           5U

/* ============================================================
   Choreography timing

   CHOREO_PERIOD_MS is computed at startup from gChoreo[] sum.
   You do NOT need to edit it when you add/remove steps.
   ============================================================ */
#define WINDOW_GRACE_MS                 450U

/* ============================================================
   Filter constants
   ============================================================ */
#define GYRO_LPF_SHIFT              2   // 1/4
#define ACCEL_LPF_SHIFT             3   // 1/8
#define GRAVITY_LPF_SHIFT           5   // 1/32
#define BIAS_LPF_SHIFT              5   // 1/32

/* ============================================================
   Stillness / bias update
   ============================================================ */
#define STILL_GYRO_MAG_THRESH       2400
#define STILL_ACCEL_DYN_THRESH      1600
#define STILL_COUNT_FOR_BIAS        20

/* ============================================================
   DEFAULT segment detection tuning

   These are the baseline thresholds used when a move does not
   override them. Per-move overrides live in gMoveInfo[].

   to start a motion, energy must exceed START_ENERGY_THRESH
   once already in motion, the segment stays alive until energy stays low enough
   if energy is low for QUIET_END_COUNT quiet counts, the segment ends
   segment must have at least SEGMENT_MIN_SAMPLES samples to count
   segment is forced to end at SEGMENT_MAX_SAMPLES max
   after ending, wait SEGMENT_COOLDOWN_MS ms before another segment can start
   ============================================================ */
#define START_ENERGY_THRESH         5400

#define CONTINUE_ENERGY_THRESH      2600
#define QUIET_END_COUNT             8
#define SEGMENT_MIN_SAMPLES         8

#define SEGMENT_MAX_SAMPLES         140
#define SEGMENT_COOLDOWN_MS         90

/* ============================================================
   Gesture scoring thresholds for rule-based moves
   ============================================================ */
#define SCORE_ACCEPT_THRESH         62
#define SCORE_STRONG_THRESH         84

#define DOMINANCE_NUM               10


/* ============================================================
   EEPROM-backed song selection
   Persists across TM4C reset button presses
   ============================================================ */
#define SONG_ID_DEFAULT         0U
#define SONG_ID_EEPROM_BLOCK    0U
#define SONG_ID_EEPROM_OFFSET   0U


/* ============================================================
   Right Arm Flex fixed-point NN model (16 -> 3 -> 1)
   NOTE: Python export was mislabeled as ARC, but these values
   are for MOVE_RIGHT_ARM_FLEX.
   ============================================================ */
#define RIGHT_ARM_FLEX_NN_INPUTS 16
#define RIGHT_ARM_FLEX_NN_HIDDEN 3
#define RIGHT_ARM_FLEX_NN_SCALE 1000

static const int64_t gRightArmFlexNnInputMeanScaled[16] = {
    -11905076, 397134943, 20038516, -1515115,
    15307987, 2019318, 402904567, 121889185,
    1299943, 845283752, -126318, 10389962,
    4571592, 6115, 18350, 1146
};

static const int32_t gRightArmFlexNnInputStdScaled[16] = {
    8438320, 189283995, 8305647, 10289860,
    8978951, 2342004, 284174002, 86004163,
    1262965, 371312594, 707671, 8413284,
    4213123, 35029, 21843, 1363
};

static const int32_t gRightArmFlexNnW1Scaled[16][3] = {
    { -1738,  276, -1429 },
    { -2524,  893,  -691 },
    { -1025, -1056, -663 },
    {  -775,  318,  -735 },
    { -1542, 2694,  1356 },
    {    52, -2692,  492 },
    {  -695, -1542, 1992 },
    {  -652, -1270, 2622 },
    {    61, -1855,  372 },
    {  -111, -1838, -493 },
    {   573,  -755, -322 },
    {   752,   313,  303 },
    {  3727,  2052,  876 },
    {     2,  -319,  516 },
    { -1726,  -953, -166 },
    { -1739,  -885, -252 }
};

static const int32_t gRightArmFlexNnB1Scaled[3] = { 3028, 192, -4220 };
static const int32_t gRightArmFlexNnW2Scaled[3] = { -5233, 5581, 5169 };
static const int32_t gRightArmFlexNnB2Scaled = -9527;
/* ============================================================
   ARC fixed-point NN model
   16 -> 3 -> 1, ReLU
   All values scaled by 1000.
   ============================================================ */
#define ARC_ML_FEATURE_COUNT        16
#define ARC_NN_INPUTS               16
#define ARC_NN_HIDDEN               3
#define ARC_NN_SCALE                1000

static const int64_t gArcNnInputMeanScaled[16] = { -4286045, 937880970, 23307576, 2960621, 21887939, 1207515, 484150636, 333695545, 372212, 2296309970, 73303, 7246667, 11352697, 19833, 31439, 4364 };

static const int32_t gArcNnInputStdScaled[16] = { 6789393, 345507439, 6554921, 6455742, 7540062, 1179725, 216339271, 153058171, 446443, 696415312, 267038, 5313862, 5347241, 41326, 20291, 2869 };

static const int32_t gArcNnW1Scaled[16][3] = {
    { -450,  346,  296 },
    { -913, 1341,  828 },
    { -122,  350,  160 },
    { -252,  215,  294 },
    {  205, -151,  -75 },
    {  507, -826, -456 },
    { -925,  698,  428 },
    { -663,  701,  713 },
    {  142, -513, -268 },
    {  289, -440, -246 },
    { -206,   48,  108 },
    {  283,  -60, -196 },
    { -416,  445,  488 },
    { -500,  163,  276 },
    {  -24, -225,  -47 },
    {  -33,  -44,  -41 }
};

static const int32_t gArcNnB1Scaled[3] = { 1359, 1411, 1427 };
static const int32_t gArcNnW2Scaled[3] = { -1512, 2407, 1534 };
static const int32_t gArcNnB2Scaled = -3410;

/* ============================================================
   Bent Arm Sway fixed-point NN model (16 -> 3 -> 1)
   ============================================================ */
#define SWAY_NN_INPUTS 16
#define SWAY_NN_HIDDEN 3
#define SWAY_NN_SCALE 1000

static const int32_t gSwayNnInputMeanScaled[16] = { 631579, 5446881, 8527537, 130519, 563302, 400102, 10752218, 6931126, 1479982, 703319, 30596, 6358372, 35628, 4568, 11896323, 1192053 };

static const int32_t gSwayNnInputStdScaled[16] = { 482376, 5216440, 5968406, 6396197, 321760, 293032, 6208636, 5269852, 5478226, 1073164, 515636, 3710559, 16712, 2595, 3992309, 1952465 };

static const int32_t gSwayNnW1Scaled[16][3] = {
    { 197, -19, 732 },
    { 271, 1955, 1560 },
    { 193, 150, 1592 },
    { 395, 915, 2368 },
    { 265, -323, 1462 },
    { 176, 92, 1058 },
    { 47, -2002, 179 },
    { -197, 599, -919 },
    { -79, 216, -411 },
    { -204, 34, -1318 },
    { 343, 439, 1909 },
    { -158, 294, -1437 },
    { 53, -699, 316 },
    { 62, -326, 271 },
    { 98, 216, 748 },
    { -7, -395, -150 }
};

static const int32_t gSwayNnB1Scaled[3] = { -41, -2154, -424 };
static const int32_t gSwayNnW2Scaled[3] = { 806, -3299, 4827 };
static const int32_t gSwayNnB2Scaled = -9809;

/* ============================================================
   Right Arm Wave fixed-point NN model (16 -> 3 -> 1)
   ============================================================ */
#define RIGHT_ARM_WAVE_NN_INPUTS 16
#define RIGHT_ARM_WAVE_NN_HIDDEN 3
#define RIGHT_ARM_WAVE_NN_SCALE 1000

static const int64_t gRightArmWaveNnInputMeanScaled[16] = {
    -5643873, 124021500, 4660091, 28718,
    5246445, 7595400, 100577700, 38666273,
    11214418, 240206573, 68573, 5672591,
    1366373, 46809, 3800, 309
};
/* ============================================================
   Right Arm Throw fixed-point NN model (16 -> 3 -> 1)
   NOTE: Python export was mislabeled as ARC, but these values
   are for MOVE_RIGHT_ARM_THROW.
   ============================================================ */
#define RIGHT_ARM_THROW_NN_INPUTS 16
#define RIGHT_ARM_THROW_NN_HIDDEN 3
#define RIGHT_ARM_THROW_NN_SCALE 1000

static const int64_t gRightArmThrowNnInputMeanScaled[16] = {
    -15414085, 178266101, 19887279, -5452488,
    11137403, 2181031, 186574860, 35907868,
    2306512, 617245775, -153264, 9961597,
    2201899, 40078, 12597, 504
};

/* ============================================================
   Right Arm Tall Scoop fixed-point NN model (16 -> 3 -> 1)
   NOTE: Python export was mislabeled as ARC, but these values
   are for MOVE_RIGHT_ARM_TALL_SCOOP.
   ============================================================ */
#define RIGHT_ARM_TALL_SCOOP_NN_INPUTS 16
#define RIGHT_ARM_TALL_SCOOP_NN_HIDDEN 3
#define RIGHT_ARM_TALL_SCOOP_NN_SCALE 1000

static const int64_t gRightArmTallScoopNnInputMeanScaled[16] = {
    -10894092, 374115626, 20586755, -868902,
    11934098, 1144650, 313671847, 116686117,
    1118098, 1190514540, 25552, 10025190,
    4978896, 60485, 8374, 644
};

static const int32_t gRightArmTallScoopNnInputStdScaled[16] = {
    11890892, 162014713, 6497215, 13903109,
    9413162, 1285931, 228504758, 85003758,
    1589565, 377046119, 784729, 8890628,
    5093632, 174839, 13407, 1031
};

static const int32_t gRightArmTallScoopNnW1Scaled[16][3] = {
    { -1313, 1347, -2130 },
    { -3904, -953, -3642 },
    { -2482, -865, -1314 },
    { -2715,  974, -1106 },
    { -1662, -2436, 2070 },
    { 1938, -4459, 2284 },
    { -1223, -1181,  753 },
    { -1151, -1176, 1017 },
    { 2548, 1465, 3795 },
    { -1503, -816,  -97 },
    { 3959, -2863, 3599 },
    { -2586,  -49,  551 },
    {  226, -1305, -2844 },
    { 1479, 3026, -868 },
    { 1621,  283,  291 },
    { 1565,  496,   47 }
};

static const int32_t gRightArmTallScoopNnB1Scaled[3] = { 1333, 868, 1113 };
static const int32_t gRightArmTallScoopNnW2Scaled[3] = { -8816, 6318, 7162 };
static const int32_t gRightArmTallScoopNnB2Scaled = -7547;
static const int32_t gRightArmThrowNnInputStdScaled[16] = {
    13491974, 109059461, 9193605, 12504597,
    8800294, 3061622, 141498832, 27190841,
    3226915, 270453709, 801525, 5169331,
    3479157, 145091, 16212, 648
};

static const int32_t gRightArmThrowNnW1Scaled[16][3] = {
    {  544, -868, -1253 },
    { -1012, -195,  -678 },
    {  873,  777,  -980 },
    {  186, -882, -1433 },
    { -2428, -460, 1493 },
    {  720, -1113, -748 },
    { -402,  757,  -622 },
    { -355,  771,  -464 },
    {  370, -515,  -918 },
    { 1528,  879, -1047 },
    {  634,   38,  -770 },
    { -1038, -283, -510 },
    {   68,   -1,  -436 },
    {  -12,   45,   -52 },
    { -651,  306,  -241 },
    { -653,  396,  -285 }
};

static const int32_t gRightArmThrowNnB1Scaled[3] = { 1871, -1196, -533 };
static const int32_t gRightArmThrowNnW2Scaled[3] = { -3552, 2644, 3362 };
static const int32_t gRightArmThrowNnB2Scaled = -5864;

/* ============================================================
   Flex Throw Hands fixed-point NN model (16 -> 3 -> 1)
   NOTE: Python export was mislabeled as ARC, but these values
   are for MOVE_FLEX_THROW_HANDS.
   ============================================================ */
#define FLEX_THROW_HANDS_NN_INPUTS 16
#define FLEX_THROW_HANDS_NN_HIDDEN 3
#define FLEX_THROW_HANDS_NN_SCALE 1000

static const int64_t gFlexThrowHandsNnInputMeanScaled[16] = {
    -17705541, 319374856, 21642342, -8536477,
    8927081, 1027577, 326410063, 84712874,
    1585910, 863936595, -290342, 9169063,
    3821658, 33793, 19063, 1009
};

static const int32_t gFlexThrowHandsNnInputStdScaled[16] = {
    13362690, 183629229, 9353837, 14728798,
    7892666, 1383379, 273709261, 75192830,
    4705605, 480386855, 754540, 7758600,
    4993876, 136125, 20419, 1070
};

static const int32_t gFlexThrowHandsNnW1Scaled[16][3] = {
    {  548, -442, -1062 },
    {  284, -750, -1437 },
    {  -57, 1131, 2104 },
    {  626, -436, -1357 },
    { 1245, -504, -677 },
    { 1248,  489,  990 },
    { -482,   23,  247 },
    {  140, -812, -1659 },
    {  863, -423, -894 },
    {  777, -778, -1867 },
    { -732,  -45,  167 },
    {  333, -260, -906 },
    { 1263,  156,  747 },
    {  366, -209, -401 },
    {  -80,  -83,   80 },
    {  289, -238, -734 }
};

static const int32_t gFlexThrowHandsNnB1Scaled[3] = { 3480, -272, -420 };
static const int32_t gFlexThrowHandsNnW2Scaled[3] = { -3075, 2149, 4503 };
static const int32_t gFlexThrowHandsNnB2Scaled = -3416;

static const int32_t gRightArmWaveNnInputStdScaled[16] = {
    4264131, 140086693, 4741404, 3109053,
    3664957, 14333050, 190194217, 75137836,
    24875734, 290425500, 665988, 4655677,
    2143262, 206754, 10522, 850
};

static const int32_t gRightArmWaveNnW1Scaled[16][3] = {
    {  677, -431, -822 },
    { -777,  210,  178 },
    {  -47,  -10, -413 },
    {  385, -309, -278 },
    { -1360, 736, 1855 },
    { -136,   85,   75 },
    {    3, -675, -953 },
    {   -7, -653, -932 },
    {  144, -210, -234 },
    {  245, -368, -483 },
    { -164,  131,  324 },
    { -572,  505,  546 },
    {   87,  -66,  148 },
    {   -7,  -20,  -11 },
    {  114, -222, -134 },
    {   91,  -14, -200 }
};

static const int32_t gRightArmWaveNnB1Scaled[3] = { 2963, 112, -249 };
static const int32_t gRightArmWaveNnW2Scaled[3] = { -1994, 1688, 2739 };
static const int32_t gRightArmWaveNnB2Scaled = -5273;


void Clock_Init16MHz_MOSC(void){
    SYSCTL_RCC2_R |= 0x80000000;          // USERCC2
    SYSCTL_RCC2_R |= 0x00000800;          // BYPASS2

    SYSCTL_RCC_R &= ~0x00000030;          // clear OSCSRC
    SYSCTL_RCC2_R &= ~0x00000070;         // clear OSCSRC2
                                              // both select MOSC = 0

    SYSCTL_RCC_R = (SYSCTL_RCC_R & ~0x000007C0) | 0x00000540; // 16 MHz crystal

    SYSCTL_RCC_R &= ~0x00400000;          // no system divider
    SYSCTL_RCC2_R |= 0x00002000;          // power down PLL

    volatile uint32_t delay;
    delay = SYSCTL_RCC_R;
    delay = SYSCTL_RCC_R;
    delay = SYSCTL_RCC_R;
    (void)delay;
}

/* ============================================================
   Helpers
   ============================================================ */
#define ARC_RATIO_SCALE 1000

int32_t SafeDivScaled(int32_t num, int32_t den, int32_t scale){
    if(den <= 0) return 0;
    return (int32_t)(((int64_t)num * scale) / den);
}

int32_t ClampNonNegative(int32_t x){
    return (x < 0) ? 0 : x;
}

int32_t Abs32(int32_t x){
    return (x < 0) ? -x : x;
}
int32_t Max32(int32_t a, int32_t b){
    return (a > b) ? a : b;
}
int32_t Min32(int32_t a, int32_t b){
    return (a < b) ? a : b;
}

/* ============================================================
   Global calibration offsets (RAW sensor frame)
   ============================================================ */
int32_t ax_offset = 0;
int32_t ay_offset = 0;
int32_t az_offset = 0;
int32_t gx_offset = 0;
int32_t gy_offset = 0;
int32_t gz_offset = 0;

/* ============================================================
   Adaptive gyro bias (logical frame)
   ============================================================ */
int32_t lgx_bias = 0;
int32_t lgy_bias = 0;
int32_t lgz_bias = 0;

/* ============================================================
   Neutral logical pose captured at calibration
   This is the sideways handheld reference pose.
   ============================================================ */
int32_t gNeutralAx = 0;
int32_t gNeutralAy = 0;
int32_t gNeutralAz = 0;

/* ============================================================
   Real-time markers (milliseconds from boot)
   ============================================================ */
uint32_t gDanceStartMs = 0;
static uint8_t gPE3StepState = 0;

/* Computed once at startup from ACTIVE choreography sum */
static uint32_t gChoreoPeriodMs = 0;

/* EEPROM-backed active song selection */
static uint8_t activeSongID = SONG_ID_DEFAULT;
static uint32_t gActiveChoreoCount = 0;

/* ============================================================
   UART5 on PE4 (RX) / PE5 (TX) — goes to espA
   PE4 = U5RX, PE5 = U5TX (PMC = 1 for both)
   Baud: 115200 @ 16 MHz
   ============================================================ */
void UART5_Init(void){
    SYSCTL_RCGCUART_R |= 0x20;        // enable UART5
    SYSCTL_RCGCGPIO_R |= 0x10;        // enable Port E
    while((SYSCTL_PRGPIO_R & 0x10) == 0){}

    UART5_CTL_R &= ~0x01;             // disable UART5 during config
    UART5_IBRD_R = 8;                 // 16 MHz / (16 * 115200) = 8.6805
    UART5_FBRD_R = 44;                // fractional part
    UART5_LCRH_R = 0x70;              // 8-bit, FIFO enable, no parity
    UART5_CC_R = 0x0;                 // system clock
    UART5_CTL_R = 0x301;              // enable UART5, TXE, RXE

    GPIO_PORTE_AFSEL_R |= 0x30;       // PE4, PE5 alt function
    GPIO_PORTE_DEN_R   |= 0x30;       // digital enable
    GPIO_PORTE_AMSEL_R &= ~0x30;      // disable analog
    /* PCTL: PE4 -> 1 (U5RX), PE5 -> 1 (U5TX) */
    GPIO_PORTE_PCTL_R = (GPIO_PORTE_PCTL_R & ~0x00FF0000) | 0x00110000;
    GPIO_PORTE_DIR_R = (GPIO_PORTE_DIR_R & ~0x10) | 0x20;  // PE4 in, PE5 out
}

void UART5_OutChar(char data){
    while((UART5_FR_R & 0x20) != 0){}
    UART5_DR_R = data;
}
void UART5_FlushRx(void){
    while((UART5_FR_R & 0x10) == 0){
        volatile char dump = (char)(UART5_DR_R & 0xFF);
        (void)dump;
    }
}
void UART5_OutString(const char *pt){
    while(*pt){
        UART5_OutChar(*pt);
        pt++;
    }
}

void UART5_OutUDec(uint32_t n){
    if(n >= 10){
        UART5_OutUDec(n / 10);
        n = n % 10;
    }
    UART5_OutChar(n + '0');
}

void UART5_OutSDec(int32_t n){
    if(n < 0){
        UART5_OutChar('-');
        n = -n;
    }
    UART5_OutUDec((uint32_t)n);
}
/* ============================================================
   Hardware timer: Timer0A free-running at 16 MHz
   ============================================================ */
void Timer0A_InitFreeRunning(void){
    SYSCTL_RCGCTIMER_R |= 0x01;
    while((SYSCTL_PRTIMER_R & 0x01) == 0){}

    TIMER0_CTL_R &= ~0x01;
    TIMER0_CFG_R = 0x00;
    TIMER0_TAMR_R = 0x02;
    TIMER0_TAILR_R = 0xFFFFFFFF;
    TIMER0_TAPR_R = 0;
    TIMER0_ICR_R = 0x01;
    TIMER0_CTL_R |= 0x01;
}

uint32_t TimeNowTicks(void){
    return (0xFFFFFFFFU - TIMER0_TAR_R);
}

uint32_t TimeNowMs(void){
    return TimeNowTicks() / TIMER_TICKS_PER_MS;
}

bool TimeReachedMs(uint32_t now, uint32_t target){
    return ((int32_t)(now - target) >= 0);
}

void WaitUntilMs(uint32_t targetMs){
    while(!TimeReachedMs(TimeNowMs(), targetMs)){
    }
}

/* Skip missed slots instead of trying to "catch up" with a burst of loops */
void AdvanceToNextSample(uint32_t *nextSampleMs, uint32_t periodMs){
    uint32_t nowMs = TimeNowMs();

    if(TimeReachedMs(nowMs, *nextSampleMs)){
        do{
            *nextSampleMs += periodMs;
        }while(TimeReachedMs(nowMs, *nextSampleMs));
    }

    WaitUntilMs(*nextSampleMs);
    *nextSampleMs += periodMs;
}

/* ============================================================
   LED setup
   ============================================================ */

void PD3_Init(void);
void PD3_On(void);
void PD3_Off(void);
void PD3_Toggle(void);

void LED_Init(void){
    SYSCTL_RCGCGPIO_R |= 0x20;
    while((SYSCTL_PRGPIO_R & 0x20) == 0){}
    GPIO_PORTF_DIR_R |= 0x0E;
    GPIO_PORTF_DEN_R |= 0x0E;
    GPIO_PORTF_AFSEL_R &= ~0x0E;
    GPIO_PORTF_AMSEL_R &= ~0x0E;
}
void LED_Set(uint8_t color){
    GPIO_PORTF_DATA_R = (GPIO_PORTF_DATA_R & ~0x0E) | color;
}
void PD3_Init(void){
    SYSCTL_RCGCGPIO_R |= 0x08;                 // enable Port D clock
    while((SYSCTL_PRGPIO_R & 0x08) == 0){}    // wait until Port D ready

    GPIO_PORTD_DIR_R   |= 0x08;               // PD3 output
    GPIO_PORTD_DEN_R   |= 0x08;               // digital enable PD3
    GPIO_PORTD_AFSEL_R &= ~0x08;              // PD3 = GPIO
    GPIO_PORTD_AMSEL_R &= ~0x08;              // no analog on PD3
    GPIO_PORTD_PCTL_R  &= ~0x0000F000;        // clear only PD3 PCTL nibble
}

void PD3_On(void){
    GPIO_PORTD_DATA_R &= ~0x08;               // negative logic: 0 = LED ON
}

void PD3_Off(void){
    GPIO_PORTD_DATA_R |= 0x08;                // negative logic: 1 = LED OFF
}

void PD3_Toggle(void){
    GPIO_PORTD_DATA_R ^= 0x08;
}

/* ============================================================
   UART0
   ============================================================ */
void UART0_Init(void){
    SYSCTL_RCGCUART_R |= 0x01;
    SYSCTL_RCGCGPIO_R |= 0x01;
    while((SYSCTL_PRGPIO_R & 0x01) == 0){}

    UART0_CTL_R &= ~0x01;
    UART0_IBRD_R = 8;     // 115200 @ 16 MHz
    UART0_FBRD_R = 44;
    UART0_LCRH_R = 0x70;
    UART0_CC_R = 0x0;
    UART0_CTL_R = 0x301;

    GPIO_PORTA_AFSEL_R |= 0x03;
    GPIO_PORTA_DEN_R   |= 0x03;
    GPIO_PORTA_AMSEL_R &= ~0x03;
    GPIO_PORTA_PCTL_R = (GPIO_PORTA_PCTL_R & ~0x000000FF) | 0x00000011;
}
void UART0_OutChar(char data){
    while((UART0_FR_R & 0x20) != 0){}
    UART0_DR_R = data;
}
void UART0_OutString(const char *pt){
    while(*pt){
        UART0_OutChar(*pt);
        pt++;
    }
}
void UART0_OutUDec(uint32_t n){
    if(n >= 10){
        UART0_OutUDec(n / 10);
        n = n % 10;
    }
    UART0_OutChar(n + '0');
}
void UART0_OutSDec(int32_t n){
    if(n < 0){
        UART0_OutChar('-');
        n = -n;
    }
    UART0_OutUDec((uint32_t)n);
}
void UART0_OutUDec3(uint32_t n){
    UART0_OutChar((char)(((n / 100U) % 10U) + '0'));
    UART0_OutChar((char)(((n / 10U) % 10U) + '0'));
    UART0_OutChar((char)((n % 10U) + '0'));
}
void UART0_OutSFixed3FromMilli(int32_t milli){
    if(milli < 0){
        UART0_OutChar('-');
        milli = -milli;
    }
    UART0_OutUDec((uint32_t)(milli / 1000));
    UART0_OutChar('.');
    UART0_OutUDec3((uint32_t)(milli % 1000));
}

/* ============================================================
   I2C1
   ============================================================ */
void I2C1_Init(void){
    SYSCTL_RCGCI2C_R |= 0x02;
    SYSCTL_RCGCGPIO_R |= 0x01;
    while((SYSCTL_PRGPIO_R & 0x01) == 0){}

    GPIO_PORTA_AFSEL_R |= 0xC0;
    GPIO_PORTA_ODR_R   |= 0x80;
    GPIO_PORTA_DEN_R   |= 0xC0;
    GPIO_PORTA_AMSEL_R &= ~0xC0;
    GPIO_PORTA_PCTL_R = (GPIO_PORTA_PCTL_R & ~0xFF000000) | 0x33000000;

    I2C1_MCR_R = 0x10;
    I2C1_MTPR_R = 1;
}
bool I2C1_Wait(void){
    volatile uint32_t timeout = 20000;   // tiny, deterministic budget

    while((I2C1_MCS_R & 0x01) && timeout){
        timeout--;
    }

    if(timeout == 0){
        return false;
    }

    if(I2C1_MCS_R & 0x02){   // ERROR
        return false;
    }

    return true;
}
bool I2C1_Write(uint8_t reg, uint8_t data){
    I2C1_MSA_R = (LSM6DSOX_ADDR << 1);
    I2C1_MDR_R = reg;
    I2C1_MCS_R = 0x03;
    if(!I2C1_Wait()) return false;

    I2C1_MDR_R = data;
    I2C1_MCS_R = 0x05;
    if(!I2C1_Wait()) return false;
    return true;
}
bool I2C1_Read(uint8_t reg, uint8_t *data){
    I2C1_MSA_R = (LSM6DSOX_ADDR << 1);
    I2C1_MDR_R = reg;
    I2C1_MCS_R = 0x03;
    if(!I2C1_Wait()) return false;

    I2C1_MSA_R = (LSM6DSOX_ADDR << 1) | 1;
    I2C1_MCS_R = 0x07;
    if(!I2C1_Wait()) return false;

    *data = I2C1_MDR_R & 0xFF;
    return true;
}

/* ============================================================
   Sensor Init
   ============================================================ */
bool LSM6DSOX_Init(void){
    uint8_t id, reg;
    uint32_t tries = 50;

    if(!I2C1_Read(WHO_AM_I, &id)) return false;
    if(id != 0x6C) return false;

    if(!I2C1_Write(CTRL3_C, CTRL3_C_SW_RESET)) return false;

    do{
        if(!I2C1_Read(CTRL3_C, &reg)) return false;
        if((reg & CTRL3_C_SW_RESET) == 0) break;
        tries--;
    }while(tries);

    if(tries == 0) return false;

    if(!I2C1_Write(CTRL3_C, CTRL3_C_BDU | CTRL3_C_IF_INC)) return false;
    if(!I2C1_Write(CTRL1_XL, 0x68)) return false;
    if(!I2C1_Write(CTRL2_G, 0x6C)) return false;

    return true;
}

/* ============================================================
   Read Raw Data
   ============================================================ */
bool ReadRaw(int16_t *ax, int16_t *ay, int16_t *az,
             int16_t *gx, int16_t *gy, int16_t *gz){
    uint8_t status;
    uint8_t lo, hi;
    uint32_t tries = 200;

    do{
        if(!I2C1_Read(STATUS_REG, &status)) return false;
        if((status & 0x03) == 0x03) break;
        tries--;
    }while(tries);

    if(tries == 0) return false;

    if(!I2C1_Read(OUTX_L_G + 0, &lo)) return false;
    if(!I2C1_Read(OUTX_L_G + 1, &hi)) return false;
    *gx = (int16_t)((hi << 8) | lo);

    if(!I2C1_Read(OUTX_L_G + 2, &lo)) return false;
    if(!I2C1_Read(OUTX_L_G + 3, &hi)) return false;
    *gy = (int16_t)((hi << 8) | lo);

    if(!I2C1_Read(OUTX_L_G + 4, &lo)) return false;
    if(!I2C1_Read(OUTX_L_G + 5, &hi)) return false;
    *gz = (int16_t)((hi << 8) | lo);

    if(!I2C1_Read(OUTX_L_A + 0, &lo)) return false;
    if(!I2C1_Read(OUTX_L_A + 1, &hi)) return false;
    *ax = (int16_t)((hi << 8) | lo);

    if(!I2C1_Read(OUTX_L_A + 2, &lo)) return false;
    if(!I2C1_Read(OUTX_L_A + 3, &hi)) return false;
    *ay = (int16_t)((hi << 8) | lo);

    if(!I2C1_Read(OUTX_L_A + 4, &lo)) return false;
    if(!I2C1_Read(OUTX_L_A + 5, &hi)) return false;
    *az = (int16_t)((hi << 8) | lo);

    return true;
}

/* ============================================================
   Mapping kept exactly as requested
      IRL X = sensor Y
      IRL Y = sensor Z
      IRL Z = sensor X
   ============================================================ */
void RemapToLogicalFrame(int32_t ax_in, int32_t ay_in, int32_t az_in,
                         int32_t gx_in, int32_t gy_in, int32_t gz_in,
                         int32_t *ax_out, int32_t *ay_out, int32_t *az_out,
                         int32_t *gx_out, int32_t *gy_out, int32_t *gz_out){
    *ax_out = ay_in;
    *ay_out = az_in;
    *az_out = ax_in;

    *gx_out = gy_in;
    *gy_out = gz_in;
    *gz_out = gx_in;
}

/* ============================================================
   Calibration
   ============================================================ */
bool Calibrate(uint32_t *calibrationStartMsOut){
    int64_t ax = 0, ay = 0, az = 0;
    int64_t gx = 0, gy = 0, gz = 0;
    int64_t lax_sum = 0, lay_sum = 0, laz_sum = 0;
    uint32_t calibStartMs;
    uint32_t nextSampleMs;
    int i;

    LED_Set(LED_RED);

    calibStartMs = TimeNowMs();
    nextSampleMs = calibStartMs + CALIBRATION_SAMPLE_PERIOD_MS;

    for(i = 0; i < 500; i++){
        int16_t axr, ayr, azr, gxr, gyr, gzr;
        int32_t lax, lay, laz, lgx, lgy, lgz;

        WaitUntilMs(nextSampleMs);
				if((i % 10) == 0){     // every 10 samples = every 100 ms
     PD3_Toggle();
}
        if(!ReadRaw(&axr, &ayr, &azr, &gxr, &gyr, &gzr)){
            return false;
        }

        ax += axr;
        ay += ayr;
        az += azr;
        gx += gxr;
        gy += gyr;
        gz += gzr;

        RemapToLogicalFrame(axr, ayr, azr, gxr, gyr, gzr,
                            &lax, &lay, &laz, &lgx, &lgy, &lgz);

        lax_sum += lax;
        lay_sum += lay;
        laz_sum += laz;

        nextSampleMs += CALIBRATION_SAMPLE_PERIOD_MS;
    }

    ax_offset = (int32_t)(ax / 500);
    ay_offset = (int32_t)(ay / 500);
    az_offset = (int32_t)(az / 500);
    gx_offset = (int32_t)(gx / 500);
    gy_offset = (int32_t)(gy / 500);
    gz_offset = (int32_t)(gz / 500);

    gNeutralAx = (int32_t)(lax_sum / 500) - ay_offset;
    gNeutralAy = (int32_t)(lay_sum / 500) - az_offset;
    gNeutralAz = (int32_t)(laz_sum / 500) - ax_offset;

    lgx_bias = 0;
    lgy_bias = 0;
    lgz_bias = 0;

*calibrationStartMsOut = calibStartMs;

PD3_Off();
gPE3StepState = 0;

return true;
}

/* ============================================================
   Processed sample
   ============================================================ */
typedef struct{
    int32_t ax;
    int32_t ay;
    int32_t az;

    int32_t gx;
    int32_t gy;
    int32_t gz;

    int32_t ax_dyn;
    int32_t ay_dyn;
    int32_t az_dyn;

    int32_t pose_dx;
    int32_t pose_dy;
    int32_t pose_dz;

    int32_t gyroMag;
    int32_t accelDynMag;
    int32_t energy;
} ProcSample_t;

/* ============================================================
   Segment summary
   ============================================================ */
typedef struct{
    uint32_t startMs;
    uint32_t endMs;
    uint16_t durationMs;
    uint16_t samples;

    int32_t accAreaX;
    int32_t accAreaY;
    int32_t accAreaZ;

    int32_t absAccAreaX;
    int32_t absAccAreaY;
    int32_t absAccAreaZ;

    int32_t gyrAreaX;
    int32_t gyrAreaY;
    int32_t gyrAreaZ;

    int32_t absGyrAreaX;
    int32_t absGyrAreaY;
    int32_t absGyrAreaZ;

    int32_t poseAreaX;
    int32_t poseAreaY;
    int32_t poseAreaZ;

    int32_t peakPosePosY;
    int32_t peakPoseNegY;
    int32_t peakPosePosZ;
    int32_t peakPoseNegZ;

    int32_t peakAccPosX;
    int32_t peakAccPosY;
    int32_t peakAccPosZ;

    int32_t peakAccNegX;
    int32_t peakAccNegY;
    int32_t peakAccNegZ;

    int32_t peakGyrX;
    int32_t peakGyrY;
    int32_t peakGyrZ;

    uint8_t zeroCrossX;
    uint8_t zeroCrossY;
    uint8_t zeroCrossZ;

    int32_t startPoseX;
    int32_t startPoseY;
    int32_t startPoseZ;

    int32_t endPoseX;
    int32_t endPoseY;
    int32_t endPoseZ;

    int32_t minPoseX;
    int32_t maxPoseX;
    int32_t minPoseZ;
    int32_t maxPoseZ;

    uint16_t minPoseZIndex;
    uint16_t maxPoseZIndex;

    uint8_t poseSlopeFlipX;
    uint8_t poseSlopeFlipZ;
		int32_t peakEnergy;
} Segment_t;

typedef struct{
    int32_t x[ARC_ML_FEATURE_COUNT];
} ArcMLFeatures_t;

typedef struct{
    int32_t durationMs;
    int32_t samples;

    int32_t netRiseZ;
    int32_t totalRiseZ;
    int32_t endHighZ;
    int32_t startLowZ;
    int32_t crossBodyX;
    int32_t smallDipY;
    int32_t finishDropFromPeak;

    int32_t oscillationCount;
    int32_t slopeFlipCount;

    int32_t totalAbsAcc;
    int32_t totalAbsGyr;
    int32_t absAccAreaZ;
    int32_t absGyrAreaZ;
    int32_t peakAccPosZ;

    int32_t netRiseFrac;
    int32_t endHighFrac;
    int32_t startLowFrac;
    int32_t crossBodyFrac;
    int32_t smallDipFrac;
    int32_t finishDropFrac;

    int32_t peakTimeFrac;
    int32_t riseOrderFlag;

    int32_t oscDensity;
    int32_t flipDensity;

    int32_t spinFracZ;
    int32_t spinPerMs;
    int32_t peakAccZFrac;
    int32_t zTravelDominance;
    int32_t accZFrac;
    int32_t avgEnergyPerSample;
} ArcDerived_t;

void BuildArcDerived(const Segment_t *seg, ArcDerived_t *d){
    int32_t totalAbsAcc;
    int32_t totalAbsGyr;
    int32_t netRiseZ;
    int32_t totalRiseZ;
    int32_t endHighZ;
    int32_t startLowZ;
    int32_t crossBodyX;
    int32_t smallDipY;
    int32_t finishDropFromPeak;
    int32_t oscillationCount;
    int32_t slopeFlipCount;
    int32_t safeRise;
    int32_t safeSamples;
    int32_t safeDuration;
    int32_t totalEnergy;

    netRiseZ = seg->endPoseZ - seg->startPoseZ;
    totalRiseZ = seg->maxPoseZ - seg->minPoseZ;
    endHighZ = seg->endPoseZ;
    startLowZ = -seg->startPoseZ;
    crossBodyX = seg->maxPoseX - seg->minPoseX;
    smallDipY = Abs32(seg->peakPoseNegY);
    finishDropFromPeak = seg->maxPoseZ - seg->endPoseZ;

    oscillationCount = seg->zeroCrossX + seg->zeroCrossY + seg->zeroCrossZ;
    slopeFlipCount = seg->poseSlopeFlipX + seg->poseSlopeFlipZ;

    totalAbsAcc = seg->absAccAreaX + seg->absAccAreaY + seg->absAccAreaZ;
    totalAbsGyr = seg->absGyrAreaX + seg->absGyrAreaY + seg->absGyrAreaZ;

    safeRise = Max32(totalRiseZ, 1);
    safeSamples = Max32(seg->samples, 1);
    safeDuration = Max32(seg->durationMs, 1);
    totalEnergy = totalAbsAcc + totalAbsGyr;

    d->durationMs = seg->durationMs;
    d->samples = seg->samples;

    d->netRiseZ = netRiseZ;
    d->totalRiseZ = totalRiseZ;
    d->endHighZ = endHighZ;
    d->startLowZ = startLowZ;
    d->crossBodyX = crossBodyX;
    d->smallDipY = smallDipY;
    d->finishDropFromPeak = finishDropFromPeak;

    d->oscillationCount = oscillationCount;
    d->slopeFlipCount = slopeFlipCount;

    d->totalAbsAcc = totalAbsAcc;
    d->totalAbsGyr = totalAbsGyr;
    d->absAccAreaZ = seg->absAccAreaZ;
    d->absGyrAreaZ = seg->absGyrAreaZ;
    d->peakAccPosZ = seg->peakAccPosZ;

    d->netRiseFrac      = SafeDivScaled(netRiseZ, safeRise, ARC_RATIO_SCALE);
    d->endHighFrac      = SafeDivScaled(ClampNonNegative(endHighZ), safeRise, ARC_RATIO_SCALE);
    d->startLowFrac     = SafeDivScaled(ClampNonNegative(startLowZ), safeRise, ARC_RATIO_SCALE);
    d->crossBodyFrac    = SafeDivScaled(crossBodyX, safeRise, ARC_RATIO_SCALE);
    d->smallDipFrac     = SafeDivScaled(smallDipY, safeRise, ARC_RATIO_SCALE);
    d->finishDropFrac   = SafeDivScaled(finishDropFromPeak, safeRise, ARC_RATIO_SCALE);

    d->peakTimeFrac     = SafeDivScaled(seg->maxPoseZIndex, safeSamples, ARC_RATIO_SCALE);
    d->riseOrderFlag    = (seg->maxPoseZIndex > seg->minPoseZIndex) ? ARC_RATIO_SCALE : 0;

    d->oscDensity       = SafeDivScaled(oscillationCount, safeSamples, ARC_RATIO_SCALE);
    d->flipDensity      = SafeDivScaled(slopeFlipCount, safeSamples, ARC_RATIO_SCALE);

    d->spinFracZ        = SafeDivScaled(seg->absGyrAreaZ, Max32(totalAbsGyr, 1), ARC_RATIO_SCALE);
    d->spinPerMs        = SafeDivScaled(seg->absGyrAreaZ, safeDuration, ARC_RATIO_SCALE);
    d->peakAccZFrac     = SafeDivScaled(seg->peakAccPosZ, Max32(seg->absAccAreaZ, 1), ARC_RATIO_SCALE);
    d->zTravelDominance = SafeDivScaled(totalRiseZ, Max32(crossBodyX, 1), ARC_RATIO_SCALE);
    d->accZFrac         = SafeDivScaled(seg->absAccAreaZ, Max32(totalAbsAcc, 1), ARC_RATIO_SCALE);
    d->avgEnergyPerSample = SafeDivScaled(totalEnergy, safeSamples, 1);
}

typedef enum{
    MOVE_NONE = 0,
    MOVE_WAIT,
    MOVE_PUMP_UP,
    MOVE_SIDE_TO_SIDE,
    MOVE_PUMP_DOWN,
    MOVE_ARC,
    MOVE_CROSS_ARMS,
    MOVE_BENT_ARM_SWAY,
    MOVE_KICK,
    MOVE_GANGAM,
    MOVE_ENERGY,
    MOVE_HYPE,
    MOVE_SWIPE,
    MOVE_COWBOY,

    MOVE_LEFT_ARM_WAVE,
    MOVE_RIGHT_ARM_WAVE,
    MOVE_WAVE,
    MOVE_HOP,
    MOVE_RIGHT_ARM_COME_HERE_SCOOP,
    MOVE_RIGHT_ARM_TALL_SCOOP,
    MOVE_SPIN_JUMP,
    MOVE_RIGHT_ARM_THROW,
    MOVE_LEFT_ARM_THROW,
    MOVE_FLEX_THROW_HANDS,
    MOVE_RIGHT_ARM_PUNCH,
    MOVE_RIGHT_ARM_FLEX,
    MOVE_WINDING_ARMS,
    MOVE_ARM_SLOW_UP_TO_DOWN,
		 MOVE_RIGHT_ARM_BEAT,
    MOVE_RIGHT_ARM_HIGH_LOW,
    MOVE_RIGHT_ARM_LOW_HIGH,
    MOVE_LEFT_ARM_LOW_HIGH,
    MOVE_PUNCH,
    MOVE_PUNCH_UP,
    MOVE_RIGHT_ARM,
    MOVE_SQUIGGLE,
    MOVE_UP,
    MOVE_ARMS_SCOOP_DOWN,
    MOVE_RIGHT_JABS,
    MOVE_LEFT_JABS,
    MOVE_POINT_LEFT_TO_RIGHT,
    MOVE_ARM_DOWN_POINT,
} Move_t;

typedef enum{
    SEG_IDLE = 0,
    SEG_ACTIVE,
    SEG_COOLDOWN
} SegmentState_t;

/* ============================================================
   Per-move segment engine tuning

   Any move can override any subset of the four segment-engine
   thresholds. A value of SEG_USE_DEFAULT means "use the global
   default constant for this field". This is how we keep the
   global defaults in one place while still allowing per-move
   customization without touching UpdateGestureEngine().
   ============================================================ */
#define SEG_USE_DEFAULT  (-1)

typedef struct{
    int32_t startThresh;        /* energy threshold to START a segment */
    int32_t continueThresh;     /* energy threshold below which we count quiet samples */
    int32_t quietCountThresh;   /* how many quiet samples end the segment */
    int32_t maxSamplesThresh;   /* hard cap on samples before forced end */
} SegmentTuning_t;

/* Convenience: everything inherits defaults */
#define DEFAULT_SEGMENT_TUNING  { SEG_USE_DEFAULT, SEG_USE_DEFAULT, SEG_USE_DEFAULT, SEG_USE_DEFAULT }

/* ============================================================
   Move registry + choreography table
   ============================================================ */
typedef int32_t (*MoveScoreFunc_t)(const Segment_t *seg);

typedef struct{
    Move_t move;
    const char *name;
    MoveScoreFunc_t scoreFunc;
    SegmentTuning_t tuning;
} MoveInfo_t;

typedef struct{
    Move_t move;
    uint32_t durationMs;
} ChoreoStep_t;
static const ChoreoStep_t *gActiveChoreo = 0;

/* Forward declarations for score functions used in table */
int32_t ScoreWait(const Segment_t *seg);
int32_t ScorePumpUp(const Segment_t *seg);
int32_t ScorePumpDown(const Segment_t *seg);
int32_t ScoreSideToSide(const Segment_t *seg);
int32_t ScoreArc(const Segment_t *seg);
int32_t ScoreCrossArms(const Segment_t *seg);
int32_t ScoreBentArmSway(const Segment_t *seg);
int32_t ScoreKick(const Segment_t *seg);
int32_t ScoreGangam(const Segment_t *seg);
int32_t ScoreSwipe(const Segment_t *seg);
int32_t ScoreCowboy(const Segment_t *seg);
int32_t ScoreEnergy(const Segment_t *seg);
int32_t ScoreHype(const Segment_t *seg);

int32_t ScoreLeftArmWave(const Segment_t *seg);
int32_t ScoreRightArmWave(const Segment_t *seg);
int32_t ScoreWave(const Segment_t *seg);
int32_t ScoreHop(const Segment_t *seg);
int32_t ScoreRightArmComeHereScoop(const Segment_t *seg);
int32_t ScoreRightArmTallScoop(const Segment_t *seg);
int32_t ScoreSpinJump(const Segment_t *seg);
int32_t ScoreRightArmThrow(const Segment_t *seg);
int32_t ScoreLeftArmThrow(const Segment_t *seg);
int32_t ScoreFlexThrowHands(const Segment_t *seg);
int32_t ScoreRightArmPunch(const Segment_t *seg);
int32_t ScoreRightArmFlex(const Segment_t *seg);
int32_t ScoreWindingArms(const Segment_t *seg);
int32_t ScoreArmSlowUpToDown(const Segment_t *seg);
int32_t ScoreRightArmBeat(const Segment_t *seg);
int32_t ScoreRightArmHighLow(const Segment_t *seg);
int32_t ScoreRightArmLowHigh(const Segment_t *seg);
int32_t ScoreLeftArmLowHigh(const Segment_t *seg);
int32_t ScorePunch(const Segment_t *seg);
int32_t ScorePunchUp(const Segment_t *seg);
int32_t ScoreRightArm(const Segment_t *seg);
int32_t ScoreSquiggle(const Segment_t *seg);
int32_t ScoreUp(const Segment_t *seg);
int32_t ScoreArmsScoopDown(const Segment_t *seg);
int32_t ScoreRightJabs(const Segment_t *seg);
int32_t ScoreLeftJabs(const Segment_t *seg);
int32_t ScorePointLeftToRight(const Segment_t *seg);
int32_t ScoreArmDownPoint(const Segment_t *seg);
/* ARC ML forward declarations */
void BuildArcMLFeatures(const Segment_t *seg, ArcMLFeatures_t *f);
int64_t RunArcModelScaled(const ArcMLFeatures_t *f);
bool ArcMlDetect(const Segment_t *seg, int32_t *scoreOutMilli);
void PrintArcMLSummary(const Segment_t *seg, int32_t logitMilli);
/* --- FLEX THROW HANDS ML FORWARD DECLARATIONS --- */
void BuildFlexThrowHandsMLFeatures(const Segment_t *seg, ArcMLFeatures_t *f);
int64_t RunFlexThrowHandsModelScaled(const ArcMLFeatures_t *f);
bool FlexThrowHandsMlDetect(const Segment_t *seg, int32_t *scoreOutMilli);
/* --- SWAY ML FORWARD DECLARATIONS --- */
void BuildSwayMLFeatures(const Segment_t *seg, ArcMLFeatures_t *f);
int64_t RunSwayModelScaled(const ArcMLFeatures_t *f);
bool SwayMlDetect(const Segment_t *seg, int32_t *scoreOutMilli);
/* --- RIGHT ARM THROW ML FORWARD DECLARATIONS --- */
void BuildRightArmThrowMLFeatures(const Segment_t *seg, ArcMLFeatures_t *f);
int64_t RunRightArmThrowModelScaled(const ArcMLFeatures_t *f);
bool RightArmThrowMlDetect(const Segment_t *seg, int32_t *scoreOutMilli);
/* --- RIGHT ARM TALL SCOOP ML FORWARD DECLARATIONS --- */
void BuildRightArmTallScoopMLFeatures(const Segment_t *seg, ArcMLFeatures_t *f);
int64_t RunRightArmTallScoopModelScaled(const ArcMLFeatures_t *f);
bool RightArmTallScoopMlDetect(const Segment_t *seg, int32_t *scoreOutMilli);

/* --- RIGHT ARM FLEX ML FORWARD DECLARATIONS --- */
void BuildRightArmFlexMLFeatures(const Segment_t *seg, ArcMLFeatures_t *f);
int64_t RunRightArmFlexModelScaled(const ArcMLFeatures_t *f);
bool RightArmFlexMlDetect(const Segment_t *seg, int32_t *scoreOutMilli);
void EEPROM_Init(void);
void EEPROM_WriteSongID(uint8_t songID);
uint8_t EEPROM_ReadSongID(void);

void SelectActiveSong(uint8_t songID);
void LoadSongIDFromEEPROM(void);
bool HandleUARTCommandsAndCheckStart(void);
uint32_t ComputeChoreoPeriodMs(void);

int32_t ScoreSwipe(const Segment_t *seg);

/* Forward declarations needed by StepScore helpers */
uint32_t GetExpectedStepIndexStrict(uint32_t nowMs);
void PrintDetectRow(Move_t expected, int32_t score);
const char* MoveName(Move_t m);
/* Choreo-step cumulative scoring helpers */
typedef struct{
    uint32_t stepIndex;
    Move_t move;
    int32_t cumScore;
    uint8_t sawAnySegment;
} StepScoreAccumulator_t;

void StepScore_Reset(void);
void StepScore_StartStep(uint32_t stepIndex, Move_t move);
void StepScore_AddSegmentScore(uint32_t stepIndex, Move_t move, int32_t segScore);
int32_t MapCumScoreToFinalScore(uint32_t stepIndex, Move_t move, int32_t cumScore);
void StepScore_FlushCurrent(void);
void StepScore_HandleBoundary(uint32_t nowMs);
void EEPROM_Init(void){
    volatile uint32_t delay;

    SYSCTL_RCGCEEPROM_R |= 0x01;
    delay = SYSCTL_RCGCEEPROM_R;
    delay = SYSCTL_RCGCEEPROM_R;
    delay = SYSCTL_RCGCEEPROM_R;
    (void)delay;

    while((SYSCTL_PREEPROM_R & 0x01) == 0){}

    while((EEPROM_EEDONE_R & EEPROM_EEDONE_WORKING) != 0){}

    if((EEPROM_EESUPP_R & (EEPROM_EESUPP_PRETRY | EEPROM_EESUPP_ERETRY)) != 0){
        SYSCTL_SREEPROM_R |= 0x01;
        SYSCTL_SREEPROM_R &= ~0x01;

        while((SYSCTL_PREEPROM_R & 0x01) == 0){}
        while((EEPROM_EEDONE_R & EEPROM_EEDONE_WORKING) != 0){}
    }
}

void EEPROM_WriteSongID(uint8_t songID){
    EEPROM_EEBLOCK_R = SONG_ID_EEPROM_BLOCK;
    EEPROM_EEOFFSET_R = SONG_ID_EEPROM_OFFSET;
    EEPROM_EERDWR_R = (uint32_t)songID;

    while((EEPROM_EEDONE_R & EEPROM_EEDONE_WORKING) != 0){}
}

uint8_t EEPROM_ReadSongID(void){
    EEPROM_EEBLOCK_R = SONG_ID_EEPROM_BLOCK;
    EEPROM_EEOFFSET_R = SONG_ID_EEPROM_OFFSET;
    return (uint8_t)(EEPROM_EERDWR_R & 0xFFU);
}



/* ============================================================
   THE MOVE REGISTRY

   One row per move. Columns:
     move enum | display name | score fn | segment tuning

   Segment tuning fields use SEG_USE_DEFAULT to inherit the
   global default. Only override what you actually need.

   Previously-hardcoded overrides that lived inside
   UpdateGestureEngine() are now here:

     - MOVE_CROSS_ARMS: lower startThresh (it's a slow pose)
     - MOVE_BENT_ARM_SWAY: tighter continue/quiet/max so the
       engine cleanly cuts between rapid back-to-back sways
   ============================================================ */
static const MoveInfo_t gMoveInfo[] = {
	//{ startThresh, continueThresh, quietCountThresh, maxSamplesThresh }
    /* move              name              scoreFunc           { startThresh,   continueThresh, quietCount,      maxSamples      } */
    { MOVE_KICK,         "KICK",                    ScoreKick,                  { SEG_USE_DEFAULT, SEG_USE_DEFAULT, 3,              60              } },
    { MOVE_GANGAM,       "GANGAM",                  ScoreGangam,                { SEG_USE_DEFAULT, SEG_USE_DEFAULT, 2,              20              } },
    { MOVE_SWIPE,        "SWIPE",                   ScoreSwipe,                 DEFAULT_SEGMENT_TUNING },
    { MOVE_COWBOY,       "COWBOY",                  ScoreCowboy,                { SEG_USE_DEFAULT, SEG_USE_DEFAULT, 2,              20              } },
    { MOVE_ENERGY,       "ENERGY",                  ScoreEnergy,                { SEG_USE_DEFAULT, SEG_USE_DEFAULT, 2,              20              } },
    { MOVE_HYPE,         "HYPE",                    ScoreHype,                  { SEG_USE_DEFAULT, SEG_USE_DEFAULT, 2,              20              } },
		 { MOVE_PUMP_DOWN,    "PUMP DOWN",      ScorePumpDown,      DEFAULT_SEGMENT_TUNING },
    { MOVE_ARC,          "ARC",            ScoreArc,           DEFAULT_SEGMENT_TUNING },
    { MOVE_CROSS_ARMS,   "CROSS ARMS",     ScoreCrossArms,     { 900,           SEG_USE_DEFAULT, SEG_USE_DEFAULT, SEG_USE_DEFAULT } },
    { MOVE_BENT_ARM_SWAY,"BENT ARM SWAY",  ScoreBentArmSway,   { SEG_USE_DEFAULT, 3800,          3,               60              } },

    { MOVE_LEFT_ARM_WAVE, "LEFT ARM WAVE", ScoreLeftArmWave, { 500, 400, 4, 80 } },
    { MOVE_RIGHT_ARM_WAVE,           "RIGHT ARM WAVE",           ScoreRightArmWave,           { 500, 400, 4, 80 } },
    { MOVE_WAVE,                     "WAVE",                     ScoreWave,                   DEFAULT_SEGMENT_TUNING },
    { MOVE_HOP,                      "HOP",                      ScoreHop,                    DEFAULT_SEGMENT_TUNING },
    { MOVE_RIGHT_ARM_COME_HERE_SCOOP,"RIGHT ARM COME HERE SCOOP",ScoreRightArmComeHereScoop,  DEFAULT_SEGMENT_TUNING },
    { MOVE_RIGHT_ARM_TALL_SCOOP,     "RIGHT ARM TALL SCOOP",     ScoreRightArmTallScoop,      { SEG_USE_DEFAULT, 0, 255, 76 } },
    { MOVE_SPIN_JUMP,                "SPIN JUMP",                ScoreSpinJump,               { 500, 400, 4, 80 } },
    { MOVE_RIGHT_ARM_THROW,          "RIGHT ARM THROW",          ScoreRightArmThrow,          { SEG_USE_DEFAULT, 0, 255, 40 } },
    { MOVE_LEFT_ARM_THROW,           "LEFT ARM THROW",           ScoreLeftArmThrow,           DEFAULT_SEGMENT_TUNING },
    { MOVE_FLEX_THROW_HANDS,         "FLEX THROW HANDS",         ScoreFlexThrowHands,         { SEG_USE_DEFAULT, 0, 255, 40 } },
    { MOVE_RIGHT_ARM_PUNCH,          "RIGHT ARM PUNCH",          ScoreRightArmPunch,          DEFAULT_SEGMENT_TUNING },
    { MOVE_RIGHT_ARM_FLEX,           "RIGHT ARM FLEX",           ScoreRightArmFlex,           { SEG_USE_DEFAULT, SEG_USE_DEFAULT, 255, 62} },
    { MOVE_WINDING_ARMS, "WINDING ARMS", ScoreWindingArms, { SEG_USE_DEFAULT, SEG_USE_DEFAULT, 3, 20 } },
    { MOVE_ARM_SLOW_UP_TO_DOWN,      "ARM SLOW UP TO DOWN",      ScoreArmSlowUpToDown,        DEFAULT_SEGMENT_TUNING },
		    { MOVE_RIGHT_ARM_BEAT, "RIGHT ARM BEAT", ScoreRightArmBeat,
    { SEG_USE_DEFAULT, SEG_USE_DEFAULT, SEG_USE_DEFAULT, 30 } },
    { MOVE_RIGHT_ARM_HIGH_LOW,    "RIGHT ARM HIGH LOW",     ScoreRightArmHighLow,     DEFAULT_SEGMENT_TUNING },
    { MOVE_RIGHT_ARM_LOW_HIGH,    "RIGHT ARM LOW HIGH",     ScoreRightArmLowHigh,     DEFAULT_SEGMENT_TUNING },
    { MOVE_LEFT_ARM_LOW_HIGH,     "LEFT ARM LOW HIGH",      ScoreLeftArmLowHigh,      DEFAULT_SEGMENT_TUNING },
    { MOVE_PUNCH,                 "PUNCH",                  ScorePunch,               DEFAULT_SEGMENT_TUNING },
    { MOVE_PUNCH_UP,              "PUNCH UP",               ScorePunchUp,             DEFAULT_SEGMENT_TUNING },
    { MOVE_RIGHT_ARM,             "RIGHT ARM",              ScoreRightArm,            DEFAULT_SEGMENT_TUNING },
    { MOVE_SQUIGGLE,              "SQUIGGLE",               ScoreSquiggle,            DEFAULT_SEGMENT_TUNING },
    { MOVE_UP,                    "UP",                     ScoreUp,                  DEFAULT_SEGMENT_TUNING },
    { MOVE_ARMS_SCOOP_DOWN,       "ARMS SCOOP DOWN",        ScoreArmsScoopDown,       DEFAULT_SEGMENT_TUNING },
    { MOVE_RIGHT_JABS,            "RIGHT JABS",             ScoreRightJabs,           DEFAULT_SEGMENT_TUNING },
    { MOVE_LEFT_JABS,             "LEFT JABS",              ScoreLeftJabs,            DEFAULT_SEGMENT_TUNING },
    { MOVE_POINT_LEFT_TO_RIGHT,   "POINT LEFT TO RIGHT",    ScorePointLeftToRight,    DEFAULT_SEGMENT_TUNING },
    { MOVE_ARM_DOWN_POINT,        "ARM DOWN POINT",         ScoreArmDownPoint,        DEFAULT_SEGMENT_TUNING },
};

#define MOVE_INFO_COUNT (sizeof(gMoveInfo) / sizeof(gMoveInfo[0]))

/* ============================================================
   THE CHOREOGRAPHY

   Ordered list of (move, duration_ms). Total period is summed
   at startup into gChoreoPeriodMs -- no #define to update.
   ============================================================ */
static const ChoreoStep_t gChoreoSong1[] = {
    // Pre-sway section (ends at boot-time 30.3s)
		{ MOVE_WAIT,           11300U },// JAY IS RED IS THIS
  //  { MOVE_WAIT,           11300U },   //espB is blue is 12300 less is espA red is 11300
		
    { MOVE_PUMP_DOWN,       3900U },
    { MOVE_ARC,             1100U },
    { MOVE_CROSS_ARMS,      1600U },
    { MOVE_PUMP_DOWN,       4650U },
    { MOVE_ARC,             1050U },
    { MOVE_CROSS_ARMS,      2500U },

    // Sway block 1 
    { MOVE_BENT_ARM_SWAY,    800U },
    { MOVE_BENT_ARM_SWAY,    900U },
    { MOVE_BENT_ARM_SWAY,    900U },
    { MOVE_BENT_ARM_SWAY,    900U },
    { MOVE_BENT_ARM_SWAY,    900U },
    { MOVE_BENT_ARM_SWAY,   1000U },

    // Kick 1
    { MOVE_KICK,            1900U },

    // Sway block 2
    { MOVE_BENT_ARM_SWAY,    800U },
    { MOVE_BENT_ARM_SWAY,    900U },
    { MOVE_BENT_ARM_SWAY,    900U },
    { MOVE_BENT_ARM_SWAY,    900U },
    { MOVE_BENT_ARM_SWAY,    900U },
    { MOVE_BENT_ARM_SWAY,   1000U },

//    // Kick 2 
//    { MOVE_KICK,            1700U },
//		
//												//{ MOVE_WAIT,           14400U },
//		{ MOVE_WAIT,            4200U },   // 0:44.7 -> 0:48.9
//		{ MOVE_ENERGY,          1100U },   // 0:48.9 -> 0:50.0
//		{ MOVE_WAIT,            6100U },   // 0:50.0 -> 0:56.1
//		{ MOVE_ENERGY,          1260U },   // 0:56.1 -> 0:57.36
//		{ MOVE_WAIT,            1740U },   // 0:57.36 -> 0:59.1
		
		// Kick 2 
    { MOVE_KICK,            1700U },
		{ MOVE_WAIT,            4200U },   // 0:44.7 -> 0:48.9
		{ MOVE_WAIT,          1100U },   // 0:48.9 -> 0:50.0
		{ MOVE_ENERGY,            6100U },   // 0:50.0 -> 0:56.1
		{ MOVE_WAIT,          1260U },   // 0:56.1 -> 0:57.36
		{ MOVE_ENERGY,            1740U },   // 0:57.36 -> 0:59.1


		{ MOVE_KICK,             400U },   // 0:59.1  -> 0:59.5
		{ MOVE_HYPE,            4880U },   // +380 ms
		{ MOVE_KICK,            1500U },
		{ MOVE_WAIT,            2120U },   // -380 ms
    { MOVE_GANGAM,          3800U },
		{ MOVE_COWBOY,          3400U },
		{ MOVE_GANGAM,          3900U },
		{ MOVE_COWBOY,          3500U },
		{ MOVE_WAIT,             300U },
		{ MOVE_KICK,            3500U },
		{ MOVE_GANGAM,          3600U },
		{ MOVE_KICK,            3480U },
		{ MOVE_COWBOY,          2120U }
};


#define CHOREO_SONG1_STEP_COUNT (sizeof(gChoreoSong1) / sizeof(gChoreoSong1[0]))
	
static const ChoreoStep_t gChoreoSong2[] = {
		{ MOVE_WAIT,                     11200U },//JAY IS RED IS THIS
    //{ MOVE_WAIT,                     12200U },  //JOSH IS BLUE
    { MOVE_LEFT_ARM_WAVE,              990U },
    { MOVE_WAIT,                       550U },
    { MOVE_RIGHT_ARM_WAVE,            1020U },
    { MOVE_WAIT,                       940U },
    { MOVE_LEFT_ARM_WAVE,             1090U },
    { MOVE_WAVE,                       790U },
    { MOVE_RIGHT_ARM_WAVE,             710U },
    { MOVE_WAIT,                      1160U },
    { MOVE_LEFT_ARM_WAVE,              600U },
    { MOVE_WAIT,                      1320U },
    { MOVE_RIGHT_ARM_WAVE,            1290U },
    { MOVE_WAIT,                       720U },
    { MOVE_LEFT_ARM_WAVE,             1060U },

    /* updated section */
    { MOVE_WAIT,                       190U },   
    { MOVE_HOP,                        700U },   
    { MOVE_WAIT,                      3020U },   
    { MOVE_RIGHT_ARM_COME_HERE_SCOOP, 1260U },   
    { MOVE_WAIT,                      6500U },   

    { MOVE_RIGHT_ARM_TALL_SCOOP,       760U }, /* Was 570 (+190) */
    { MOVE_WAIT,                       760U }, /* Was 950 (-190) */
    { MOVE_RIGHT_ARM_TALL_SCOOP,       760U }, /* Was 900 (-140) */
    { MOVE_WAIT,                      1320U }, /* Was 1180 (+140) */
    { MOVE_RIGHT_ARM_TALL_SCOOP,       760U }, /* Was 740 (+20) */
    { MOVE_WAIT,                       800U }, /* Was 820 (-20) */
    { MOVE_SPIN_JUMP,                 1980U },
    { MOVE_WAIT,                       420U },
    { MOVE_RIGHT_ARM_TALL_SCOOP,       760U }, /* Was 460 (+300) */
    { MOVE_WAIT,                       730U }, /* Was 1030 (-300) */
    { MOVE_RIGHT_ARM_TALL_SCOOP,       760U }, /* Was 780 (-20) */
    { MOVE_WAIT,                      1010U }, /* Was 990 (+20) */
    { MOVE_RIGHT_ARM_TALL_SCOOP,       760U }, /* Was 820 (-60) */
    { MOVE_WAIT,                      1020U }, /* Was 960 (+60) */
    { MOVE_SPIN_JUMP,                 1640U },
    { MOVE_WAIT,                       320U },
    { MOVE_RIGHT_ARM_THROW,            320U },
    { MOVE_WAIT,                       200U },
    { MOVE_LEFT_ARM_THROW,             280U },

    { MOVE_WAIT,                      2340U },
    { MOVE_FLEX_THROW_HANDS,           370U },

    { MOVE_WAIT,                       280U },
    { MOVE_RIGHT_ARM_THROW,            310U },
    { MOVE_WAIT,                       170U },
    { MOVE_LEFT_ARM_THROW,             200U },

    { MOVE_WAIT,                       540U },
    { MOVE_FLEX_THROW_HANDS,           350U },

    { MOVE_WAIT,                       300U },
    { MOVE_RIGHT_ARM_THROW,            450U },
    //{ MOVE_WAIT,                       220U },
    { MOVE_LEFT_ARM_THROW,             700U },

   // { MOVE_WAIT,                       440U },
    { MOVE_FLEX_THROW_HANDS,           670U },

   // { MOVE_WAIT,                       250U },
    { MOVE_RIGHT_ARM_THROW,            460U },
  //  { MOVE_WAIT,                       220U },
    { MOVE_LEFT_ARM_THROW,             720U },

   // { MOVE_WAIT,                       510U },
    { MOVE_FLEX_THROW_HANDS,           670U },

   // { MOVE_WAIT,                       190U },
    { MOVE_RIGHT_ARM_THROW,            470U },
    //{ MOVE_WAIT,                       220U },
    { MOVE_LEFT_ARM_THROW,             680U },

   // { MOVE_WAIT,                       480U },
    { MOVE_FLEX_THROW_HANDS,           690U },

  //  { MOVE_WAIT,                       280U },
    { MOVE_RIGHT_ARM_THROW,            500U },
   // { MOVE_WAIT,                       250U },
    { MOVE_LEFT_ARM_THROW,             670U },

   // { MOVE_WAIT,                       520U },
    { MOVE_FLEX_THROW_HANDS,           650U },

     { MOVE_HOP,                       1960U },  // 1100 + 860
    { MOVE_RIGHT_ARM_PUNCH,            700U },  // 190 + 510
    { MOVE_RIGHT_ARM_FLEX,             900U },  // 310 + 590
    { MOVE_WINDING_ARMS,              1960U },  // 1630 + 330
    { MOVE_RIGHT_ARM_THROW,            820U },  // 340 + 480
    { MOVE_RIGHT_ARM_FLEX,             810U },  // 310 + 500
    { MOVE_WINDING_ARMS,              2090U },  // 1530 + 560
    { MOVE_RIGHT_ARM_THROW,            840U },  // 290 + 550
    { MOVE_RIGHT_ARM_FLEX,             730U },  // 310 + 420
    { MOVE_WINDING_ARMS,              2050U },  // 1720 + 330
    { MOVE_RIGHT_ARM_THROW,            830U },  // 380 + 450
    { MOVE_RIGHT_ARM_FLEX,            1310U },  // 310 + 1000
    { MOVE_ARM_SLOW_UP_TO_DOWN,       1460U }
};




#define CHOREO_SONG2_STEP_COUNT (sizeof(gChoreoSong2) / sizeof(gChoreoSong2[0]))
	
static const ChoreoStep_t gChoreoSong3[] = {
    { MOVE_WAIT,                   6647U },

    { MOVE_RIGHT_ARM_BEAT,          167U }, // 11.647 - 11.814
    { MOVE_WAIT,                    270U },
    { MOVE_RIGHT_ARM_BEAT,          200U }, // 12.084 - 12.284
    { MOVE_WAIT,                    305U },
    { MOVE_RIGHT_ARM_BEAT,          133U }, // 12.589 - 12.722
    { MOVE_WAIT,                    317U },
    { MOVE_RIGHT_ARM_BEAT,          166U }, // 13.039 - 13.205
    { MOVE_WAIT,                    317U },
    { MOVE_RIGHT_ARM_BEAT,          158U }, // 13.522 - 13.680
    { MOVE_WAIT,                    334U },
    { MOVE_RIGHT_ARM_BEAT,          133U }, // 14.014 - 14.147
    { MOVE_WAIT,                    317U },
    { MOVE_RIGHT_ARM_BEAT,          133U }, // 14.464 - 14.597
    { MOVE_WAIT,                    322U },
    { MOVE_RIGHT_ARM_BEAT,          150U }, // 14.919 - 15.069
    { MOVE_WAIT,                    300U },
    { MOVE_RIGHT_ARM_BEAT,          133U }, // 15.369 - 15.502
    { MOVE_WAIT,                    350U },
    { MOVE_RIGHT_ARM_BEAT,          133U }, // 15.852 - 15.985
    { MOVE_WAIT,                    334U },
    { MOVE_RIGHT_ARM_BEAT,          133U }, // 16.319 - 16.452
    { MOVE_WAIT,                    350U },
    { MOVE_RIGHT_ARM_BEAT,          133U }, // 16.802 - 16.935
    { MOVE_WAIT,                    334U },
    { MOVE_RIGHT_ARM_BEAT,          133U }, // 17.269 - 17.402
    { MOVE_WAIT,                    333U },
    { MOVE_RIGHT_ARM_BEAT,          150U }, // 17.735 - 17.885
    { MOVE_WAIT,                    267U },
    { MOVE_RIGHT_ARM_BEAT,          192U }, // 18.152 - 18.344
    { MOVE_WAIT,                    300U },
    { MOVE_RIGHT_ARM_BEAT,          150U }, // 18.644 - 18.794
    { MOVE_WAIT,                    436U },

    { MOVE_RIGHT_ARM_HIGH_LOW,      470U }, // 19.23 - 19.7
    { MOVE_WAIT,                    500U },
    { MOVE_RIGHT_ARM_HIGH_LOW,      350U }, // 20.2 - 20.55
    { MOVE_WAIT,                    650U },

    { MOVE_RIGHT_ARM_LOW_HIGH,      300U }, // 21.20 - 21.5
    { MOVE_WAIT,                    480U },

    { MOVE_LEFT_ARM_LOW_HIGH,       460U }, // 21.98 - 22.44
    { MOVE_WAIT,                    480U },

    { MOVE_RIGHT_ARM_LOW_HIGH,      440U }, // 22.92 - 23.36
    { MOVE_WAIT,                    510U },

    { MOVE_RIGHT_ARM_LOW_HIGH,      530U }, // 23.87 - 24.4
    { MOVE_WAIT,                    600U },

    { MOVE_PUNCH,                   900U }, // 25.00 - 25.9
    { MOVE_PUNCH_UP,                850U }, // 25.9 - 26.75
    { MOVE_RIGHT_ARM_LOW_HIGH,      350U }, // 26.75 - 27.1
    { MOVE_WAIT,                    600U },

    { MOVE_RIGHT_ARM_LOW_HIGH,      350U }, // 27.7 - 28.05
    { MOVE_WAIT,                    550U },

    { MOVE_LEFT_ARM_LOW_HIGH,       400U }, // 28.6 - 29
    { MOVE_WAIT,                    600U },

    { MOVE_LEFT_ARM_LOW_HIGH,       300U }, // 29.6 - 29.9
    { MOVE_WAIT,                    600U },

    { MOVE_RIGHT_ARM,               370U }, // 30.5 - 30.87
    { MOVE_WAIT,                    530U },

    { MOVE_RIGHT_ARM,               470U }, // 31.4 - 31.87
    { MOVE_WAIT,                    430U },

    { MOVE_PUNCH,                  1200U }, // 32.3 - 33.5
    { MOVE_PUNCH_UP,                580U }, // 33.5 - 34.08
    { MOVE_WAIT,                   1820U },

    { MOVE_SQUIGGLE,               1900U }, // 35.9 - 37.8
    { MOVE_WAIT,                   1900U },

    { MOVE_SQUIGGLE,               1900U }, // 39.7 - 41.6
    { MOVE_WAIT,                   1700U },

    { MOVE_SQUIGGLE,               1900U }, // 43.3 - 45.2
    { MOVE_WAIT,                   4100U },

    { MOVE_UP,                     3000U }, // 49.3 - 52.3
    { MOVE_WAIT,                    500U },

    { MOVE_ENERGY,                 3600U }, // 52.8 - 56.4
    { MOVE_WAIT,                    440U },

    { MOVE_ARMS_SCOOP_DOWN,         560U }, // 56.84 - 57.4
    { MOVE_WAIT,                   1470U },

    { MOVE_ARMS_SCOOP_DOWN,         530U }, // 58.87 - 59.4
    { MOVE_WAIT,                   1300U },

    { MOVE_ARMS_SCOOP_DOWN,         540U }, // 1:00.7 - 1:01.24
    { MOVE_WAIT,                   1392U },

    { MOVE_ARMS_SCOOP_DOWN,         350U }, // 1:02.632 - 1:02.982
    { MOVE_WAIT,                   1148U },

    { MOVE_RIGHT_JABS,             1655U }, // 1:04.130 - 1:05.785
    { MOVE_WAIT,                    377U },

    { MOVE_LEFT_JABS,              1324U }, // 1:06.162 - 1:07.486
    { MOVE_WAIT,                    333U },

    { MOVE_POINT_LEFT_TO_RIGHT,    1457U }, // 1:07.819 - 1:09.276
    { MOVE_WAIT,                    466U },

    { MOVE_ARM_DOWN_POINT,         1292U }, // 1:09.742 - 1:11.034
    { MOVE_WAIT,                    624U },

    { MOVE_RIGHT_JABS,             1515U }, // 1:11.658 - 1:13.173
    { MOVE_WAIT,                    434U },

    { MOVE_LEFT_JABS,              1335U }, // 1:13.607 - 1:14.942
    { MOVE_WAIT,                    365U },

    { MOVE_POINT_LEFT_TO_RIGHT,    1471U }, // 1:15.307 - 1:16.778
    { MOVE_WAIT,                    429U },

    { MOVE_ARM_DOWN_POINT,         1043U }  // 1:17.207 - 1:18.250
};
#define CHOREO_SONG3_STEP_COUNT (sizeof(gChoreoSong3) / sizeof(gChoreoSong3[0]))
///REPLACE THIS 	CHATGPT IF YOU SEE THIS REMIND ME

void SelectActiveSong(uint8_t songID){
    if(songID == 2U){
        activeSongID = 2U;
        gActiveChoreo = gChoreoSong3;
        gActiveChoreoCount = CHOREO_SONG3_STEP_COUNT;
    }else if(songID == 1U){
        activeSongID = 1U;
        gActiveChoreo = gChoreoSong2;
        gActiveChoreoCount = CHOREO_SONG2_STEP_COUNT;
    }else{
        activeSongID = 0U;
        gActiveChoreo = gChoreoSong1;
        gActiveChoreoCount = CHOREO_SONG1_STEP_COUNT;
    }
}

/* choose song 1
void SelectActiveSong(uint8_t songID){
    (void)songID;
    activeSongID = 1U;
    gActiveChoreo = gChoreoSong1;
    gActiveChoreoCount = CHOREO_SONG1_STEP_COUNT;
}
*/
/*
 //chose song 2
void SelectActiveSong(uint8_t songID){ 
    (void)songID;
    activeSongID = 1U;
    gActiveChoreo = gChoreoSong2;
    gActiveChoreoCount = CHOREO_SONG2_STEP_COUNT;
}
*/
void LoadSongIDFromEEPROM(void){
    uint8_t savedSongID = EEPROM_ReadSongID();

    if((savedSongID != 0U) && (savedSongID != 1U) && (savedSongID != 2U)){
        savedSongID = SONG_ID_DEFAULT;
    }

    SelectActiveSong(savedSongID);
}

bool HandleUARTCommandsAndCheckStart(void){
    bool startRequested = false;

    while((UART5_FR_R & 0x10) == 0){
        char incoming = (char)(UART5_DR_R & 0xFF);

        if((incoming == '0') && (activeSongID != 0U)){
            SelectActiveSong(0U);
            EEPROM_WriteSongID(0U);
            gChoreoPeriodMs = ComputeChoreoPeriodMs();
            UART0_OutString("[SONG] saved SONG 1 for next run\r\n");
        }else if((incoming == '1') && (activeSongID != 1U)){
            SelectActiveSong(1U);
            EEPROM_WriteSongID(1U);
            gChoreoPeriodMs = ComputeChoreoPeriodMs();
            UART0_OutString("[SONG] saved SONG 2 for next run\r\n");
        }else if((incoming == '2') && (activeSongID != 2U)){
            SelectActiveSong(2U);
            EEPROM_WriteSongID(2U);
            gChoreoPeriodMs = ComputeChoreoPeriodMs();
            UART0_OutString("[SONG] saved SONG 3 for next run\r\n");
        }else if((incoming == 's') || (incoming == 'S')){
            startRequested = true;
        }
    }

    return startRequested;
}
/*
bool HandleUARTCommandsAndCheckStart(void){
    return true;
}
*/
/* ============================================================
   Globals for filters / segment engine
   ============================================================ */
static int32_t gAxFilt = 0;
static int32_t gAyFilt = 0;
static int32_t gAzFilt = 0;

static int32_t gGxFilt = 0;
static int32_t gGyFilt = 0;
static int32_t gGzFilt = 0;

static int32_t gAxBase = 0;
static int32_t gAyBase = 0;
static int32_t gAzBase = 0;

static uint32_t gStillCount = 0;

static SegmentState_t gSegState = SEG_IDLE;
static Segment_t gCurSeg;
static uint32_t gQuietCount = 0;
static uint32_t gCooldownStartMs = 0;

static Move_t gSegExpectedAtStart = MOVE_NONE;

static int8_t gLastSignX = 0;
static int8_t gLastSignY = 0;
static int8_t gLastSignZ = 0;

static int32_t gPrevPoseX = 0;
static int32_t gPrevPoseZ = 0;
static int8_t gLastPoseSlopeX = 0;
static int8_t gLastPoseSlopeZ = 0;

static uint32_t gLastPrintedStepIndex = 0xFFFFFFFFU;
static StepScoreAccumulator_t gStepScore = { 0xFFFFFFFFU, MOVE_NONE, 0, 0 };
void StepScore_Reset(void){
    gStepScore.stepIndex = 0xFFFFFFFFU;
    gStepScore.move = MOVE_NONE;
    gStepScore.cumScore = 0;
    gStepScore.sawAnySegment = 0;
}

void StepScore_StartStep(uint32_t stepIndex, Move_t move){
    gStepScore.stepIndex = stepIndex;
    gStepScore.move = move;
    gStepScore.cumScore = 0;
    gStepScore.sawAnySegment = 0;
}

void StepScore_AddSegmentScore(uint32_t stepIndex, Move_t move, int32_t segScore){
    if(gStepScore.stepIndex != stepIndex || gStepScore.move != move){
        StepScore_StartStep(stepIndex, move);
    }

#if PRINT_SEGMENT_SCORES
    if(!TEST_MODE_ENABLE){   // keep it clean for choreo only
        UART0_OutString("[SEG_SCORE] ");
        UART0_OutString(MoveName(move));
        UART0_OutString(" = ");
        UART0_OutSDec(segScore);
        UART0_OutString("\r\n");
    }
#endif

    gStepScore.cumScore += segScore;
    gStepScore.sawAnySegment = 1;
}

/*
  Per-step final mapping goes here.
  This is where duplicate moves can behave differently because
  we switch on stepIndex first, not just move.
*/
int32_t MapCumScoreToFinalScore(uint32_t stepIndex, Move_t move, int32_t cumScore){
    /*
      Return meaning:
        >= 0  -> print DETECT,<move>,<score>
        -1    -> do not print anything
    */

    /* WAIT: never print */
    if(move == MOVE_WAIT){
        return -1;
    }

    /* ARC: any positive cumulative ML evidence -> 100 */
    if(move == MOVE_ARC){
        if(cumScore > 0){
            return 100;
        }else{
            return -1;
        }
    }

    /* PUMP DOWN */
    if(move == MOVE_PUMP_DOWN){
        if(cumScore > 280){
            return 100;
        }else if(cumScore > 210){
            return 70;
        }else{
            return -1;
        }
    }

    /* CROSS ARMS */
    if(move == MOVE_CROSS_ARMS){
        if(cumScore > 480){
            return 100;
        }else if(cumScore > 160){
            return 70;
        }else{
            return -1;
        }
    }

    /* BENT ARM SWAY */
    if(move == MOVE_BENT_ARM_SWAY){
        if(cumScore > 100){
            return 30;
        }else{
            return 10;
        }
    }

    /* RIGHT ARM WAVE */
    if(move == MOVE_RIGHT_ARM_WAVE){
        if(cumScore >= 150){
            return 100;
        }else if(cumScore >= 100){
            return 70;
        }else{
            return 0;
        }
    }

    /* WINDING ARMS */
    if(move == MOVE_WINDING_ARMS){
        if(cumScore >= 600){
            return 100;
        }else if(cumScore >= 500){
            return 70;
        }else{
            return 0;
        }
    }

    /* SPIN JUMP */
    if(move == MOVE_SPIN_JUMP){
        if(cumScore >= 390){
            return 100;
        }else if(cumScore >= 290){
            return 70;
        }else{
            return 0;
        }
    }

    /* RIGHT ARM TALL SCOOP */
    if(move == MOVE_RIGHT_ARM_TALL_SCOOP){
        if(cumScore >= 200){
            return 150;
        }else if(cumScore >= 100){
            return 100;
        }else{
            return 0;
        }
    }

    /* Moves with 100 / 70 / 0 mapping at 100 and 70 thresholds */
    if(move == MOVE_RIGHT_ARM_COME_HERE_SCOOP ||
       move == MOVE_HOP ||
       move == MOVE_RIGHT_ARM_THROW ||
       move == MOVE_FLEX_THROW_HANDS ||
       move == MOVE_RIGHT_ARM_FLEX){
        if(cumScore >= 100){
            return 100;
        }else if(cumScore >= 70){
            return 70;
        }else{
            return 0;
        }
    }

    /*
      Separate duplicate choreography occurrences by stepIndex
      gChoreoSong1 indices:
        28 = GANGAM 1
        29 = COWBOY 1
        30 = GANGAM 2
        31 = COWBOY 2
        34 = GANGAM 3
        36 = COWBOY 3
    */
    switch(stepIndex){
        case 28:   /* GANGAM 1 */
            if(cumScore > 1700){
                return 100;
            }else if(cumScore > 1500){
                return 70;
            }else{
                return -1;
            }

        case 30:   /* GANGAM 2 */
            if(cumScore > 1700){
                return 100;
            }else if(cumScore > 1500){
                return 70;
            }else{
                return -1;
            }

        case 34:   /* GANGAM 3 */
            if(cumScore > 1500){
                return 100;
            }else if(cumScore > 1300){
                return 70;
            }else{
                return -1;
            }

        case 29:   /* COWBOY 1 */
            if(cumScore > 975){
                return 100;
            }else if(cumScore > 845){
                return 70;
            }else{
                return -1;
            }

        case 31:   /* COWBOY 2 */
            if(cumScore > 975){
                return 100;
            }else if(cumScore > 845){
                return 70;
            }else{
                return -1;
            }

        case 36:   /* COWBOY 3 */
            if(cumScore > 650){
                return 100;
            }else if(cumScore > 520){
                return 70;
            }else{
                return -1;
            }

        default:
            if(cumScore > 99){
                return 100;
            }else{
                return -1;
            }
    }
}
void StepScore_FlushCurrent(void){
    int32_t finalScore;

    if(TEST_MODE_ENABLE){
        return;
    }

    if(gStepScore.stepIndex == 0xFFFFFFFFU){
        return;
    }

    if(!gStepScore.sawAnySegment){
        StepScore_Reset();
        return;
    }

    finalScore = MapCumScoreToFinalScore(gStepScore.stepIndex,
                                         gStepScore.move,
                                         gStepScore.cumScore);

    PrintDetectRow(gStepScore.move, finalScore);
    StepScore_Reset();
}

void StepScore_HandleBoundary(uint32_t nowMs){
    uint32_t stepIndex;
    Move_t move;

    if(TEST_MODE_ENABLE){
        return;
    }

    if(!TimeReachedMs(nowMs, gDanceStartMs)){
        return;
    }

    stepIndex = GetExpectedStepIndexStrict(nowMs);
    move = gActiveChoreo[stepIndex].move;

    /*
      First entry into runtime after calibration:
      initialize accumulator to current step, do not flush.
    */
    if(gStepScore.stepIndex == 0xFFFFFFFFU){
        StepScore_StartStep(stepIndex, move);
        return;
    }

    /*
      When choreography advances to a new step:
      flush the prior step once, then begin the new step.
    */
    if(stepIndex != gStepScore.stepIndex){
        StepScore_FlushCurrent();
        StepScore_StartStep(stepIndex, move);
    }
}
/* ============================================================
   Utility
   ============================================================ */
int8_t SignFromThreshold(int32_t v, int32_t thresh){
    if(v > thresh) return 1;
    if(v < -thresh) return -1;
    return 0;
}

/* ============================================================
   Move registry lookups
   ============================================================ */
const MoveInfo_t* GetMoveInfo(Move_t m){
    uint32_t i;

    for(i = 0; i < MOVE_INFO_COUNT; i++){
        if(gMoveInfo[i].move == m){
            return &gMoveInfo[i];
        }
    }

    return (const MoveInfo_t*)0;
}

const char* MoveName(Move_t m){
    const MoveInfo_t *info = GetMoveInfo(m);
    if(info != 0){
        return info->name;
    }
    return "NONE";
}

/* ============================================================
   Resolve per-move segment tuning.

   For the current expected move, fill out all four tuning
   fields, falling back to the global defaults for any field
   left as SEG_USE_DEFAULT. If the move isn't in the registry
   (shouldn't happen), use all defaults.
   ============================================================ */
void ResolveSegmentTuning(Move_t expected,
                          int32_t *startThresh,
                          int32_t *continueThresh,
                          uint32_t *quietCountThresh,
                          uint32_t *maxSamplesThresh){
    const MoveInfo_t *info = GetMoveInfo(expected);

    /* Start with global defaults */
    *startThresh      = START_ENERGY_THRESH;
    *continueThresh   = CONTINUE_ENERGY_THRESH;
    *quietCountThresh = QUIET_END_COUNT;
    *maxSamplesThresh = SEGMENT_MAX_SAMPLES;

    if(info == 0){
        return;
    }

    if(info->tuning.startThresh      != SEG_USE_DEFAULT) *startThresh      = info->tuning.startThresh;
    if(info->tuning.continueThresh   != SEG_USE_DEFAULT) *continueThresh   = info->tuning.continueThresh;
    if(info->tuning.quietCountThresh != SEG_USE_DEFAULT) *quietCountThresh = (uint32_t)info->tuning.quietCountThresh;
    if(info->tuning.maxSamplesThresh != SEG_USE_DEFAULT) *maxSamplesThresh = (uint32_t)info->tuning.maxSamplesThresh;
}

/* ============================================================
   Choreography helpers
   ============================================================ */
uint32_t ComputeChoreoPeriodMs(void){
    uint32_t i;
    uint32_t sum = 0;

    for(i = 0; i < gActiveChoreoCount; i++){
        sum += gActiveChoreo[i].durationMs;
    }
    return sum;
}

uint32_t GetExpectedStepIndexStrict(uint32_t nowMs){
    uint32_t rel;
    uint32_t i;
    uint32_t accum = 0;

    rel = (nowMs - gDanceStartMs) % gChoreoPeriodMs;

    for(i = 0; i < gActiveChoreoCount; i++){
        accum += gActiveChoreo[i].durationMs;
        if(rel < accum){
            return i;
        }
    }

    return (gActiveChoreoCount - 1U);
}

Move_t GetExpectedMoveStrict(uint32_t nowMs){
    return gActiveChoreo[GetExpectedStepIndexStrict(nowMs)].move;
}

Move_t GetExpectedMoveForRuntime(uint32_t nowMs){
    if(TEST_MODE_ENABLE){
        (void)nowMs;
        return TEST_MODE_MOVE;
    }
    return GetExpectedMoveStrict(nowMs);
}

bool InBoundaryGrace(uint32_t nowMs){
    uint32_t rel;
    uint32_t accum;
    uint32_t i;

    rel = (nowMs - gDanceStartMs) % gChoreoPeriodMs;

    if((rel < WINDOW_GRACE_MS) || (rel > (gChoreoPeriodMs - WINDOW_GRACE_MS))){
        return true;
    }

    accum = 0;
    for(i = 0; i < gActiveChoreoCount; i++){
        accum += gActiveChoreo[i].durationMs;
        if(accum >= gChoreoPeriodMs){
            break;
        }
        if(Abs32((int32_t)rel - (int32_t)accum) < (int32_t)WINDOW_GRACE_MS){
            return true;
        }
    }

    return false;
}

Move_t GetPrevStepMove(uint32_t stepIndex){
    if(stepIndex == 0U){
        return gActiveChoreo[gActiveChoreoCount - 1U].move;
    }
    return gActiveChoreo[stepIndex - 1U].move;
}

Move_t GetNextStepMove(uint32_t stepIndex){
    if(stepIndex >= (gActiveChoreoCount - 1U)){
        return gActiveChoreo[0].move;
    }
    return gActiveChoreo[stepIndex + 1U].move;
}

/* ============================================================
   Printing helpers
   ============================================================ */



/* ============================================================
   Emit a SEG,<move>,<32 features> line for espA to parse.
   espA expects EXACTLY 32 comma-separated int32 values after the name,
   terminated with \n (readStringUntil handles \r\n fine via trim()).
   ============================================================ */
void PrintSegmentRow(const Segment_t *seg, Move_t expected){
    ArcDerived_t d;
    BuildArcDerived(seg, &d);

    UART5_OutString("SEG,");
    UART5_OutString(MoveName(expected)); UART5_OutChar(',');

    UART5_OutSDec(d.durationMs); UART5_OutChar(',');
    UART5_OutSDec(d.samples); UART5_OutChar(',');
    UART5_OutSDec(d.netRiseZ); UART5_OutChar(',');
    UART5_OutSDec(d.totalRiseZ); UART5_OutChar(',');
    UART5_OutSDec(d.endHighZ); UART5_OutChar(',');
    UART5_OutSDec(d.startLowZ); UART5_OutChar(',');
    UART5_OutSDec(d.crossBodyX); UART5_OutChar(',');
    UART5_OutSDec(d.smallDipY); UART5_OutChar(',');
    UART5_OutSDec(d.finishDropFromPeak); UART5_OutChar(',');
    UART5_OutSDec(d.oscillationCount); UART5_OutChar(',');
    UART5_OutSDec(d.slopeFlipCount); UART5_OutChar(',');
    UART5_OutSDec(d.totalAbsAcc); UART5_OutChar(',');
    UART5_OutSDec(d.totalAbsGyr); UART5_OutChar(',');
    UART5_OutSDec(d.absAccAreaZ); UART5_OutChar(',');
    UART5_OutSDec(d.absGyrAreaZ); UART5_OutChar(',');
    UART5_OutSDec(d.peakAccPosZ); UART5_OutChar(',');

    UART5_OutSDec(d.netRiseFrac); UART5_OutChar(',');
    UART5_OutSDec(d.endHighFrac); UART5_OutChar(',');
    UART5_OutSDec(d.startLowFrac); UART5_OutChar(',');
    UART5_OutSDec(d.crossBodyFrac); UART5_OutChar(',');
    UART5_OutSDec(d.smallDipFrac); UART5_OutChar(',');
    UART5_OutSDec(d.finishDropFrac); UART5_OutChar(',');
    UART5_OutSDec(d.peakTimeFrac); UART5_OutChar(',');
    UART5_OutSDec(d.riseOrderFlag); UART5_OutChar(',');
    UART5_OutSDec(d.oscDensity); UART5_OutChar(',');
    UART5_OutSDec(d.flipDensity); UART5_OutChar(',');
    UART5_OutSDec(d.spinFracZ); UART5_OutChar(',');
    UART5_OutSDec(d.spinPerMs); UART5_OutChar(',');
    UART5_OutSDec(d.peakAccZFrac); UART5_OutChar(',');
    UART5_OutSDec(d.zTravelDominance); UART5_OutChar(',');
    UART5_OutSDec(d.accZFrac); UART5_OutChar(',');
    UART5_OutSDec(d.avgEnergyPerSample);
    UART5_OutString("\r\n");
}
void PrintDanceTimeMs(uint32_t nowMs){
    uint32_t relMs;

    if(!TimeReachedMs(nowMs, gDanceStartMs)){
        UART0_OutChar('-');
        relMs = gDanceStartMs - nowMs;
    }else{
        relMs = nowMs - gDanceStartMs;
    }

    UART0_OutUDec(relMs / 1000U);
    UART0_OutChar('.');
    UART0_OutUDec3(relMs % 1000U);
}

void PrintStepChangeIfNeeded(uint32_t nowMs){
    uint32_t stepIndex;
    Move_t expected;

    if(TEST_MODE_ENABLE){
        return;
    }

    if(!TimeReachedMs(nowMs, gDanceStartMs)){
        return;
    }

    stepIndex = GetExpectedStepIndexStrict(nowMs);
		expected = gActiveChoreo[stepIndex].move;

if(stepIndex != gLastPrintedStepIndex){
    gPE3StepState ^= 1;

    if(gPE3StepState){
        PD3_On();
    }else{
        PD3_Off();
    }

    UART0_OutString("[STEP] t=");
    PrintDanceTimeMs(nowMs);
    UART0_OutString("s | EXPECTED: ");
    UART0_OutString(MoveName(expected));
    UART0_OutUDec(gActiveChoreo[stepIndex].durationMs / 1000U);
UART0_OutChar('.');
UART0_OutUDec3(gActiveChoreo[stepIndex].durationMs % 1000U);
    UART0_OutString("s\r\n");

    gLastPrintedStepIndex = stepIndex;
}
}

void PrintRuntimeBanner(void){
    UART0_OutString("--------------------------------------------------\r\n");
    UART0_OutString("Runtime config:\r\n");

    if(TEST_MODE_ENABLE){
        UART0_OutString("MODE: TEST\r\n");
        UART0_OutString("TEST MOVE: ");
        UART0_OutString(MoveName(TEST_MODE_MOVE));
        UART0_OutString("\r\n");
    }else{
        UART0_OutString("MODE: CHOREOGRAPHY\r\n");
    }

#if ARC_ML_INFERENCE_ENABLE
    UART0_OutString("ARC DETECTOR: ML FIXED-POINT\r\n");
#else
    UART0_OutString("ARC DETECTOR: RULE BASED\r\n");
#endif

#if ARC_ML_DATASET_MODE
    UART0_OutString("ARC DATASET CSV: ON\r\n");
#else
    UART0_OutString("ARC DATASET CSV: OFF\r\n");
#endif

#if TEST_STREAM_PRINT_ENABLE
    UART0_OutString("TEST STREAM PRINT: ON\r\n");
#else
    UART0_OutString("TEST STREAM PRINT: OFF\r\n");
#endif

#if CHOREO_STREAM_PRINT_ENABLE
    UART0_OutString("CHOREO STREAM PRINT: ON\r\n");
#else
    UART0_OutString("CHOREO STREAM PRINT: OFF\r\n");
#endif

    UART0_OutString("CHOREO PERIOD (ms): ");
    UART0_OutUDec(gChoreoPeriodMs);
    UART0_OutString("\r\n");

    UART0_OutString("--------------------------------------------------\r\n");
}

/* ============================================================
   Stillness detection + adaptive gyro bias
   ============================================================ */
void UpdateStillnessAndBias(const ProcSample_t *s,
                            int32_t gx_raw_logical,
                            int32_t gy_raw_logical,
                            int32_t gz_raw_logical){
    if((s->gyroMag < STILL_GYRO_MAG_THRESH) &&
       (s->accelDynMag < STILL_ACCEL_DYN_THRESH)){
        gStillCount++;

        if(gStillCount >= STILL_COUNT_FOR_BIAS){
            lgx_bias += ((gx_raw_logical - lgx_bias) >> BIAS_LPF_SHIFT);
            lgy_bias += ((gy_raw_logical - lgy_bias) >> BIAS_LPF_SHIFT);
            lgz_bias += ((gz_raw_logical - lgz_bias) >> BIAS_LPF_SHIFT);
        }
    }else{
        gStillCount = 0;
    }
}

/* ============================================================
   Convert raw sample -> processed sample
   ============================================================ */
void BuildProcessedSample(int16_t ax_raw, int16_t ay_raw, int16_t az_raw,
                          int16_t gx_raw, int16_t gy_raw, int16_t gz_raw,
                          ProcSample_t *s){
    int32_t axc, ayc, azc, gxc, gyc, gzc;
    int32_t lax, lay, laz, lgx, lgy, lgz;
    int32_t lgx_unbiased, lgy_unbiased, lgz_unbiased;

    axc = (int32_t)ax_raw - ax_offset;
    ayc = (int32_t)ay_raw - ay_offset;
    azc = (int32_t)az_raw - az_offset;

    gxc = (int32_t)gx_raw - gx_offset;
    gyc = (int32_t)gy_raw - gy_offset;
    gzc = (int32_t)gz_raw - gz_offset;

    RemapToLogicalFrame(axc, ayc, azc, gxc, gyc, gzc,
                        &lax, &lay, &laz, &lgx, &lgy, &lgz);

    lgx_unbiased = lgx - lgx_bias;
    lgy_unbiased = lgy - lgy_bias;
    lgz_unbiased = lgz - lgz_bias;

    gGxFilt += ((lgx_unbiased - gGxFilt) >> GYRO_LPF_SHIFT);
    gGyFilt += ((lgy_unbiased - gGyFilt) >> GYRO_LPF_SHIFT);
    gGzFilt += ((lgz_unbiased - gGzFilt) >> GYRO_LPF_SHIFT);

    gAxFilt += ((lax - gAxFilt) >> ACCEL_LPF_SHIFT);
    gAyFilt += ((lay - gAyFilt) >> ACCEL_LPF_SHIFT);
    gAzFilt += ((laz - gAzFilt) >> ACCEL_LPF_SHIFT);

    gAxBase += ((gAxFilt - gAxBase) >> GRAVITY_LPF_SHIFT);
    gAyBase += ((gAyFilt - gAyBase) >> GRAVITY_LPF_SHIFT);
    gAzBase += ((gAzFilt - gAzBase) >> GRAVITY_LPF_SHIFT);

    s->ax = gAxFilt;
    s->ay = gAyFilt;
    s->az = gAzFilt;

    s->gx = gGxFilt;
    s->gy = gGyFilt;
    s->gz = gGzFilt;

    s->ax_dyn = gAxFilt - gAxBase;
    s->ay_dyn = gAyFilt - gAyBase;
    s->az_dyn = gAzFilt - gAzBase;

    s->pose_dx = gAxFilt - gNeutralAx;
    s->pose_dy = gAyFilt - gNeutralAy;
    s->pose_dz = gAzFilt - gNeutralAz;

    s->gyroMag = Abs32(s->gx) + Abs32(s->gy) + Abs32(s->gz);
    s->accelDynMag = Abs32(s->ax_dyn) + Abs32(s->ay_dyn) + Abs32(s->az_dyn);
    s->energy = s->accelDynMag + (s->gyroMag >> 1);

    UpdateStillnessAndBias(s, lgx, lgy, lgz);
}

/* ============================================================
   Segment accumulation
   ============================================================ */
void SegmentClear(Segment_t *seg){
    seg->startMs = 0;
    seg->endMs = 0;
    seg->durationMs = 0;
    seg->samples = 0;

    seg->accAreaX = 0; seg->accAreaY = 0; seg->accAreaZ = 0;
    seg->absAccAreaX = 0; seg->absAccAreaY = 0; seg->absAccAreaZ = 0;

    seg->gyrAreaX = 0; seg->gyrAreaY = 0; seg->gyrAreaZ = 0;
    seg->absGyrAreaX = 0; seg->absGyrAreaY = 0; seg->absGyrAreaZ = 0;

    seg->poseAreaX = 0; seg->poseAreaY = 0; seg->poseAreaZ = 0;

    seg->peakPosePosY = 0; seg->peakPoseNegY = 0;
    seg->peakPosePosZ = 0; seg->peakPoseNegZ = 0;
		seg->peakEnergy = 0;
    seg->peakAccPosX = 0; seg->peakAccPosY = 0; seg->peakAccPosZ = 0;
    seg->peakAccNegX = 0; seg->peakAccNegY = 0; seg->peakAccNegZ = 0;

    seg->peakGyrX = 0; seg->peakGyrY = 0; seg->peakGyrZ = 0;

    seg->zeroCrossX = 0;
    seg->zeroCrossY = 0;
    seg->zeroCrossZ = 0;

    seg->startPoseX = 0; seg->startPoseY = 0; seg->startPoseZ = 0;
    seg->endPoseX = 0; seg->endPoseY = 0; seg->endPoseZ = 0;

    seg->minPoseX = 0; seg->maxPoseX = 0;
    seg->minPoseZ = 0; seg->maxPoseZ = 0;

    seg->minPoseZIndex = 0;
    seg->maxPoseZIndex = 0;

    seg->poseSlopeFlipX = 0;
    seg->poseSlopeFlipZ = 0;
}

void SegmentStart(Segment_t *seg, const ProcSample_t *s){
    SegmentClear(seg);
    seg->startMs = TimeNowMs();
    gLastSignX = SignFromThreshold(s->ax_dyn, 700);
    gLastSignY = SignFromThreshold(s->ay_dyn, 700);
    gLastSignZ = SignFromThreshold(s->az_dyn, 700);

    seg->startPoseX = s->pose_dx;
    seg->startPoseY = s->pose_dy;
    seg->startPoseZ = s->pose_dz;

    seg->endPoseX = s->pose_dx;
    seg->endPoseY = s->pose_dy;
    seg->endPoseZ = s->pose_dz;

    seg->minPoseX = s->pose_dx;
    seg->maxPoseX = s->pose_dx;
    seg->minPoseZ = s->pose_dz;
    seg->maxPoseZ = s->pose_dz;

    seg->minPoseZIndex = 0;
    seg->maxPoseZIndex = 0;

    seg->poseSlopeFlipX = 0;
    seg->poseSlopeFlipZ = 0;

    gPrevPoseX = s->pose_dx;
    gPrevPoseZ = s->pose_dz;
    gLastPoseSlopeX = 0;
    gLastPoseSlopeZ = 0;
}

void SegmentAccumulate(Segment_t *seg, const ProcSample_t *s){
    int8_t sx, sy, sz;
    int32_t dPoseX, dPoseZ;
    int8_t slopeX, slopeZ;

    seg->samples++;

    seg->accAreaX += s->ax_dyn;
    seg->accAreaY += s->ay_dyn;
    seg->accAreaZ += s->az_dyn;

    seg->absAccAreaX += Abs32(s->ax_dyn);
    seg->absAccAreaY += Abs32(s->ay_dyn);
    seg->absAccAreaZ += Abs32(s->az_dyn);

    seg->gyrAreaX += s->gx;
    seg->gyrAreaY += s->gy;
    seg->gyrAreaZ += s->gz;

    seg->absGyrAreaX += Abs32(s->gx);
    seg->absGyrAreaY += Abs32(s->gy);
    seg->absGyrAreaZ += Abs32(s->gz);

    seg->poseAreaX += s->pose_dx;
    seg->poseAreaY += s->pose_dy;
    seg->poseAreaZ += s->pose_dz;

    seg->endPoseX = s->pose_dx;
    seg->endPoseY = s->pose_dy;
    seg->endPoseZ = s->pose_dz;

    if(s->pose_dx < seg->minPoseX) seg->minPoseX = s->pose_dx;
    if(s->pose_dx > seg->maxPoseX) seg->maxPoseX = s->pose_dx;

    if(s->pose_dz < seg->minPoseZ){
        seg->minPoseZ = s->pose_dz;
        seg->minPoseZIndex = seg->samples;
    }
    if(s->pose_dz > seg->maxPoseZ){
        seg->maxPoseZ = s->pose_dz;
        seg->maxPoseZIndex = seg->samples;
    }
		if(s->energy > seg->peakEnergy) seg->peakEnergy = s->energy;
    dPoseX = s->pose_dx - gPrevPoseX;
    dPoseZ = s->pose_dz - gPrevPoseZ;

    slopeX = SignFromThreshold(dPoseX, 450);
    slopeZ = SignFromThreshold(dPoseZ, 450);

    if((gLastPoseSlopeX != 0) && (slopeX != 0) && (slopeX != gLastPoseSlopeX)){
        seg->poseSlopeFlipX++;
    }
    if((gLastPoseSlopeZ != 0) && (slopeZ != 0) && (slopeZ != gLastPoseSlopeZ)){
        seg->poseSlopeFlipZ++;
    }

    if(slopeX != 0) gLastPoseSlopeX = slopeX;
    if(slopeZ != 0) gLastPoseSlopeZ = slopeZ;

    gPrevPoseX = s->pose_dx;
    gPrevPoseZ = s->pose_dz;

    if(s->pose_dy > seg->peakPosePosY) seg->peakPosePosY = s->pose_dy;
    if(s->pose_dy < seg->peakPoseNegY) seg->peakPoseNegY = s->pose_dy;
    if(s->pose_dz > seg->peakPosePosZ) seg->peakPosePosZ = s->pose_dz;
    if(s->pose_dz < seg->peakPoseNegZ) seg->peakPoseNegZ = s->pose_dz;

    if(s->ax_dyn > seg->peakAccPosX) seg->peakAccPosX = s->ax_dyn;
    if(s->ay_dyn > seg->peakAccPosY) seg->peakAccPosY = s->ay_dyn;
    if(s->az_dyn > seg->peakAccPosZ) seg->peakAccPosZ = s->az_dyn;

    if(s->ax_dyn < seg->peakAccNegX) seg->peakAccNegX = s->ax_dyn;
    if(s->ay_dyn < seg->peakAccNegY) seg->peakAccNegY = s->ay_dyn;
    if(s->az_dyn < seg->peakAccNegZ) seg->peakAccNegZ = s->az_dyn;

    seg->peakGyrX = Max32(seg->peakGyrX, Abs32(s->gx));
    seg->peakGyrY = Max32(seg->peakGyrY, Abs32(s->gy));
    seg->peakGyrZ = Max32(seg->peakGyrZ, Abs32(s->gz));

    sx = SignFromThreshold(s->ax_dyn, 900);
    sy = SignFromThreshold(s->ay_dyn, 900);
    sz = SignFromThreshold(s->az_dyn, 900);

    if((gLastSignX != 0) && (sx != 0) && (sx != gLastSignX)) seg->zeroCrossX++;
    if((gLastSignY != 0) && (sy != 0) && (sy != gLastSignY)) seg->zeroCrossY++;
    if((gLastSignZ != 0) && (sz != 0) && (sz != gLastSignZ)) seg->zeroCrossZ++;

    if(sx != 0) gLastSignX = sx;
    if(sy != 0) gLastSignY = sy;
    if(sz != 0) gLastSignZ = sz;
}

void SegmentFinish(Segment_t *seg){
    seg->endMs = TimeNowMs();
    seg->durationMs = (uint16_t)(seg->endMs - seg->startMs);
}

/* ============================================================
   Score helpers
   ============================================================ */
int32_t ScoreDuration(uint16_t d, uint16_t idealMin, uint16_t idealMax){
    if(d >= idealMin && d <= idealMax) return 20;
    if(d >= (idealMin / 2) && d <= (idealMax + 250)) return 10;
    return 0;
}
int32_t ScorePeak(int32_t peak, int32_t goodThresh, int32_t okayThresh){
    if(peak >= goodThresh) return 18;
    if(peak >= okayThresh) return 9;
    return 0;
}

int32_t ScoreWait(const Segment_t *seg){
    (void)seg;
    return 0;
}

/* ============================================================
   Bent-arm sway / groove
   ============================================================ */
/* ============================================================
   Bent Arm Sway ML feature building + fixed-point inference
   ============================================================ */
void BuildSwayMLFeatures(const Segment_t *seg, ArcMLFeatures_t *f){
    ArcDerived_t d;
    BuildArcDerived(seg, &d);

    /* EXACT order from NEW Python SELECTED_FEATURES */
    f->x[0]  = d.riseOrderFlag;
    f->x[1]  = d.startLowZ;
    f->x[2]  = d.finishDropFromPeak;
    f->x[3]  = d.netRiseZ;
    f->x[4]  = d.finishDropFrac;
    f->x[5]  = d.peakTimeFrac;
    f->x[6]  = d.smallDipY;
    f->x[7]  = d.crossBodyX;
    f->x[8]  = d.startLowFrac;
    f->x[9]  = d.crossBodyFrac;
    f->x[10] = d.netRiseFrac;
    f->x[11] = d.peakAccPosZ;
    f->x[12] = d.oscDensity;
    f->x[13] = d.oscillationCount;
    f->x[14] = d.avgEnergyPerSample;
    f->x[15] = d.smallDipFrac;
}

int64_t RunSwayModelScaled(const ArcMLFeatures_t *f){
    int32_t xNorm[SWAY_NN_INPUTS];
    int32_t h[SWAY_NN_HIDDEN];
    uint32_t i, j;
    int64_t acc;

    /* Normalize inputs */
    for(i = 0; i < SWAY_NN_INPUTS; i++){
        int64_t num = ((int64_t)f->x[i] * SWAY_NN_SCALE) - gSwayNnInputMeanScaled[i];
        int32_t den = gSwayNnInputStdScaled[i];
        if(den == 0) { xNorm[i] = 0; }
        else { xNorm[i] = (int32_t)((num * SWAY_NN_SCALE) / den); }
    }

    /* Hidden layer & ReLU */
    for(j = 0; j < SWAY_NN_HIDDEN; j++){
        acc = gSwayNnB1Scaled[j];
        for(i = 0; i < SWAY_NN_INPUTS; i++){
            acc += ((int64_t)gSwayNnW1Scaled[i][j] * (int64_t)xNorm[i]) / SWAY_NN_SCALE;
        }
        if(acc < 0) { acc = 0; } // ReLU
        h[j] = (int32_t)acc;
    }

    /* Output layer */
    acc = gSwayNnB2Scaled;
    for(j = 0; j < SWAY_NN_HIDDEN; j++){
        acc += ((int64_t)gSwayNnW2Scaled[j] * (int64_t)h[j]) / SWAY_NN_SCALE;
    }
    return acc;
}

#define SWAY_ML_ACCEPT_LOGIT_MILLI 0

bool SwayMlDetect(const Segment_t *seg, int32_t *scoreOutMilli){
    ArcMLFeatures_t f;
    int32_t outMilli;

    BuildSwayMLFeatures(seg, &f);
    outMilli = (int32_t)RunSwayModelScaled(&f);
    *scoreOutMilli = outMilli;

    return (outMilli >= SWAY_ML_ACCEPT_LOGIT_MILLI);
}

int32_t ScoreBentArmSway(const Segment_t *seg){
    int32_t logitMilli = 0;
    bool passed = SwayMlDetect(seg, &logitMilli);

    // Crack open the black box and print the raw Neural Net score
    UART0_OutString("[SWAY AI] Logit: ");
    UART0_OutSFixed3FromMilli(logitMilli);
    
    if(passed){
        UART0_OutString("  -> PASSED!\r\n");
        return 100; // Safely passes the system's >62 threshold
    } else {
        UART0_OutString("  -> REJECTED\r\n");
        return 0;   // Fails
    }
}


void BuildRightArmWaveMLFeatures(const Segment_t *seg, ArcMLFeatures_t *f){
    ArcDerived_t d;
    BuildArcDerived(seg, &d);

    /* EXACT order from your Python output for MOVE_RIGHT_ARM_WAVE */
    f->x[0]  = d.endHighZ;            // feature 5
    f->x[1]  = d.totalAbsGyr;         // feature 13
    f->x[2]  = d.avgEnergyPerSample;  // feature 32
    f->x[3]  = d.netRiseZ;            // feature 3
    f->x[4]  = d.smallDipY;           // feature 8
    f->x[5]  = d.smallDipFrac;        // feature 21
    f->x[6]  = d.spinPerMs;           // feature 28
    f->x[7]  = d.absGyrAreaZ;         // feature 15
    f->x[8]  = d.startLowFrac;        // feature 19
    f->x[9]  = d.totalAbsAcc;         // feature 12
    f->x[10] = d.netRiseFrac;         // feature 17
    f->x[11] = d.startLowZ;           // feature 6
    f->x[12] = d.peakAccPosZ;         // feature 16
    f->x[13] = d.endHighFrac;         // feature 18
    f->x[14] = d.flipDensity;         // feature 26
    f->x[15] = d.slopeFlipCount;      // feature 11
}
void BuildRightArmThrowMLFeatures(const Segment_t *seg, ArcMLFeatures_t *f){
    ArcDerived_t d;
    BuildArcDerived(seg, &d);

    /* EXACT order from your Python output for MOVE_RIGHT_ARM_THROW
       NOTE: Python export label said ARC, but this order is for THROW. */
    f->x[0]  = d.endHighZ;            // feature 5
    f->x[1]  = d.totalAbsGyr;         // feature 13
    f->x[2]  = d.avgEnergyPerSample;  // feature 32
    f->x[3]  = d.netRiseZ;            // feature 3
    f->x[4]  = d.smallDipY;           // feature 8
    f->x[5]  = d.smallDipFrac;        // feature 21
    f->x[6]  = d.spinPerMs;           // feature 28
    f->x[7]  = d.absGyrAreaZ;         // feature 15
    f->x[8]  = d.startLowFrac;        // feature 19
    f->x[9]  = d.totalAbsAcc;         // feature 12
    f->x[10] = d.netRiseFrac;         // feature 17
    f->x[11] = d.startLowZ;           // feature 6
    f->x[12] = d.peakAccPosZ;         // feature 16
    f->x[13] = d.endHighFrac;         // feature 18
    f->x[14] = d.flipDensity;         // feature 26
    f->x[15] = d.slopeFlipCount;      // feature 11
}
void BuildRightArmFlexMLFeatures(const Segment_t *seg, ArcMLFeatures_t *f){
    ArcDerived_t d;
    BuildArcDerived(seg, &d);

    /* EXACT order from your Python output for MOVE_RIGHT_ARM_FLEX
       NOTE: Python export label said ARC, but this order is for RIGHT ARM FLEX. */
    f->x[0]  = d.endHighZ;            // feature 5
    f->x[1]  = d.totalAbsGyr;         // feature 13
    f->x[2]  = d.avgEnergyPerSample;  // feature 32
    f->x[3]  = d.netRiseZ;            // feature 3
    f->x[4]  = d.smallDipY;           // feature 8
    f->x[5]  = d.smallDipFrac;        // feature 21
    f->x[6]  = d.spinPerMs;           // feature 28
    f->x[7]  = d.absGyrAreaZ;         // feature 15
    f->x[8]  = d.startLowFrac;        // feature 19
    f->x[9]  = d.totalAbsAcc;         // feature 12
    f->x[10] = d.netRiseFrac;         // feature 17
    f->x[11] = d.startLowZ;           // feature 6
    f->x[12] = d.peakAccPosZ;         // feature 16
    f->x[13] = d.endHighFrac;         // feature 18
    f->x[14] = d.flipDensity;         // feature 26
    f->x[15] = d.slopeFlipCount;      // feature 11
}
void BuildFlexThrowHandsMLFeatures(const Segment_t *seg, ArcMLFeatures_t *f){
    ArcDerived_t d;
    BuildArcDerived(seg, &d);

    /* EXACT order from your Python output for MOVE_FLEX_THROW_HANDS
       NOTE: Python export label said ARC, but this order is for FLEX THROW HANDS. */
    f->x[0]  = d.endHighZ;            // feature 5
    f->x[1]  = d.totalAbsGyr;         // feature 13
    f->x[2]  = d.avgEnergyPerSample;  // feature 32
    f->x[3]  = d.netRiseZ;            // feature 3
    f->x[4]  = d.smallDipY;           // feature 8
    f->x[5]  = d.smallDipFrac;        // feature 21
    f->x[6]  = d.spinPerMs;           // feature 28
    f->x[7]  = d.absGyrAreaZ;         // feature 15
    f->x[8]  = d.startLowFrac;        // feature 19
    f->x[9]  = d.totalAbsAcc;         // feature 12
    f->x[10] = d.netRiseFrac;         // feature 17
    f->x[11] = d.startLowZ;           // feature 6
    f->x[12] = d.peakAccPosZ;         // feature 16
    f->x[13] = d.endHighFrac;         // feature 18
    f->x[14] = d.flipDensity;         // feature 26
    f->x[15] = d.slopeFlipCount;      // feature 11
}
void BuildRightArmTallScoopMLFeatures(const Segment_t *seg, ArcMLFeatures_t *f){
    ArcDerived_t d;
    BuildArcDerived(seg, &d);

    /* EXACT order from your Python output for MOVE_RIGHT_ARM_TALL_SCOOP
       NOTE: Python export label said ARC, but this order is for TALL SCOOP. */
    f->x[0]  = d.endHighZ;            // feature 5
    f->x[1]  = d.totalAbsGyr;         // feature 13
    f->x[2]  = d.avgEnergyPerSample;  // feature 32
    f->x[3]  = d.netRiseZ;            // feature 3
    f->x[4]  = d.smallDipY;           // feature 8
    f->x[5]  = d.smallDipFrac;        // feature 21
    f->x[6]  = d.spinPerMs;           // feature 28
    f->x[7]  = d.absGyrAreaZ;         // feature 15
    f->x[8]  = d.startLowFrac;        // feature 19
    f->x[9]  = d.totalAbsAcc;         // feature 12
    f->x[10] = d.netRiseFrac;         // feature 17
    f->x[11] = d.startLowZ;           // feature 6
    f->x[12] = d.peakAccPosZ;         // feature 16
    f->x[13] = d.endHighFrac;         // feature 18
    f->x[14] = d.flipDensity;         // feature 26
    f->x[15] = d.slopeFlipCount;      // feature 11
}
int64_t RunRightArmThrowModelScaled(const ArcMLFeatures_t *f){
    int32_t xNorm[RIGHT_ARM_THROW_NN_INPUTS];
    int32_t h[RIGHT_ARM_THROW_NN_HIDDEN];
    uint32_t i, j;
    int64_t acc;

    for(i = 0; i < RIGHT_ARM_THROW_NN_INPUTS; i++){
        int64_t num = ((int64_t)f->x[i] * RIGHT_ARM_THROW_NN_SCALE)
                    - gRightArmThrowNnInputMeanScaled[i];
        int32_t den = gRightArmThrowNnInputStdScaled[i];
        if(den == 0){
            xNorm[i] = 0;
        }else{
            xNorm[i] = (int32_t)((num * RIGHT_ARM_THROW_NN_SCALE) / den);
        }
    }

    for(j = 0; j < RIGHT_ARM_THROW_NN_HIDDEN; j++){
        acc = gRightArmThrowNnB1Scaled[j];
        for(i = 0; i < RIGHT_ARM_THROW_NN_INPUTS; i++){
            acc += ((int64_t)gRightArmThrowNnW1Scaled[i][j] * (int64_t)xNorm[i])
                 / RIGHT_ARM_THROW_NN_SCALE;
        }
        if(acc < 0){
            acc = 0;
        }
        h[j] = (int32_t)acc;
    }

    acc = gRightArmThrowNnB2Scaled;
    for(j = 0; j < RIGHT_ARM_THROW_NN_HIDDEN; j++){
        acc += ((int64_t)gRightArmThrowNnW2Scaled[j] * (int64_t)h[j])
             / RIGHT_ARM_THROW_NN_SCALE;
    }

    return acc;
}

int64_t RunFlexThrowHandsModelScaled(const ArcMLFeatures_t *f){
    int32_t xNorm[FLEX_THROW_HANDS_NN_INPUTS];
    int32_t h[FLEX_THROW_HANDS_NN_HIDDEN];
    uint32_t i, j;
    int64_t acc;

    for(i = 0; i < FLEX_THROW_HANDS_NN_INPUTS; i++){
        int64_t num = ((int64_t)f->x[i] * FLEX_THROW_HANDS_NN_SCALE)
                    - gFlexThrowHandsNnInputMeanScaled[i];
        int32_t den = gFlexThrowHandsNnInputStdScaled[i];
        if(den == 0){
            xNorm[i] = 0;
        }else{
            xNorm[i] = (int32_t)((num * FLEX_THROW_HANDS_NN_SCALE) / den);
        }
    }

    for(j = 0; j < FLEX_THROW_HANDS_NN_HIDDEN; j++){
        acc = gFlexThrowHandsNnB1Scaled[j];
        for(i = 0; i < FLEX_THROW_HANDS_NN_INPUTS; i++){
            acc += ((int64_t)gFlexThrowHandsNnW1Scaled[i][j] * (int64_t)xNorm[i])
                 / FLEX_THROW_HANDS_NN_SCALE;
        }
        if(acc < 0){
            acc = 0;
        }
        h[j] = (int32_t)acc;
    }

    acc = gFlexThrowHandsNnB2Scaled;
    for(j = 0; j < FLEX_THROW_HANDS_NN_HIDDEN; j++){
        acc += ((int64_t)gFlexThrowHandsNnW2Scaled[j] * (int64_t)h[j])
             / FLEX_THROW_HANDS_NN_SCALE;
    }

    return acc;
}
int64_t RunRightArmFlexModelScaled(const ArcMLFeatures_t *f){
    int32_t xNorm[RIGHT_ARM_FLEX_NN_INPUTS];
    int32_t h[RIGHT_ARM_FLEX_NN_HIDDEN];
    uint32_t i, j;
    int64_t acc;

    for(i = 0; i < RIGHT_ARM_FLEX_NN_INPUTS; i++){
        int64_t num = ((int64_t)f->x[i] * RIGHT_ARM_FLEX_NN_SCALE)
                    - gRightArmFlexNnInputMeanScaled[i];
        int32_t den = gRightArmFlexNnInputStdScaled[i];
        if(den == 0){
            xNorm[i] = 0;
        }else{
            xNorm[i] = (int32_t)((num * RIGHT_ARM_FLEX_NN_SCALE) / den);
        }
    }

    for(j = 0; j < RIGHT_ARM_FLEX_NN_HIDDEN; j++){
        acc = gRightArmFlexNnB1Scaled[j];
        for(i = 0; i < RIGHT_ARM_FLEX_NN_INPUTS; i++){
            acc += ((int64_t)gRightArmFlexNnW1Scaled[i][j] * (int64_t)xNorm[i])
                 / RIGHT_ARM_FLEX_NN_SCALE;
        }
        if(acc < 0){
            acc = 0;
        }
        h[j] = (int32_t)acc;
    }

    acc = gRightArmFlexNnB2Scaled;
    for(j = 0; j < RIGHT_ARM_FLEX_NN_HIDDEN; j++){
        acc += ((int64_t)gRightArmFlexNnW2Scaled[j] * (int64_t)h[j])
             / RIGHT_ARM_FLEX_NN_SCALE;
    }

    return acc;
}

bool RightArmFlexMlDetect(const Segment_t *seg, int32_t *scoreOutMilli){
    ArcMLFeatures_t f;
    int64_t logit;

    BuildRightArmFlexMLFeatures(seg, &f);
    logit = RunRightArmFlexModelScaled(&f);

    if(scoreOutMilli){
        *scoreOutMilli = (int32_t)logit;
    }

    return (logit >= 0);
}
bool FlexThrowHandsMlDetect(const Segment_t *seg, int32_t *scoreOutMilli){
    ArcMLFeatures_t f;
    int64_t logit;

    BuildFlexThrowHandsMLFeatures(seg, &f);
    logit = RunFlexThrowHandsModelScaled(&f);

    if(scoreOutMilli){
        *scoreOutMilli = (int32_t)logit;
    }

    return (logit >= 0);
}
int64_t RunRightArmTallScoopModelScaled(const ArcMLFeatures_t *f){
    int32_t xNorm[RIGHT_ARM_TALL_SCOOP_NN_INPUTS];
    int32_t h[RIGHT_ARM_TALL_SCOOP_NN_HIDDEN];
    uint32_t i, j;
    int64_t acc;

    for(i = 0; i < RIGHT_ARM_TALL_SCOOP_NN_INPUTS; i++){
        int64_t num = ((int64_t)f->x[i] * RIGHT_ARM_TALL_SCOOP_NN_SCALE)
                    - gRightArmTallScoopNnInputMeanScaled[i];
        int32_t den = gRightArmTallScoopNnInputStdScaled[i];
        if(den == 0){
            xNorm[i] = 0;
        }else{
            xNorm[i] = (int32_t)((num * RIGHT_ARM_TALL_SCOOP_NN_SCALE) / den);
        }
    }

    for(j = 0; j < RIGHT_ARM_TALL_SCOOP_NN_HIDDEN; j++){
        acc = gRightArmTallScoopNnB1Scaled[j];
        for(i = 0; i < RIGHT_ARM_TALL_SCOOP_NN_INPUTS; i++){
            acc += ((int64_t)gRightArmTallScoopNnW1Scaled[i][j] * (int64_t)xNorm[i])
                 / RIGHT_ARM_TALL_SCOOP_NN_SCALE;
        }
        if(acc < 0){
            acc = 0;
        }
        h[j] = (int32_t)acc;
    }

    acc = gRightArmTallScoopNnB2Scaled;
    for(j = 0; j < RIGHT_ARM_TALL_SCOOP_NN_HIDDEN; j++){
        acc += ((int64_t)gRightArmTallScoopNnW2Scaled[j] * (int64_t)h[j])
             / RIGHT_ARM_TALL_SCOOP_NN_SCALE;
    }

    return acc;
}
#define RIGHT_ARM_TALL_SCOOP_ML_ACCEPT_LOGIT_MILLI 0

bool RightArmTallScoopMlDetect(const Segment_t *seg, int32_t *scoreOutMilli){
    ArcMLFeatures_t f;
    int32_t outMilli;

    BuildRightArmTallScoopMLFeatures(seg, &f);
    outMilli = (int32_t)RunRightArmTallScoopModelScaled(&f);
    *scoreOutMilli = outMilli;

    return (outMilli >= RIGHT_ARM_TALL_SCOOP_ML_ACCEPT_LOGIT_MILLI);
}

#define RIGHT_ARM_THROW_ML_ACCEPT_LOGIT_MILLI 0

bool RightArmThrowMlDetect(const Segment_t *seg, int32_t *scoreOutMilli){
    ArcMLFeatures_t f;
    int32_t outMilli;

    BuildRightArmThrowMLFeatures(seg, &f);
    outMilli = (int32_t)RunRightArmThrowModelScaled(&f);
    *scoreOutMilli = outMilli;

    return (outMilli >= RIGHT_ARM_THROW_ML_ACCEPT_LOGIT_MILLI);
}
int64_t RunRightArmWaveModelScaled(const ArcMLFeatures_t *f){
    int32_t xNorm[RIGHT_ARM_WAVE_NN_INPUTS];
    int32_t h[RIGHT_ARM_WAVE_NN_HIDDEN];
    uint32_t i, j;
    int64_t acc;

    for(i = 0; i < RIGHT_ARM_WAVE_NN_INPUTS; i++){
        int64_t num = ((int64_t)f->x[i] * RIGHT_ARM_WAVE_NN_SCALE)
                    - gRightArmWaveNnInputMeanScaled[i];
        int32_t den = gRightArmWaveNnInputStdScaled[i];
        if(den == 0){
            xNorm[i] = 0;
        }else{
            xNorm[i] = (int32_t)((num * RIGHT_ARM_WAVE_NN_SCALE) / den);
        }
    }

    for(j = 0; j < RIGHT_ARM_WAVE_NN_HIDDEN; j++){
        acc = gRightArmWaveNnB1Scaled[j];
        for(i = 0; i < RIGHT_ARM_WAVE_NN_INPUTS; i++){
            acc += ((int64_t)gRightArmWaveNnW1Scaled[i][j] * (int64_t)xNorm[i])
                 / RIGHT_ARM_WAVE_NN_SCALE;
        }
        if(acc < 0){
            acc = 0;
        }
        h[j] = (int32_t)acc;
    }

    acc = gRightArmWaveNnB2Scaled;
    for(j = 0; j < RIGHT_ARM_WAVE_NN_HIDDEN; j++){
        acc += ((int64_t)gRightArmWaveNnW2Scaled[j] * (int64_t)h[j])
             / RIGHT_ARM_WAVE_NN_SCALE;
    }

    return acc;
}

#define RIGHT_ARM_WAVE_ML_ACCEPT_LOGIT_MILLI 0

bool RightArmWaveMlDetect(const Segment_t *seg, int32_t *scoreOutMilli){
    ArcMLFeatures_t f;
    int32_t outMilli;

    BuildRightArmWaveMLFeatures(seg, &f);
    outMilli = (int32_t)RunRightArmWaveModelScaled(&f);
    *scoreOutMilli = outMilli;

    return (outMilli >= RIGHT_ARM_WAVE_ML_ACCEPT_LOGIT_MILLI);
}

int32_t ScoreRightArmWave(const Segment_t *seg){
    int32_t logitMilli = 0;
    bool passed = RightArmWaveMlDetect(seg, &logitMilli);

    UART0_OutString("[RIGHT ARM WAVE AI] Logit: ");
    UART0_OutSFixed3FromMilli(logitMilli);

    if(passed){
        UART0_OutString("  -> PASSED!\r\n");
        return 100;
    }else{
        UART0_OutString("  -> REJECTED\r\n");
        return 0;
    }
}

int32_t ScoreCowboy(const Segment_t *seg){
    int32_t score = 0;

    int32_t totalAbsAcc;
    int32_t totalAbsGyr;
    int32_t crossBodyX;
    int32_t totalRiseZ;
    int32_t oscillationCount;
    int32_t slopeFlipCount;
    int32_t gyroDom, accDom;
    int32_t spinFrac;
    int32_t gyroPerMs;
    int32_t avgAccPerSample;
    int32_t netRiseZ;

    totalAbsAcc = seg->absAccAreaX + seg->absAccAreaY + seg->absAccAreaZ;
    totalAbsGyr = seg->absGyrAreaX + seg->absGyrAreaY + seg->absGyrAreaZ;

    crossBodyX = seg->maxPoseX - seg->minPoseX;
    totalRiseZ = seg->maxPoseZ - seg->minPoseZ;
    netRiseZ = seg->endPoseZ - seg->startPoseZ;

    oscillationCount = seg->zeroCrossX + seg->zeroCrossY + seg->zeroCrossZ;
    slopeFlipCount = seg->poseSlopeFlipX + seg->poseSlopeFlipZ;

    gyroDom = Max32(seg->absGyrAreaX, Max32(seg->absGyrAreaY, seg->absGyrAreaZ));
    accDom  = Max32(seg->absAccAreaX, Max32(seg->absAccAreaY, seg->absAccAreaZ));

    spinFrac = (1000 * gyroDom) / Max32(totalAbsGyr, 1);
    gyroPerMs = totalAbsGyr / Max32(seg->durationMs, 1);
    avgAccPerSample = totalAbsAcc / Max32(seg->samples, 1);

    /* These windows are short in your dataset, so score short chunks */
    score += ScoreDuration(seg->durationMs, 70, 130);

    /* Strong rotational content is the main signature */
    if(totalAbsGyr > 220000) score += 24;
    else if(totalAbsGyr > 150000) score += 16;
    else if(totalAbsGyr > 100000) score += 8;
    else score -= 18;

    /* Gyro should matter more than accel for this move */
    if(totalAbsGyr > (totalAbsAcc + (totalAbsAcc >> 1))) score += 18;   // > 1.5x acc
    else if(totalAbsGyr > totalAbsAcc) score += 10;
    else score -= 14;

    /* Some side sweep/cross-body travel is expected */
    if(crossBodyX > 8000) score += 14;
    else if(crossBodyX > 3500) score += 8;
    else score -= 8;

    /* Some Z travel helps, but do NOT force a big upward arc */
    if(totalRiseZ > 4000) score += 8;
    else if(totalRiseZ < 1200) score -= 6;

    /* Repeated motion should have a little oscillation, not dead-straight */
    if(oscillationCount >= 1 && oscillationCount <= 4) score += 10;
    else if(oscillationCount == 0) score -= 8;
    else score -= 4;

    if(slopeFlipCount >= 1 && slopeFlipCount <= 3) score += 8;
    else if(slopeFlipCount > 5) score -= 6;

    /* Dominant spin axis should exist, but not be absurdly one-axis-only */
    if(spinFrac >= 380 && spinFrac <= 820) score += 8;
    else if(spinFrac < 250) score -= 6;

    /* Good rotation density */
    if(gyroPerMs > 1800) score += 10;
    else if(gyroPerMs > 1100) score += 5;
    else score -= 8;

    /* Penalize huge translation-heavy smacks */
    if(avgAccPerSample > 13000) score -= 10;

    /* A cowboy loop chunk can rise or fall depending on where the window starts.
       So only punish almost-no-Z-change a little, not sign. */
    if(Abs32(netRiseZ) < 500) score -= 4;

    return score;
}
/* ============================================================
   Kick

   Placeholder: always returns 100 (auto-pass).
   Replace with a NN-backed detector later; the segment engine
   and choreo wiring are already in place.
   ============================================================ */
int32_t ScoreKick(const Segment_t *seg){
    (void)seg;
    UART0_OutString("[KICK] auto-pass (placeholder)\r\n");
    return 100;
}

int32_t ScoreLeftArmWave(const Segment_t *seg){
    (void)seg;
    return 0;
}



int32_t ScoreWave(const Segment_t *seg){
    (void)seg;
    return 0;
}

int32_t ScoreHop(const Segment_t *seg){
    (void)seg;
    return 0;
}
int32_t ScoreRightArmBeat(const Segment_t *seg){ (void)seg; return 0; }
int32_t ScoreRightArmHighLow(const Segment_t *seg){ (void)seg; return 0; }
int32_t ScoreRightArmLowHigh(const Segment_t *seg){ (void)seg; return 0; }
int32_t ScoreLeftArmLowHigh(const Segment_t *seg){ (void)seg; return 0; }
int32_t ScorePunch(const Segment_t *seg){ (void)seg; return 0; }
int32_t ScorePunchUp(const Segment_t *seg){ (void)seg; return 0; }
int32_t ScoreRightArm(const Segment_t *seg){ (void)seg; return 0; }
int32_t ScoreSquiggle(const Segment_t *seg){ (void)seg; return 0; }
int32_t ScoreUp(const Segment_t *seg){ (void)seg; return 0; }
int32_t ScoreArmsScoopDown(const Segment_t *seg){ (void)seg; return 0; }
int32_t ScoreRightJabs(const Segment_t *seg){ (void)seg; return 0; }
int32_t ScoreLeftJabs(const Segment_t *seg){ (void)seg; return 0; }
int32_t ScorePointLeftToRight(const Segment_t *seg){ (void)seg; return 0; }
int32_t ScoreArmDownPoint(const Segment_t *seg){ (void)seg; return 0; }
int32_t ScoreRightArmComeHereScoop(const Segment_t *seg){
    int32_t totalAbsAcc;
    int32_t totalAbsGyr;
    int32_t avgEnergyPerSample;

    totalAbsAcc = seg->absAccAreaX + seg->absAccAreaY + seg->absAccAreaZ;
    totalAbsGyr = seg->absGyrAreaX + seg->absGyrAreaY + seg->absGyrAreaZ;
    avgEnergyPerSample = (totalAbsAcc + (totalAbsGyr >> 1)) / Max32(seg->samples, 1);

    if(seg->peakEnergy >= 6000) return 100;
    if(avgEnergyPerSample >= 4000) return 100;

    return 0;
}

int32_t ScoreRightArmTallScoop(const Segment_t *seg){
    int32_t logitMilli = 0;
    bool passed = RightArmTallScoopMlDetect(seg, &logitMilli);

    UART0_OutString("[RIGHT ARM TALL SCOOP AI] Logit: ");
    UART0_OutSFixed3FromMilli(logitMilli);

    if(passed){
        UART0_OutString("  -> PASSED!\r\n");
        return 100;
    }else{
        UART0_OutString("  -> REJECTED\r\n");
        return 0;
    }
}

int32_t ScoreSpinJump(const Segment_t *seg){

    ArcDerived_t d;
    int highCount = 0;
    int midCount  = 0;

    BuildArcDerived(seg, &d);

    /* reject obvious fragment / junk segments first */
    if(d.samples < 50)     return 0;
    if(d.durationMs < 250) return 0;

    /* -------------------------
       HIGH band checks
       Based on the stronger middle/later rows:
       - totalAbsGyr commonly ~500k to 747k
       - spinPerMs commonly ~235k to 499k
       - crossBodyX commonly ~30k to 48k
       - avgEnergyPerSample commonly ~29k to 42k
       - oscillationCount usually ~4 to 7
       - slopeFlipCount usually ~3 to 5
       ------------------------- */
    if(d.totalAbsGyr >= 600000)      highCount++;
    else if(d.totalAbsGyr >= 450000) midCount++;

    if(d.spinPerMs >= 320000)        highCount++;
    else if(d.spinPerMs >= 220000)   midCount++;

    if(d.crossBodyX >= 34000)        highCount++;
    else if(d.crossBodyX >= 24000)   midCount++;

    if(d.avgEnergyPerSample >= 34000)      highCount++;
    else if(d.avgEnergyPerSample >= 26000) midCount++;

    if(d.oscillationCount >= 4 && d.oscillationCount <= 7) highCount++;
    else if(d.oscillationCount >= 3 && d.oscillationCount <= 8) midCount++;

    if(d.slopeFlipCount >= 3 && d.slopeFlipCount <= 5) highCount++;
    else if(d.slopeFlipCount >= 2 && d.slopeFlipCount <= 5) midCount++;

    /* spinFracZ is supportive only, not a core gate */
    if(d.spinFracZ >= 220)           highCount++;
    else if(d.spinFracZ >= 160)      midCount++;

    /* -------------------------
       final mapping
       100 = clearly matches the strong cluster
       70  = enough mid-level evidence
       0   = everything else
       ------------------------- */
    if(highCount >= 5) return 100;

    if((highCount + midCount) >= 5 && highCount >= 2) return 70;

    return 0;
}

int32_t ScoreRightArmThrow(const Segment_t *seg){
    int32_t logitMilli = 0;
    bool passed = RightArmThrowMlDetect(seg, &logitMilli);

    UART0_OutString("[RIGHT ARM THROW AI] Logit: ");
    UART0_OutSFixed3FromMilli(logitMilli);

    if(passed){
        UART0_OutString("  -> PASSED!\r\n");
        return 100;
    }else{
        UART0_OutString("  -> REJECTED\r\n");
        return 0;
    }
}

int32_t ScoreLeftArmThrow(const Segment_t *seg){
    (void)seg;
    return 0;
}

int32_t ScoreFlexThrowHands(const Segment_t *seg){
    int32_t logitMilli = 0;
    bool passed = FlexThrowHandsMlDetect(seg, &logitMilli);

    UART0_OutString("[FLEX THROW HANDS AI] Logit: ");
    UART0_OutSFixed3FromMilli(logitMilli);

    if(passed){
        UART0_OutString("  -> PASSED!\r\n");
        return 100;
    }else{
        UART0_OutString("  -> REJECTED\r\n");
        return 0;
    }
}

int32_t ScoreRightArmPunch(const Segment_t *seg){
    (void)seg;
    return 0;
}

int32_t ScoreRightArmFlex(const Segment_t *seg){
    int32_t logitMilli = 0;
    bool passed = RightArmFlexMlDetect(seg, &logitMilli);

    UART0_OutString("[RIGHT ARM FLEX AI] Logit: ");
    UART0_OutSFixed3FromMilli(logitMilli);

    if(passed){
        UART0_OutString("  -> NN PASSED!\r\n");
        return 100;
    }

    UART0_OutString("  -> NN REJECTED, trying fallback...\r\n");

    /* =========================
       FALLBACK RULE SCORER
       ========================= */

    ArcDerived_t d;
    int32_t score = 0;
    int32_t strongCount = 0;
    int32_t broadCount = 0;

    BuildArcDerived(seg, &d);

    if(d.endHighZ <= -10000){
        broadCount++;
        score += 18;
        if(d.endHighZ <= -14500){
            strongCount++;
            score += 6;
        }
    }

    if(d.netRiseZ <= -4000){
        broadCount++;
        score += 16;
        if(d.netRiseZ <= -8500){
            strongCount++;
            score += 6;
        }
    }

    if(d.smallDipY >= 17000){
        broadCount++;
        score += 16;
        if(d.smallDipY >= 22000){
            strongCount++;
            score += 5;
        }
    }

    if(d.finishDropFromPeak >= 6000){
        broadCount++;
        score += 12;
        if(d.finishDropFromPeak >= 11000){
            strongCount++;
            score += 5;
        }
    }

    if(d.totalRiseZ >= 8000){
        broadCount++;
        score += 10;
        if(d.totalRiseZ >= 14000){
            strongCount++;
            score += 4;
        }
    }

    if(d.crossBodyX >= 9000){
        broadCount++;
        score += 8;
        if(d.crossBodyX >= 18000){
            strongCount++;
            score += 4;
        }
    }

    if(d.avgEnergyPerSample >= 18000){
        broadCount++;
        score += 8;
        if(d.avgEnergyPerSample >= 26000){
            strongCount++;
            score += 3;
        }
    }

    if(d.totalAbsAcc >= 700000){
        broadCount++;
        score += 6;
        if(d.totalAbsAcc >= 1000000){
            strongCount++;
            score += 3;
        }
    }

    if(d.totalAbsGyr >= 300000){
        score += 4;
        if(d.totalAbsGyr >= 600000){
            score += 2;
        }
    }

    /* ---- FINAL DECISION ---- */

    if(!(d.endHighZ <= -9000 || d.netRiseZ <= -3500 || d.smallDipY >= 16000)){
        UART0_OutString("  -> RULE FAIL (not even close)\r\n");
        return 0;
    }

    if(broadCount < 3){
        UART0_OutString("  -> RULE FAIL (too weak)\r\n");
        return 0;
    }

    UART0_OutString("  -> RULE score=");
    UART0_OutSDec(score);
    UART0_OutString("\r\n");

    if(score >= 78 || (broadCount >= 5 && strongCount >= 2)){
        return 70;
    }else{
        return 70;
    }
}

int32_t ScoreWindingArms(const Segment_t *seg){
    ArcDerived_t d;
    int hasRotation;
    int hasMotion;

    BuildArcDerived(seg, &d);

    /* Hard reject only for extremely weak junk */
    if(d.totalAbsAcc < 85000 &&
       d.totalAbsGyr < 3000 &&
       d.totalRiseZ < 300 &&
       d.crossBodyX < 150 &&
       d.avgEnergyPerSample < 4500){
        return 0;
    }

    /* Must show SOME rotational character */
    hasRotation =
        (d.spinPerMs >= 10000) ||
        (d.spinFracZ >= 12) ||
        (d.absGyrAreaZ >= 350);

    /* Optional supporting motion (very loose) */
    hasMotion =
        (d.totalAbsAcc >= 70000) ||
        (d.totalRiseZ >= 250) ||
        (d.crossBodyX >= 120);

    if(hasRotation && hasMotion){
        return 100;
    }

    return 0;
}

int32_t ScoreArmSlowUpToDown(const Segment_t *seg){
    (void)seg;
    return 0;
}

int32_t ScoreEnergy(const Segment_t *seg){
    int32_t totalAbsAcc;
    int32_t totalAbsGyr;
    int32_t avgEnergyPerSample;

    totalAbsAcc = seg->absAccAreaX + seg->absAccAreaY + seg->absAccAreaZ;
    totalAbsGyr = seg->absGyrAreaX + seg->absGyrAreaY + seg->absGyrAreaZ;
    avgEnergyPerSample = (totalAbsAcc + (totalAbsGyr >> 1)) / Max32(seg->samples, 1);

    if(seg->peakEnergy >= 14000) return 100;
    if(avgEnergyPerSample >= 9500) return 100;

    return 0;
}
int32_t ScoreHype(const Segment_t *seg){
    int32_t totalAbsAcc;
    int32_t totalAbsGyr;
    int32_t avgEnergyPerSample;

    totalAbsAcc = seg->absAccAreaX + seg->absAccAreaY + seg->absAccAreaZ;
    totalAbsGyr = seg->absGyrAreaX + seg->absGyrAreaY + seg->absGyrAreaZ;
    avgEnergyPerSample = (totalAbsAcc + (totalAbsGyr >> 1)) / Max32(seg->samples, 1);

    if(seg->peakEnergy >= 14000) return 100;
    if(avgEnergyPerSample >= 9500) return 100;

    return 0;
}
/* ============================================================
   Pump up
   ============================================================ */
int32_t ScorePumpUp(const Segment_t *seg){
    int32_t score = 0;
    int32_t poseUp;
    int32_t poseDown;
    int32_t sideSpin;

    poseUp = (2 * seg->peakPosePosY) + seg->peakPosePosZ;
    poseDown = (2 * Abs32(seg->peakPoseNegY)) + Abs32(seg->peakPoseNegZ);
    sideSpin = seg->absGyrAreaZ;

    score += ScoreDuration(seg->durationMs, 140, 1100);
    score += ScorePeak(poseUp, 9000, 4500);

    if(poseUp > poseDown) score += 18;

    if((2 * seg->peakAccPosY + seg->peakAccPosZ) >= 5500) score += 14;
    else if((2 * seg->peakAccPosY + seg->peakAccPosZ) >= 2500) score += 7;

    if(seg->peakGyrX >= 1200 || seg->peakGyrY >= 1200 || seg->peakGyrZ >= 1200) score += 8;

    if(sideSpin < 45000) score += 12;
    else if(sideSpin < 70000) score += 6;

    if(seg->zeroCrossY <= 2 && seg->zeroCrossZ <= 2) score += 8;

    return score;
}
int32_t ScoreGangam(const Segment_t* seg){
    ArcDerived_t d;
    int32_t gyrPerSample;

    BuildArcDerived(seg, &d);

    // need a real full window (engine caps at 20)
    if(seg->samples < 18) return 0;

    // HARD GATES -- all must pass. Thresholds sit ~15-20% below
    // training-set minimums to allow untrained dancers through.

    // 1) sustained energy across the window (training min 7773)
    if(d.avgEnergyPerSample < 6500) return 0;

    // 2) real linear acceleration (training min totalAbsAcc 96356)
    if(d.totalAbsAcc < 80000) return 0;

    // 3) real rotation -- this is what kills pure flicks
    //    (training min totalAbsGyr 24360, min per-sample ~1218)
    if(d.totalAbsGyr < 20000) return 0;
    gyrPerSample = d.totalAbsGyr / seg->samples;
    if(gyrPerSample < 1000) return 0;

    // 4) Z-axis content must be present in BOTH acc and gyro
    //    (training mins: absAccAreaZ 7274, absGyrAreaZ 8779, totalRiseZ 459)
    if(d.absAccAreaZ < 5500) return 0;
    if(seg->absGyrAreaZ < 7000) return 0;
    if(d.totalRiseZ < 350) return 0;

    // 5) POSE SIGNATURE -- the distinguishing feature.
    //    Gangnam "horse" pose leans downward throughout:
    //      start pose Z is at or below neutral  (startLowZ >= ~0)
    //      end   pose Z is at or below neutral  (endHighZ  <= ~0)
    //    111 of 112 training rows have endHighZ <= 0; the outlier
    //    sits at 2279. Random upward flicks fail here hard.
    if(d.startLowZ < -1500) return 0;   // training min -983
    if(d.endHighZ  >  3000) return 0;   // training max  2279

    // Passed all gates. Emit diagnostics so tuning is easy.
    UART0_OutString("[GANGAM] E/s=");
    UART0_OutSDec(d.avgEnergyPerSample);
    UART0_OutString(" acc=");
    UART0_OutSDec(d.totalAbsAcc);
    UART0_OutString(" gyr=");
    UART0_OutSDec(d.totalAbsGyr);
    UART0_OutString(" startLow=");
    UART0_OutSDec(d.startLowZ);
    UART0_OutString(" endHigh=");
    UART0_OutSDec(d.endHighZ);
    UART0_OutString(" totRiseZ=");
    UART0_OutSDec(d.totalRiseZ);
    UART0_OutString(" -> PASS\r\n");

    return 100;
}

int32_t ScoreSwipe(const Segment_t *seg){
    (void)seg;
    return 0;
}


/* ============================================================
   Pump down
   ============================================================ */
int32_t ScorePumpDown(const Segment_t *seg){
    int32_t score = 0;
    ArcDerived_t d;

    BuildArcDerived(seg, &d);
		if(seg->durationMs < 550) return 0;
if(seg->samples < 110) return 0;
if(d.totalRiseZ < 3500) return 0;
if(d.absAccAreaZ < 90000) return 0;
if(d.accZFrac < 40) return 0;

    /* --------------------------------------------------
       1) Duration / samples
       Tiny flicks are short. Real pumps are longer.
       -------------------------------------------------- */
    if(seg->durationMs >= 600 && seg->durationMs <= 850) score += 24;
    else if(seg->durationMs >= 520 && seg->durationMs <= 950) score += 12;
    else return 0;   // kill short junk immediately

    if(seg->samples >= 120) score += 12;
    else if(seg->samples >= 105) score += 6;
    else return 0;   // tiny flick segments die here

    /* --------------------------------------------------
       2) Real downward pose excursion
       This is the big fix. Flicks had tiny start/end pose.
       Actual pumps show meaningful startLowZ / endHighZ.
       -------------------------------------------------- */
    if(d.startLowZ >= 3000) score += 16;
    else if(d.startLowZ >= 1500) score += 8;

    if(d.endHighZ <= -1500) score += 16;
    else if(d.endHighZ <= -700) score += 8;

    /* Prefer true downward overall travel, but don't make
       it the only thing because some segments include return */
    if(d.netRiseZ <= -1200) score += 10;
    else if(d.netRiseZ < 0) score += 5;

    /* --------------------------------------------------
       3) Z-axis focus
       Keep this, but tone down the stupid flick farming.
       -------------------------------------------------- */
    if(d.accZFrac >= 70) score += 10;
    else if(d.accZFrac >= 50) score += 5;

    if(d.zTravelDominance >= 250) score += 10;
    else if(d.zTravelDominance >= 180) score += 5;

    if(d.absAccAreaZ >= 120000) score += 10;
    else if(d.absAccAreaZ >= 80000) score += 5;

    /* --------------------------------------------------
       4) Actual motion energy
       -------------------------------------------------- */
    if(d.avgEnergyPerSample >= 14000) score += 8;
    else if(d.avgEnergyPerSample >= 11000) score += 4;

    /* --------------------------------------------------
       5) Cleanliness
       -------------------------------------------------- */
    if(d.oscillationCount <= 10) score += 6;
    if(d.slopeFlipCount <= 5) score += 6;
    else if(d.slopeFlipCount <= 7) score += 3;

    return (score * 62) / 110;;
}

/* ============================================================
   Side-to-side
   ============================================================ */
int32_t ScoreSideToSide(const Segment_t *seg){
    int32_t score = 0;
    int32_t sidePrimary;
    int32_t poseSwing;

    sidePrimary = seg->absGyrAreaZ + (seg->absAccAreaX >> 1);
    poseSwing = Abs32(seg->poseAreaX) + Abs32(seg->poseAreaZ >> 1);

    score += ScoreDuration(seg->durationMs, 180, 1500);

    if(seg->peakGyrZ >= 6500) score += 22;
    else if(seg->peakGyrZ >= 3200) score += 12;

    if(seg->absGyrAreaZ >= 45000) score += 18;
    else if(seg->absGyrAreaZ >= 22000) score += 9;

    if(seg->zeroCrossX >= 1 || seg->zeroCrossZ >= 1) score += 14;

    if(seg->absAccAreaX >= 18000) score += 10;
    else if(seg->absAccAreaX >= 9000) score += 5;

    if(poseSwing >= 14000) score += 10;
    else if(poseSwing >= 7000) score += 5;

    if(Abs32(seg->poseAreaY) < (seg->absGyrAreaZ >> 1)) score += 8;

    (void)sidePrimary;
    return score;
}

/* ============================================================
   Arc across body low-to-high (fallback rule-based version)
   ============================================================ */
int32_t ScoreArc(const Segment_t *seg){
    int32_t score = 0;

    int32_t netRiseZ;
    int32_t totalRiseZ;
    int32_t endHighZ;
    int32_t startLowZ;
    int32_t crossBodyX;
    int32_t smallDipY;
    int32_t oscillationCount;
    int32_t slopeFlipCount;
    int32_t sideSpin;
    int32_t finishDropFromPeak;

    netRiseZ = seg->endPoseZ - seg->startPoseZ;
    totalRiseZ = seg->maxPoseZ - seg->minPoseZ;
    endHighZ = seg->endPoseZ;
    startLowZ = -seg->startPoseZ;
    crossBodyX = seg->maxPoseX - seg->minPoseX;
    smallDipY = Abs32(seg->peakPoseNegY);

    oscillationCount = seg->zeroCrossX + seg->zeroCrossY + seg->zeroCrossZ;
    slopeFlipCount = seg->poseSlopeFlipX + seg->poseSlopeFlipZ;
    sideSpin = seg->absGyrAreaZ;
    finishDropFromPeak = seg->maxPoseZ - seg->endPoseZ;

    score += ScoreDuration(seg->durationMs, 420, 1250);

    if(netRiseZ > 9000) score += 24;
    else if(netRiseZ > 5500) score += 12;

    if(totalRiseZ > 15000) score += 20;
    else if(totalRiseZ > 9500) score += 10;

    if(endHighZ > 8500) score += 18;
    else if(endHighZ > 4500) score += 9;

    if(startLowZ > 1800) score += 8;
    else if(startLowZ > 700) score += 4;

    if(crossBodyX > 4500 && crossBodyX < 22000) score += 12;
    else if(crossBodyX > 2500) score += 6;

    if(smallDipY > 800 && smallDipY < 5000) score += 6;
    else if(smallDipY > 8500) score -= 10;

    if(seg->maxPoseZIndex > seg->minPoseZIndex) score += 14;
    else score -= 18;

    if(finishDropFromPeak < 3500) score += 12;
    else if(finishDropFromPeak > 7000) score -= 14;

    if(oscillationCount <= 2) score += 10;
    else if(oscillationCount <= 4) score += 4;
    else score -= 12;

    if(slopeFlipCount <= 2) score += 12;
    else if(slopeFlipCount <= 4) score += 5;
    else score -= 14;

    if(sideSpin < 26000) score += 8;
    else if(sideSpin > 42000) score -= 8;

    if(seg->peakAccPosZ > 5000) score += 8;
    else if(seg->peakAccPosZ > 2600) score += 4;

    if(netRiseZ < 3500) score -= 24;
    if(totalRiseZ < 7000) score -= 20;
    if(endHighZ < 2500) score -= 20;
    if(seg->maxPoseZIndex < (seg->samples / 3)) score -= 10;
    if(seg->peakPosePosZ < Abs32(seg->peakPoseNegZ) + 2000) score -= 10;

    return score;
}

/* ============================================================
   Cross arms
   ============================================================ */
int32_t ScoreCrossArms(const Segment_t *seg){
    int32_t score = 0;
    int32_t poseHoldMag;
    int32_t avgDynAccel;
    int32_t avgGyro;
    int32_t oscillationCount;

    if(seg->samples <= 0) return 0;

    poseHoldMag = Abs32(seg->poseAreaX) + Abs32(seg->poseAreaY) + Abs32(seg->poseAreaZ);

    avgDynAccel = (seg->absAccAreaX + seg->absAccAreaY + seg->absAccAreaZ) / seg->samples;
    avgGyro = (seg->absGyrAreaX + seg->absGyrAreaY + seg->absGyrAreaZ) / seg->samples;

    oscillationCount = seg->zeroCrossX + seg->zeroCrossY + seg->zeroCrossZ;

    score += ScoreDuration(seg->durationMs, 700, 1600);

    if(poseHoldMag > 18000) score += 18;
    else if(poseHoldMag > 9000) score += 9;

    if(avgDynAccel < 1400) score += 18;
    else if(avgDynAccel < 2400) score += 9;

    if(avgGyro < 700) score += 18;
    else if(avgGyro < 1300) score += 9;

    if(oscillationCount <= 1) score += 16;
    else if(oscillationCount <= 3) score += 8;

    if(avgDynAccel > 4200) score -= 18;
    if(avgGyro > 2200) score -= 18;

    return score;
}

/* ============================================================
   ARC ML feature building + fixed-point inference
   ============================================================ */
void BuildArcMLFeatures(const Segment_t *seg, ArcMLFeatures_t *f){
    ArcDerived_t d;

    BuildArcDerived(seg, &d);

    /* EXACT order from Python SELECTED_FEATURES */
    f->x[0]  = d.endHighZ;
    f->x[1]  = d.totalAbsGyr;
    f->x[2]  = d.avgEnergyPerSample;
    f->x[3]  = d.netRiseZ;
    f->x[4]  = d.smallDipY;
    f->x[5]  = d.smallDipFrac;
    f->x[6]  = d.spinPerMs;
    f->x[7]  = d.absGyrAreaZ;
    f->x[8]  = d.startLowFrac;
    f->x[9]  = d.totalAbsAcc;
    f->x[10] = d.netRiseFrac;
    f->x[11] = d.startLowZ;
    f->x[12] = d.peakAccPosZ;
    f->x[13] = d.endHighFrac;
    f->x[14] = d.flipDensity;
    f->x[15] = d.slopeFlipCount;
}
void PrintDetectRow(Move_t expected, int32_t score){
    UART5_OutString("DETECT,");
    UART5_OutString(MoveName(expected));
    UART5_OutChar(',');
    UART5_OutSDec(score);
    UART5_OutString("\r\n");
}
void PrintArcDatasetHeader(void){
    UART0_OutString(
        "ARC_DATASET,"
        "expectedMove,"
        "durationMs,"
        "samples,"
        "netRiseZ,"
        "totalRiseZ,"
        "endHighZ,"
        "startLowZ,"
        "crossBodyX,"
        "smallDipY,"
        "finishDropFromPeak,"
        "oscillationCount,"
        "slopeFlipCount,"
        "totalAbsAcc,"
        "totalAbsGyr,"
        "absAccAreaZ,"
        "absGyrAreaZ,"
        "peakAccPosZ,"
        "netRiseFrac,"
        "endHighFrac,"
        "startLowFrac,"
        "crossBodyFrac,"
        "smallDipFrac,"
        "finishDropFrac,"
        "peakTimeFrac,"
        "riseOrderFlag,"
        "oscDensity,"
        "flipDensity,"
        "spinFracZ,"
        "spinPerMs,"
        "peakAccZFrac,"
        "zTravelDominance,"
        "accZFrac,"
        "avgEnergyPerSample\r\n"
    );
}

void PrintArcDatasetRow(const Segment_t *seg, Move_t expected){
    ArcDerived_t d;

    BuildArcDerived(seg, &d);

    UART0_OutString("ARC_DATASET,");
    UART0_OutString(MoveName(expected)); UART0_OutChar(',');

    UART0_OutSDec(d.durationMs); UART0_OutChar(',');
    UART0_OutSDec(d.samples); UART0_OutChar(',');
    UART0_OutSDec(d.netRiseZ); UART0_OutChar(',');
    UART0_OutSDec(d.totalRiseZ); UART0_OutChar(',');
    UART0_OutSDec(d.endHighZ); UART0_OutChar(',');
    UART0_OutSDec(d.startLowZ); UART0_OutChar(',');
    UART0_OutSDec(d.crossBodyX); UART0_OutChar(',');
    UART0_OutSDec(d.smallDipY); UART0_OutChar(',');
    UART0_OutSDec(d.finishDropFromPeak); UART0_OutChar(',');
    UART0_OutSDec(d.oscillationCount); UART0_OutChar(',');
    UART0_OutSDec(d.slopeFlipCount); UART0_OutChar(',');
    UART0_OutSDec(d.totalAbsAcc); UART0_OutChar(',');
    UART0_OutSDec(d.totalAbsGyr); UART0_OutChar(',');
    UART0_OutSDec(d.absAccAreaZ); UART0_OutChar(',');
    UART0_OutSDec(d.absGyrAreaZ); UART0_OutChar(',');
    UART0_OutSDec(d.peakAccPosZ); UART0_OutChar(',');

    UART0_OutSDec(d.netRiseFrac); UART0_OutChar(',');
    UART0_OutSDec(d.endHighFrac); UART0_OutChar(',');
    UART0_OutSDec(d.startLowFrac); UART0_OutChar(',');
    UART0_OutSDec(d.crossBodyFrac); UART0_OutChar(',');
    UART0_OutSDec(d.smallDipFrac); UART0_OutChar(',');
    UART0_OutSDec(d.finishDropFrac); UART0_OutChar(',');
    UART0_OutSDec(d.peakTimeFrac); UART0_OutChar(',');
    UART0_OutSDec(d.riseOrderFlag); UART0_OutChar(',');
    UART0_OutSDec(d.oscDensity); UART0_OutChar(',');
    UART0_OutSDec(d.flipDensity); UART0_OutChar(',');
    UART0_OutSDec(d.spinFracZ); UART0_OutChar(',');
    UART0_OutSDec(d.spinPerMs); UART0_OutChar(',');
    UART0_OutSDec(d.peakAccZFrac); UART0_OutChar(',');
    UART0_OutSDec(d.zTravelDominance); UART0_OutChar(',');
    UART0_OutSDec(d.accZFrac); UART0_OutChar(',');
    UART0_OutSDec(d.avgEnergyPerSample);
    UART0_OutString("\r\n");
}
int64_t RunArcModelScaled(const ArcMLFeatures_t *f){
    int32_t xNorm[ARC_NN_INPUTS];
    int32_t h[ARC_NN_HIDDEN];
    uint32_t i, j;
    int64_t acc;

    /* Normalize inputs into SCALE=1000 domain */
    for(i = 0; i < ARC_NN_INPUTS; i++){
        int64_t num = ((int64_t)f->x[i] * ARC_NN_SCALE) - gArcNnInputMeanScaled[i];
        int32_t den = gArcNnInputStdScaled[i];

        if(den == 0){
            xNorm[i] = 0;
        }else{
            xNorm[i] = (int32_t)((num * ARC_NN_SCALE) / den);
        }
    }

    /* Hidden layer: h = ReLU(b1 + sum(w1*x)/SCALE) */
    for(j = 0; j < ARC_NN_HIDDEN; j++){
        acc = gArcNnB1Scaled[j];

        for(i = 0; i < ARC_NN_INPUTS; i++){
            acc += ((int64_t)gArcNnW1Scaled[i][j] * (int64_t)xNorm[i]) / ARC_NN_SCALE;
        }

        if(acc < 0){
            acc = 0;
        }

        h[j] = (int32_t)acc;
    }

    /* Output layer: out = b2 + sum(w2*h)/SCALE */
    acc = gArcNnB2Scaled;
    for(j = 0; j < ARC_NN_HIDDEN; j++){
        acc += ((int64_t)gArcNnW2Scaled[j] * (int64_t)h[j]) / ARC_NN_SCALE;
    }

    return acc;
}


#define ARC_ML_ACCEPT_LOGIT_MILLI      2500

bool ArcMlDetect(const Segment_t *seg, int32_t *scoreOutMilli){
    ArcMLFeatures_t f;
    int32_t outMilli;

    BuildArcMLFeatures(seg, &f);

    outMilli = (int32_t)RunArcModelScaled(&f);
    *scoreOutMilli = outMilli;

    return (outMilli >= ARC_ML_ACCEPT_LOGIT_MILLI);
}


void PrintArcMLSummary(const Segment_t *seg, int32_t logitMilli){
    ArcDerived_t d;
    BuildArcDerived(seg, &d);

    UART0_OutString("[ARC NN] out=");
    UART0_OutSFixed3FromMilli(logitMilli);
    UART0_OutString(" | endHighZ=");
    UART0_OutSDec(d.endHighZ);
    UART0_OutString(" totalAbsGyr=");
    UART0_OutSDec(d.totalAbsGyr);
    UART0_OutString(" avgEnergy=");
    UART0_OutSDec(d.avgEnergyPerSample);
    UART0_OutString(" netRiseZ=");
    UART0_OutSDec(d.netRiseZ);
    UART0_OutString(" smallDipY=");
    UART0_OutSDec(d.smallDipY);
    UART0_OutString(" spinPerMs=");
    UART0_OutSDec(d.spinPerMs);
    UART0_OutString("\r\n");
}

/* ============================================================
   Segment classification
   ============================================================ */
Move_t EvaluateSingleMove(const Segment_t *seg, Move_t move, int32_t *scoreOut){
    const MoveInfo_t *info;
    int32_t score = 0;

    if(move == MOVE_ARC){
#if ARC_ML_INFERENCE_ENABLE
        if(ArcMlDetect(seg, &score)){
            *scoreOut = score;
            return MOVE_ARC;
        }else{
            *scoreOut = score;
            return MOVE_NONE;
        }
#else
        score = ScoreArc(seg);
        *scoreOut = score;
        if(score >= SCORE_ACCEPT_THRESH) return MOVE_ARC;
        return MOVE_NONE;
#endif
    }

    info = GetMoveInfo(move);
    if(info != 0){
        score = info->scoreFunc(seg);
    }else{
        score = 0;
    }

    *scoreOut = score;
    if(score >= SCORE_ACCEPT_THRESH) return move;
    return MOVE_NONE;
}

Move_t EvaluateSegment(const Segment_t *seg, Move_t expected, int32_t *bestScoreOut){
    Move_t detected;
    int32_t score = 0;

    if(seg->samples < SEGMENT_MIN_SAMPLES){
        *bestScoreOut = 0;
        return MOVE_NONE;
    }

    if(seg->durationMs > 2300){
        *bestScoreOut = 0;
        return MOVE_NONE;
    }

    detected = EvaluateSingleMove(seg, expected, &score);
    *bestScoreOut = score;
    return detected;
}

/* ============================================================
   Optional debug print
   ============================================================ */
void PrintReadingLogicalProcessed(const ProcSample_t *s, Move_t expected){
    UART0_OutString("AX:");
    UART0_OutSDec(s->ax);
    UART0_OutString(" AY:");
    UART0_OutSDec(s->ay);
    UART0_OutString(" AZ:");
    UART0_OutSDec(s->az);

    UART0_OutString(" | dAX:");
    UART0_OutSDec(s->ax_dyn);
    UART0_OutString(" dAY:");
    UART0_OutSDec(s->ay_dyn);
    UART0_OutString(" dAZ:");
    UART0_OutSDec(s->az_dyn);

    UART0_OutString(" | pX:");
    UART0_OutSDec(s->pose_dx);
    UART0_OutString(" pY:");
    UART0_OutSDec(s->pose_dy);
    UART0_OutString(" pZ:");
    UART0_OutSDec(s->pose_dz);

    UART0_OutString(" | GX:");
    UART0_OutSDec(s->gx);
    UART0_OutString(" GY:");
    UART0_OutSDec(s->gy);
    UART0_OutString(" GZ:");
    UART0_OutSDec(s->gz);

    UART0_OutString(" | E:");
    UART0_OutSDec(s->energy);
    UART0_OutString(" | EXP:");
    UART0_OutString(MoveName(expected));
    UART0_OutString("\r\n");
}

/* ============================================================
   Segment engine

   Per-move segment tuning now comes entirely from gMoveInfo[]
   via ResolveSegmentTuning(). To tweak thresholds for a move,
   edit that move's row in gMoveInfo[] -- not this function.
   ============================================================ */
Move_t UpdateGestureEngine(const ProcSample_t *s, Move_t *expectedOut, int32_t *scoreOut){
    uint32_t nowMs = TimeNowMs();
    Move_t expected = GetExpectedMoveForRuntime(nowMs);
    Move_t detected = MOVE_NONE;

    /* Pull per-move segment tuning from the registry (with fallback to defaults) */
    int32_t  startThresh;
    int32_t  continueThresh;
    uint32_t quietCountThresh;
    uint32_t maxSamplesThresh;

    ResolveSegmentTuning(expected,
                         &startThresh,
                         &continueThresh,
                         &quietCountThresh,
                         &maxSamplesThresh);

    *expectedOut = expected;
    *scoreOut = 0;

    switch(gSegState){

        case SEG_IDLE:
            if(s->energy >= startThresh){
                UART0_OutString("[SEG START] E=");
                UART0_OutSDec(s->energy);
                UART0_OutString(" | EXP:");
                UART0_OutString(MoveName(expected));
                UART0_OutString("\r\n");

                SegmentStart(&gCurSeg, s);
                SegmentAccumulate(&gCurSeg, s);
                gQuietCount = 0;
                gSegExpectedAtStart = expected;
                gSegState = SEG_ACTIVE;
            }
            break;

        case SEG_ACTIVE:
            if(expected != gSegExpectedAtStart){
                UART0_OutString("[SEG DROP] choreography changed from ");
                UART0_OutString(MoveName(gSegExpectedAtStart));
                UART0_OutString(" to ");
                UART0_OutString(MoveName(expected));
                UART0_OutString("\r\n");

                SegmentClear(&gCurSeg);
                gQuietCount = 0;
                gSegExpectedAtStart = MOVE_NONE;
                gSegState = SEG_IDLE;
                break;
            }

            SegmentAccumulate(&gCurSeg, s);

            if((s->energy < continueThresh) || (gCurSeg.samples >= maxSamplesThresh)){
                gQuietCount++;
            }else{
                if(gQuietCount > 0) gQuietCount--;
            }

            if(gQuietCount >= quietCountThresh || gCurSeg.samples >= maxSamplesThresh){
                int32_t bestScore = 0;

                SegmentFinish(&gCurSeg);
							
                UART0_OutString("[SEG END] dur=");
               UART0_OutUDec(gCurSeg.durationMs);
UART0_OutString(" ms samples=");
UART0_OutUDec(gCurSeg.samples);
UART0_OutString(" | EXP:");
UART0_OutString(MoveName(expected));
UART0_OutString("\r\n");

/* Send full 32-feature segment row to espA -> espC */
//PrintSegmentRow(&gCurSeg, expected);   ///REMOVE THIS
//ADD THIS BACK
#if ARC_ML_DATASET_MODE
PrintArcDatasetRow(&gCurSeg, expected);
#endif

/* Evaluate AFTER printing segment */
/* Evaluate AFTER printing segment */
detected = EvaluateSegment(&gCurSeg, expected, &bestScore);
*scoreOut = bestScore;

#if PRINT_INDIVIDUAL_SEGMENT_SCORES
UART5_OutString("DETECT,");
UART5_OutString(MoveName(expected));
UART5_OutChar(',');
UART5_OutSDec(bestScore);
UART5_OutString("\r\n");
#endif

if(TEST_MODE_ENABLE){
    /* keep old immediate behavior in test mode */
    PrintDetectRow(expected, bestScore);
}else{
    if(TimeReachedMs(nowMs, gDanceStartMs)){
        if(expected == MOVE_HYPE){
            /* HYPE is special: print every segment immediately in choreography mode */
            PrintDetectRow(expected, bestScore);
        }else{
            /* all other choreography moves keep cumulative step scoring */
            uint32_t stepIndex = GetExpectedStepIndexStrict(nowMs);
            StepScore_AddSegmentScore(stepIndex, expected, bestScore);
        }
    }
}

#if ARC_ML_INFERENCE_ENABLE
    if(expected == MOVE_ARC){
        PrintArcMLSummary(&gCurSeg, bestScore);
    }
#endif

                gCooldownStartMs = TimeNowMs();
                gSegExpectedAtStart = MOVE_NONE;
                gSegState = SEG_COOLDOWN;
            }
            break;

        case SEG_COOLDOWN:
            if((TimeNowMs() - gCooldownStartMs) >= SEGMENT_COOLDOWN_MS){
                gSegExpectedAtStart = MOVE_NONE;
                gSegState = SEG_IDLE;
            }
            break;

        default:
            gSegState = SEG_IDLE;
            break;
    }

    return detected;
}

/* ============================================================
   Main
   ============================================================ */
int main(void){
    int16_t ax, ay, az, gx, gy, gz;
    ProcSample_t s;
    Move_t expected, detected;
    int32_t score;
    uint32_t printCount = 0;
    uint32_t calibrationStartMs = 0;
    uint32_t nextMainSampleMs;
    uint32_t nowMs;
    bool runStarted = false;

    Clock_Init16MHz_MOSC();
    LED_Init();
    PD3_Init();
    PD3_On();
    LED_Set(LED_BLUE);

    Timer0A_InitFreeRunning();
    UART0_Init();
    UART5_Init();
	UART5_FlushRx();
    I2C1_Init();

    EEPROM_Init();
    LoadSongIDFromEEPROM();
    gChoreoPeriodMs = ComputeChoreoPeriodMs();

    UART0_OutString("LSM6DSOX Just Dance framework starting\r\n");
    UART0_OutString("Boot song ID: ");
    UART0_OutUDec(activeSongID);
    UART0_OutString("\r\n");

bool ok = false;

while(!ok){
    for(int i = 0; i < 5; i++){
        I2C1_Init();

        uint32_t t = TimeNowMs();
        WaitUntilMs(t + 20);

        if(LSM6DSOX_Init()){
            ok = true;
            break;
        }
    }

    if(!ok){
        UART0_OutString("Sensor init failed, retrying...\r\n");
        LED_Set(LED_BLUE);

        uint32_t t = TimeNowMs();
        WaitUntilMs(t + 100);   // small retry delay
    }
}

    UART0_OutString("Idle. Send '0', '1', or '2' to choose song, send 's' to start.\r\n");

    while(1){

        if(!runStarted){
            if(HandleUARTCommandsAndCheckStart()){
                UART0_OutString("Start command received\r\n");
                UART0_OutString("Calibrating for 5 seconds in handheld start pose.\r\n");

                if(!Calibrate(&calibrationStartMs)){
                    UART0_OutString("Calibration failed\r\n");
                    LED_Set(LED_BLUE);
                    while(1){}
                }

                gDanceStartMs = calibrationStartMs + CALIBRATION_MS;

                UART0_OutString("Calibration done\r\n");
								PrintDetectRow(MOVE_WAIT, 69);
                PrintRuntimeBanner();

                if(!TEST_MODE_ENABLE){
                    UART0_OutString("Schedule: CAL 5.0, WAIT 10.5, PUMP DOWN 3.5, ARC 1.5, CROSS ARMS 2.0, PUMP DOWN 3.5, ARC 1.5, CROSS ARMS 2.8, SWAY x6 (5.4), KICK 1.9, SWAY x6 (5.4), KICK 1.7\r\n");
                }

#if ARC_ML_DATASET_MODE
                PrintArcDatasetHeader();
#endif

                LED_Set(LED_GREEN);

                gLastPrintedStepIndex = 0xFFFFFFFFU;
                StepScore_Reset();
                nextMainSampleMs = gDanceStartMs + MAIN_SAMPLE_PERIOD_MS;
                printCount = 0;
                runStarted = true;
            }

            continue;
        }

        AdvanceToNextSample(&nextMainSampleMs, MAIN_SAMPLE_PERIOD_MS);
        (void)HandleUARTCommandsAndCheckStart();   // keep accepting song changes for next run

        nowMs = TimeNowMs();

        /* Handle one-shot step-final detect emission on step boundaries */
        StepScore_HandleBoundary(nowMs);

        if(ReadRaw(&ax, &ay, &az, &gx, &gy, &gz)){
            BuildProcessedSample(ax, ay, az, gx, gy, gz, &s);

            detected = UpdateGestureEngine(&s, &expected, &score);
            nowMs = TimeNowMs();

            PrintStepChangeIfNeeded(nowMs);

#if TEST_STREAM_PRINT_ENABLE
            if(TEST_MODE_ENABLE){
                printCount++;
                if(printCount >= STREAM_PRINT_DIVIDER){
                    PrintReadingLogicalProcessed(&s, expected);
                    printCount = 0;
                }
            }else
#endif
#if CHOREO_STREAM_PRINT_ENABLE
            {
                printCount++;
                if(printCount >= STREAM_PRINT_DIVIDER){
                    UART0_OutString("[E] ");
                    UART0_OutSDec(s.energy);
                    UART0_OutString(" | EXP:");
                    UART0_OutString(MoveName(expected));
                    UART0_OutString("\r\n");
                    printCount = 0;
                }
            }
#else
            {
                (void)printCount;
            }
#endif

            if(detected != MOVE_NONE){
                UART0_OutString("[DETECT] t=");
                PrintDanceTimeMs(nowMs);
                UART0_OutString("s | MOVE: ");
                UART0_OutString(MoveName(detected));
                UART0_OutString(" | SCORE: ");
                UART0_OutSDec(score);
                UART0_OutString("\r\n");
            }
        }
    }
}