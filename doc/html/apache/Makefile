
DOTS = $(wildcard *.dot)
GIFS = $(patsubst %.dot, %.gif, $(DOTS))

all: $(GIFS)

$(GIFS) : %.gif : %.dot
	dot -Tgif -o$@ $<


