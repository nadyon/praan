#ifndef _PPG_ALGORITHM_H_
#define _PPG_ALGORITHM_H_

#include <stdint.h>
#include <string.h>

/************************************************************
 Sampling constants (Fs = 50 Hz)
 Dataset uses MAX30102 at 50Hz — 250 samples = 5 seconds
************************************************************/
#define PPG150MS            ((int16_t)(8))
#define PPG200MS            ((int16_t)(10))
#define PPG360MS            ((int16_t)(18))
#define PPG1000MS           ((int16_t)(50))
#define PPG2000MS           ((int16_t)(100))
#define PPG4000MS           ((int16_t)(200))

#define GENERAL_DELAY       ((int16_t)(18))

/************************************************************
 Pulse interval limits
************************************************************/
#define PI92PERCENT         ((int16_t)(92))
#define PI116PERCENT        ((int16_t)(116))
#define PI166PERCENT        ((int16_t)(166))

/************************************************************
 Buffer sizes
************************************************************/
#define LP_BUFFER_SIZE      ((int16_t)(16))
#define HP_BUFFER_SIZE      ((int16_t)(32))
#define DR_BUFFER_SIZE      ((int16_t)(4))
#define MVA_BUFFER_SIZE     ((int16_t)(20))
#define PI_BUFFER_SIZE      ((int16_t)(8))

/************************************************************
 Fixed-point limits
************************************************************/
#define FILTER_FORM         2

#define SQR_LIM_VAL         ((int16_t)(256))
#define SQR_LIM_OUT         ((uint16_t)(30000))
#define MVA_LIM_VAL         ((int16_t)(32000))

/************************************************************
 State machine
************************************************************/
#define START_UP            0
#define LEARN_PH_1          1
#define LEARN_PH_2          2
#define DETECTING           3

#define IRREGULAR_PR        1
#define REGULAR_PR          0

/************************************************************
 Signal quality limits
************************************************************/
#define SQI_GOOD            80
#define SQI_MED             50
#define SQI_POOR            30

/************************************************************
 Main data structure
************************************************************/
struct PPG_struct
{

/* filter pointers */

int16_t LP_pointer;
int16_t HP_pointer;
int16_t MVA_pointer;

/* algorithm state */

int16_t PPG_state;

/* pulse interval */

int16_t PulseInterval;
int16_t Recent_PI_M;

/************************************************************
 Filter outputs
************************************************************/

int16_t LPF_val;
int16_t HPF_val;
int16_t DRF_val;

uint16_t SQF_val;
uint16_t MVA_val;

/************************************************************
 Adaptive thresholds
************************************************************/

uint16_t ThI1;
uint16_t SPKI;
uint16_t NPKI;
uint16_t ThI2;

int16_t ThF1;
int16_t SPKF;
int16_t NPKF;
int16_t ThF2;

/************************************************************
 Pulse interval statistics
************************************************************/

int16_t PI_M;
int16_t PI_Low_L;
int16_t PI_High_L;
int16_t PI_Missed_L;

int16_t PR_State;

/************************************************************
 Optical components
************************************************************/

/* DC components */

uint32_t RED_DC;
uint32_t IR_DC;

/* AC components — instantaneous signed AC signal (zero-centred).
   MUST be int16_t: PPG_DCRemoval computes sample - DC which is
   negative during signal trough. uint16_t wraps negative values
   to ~63000+ causing all downstream amplitude tracking to fail. */

int16_t  RED_AC;
int16_t  IR_AC;

/* FIX: peak-to-peak amplitude over one full pulse cycle.
   Computed by Compute_ACDC() and used by SpO2, PI, SQI.
   Previously these functions incorrectly read raw IR_AC. */

uint16_t IR_amplitude;

/* FIX: RED channel peak amplitude for SpO2 numerator.
   RED_AC (instantaneous) goes negative in signal trough,
   making R negative and SpO2 clamp to 100. */

uint16_t RED_amplitude;

/************************************************************
 Peak detector edge-detection state
************************************************************/

uint8_t  hr_peak_max_u8_pad;   /* alignment padding */

/************************************************************
 Physiological outputs
************************************************************/

uint16_t HeartRate;
uint16_t SpO2;
uint16_t RespRate;
uint16_t PerfusionIndex;

/************************************************************
 Heart rate peak detector state (HPF-based, replaces Pan-Tompkins)
************************************************************/

int16_t  hr_prev;
int16_t  hr_prev2;
int16_t  hr_peak_max;
int16_t  hr_refractory;

/************************************************************
 ACDC tracker state (replaces static locals in Compute_ACDC)
 int16_t to match signed IR_AC / RED_AC
************************************************************/

int16_t  acdc_peak;
int16_t  acdc_valley;
int16_t  red_peak;
int16_t  red_valley;

/************************************************************
 UpdatePI state (replaces static locals in UpdatePI)
************************************************************/

int16_t  sample_counter;
uint16_t startup_samples;

/************************************************************
 Respiration counter (replaces static local in Compute_Respiration)
************************************************************/

int16_t  resp_counter;

/************************************************************
 Adaptive threshold state (replaces static locals in UpdateThI)
************************************************************/

uint16_t signal_level;
uint16_t noise_level;

/************************************************************
 Signal quality
************************************************************/

uint16_t SQI;

/************************************************************
 Circular buffers
************************************************************/

int16_t LP_buf[LP_BUFFER_SIZE];
int16_t HP_buf[HP_BUFFER_SIZE];
int16_t DR_buf[DR_BUFFER_SIZE];

uint16_t MVA_buf[MVA_BUFFER_SIZE];

int16_t PI_AVRG1_buf[PI_BUFFER_SIZE];
int16_t PI_AVRG2_buf[PI_BUFFER_SIZE];

};

/************************************************************
 Core API
************************************************************/

void PPG_init(void);
void PPG_prewarm_DC(int16_t dc_estimate);

int16_t PPG_StateMachine(
        int16_t red_sample,
        int16_t ir_sample);

/************************************************************
 Pre-processing
************************************************************/

void PPG_DCRemoval(
        int16_t red_sample,
        int16_t ir_sample);

void LPFilter(int16_t *val);
void HPFilter(void);
void DerivFilter(void);
void SQRFilter(void);
void MVAFilter(void);

/************************************************************
 Heart rate detection (HPF peak-based, replaces Pan-Tompkins)
************************************************************/

void PPG_HeartRatePeak(int16_t hpf_val);

/************************************************************
 Adaptive thresholds
************************************************************/

void UpdateThI(uint16_t *PEAKI, int8_t NOISE_F);
void UpdateThF(int16_t *PEAKF, int8_t NOISE_F);

/************************************************************
 Physiological computations
************************************************************/

void Compute_ACDC(void);
void Compute_SpO2(void);
void Compute_Respiration(void);
void Compute_PerfusionIndex(void);

/************************************************************
 Signal quality
************************************************************/

void Compute_SQI(void);
void MotionArtifactCheck(void);

/************************************************************
 Debug outputs
************************************************************/

int16_t PPG_get_LPFilter_output(void);
int16_t PPG_get_HPFilter_output(void);
int16_t PPG_get_DRFilter_output(void);

uint16_t PPG_get_SQRFilter_output(void);
uint16_t PPG_get_MVFilter_output(void);

uint16_t PPG_get_HeartRate(void);
uint16_t PPG_get_SpO2(void);
uint16_t PPG_get_RespRate(void);
uint16_t PPG_get_PerfusionIndex(void);
uint16_t PPG_get_SQI(void);

uint16_t PPG_get_ThI1_output(void);
int16_t  PPG_get_ThF1_output(void);

int16_t  PPG_get_PRState_output(void);

/************************************************************
 Global state
************************************************************/
extern struct PPG_struct PPG_data;


#endif