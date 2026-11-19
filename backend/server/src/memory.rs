use std::fs::OpenOptions;
use std::io;
use std::os::windows::fs::OpenOptionsExt;
use std::path::Path;
use std::time::{SystemTime, UNIX_EPOCH};

use jyutping_core::{Engine, Lexicon, LexiconKind, MemoryRow, MemorySource, memory_serial_fields};

use crate::sqlite::{Binding, Connection, OpenMode, Statement};

const SCHEMA_VERSION: i64 = 2608;
const WRITE_TIMEOUT_MS: i32 = 250;
const SHARE_READ_WRITE_DELETE: u32 = 7;

const SCHEMA: &[&std::ffi::CStr] = &[
        c"CREATE TABLE IF NOT EXISTS memory2608 (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                word TEXT NOT NULL, romanization TEXT NOT NULL, frequency INTEGER NOT NULL, latest INTEGER NOT NULL,
                char_count INTEGER NOT NULL, letter_count INTEGER NOT NULL, complexity INTEGER NOT NULL,
                anchors INTEGER NOT NULL, spell INTEGER NOT NULL, UNIQUE (word, romanization)
        )",
        c"CREATE INDEX IF NOT EXISTS ix2608_frequency ON memory2608 (frequency)",
        c"CREATE INDEX IF NOT EXISTS ix2608_anchors ON memory2608 (anchors, char_count, frequency DESC)",
        c"CREATE INDEX IF NOT EXISTS ix2608_spell ON memory2608 (spell, letter_count, complexity, frequency DESC)",
        c"CREATE INDEX IF NOT EXISTS ix2608_word ON memory2608 (word, frequency DESC)",
        c"CREATE INDEX IF NOT EXISTS ix2608_lexicon ON memory2608 (word, romanization)",
        c"CREATE TABLE IF NOT EXISTS memory2608_generation (id INTEGER PRIMARY KEY CHECK (id = 1), generation INTEGER NOT NULL)",
        c"INSERT OR IGNORE INTO memory2608_generation VALUES (1, 1)",
];

pub struct InputMemory(pub(crate) Connection);

pub(crate) struct MemoryQuery<'a> {
        anchors: Statement<'a>,
        spell: Statement<'a>,
}

impl InputMemory {
        /// Open the specified Windows memory database, migrating the existing schema in-place.
        pub fn open(path: &Path) -> io::Result<Self> {
                let connection = Connection::open(path, OpenMode::Create)?;
                connection.set_busy_timeout(WRITE_TIMEOUT_MS)?;
                let dictionary = connection.prepare(c"SELECT 1 FROM sqlite_master WHERE name = 'lexicon_core'")?.read_rows(|_| Ok(()))?;
                if !dictionary.is_empty() {
                        return Err(io::Error::new(io::ErrorKind::InvalidInput, "the input memory path points to an IME dictionary"));
                }
                prepare_journal(&connection, path)?;
                connection.transaction(|| prepare_schema(&connection))?;
                Ok(Self(connection))
        }

        pub fn open_read_only(path: &Path) -> io::Result<Self> {
                Connection::open(path, OpenMode::ReadOnly).map(Self)
        }

        pub fn open_writer(path: &Path) -> io::Result<Self> {
                let connection = Connection::open(path, OpenMode::ReadWrite)?;
                connection.set_busy_timeout(WRITE_TIMEOUT_MS)?;
                prepare_journal(&connection, path)?;
                Ok(Self(connection))
        }

        pub fn remember(&self, lexicon: &Lexicon) -> io::Result<()> {
                self.remember_at(lexicon, self.generation()?).and_then(check_generation)
        }

        pub fn remember_at(&self, lexicon: &Lexicon, generation: u64) -> io::Result<bool> {
                validate(lexicon)?;
                let now = SystemTime::now().duration_since(UNIX_EPOCH).map_err(io::Error::other)?.as_millis();
                let now = i64::try_from(now).map_err(io::Error::other)?;
                self.0.transaction(|| {
                        if self.generation()? != generation {
                                return Ok(false);
                        }
                        let mut update = self.0.prepare(c"UPDATE memory2608 SET frequency = frequency + 1, latest = ?1 WHERE word = ?2 AND romanization = ?3")?;
                        update.execute([now.into(), Binding::Text(&lexicon.text), Binding::Text(&lexicon.romanization)])?;
                        if self.0.changes() == 0 {
                                let mut insert = self.0.prepare(
                                        c"
                                        INSERT INTO memory2608 (word, romanization, frequency, latest, char_count, letter_count, complexity, anchors, spell)
                                        VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)
                                ",
                                )?;
                                insert_entry(&mut insert, &lexicon.text, &lexicon.romanization, 1, now)?;
                        }
                        Ok(true)
                })
        }

        pub fn forget(&self, lexicon: &Lexicon) -> io::Result<()> {
                self.forget_at(lexicon, self.generation()?).and_then(check_generation)
        }

        pub fn forget_at(&self, lexicon: &Lexicon, generation: u64) -> io::Result<bool> {
                validate(lexicon)?;
                self.0.transaction(|| {
                        if self.generation()? != generation {
                                return Ok(false);
                        }
                        self.0.prepare(c"DELETE FROM memory2608 WHERE word = ?1 AND romanization = ?2")?
                                .execute([Binding::Text(&lexicon.text), Binding::Text(&lexicon.romanization)])?;
                        Ok(true)
                })
        }

        pub fn clear(&self) -> io::Result<()> {
                self.0.transaction(|| {
                        self.0.execute(c"DELETE FROM memory2608")?;
                        self.0.execute(c"UPDATE memory2608_generation SET generation = generation + 1 WHERE id = 1")
                })
        }

        pub fn generation(&self) -> io::Result<u64> {
                let generations = self.0.prepare(c"SELECT generation FROM memory2608_generation WHERE id = 1")?.read_rows(|row| Ok(row.column_integer(0)))?;
                generations
                        .first()
                        .and_then(|&generation| u64::try_from(generation).ok())
                        .filter(|&generation| generation != 0)
                        .ok_or_else(|| io::Error::other("input-memory generation is unavailable"))
        }

        pub fn suggest(&self, engine: &Engine, input: &str) -> io::Result<Vec<Lexicon>> {
                engine.suggest_memory(&mut self.query()?, input)
        }

        pub(crate) fn query(&self) -> io::Result<MemoryQuery<'_>> {
                Ok(MemoryQuery {
                        anchors: self.0.prepare(c"SELECT word, romanization, frequency, latest FROM memory2608 WHERE anchors = ?1 AND char_count = ?2 ORDER BY frequency DESC LIMIT ?3")?,
                        spell: self.0.prepare(c"SELECT word, romanization, frequency, latest FROM memory2608 WHERE spell = ?1 AND letter_count = ?2 AND complexity = ?3 ORDER BY frequency DESC LIMIT ?4")?,
                })
        }
}

fn check_generation(applied: bool) -> io::Result<()> {
        if applied { Ok(()) } else { Err(io::Error::other("input memory changed before the write")) }
}

impl MemorySource for MemoryQuery<'_> {
        fn by_anchors(&mut self, anchors: i64, count: usize, limit: i64) -> io::Result<Vec<MemoryRow>> {
                self.anchors.query([anchors, count as i64, limit], read_memory_row)
        }

        fn by_spell(&mut self, spell: i64, count: usize, complexity: i64, limit: i64) -> io::Result<Vec<MemoryRow>> {
                self.spell.query([spell, count as i64, complexity, limit], read_memory_row)
        }
}

fn read_memory_row(row: &Statement<'_>) -> io::Result<MemoryRow> {
        Ok(MemoryRow { word: row.column_text(0)?.to_owned(), romanization: row.column_text(1)?.to_owned(), frequency: row.column_integer(2), latest: row.column_integer(3) })
}

fn validate(lexicon: &Lexicon) -> io::Result<()> {
        if lexicon.kind != LexiconKind::Cantonese
                || lexicon.text.is_empty()
                || lexicon.romanization.is_empty()
                || lexicon.text.contains('\0')
                || lexicon.romanization.contains('\0')
        {
                return Err(io::Error::new(io::ErrorKind::InvalidInput, "expected an original Cantonese lexicon"));
        }
        Ok(())
}

fn prepare_journal(connection: &Connection, path: &Path) -> io::Result<()> {
        let mut journal = path.as_os_str().to_os_string();
        journal.push("-journal");
        let open_journal = || OpenOptions::new().read(true).write(true).create(true).truncate(false).share_mode(SHARE_READ_WRITE_DELETE).open(&journal);
        // Match the desktop DLL's rollback journal mode and keep the sidecar available to packaged hosts.
        let original = open_journal()?;
        let modes = connection.prepare(c"PRAGMA journal_mode = TRUNCATE")?.read_rows(|row| Ok(row.column_text(0)?.to_owned()))?;
        if modes.first().is_none_or(|mode| mode != "truncate") {
                return Err(io::Error::other("SQLite could not enable the memory database's TRUNCATE journal"));
        }
        drop(original);
        open_journal()?;
        Ok(())
}

fn prepare_schema(connection: &Connection) -> io::Result<()> {
        let versions = connection.prepare(c"PRAGMA user_version")?.read_rows(|row| Ok(row.column_integer(0)))?;
        let version = versions.first().copied().ok_or_else(|| io::Error::other("SQLite did not return a memory schema version"))?;
        if version > SCHEMA_VERSION {
                return Err(io::Error::new(io::ErrorKind::InvalidData, "input memory uses a newer schema"));
        }
        for sql in SCHEMA {
                connection.execute(sql)?;
        }
        if version < SCHEMA_VERSION {
                let legacy = connection.prepare(c"SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'core_memory'")?.read_rows(|_| Ok(()))?;
                if !legacy.is_empty() {
                        migrate_legacy(connection)?;
                }
                connection.execute(c"PRAGMA user_version = 2608")?;
        }
        Ok(())
}

fn migrate_legacy(connection: &Connection) -> io::Result<()> {
        let mut list = connection.prepare(c"SELECT word, romanization, frequency, latest FROM core_memory ORDER BY rowid")?;
        let mut insert = connection.prepare(
                c"
                INSERT OR IGNORE INTO memory2608 (word, romanization, frequency, latest, char_count, letter_count, complexity, anchors, spell)
                VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)
        ",
        )?;
        list.read_rows(|row| insert_entry(&mut insert, row.column_text(0)?, row.column_text(1)?, row.column_integer(2), row.column_integer(3)))?;
        Ok(())
}

pub(crate) fn insert_entry(statement: &mut Statement<'_>, word: &str, romanization: &str, frequency: i64, latest: i64) -> io::Result<()> {
        let fields = memory_serial_fields(word, romanization);
        statement.execute([
                Binding::Text(word),
                Binding::Text(romanization),
                frequency.into(),
                latest.into(),
                fields.char_count.into(),
                fields.letter_count.into(),
                fields.complexity.into(),
                fields.anchors.into(),
                fields.spell.into(),
        ])
}

#[cfg(test)]
mod tests {
        use std::fs;
        use std::path::PathBuf;
        use std::sync::Barrier;
        use std::sync::atomic::AtomicBool;
        use std::thread;

        use super::*;

        struct Fixture(PathBuf);

        impl Fixture {
                fn new(name: &str) -> Self {
                        let nonce = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos();
                        let directory = std::env::temp_dir().join(format!("jyutping-{name}-{}-{nonce}", std::process::id()));
                        fs::create_dir(&directory).unwrap();
                        Self(directory)
                }

                fn path(&self) -> PathBuf {
                        self.0.join("memory.sqlite3")
                }
        }

        impl Drop for Fixture {
                fn drop(&mut self) {
                        let _ = fs::remove_file(self.0.join("memory.sqlite3-journal"));
                        let _ = fs::remove_file(self.path());
                        let _ = fs::remove_dir(&self.0);
                }
        }

        fn learned_word() -> Lexicon {
                Lexicon {
                        kind: LexiconKind::Cantonese,
                        text: "𠁣𠃛".to_owned(),
                        romanization: "ngi1 nget1 hoeng1 gong2".to_owned(),
                        input: String::new(),
                        mark: String::new(),
                        number: 0,
                        attached: None,
                }
        }

        #[test]
        fn legacy_migration_retains_metadata_and_is_idempotent() {
                let fixture = Fixture::new("memory-migration");
                let path = fixture.path();
                let lexicon = learned_word();
                {
                        let legacy = Connection::open(&path, OpenMode::Create).unwrap();
                        legacy.execute(c"CREATE TABLE core_memory (word TEXT, romanization TEXT, frequency INTEGER, latest INTEGER)").unwrap();
                        legacy.prepare(c"INSERT INTO core_memory VALUES (?1, ?2, 7, 1234)")
                                .unwrap()
                                .execute([Binding::Text(&lexicon.text), Binding::Text(&lexicon.romanization)])
                                .unwrap();
                }
                let memory = InputMemory::open(&path).unwrap();
                let rows =
                        memory.0.prepare(c"SELECT frequency, latest, char_count, letter_count, complexity, anchors, spell FROM memory2608")
                                .unwrap()
                                .read_rows(|row| Ok([0, 1, 2, 3, 4, 5, 6].map(|column| row.column_integer(column))))
                                .unwrap();
                let spell = b"ngingethoenggong".iter().fold(0u64, |code, letter| code.wrapping_mul(100).wrapping_add(u64::from(letter - b'a' + 20))) as i64;
                assert_eq!(rows, [[7, 1234, 2, 16, 3454, 33332726, spell]]);
                memory.forget(&lexicon).unwrap();
                drop(memory);
                let reopened = InputMemory::open(&path).unwrap();
                assert!(reopened.0.prepare(c"SELECT 1 FROM memory2608").unwrap().read_rows(|_| Ok(())).unwrap().is_empty());
                assert_eq!(reopened.0.prepare(c"SELECT 1 FROM core_memory").unwrap().read_rows(|_| Ok(())).unwrap().len(), 1);
        }

        #[test]
        fn failed_migration_rolls_back() {
                let fixture = Fixture::new("memory-rollback");
                let path = fixture.path();
                {
                        let legacy = Connection::open(&path, OpenMode::Create).unwrap();
                        legacy.execute(c"CREATE TABLE core_memory (word TEXT, romanization TEXT, frequency INTEGER, latest INTEGER)").unwrap();
                        legacy.execute(c"INSERT INTO core_memory VALUES ('好', 'hou2', 3, 1), ('我', NULL, 4, 2)").unwrap();
                }
                assert!(InputMemory::open(&path).is_err());
                let legacy = Connection::open(&path, OpenMode::ReadOnly).unwrap();
                let version = legacy.prepare(c"PRAGMA user_version").unwrap().read_rows(|row| Ok(row.column_integer(0))).unwrap();
                assert_eq!(version, [0]);
                let table = legacy.prepare(c"SELECT 1 FROM sqlite_master WHERE name = 'memory2608'").unwrap().read_rows(|_| Ok(())).unwrap();
                assert!(table.is_empty());
        }

        #[test]
        fn concurrent_writes_survive_reopen_and_clear() {
                let fixture = Fixture::new("memory-concurrent");
                let path = fixture.path();
                let barrier = Barrier::new(2);
                thread::scope(|workers| {
                        for _ in 0..2 {
                                workers.spawn(|| {
                                        barrier.wait();
                                        let memory = InputMemory::open(&path).unwrap();
                                        for _ in 0..10 {
                                                memory.remember(&learned_word()).unwrap();
                                        }
                                });
                        }
                });

                let memory = InputMemory::open(&path).unwrap();
                let frequency = memory.0.prepare(c"SELECT frequency FROM memory2608").unwrap().read_rows(|row| Ok(row.column_integer(0))).unwrap();
                assert_eq!(frequency, [20]);
                memory.clear().unwrap();
                drop(memory);
                let reopened = InputMemory::open(&path).unwrap();
                assert!(reopened.0.prepare(c"SELECT 1 FROM memory2608").unwrap().read_rows(|_| Ok(())).unwrap().is_empty());
        }

        #[test]
        fn transfer_round_trip_and_failed_import_are_atomic() {
                let fixture = Fixture::new("memory-transfer");
                let memory = InputMemory::open(&fixture.path()).unwrap();
                let word = learned_word();
                memory.remember(&word).unwrap();
                memory.remember(&word).unwrap();
                let file = fixture.0.join("memory-20261120-120000.sqlite3");
                let cancelled = AtomicBool::new(false);
                fs::write(&file, b"older export").unwrap();
                memory.export(&file, &cancelled).unwrap();
                {
                        let exported = Connection::open(&file, OpenMode::ReadOnly).unwrap();
                        let version = exported.prepare(c"PRAGMA user_version").unwrap().read_rows(|row| Ok(row.column_integer(0))).unwrap();
                        let schema = exported.prepare(c"SELECT sql FROM sqlite_master WHERE sql IS NOT NULL ORDER BY type DESC, name").unwrap()
                                .read_rows(|row| Ok(row.column_text(0)?.to_owned())).unwrap();
                        assert_eq!(version, [1]);
                        assert_eq!(schema, [
                                "CREATE TABLE input_memory (word TEXT NOT NULL, romanization TEXT NOT NULL, frequency INTEGER NOT NULL, latest INTEGER NOT NULL, UNIQUE (word, romanization))",
                                "CREATE INDEX ix_input_memory_frequency ON input_memory (frequency)",
                                "CREATE INDEX ix_input_memory_latest ON input_memory (latest)",
                        ]);
                        let journal = exported.prepare(c"PRAGMA journal_mode").unwrap().read_rows(|row| Ok(row.column_text(0)?.to_owned())).unwrap();
                        assert_eq!(journal, ["delete"]);
                }
                memory.clear().unwrap();
                memory.import(&file, &cancelled).unwrap();
                memory.import(&file, &cancelled).unwrap();
                let persisted = memory.0.prepare(c"SELECT word, romanization, frequency FROM memory2608").unwrap()
                        .read_rows(|row| Ok((row.column_text(0)?.to_owned(), row.column_text(1)?.to_owned(), row.column_integer(2)))).unwrap();
                assert_eq!(persisted, [(word.text, word.romanization, 2)]);

                let generation = memory.generation().unwrap();
                let saved = fs::read(&file).unwrap();
                {
                        let unsupported = Connection::open(&file, OpenMode::ReadWrite).unwrap();
                        unsupported.execute(c"PRAGMA user_version = 2").unwrap();
                }
                assert!(memory.import(&file, &cancelled).is_err());
                fs::write(&file, &saved).unwrap();
                cancelled.store(true, std::sync::atomic::Ordering::Relaxed);
                assert_eq!(memory.import(&file, &cancelled).unwrap_err().kind(), io::ErrorKind::Interrupted);
                assert_eq!(memory.export(&file, &cancelled).unwrap_err().kind(), io::ErrorKind::Interrupted);
                assert_eq!(fs::read(&file).unwrap(), saved);
                assert_eq!(memory.generation().unwrap(), generation);
                fs::remove_file(file).unwrap();
        }

        #[test]
        fn import_accepts_mac_backups_and_merges_valid_rows() {
                let fixture = Fixture::new("memory-import-mac");
                let file = fixture.0.join("memory-20261120-120000.sqlite3");
                {
                        let backup = Connection::open(&file, OpenMode::Create).unwrap();
                        // A version 1 reader also accepts files without indexes or column constraints.
                        backup.execute(c"CREATE TABLE input_memory (word, romanization, frequency, latest)").unwrap();
                        backup.execute(c"PRAGMA user_version = 1").unwrap();
                        backup.execute(c"INSERT INTO input_memory VALUES
                                ('香港', 'hoeng1 gong2', 9, 1700000000000),
                                ('香港', 'hoeng1 gong2', 2, 1800000000000),
                                ('𠁣', 'ngi1', 3, 1800000000001),
                                ('no timestamp', 'mou5', 4, 0),
                                ('bad reading', 'MOU5', 4, 1800000000001),
                                ('bad frequency', 'mou5', '4', 1800000000001),
                                ('no reading', NULL, 4, 1800000000001)").unwrap();
                }
                let memory = InputMemory::open(&fixture.path()).unwrap();
                memory.import(&file, &AtomicBool::new(false)).unwrap();
                let rows = memory.0.prepare(c"SELECT word, romanization, frequency, latest, letter_count FROM memory2608 ORDER BY id").unwrap()
                        .read_rows(|row| Ok((row.column_text(0)?.to_owned(), row.column_text(1)?.to_owned(), row.column_integer(2), row.column_integer(3), row.column_integer(4))))
                        .unwrap();
                assert_eq!(rows, [
                        ("香港".into(), "hoeng1 gong2".into(), 9, 1800000000000, 9),
                        ("𠁣".into(), "ngi1".into(), 3, 1800000000001, 3),
                ]);
                fs::remove_file(file).unwrap();
        }
}
