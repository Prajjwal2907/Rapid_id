CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Iinclude $(EXTRA_INCLUDES)
LDLIBS ?= -lcurl

SRCS = $(filter-out src/main.cpp,$(wildcard src/*.cpp))
LIB_OBJS = $(patsubst src/%.cpp,build/%.o,$(SRCS))
DEPS = $(LIB_OBJS:.o=.d) build/main.d

TEST_SRCS = $(wildcard tests/test_*.cpp)
TEST_EXES = $(patsubst tests/%.cpp,build/%.exe,$(TEST_SRCS))
TEST_DEPS = $(patsubst tests/%.cpp,build/%.d,$(TEST_SRCS))

OFFLINE_TEST_SRCS = $(wildcard tests/*_offline.cpp)
OFFLINE_TEST_EXES = $(patsubst tests/%.cpp,build/%.exe,$(OFFLINE_TEST_SRCS))

.PHONY: all app tests offline clean

ifneq ($(wildcard main.cpp),)
all: app tests
else
all: tests
endif

build:
	mkdir -p build

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

build/main.o: main.cpp | build
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

ifneq ($(wildcard main.cpp),)
app: build/rapidaid.exe

build/rapidaid.exe: build/main.o $(LIB_OBJS) | build
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)
else
app:
	@echo "Error: main.cpp does not exist. Cannot build app." >&2
	@exit 1
endif

tests: $(TEST_EXES)

build/test_%.exe: tests/test_%.cpp $(LIB_OBJS) | build
	$(CXX) $(CXXFLAGS) -MMD -MP $< $(LIB_OBJS) -o $@ $(LDLIBS)

test_%: build/test_%.exe ;

offline: $(OFFLINE_TEST_EXES)
	@if [ -z "$(OFFLINE_TEST_EXES)" ]; then \
		echo "No offline tests found."; \
	else \
		for test in $(OFFLINE_TEST_EXES); do \
			echo "Running $$test..."; \
			./$$test || exit 1; \
		done; \
	fi

clean:
	rm -rf build

-include $(DEPS)
-include $(TEST_DEPS)
