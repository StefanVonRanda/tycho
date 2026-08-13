# tycho-make demo rulefile.
#
# The two object files are deliberately named zeta.o and alpha.o and written in
# that order: the topological order below has to put zeta.o first, which is
# declaration order, and NOT alpha.o first, which is alphabetical. A tie-break
# whose two candidate rules agree would gate nothing.

app: zeta.o alpha.o
	cc -o app zeta.o alpha.o

zeta.o: zeta.c common.h
	cc -c zeta.c

alpha.o: alpha.c common.h
	cc -c alpha.c

# A second, disconnected component: a graph with one root would never exercise
# the resumption of the scan after a component runs out.
docs: README.md
	pandoc README.md -o docs
