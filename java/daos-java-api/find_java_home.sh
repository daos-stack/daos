dirname $(readlink -f $(which java)) | sed 's^/jre/bin^^'
