# Overview

wchkdsk is webOS check disk for microsoft filesystem (ntfs, exfat, fat).
wchkdsk is just wrapper program to execute fsck repectively.
It satisfy requirement of webOS. It need to below projects.
wchkdsk fork fsck of below projects according to it's '-f' option.

ntfs : ntfsprogs-plus ([https://github.com/ntfsprogs-plus/ntfsprogs-plus](https://github.com/ntfsprogs-plus/ntfsprogs-plus))

exfat : exfatprogs ([https://github.com/exfatprogs/exfatprogs](https://github.com/exfatprogs/exfatprogs))

fat : fatprogs ([https://github.com/fatprogs/fatprogs](https://github.com/fatprogs/fatprogs))

## examples

wchkdsk will fork ntfsck with 20 seconds timeout.

`wchkdsk -f ntfs -t 20 /dev/sdc1`

wchkdsk will fork dosfsck with 20 seconds timeout and priority 1.

`wchkdsk -f fat -p 1 -t 20 /dev/sdc1`
