ORPL: Opportunistic RPL

===
The source code for our paper "Let the Tree Bloom: Scalable Opportunistic Routing with ORPL" published at ACM SenSys 2013.

To compile ORPL with a collect-only application, go to the `orpl` directory, and type `make app-collect-only.sky`.
Start cooja and load `orpl-collect-only.csc`.
Building other applications require to play with project-conf.h, for example, to build `app-down-only`, one needs to set `UP_ONLY` to 0.
Conact Simon Duquennoy for more information: simonduq (at) sics (dot) se
