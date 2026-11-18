use std::ffi::c_void;
use std::fs::File;
use std::io;
use std::os::windows::io::{AsRawHandle, FromRawHandle, OwnedHandle, RawHandle};
use std::ptr;

#[repr(C)]
struct SecurityAttributes {
        length: u32,
        descriptor: *mut c_void,
        inherit_handle: i32,
}

#[link(name = "kernel32")]
unsafe extern "system" {
        fn GetCurrentProcess() -> RawHandle;
        fn GetCurrentProcessId() -> u32;
        fn ProcessIdToSessionId(process_id: u32, session_id: *mut u32) -> i32;
        fn LocalFree(memory: *mut c_void) -> *mut c_void;
        fn CreateNamedPipeW(name: *const u16, open_mode: u32, pipe_mode: u32, max_instances: u32, out_size: u32, in_size: u32, timeout: u32, attributes: *const SecurityAttributes) -> RawHandle;
        fn ConnectNamedPipe(pipe: RawHandle, overlapped: *mut c_void) -> i32;
        fn DisconnectNamedPipe(pipe: RawHandle) -> i32;
        fn FlushFileBuffers(file: RawHandle) -> i32;
        fn GetNamedPipeClientSessionId(pipe: RawHandle, session_id: *mut u32) -> i32;
}

#[link(name = "advapi32")]
unsafe extern "system" {
        fn OpenProcessToken(process: RawHandle, access: u32, token: *mut RawHandle) -> i32;
        fn GetTokenInformation(token: RawHandle, class: u32, data: *mut c_void, length: u32, written: *mut u32) -> i32;
        fn ConvertSidToStringSidW(sid: *const c_void, text: *mut *mut u16) -> i32;
        fn ConvertStringSecurityDescriptorToSecurityDescriptorW(text: *const u16, revision: u32, descriptor: *mut *mut c_void, size: *mut u32) -> i32;
}

struct LocalAllocation<T>(*mut T);

impl<T> Drop for LocalAllocation<T> {
        fn drop(&mut self) {
                unsafe { LocalFree(self.0.cast()) };
        }
}

pub struct Pipe {
        file: File,
        session_id: u32,
}

impl Pipe {
        pub fn listen() -> io::Result<Self> {
                const PIPE_ACCESS_DUPLEX: u32 = 3;
                const FILE_FLAG_FIRST_PIPE_INSTANCE: u32 = 0x0008_0000;
                const PIPE_TYPE_MESSAGE: u32 = 4;
                const PIPE_READMODE_MESSAGE: u32 = 2;
                const PIPE_REJECT_REMOTE_CLIENTS: u32 = 8;

                let mut session_id = 0;
                check(unsafe { ProcessIdToSessionId(GetCurrentProcessId(), &mut session_id) })?;
                let sid = user_sid()?;
                let name = format!(r"\\.\pipe\Jyutping.IpcProbe.v0.{session_id}.{sid}");
                // The client can read data/attributes/owner and write data/attributes. Creating pipe instances requires a separate right.
                let security = wide(&format!("O:{sid}D:P(A;;0x00120183;;;{sid})"));
                let mut descriptor = ptr::null_mut();
                check(unsafe { ConvertStringSecurityDescriptorToSecurityDescriptorW(security.as_ptr(), 1, &mut descriptor, ptr::null_mut()) })?;
                let descriptor = LocalAllocation(descriptor);
                let attributes = SecurityAttributes { length: size_of::<SecurityAttributes>() as u32, descriptor: descriptor.0, inherit_handle: 0 };
                let name_wide = wide(&name);
                let handle = unsafe {
                        CreateNamedPipeW(name_wide.as_ptr(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_REJECT_REMOTE_CLIENTS, 1, 256, 32, 0, &attributes)
                };
                if handle as isize == -1 {
                        return Err(io::Error::last_os_error());
                }
                Ok(Self { file: unsafe { File::from_raw_handle(handle) }, session_id })
        }

        pub fn connect(&self) -> io::Result<()> {
                const ERROR_NO_DATA: i32 = 232;
                const ERROR_PIPE_CONNECTED: i32 = 535;
                let handle = self.file.as_raw_handle();
                loop {
                        if let Err(error) = check(unsafe { ConnectNamedPipe(handle, ptr::null_mut()) }) {
                                match error.raw_os_error() {
                                        Some(ERROR_NO_DATA) => {
                                                // The peer closed after opening the pipe but before ConnectNamedPipe.
                                                self.disconnect()?;
                                                continue;
                                        }
                                        Some(ERROR_PIPE_CONNECTED) => {}
                                        _ => return Err(error),
                                }
                        }
                        let mut peer_session = 0;
                        if unsafe { GetNamedPipeClientSessionId(handle, &mut peer_session) } != 0 && peer_session == self.session_id {
                                return Ok(());
                        }
                        self.disconnect()?;
                }
        }

        pub fn stream(&mut self) -> &mut File {
                &mut self.file
        }

        pub fn disconnect(&self) -> io::Result<()> {
                check(unsafe { DisconnectNamedPipe(self.file.as_raw_handle()) })
        }

        pub fn flush(&self) {
                // Keep the response available until the client reads it or closes the handle.
                unsafe { FlushFileBuffers(self.file.as_raw_handle()) };
        }
}

fn user_sid() -> io::Result<String> {
        const TOKEN_QUERY: u32 = 8;
        const TOKEN_USER: u32 = 1;
        let mut token = ptr::null_mut();
        check(unsafe { OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &mut token) })?;
        let token = unsafe { OwnedHandle::from_raw_handle(token) };
        let mut length = 0;
        unsafe { GetTokenInformation(token.as_raw_handle(), TOKEN_USER, ptr::null_mut(), 0, &mut length) };
        if length == 0 {
                return Err(io::Error::last_os_error());
        }
        let mut data = vec![0_usize; (length as usize).div_ceil(size_of::<usize>())];
        check(unsafe { GetTokenInformation(token.as_raw_handle(), TOKEN_USER, data.as_mut_ptr().cast(), length, &mut length) })?;
        // TOKEN_USER begins with a SID pointer whose storage is inside the aligned token buffer.
        let mut text = ptr::null_mut();
        check(unsafe { ConvertSidToStringSidW(data[0] as *const c_void, &mut text) })?;
        let text = LocalAllocation(text);
        let length = (0..).take_while(|&index| unsafe { *text.0.add(index) } != 0).count();
        Ok(String::from_utf16_lossy(unsafe { std::slice::from_raw_parts(text.0, length) }))
}

fn wide(text: &str) -> Vec<u16> {
        text.encode_utf16().chain(Some(0)).collect()
}

fn check(ok: i32) -> io::Result<()> {
        if ok == 0 { Err(io::Error::last_os_error()) } else { Ok(()) }
}
