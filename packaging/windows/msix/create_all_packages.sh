#!/bin/bash

set -e -x

cd "$(dirname "$(readlink -f "$0")")"

base_dir=${base_dir:-/z/home/mosu/files/html/bunkus.org/videotools/mkvtoolnix/windows/releases}
archive_dir="${archive_dir:-${base_dir}/${mtxversion}}"

function determine_latest_version {
  ls "${base_dir}" | \
    grep '[0-9]*\.[0-9]*' | \
    sed -Ee 's!.*/!!' | \
    sort -V | \
    tail -n 1
}

mtxversion=$(determine_latest_version)

for file in "${archive_dir}"/*.7z; do
  ./create_package.sh "$file" --sign
done

mv -v package-*/*.msix /z/home/mosu/dl/
