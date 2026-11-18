use std::ffi::{CString, c_char, c_int, c_void};
use std::io;
use std::path::Path;
use std::ptr;
use std::slice;
use std::str;

#[link(name = "winsqlite3")]
unsafe extern "C" {
        fn sqlite3_open_v2(path: *const c_char, database: *mut *mut c_void, flags: c_int, vfs: *const c_char) -> c_int;
        fn sqlite3_close_v2(database: *mut c_void) -> c_int;
        fn sqlite3_prepare_v2(database: *mut c_void, sql: *const c_char, length: c_int, statement: *mut *mut c_void, tail: *mut *const c_char) -> c_int;
        fn sqlite3_finalize(statement: *mut c_void) -> c_int;
        fn sqlite3_bind_int64(statement: *mut c_void, index: c_int, value: i64) -> c_int;
        fn sqlite3_step(statement: *mut c_void) -> c_int;
        fn sqlite3_column_text(statement: *mut c_void, column: c_int) -> *const u8;
        fn sqlite3_column_bytes(statement: *mut c_void, column: c_int) -> c_int;
}

pub struct Dictionary(*mut c_void);
struct Statement(*mut c_void);

impl Dictionary {
        pub fn open(path: &Path) -> io::Result<Self> {
                const SQLITE_OPEN_READONLY: c_int = 1;
                let path = path.to_str().ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "dictionary path is not valid UTF-8"))?;
                let path = CString::new(path)?;
                let mut database = ptr::null_mut();
                let status = unsafe { sqlite3_open_v2(path.as_ptr(), &mut database, SQLITE_OPEN_READONLY, ptr::null()) };
                let dictionary = Self(database);
                check(status, "open")?;
                Ok(dictionary)
        }

        fn prepare_lookup(&self) -> io::Result<Statement> {
                let sql = c"SELECT word, romanization FROM lexicon_core WHERE spell = ?1 ORDER BY rowid LIMIT 1";
                let mut statement = ptr::null_mut();
                let status = unsafe { sqlite3_prepare_v2(self.0, sql.as_ptr(), -1, &mut statement, ptr::null_mut()) };
                check(status, "prepare lookup")?;
                Ok(Statement(statement))
        }

        pub fn verify_schema(&self) -> io::Result<()> {
                self.prepare_lookup()?;
                Ok(())
        }

        pub fn suggest(&self, spell: i64, text: &mut [u8], romanization: &mut [u8]) -> io::Result<Option<(usize, usize)>> {
                const SQLITE_ROW: c_int = 100;
                const SQLITE_DONE: c_int = 101;
                let statement = self.prepare_lookup()?;
                check(unsafe { sqlite3_bind_int64(statement.0, 1, spell) }, "bind spelling")?;
                match unsafe { sqlite3_step(statement.0) } {
                        SQLITE_ROW => Ok(Some((statement.copy_column(0, text)?, statement.copy_column(1, romanization)?))),
                        SQLITE_DONE => Ok(None),
                        status => Err(io::Error::other(format!("SQLite lookup failed ({status})"))),
                }
        }
}

impl Statement {
        fn copy_column(&self, column: c_int, destination: &mut [u8]) -> io::Result<usize> {
                let value = unsafe { sqlite3_column_text(self.0, column) };
                let length = unsafe { sqlite3_column_bytes(self.0, column) } as usize;
                if value.is_null() || length == 0 || length > destination.len() {
                        return Err(io::Error::other("dictionary value is missing or exceeds the protocol limit"));
                }
                // SQLite owns these bytes until the statement is stepped or finalized.
                let bytes = unsafe { slice::from_raw_parts(value, length) };
                if bytes.contains(&0) || str::from_utf8(bytes).is_err() {
                        return Err(io::Error::other("dictionary value is not valid text"));
                }
                destination[..length].copy_from_slice(bytes);
                Ok(length)
        }
}

impl Drop for Dictionary {
        fn drop(&mut self) {
                unsafe { sqlite3_close_v2(self.0) };
        }
}

impl Drop for Statement {
        fn drop(&mut self) {
                unsafe { sqlite3_finalize(self.0) };
        }
}

fn check(status: c_int, operation: &str) -> io::Result<()> {
        if status == 0 {
                Ok(())
        } else {
                Err(io::Error::other(format!("SQLite {operation} failed ({status})")))
        }
}
