/* Auto-generated from DecisionTreeClassifier */
/* Features: HeartRate, SpO2, RespRate, PerfusionIndex, RedAC, IRAC */

const char* ppg_classify(float HeartRate, float SpO2, float RespRate, float PerfusionIndex, float RedAC, float IRAC)
{
    if (HeartRate <= -0.884545f) {
        return "Bradycardia";
    } else {
        if (SpO2 <= -0.496826f) {
            return "Low_SpO2";
        } else {
            if (HeartRate <= 1.479392f) {
                if (PerfusionIndex <= -0.779080f) {
                    return "Low_Perfusion";
                } else {
                    return "Normal";
                }
            } else {
                return "Tachycardia";
            }
        }
    }
}