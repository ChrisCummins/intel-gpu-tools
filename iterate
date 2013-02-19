#!/bin/bash

set -e

dir=pngs

make
mkdir -p $dir

sudo ./tests/cec_test2
mv -f rendercopy-output.png $dir/hello-world.png
sudo ./tests/cec_test2 test.png
mv -f rendercopy-output.png $dir/test.png
sudo ./tests/cec_test2 rainbow.png
mv -f rendercopy-output.png $dir/rainbow.png

sudo ./tests/cec_test
for f in $(ls | grep '\-dst.png')
do
    mv -f $f $dir
done
