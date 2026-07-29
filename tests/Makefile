CXX      ?= g++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Werror -O2 -pthread -I.. -I.

# Per-call printing is off so the suite's output is its own results and nothing
# else. The CI matrix builds with it on as well, to keep that path compiling.
DEFINES  ?= -DTEMPO_PRINT_ENABLED=0

SRCS := $(wildcard [0-9]*.cpp)
BINS := $(patsubst %.cpp,build/%,$(SRCS))

all: $(BINS)

build:
	@mkdir -p build

build/%: %.cpp ../tempo.hpp tempo_test.hpp | build
	$(CXX) $(CXXFLAGS) $(DEFINES) $< -o $@

# Runs every test binary and fails if any of them does.
run: all
	@failed=0; \
	for bin in $(BINS); do \
		echo ""; \
		echo "########## $$bin ##########"; \
		./$$bin || failed=1; \
	done; \
	echo ""; \
	if [ $$failed -eq 0 ]; then echo "ALL TEST FILES PASSED"; \
	else echo "SOME TEST FILES FAILED"; fi; \
	exit $$failed

# The combinations that change tempo's behaviour, all from the same sources.
# TEMPO_ENABLED=0 is not included here: it turns TEMPO_INSTRUMENT into a plain
# function pointer, which the tests deliberately inspect, so it is covered by
# the examples instead.
matrix:
	@failed=0; \
	for defs in \
		"-DTEMPO_PRINT_ENABLED=0" \
		"-DTEMPO_PRINT_ENABLED=0 -DTEMPO_COUNT_RECURSION=1" \
		"-DTEMPO_PRINT_ENABLED=1" \
		"-DTEMPO_PRINT_ENABLED=1 -DTEMPO_COUNT_RECURSION=1" ; do \
		echo ""; \
		echo "===================================================="; \
		echo "  $(CXX) $$defs"; \
		echo "===================================================="; \
		$(MAKE) --no-print-directory clean >/dev/null; \
		$(MAKE) --no-print-directory run DEFINES="$$defs" \
			| grep -E "^(PASS|FAIL|ALL|SOME|  FAIL|    )" || failed=1; \
	done; \
	$(MAKE) --no-print-directory clean >/dev/null; \
	exit $$failed

# Undefined and address sanitizers. Slower, and worth it: the depth counter and
# the argument snapshots are exactly the kind of state these catch.
sanitize:
	@$(MAKE) --no-print-directory clean >/dev/null
	@$(MAKE) --no-print-directory run \
		CXXFLAGS="-std=c++20 -Wall -Wextra -O1 -g -pthread -I.. -I. -fsanitize=address,undefined -fno-omit-frame-pointer"

# ThreadSanitizer, for 08_threads above all. Kept separate because it cannot be
# combined with the address sanitizer.
tsan:
	@$(MAKE) --no-print-directory clean >/dev/null
	@$(MAKE) --no-print-directory run \
		CXXFLAGS="-std=c++20 -Wall -Wextra -O1 -g -pthread -I.. -I. -fsanitize=thread"

clean:
	rm -rf build

.PHONY: all run matrix sanitize tsan clean
