#!/bin/bash
g++ -O2 -std=gnu++17 -fPIC gui_qt.cpp -o LiveHapticsGUI $(pkg-config --cflags --libs Qt5Widgets) -lhidapi-hidraw
