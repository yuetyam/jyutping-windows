# Preparing

`preparing` builds the desktop CoreIME database from the source text files in `res`.
The schema and converted data match the Swift/macOS `CoreIMEDesktopData/Resources/desktop.sqlite3` database.
Mobile and 9-key tables are intentionally excluded.

Run it from this directory:

```sh
cargo run --release
```

By default, the completed database is written atomically to `../Jyutping/Resources/ime.sqlite3`.
Pass a path after `--` to write it elsewhere:

```sh
cargo run --release -- /path/to/ime.sqlite3
```

The project has no third-party Rust dependencies.
It uses the SQLite library provided by the operating system: `winsqlite3.dll` on Windows and `libsqlite3` on other supported development platforms.
