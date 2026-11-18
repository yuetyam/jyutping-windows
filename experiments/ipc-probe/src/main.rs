mod pipe;

use std::env;
use std::io::{self, Read, Write};
use std::path::PathBuf;
use std::thread;
use std::time::{Duration, Instant};

use jyutping_ipc_probe::Dictionary;
use pipe::Pipe;

fn main() -> io::Result<()> {
        let mut args = env::args_os().skip(1);
        let path = args.next().map(PathBuf::from).ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "usage: jyutping-ipc-probe <ime.sqlite3> [delay-ms]"))?;
        let delay = match args.next() {
                None => Duration::ZERO,
                Some(value) => Duration::from_millis(value.to_str().and_then(|value| value.parse().ok()).filter(|&value| value <= 10_000).ok_or_else(|| {
                        io::Error::new(io::ErrorKind::InvalidInput, "delay-ms must be between 0 and 10000")
                })?),
        };
        if args.next().is_some() {
                return Err(io::Error::new(io::ErrorKind::InvalidInput, "unexpected argument"));
        }

        let start = Instant::now();
        let dictionary = Dictionary::open(&path)?;
        let mut pipe = Pipe::listen()?;
        println!("Listening (startup_us={})", start.elapsed().as_micros());
        loop {
                pipe.connect()?;
                match reply(pipe.stream(), &dictionary, delay) {
                        Ok(()) => pipe.flush(),
                        Err(error) => eprintln!("Probe request ended: {error}"),
                }
                pipe.disconnect()?;
        }
}

fn reply(stream: &mut std::fs::File, dictionary: &Dictionary, delay: Duration) -> io::Result<()> {
        let mut request = [0_u8; 32];
        let count = stream.read(&mut request)?;
        if &request[..count] != b"JYP0 hoenggong" {
                return stream.write_all(b"ERR request");
        }
        thread::sleep(delay);
        let start = Instant::now();
        let response = match dictionary.suggest() {
                Ok(Some(candidate)) if candidate.len() <= 252 => format!("OK\t{candidate}"),
                Ok(None) => "EMPTY".to_owned(),
                Ok(Some(_)) => "ERR size".to_owned(),
                Err(error) => {
                        eprintln!("{error}");
                        "ERR dictionary".to_owned()
                }
        };
        println!("lookup_us={}", start.elapsed().as_micros());
        stream.write_all(response.as_bytes())
}
