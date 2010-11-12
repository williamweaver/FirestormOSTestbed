#!/bin/bash

###
### Constants
###

TRUE=0 # Map the shell's idea of truth to a variable for better documentation
FALSE=1

# If you have some other version of python installed other than 26, do not edit this file.
# Add a line like this to ~/.bashrc:
#	WINPYTHON="/cygdrive/c/path_to_your_python.exe"
# And then close and restart cygwin
if [ -z $WINPYTHON ] ; then
	WINPYTHON="/cygdrive/c/Python26/python.exe"
fi
LOG="`pwd`/logs/build_win32.log"

###
### Global Variables
###

WANTS_CLEAN=$TRUE
WANTS_CONFIG=$TRUE
WANTS_BUILD=$TRUE
WANTS_PACKAGE=$TRUE

###
### Helper Functions
###

if [ "$1" == "--rebuild" ] ; then
	echo "rebuilding..."
	WANTS_CLEAN=$FALSE
	WANTS_CONFIG=$FALSE
elif [ "$1" == "--config" ] ; then
	echo "configuring..."
	WANTS_BUILD=$FALSE
	WANTS_PACKAGE=$FALSE
fi

###
###  Main Logic
### 

path=$WINPATH:/usr/local/bin:/usr/bin:/bin
if [ ! -f "$WINPYTHON" ] ; then
	echo "ERROR: You need to edit .bashrc and set WINPYTHON at the top to point at the path of your windows python executable."
	exit 1
fi

pushd indra > /dev/null
if [ $WANTS_CLEAN -eq $TRUE ] ; then
	$WINPYTHON develop.py clean
	find . -name "*.pyc" -exec rm {} \;
fi


if [ $WANTS_CONFIG -eq $TRUE ] ; then
	mkdir -p ../logs > /dev/null 2>&1
	$WINPYTHON develop.py -G vc80 -t Release configure -DPACKAGE:BOOL=ON -DLL_TESTS:BOOL=OFF  2>&1 | tee $LOG
	mkdir -p build-vc80/sharedlibs/RelWithDebInfo
	mkdir -p build-vc80/sharedlibs/Release
	cp fmod.dll ./build-vc80/sharedlibs/RelWithDebInfo
	cp fmod.dll ./build-vc80/sharedlibs/Release
fi

if [ $WANTS_BUILD -eq $TRUE ] ; then
	echo -n "Setting build version to "
	buildVer=`hg summary | grep parent | sed 's/parent: //' | sed 's/:.*//'`
	echo "$buildVer."
	cp llcommon/llversionviewer.h llcommon/llversionviewer.h.build
	trap "mv llcommon/llversionviewer.h.build llcommon/llversionviewer.h" INT TERM EXIT
	sed -e "s#LL_VERSION_BUILD = [0-9][0-9]*#LL_VERSION_BUILD = ${buildVer}#"\
		llcommon/llversionviewer.h.build > llcommon/llversionviewer.h


	echo "Building in progress. Check $LOG for verbose status."
	$WINPYTHON develop.py -G vc80 -t Release build  2>&1 | tee -a $LOG | grep Build
	trap - INT TERM EXIT
	mv llcommon/llversionviewer.h.build llcommon/llversionviewer.h
	echo "Complete"
fi
popd > /dev/null
