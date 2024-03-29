#!/bin/bash
if ! [ $(id -u) = 0 ]; then
   echo "The script need to be run as root." >&2
   exit 1
fi

if [ $SUDO_USER ]; then
    real_user=$SUDO_USER
else
    real_user=$(whoami)
fi

origval=$(sudo cat /sys/kernel/mm/transparent_hugepage/enabled)
sudo -u $real_user echo $origval
set -e
function cleanup {
  echo "Restoring hugepages to madvise"
  echo "madvise" > /sys/kernel/mm/transparent_hugepage/enabled
}
trap cleanup EXIT

for mode in "always" ; do # "never
  sudo -u $real_user echo "mode: " $mode
  echo $mode > /sys/kernel/mm/transparent_hugepage/enabled
  echo $(sudo cat /sys/kernel/mm/transparent_hugepage/enabled)
  $@
done
echo "Done."

