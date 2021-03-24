#!/bin/zsh

set -e
setopt nullglob

mkdir -p ${0:a:h}/assets
cd ${0:a:h}/assets

rm -f *.png

src=../../../../share/icons/256x256/mkvtoolnix-gui.png

for height in 16 24 32 48 64 256; do
  convert ${src} -scale ${height}x${height} fileicon.targetsize-${height}.png
done

for scale in 100 125 150 200 400; do
  height=$(( ((scale * 10 * 150) / 100 + 5) / 10 ))
  convert ${src} -scale ${height}x${height} Square150x150Logo.scale-${scale}.png
done

for scale in 100 125 150 200 400; do
  height=$(( ((scale * 10 * 310) / 100 + 5) / 10 ))
  convert ${src} -scale ${height}x${height} Square310x310Logo.scale-${scale}.png
done

for scale in 100 125 150 200 400; do
  height=$(( ((scale * 10 * 44) / 100 + 5) / 10 ))
  convert ${src} -scale ${height}x${height} Square44x44Logo.scale-${scale}.png
done

for height in 16 24 32 48 256; do
  convert ${src} -scale ${height}x${height} Square44x44Logo.targetsize-${height}.png
done

for scale in 100 125 150 200 400; do
  height=$(( ((scale * 10 * 71) / 100 + 5) / 10 ))
  convert ${src} -scale ${height}x${height} Square71x71Logo.scale-${scale}.png
done

for scale in 100 125 150 200 400; do
  height=$(( ((scale * 10) / 2 + 5) / 10 ))
  convert ${src} -scale ${height}x${height} StoreLogo.scale-${scale}.png
done

for scale in 100 125 150 200 400; do
  height=$(( ((scale * 10 * 150) / 100 + 5) / 10 ))
  width=$(( ((scale * 10 * 310) / 100 + 5) / 10 ))
  convert ${src} -scale ${height}x${height} -background transparent -gravity center -extent ${width}x${height} Wide310x150Logo.scale-${scale}.png
done

for file in *.scale-100.png; do
  cp ${file} ${file/.scale-100/}
done
