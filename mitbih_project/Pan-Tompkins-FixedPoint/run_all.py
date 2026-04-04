import os
import subprocess
import sys

# Directory containing cleaned ECG CSVs
INPUT_DIR = "../cleaned_ecg"
FS = "360"

# Path to pan_det executable
PAN_DET = "./pan_det"   # <-- IMPORTANT FIX

# Output folder
OUTDIR = "features_ecg"

def main():
    # Check if executable exists
    if not os.path.isfile(PAN_DET):
        print("ERROR: pan_det not found. Compile with:")
        print("       gcc -O3 pan_main.c PanTompkins.c -o pan_det")
        return

    # Ensure output folder exists
    os.makedirs(OUTDIR, exist_ok=True)

    print("Running Pan-Tompkins on all cleaned ECG files...\n")

    for fname in sorted(os.listdir(INPUT_DIR)):
        if not fname.endswith("_clean.csv"):
            continue

        record = fname.replace("_clean.csv", "")
        in_path = os.path.join(INPUT_DIR, fname)

        print(f"→ Processing {record} ...")

        cmd = [PAN_DET, in_path, FS]

        # Run the binary
        result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

        # Print C program output
        print(result.stdout)

        # Print any errors
        if result.stderr.strip():
            print("ERROR:", result.stderr)

    print("\n✔ Finished processing all ECG records.")
    print("Results saved inside features_ecg/all_ecg_features.csv")

if __name__ == "__main__":
    main()