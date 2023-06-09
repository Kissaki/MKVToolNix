#!/usr/bin/env bash
set -e

echo "Comparing tags…"
dups=.git/refs/upstream/tags/
dmir=.git/refs/mirror/tags/
for fpath in ${dups}*
do
  tag=${fpath#${dups}}
  if [ ! -e "${dmir}${tag}" ]
  then
    echo "Identified new tag ${tag}"

    # Push tag to mirror
    echo "Pushing tag to mirror…"
    git push origin refs/upstream/tags/${tag}:refs/tags/${tag}

    echo "Extracting changelog…"
    rnfile="rn.md"
    ./extract-release-notes.sh "${tag}" "${rnfile}"

    echo "Creating new release…"
    echo "hub release create --file \"${rnfile}\" \"$tag\""
    hub release create --file "${rnfile}" "$tag"
  fi
done
