# MIT-BIH Arrhythmia Project

## Setup Instructions

### Prerequisites
- Python 3.x
- pip

### Installation

1. Clone the repository:
```bash
git clone https://github.com/YOUR_USERNAME/mitbih_project.git
cd mitbih_project
```

2. Create a virtual environment:
```bash
python3 -m venv venv
```

3. Activate the virtual environment:
```bash
source venv/bin/activate  # On Linux/Mac
# or
venv\Scripts\activate  # On Windows
```

4. Install dependencies:
```bash
pip install -r requirements.txt
```

### Usage

For Pan-Tompkins algorithm implementation and usage instructions:
```bash
cd Pan-Tompkins-FixedPoint
```

Refer to the README or documentation in the `Pan-Tompkins-FixedPoint` directory for specific usage details.

## Project Structure

- `01_explore_mitbih.ipynb` - Jupyter notebook for exploring the MIT-BIH dataset
- `cleaned_ecg/` - Cleaned ECG data
- `data/` - Raw data files
- `Pan-Tompkins-FixedPoint/` - Pan-Tompkins algorithm implementation
