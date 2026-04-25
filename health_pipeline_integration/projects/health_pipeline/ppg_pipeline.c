/*
 * ppg_pipeline.c
 *
 * Host-side PPG feature extraction pipeline.
 * Reads a UART log file (one "red,ir" pair per line),
 * runs the PPG algorithm (ppgalgorithm.c),
 * extracts 6 features, standardises them, and prints the
 * feature vector for the ML model.
 *
 * Compile (inside health_pipeline/):
 *   make ppg
 *
 * Usage:
 *   ./ppg_pipeline  <uart_log_file>
 *   ./ppg_pipeline  ../../scripts/uart_logs/ppg_20250101_120000.txt
 *
 * Output (stdout):
 *   PPG_FEATURES: HeartRate=75 SpO2=95 ...   (raw)
 *   PPG_SCALED:   0.123456 -0.456789 ...     (z-scored, 6 values)
 *   PPG_CLASS:    Normal                     (from embedded model)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ppgalgorithm.h"
#include "ppg_model.h"

/* ------------------------------------------------------------------ */
/* PPG preprocessing constants                                         */
/* Match ppg_preprocessing.ipynb: TARGET_DC=15000, TARGET_AC=2000     */
/* ------------------------------------------------------------------ */
#define TARGET_DC   15000
#define TARGET_AC   2000
#define MAX_SAMPLES 2000

/* ------------------------------------------------------------------ */
/* StandardScaler constants (fitted on Hb PPG training set)            */
/* Order: HeartRate, SpO2, RespRate, PerfusionIndex, RedAC, IRAC       */
/* ------------------------------------------------------------------ */
static const double PPG_MEAN[6] = {
    75.33823529411765,
    84.08823529411765,
    15.0,
    163.23529411764707,
    2500.235294117647,
    2416.455882352941
};
static const double PPG_STD[6] = {
    19.036041894350312,
    5.209536769848293,
    1.0,
    42.01789452373372,
    598.6781301001462,
    629.0641843672497
};

/* ------------------------------------------------------------------ */
/* Scale raw sensor values to match preprocessing notebook             */
/* ppg_preprocessing.ipynb: scale uniformly so max→30000,             */
/* then the C algorithm sees DC~15000 and AC~2000                      */
/* ------------------------------------------------------------------ */
static void scale_samples(int *red_raw, int *ir_raw, int n,
                           int16_t *red_out, int16_t *ir_out)
{
    /* Find max across both channels */
    int maxval = 1;
    for (int i = 0; i < n; i++) {
        if (red_raw[i] > maxval) maxval = red_raw[i];
        if (ir_raw[i]  > maxval) maxval = ir_raw[i];
    }

    for (int i = 0; i < n; i++) {
        int32_t r = (int32_t)red_raw[i] * 30000 / maxval;
        int32_t ir = (int32_t)ir_raw[i]  * 30000 / maxval;
        if (r  >  32767) r  =  32767;
        if (ir >  32767) ir =  32767;
        red_out[i] = (int16_t)r;
        ir_out[i]  = (int16_t)ir;
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <uart_log_file>\n", argv[0]);
        fprintf(stderr, "  e.g. %s ../../scripts/uart_logs/ppg_20250101_120000.txt\n", argv[0]);
        return 1;
    }

    /* ---- 1. Load UART log ---- */
    FILE *fp = fopen(argv[1], "r");
    if (!fp) {
        fprintf(stderr, "[ppg_pipeline] Cannot open: %s\n", argv[1]);
        return 1;
    }

    static int red_raw[MAX_SAMPLES];
    static int ir_raw[MAX_SAMPLES];
    int n_raw = 0;
    char line[64];

    while (fgets(line, sizeof(line), fp) && n_raw < MAX_SAMPLES) {
        int r, ir;
        if (sscanf(line, "%d,%d", &r, &ir) == 2) {
            red_raw[n_raw] = r;
            ir_raw[n_raw]  = ir;
            n_raw++;
        }
    }
    fclose(fp);

    if (n_raw < 50) {
        fprintf(stderr, "[ppg_pipeline] Too few samples (%d) in %s\n", n_raw, argv[1]);
        return 1;
    }
    printf("[ppg_pipeline] Loaded %d samples from %s\n", n_raw, argv[1]);

    /* ---- 2. Scale to int16 range (match preprocessing notebook) ---- */
    static int16_t red_scaled[MAX_SAMPLES];
    static int16_t ir_scaled[MAX_SAMPLES];
    scale_samples(red_raw, ir_raw, n_raw, red_scaled, ir_scaled);

    /* ---- 3. Run PPG algorithm ---- */
    PPG_init();

    /* Pre-warm DC filter with first sample value */
    int16_t dc_est = (int16_t)((red_scaled[0] + ir_scaled[0]) / 2);
    PPG_prewarm_DC(dc_est);

    /* Feed first sample (already consumed by prewarm conceptually,
       but algorithm still needs it in the state machine) */
    PPG_StateMachine(red_scaled[0], ir_scaled[0]);

    for (int i = 1; i < n_raw; i++) {
        PPG_StateMachine(red_scaled[i], ir_scaled[i]);
    }

    /* ---- 4. Extract features ---- */
    uint16_t hr   = PPG_get_HeartRate();
    uint16_t spo2 = PPG_get_SpO2();
    uint16_t rr   = PPG_get_RespRate();
    uint16_t pi   = PPG_get_PerfusionIndex();
    uint16_t red_amp = PPG_data.RED_amplitude;
    uint16_t ir_amp  = PPG_data.IR_amplitude;

    /* Fallback: if HR=0, use dataset mean */
    if (hr == 0) {
        hr = 75;
        printf("[ppg_pipeline] Warning: HR=0 detected, using fallback 75 bpm\n");
    }

    /* ---- 5. Print raw features ---- */
    printf("\n");
    printf("PPG_FEATURES: HeartRate=%u SpO2=%u RespRate=%u "
           "PerfusionIndex=%u RedAC=%u IRAC=%u\n",
           hr, spo2, rr, pi, red_amp, ir_amp);

    /* ---- 6. Standardise (z-score) ---- */
    double raw[6]    = { (double)hr,      (double)spo2,
                         (double)rr,      (double)pi,
                         (double)red_amp, (double)ir_amp };
    double scaled[6];
    for (int i = 0; i < 6; i++) {
        scaled[i] = (raw[i] - PPG_MEAN[i]) / PPG_STD[i];
    }

    printf("PPG_SCALED:   %.6f %.6f %.6f %.6f %.6f %.6f\n",
           scaled[0], scaled[1], scaled[2],
           scaled[3], scaled[4], scaled[5]);

    /* ---- 7. Classify using embedded decision tree ---- */
    const char *ppg_class = ppg_classify(
        (float)scaled[0], (float)scaled[1], (float)scaled[2],
        (float)scaled[3], (float)scaled[4], (float)scaled[5]);

    printf("PPG_CLASS:    %s\n\n", ppg_class);

    return 0;
}
