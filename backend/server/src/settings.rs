use std::ffi::{OsStr, c_void};
use std::io;
use std::os::windows::ffi::OsStrExt;
use std::ptr;
use std::sync::Mutex;

pub const SETTING_COUNT: usize = 8;
const PREFERENCES: &str = "Software\\Jyutping\\Preferences";
const LEGACY: &str = "Software\\Jyutping\\Settings";
const VERSION: u32 = 1;
const KEY_QUERY_VALUE: u32 = 1;
const KEY_SET_VALUE: u32 = 2;
const REG_DWORD: u32 = 4;
const HKEY_CURRENT_USER: *mut c_void = 0x8000_0001u32 as i32 as isize as *mut c_void;

const FIELDS: [(&str, u32, u32, u32); SETTING_COUNT] = [
        ("InputMethodMode", 1, 2, 1),
        ("CharacterForm", 1, 2, 1),
        ("PunctuationForm", 1, 2, 1),
        ("CharacterVariant", 1, 4, 1),
        ("CandidatePageSize", 1, 10, 7),
        ("CandidateFontSize", 11, 24, 16),
        ("LabelFontSize", 11, 24, 13),
        ("CommentFontSize", 11, 24, 13),
];

#[link(name = "advapi32")]
unsafe extern "system" {
        fn RegOpenKeyExW(key: *mut c_void, path: *const u16, options: u32, access: u32, result: *mut *mut c_void) -> i32;
        fn RegCreateKeyExW(
                key: *mut c_void,
                path: *const u16,
                reserved: u32,
                class: *const u16,
                options: u32,
                access: u32,
                security: *const c_void,
                result: *mut *mut c_void,
                disposition: *mut u32,
        ) -> i32;
        fn RegQueryValueExW(key: *mut c_void, name: *const u16, reserved: *mut u32, kind: *mut u32, data: *mut u8, count: *mut u32) -> i32;
        fn RegSetValueExW(key: *mut c_void, name: *const u16, reserved: u32, kind: u32, data: *const u8, count: u32) -> i32;
        fn RegCloseKey(key: *mut c_void) -> i32;
}

struct RegistryKey(*mut c_void);

impl RegistryKey {
        fn open(path: &str) -> io::Result<Option<Self>> {
                let path = wide(path);
                let mut result = ptr::null_mut();
                let status = unsafe { RegOpenKeyExW(HKEY_CURRENT_USER, path.as_ptr(), 0, KEY_QUERY_VALUE, &mut result) };
                if status == 2 {
                        return Ok(None);
                }
                check(status)?;
                Ok(Some(Self(result)))
        }

        fn create(path: &str) -> io::Result<Self> {
                let path = wide(path);
                let mut result = ptr::null_mut();
                let status =
                        unsafe { RegCreateKeyExW(HKEY_CURRENT_USER, path.as_ptr(), 0, ptr::null(), 0, KEY_QUERY_VALUE | KEY_SET_VALUE, ptr::null(), &mut result, ptr::null_mut()) };
                check(status)?;
                Ok(Self(result))
        }

        fn get(&self, name: &str) -> io::Result<Option<u32>> {
                let name = wide(name);
                let mut value = 0u32;
                let mut kind = 0;
                let mut count = 4;
                let status = unsafe { RegQueryValueExW(self.0, name.as_ptr(), ptr::null_mut(), &mut kind, (&mut value as *mut u32).cast(), &mut count) };
                match status {
                        0 if kind == REG_DWORD && count == 4 => Ok(Some(value)),
                        0 | 2 | 234 => Ok(None),
                        _ => Err(io::Error::from_raw_os_error(status)),
                }
        }

        fn set(&self, name: &str, value: u32) -> io::Result<()> {
                let name = wide(name);
                let status = unsafe { RegSetValueExW(self.0, name.as_ptr(), 0, REG_DWORD, (&value as *const u32).cast(), 4) };
                check(status)
        }
}

impl Drop for RegistryKey {
        fn drop(&mut self) {
                unsafe { RegCloseKey(self.0) };
        }
}

fn wide(text: &str) -> Vec<u16> {
        OsStr::new(text).encode_wide().chain([0]).collect()
}

fn check(status: i32) -> io::Result<()> {
        if status == 0 { Ok(()) } else { Err(io::Error::from_raw_os_error(status)) }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SettingsSnapshot {
        pub revision: u64,
        pub values: [u32; SETTING_COUNT],
}

pub struct SettingsStore {
        path: String,
        legacy: Option<String>,
        snapshot: Mutex<SettingsSnapshot>,
}

impl SettingsStore {
        pub fn open(path: Option<String>) -> io::Result<Self> {
                let legacy = if path.is_none() { Some(LEGACY.to_owned()) } else { None };
                let path = path.unwrap_or_else(|| PREFERENCES.to_owned());
                if !path.starts_with("Software\\") || path.contains('\0') {
                        return Err(io::Error::new(io::ErrorKind::InvalidInput, "invalid registry preferences path"));
                }
                let values = read_values(&path, legacy.as_deref())?;
                Ok(Self { path, legacy, snapshot: Mutex::new(SettingsSnapshot { revision: 1, values }) })
        }

        pub fn get(&self) -> io::Result<SettingsSnapshot> {
                let mut snapshot = self.snapshot.lock().map_err(|_| io::Error::other("settings state unavailable"))?;
                self.refresh(&mut snapshot)?;
                Ok(*snapshot)
        }

        pub fn set(&self, index: u32, value: u32) -> io::Result<SettingsSnapshot> {
                let Some(&(name, minimum, maximum, _)) = FIELDS.get(index as usize) else {
                        return Err(io::Error::new(io::ErrorKind::InvalidInput, "unknown settings field"));
                };
                if !(minimum..=maximum).contains(&value) {
                        return Err(io::Error::new(io::ErrorKind::InvalidInput, "setting value outside the Windows range"));
                }
                let mut snapshot = self.snapshot.lock().map_err(|_| io::Error::other("settings state unavailable"))?;
                self.refresh(&mut snapshot)?;
                if snapshot.values[index as usize] == value {
                        return Ok(*snapshot);
                }
                let key = RegistryKey::create(&self.path)?;
                // During the transition, seed the legacy fields before publishing the first field-level update.
                if key.get("Version")?.is_none() {
                        if let Some(legacy) = self.legacy.as_deref().map(RegistryKey::open).transpose()?.flatten() {
                                for (name, minimum, maximum, _) in FIELDS {
                                        if key.get(name)?.is_none() {
                                                if let Some(value) = legacy.get(name)?.filter(|value| (minimum..=maximum).contains(value)) {
                                                        key.set(name, value)?;
                                                }
                                        }
                                }
                        }
                        key.set("Version", VERSION)?;
                }
                key.set(name, value)?;
                self.refresh(&mut snapshot)?;
                Ok(*snapshot)
        }

        fn refresh(&self, snapshot: &mut SettingsSnapshot) -> io::Result<()> {
                let values = read_values(&self.path, self.legacy.as_deref())?;
                if values != snapshot.values {
                        snapshot.values = values;
                        snapshot.revision += 1;
                }
                Ok(())
        }
}

fn read_values(path: &str, legacy: Option<&str>) -> io::Result<[u32; SETTING_COUNT]> {
        let mut values = FIELDS.map(|(_, _, _, default)| default);
        let key = match RegistryKey::open(path)? {
                Some(key) => Some(key),
                None => legacy.map(RegistryKey::open).transpose()?.flatten(),
        };
        if let Some(key) = key {
                for (index, (name, minimum, maximum, _)) in FIELDS.into_iter().enumerate() {
                        if let Some(value) = key.get(name)?.filter(|value| (minimum..=maximum).contains(value)) {
                                values[index] = value;
                        }
                }
        }
        Ok(values)
}
