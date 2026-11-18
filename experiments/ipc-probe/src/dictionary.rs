use std::ffi::{CStr, CString, c_char, c_int, c_void};
use std::io;
use std::path::Path;
use std::ptr;

#[link(name = "winsqlite3")]
unsafe extern "C" {
        fn sqlite3_open_v2(path: *const c_char, database: *mut *mut c_void, flags: c_int, vfs: *const c_char) -> c_int;
        fn sqlite3_close_v2(database: *mut c_void) -> c_int;
        fn sqlite3_exec(
                database: *mut c_void,
                sql: *const c_char,
                callback: unsafe extern "C" fn(*mut c_void, c_int, *mut *mut c_char, *mut *mut c_char) -> c_int,
                context: *mut c_void,
                error: *mut *mut c_char,
        ) -> c_int;
}

pub struct Dictionary(*mut c_void);

impl Dictionary {
        pub fn open(path: &Path) -> io::Result<Self> {
                const SQLITE_OPEN_READONLY: c_int = 1;
                let path = path.to_str().ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "dictionary path is not valid UTF-8"))?;
                let path = CString::new(path)?;
                let mut raw = ptr::null_mut();
                let status = unsafe { sqlite3_open_v2(path.as_ptr(), &mut raw, SQLITE_OPEN_READONLY, ptr::null()) };
                let dictionary = Self(raw);
                if status != 0 {
                        return Err(io::Error::other(format!("SQLite open failed ({status})")));
                }
                Ok(dictionary)
        }

        pub fn suggest(&self) -> io::Result<Option<String>> {
                // One exact-spell match against the shipped lexicon. hoeng has five letters and gong has four.
                let spell = b"hoenggong".iter().fold(0_i64, |code, letter| code.wrapping_mul(100).wrapping_add(i64::from(letter - b'a' + 20)));
                let sql = CString::new(format!("SELECT word || char(9) || romanization FROM lexicon_core WHERE spell = {spell} AND complexity = 54 ORDER BY rowid LIMIT 1"))?;
                let mut result: Option<String> = None;
                let status = unsafe { sqlite3_exec(self.0, sql.as_ptr(), read_candidate, ptr::from_mut(&mut result).cast(), ptr::null_mut()) };
                if status != 0 {
                        return Err(io::Error::other(format!("SQLite lookup failed ({status})")));
                }
                Ok(result)
        }
}

impl Drop for Dictionary {
        fn drop(&mut self) {
                unsafe { sqlite3_close_v2(self.0) };
        }
}

unsafe extern "C" fn read_candidate(context: *mut c_void, count: c_int, values: *mut *mut c_char, _: *mut *mut c_char) -> c_int {
        // The query supplies one non-NULL column. SQLite keeps its UTF-8 value valid during this callback.
        if count != 1 || unsafe { (*values).is_null() } {
                return 1;
        }
        let value = unsafe { CStr::from_ptr(*values) };
        let Ok(candidate) = value.to_str() else { return 1 };
        unsafe { *context.cast::<Option<String>>() = Some(candidate.to_owned()) };
        0
}
