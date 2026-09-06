# ==========================================================================
#  OblivRec build.
#
#  Hand-written rather than CMake: cmake is not installed on the dev box and
#  installing it would breach the standing no-new-dependencies rule
#  (RULES.md A7). REQUIREMENTS.md section 9 specifies CMake; amended by a
#  Decisions Log entry.
#
#  RUN AS:   mingw32-make <target>
#  with /c/msys64/mingw64/bin on PATH. If it is not, g++ cannot spawn as/ld
#  and fails SILENTLY, producing no binary and no error. The check-toolchain
#  target below turns that into a loud failure and is a prerequisite of
#  everything else.
# ==========================================================================

CXX      := g++
STD      := -std=gnu++17
OPT      := -O2 -maes -msse4.1
WARN     := -Wall -Wextra -Wpedantic
INC      := -Iinclude
GIT_SHA  := $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)
CXXFLAGS := $(STD) $(OPT) $(WARN) $(INC) -DOBLIVREC_GIT_SHA=\"$(GIT_SHA)\"
PY       := py -3.13

BUILD    := build
SRC      := $(wildcard src/common/*.cpp src/dpf/*.cpp src/pir/*.cpp src/serve/*.cpp src/net/*.cpp)
OBJ      := $(patsubst %.cpp,$(BUILD)/%.o,$(SRC))

TESTSRC  := $(wildcard tests/test_*.cpp)
TESTBIN  := $(patsubst tests/%.cpp,$(BUILD)/%.exe,$(TESTSRC))

.PHONY: all test bench figures clean check-toolchain

all: check-toolchain $(TESTBIN)

# --------------------------------------------------------------------------
# The guard. Compiles a 3-line translation unit and hard-fails if no object
# file appears, which is exactly the symptom of the silent as/ld spawn
# failure described above.
# --------------------------------------------------------------------------
check-toolchain:
	@mkdir -p $(BUILD)
	@printf 'int f(){return 0;}\n' > $(BUILD)/_probe.cpp
	@$(CXX) $(STD) -c $(BUILD)/_probe.cpp -o $(BUILD)/_probe.o 2>/dev/null || true
	@test -f $(BUILD)/_probe.o || { \
	  echo ""; \
	  echo "TOOLCHAIN BROKEN: g++ produced no object file."; \
	  echo "Almost certainly /c/msys64/mingw64/bin is not on PATH, so g++"; \
	  echo "cannot spawn as/ld. It fails silently with no diagnostic."; \
	  echo "Fix:  export PATH=\"/c/msys64/mingw64/bin:\$$PATH\""; \
	  echo ""; exit 1; }
	@rm -f $(BUILD)/_probe.cpp $(BUILD)/_probe.o
	@echo "toolchain ok: $$($(CXX) --version | head -1)"

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.exe: tests/%.cpp $(OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $< $(OBJ) -o $@

test: all
	@fail=0; \
	for t in $(TESTBIN); do \
	  echo "--- $$t"; \
	  ./$$t || fail=1; \
	done; \
	if [ $$fail -ne 0 ]; then echo ""; echo "TESTS FAILED"; exit 1; fi; \
	echo ""; echo "all tests passed"

figures:
	$(PY) bench/scripts/make_figures.py

clean:
	rm -rf $(BUILD)
