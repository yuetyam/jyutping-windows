mod candidate;
mod character_standard;
mod extra_entries;
mod memory;
mod normal;
mod pinyin;
mod reverse;
mod segmenter;
mod tones;
mod variant_phrases;

pub use candidate::{Candidate, RomanizationForm, present_candidates};
pub use character_standard::{CharacterStandard, convert_text};
pub use memory::{MemoryRow, MemorySerialFields, MemorySource, memory_serial_fields};
pub use normal::{Engine, Lexicon, LexiconKind, LexiconRow, LexiconSource, SymbolRow};
pub use reverse::{CangjieVariant, ReverseLookup, ReverseLookupSource, ShapeRow};
pub use segmenter::{Segmenter, Syllable};

/// Encode a bounded exact spelling using the bundled Windows lexicon's base-100 key.
pub fn exact_spell_code(input: &[u8]) -> Option<i64> {
        if input.is_empty() || input.len() > 9 || !input.iter().all(u8::is_ascii_lowercase) {
                return None;
        }
        Some(combined_code(input.iter().copied()))
}

fn letter_code(letter: u8) -> i64 {
        i64::from(letter - b'a' + 20)
}

fn combined_code(keys: impl IntoIterator<Item = u8>) -> i64 {
        keys.into_iter().fold(0i64, |code, key| {
                let key_code = match key {
                        b'a'..=b'z' => letter_code(key),
                        b'0'..=b'9' => i64::from(key - b'0' + 10),
                        b'\'' => 47,
                        96 => 48,
                        _ => unreachable!("expected a Windows input key"),
                };
                code.wrapping_mul(100).wrapping_add(key_code)
        })
}

fn is_syllable_letter(key: u8) -> bool {
        key.is_ascii_lowercase() && !matches!(key, b'v' | b'x' | b'q')
}

fn anchor_key(key: u8) -> u8 {
        if key == b'y' { b'j' } else { key }
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
