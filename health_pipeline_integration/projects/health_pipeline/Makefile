# Makefile for health_pipeline
# Host-side feature extraction + ML inference (gcc, NOT riscv-gcc)
# Runs on your laptop/PC reading UART log files from the FPGA.
#
# Targets:
#   make ecg       — build ./ecg_pipeline
#   make ppg       — build ./ppg_pipeline
#   make all       — build both
#   make clean     — remove build artefacts

CC     = gcc
CFLAGS = -Wall -O2 -std=gnu99 -I.
LIBS   = -lm

ECG_SRCS = ecg_pipeline.c PanTompkins.c ecg_model.c
PPG_SRCS = ppg_pipeline.c ppgalgorithm.c ppg_model.c

ECG_OBJS = $(ECG_SRCS:.c=.o)
PPG_OBJS = $(PPG_SRCS:.c=.o)

.PHONY: all ecg ppg clean

all: ecg ppg
	@echo ""
	@echo "Build complete."
	@echo "  ECG pipeline: ./ecg_pipeline <uart_logs/ecg_*.txt>"
	@echo "  PPG pipeline: ./ppg_pipeline <uart_logs/ppg_*.txt>"

ecg: $(ECG_OBJS)
	@$(CC) $(CFLAGS) -o ecg_pipeline $(ECG_OBJS) $(LIBS)
	@echo "Built: ecg_pipeline"

ppg: $(PPG_OBJS)
	@$(CC) $(CFLAGS) -o ppg_pipeline $(PPG_OBJS) $(LIBS)
	@echo "Built: ppg_pipeline"

%.o: %.c
	@$(CC) $(CFLAGS) -c $< -o $@

clean:
	@rm -f *.o ecg_pipeline ppg_pipeline
	@echo "Cleaned."
