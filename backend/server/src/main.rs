mod dictionary;

use std::env;
use std::io;
use std::os::windows::ffi::OsStrExt;
use std::path::PathBuf;
use std::process::ExitCode;
use std::ptr;
use std::slice;
use std::sync::OnceLock;

use dictionary::Dictionary;
use jyutping_core::exact_spell_code;

static DICTIONARY_PATH: OnceLock<PathBuf> = OnceLock::new();

unsafe extern "C" {
        fn RunJyutpingRpcServer(app_container: *const u16) -> u32;
}

fn run() -> io::Result<()> {
        let mut arguments = env::args_os().skip(1);
        let dictionary = match arguments.next() {
                Some(path) => PathBuf::from(path),
                None => env::current_exe()?.with_file_name("ime.sqlite3"),
        };
        let app_container: Option<Vec<u16>> = arguments.next().map(|name| name.encode_wide().chain([0]).collect());
        if arguments.next().is_some() {
                return Err(io::Error::new(io::ErrorKind::InvalidInput, "usage: JyutpingServer [ime.sqlite3] [app-container-family]"));
        }
        let dictionary = dictionary.canonicalize()?;
        Dictionary::open(&dictionary)?.verify_schema()?;
        DICTIONARY_PATH.set(dictionary).expect("dictionary path set once");
        let app_container = app_container.as_ref().map_or(ptr::null(), Vec::as_ptr);
        let status = unsafe { RunJyutpingRpcServer(app_container) };
        if status == 0 {
                Ok(())
        } else {
                Err(io::Error::other(format!("local RPC host failed ({status})")))
        }
}

fn main() -> ExitCode {
        match run() {
                Ok(()) => ExitCode::SUCCESS,
                Err(error) => {
                        eprintln!("JyutpingServer: {error}");
                        ExitCode::FAILURE
                }
        }
}

// The native RPC adapter provides bounded, disjoint buffers valid for this call.
#[unsafe(no_mangle)]
unsafe extern "C" fn JyutpingSuggestExact(
        spelling: *const u8,
        input_count: u32,
        text: *mut u8,
        romanization: *mut u8,
        capacity: u32,
        text_length: *mut u32,
        romanization_length: *mut u32,
) -> i32 {
        let spelling = unsafe { slice::from_raw_parts(spelling, input_count as usize) };
        let Some(spell) = exact_spell_code(spelling) else { return -2 };
        let Some(path) = DICTIONARY_PATH.get() else { return -1 };
        let text = unsafe { slice::from_raw_parts_mut(text, capacity as usize) };
        let romanization = unsafe { slice::from_raw_parts_mut(romanization, capacity as usize) };
        match Dictionary::open(path).and_then(|dictionary| dictionary.suggest(spell, text, romanization)) {
                Ok(Some((text_bytes, romanization_bytes))) => {
                        unsafe {
                                *text_length = text_bytes as u32;
                                *romanization_length = romanization_bytes as u32;
                        }
                        1
                }
                Ok(None) => 0,
                Err(_) => -1,
        }
}
