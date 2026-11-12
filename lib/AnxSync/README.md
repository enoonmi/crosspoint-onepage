# AnxSync — Anx Reader WebDAV sync for CrossPoint / OnePage

Integration layer that lets this firmware pull a book library from
[Anx Reader](https://github.com/Anxcye/anx-reader) over WebDAV, and exchange
reading progress both ways.

Background, protocol details and the milestones live in the integration
repository: <https://github.com/enoonmi/anx-onepage>.

## Module layout

Everything in this directory is **deliberately free of Arduino / ESP-IDF
dependencies**, so it compiles and runs on a development machine. The tests in
`tests/host/` exercise all of it.

| File | Purpose | Host-tested |
|---|---|---|
| `AnxHttpDate.{h,cpp}` | RFC 1123 date parse/format for `getlastmodified` | ✅ |
| `AnxPropfindParser.{h,cpp}` | `207 Multi-Status` parsing, href decoding | ✅ |
| `AnxPathMap.{h,cpp}` | remote href → `/Books/...`, traversal guards | ✅ |
| `AnxSyncPlan.{h,cpp}` | diff remote vs local, decide download/replace/skip | ✅ |
| `AnxProgressJson.{h,cpp}` | `anx/onepage/progress.json` read/write/merge | ✅ |

The network and storage layers (`AnxWebDavClient`, SD-card writes, the settings
UI) are **not in this directory yet** — they need `esp_http_client`, `HalStorage`
and the activity framework, and therefore cannot be verified on the host.
See the milestone list in the integration repo.

## Hard constraints honoured by this code

1. **Never upload `anx/database*.db`.** Anx Reader syncs its SQLite database as a
   whole file and prunes remote book files that are not in it. A device that
   uploaded its own database would erase the phone's notes, groups and reading
   statistics. This code only ever *reads* the remote book list.
2. **Never write into `anx/data/file/` or `anx/data/cover/`.** `syncFiles()` in
   Anx Reader deletes anything under those paths that is not in `tb_books`.
3. **Progress goes to `anx/onepage/progress.json`,** a path Anx Reader ignores and
   its file sync does not touch.
4. **Only `reading_percentage` is transported.** `tb_books.last_read_position`
   holds an EPUB CFI produced by foliate-js; a device without a DOM cannot
   generate one, and overwriting it would destroy the phone's precise position.
5. **Local books are never deleted by a sync pass.** Files present locally but
   absent remotely are reported, not removed, so an accidental server-side wipe
   stays recoverable.

## Memory notes

The upstream CrossPoint targets an ESP32-C3 with ~380 KB RAM; this port targets
the OnePage ESP32-C61 with 2 MB PSRAM. The code here still avoids large
allocations and keeps parsing streaming and buffer-light, but the C61 is not
under the X4's extreme pressure. See `.skills/SKILL.md` for the project's
general coding rules — note that its RAM figures refer to the C3 and do not
apply unchanged to this board.

## Testing

```powershell
powershell -ExecutionPolicy Bypass -File tests\host\run_tests.ps1
```

See `tests/host/README.md` for the end-to-end WebDAV exercise.
