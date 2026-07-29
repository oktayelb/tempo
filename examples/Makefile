CXX      ?= g++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Werror -O2 -pthread -I..

# The examples take their macros from the command line like everything else.
# 08_report.cpp and 10_recursion.cpp set TEMPO_PRINT_ENABLED themselves, behind
# an #ifndef, so a value passed here wins over their default.
DEFINES  ?=

SRCS := $(wildcard *.cpp)
BINS := $(patsubst %.cpp,build/%,$(SRCS))

all: $(BINS)

build:
	@mkdir -p build

build/%: %.cpp ../tempo.hpp | build
	$(CXX) $(CXXFLAGS) $(DEFINES) $< -o $@

run: all
	@for bin in $(BINS); do \
		echo ""; \
		echo "########## $$bin ##########"; \
		./$$bin || exit 1; \
	done

# The examples are the only place TEMPO_ENABLED=0 is exercised end to end, since
# the tests deliberately inspect wrapper types that stop existing when it is off.
# Compiled only: with the switch off there are no measurements left to check.
matrix:
	@failed=0; \
	for defs in \
		"-DTEMPO_ENABLED=0" \
		"-DTEMPO_COUNT_RECURSION=1" \
		"-DTEMPO_PRINT_ENABLED=0" \
		"-DTEMPO_ENABLED=0 -DTEMPO_COUNT_RECURSION=1" \
		"-DTEMPO_PRINT_ENABLED=0 -DTEMPO_COUNT_RECURSION=1" ; do \
		echo ""; \
		echo "  $(CXX) $$defs"; \
		for file in $(SRCS); do \
			$(CXX) $(CXXFLAGS) $$defs $$file -o /dev/null || failed=1; \
		done; \
	done; \
	exit $$failed

# The examples are ordinary programs using tempo, so there is no reason they
# should be the one thing nothing checks for undefined behaviour. This is not
# hypothetical: the busy-wait loops in 07 and 08 overflowed a signed int for as
# long as the sanitizers ran over tests/ only.
sanitize:
	@$(MAKE) --no-print-directory clean >/dev/null
	@$(MAKE) --no-print-directory run \
		CXXFLAGS="-std=c++20 -Wall -Wextra -Werror -O1 -g -pthread -I.. -fsanitize=address,undefined -fno-omit-frame-pointer"

tsan:
	@$(MAKE) --no-print-directory clean >/dev/null
	@$(MAKE) --no-print-directory run \
		CXXFLAGS="-std=c++20 -Wall -Wextra -Werror -O1 -g -pthread -I.. -fsanitize=thread"

clean:
	rm -rf build

.PHONY: all run matrix sanitize tsan clean
