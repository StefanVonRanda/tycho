# The rulefile that separates a work queue from a wavefront, and does nothing
# else. See tools/tycho-make/run.sh [8b].
#
# One source, then four nodes at depth 1: the head of a chain and three
# sleepers. c2 and c3 sit at depths 2 and 3 BEHIND THE CHAIN ONLY -- they name
# no sleeper, so nothing but a level-shaped scheduler can make them wait for
# one. Every recipe brackets itself with `start`/`end` lines in `rtrace`, which
# is the real execution order; the build log is the reassembled one and cannot
# show this.
#
# The sleepers sleep a whole second against a chain that takes milliseconds, so
# the margin is not a timing guess -- but it IS a duration, which is why the
# assertion is an ordering and never a measurement.

all: c3 w1 w2 w3

c1: base
	printf 'start c1\n' >> rtrace
	cp base c1
	printf 'end c1\n' >> rtrace

c2: c1
	printf 'start c2\n' >> rtrace
	cp c1 c2
	printf 'end c2\n' >> rtrace

c3: c2
	printf 'start c3\n' >> rtrace
	cp c2 c3
	printf 'end c3\n' >> rtrace

w1: base
	printf 'start w1\n' >> rtrace
	sleep 1
	cp base w1
	printf 'end w1\n' >> rtrace

w2: base
	printf 'start w2\n' >> rtrace
	sleep 1
	cp base w2
	printf 'end w2\n' >> rtrace

w3: base
	printf 'start w3\n' >> rtrace
	sleep 1
	cp base w3
	printf 'end w3\n' >> rtrace
