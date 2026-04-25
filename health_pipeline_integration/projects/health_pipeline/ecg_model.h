#ifndef ECG_MODEL_H
#define ECG_MODEL_H

/* Auto-generated ECG decision tree classifier.
   Input features must be z-scored using ECG training set scalers.
   Features: RR_mean_ms, HR_bpm, SDNN_ms, RMSSD_ms, mean_QRS_ms, SQI_q10 */

const char* ecg_classify(float RR_mean_ms, float HR_bpm, float SDNN_ms,
                          float RMSSD_ms,   float mean_QRS_ms, float SQI_q10);

#endif
