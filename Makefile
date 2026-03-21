CC ?= gcc
CXX ?= g++
CUDA_HOME ?= /usr/local/cuda
NVCC := $(CUDA_HOME)/bin/nvcc
SHELL := /bin/bash

SRC_DIR := src
TEST_DIR := tests
OUT_DIR ?= out
TEST ?= test
BENCHMARK_TESTS ?= test_cyclic test_offsets test_pinned test_fallbacks

CUDA_INCLUDE := $(CUDA_HOME)/include
TEST_SRC := $(TEST_DIR)/$(TEST).cu
BUILD_DIR := $(OUT_DIR)/$(TEST)
BASE_BIN := $(BUILD_DIR)/$(TEST)
ANALYZER_OBJ := $(BUILD_DIR)/xzx_analyzer.o
OPTIMIZER_OBJ := $(BUILD_DIR)/xzx_optimizer.o

.PHONY: test baseline analysis optimization run-test run-analysis run-optimization benchmark benchmark-all clean

test: $(BASE_BIN)

baseline: $(BASE_BIN)
	@cd $(BUILD_DIR) && python3 -c "import subprocess,time; t=time.perf_counter(); \
subprocess.run(['./$(TEST)'], stdout=open('baseline_output.txt','w'), stderr=subprocess.STDOUT, check=True); \
print('elapsed=%.6f' % (time.perf_counter()-t))" > baseline_metrics.txt

analysis: $(BUILD_DIR)/$(TEST)_analysis
	@cp $(SRC_DIR)/info.cfg $(BUILD_DIR)/info.cfg
	@cd $(BUILD_DIR) && python3 -c "import subprocess,time; t=time.perf_counter(); \
subprocess.run(['./$(TEST)_analysis'], stdout=open('analysis_output.txt','w'), stderr=subprocess.STDOUT, check=True); \
print('elapsed=%.6f' % (time.perf_counter()-t))" > analysis_metrics.txt

optimization: $(BUILD_DIR)/$(TEST)_optimization

run-test: $(BASE_BIN)
	@cd $(BUILD_DIR) && ./$(TEST)

run-analysis: analysis

run-optimization: $(BUILD_DIR)/$(TEST)_optimization
	@cd $(BUILD_DIR) && python3 -c "import subprocess,time; t=time.perf_counter(); \
subprocess.run(['./$(TEST)_optimization'], stdout=open('optimization_output.txt','w'), stderr=subprocess.STDOUT, check=True); \
print('elapsed=%.6f' % (time.perf_counter()-t))" > optimization_metrics.txt

benchmark: $(BASE_BIN) $(BUILD_DIR)/$(TEST)_analysis $(BUILD_DIR)/$(TEST)_optimization
	@cp $(SRC_DIR)/info.cfg $(BUILD_DIR)/info.cfg
	@cd $(BUILD_DIR) && python3 -c "import subprocess,time; t=time.perf_counter(); \
subprocess.run(['./$(TEST)'], stdout=open('baseline_output.txt','w'), stderr=subprocess.STDOUT, check=True); \
print('elapsed=%.6f' % (time.perf_counter()-t))" > baseline_metrics.txt
	@cd $(BUILD_DIR) && python3 -c "import subprocess,time; t=time.perf_counter(); \
subprocess.run(['./$(TEST)_analysis'], stdout=open('analysis_output.txt','w'), stderr=subprocess.STDOUT, check=True); \
print('elapsed=%.6f' % (time.perf_counter()-t))" > analysis_metrics.txt
	@cd $(BUILD_DIR) && python3 -c "import subprocess,time; t=time.perf_counter(); \
subprocess.run(['./$(TEST)_optimization'], stdout=open('optimization_output.txt','w'), stderr=subprocess.STDOUT, check=True); \
print('elapsed=%.6f' % (time.perf_counter()-t))" > optimization_metrics.txt
	@cd $(BUILD_DIR) && { \
		echo "test=$(TEST)"; \
		echo "[baseline]"; cat baseline_metrics.txt; \
		echo "[analysis]"; cat analysis_metrics.txt; \
		echo "[optimization]"; cat optimization_metrics.txt; \
	} > benchmark_summary.txt

benchmark-all:
	@mkdir -p $(OUT_DIR)
	@rm -f $(OUT_DIR)/benchmark_all_summary.txt
	@for test_name in $(BENCHMARK_TESTS); do \
		$(MAKE) TEST=$$test_name OUT_DIR=$(OUT_DIR) benchmark; \
		{ \
			echo "test=$$test_name"; \
			sed -n '1p' $(OUT_DIR)/$$test_name/baseline_metrics.txt; \
			sed -n '1p' $(OUT_DIR)/$$test_name/analysis_metrics.txt; \
			sed -n '1p' $(OUT_DIR)/$$test_name/optimization_metrics.txt; \
			echo "---"; \
		} >> $(OUT_DIR)/benchmark_all_summary.txt; \
	done

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BASE_BIN): $(TEST_SRC) | $(BUILD_DIR)
	@$(NVCC) -I$(SRC_DIR) -I$(TEST_DIR) $(TEST_SRC) -o $(BASE_BIN) -lcudart

$(ANALYZER_OBJ): $(SRC_DIR)/xzx_analyzer.cpp | $(BUILD_DIR)
	@$(CXX) -I$(CUDA_INCLUDE) -I$(SRC_DIR) -c $(SRC_DIR)/xzx_analyzer.cpp -o $(ANALYZER_OBJ)

$(OPTIMIZER_OBJ): $(SRC_DIR)/xzx_optimizer.cpp | $(BUILD_DIR)
	@$(CXX) -I$(CUDA_INCLUDE) -I$(SRC_DIR) -c $(SRC_DIR)/xzx_optimizer.cpp -o $(OPTIMIZER_OBJ)

$(BUILD_DIR)/$(TEST)_analysis: $(SRC_DIR)/allocator_status.cpp $(SRC_DIR)/allocator_status.h $(SRC_DIR)/xzx_analyzer.h $(SRC_DIR)/xerozerox.h $(TEST_SRC) $(ANALYZER_OBJ) | $(BUILD_DIR)
	@$(NVCC) -I$(SRC_DIR) -I$(TEST_DIR) -DXZX_ANALYZE $(SRC_DIR)/allocator_status.cpp $(ANALYZER_OBJ) $(TEST_SRC) -o $(BUILD_DIR)/$(TEST)_analysis -lcudart

$(BUILD_DIR)/$(TEST)_optimization: $(SRC_DIR)/xzx_optimizer.h $(SRC_DIR)/xerozerox.h $(TEST_SRC) $(OPTIMIZER_OBJ) | $(BUILD_DIR)
	@$(NVCC) -I$(SRC_DIR) -I$(TEST_DIR) -DXZX_OPTIMIZE $(OPTIMIZER_OBJ) $(TEST_SRC) -o $(BUILD_DIR)/$(TEST)_optimization -lcudart

clean:
	@rm -rf $(OUT_DIR)
