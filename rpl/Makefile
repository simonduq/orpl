CONTIKI=../contiki
CONTIKI_TOOLS=$(CONTIKI)/tools
CONTIKI_PROJECT = app-collect-only app-down-only app-any-to-any
CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"
TARGET=sky

WITH_UIP6=1
UIP_CONF_IPV6=1
CFLAGS+= -DUIP_CONF_IPV6_RPL -I-
PROJECT_SOURCEFILES += simple-energest.c rpl-tools.c

ifdef NO_WARNINGS
	CFLAGS += -w
endif

CYCLE_TIME ?= 500
CFLAGS += -DCMD_CYCLE_TIME=$(CYCLE_TIME)

PHASE_LOCK ?= 1
CFLAGS += -DCMD_PHASE_LOCK=$(PHASE_LOCK)

.PHONY: curr_contiki_head
all: $(CONTIKI_PROJECT) curr_contiki_head

include Makefile.include

curr_contiki_head:
	git --git-dir=$(CONTIKI)/.git log -1 | head -n 1 > curr_contiki_head

%.exe: %.sky
	cp $< $@
