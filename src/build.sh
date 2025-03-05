#!/bin/bash

gcc -o uviemon *.c -L./lib/ftdi/build -lftd2xx -lreadline -lm -Wall -std=c17
