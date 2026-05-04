#!/bin/bash

gcc -o uviemon *.c -L./lib/ftdi/build -lftd2xx -lreadline -lm -lbsd -Wall -std=c17
