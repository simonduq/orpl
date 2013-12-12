#ORPL: Opportunistic RPL

The source code for our paper "Let the Tree Bloom: Scalable Opportunistic Routing with ORPL" published at ACM SenSys 2013.

## Compiling
To compile ORPL with a collect-only application, go to the `orpl` directory, and type `make app-collect-only.sky`.
Start cooja and load `orpl-collect-only.csc`.
Building other applications require to play with project-conf.h, for example, to build `app-down-only`, one needs to set `UP_ONLY` to 0.
Conact Simon Duquennoy for more information: simonduq (at) sics (dot) se

## Content
* contiki: the contiki tree used for the SenSys'13 paper experiments
* common: some common code and tools used for both RPL and ORPL in the SenSys'13 paper experiments
* rpl: the RPL code and applications used in the SenSys'13 paper experiments
* orpl: the ORPL code and applications used in the SenSys'13 paper experiments
* orpl2: an ongoing port of ORPL to Contiki 2.7 and general cleanup
