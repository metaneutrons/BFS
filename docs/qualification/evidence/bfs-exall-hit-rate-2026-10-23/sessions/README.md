# Sessions

Put the counter file of every usage session here, one file per session, named
`<date>-hitrate.txt` (for example `2026-10-24-hitrate.txt`). It is
`System/hitrate.txt` of the kit, written by the background loop. Each session
starts at zero. Read a file with

```sh
python3 ../summarize.py <date>-hitrate.txt
```

The files are tab separated key and value lines, a few kilobytes each, and
carry no names of files or directories, only counts.
