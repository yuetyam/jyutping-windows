use std::fs::{self, OpenOptions};
use std::io;
use std::path::Path;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

use crate::memory::{InputMemory, insert_entry};
use crate::sqlite::{Binding, Connection, OpenMode, Statement};

// Keep the portable schema in sync with the macOS InputMemoryExporter.
const SCHEMA: &std::ffi::CStr = c"CREATE TABLE input_memory (word TEXT NOT NULL, romanization TEXT NOT NULL, frequency INTEGER NOT NULL, latest INTEGER NOT NULL, UNIQUE (word, romanization));";

struct Record<'a> {
        word: &'a str,
        romanization: &'a str,
        frequency: i64,
        latest: i64,
}

impl<'a> Record<'a> {
        fn read(row: &'a Statement<'_>) -> io::Result<Self> {
                if !row.column_is_text(0) || !row.column_is_text(1) || !row.column_is_integer(2) || !row.column_is_integer(3) {
                        return Err(invalid_data());
                }
                Ok(Self {
                        word: row.column_text(0)?,
                        romanization: row.column_text(1)?,
                        frequency: row.column_integer(2),
                        latest: row.column_integer(3),
                })
        }

        fn is_valid(&self) -> bool {
                !self.word.is_empty()
                        && !self.word.contains('\0')
                        && self.frequency > 0
                        && self.latest > 0
                        && self.romanization.bytes().any(|byte| byte.is_ascii_lowercase())
                        && self.romanization.bytes().all(|byte| matches!(byte, b'a'..=b'z' | b'1'..=b'6' | b' '))
        }
}

impl InputMemory {
        pub fn export(&self, path: &Path, cancelled: &AtomicBool) -> io::Result<()> {
                check_cancelled(cancelled)?;
                let nonce = SystemTime::now().duration_since(UNIX_EPOCH).map_err(io::Error::other)?.as_nanos();
                let temporary = path.with_file_name(format!(".jyutping-memory-{}-{nonce}.tmp", std::process::id()));
                OpenOptions::new().write(true).create_new(true).open(&temporary)?;
                let result = (|| {
                        let output = Connection::open(&temporary, OpenMode::ReadWrite)?;
                        output.prepare(c"PRAGMA journal_mode = DELETE")?.read_rows(|_| Ok(()))?;
                        output.transaction(|| {
                                output.execute(SCHEMA)?;
                                output.execute(c"PRAGMA user_version = 1")?;
                                let mut select = self.0.prepare(c"SELECT word, romanization, frequency, latest FROM memory2608 ORDER BY id")?;
                                let mut insert = output.prepare(c"INSERT INTO input_memory (word, romanization, frequency, latest) VALUES (?1, ?2, ?3, ?4)")?;
                                select.visit_rows(|row| {
                                        check_cancelled(cancelled)?;
                                        let record = Record::read(row)?;
                                        insert.execute([Binding::Text(record.word), Binding::Text(record.romanization), record.frequency.into(), record.latest.into()])
                                })?;
                                check_cancelled(cancelled)?;
                                output.execute(c"CREATE INDEX ix_input_memory_frequency ON input_memory (frequency);")?;
                                check_cancelled(cancelled)?;
                                output.execute(c"CREATE INDEX ix_input_memory_latest ON input_memory (latest);")?;
                                check_cancelled(cancelled)
                        })?;
                        drop(output);
                        check_cancelled(cancelled)?;
                        fs::rename(&temporary, path)
                })();
                if result.is_err() {
                        let _ = fs::remove_file(&temporary);
                }
                result
        }

        pub fn import(&self, path: &Path, cancelled: &AtomicBool) -> io::Result<()> {
                check_cancelled(cancelled)?;
                let generation = self.generation()?;
                let source = Connection::open(path, OpenMode::ReadOnly)?;
                source.execute(c"PRAGMA trusted_schema = OFF")?;
                // Keep schema validation and row reads on the same SQLite snapshot.
                source.execute(c"BEGIN")?;
                validate_source(&source)?;
                check_cancelled(cancelled)?;
                let mut records = source.prepare(c"SELECT word, romanization, frequency, latest FROM input_memory")?;
                self.0.transaction(|| {
                        if self.generation()? != generation {
                                return Err(io::Error::other("input memory changed while opening the import"));
                        }
                        let mut insert = self.0.prepare(
                                c"
                                INSERT INTO memory2608 (word, romanization, frequency, latest, char_count, letter_count, complexity, anchors, spell)
                                VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)
                        ",
                        )?;
                        let mut update = self.0.prepare(
                                c"
                                UPDATE memory2608 SET frequency = MAX(frequency, ?1), latest = MAX(latest, ?2)
                                WHERE word = ?3 AND romanization = ?4
                        ",
                        )?;
                        records.visit_rows(|row| {
                                check_cancelled(cancelled)?;
                                let Ok(record) = Record::read(row) else {
                                        return Ok(());
                                };
                                if record.is_valid() {
                                        update.execute([record.frequency.into(), record.latest.into(), Binding::Text(record.word), Binding::Text(record.romanization)])?;
                                        if self.0.changes() == 0 {
                                                insert_entry(&mut insert, record.word, record.romanization, record.frequency, record.latest)?;
                                        }
                                }
                                Ok(())
                        })?;
                        check_cancelled(cancelled)?;
                        self.0.execute(c"UPDATE memory2608_generation SET generation = generation + 1 WHERE id = 1")
                })
        }
}

fn validate_source(source: &Connection) -> io::Result<()> {
        let version = source.prepare(c"PRAGMA user_version")?.read_rows(|row| Ok(row.column_integer(0)))?;
        if version != [1] {
                return Err(invalid_data());
        }
        let table = source.prepare(c"SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'input_memory'")?.read_rows(|_| Ok(()))?;
        if table.is_empty() {
                return Err(invalid_data());
        }
        let columns = source.prepare(c"SELECT count(*) FROM pragma_table_info('input_memory') WHERE name IN ('word', 'romanization', 'frequency', 'latest')")?
                .read_rows(|row| Ok(row.column_integer(0)))?;
        let integrity = source.prepare(c"PRAGMA integrity_check(1)")?.read_rows(|row| Ok(row.column_text(0)? == "ok"))?;
        if columns != [4] || integrity != [true] {
                return Err(invalid_data());
        }
        Ok(())
}

fn check_cancelled(cancelled: &AtomicBool) -> io::Result<()> {
        if cancelled.load(Ordering::Relaxed) {
                return Err(io::Error::new(io::ErrorKind::Interrupted, "memory transfer cancelled"));
        }
        Ok(())
}

fn invalid_data() -> io::Error {
        io::Error::new(io::ErrorKind::InvalidData, "invalid Jyutping input memory database")
}
