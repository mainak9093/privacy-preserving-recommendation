# ==========================================================================
#  OblivRec build.
#
#  Hand-written rather than CMake: cmake is not installed on the dev box and
#  installing it would breach the standing no-new-dependencies rule
#  (RULES.md A7). REQUIREMENTS.md section 9 specifies CMake; amended by a
#  Decisions Log entry.
#
#  RUN AS:   mingw32-make <target>
#  with /c/msys64/mingw64/bin FIRST on PATH. Two separate reasons:
#
#    1. Without it g++ cannot spawn as/ld and fails SILENTLY, producing no
#       binary and no error. The check-toolchain target turns that into a loud
#       failure and is a prerequisite of everything else.
#
#    2. The hardened `check` binaries import libwinpthread-1.dll, because
#       _GLIBCXX_DEBUG's safe-iterator registry takes a mutex. Git for Windows
#       ships an incompatible copy at /mingw64/bin. If that one wins the search
#       order the binaries exit 127 before main() runs. The check recipe
#       detects exit 127 and says so rather than reporting a test failure.
#
#  TARGETS
#    all              build every test binary
#    test             run them (default coverage tier, about 1.7 s)
#    check            hardened build: UBSan trap mode, _GLIBCXX_DEBUG, checked Span
#    test-exhaustive  the full REQUIREMENTS section 7 sweep, about 15 minutes
#    bench            run benchmarks, appending JSONL to bench/results/
#    figures          regenerate figures (Day 6; guarded until the script exists)
#    clean
# ==========================================================================

CXX      := g++
STD      := -std=gnu++17
OPT      := -O2 -maes -msse4.1
WARN     := -Wall -Wextra -Wpedantic
INC      := -Iinclude
GIT_SHA  := $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)
CXXFLAGS := $(STD) $(OPT) $(WARN) $(INC) -DOBLIVREC_GIT_SHA=\"$(GIT_SHA)\"
PY       := py -3.13
# bcrypt is the Windows CNG CSPRNG used for DPF key material (see
# include/oblivrec/csprng.hpp). ws2_32 is for the TCP channel. Both are
# Windows system libraries, not third-party dependencies.
LDLIBS   := -lbcrypt -lws2_32

BUILD    := build
RESULTS  := bench/results
SRC      := $(wildcard src/common/*.cpp src/dpf/*.cpp src/pir/*.cpp src/serve/*.cpp src/net/*.cpp)
OBJ      := $(patsubst %.cpp,$(BUILD)/%.o,$(SRC))

TESTSRC  := $(wildcard tests/test_*.cpp)
TESTBIN  := $(patsubst tests/%.cpp,$(BUILD)/%.exe,$(TESTSRC))

BENCHSRC := $(wildcard bench/bench_*.cpp)
BENCHBIN := $(patsubst bench/%.cpp,$(BUILD)/bench/%.exe,$(BENCHSRC))

# Coverage bounds. The default suite stays fast; the full sweep is opt-in.
EXHAUSTIVE_BITS ?= 16
CHECK_BITS      ?= 8

.PHONY: all test test-exhaustive check bench figures clean check-toolchain FORCE

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
	$(CXX) $(CXXFLAGS) $< $(OBJ) -o $@ $(LDLIBS)

test: all
	@fail=0; \
	for t in $(TESTBIN); do \
	  echo "--- $$t"; \
	  ./$$t || fail=1; \
	done; \
	if [ $$fail -ne 0 ]; then echo ""; echo "TESTS FAILED"; exit 1; fi; \
	echo ""; echo "all tests passed"

# --------------------------------------------------------------------------
#  The full REQUIREMENTS section 7 sweep: every alpha against every x, to 16
#  domain bits, both rings. Roughly fifteen minutes.
#
#  Deliberately NOT part of `test`. A suite that takes fifteen minutes stops
#  being run, and then it stops catching anything. Run this after a meaningful
#  change to the DPF, and record the SHA, date and wall time in MEMORY.md.
# --------------------------------------------------------------------------
test-exhaustive: all
	@echo "full DPF sweep to domain_bits=$(EXHAUSTIVE_BITS). At 16 this is ~15 min."
	./$(BUILD)/test_dpf_exhaustive.exe $(EXHAUSTIVE_BITS)

# --------------------------------------------------------------------------
#  Hardened verification build.
#
#  GCC on mingw ships no libasan/libubsan, so -fsanitize=address cannot link
#  and plain -fsanitize=undefined cannot either. UBSan still works in TRAP
#  mode, which needs no runtime library: a violation becomes an illegal
#  instruction, so the test simply dies and the suite reports it.
#
#  _GLIBCXX_DEBUG covers std::vector and iterators. It does NOT cover
#  oblivrec::Span, which is our own type and carries the deserialisation path,
#  so Span does its own bounds check under these same macros.
#
#  Both instrumentations are verified to actually fire, by canaries kept in
#  the scratchpad rather than by assuming a clean run means a checked run.
#
#  The bound is reduced here on purpose: this target's job is finding
#  undefined behaviour and bounds violations, not re-establishing coverage.
#  Every code path is still exercised at 8 domain bits.
# --------------------------------------------------------------------------
CHECKFLAGS := -D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC -D_GLIBCXX_ASSERTIONS \
              -fsanitize=undefined -fsanitize-undefined-trap-on-error \
              -fstack-protector-all

check: check-toolchain
	@mkdir -p $(BUILD)/check
	@fail=0; \
	for t in $(TESTSRC); do \
	  n=$$(basename $$t .cpp); \
	  $(CXX) $(STD) -O1 -g $(OPT) $(INC) $(CHECKFLAGS) $$t $(SRC) \
	      -o $(BUILD)/check/$$n.exe $(LDLIBS) \
	      || { echo "  $$n: BUILD FAILED"; fail=1; continue; }; \
	  if OBLIVREC_MAX_BITS=$(CHECK_BITS) ./$(BUILD)/check/$$n.exe \
	       > $(BUILD)/check/$$n.log 2>&1; then \
	    echo "  $$n: clean"; \
	  else \
	    rc=$$?; \
	    echo "  $$n: FAILED under hardening (exit $$rc)"; \
	    sed 's/^/      /' $(BUILD)/check/$$n.log; \
	    if [ $$rc -eq 127 ]; then \
	      echo "      exit 127 is the LOADER, not the test."; \
	      echo "      Hardened binaries import libwinpthread-1.dll because"; \
	      echo "      _GLIBCXX_DEBUG's safe-iterator registry takes a mutex, and"; \
	      echo "      Git for Windows ships an incompatible copy at /mingw64/bin."; \
	      echo "      Fix: put /c/msys64/mingw64/bin FIRST on PATH."; \
	    fi; \
	    fail=1; \
	  fi; \
	done; \
	if [ $$fail -ne 0 ]; then echo ""; echo "HARDENED CHECK FAILED"; exit 1; fi; \
	echo ""; echo "all clean under UBSan + _GLIBCXX_DEBUG + stack protector + checked Span"

# --------------------------------------------------------------------------
#  Benchmarks.
#
#  Bench binaries live in build/bench/ so this pattern rule cannot be confused
#  with the tests' rule, and so the `test` loop can never pick one up and run
#  it as a test.
#
#  FORCE is not decoration. GIT_SHA is baked in through CXXFLAGS, and make does
#  not track CXXFLAGS, so an unchanged bench source at a new HEAD would relink
#  nothing and every row would silently carry the PREVIOUS commit's SHA. RULES
#  D5 requires every benchmark number to carry its SHA, so the one translation
#  unit holding that macro is rebuilt on every invocation. It costs a second.
#
#  The binary writes JSONL to stdout and its human-readable table to stderr.
#  The redirect lives here rather than in C++, so the binary has no file I/O
#  and no cwd assumption, and append-only-ness (RULES B5) is visible in the
#  recipe. A bad run is superseded by a new one and filtered by git_sha at
#  plotting time, never deleted.
# --------------------------------------------------------------------------
FORCE:

$(BUILD)/bench/%.exe: bench/%.cpp $(OBJ) FORCE
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $< $(OBJ) -o $@ $(LDLIBS)

bench: check-toolchain $(BENCHBIN)
	@mkdir -p $(RESULTS)
	@for b in $(BENCHBIN); do \
	  n=$$(basename $$b .exe); \
	  echo "--- $$n"; \
	  ./$$b >> $(RESULTS)/$$n.jsonl \
	    || { echo "BENCH FAILED: $$n"; exit 1; }; \
	  echo "    appended to $(RESULTS)/$$n.jsonl ($$(wc -l < $(RESULTS)/$$n.jsonl) rows total)"; \
	done

# --------------------------------------------------------------------------
#  ARCHITECTURE section 10 and RULES B5 both require that every figure comes
#  from this target and is never hand-edited. The script is Day 6 work, so the
#  target fails with an explanation rather than a Python traceback.
# --------------------------------------------------------------------------
figures:
	@test -f bench/scripts/make_figures.py || { \
	  echo "bench/scripts/make_figures.py does not exist yet. It is Day 6 work."; \
	  echo "ARCHITECTURE section 10 requires every figure to come from this"; \
	  echo "target, so figures stay unbuildable until it lands, deliberately."; \
	  exit 1; }
	$(PY) bench/scripts/make_figures.py

clean:
	rm -rf $(BUILD)
