#!/bin/sh

set -eu

bsdtar --strip-components 1 -xf szafir_Linux.zip Szafir_Linux/szafir_708.jar Szafir_Linux/libCCGraphiteP11.2.0.5.6.so
rm szafir_Linux.zip
