#!/bin/sh
include_aosen_path="/usr/include/aosen/"
include_path="/usr/include/"
aosen="aosen"

cd src
rm tags
cd ..
if [ ! -d "$include_aosen_path" ]
then
	mkdir $include_aosen_path
else
    rm -rf $include_aosen_path
	mkdir $include_aosen_path
fi
cd src
for dir in `ls`
do
	cd $dir
	cp -r *.h $include_aosen_path
	cd ../
done
ctags -R
echo "copy success"

