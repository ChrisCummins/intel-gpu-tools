#!/bin/bash

set -e

dir=pngs

make
mkdir -p $dir

sudo ./tests/cec_test2
mv -f rendercopy-output.png $dir/hello-world.png
sudo ./tests/cec_test2 ~/Downloads/png.png
mv -f rendercopy-output.png $dir/test.png
sudo ./tests/cec_test2 ~/Untitled.png
mv -f rendercopy-output.png $dir/rainbow.png

sudo ./tests/cec_test
for f in $(ls | grep '\-dst.png')
do
    mv -f $f $dir
done
