override CXXFLAGS += -Wall -Wextra
override LDFLAGS +=

RM=rm -f
NUM_TRIALS=10
# $(wildcard *.cpp /xxx/xxx/*.cpp): get all .cpp files from the current directory and dir "/xxx/xxx/"
SRCS := $(wildcard objects/*.cpp *.cpp)
# $(patsubst %.cpp,%.o,$(SRCS)): substitute all ".cpp" file name strings to ".o" file name strings
OBJS := $(patsubst %.cpp,%.o,$(SRCS))

# Allows one to enable verbose builds with VERBOSE=1
V := @
ifeq ($(VERBOSE),1)
	V :=
endif

all: release

cgoto: clean
cgoto: CXXFLAGS += -DNEXT_USE_COMPUTED_GOTO
cgoto: release

release: CXXFLAGS += -O3
release: LDFLAGS += -s
release: next

debug: CXXFLAGS += -g3 -DDEBUG
debug: next

debug_ins: CXXFLAGS += -DDEBUG_INS
debug_ins: debug

debug_gc: CXXFLAGS += -DDEBUG_GC
debug_gc: debug

debug_all: CXXFLAGS += -DDEBUG_INS -DDEBUG_GC
debug_all: debug

pgo: merge_profraw pgouse

clean_pgodata: clean
	$(V) rm -f default_*.profraw default.profdata

pgobuild: CXXFLAGS+=-fprofile-generate
pgobuild: LDFLAGS+=-fprofile-generate
pgobuild: | clean_pgodata cgoto

pgorun: NUM_TRIALS=2
pgorun: | pgobuild benchmark tests

merge_profraw: pgorun
	$(V) llvm-profdata merge --output=default.profdata default_*.profraw

pgouse: merge_profraw
	$(V) $(MAKE) clean
	$(V) $(MAKE) cgoto CXXFLAGS=-fprofile-use LDFLAGS=-fprofile-use
	$(V) $(MAKE) benchmark

tests: release
	$(V) ./next tests/orchestrator.n

next: $(OBJS)
	$(CXX) $(OBJS) $(LDFLAGS) -o next

benchmark: release
	$(V) python3 util/benchmark.py -n $(NUM_TRIALS) -l next $(suite)

benchmark_baseline: release
	$(V) python3 util/benchmark.py --generate-baseline

depend: .depend

.depend: $(SRCS)
	$(RM) ./.depend
	$(CXX) $(CXXFLAGS) -MM $^>>./.depend;

clean:
	$(RM) $(OBJS)

distclean: clean
	$(RM) *~ .depend

include .depend

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@
