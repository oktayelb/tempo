CXX      ?= g++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -O2 -I..

SRCS := $(wildcard *.cpp)
BINS := $(patsubst %.cpp,build/%,$(SRCS))

all: $(BINS)

build:
	@mkdir -p build

build/%: %.cpp ../tempo.hpp | build
	$(CXX) $(CXXFLAGS) $< -o $@

run: all
	@for bin in $(BINS); do \
		echo ""; \
		echo "########## $$bin ##########"; \
		./$$bin || exit 1; \
	done

clean:
	rm -rf build

.PHONY: all run clean
