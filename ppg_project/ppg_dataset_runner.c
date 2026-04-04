#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ppgalgorithm.h"

#define MAX_LINE     256
#define NUM_SUBJECTS 68

/* Store all results before writing so we can impute HR=0 with dataset mean */
typedef struct {
    int      subject_id;
    uint16_t HeartRate;
    uint16_t SpO2;
    uint16_t RespRate;
    uint16_t PerfusionIndex;
    uint16_t RED_amplitude;
    uint16_t IR_amplitude;
} SubjectResult;

static SubjectResult results[NUM_SUBJECTS];


void process_subject(int subject_id, int idx)
{
    char filename[128];
    char line[MAX_LINE];
    int  red, ir;

    sprintf(filename, "cleaned_ppg/%d_clean.csv", subject_id);

    FILE *fp = fopen(filename, "r");
    if(!fp)
    {
        printf("Failed to open %s\n", filename);
        results[idx].subject_id    = subject_id;
        results[idx].HeartRate     = 0;
        results[idx].SpO2          = 0;
        results[idx].RespRate      = 0;
        results[idx].PerfusionIndex = 0;
        results[idx].RED_amplitude = 0;
        results[idx].IR_amplitude  = 0;
        return;
    }

    fgets(line, MAX_LINE, fp);   /* skip header */

    PPG_init();

    if(!fgets(line, MAX_LINE, fp))
    {
        fclose(fp);
        return;
    }

    if(sscanf(line, "%d,%d", &red, &ir) == 2)
    {
        int16_t dc_est = (int16_t)((red + ir) / 2);
        PPG_prewarm_DC(dc_est);
        PPG_StateMachine((int16_t)red, (int16_t)ir);
    }

    while(fgets(line, MAX_LINE, fp))
    {
        if(sscanf(line, "%d,%d", &red, &ir) != 2)
            continue;
        PPG_StateMachine((int16_t)red, (int16_t)ir);
    }

    fclose(fp);

    results[idx].subject_id     = subject_id;
    results[idx].HeartRate      = PPG_data.HeartRate;
    results[idx].SpO2           = PPG_data.SpO2;
    results[idx].RespRate       = PPG_data.RespRate;
    results[idx].PerfusionIndex = PPG_data.PerfusionIndex;
    results[idx].RED_amplitude  = PPG_data.RED_amplitude;
    results[idx].IR_amplitude   = PPG_data.IR_amplitude;
}


int main(void)
{
    /* Pass 1: process all subjects */
    for(int i = 0; i < NUM_SUBJECTS; i++)
    {
        printf("Processing subject %d\n", i + 1);
        process_subject(i + 1, i);
    }

    /* Pass 2: compute mean HR from valid (non-zero) subjects */
    uint32_t hr_sum   = 0;
    int      hr_count = 0;

    for(int i = 0; i < NUM_SUBJECTS; i++)
    {
        if(results[i].HeartRate > 0)
        {
            hr_sum += results[i].HeartRate;
            hr_count++;
        }
    }

    uint16_t hr_mean = (hr_count > 0) ? (uint16_t)(hr_sum / hr_count) : 75;

    printf("\nDataset HR mean (from %d valid subjects): %u bpm\n",
           hr_count, hr_mean);

    int imputed = 0;
    for(int i = 0; i < NUM_SUBJECTS; i++)
    {
        if(results[i].HeartRate == 0)
        {
            results[i].HeartRate = hr_mean;
            imputed++;
        }
    }

    if(imputed > 0)
        printf("Imputed HR=0 for %d subjects with mean %u bpm\n",
               imputed, hr_mean);

    /* Pass 3: write output */
    FILE *out = fopen("ppg_ml_dataset.csv", "w");
    if(!out)
    {
        printf("Cannot create output file\n");
        return -1;
    }

    fprintf(out,
            "Subject,HeartRate,SpO2,RespRate,PerfusionIndex,RedAC,IRAC\n");

    for(int i = 0; i < NUM_SUBJECTS; i++)
    {
        fprintf(out, "%d,%u,%u,%u,%u,%u,%u\n",
                results[i].subject_id,
                results[i].HeartRate,
                results[i].SpO2,
                results[i].RespRate,
                results[i].PerfusionIndex,
                results[i].RED_amplitude,
                results[i].IR_amplitude);
    }

    fclose(out);
    printf("\nDataset generation complete\n");
    return 0;
}