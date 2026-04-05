/* Auto-generated from DecisionTreeClassifier */
/* Features: RR_mean_ms, HR_bpm, SDNN_ms, RMSSD_ms, mean_QRS_ms, SQI_q10 */

const char* ecg_classify(float RR_mean_ms, float HR_bpm, float SDNN_ms, float RMSSD_ms, float mean_QRS_ms, float SQI_q10)
{
    if (RR_mean_ms <= 1.331960f) {
        if (RR_mean_ms <= -1.270778f) {
            return "Tachycardia";
        } else {
            if (RMSSD_ms <= -0.852380f) {
                return "Low_HRV";
            } else {
                return "Normal";
            }
        }
    } else {
        return "Bradycardia";
    }
}