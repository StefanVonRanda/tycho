# tycho-make's executable demo rulefile. Every recipe is POSIX and deterministic
# -- no compiler, no clock, no $RANDOM -- because its output is half the golden.
#
# The shape is chosen for the scheduler, not for realism:
#   - zeta.o, alpha.o and docs sit at the same depth and RUN AT ONCE, so the log
#     is assembled from a real race rather than from a sequence.
#   - common.h is shared by zeta.o and alpha.o, so touching it must move both,
#     and changing alpha.c alone must move alpha.o and app but NOT zeta.o.
#   - `all` has no recipe: a group node is never stale and never on disk.
#   - every recipe appends its target's name to `trace`, which nothing in the
#     rulefile depends on. That file is the REAL execution order; the log is the
#     reassembled one, and the gate reads both because they are different claims.

all: app docs

app: zeta.o alpha.o
	cat zeta.o alpha.o > app
	printf 'linking app\n'
	printf 'app\n' >> trace

zeta.o: zeta.c common.h
	cat common.h zeta.c > zeta.o
	printf 'zeta.o\n' >> trace

alpha.o: alpha.c common.h
	cat common.h alpha.c > alpha.o
	printf 'alpha.o\n' >> trace

docs: README.md
	tr abc ABC < README.md > docs
	printf 'docs\n' >> trace
