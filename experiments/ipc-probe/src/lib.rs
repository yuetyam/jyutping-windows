mod dictionary;

pub use dictionary::Dictionary;

#[cfg(feature = "rpc-probe")]
mod rpc {
        use std::ffi::OsString;
        use std::os::windows::ffi::OsStringExt;
        use std::path::Path;
        use std::slice;

        use crate::Dictionary;

        // The C++ probe supplies a UTF-16 path and the fixed-size buffers declared in ProbeRpc.idl.
        #[unsafe(no_mangle)]
        unsafe extern "C" fn JyutpingProbeSuggest(path: *const u16, path_length: u32, input: *const u8, output: *mut u8) -> i32 {
                let input = unsafe { slice::from_raw_parts(input, 9) };
                let length = input.iter().position(|&letter| letter == 0).unwrap_or(input.len());
                if input[length..].iter().any(|&letter| letter != 0) {
                        return -2;
                }
                let path = OsString::from_wide(unsafe { slice::from_raw_parts(path, path_length as usize) });
                let result = Dictionary::open(Path::new(&path)).and_then(|dictionary| dictionary.suggest_exact(&input[..length]));
                match result {
                        Ok(Some(candidate)) if candidate.len() <= 256 => {
                                let output = unsafe { slice::from_raw_parts_mut(output, 256) };
                                output[..candidate.len()].copy_from_slice(candidate.as_bytes());
                                candidate.len() as i32
                        }
                        Ok(None) => 0,
                        Err(error) if error.kind() == std::io::ErrorKind::InvalidInput => -2,
                        _ => -1,
                }
        }
}
