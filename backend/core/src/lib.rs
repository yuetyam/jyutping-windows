/// Encode a bounded exact spelling using the bundled Windows lexicon's base-100 key.
pub fn exact_spell_code(input: &[u8]) -> Option<i64> {
        if input.is_empty() || input.len() > 9 || !input.iter().all(u8::is_ascii_lowercase) {
                return None;
        }
        // Every supported key fits in 18 decimal digits.
        Some(input.iter().fold(0, |code, letter| code * 100 + i64::from(letter - b'a' + 20)))
}

#[cfg(test)]
mod tests {
        use super::exact_spell_code;

        #[test]
        fn encodes_windows_lexicon_keys() {
                assert_eq!(exact_spell_code(b"hoenggong"), Some(273424332626343326));
                assert_eq!(exact_spell_code(b"zzzzzzzzz"), Some(454545454545454545));
        }

        #[test]
        fn rejects_input_outside_the_exact_lookup_slice() {
                for input in [b"".as_slice(), b"Hou", b"hou2", b"hou'", b"abcdefghij", "好".as_bytes()] {
                        assert_eq!(exact_spell_code(input), None);
                }
        }
}
