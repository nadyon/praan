"""
Add this as a new cell at the TOP of ml_pipeline.ipynb (after imports).
It reads the feature vector printed by ecg_pipeline / ppg_pipeline
and runs it through the trained models.

Workflow:
  1. Flash ecg_monitor.shakti  → capture with ecg_capture.py
  2. Flash max30102_demo.shakti → capture with ppg_capture.py
  3. Run:  cd health_pipeline && ./ecg_pipeline ../scripts/uart_logs/ecg_*.txt
  4. Run:  cd health_pipeline && ./ppg_pipeline ../scripts/uart_logs/ppg_*.txt
  5. Paste the output lines into ecg_output / ppg_output below, then run this cell.
"""

# ── paste the full terminal output from ecg_pipeline here ──────────────────
ecg_output = """
ECG_FEATURES: RR_mean_ms=810.50 HR_bpm=74.02 SDNN_ms=32.00 RMSSD_ms=32.00 mean_QRS_ms=206.00 SQI_q10=1030.00
ECG_SCALED:   0.031512 -0.208096 -0.706289 -0.808553 -0.209095 0.782340
ECG_CLASS:    Normal
"""

# ── paste the full terminal output from ppg_pipeline here ──────────────────
ppg_output = """
PPG_FEATURES: HeartRate=76 SpO2=93 RespRate=15 PerfusionIndex=164 RedAC=1584 IRAC=2315
PPG_SCALED:   0.034499 1.710248 0.000000 0.017783 -1.530327 -0.161028
PPG_CLASS:    Normal
"""

# ── parser ─────────────────────────────────────────────────────────────────
import re
import numpy as np

def parse_pipeline_output(text, tag_features, tag_scaled, tag_class):
    feat_line   = next(l for l in text.splitlines() if tag_features in l)
    scaled_line = next(l for l in text.splitlines() if tag_scaled   in l)
    class_line  = next(l for l in text.splitlines() if tag_class    in l)

    # raw features as dict
    pairs   = re.findall(r'(\w+)=([\d.]+)', feat_line)
    raw     = {k: float(v) for k, v in pairs}

    # scaled as numpy array
    nums    = list(map(float, scaled_line.split(':')[1].split()))
    scaled  = np.array(nums).reshape(1, -1)

    # class label
    label   = class_line.split(':')[1].strip()

    return raw, scaled, label

ecg_raw, ecg_scaled_vec, ecg_class_c = parse_pipeline_output(
    ecg_output, 'ECG_FEATURES', 'ECG_SCALED', 'ECG_CLASS')

ppg_raw, ppg_scaled_vec, ppg_class_c = parse_pipeline_output(
    ppg_output, 'PPG_FEATURES', 'PPG_SCALED', 'PPG_CLASS')

# ── run through sklearn models (uses the already-trained ecg_model / ppg_model)
ecg_class_sk = ecg_model.predict(ecg_scaled_vec)[0]
ppg_class_sk = ppg_model.predict(ppg_scaled_vec)[0]

decision = final_decision(ecg_class_sk, ppg_class_sk)

print('=' * 60)
print('LIVE SENSOR INFERENCE')
print('=' * 60)
print(f'ECG raw  : {ecg_raw}')
print(f'PPG raw  : {ppg_raw}')
print(f'ECG class (C tree) : {ecg_class_c}')
print(f'ECG class (sklearn): {ecg_class_sk}')
print(f'PPG class (C tree) : {ppg_class_c}')
print(f'PPG class (sklearn): {ppg_class_sk}')
print(f'FINAL DECISION     : {decision}')
