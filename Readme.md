#ORPL: Opportunistic RPL

The source code of our opportunistic extension of RPL, ORPL, presented in "Let the Tree Bloom: Scalable Opportunistic Routing with ORPL" published at ACM SenSys 2013.
The code is intended for Contiki 2.7 and the sky platform.
Main author and contact: Simon Duquennoy: simonduq (at) sics (dot) se

## Compiling
To compile ORPL with a collect-only application, go to the `example-full` directory, open the Makefile and set the CONTIKI definition so it points to your a Contiki 2.7 copy.
Type `make app-collect-only.sky`.
Start cooja, load `orpl-collect-only.csc`, and start the simulation.
A number of logs are enabled by default, showing how packets are forwarded along multiple hops and at different layers of Contiki.

## Tags
There are a number of git tags that point to different versions of ORPL:
* orpl1-sensys2013: the code of ORPL1, used for the experiments in the SenSys'13 paper
* orpl1-orpl2: ORPL1 with minor updates and fixes, along with the new version ORPL2, which is ported to Contiki 2.7, cleaned up, and more modular than ORPL1
* orpl2: first revision that contains only ORPL2, making ORPL1 obsolete
