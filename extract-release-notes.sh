#!/usr/bin/env bash
set -e

tag="$1"
trgpath="${2:=rn.md}"


ver=${tag#release-}

git checkout refs/upstream/tags/${tag} -- NEWS.md


begin="# Version ${ver} "
end="# Version "
src="NEWS.md"

echo "searching '$begin' to '$end' from ${src} ${trgpath}…"

hit=0
while read -r line; do
  if (( $hit == 0 )) && [[ "$line" == "${begin}"* ]]; then
    echo "Found begin $line"
    hit=1
    echo "$line" > "$trgpath"
  elif (( $hit == 1 )) && [[ "$line" == "${end}"* ]]; then
    hit=0
  elif [[ "$hit" == "1" ]]; then
    echo "$line" >> "$trgpath"
  fi
done < "$src"
