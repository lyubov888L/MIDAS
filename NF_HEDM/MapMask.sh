#!/bin/bash

Xmin=$1 
Xmax=$2
Ymin=$3 
Ymax=$4

awk ' { if (($3 >= '$Xmin') && ($3 <= '$Xmax') && ($4 >= '$Ymin') && ($4 <= '$Ymax'))  print }' < grid.txt >grid_new.txt


