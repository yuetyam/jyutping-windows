#![cfg_attr(feature = "installed", windows_subsystem = "windows")]

use std::env;
use std::fs;
use std::io;
use std::os::windows::ffi::OsStrExt;
use std::path::{Path, PathBuf};
use std::process::ExitCode;
use std::ptr;
use std::slice;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, OnceLock};

use jyutping_core::{Candidate, CangjieVariant, CharacterStandard, Engine, Lexicon, LexiconKind, ReverseLookup, RomanizationForm, exact_spell_code};
use jyutping_server::{Dictionary, InputMemory, SETTING_COUNT, SettingsSnapshot, SettingsStore};

static DICTIONARY_PATH: OnceLock<PathBuf> = OnceLock::new();
static MEMORY_PATH: OnceLock<PathBuf> = OnceLock::new();
static ENGINE: OnceLock<Engine> = OnceLock::new();
static SETTINGS: OnceLock<SettingsStore> = OnceLock::new();
static INSTALLED: OnceLock<()> = OnceLock::new();

unsafe extern "C" {
        fn RunJyutpingRpcServer(app_container: *const u16) -> u32;
        fn StopJyutpingRpcServer() -> u32;
}

fn run() -> io::Result<()> {
        let invalid_arguments = || {
                io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "usage: JyutpingServer --installed | --stop | (--import-memory | --export-memory) file.sqlite3 | [ime.sqlite3] [app-container-family] [--memory memory.sqlite3] [--settings | --settings-key HKCU-subkey]",
                )
        };
        let mut arguments = env::args_os().skip(1).peekable();
        if arguments.peek().is_some_and(|argument| argument == "--stop") {
                return match unsafe { StopJyutpingRpcServer() } {
                        0 => Ok(()),
                        status => Err(io::Error::from_raw_os_error(status as i32)),
                };
        }
        if arguments.peek().is_some_and(|argument| argument == "--import-memory" || argument == "--export-memory") {
                let importing = arguments.next().is_some_and(|argument| argument == "--import-memory");
                let path = arguments.next().map(PathBuf::from).ok_or_else(invalid_arguments)?;
                if arguments.next().is_some() {
                        return Err(invalid_arguments());
                }
                return transfer_memory(&path, importing);
        }
        let mut memory_path = None;
        let mut settings = None;
        let mut installed = false;
        let mut positional = Vec::new();
        while let Some(argument) = arguments.next() {
                if argument == "--installed" {
                        installed = true;
                } else if argument == "--memory" {
                        if memory_path.is_some() {
                                return Err(invalid_arguments());
                        }
                        memory_path = Some(PathBuf::from(arguments.next().ok_or_else(invalid_arguments)?));
                } else if argument == "--settings" || argument == "--settings-key" {
                        if settings.is_some() {
                                return Err(invalid_arguments());
                        }
                        settings = Some(if argument == "--settings-key" {
                                Some(arguments.next().and_then(|value| value.into_string().ok()).ok_or_else(invalid_arguments)?)
                        } else {
                                None
                        });
                } else if positional.len() < 2 {
                        positional.push(argument);
                } else {
                        return Err(invalid_arguments());
                }
        }
        if installed {
                if !positional.is_empty() || settings.is_some() || memory_path.is_some() {
                        return Err(invalid_arguments());
                }
                settings = Some(None);
                INSTALLED.set(()).expect("installed mode set once");
                if let Ok(path) = installed_memory_path() {
                        fs::create_dir_all(path.parent().unwrap())?;
                        memory_path = Some(path);
                }
        }
        let mut positional = positional.into_iter();
        let dictionary = match positional.next() {
                Some(path) => PathBuf::from(path),
                None => env::current_exe()?.with_file_name("ime.sqlite3"),
        };
        let app_container: Option<Vec<u16>> = positional.next().map(|name| name.encode_wide().chain([0]).collect());
        let dictionary = dictionary.canonicalize()?;
        let connection = Dictionary::open(&dictionary)?;
        connection.verify_schema()?;
        let engine = connection.load_engine()?;
        connection.lexicon_query()?;
        ENGINE.set(engine).ok().expect("engine set once");
        drop(connection);
        DICTIONARY_PATH.set(dictionary).expect("dictionary path set once");
        if let Some(path) = memory_path {
                InputMemory::open(&path)?;
                MEMORY_PATH.set(path.canonicalize()?).expect("memory path set once");
        }
        if let Some(path) = settings {
                SETTINGS.set(SettingsStore::open(path)?).ok().expect("settings store set once");
        }
        let app_container = app_container.as_ref().map_or(ptr::null(), Vec::as_ptr);
        let status = unsafe { RunJyutpingRpcServer(app_container) };
        if status == 0 { Ok(()) } else { Err(io::Error::other(format!("local RPC host failed ({status})"))) }
}

fn installed_memory_path() -> io::Result<PathBuf> {
        env::var_os("LOCALAPPDATA")
                .map(PathBuf::from)
                .filter(|path| path.is_absolute())
                .map(|path| path.join("Jyutping").join("memory.sqlite3"))
                .ok_or_else(|| io::Error::other("Local AppData is unavailable"))
}

fn transfer_memory(path: &Path, importing: bool) -> io::Result<()> {
        if !path.is_absolute() || !path.extension().is_some_and(|extension| extension.eq_ignore_ascii_case("sqlite3")) {
                return Err(io::Error::new(io::ErrorKind::InvalidInput, "expected an absolute path to a Jyutping memory backup (.sqlite3)"));
        }
        let memory_path = installed_memory_path()?;
        if !importing {
                if let Ok(destination) = path.canonicalize() {
                        let dictionary = env::current_exe()?.with_file_name("ime.sqlite3");
                        if destination == memory_path.canonicalize()? || dictionary.canonicalize().is_ok_and(|path| path == destination) {
                                return Err(io::Error::new(io::ErrorKind::InvalidInput, "a memory export cannot replace the live input memory or dictionary"));
                        }
                }
        }
        let cancelled = Arc::new(AtomicBool::new(false));
        let signal = Arc::clone(&cancelled);
        std::thread::spawn(move || {
                let mut command = String::new();
                if io::stdin().read_line(&mut command).is_ok() && command.trim() == "cancel" {
                        signal.store(true, Ordering::Relaxed);
                }
        });
        if importing { InputMemory::open_writer(&memory_path)?.import(path, &cancelled) } else { InputMemory::open_read_only(&memory_path)?.export(path, &cancelled) }
}

// Mirrors JyutpingEngineCandidate in the native MIDL adapter.
const FIELD_BYTES: usize = 256;

#[repr(C)]
struct CandidateField {
        length: u32,
        bytes: [u8; FIELD_BYTES],
}

#[repr(C)]
struct EngineCandidate {
        kind: u32,
        consumed_input: u32,
        text: CandidateField,
        comment: CandidateField,
        mark: CandidateField,
        original_text: CandidateField,
        romanization: CandidateField,
}

const _: () = assert!(std::mem::size_of::<EngineCandidate>() == 1308 && std::mem::align_of::<EngineCandidate>() == 4);

impl CandidateField {
        fn write(&mut self, value: &str) -> io::Result<()> {
                if value.len() > self.bytes.len() || value.contains('\0') {
                        return Err(io::Error::new(io::ErrorKind::InvalidData, "candidate field exceeds the RPC limits"));
                }
                self.length = value.len() as u32;
                self.bytes[..value.len()].copy_from_slice(value.as_bytes());
                Ok(())
        }
}

impl EngineCandidate {
        fn write(&mut self, candidate: &Candidate) -> io::Result<()> {
                let lexicon = &candidate.lexicon;
                self.kind = match lexicon.kind {
                        LexiconKind::Cantonese => 0,
                        LexiconKind::Text => 1,
                        LexiconKind::Emoji => 2,
                        LexiconKind::Symbol => 3,
                };
                self.consumed_input = lexicon.input_count() as u32;
                self.text.write(&candidate.text)?;
                self.comment.write(candidate.comment.as_deref().unwrap_or(""))?;
                self.mark.write(&lexicon.mark)?;
                self.original_text.write(&lexicon.text)?;
                self.romanization.write(&lexicon.romanization)
        }
}

fn query_candidates(input: &str, mode: u32, trigger_key: u32, standard: u32, form: u32) -> io::Result<(Vec<Candidate>, u64)> {
        let (reverse, expected_trigger) = match mode {
                0 => (None, 0),
                1 => (Some(ReverseLookup::Pinyin), b'r'),
                2 => (Some(ReverseLookup::Cangjie(CangjieVariant::Cangjie5)), b'v'),
                3 => (Some(ReverseLookup::Stroke), b'x'),
                4 => (Some(ReverseLookup::Structure), b'q'),
                _ => return Err(io::Error::new(io::ErrorKind::InvalidInput, "unknown lookup mode")),
        };
        let trigger = match trigger_key {
                0 => None,
                key if key == u32::from(expected_trigger) => Some(char::from(expected_trigger)),
                _ => return Err(io::Error::new(io::ErrorKind::InvalidInput, "trigger does not match the lookup mode")),
        };
        let standard = match standard {
                1..=9 | 51 => CharacterStandard::from(standard as i32),
                _ => return Err(io::Error::new(io::ErrorKind::InvalidInput, "unknown character standard")),
        };
        let form = match form {
                1 => RomanizationForm::Full,
                2 => RomanizationForm::Toneless,
                3 => RomanizationForm::Nothing,
                _ => return Err(io::Error::new(io::ErrorKind::InvalidInput, "unknown romanization form")),
        };
        let path = DICTIONARY_PATH.get().ok_or_else(|| io::Error::other("dictionary unavailable"))?;
        let engine = ENGINE.get().ok_or_else(|| io::Error::other("engine unavailable"))?;
        let dictionary = Dictionary::open(path)?;
        match reverse {
                Some(method) => dictionary.reverse_candidates(engine, method, input, trigger, standard, form).map(|items| (items, 0)),
                None => {
                        if let Some(memory) = MEMORY_PATH.get().and_then(|path| InputMemory::open_read_only(path).ok()) {
                                if let Ok(generation) = memory.generation() {
                                        if let Ok(items) = dictionary.suggest(engine, Some(&memory), input, standard, form) {
                                                return Ok((items, generation));
                                        }
                                }
                        }
                        dictionary.suggest(engine, None, input, standard, form).map(|items| (items, 0))
                }
        }
}

#[unsafe(no_mangle)]
extern "C" fn JyutpingMemoryEnabled() -> bool {
        MEMORY_PATH.get().is_some()
}

#[unsafe(no_mangle)]
extern "C" fn JyutpingMemoryTransferEnabled() -> bool {
        INSTALLED.get().is_some() && JyutpingMemoryEnabled()
}

#[unsafe(no_mangle)]
extern "C" fn JyutpingSettingsEnabled() -> bool {
        SETTINGS.get().is_some()
}

// The native adapter supplies one revision and exactly SETTING_COUNT writable DWORDs.
#[unsafe(no_mangle)]
unsafe extern "C" fn JyutpingGetSettings(known_revision: u64, revision: *mut u64, values: *mut u32, count: u32) -> u32 {
        let Some(settings) = SETTINGS.get() else { return 6 };
        if count as usize != SETTING_COUNT {
                return 2;
        }
        let values = unsafe { slice::from_raw_parts_mut(values, SETTING_COUNT) };
        settings_result(settings.get(), known_revision, unsafe { &mut *revision }, values)
}

#[unsafe(no_mangle)]
unsafe extern "C" fn JyutpingSetSetting(field: u32, value: u32, revision: *mut u64, values: *mut u32, count: u32) -> u32 {
        let Some(settings) = SETTINGS.get() else { return 6 };
        if count as usize != SETTING_COUNT {
                return 2;
        }
        let values = unsafe { slice::from_raw_parts_mut(values, SETTING_COUNT) };
        settings_result(settings.set(field, value), 0, unsafe { &mut *revision }, values)
}

fn settings_result(result: io::Result<SettingsSnapshot>, known_revision: u64, revision: &mut u64, values: &mut [u32]) -> u32 {
        match result {
                Ok(snapshot) => {
                        *revision = snapshot.revision;
                        if known_revision == snapshot.revision {
                                return 7;
                        }
                        values.copy_from_slice(&snapshot.values);
                        0
                }
                Err(error) if error.kind() == io::ErrorKind::InvalidInput => 2,
                Err(_) => 5,
        }
}

// The native adapter validates both lengths before calling Rust.
#[unsafe(no_mangle)]
unsafe extern "C" fn JyutpingMutateMemory(operation: u32, generation: u64, text: *const u8, text_length: u32, romanization: *const u8, romanization_length: u32) -> u32 {
        let Some(path) = MEMORY_PATH.get() else { return 6 };
        if operation == 3 {
                return if InputMemory::open_writer(path).and_then(|memory| memory.clear()).is_ok() { 0 } else { 5 };
        }
        let text = unsafe { slice::from_raw_parts(text, text_length as usize) };
        let romanization = unsafe { slice::from_raw_parts(romanization, romanization_length as usize) };
        let (Ok(text), Ok(romanization)) = (std::str::from_utf8(text), std::str::from_utf8(romanization)) else { return 2 };
        if text.contains('\0') || romanization.contains('\0') {
                return 2;
        }
        let lexicon = Lexicon {
                kind: LexiconKind::Cantonese,
                text: text.to_owned(),
                romanization: romanization.to_owned(),
                input: String::new(),
                mark: String::new(),
                number: 0,
                attached: None,
        };
        let result = InputMemory::open_writer(path).and_then(|memory| match operation {
                1 => memory.remember_at(&lexicon, generation),
                2 => memory.forget_at(&lexicon, generation),
                _ => Err(io::Error::new(io::ErrorKind::InvalidInput, "unknown input memory operation")),
        });
        match result {
                Ok(true) => 0,
                Ok(false) => 8,
                Err(error) if error.kind() == io::ErrorKind::InvalidInput => 2,
                Err(_) => 5,
        }
}

// The native adapter validates counts and initializes the output before calling Rust.
#[unsafe(no_mangle)]
unsafe extern "C" fn JyutpingSuggestCandidates(
        input: *const u8,
        count: u32,
        mode: u32,
        trigger_key: u32,
        standard: u32,
        form: u32,
        candidates: *mut EngineCandidate,
        limit: u32,
        candidate_count: *mut u32,
        has_more: *mut u32,
        memory_generation: *mut u64,
) -> u32 {
        let input = unsafe { slice::from_raw_parts(input, count as usize) };
        let Ok(input) = std::str::from_utf8(input) else { return 2 };
        let (results, generation) = match query_candidates(input, mode, trigger_key, standard, form) {
                Ok(results) => results,
                Err(error) if error.kind() == io::ErrorKind::InvalidInput => return 2,
                Err(_) => return 5,
        };
        if results.is_empty() {
                return 1;
        }
        let candidates = unsafe { slice::from_raw_parts_mut(candidates, limit as usize) };
        for (field, candidate) in candidates.iter_mut().zip(&results) {
                if field.write(candidate).is_err() {
                        return 5;
                }
        }
        unsafe {
                *candidate_count = limit.min(results.len() as u32);
                *has_more = u32::from(results.len() > limit as usize);
                *memory_generation = generation;
        }
        0
}

fn main() -> ExitCode {
        match run() {
                Ok(()) => ExitCode::SUCCESS,
                Err(error) if error.kind() == io::ErrorKind::Interrupted => ExitCode::from(2),
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
        match Dictionary::open(path).and_then(|dictionary| dictionary.suggest_exact(spell, text, romanization)) {
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
