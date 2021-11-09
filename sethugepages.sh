#!/bin/bash

echo 0 > /proc/sys/vm/swappiness
echo 255000 > /proc/sys/vm/nr_hugepages

