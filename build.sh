#!/bin/bash
g++ -O2 -std=gnu++23 haptics.cpp -o livehaptics -lhidapi-hidraw -lpulse-simple -lpulse -lpthread
