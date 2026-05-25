#!/usr/bin/env bash
set -e

echo "Comparing tags…"
dups=refs/upstream/tags/
dmir=refs/mirror/tags/
git show-ref --tags | while read -r commit ref; do
  if [[ "$ref" == "$dups"* ]]; then
    tag="${ref#$dups}"
    mirror_ref="${dmir}${tag}"
    git rev-parse --verify --quiet ${mirror_ref}
    if ! git rev-parse --verify --quiet "$mirror_ref"; then
      echo "Identified new tag ${tag}"
      
      # Push tag to mirror
      echo "Pushing tag to mirror…"
      git push origin refs/upstream/tags/${tag}:refs/tags/${tag}

      echo "Extracting changelog…"
      rnfile="rn.md"
      ./extract-release-notes.sh "${tag}" "${rnfile}"

      echo "Creating new release…"
      echo "gh release create \"$tag\" --notes-file \"${rnfile}\""
      gh release create "$tag" --notes-file "${rnfile}" 
    fi
  fi
done
