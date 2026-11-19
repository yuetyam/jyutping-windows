use std::ffi::{CStr, CString, c_char, c_int, c_void};
use std::io;
use std::path::Path;
use std::ptr;
use std::slice;
use std::str;

const SQLITE_ROW: c_int = 100;
const SQLITE_DONE: c_int = 101;

#[link(name = "winsqlite3")]
unsafe extern "C" {
        fn sqlite3_open_v2(path: *const c_char, database: *mut *mut c_void, flags: c_int, vfs: *const c_char) -> c_int;
        fn sqlite3_close_v2(database: *mut c_void) -> c_int;
        fn sqlite3_busy_timeout(database: *mut c_void, milliseconds: c_int) -> c_int;
        fn sqlite3_changes(database: *mut c_void) -> c_int;
        fn sqlite3_prepare_v2(database: *mut c_void, sql: *const c_char, length: c_int, statement: *mut *mut c_void, tail: *mut *const c_char) -> c_int;
        fn sqlite3_finalize(statement: *mut c_void) -> c_int;
        fn sqlite3_reset(statement: *mut c_void) -> c_int;
        fn sqlite3_clear_bindings(statement: *mut c_void) -> c_int;
        fn sqlite3_bind_int64(statement: *mut c_void, index: c_int, value: i64) -> c_int;
        fn sqlite3_bind_text(statement: *mut c_void, index: c_int, text: *const c_char, length: c_int, destructor: Option<unsafe extern "C" fn(*mut c_void)>) -> c_int;
        fn sqlite3_malloc(bytes: c_int) -> *mut c_void;
        fn sqlite3_free(value: *mut c_void);
        fn sqlite3_step(statement: *mut c_void) -> c_int;
        fn sqlite3_column_type(statement: *mut c_void, column: c_int) -> c_int;
        fn sqlite3_column_int64(statement: *mut c_void, column: c_int) -> i64;
        fn sqlite3_column_text(statement: *mut c_void, column: c_int) -> *const u8;
        fn sqlite3_column_bytes(statement: *mut c_void, column: c_int) -> c_int;
}

pub(crate) struct Connection(*mut c_void);

pub(crate) enum OpenMode {
        ReadOnly,
        ReadWrite,
        Create,
}
pub(crate) struct Statement<'a> {
        handle: *mut c_void,
        _connection: &'a Connection,
}

pub(crate) enum Binding<'a> {
        Integer(i64),
        Text(&'a str),
}

impl From<i64> for Binding<'_> {
        fn from(value: i64) -> Self {
                Self::Integer(value)
        }
}

impl Connection {
        pub(crate) fn open(path: &Path, mode: OpenMode) -> io::Result<Self> {
                const SQLITE_OPEN_READONLY: c_int = 0x01;
                const SQLITE_OPEN_READWRITE: c_int = 0x02;
                const SQLITE_OPEN_CREATE: c_int = 0x04;
                let flags = match mode {
                        OpenMode::ReadOnly => SQLITE_OPEN_READONLY,
                        OpenMode::ReadWrite => SQLITE_OPEN_READWRITE,
                        OpenMode::Create => SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                };
                let path = path.to_str().ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "database path is not valid UTF-8"))?;
                let path = CString::new(path)?;
                let mut database = ptr::null_mut();
                let status = unsafe { sqlite3_open_v2(path.as_ptr(), &mut database, flags, ptr::null()) };
                let connection = Self(database);
                check(status, "open")?;
                Ok(connection)
        }

        pub(crate) fn set_busy_timeout(&self, milliseconds: c_int) -> io::Result<()> {
                check(unsafe { sqlite3_busy_timeout(self.0, milliseconds) }, "set busy timeout")
        }

        pub(crate) fn execute(&self, sql: &CStr) -> io::Result<()> {
                self.prepare(sql)?.execute([])
        }

        pub(crate) fn changes(&self) -> c_int {
                unsafe { sqlite3_changes(self.0) }
        }

        pub(crate) fn transaction<T>(&self, operation: impl FnOnce() -> io::Result<T>) -> io::Result<T> {
                self.execute(c"BEGIN IMMEDIATE")?;
                let result = operation().and_then(|value| {
                        self.execute(c"COMMIT")?;
                        Ok(value)
                });
                if result.is_err() {
                        let _ = self.execute(c"ROLLBACK");
                }
                result
        }

        pub(crate) fn prepare(&self, sql: &CStr) -> io::Result<Statement<'_>> {
                let mut statement = ptr::null_mut();
                let status = unsafe { sqlite3_prepare_v2(self.0, sql.as_ptr(), -1, &mut statement, ptr::null_mut()) };
                check(status, "prepare statement")?;
                Ok(Statement { handle: statement, _connection: self })
        }
}

impl Statement<'_> {
        fn bind_integer(&self, index: c_int, value: i64) -> io::Result<()> {
                check(unsafe { sqlite3_bind_int64(self.handle, index, value) }, "bind integer")
        }

        pub(crate) fn column_integer(&self, column: c_int) -> i64 {
                unsafe { sqlite3_column_int64(self.handle, column) }
        }

        pub(crate) fn column_is_integer(&self, column: c_int) -> bool {
                const SQLITE_INTEGER: c_int = 1;
                (unsafe { sqlite3_column_type(self.handle, column) }) == SQLITE_INTEGER
        }

        pub(crate) fn column_is_text(&self, column: c_int) -> bool {
                const SQLITE_TEXT: c_int = 3;
                (unsafe { sqlite3_column_type(self.handle, column) }) == SQLITE_TEXT
        }

        fn bind(&self, index: c_int, value: Binding<'_>) -> io::Result<()> {
                match value {
                        Binding::Integer(value) => self.bind_integer(index, value),
                        Binding::Text(value) => {
                                let length = c_int::try_from(value.len()).map_err(|_| io::Error::new(io::ErrorKind::InvalidInput, "SQLite text binding exceeds its byte limit"))?;
                                // SQLite frees its copy even if binding fails.
                                let copy = unsafe { sqlite3_malloc(length.max(1)) };
                                if copy.is_null() {
                                        return Err(io::Error::other("SQLite could not allocate a text binding"));
                                }
                                unsafe { ptr::copy_nonoverlapping(value.as_ptr(), copy.cast(), value.len()) };
                                check(unsafe { sqlite3_bind_text(self.handle, index, copy.cast(), length, Some(sqlite3_free)) }, "bind text")
                        }
                }
        }

        pub(crate) fn query<'a, T, B: Into<Binding<'a>>>(&mut self, bindings: impl IntoIterator<Item = B>, read_row: impl Fn(&Self) -> io::Result<T>) -> io::Result<Vec<T>> {
                let rows = (|| {
                        for (index, value) in bindings.into_iter().enumerate() {
                                self.bind(index as c_int + 1, value.into())?;
                        }
                        self.read_rows(read_row)
                })();
                let reset = check(unsafe { sqlite3_reset(self.handle) }, "reset statement");
                let clear = check(unsafe { sqlite3_clear_bindings(self.handle) }, "clear bindings");
                let rows = rows?;
                reset?;
                clear?;
                Ok(rows)
        }

        pub(crate) fn execute<'a>(&mut self, bindings: impl IntoIterator<Item = Binding<'a>>) -> io::Result<()> {
                self.query(bindings, |_| Err::<(), _>(io::Error::other("unexpected SQLite result row")))?;
                Ok(())
        }

        pub(crate) fn read_rows<T>(&mut self, mut read_row: impl FnMut(&Self) -> io::Result<T>) -> io::Result<Vec<T>> {
                let mut rows = Vec::new();
                self.visit_rows(|statement| {
                        rows.push(read_row(statement)?);
                        Ok(())
                })?;
                Ok(rows)
        }

        pub(crate) fn visit_rows(&mut self, mut read_row: impl FnMut(&Self) -> io::Result<()>) -> io::Result<()> {
                loop {
                        match unsafe { sqlite3_step(self.handle) } {
                                SQLITE_ROW => read_row(self)?,
                                SQLITE_DONE => return Ok(()),
                                status => return Err(io::Error::other(format!("SQLite lookup failed ({status})"))),
                        }
                }
        }

        pub(crate) fn column_text(&self, column: c_int) -> io::Result<&str> {
                let value = unsafe { sqlite3_column_text(self.handle, column) };
                let length = unsafe { sqlite3_column_bytes(self.handle, column) } as usize;
                if value.is_null() {
                        return Err(io::Error::other("SQLite text value is missing"));
                }
                // SQLite owns these bytes until the statement is stepped or finalized.
                let bytes = unsafe { slice::from_raw_parts(value, length) };
                str::from_utf8(bytes).map_err(|error| io::Error::new(io::ErrorKind::InvalidData, error))
        }
}

impl Drop for Connection {
        fn drop(&mut self) {
                unsafe { sqlite3_close_v2(self.0) };
        }
}

impl Drop for Statement<'_> {
        fn drop(&mut self) {
                unsafe { sqlite3_finalize(self.handle) };
        }
}

fn check(status: c_int, operation: &str) -> io::Result<()> {
        if status == 0 { Ok(()) } else { Err(io::Error::other(format!("SQLite {operation} failed ({status})"))) }
}
