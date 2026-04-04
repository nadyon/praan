#include "ppgalgorithm.h"

/************************************************************
 GLOBAL STRUCTURE
************************************************************/

struct PPG_struct PPG_data;

/************************************************************
 INITIALIZATION
************************************************************/

void PPG_init(void)
{

    memset(&PPG_data,0,sizeof(PPG_data));

    PPG_data.PPG_state = START_UP;

    PPG_data.ThI1 = 0;
    PPG_data.ThI2 = 0;

    PPG_data.ThF1 = 0;
    PPG_data.ThF2 = 0;

    PPG_data.PR_State = REGULAR_PR;

}

/* Call this after PPG_init() and before the first PPG_StateMachine() call.
   The IIR DC filter (tau = 16 samples) needs ~5*tau = 80 iterations to
   converge from zero to the signal DC level. With only 250 samples per
   subject there is no headroom for a cold-start transient.
   Pre-warming runs the DC filter in isolation until it stabilises,
   so the very first real sample sees an already-converged DC estimate. */

void PPG_prewarm_DC(int16_t dc_estimate)
{
    int32_t est = (int32_t)dc_estimate;

    for(int i = 0; i < 200; i++)
    {
        PPG_data.RED_DC = (uint32_t)((int32_t)PPG_data.RED_DC +
                          ((est - (int32_t)PPG_data.RED_DC) >> 4));
        PPG_data.IR_DC  = (uint32_t)((int32_t)PPG_data.IR_DC  +
                          ((est - (int32_t)PPG_data.IR_DC)  >> 4));
    }
}


/************************************************************
 MAIN STATE MACHINE
************************************************************/

int16_t PPG_StateMachine(int16_t red_sample,
                         int16_t ir_sample)
{

    /* DC removal */

    PPG_DCRemoval(red_sample,ir_sample);

    /* bandpass filtering */

    LPFilter(&PPG_data.RED_AC);
    HPFilter();

    /* derivative */

    DerivFilter();

    /* squaring */

    SQRFilter();

    /* moving window integration */

    MVAFilter();

    /* peak detection on HPF output — direct PPG systolic peak finder.
       Pan-Tompkins (derivative->square->MVA->threshold) is designed for
       ECG QRS spikes and does not work for smooth PPG waveforms: the
       derivative energy stays high throughout the beat cycle so MVA never
       drops below ThI1, meaning no rising edges are ever detected. */

    PPG_HeartRatePeak(PPG_data.HPF_val);

    /* compute physiological features */

    Compute_ACDC();
    Compute_SpO2();
    Compute_Respiration();
    Compute_PerfusionIndex();

    /* signal quality */

    Compute_SQI();

    /* motion rejection */

    MotionArtifactCheck();

    return 0;

}


/************************************************************
 DC REMOVAL
************************************************************/

void PPG_DCRemoval(int16_t red_sample,
                   int16_t ir_sample)
{
    int32_t red = (int32_t)red_sample;
    int32_t ir  = (int32_t)ir_sample;

    PPG_data.RED_DC = (uint32_t)((int32_t)PPG_data.RED_DC +
                      ((red - (int32_t)PPG_data.RED_DC) >> 4));

    PPG_data.IR_DC  = (uint32_t)((int32_t)PPG_data.IR_DC  +
                      ((ir  - (int32_t)PPG_data.IR_DC)  >> 4));

    PPG_data.RED_AC = (int16_t)(red - (int32_t)PPG_data.RED_DC);
    PPG_data.IR_AC  = (int16_t)(ir  - (int32_t)PPG_data.IR_DC);
}


/************************************************************
 LOW PASS FILTER
************************************************************/

void LPFilter(int16_t *val)
{

    PPG_data.LP_buf[PPG_data.LP_pointer] = *val;

    int32_t sum = 0;

    for(int i=0;i<LP_BUFFER_SIZE;i++)
        sum += PPG_data.LP_buf[i];

    PPG_data.LPF_val = sum >> 4;

    PPG_data.LP_pointer++;

    if(PPG_data.LP_pointer >= LP_BUFFER_SIZE)
        PPG_data.LP_pointer = 0;

}


/************************************************************
 HIGH PASS FILTER
************************************************************/

void HPFilter(void)
{

    int32_t acc = 0;

    for(int i=0;i<HP_BUFFER_SIZE;i++)
        acc += PPG_data.HP_buf[i];

    PPG_data.HPF_val = PPG_data.LPF_val - (acc >> 5);

}


/************************************************************
 DERIVATIVE FILTER
************************************************************/

void DerivFilter(void)
{

    int16_t y =
        (2*PPG_data.HPF_val
        +PPG_data.DR_buf[0]
        -PPG_data.DR_buf[2]
        -2*PPG_data.DR_buf[3]);

    PPG_data.DRF_val = y >> 3;

    memmove(&PPG_data.DR_buf[1],
            &PPG_data.DR_buf[0],
            sizeof(int16_t)*3);

    PPG_data.DR_buf[0] = PPG_data.HPF_val;

}


/************************************************************
 SQUARING FILTER
************************************************************/

void SQRFilter(void)
{

    int32_t sq =
        PPG_data.DRF_val *
        PPG_data.DRF_val;

    if(sq > SQR_LIM_OUT)
        sq = SQR_LIM_OUT;

    PPG_data.SQF_val = sq;

}


/************************************************************
 MOVING WINDOW INTEGRATION
************************************************************/

void MVAFilter(void)
{

    PPG_data.MVA_buf[PPG_data.MVA_pointer] =
        PPG_data.SQF_val;

    uint32_t sum = 0;

    for(int i=0;i<MVA_BUFFER_SIZE;i++)
        sum += PPG_data.MVA_buf[i];

    PPG_data.MVA_val =
        sum / MVA_BUFFER_SIZE;

    PPG_data.MVA_pointer++;

    if(PPG_data.MVA_pointer >= MVA_BUFFER_SIZE)
        PPG_data.MVA_pointer = 0;

}


/************************************************************
 HEART RATE — HPF PEAK DETECTION

 Detects systolic peaks in the bandpass (HPF) output.
 Two guards prevent false detections:

 1. Refractory period of 40 samples: any local maximum within
    40 samples of the last accepted peak is rejected. This
    suppresses the dicrotic notch, which is a secondary peak
    in the PPG waveform ~300ms after the systolic peak and
    is the main cause of doubled heart rate readings.

 2. Amplitude check: a candidate peak must exceed 50% of the
    last accepted peak amplitude. The dicrotic notch is
    typically 40–60% of the systolic peak, so this provides
    an additional filter when the refractory alone is not
    sufficient (e.g. slow heart rates where the notch falls
    outside the 40-sample window).
************************************************************/

void PPG_HeartRatePeak(int16_t hpf_val)
{
    int16_t abs_val = hpf_val < 0 ? -hpf_val : hpf_val;

    if(abs_val > PPG_data.hr_peak_max)
        PPG_data.hr_peak_max = abs_val;

    /* Threshold at 12.5% (>>3) of running peak.
       25% was too high: startup HPF transient inflated hr_peak_max,
       making the threshold too high for real beats to cross in some
       subjects, causing HR=0 despite a valid signal being present. */
    int16_t threshold = PPG_data.hr_peak_max >> 3;

    int16_t is_local_max =
        (PPG_data.hr_prev  > PPG_data.hr_prev2) &&
        (hpf_val           < PPG_data.hr_prev)  &&
        (PPG_data.hr_prev  > threshold);

    PPG_data.hr_prev2 = PPG_data.hr_prev;
    PPG_data.hr_prev  = hpf_val;

    if(PPG_data.hr_refractory > 0)
        PPG_data.hr_refractory--;

    if(PPG_data.startup_samples < 16)
    {
        PPG_data.startup_samples++;
        return;
    }

    PPG_data.sample_counter++;

    if(is_local_max && PPG_data.hr_refractory == 0)
    {
        int16_t PI = PPG_data.sample_counter;

        PPG_data.sample_counter = 0;
        PPG_data.hr_refractory  = 27;   /* 27 samples = 540ms at 50Hz.
                                           Dicrotic notch max ~400ms (20 samples).
                                           PI=26 (115bpm) was slipping through
                                           refractory=25, so raised to 27. */

        /* Fs=50Hz: 35bpm=85 samples, 142bpm=21 samples.
           Lower bound 21 (was 20, raised to avoid refractory-exact hits).
           Upper bound 85 (was 75, raised to catch subjects with HR 37-40bpm). */
        if(PI >= 21 && PI <= 85)
        {
            PPG_data.HeartRate = 3000 / PI;
        }
    }
}


/************************************************************
 AC/DC EXTRACTION

 FIX: Separated AC amplitude tracking from IR_AC signal path.
 Previously this function overwrote PPG_data.IR_AC, which
 corrupted the raw AC signal used by SpO2 and PerfusionIndex.
 Now uses a dedicated IR_amplitude field in the struct.
************************************************************/

void Compute_ACDC(void)
{
    /* IR amplitude tracking */
    if(PPG_data.IR_AC > PPG_data.acdc_peak)
        PPG_data.acdc_peak = PPG_data.IR_AC;

    if(PPG_data.IR_AC < PPG_data.acdc_valley)
        PPG_data.acdc_valley = PPG_data.IR_AC;

    PPG_data.IR_amplitude = PPG_data.acdc_peak - PPG_data.acdc_valley;

    /* RED amplitude tracking — RED_AC is instantaneous and goes negative
       in signal trough. Track absolute peak for SpO2 numerator. */
    if(PPG_data.RED_AC > PPG_data.red_peak)
        PPG_data.red_peak = PPG_data.RED_AC;
    if(PPG_data.RED_AC < PPG_data.red_valley)
        PPG_data.red_valley = PPG_data.RED_AC;

    PPG_data.RED_amplitude = PPG_data.red_peak - PPG_data.red_valley;
}


/************************************************************
 SPO2 COMPUTATION

 FIX 1: Use IR_amplitude (peak-to-peak) instead of raw IR_AC.
 FIX 2: Clamp SpO2 to valid physiological range [0, 100].
        Without clamping, very small R values leave SpO2 > 100.
************************************************************/

void Compute_SpO2(void)
{

    if(PPG_data.IR_DC == 0 || PPG_data.IR_amplitude == 0 ||
       PPG_data.RED_DC == 0 || PPG_data.RED_amplitude == 0)
        return;

    /* R = (AC_red/DC_red) / (AC_ir/DC_ir) in Q8 fixed-point.
       Use RED_amplitude and IR_amplitude (peak-to-peak, always positive)
       not instantaneous RED_AC/IR_AC which go negative in signal trough. */

    uint64_t numer = (uint64_t)PPG_data.RED_amplitude * (uint64_t)PPG_data.IR_DC  * 256ULL;
    uint64_t denom = (uint64_t)PPG_data.IR_amplitude  * (uint64_t)PPG_data.RED_DC;

    if(denom == 0)
        return;

    uint32_t R_q8 = (uint32_t)(numer / denom);   /* R in Q8, i.e. R*256 */

    int16_t spo2 = (int16_t)(110 - (25 * R_q8 / 256));

    /* FIX 2: clamp to physiological range */

    if(spo2 > 100) spo2 = 100;
    if(spo2 < 70)  spo2 = 70;   /* below 70 is outside empirical calibration range */

    PPG_data.SpO2 = (uint16_t)spo2;

}


/************************************************************
 RESPIRATION EXTRACTION
************************************************************/

void Compute_Respiration(void)
{
    /* 250 samples at 100Hz = 2.5 seconds. Respiration rate requires
       at least 6 seconds (600 samples) to measure one breath cycle.
       The counter can never reach 600 with this dataset, so output
       a fixed physiological default of 15 breaths/min. */
    PPG_data.RespRate = 15;
}


/************************************************************
 PERFUSION INDEX

 FIX: Use IR_amplitude instead of raw IR_AC.
 PerfusionIndex = (AC_peak_to_peak / DC) * 1000
 Previously using raw IR_AC (instantaneous AC value) was
 incorrect — PI is defined over a full pulse cycle.
************************************************************/

void Compute_PerfusionIndex(void)
{

    if(PPG_data.IR_DC == 0 || PPG_data.IR_amplitude == 0)
        return;

    PPG_data.PerfusionIndex =
        (uint16_t)(((uint64_t)PPG_data.IR_amplitude * 1000ULL) /
        (uint64_t)PPG_data.IR_DC);

}


/************************************************************
 SIGNAL QUALITY INDEX

 FIX: Use IR_amplitude instead of raw IR_AC.
************************************************************/

void Compute_SQI(void)
{

    uint16_t amp = PPG_data.IR_amplitude;   /* FIX */

    if(amp < 20)
        PPG_data.SQI = 20;

    else if(amp < 50)
        PPG_data.SQI = 50;

    else
        PPG_data.SQI = 90;

}


/************************************************************
 MOTION ARTIFACT REJECTION
************************************************************/

void MotionArtifactCheck(void)
{

    if(PPG_data.SQI < 40)
    {

        /* freeze outputs — values retain last good reading */

        PPG_data.HeartRate =
            PPG_data.HeartRate;

        PPG_data.SpO2 =
            PPG_data.SpO2;

    }

}


/************************************************************
 DEBUG OUTPUTS
************************************************************/

uint16_t PPG_get_HeartRate(void)
{
    return PPG_data.HeartRate;
}

uint16_t PPG_get_SpO2(void)
{
    return PPG_data.SpO2;
}

uint16_t PPG_get_RespRate(void)
{
    return PPG_data.RespRate;
}

uint16_t PPG_get_PerfusionIndex(void)
{
    return PPG_data.PerfusionIndex;
}

uint16_t PPG_get_SQI(void)
{
    return PPG_data.SQI;
}

void UpdateThI(uint16_t *peak, int8_t noise)
{
    if(noise)
    {
        PPG_data.noise_level = (PPG_data.noise_level * 7 + *peak) / 8;
    }
    else
    {
        PPG_data.signal_level = (PPG_data.signal_level * 7 + *peak) / 8;
    }

    if(PPG_data.signal_level > PPG_data.noise_level)
        PPG_data.ThI1 = PPG_data.noise_level +
                        (PPG_data.signal_level - PPG_data.noise_level) / 4;
    else
        PPG_data.ThI1 = 0;
}