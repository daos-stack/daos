git for-each-ref --sort=-*authordate 'refs/tags/v*' | awk '{printf $3 " "}' | sed 's/refs\/tags\///g' | sed 's/refs\/heads\///g' |  awk '{ for (i=1;i<NF; i++) print $(i+1) " " $i; print $NF}' | xargs -n 2 ./shortlog.sh

