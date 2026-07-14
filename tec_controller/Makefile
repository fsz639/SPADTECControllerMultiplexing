# Makefile - control TEC (BBB), solo ARM (sin PRU)
# Cross-compilacion en el HOST/VM (ver seccion 14.4 para el toolchain ARM):
CROSS_COMPILE ?= arm-linux-gnueabihf-
CXX      := $(CROSS_COMPILE)g++
CXXFLAGS := -O2 -mtune=cortex-a8 -march=armv7-a+fp -Wall
TARGET   := tec_controller

all: $(TARGET)

$(TARGET): tec_controller.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)

.PHONY: all clean
