TEST ?= test
OUT_DIR ?= ../out

.PHONY: test analysis optimization run-test run-analysis run-optimization clean

test:
	@$(MAKE) -C .. TEST=$(TEST) OUT_DIR=$(OUT_DIR) test

analysis:
	@$(MAKE) -C .. TEST=$(TEST) OUT_DIR=$(OUT_DIR) analysis

optimization:
	@$(MAKE) -C .. TEST=$(TEST) OUT_DIR=$(OUT_DIR) optimization

run-test:
	@$(MAKE) -C .. TEST=$(TEST) OUT_DIR=$(OUT_DIR) run-test

run-analysis:
	@$(MAKE) -C .. TEST=$(TEST) OUT_DIR=$(OUT_DIR) run-analysis

run-optimization:
	@$(MAKE) -C .. TEST=$(TEST) OUT_DIR=$(OUT_DIR) run-optimization

clean:
	@$(MAKE) -C .. OUT_DIR=$(OUT_DIR) clean
