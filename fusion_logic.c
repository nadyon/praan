/* Late fusion decision logic */
#include <string.h>

typedef enum {
    DECISION_NORMAL,
    DECISION_CARDIAC_ALERT,
    DECISION_HRV_ALERT,
    DECISION_OXYGEN_ALERT,
    DECISION_CIRCULATION_ALERT,
    DECISION_DUAL_ALERT,
    DECISION_CRITICAL,
    DECISION_CHECK_SIGNAL
} FusionDecision;

FusionDecision fusion_decide(const char *ecg, const char *ppg)
{
    int ea = strcmp(ecg, "Normal") != 0;
    int pa = strcmp(ppg, "Normal") != 0;

    if (ea && strcmp(ppg, "Low_SpO2") == 0)      return DECISION_CRITICAL;
    if (strcmp(ecg,"Tachycardia")==0 && strcmp(ppg,"Tachycardia")==0) return DECISION_CRITICAL;
    if (strcmp(ecg,"Bradycardia")==0 && strcmp(ppg,"Bradycardia")==0) return DECISION_CRITICAL;
    if (ea && pa)                                 return DECISION_DUAL_ALERT;
    if (strcmp(ecg,"Tachycardia")==0 || strcmp(ecg,"Bradycardia")==0) return DECISION_CARDIAC_ALERT;
    if (strcmp(ecg,"Low_HRV")==0)                return DECISION_HRV_ALERT;
    if (strcmp(ppg,"Low_SpO2")==0)               return DECISION_OXYGEN_ALERT;
    if (strcmp(ppg,"Low_Perfusion")==0)          return DECISION_CIRCULATION_ALERT;
    if (strcmp(ppg,"Tachycardia")==0 || strcmp(ppg,"Bradycardia")==0) return DECISION_CARDIAC_ALERT;
    if (strcmp(ecg,"Noisy")==0 || pa)            return DECISION_CHECK_SIGNAL;
    return DECISION_NORMAL;
}
