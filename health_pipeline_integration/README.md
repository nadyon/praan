# GC2025 Health Pipeline — End-to-End Workflow

## Directory layout (what you're building)

```
~/nadyon/gc2025A/
├── scripts/
│   ├── ecg_capture.py        ← captures ECG UART → uart_logs/
│   ├── ppg_capture.py        ← captures PPG UART → uart_logs/
│   └── uart_logs/            ← all raw captures land here (auto-created)
│
└── GCSDK/software/projects/
    ├── ecg_monitor/          (existing — unchanged)
    ├── max30102_demo/        (existing — unchanged)
    └── health_pipeline/      ← NEW: C inference engine
        ├── Makefile
        ├── ecg_pipeline.c    ← PanTompkins → 6 ECG features → ecg_classify()
        ├── ppg_pipeline.c    ← ppgalgorithm → 6 PPG features → ppg_classify()
        ├── ecg_model.h / ppg_model.h
        ├── PanTompkins.c / .h          (copy from praan)
        ├── ppgalgorithm.c / .h         (copy from praan)
        ├── ecg_model.c / ppg_model.c   (copy from praan)
        └── fusion_logic.c              (copy from praan)

~/nadyon/praan/
└── ml_pipeline.ipynb         ← add sensor_inference_cell.py as a new cell
```

---

## Step 0 — One-time setup (run once)

### Make scripts & paste 2 files

### Copy algorithm files into health_pipeline
```bash
cd ~/nadyon/gc2025A/GCSDK/software/projects/health_pipeline

# Copy praan algorithm files
cp ~/nadyon/praan/mitbih_project/Pan-Tompkins-FixedPoint/PanTompkins.c  .
cp ~/nadyon/praan/mitbih_project/Pan-Tompkins-FixedPoint/PanTompkins.h  .
cp ~/nadyon/praan/ppg_project/ppgalgorithm.c                            .
cp ~/nadyon/praan/ppg_project/ppgalgorithm.h                            .
cp ~/nadyon/praan/ecg_model.c                                           .
cp ~/nadyon/praan/ppg_model.c                                           .
cp ~/nadyon/praan/fusion_logic.c                                        .
```
### also copy all other files from health_pipeline zip (12 in total)

### Fix PanTompkins.h name conflict
PanTompkins.h defines LP_BUFFER_SIZE, HP_BUFFER_SIZE etc. which clash with
ppgalgorithm.h. The health_pipeline compiles them as separate binaries so
there is NO conflict — ecg_pipeline.c includes only PanTompkins.h and
ppg_pipeline.c includes only ppgalgorithm.h. Do NOT include both in the
same .c file.

### Build the C pipelines
```bash
cd ~/nadyon/gc2025A/GCSDK/software/projects/health_pipeline
make all
```
Expected output:
```
Built: ecg_pipeline
Built: ppg_pipeline
Build complete.
```

### Install pyserial (if not already)
pip install pyserial

## Step 1 — Flash and capture ECG

### Flash
```bash
cd ~/nadyon/gc2025A/GCSDK
make project PROGRAM=ecg_monitor TARGET=yamuna
# then load the .shakti binary onto the board via your usual flow
```

### Capture UART
```bash
cd ~/nadyon/gc2025A/scripts
python3 ecg_capture.py /dev/ttyUSB1
```
- Waits for board to stream data
- Filters out all non-float lines automatically (ADS1292R ID, init messages etc.)
- Saves to: `uart_logs/ecg_YYYYMMDD_HHMMSS.txt`
- Stops automatically when it sees `--- Done: 1250 samples ---`

---

## Step 2 — Flash and capture PPG

**Power-cycle the board first** (unplug USB, wait 3s, replug).

### Flash
```bash
cd ~/nadyon/gc2025A/GCSDK
make project PROGRAM=max30102_demo TARGET=yamuna
# load onto board
```

### Capture UART
```bash
cd ~/nadyon/gc2025A/scripts
python3 ppg_capture.py /dev/ttyUSB1
```
- Filters to only `red,ir` integer pairs
- Saves to: `uart_logs/ppg_YYYYMMDD_HHMMSS.txt`

---

## Step 3 — Run C pipelines (feature extraction)

```bash
cd ~/nadyon/gc2025A/GCSDK/software/projects/health_pipeline

# ECG pipeline
./ecg_pipeline ../../scripts/uart_logs/ecg_YYYYMMDD_HHMMSS.txt

# PPG pipeline
./ppg_pipeline ../../scripts/uart_logs/ppg_YYYYMMDD_HHMMSS.txt
```

### ECG output looks like:
```
[ecg_pipeline] Loaded 1250 samples from ...
[ecg_pipeline] Resampled to 1000 samples at 200 Hz
[ecg_pipeline] Pan-Tompkins detected 12 beats

ECG_FEATURES: RR_mean_ms=810.50 HR_bpm=74.02 SDNN_ms=32.00 RMSSD_ms=32.00 mean_QRS_ms=206.00 SQI_q10=1030.00
ECG_SCALED:   0.031512 -0.208096 -0.706289 -0.808553 -0.209095 0.782340
ECG_CLASS:    Normal
```

### PPG output looks like:
```
[ppg_pipeline] Loaded 250 samples from ...

PPG_FEATURES: HeartRate=76 SpO2=93 RespRate=15 PerfusionIndex=164 RedAC=1584 IRAC=2315
PPG_SCALED:   0.034499 1.710248 0.000000 0.017783 -1.530327 -0.161028
PPG_CLASS:    Normal
```

---

## Step 4 — Run ML model in notebook

1. Open `~/nadyon/praan/ml_pipeline.ipynb`
2. Run all existing cells (trains models, fits scalers)
3. Add the contents of `sensor_inference_cell.py` as a new cell at the end
4. Paste your ECG pipeline output into `ecg_output = """..."""`
5. Paste your PPG pipeline output into `ppg_output = """..."""`
6. Run the cell → get final decision

---

## Troubleshooting

| Problem | Fix |
|---|---|
| `No samples captured` | Check USB port: `ls /dev/ttyUSB*`. Try `ttyUSB0` |
| `Cannot open port` | `sudo chmod 666 /dev/ttyUSB1` or add user to `dialout`: `sudo usermod -aG dialout $USER` |
| `ADS1292R ID = 0x00` | Power-cycle board. ECG code resets pinmux but stale state from PPG can persist |
| `HR=0 from ppg_pipeline` | Normal for short/noisy recordings — pipeline substitutes 75 bpm mean |
| `make: riscv64-unknown-elf-gcc: not found` | You're building health_pipeline with wrong make. Use `make all` inside `health_pipeline/`, not GCSDK root |
| `LP_BUFFER_SIZE redefined` | You included both PanTompkins.h and ppgalgorithm.h in the same file — don't |
