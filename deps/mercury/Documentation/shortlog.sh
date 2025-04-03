#!/bin/bash

# $1 from tag
# $2 to tag

if [ -n "$2" ]
 then 
  echo "================"
  echo -e $2
  echo "================"
  git log $1..$2 --reverse --no-merges --pretty=format:"commit %h%x0AAuthor: %an <%ae> %x0A%x0A %x09 %ad %s %x0A" --date=short | git shortlog -n -w130,2,13

#  git shortlog --no-merges --pretty=format:"%ad %h %s" --date=short $1..$2
else
  echo "================"
  echo -e $1
  echo "================"
  git log $1 --reverse --no-merges --pretty=format:"commit %h%x0AAuthor: %an <%ae> %x0A%x0A %x09 %ad %s %x0A" --date=short | git shortlog -n -w130,2,13

#  git shortlog --no-merges --pretty=format:"%ad %h %s" --date=short $1
fi
