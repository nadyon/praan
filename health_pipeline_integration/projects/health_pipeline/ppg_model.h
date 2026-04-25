#ifndef PPG_MODEL_H
#define PPG_MODEL_H

/* Auto-generated PPG decision tree classifier.
   Input features must be z-scored using PPG training set scalers.
   Features: HeartRate, SpO2, RespRate, PerfusionIndex, RedAC, IRAC */

const char* ppg_classify(float HeartRate, float SpO2, float RespRate,
                          float PerfusionIndex, float RedAC, float IRAC);

#endif
