#!/bin/sh -b

function fail
{
    echo $*
    exit 1
}

type git >&/dev/null || fail "Failed to find git"
ROOT=`git root`
cd $ROOT
FILES=$(find * -path dist -prune -o -type f -print)
echo util:lha a -r bak $FILES >cmd
echo "execute cmd" on Amiga

