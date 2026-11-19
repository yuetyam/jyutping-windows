use std::collections::HashMap;
use std::io;
use std::sync::LazyLock;

use crate::variant_phrases::{MUTILATED, PRC_GENERAL};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CharacterStandard {
        Preset = 1,
        Custom = 2,
        Inherited = 3,
        Etymology = 4,
        OpenCC = 5,
        HongKong = 6,
        Taiwan = 7,
        PrcGeneral = 8,
        AncientBooksPublishing = 9,
        Mutilated = 51,
}

impl From<i32> for CharacterStandard {
        fn from(value: i32) -> Self {
                match value {
                        2 => Self::Custom,
                        3 => Self::Inherited,
                        4 => Self::Etymology,
                        5 => Self::OpenCC,
                        6 => Self::HongKong,
                        7 => Self::Taiwan,
                        8 => Self::PrcGeneral,
                        9 => Self::AncientBooksPublishing,
                        51 => Self::Mutilated,
                        _ => Self::Preset,
                }
        }
}

struct Phrases {
        replacements: HashMap<&'static str, &'static str>,
        max_length: usize,
}

impl Phrases {
        fn new(entries: &[(&'static str, &'static str)]) -> Self {
                let mut replacements = HashMap::with_capacity(entries.len());
                let mut max_length = 0;
                for &(source, target) in entries {
                        replacements.entry(source).or_insert(target);
                        max_length = max_length.max(source.chars().count());
                }
                Self { replacements, max_length }
        }
}

static PRC_PHRASES: LazyLock<Phrases> = LazyLock::new(|| Phrases::new(PRC_GENERAL));
static SIMPLIFIED_PHRASES: LazyLock<Phrases> = LazyLock::new(|| Phrases::new(MUTILATED));

/// Convert display text using the existing Windows phrase overrides and scalar variant table.
pub fn convert_text(text: &str, standard: CharacterStandard, mut lookup: impl FnMut(char) -> io::Result<Option<char>>) -> io::Result<String> {
        let phrases = match standard {
                CharacterStandard::PrcGeneral => &*PRC_PHRASES,
                CharacterStandard::Mutilated => &*SIMPLIFIED_PHRASES,
                CharacterStandard::Inherited | CharacterStandard::HongKong | CharacterStandard::Taiwan | CharacterStandard::AncientBooksPublishing => {
                        return text.chars().map(|character| lookup(character).map(|variant| variant.unwrap_or(character))).collect();
                }
                CharacterStandard::Preset | CharacterStandard::Custom | CharacterStandard::Etymology | CharacterStandard::OpenCC => return Ok(text.to_owned()),
        };

        if let Some(&replacement) = phrases.replacements.get(text) {
                return Ok(replacement.to_owned());
        }
        let mut offsets: Vec<_> = text.char_indices().map(|(index, _)| index).collect();
        if offsets.len() <= 2 {
                return text.chars().map(|character| lookup(character).map(|variant| variant.unwrap_or(character))).collect();
        }
        offsets.push(text.len());
        let mut converted = String::with_capacity(text.len());
        let mut index = 0;
        while index + 1 < offsets.len() {
                let max_length = phrases.max_length.min(offsets.len() - 1 - index);
                let matched_phrase = (2..=max_length).rev().find_map(|length| {
                        let phrase = &text[offsets[index]..offsets[index + length]];
                        phrases.replacements.get(phrase).map(|target| (length, target))
                });
                if let Some((length, target)) = matched_phrase {
                        converted.push_str(target);
                        index += length;
                        continue;
                }

                let character = text[offsets[index]..offsets[index + 1]].chars().next().unwrap();
                let mapped = if is_ideographic(character) { lookup(character)?.unwrap_or(character) } else { character };
                converted.push(mapped);
                index += 1;
        }
        Ok(converted)
}

fn is_ideographic(character: char) -> bool {
        matches!(u32::from(character),
                0x3007 | 0x3400..=0x4DBF | 0x4E00..=0x9FFF | 0x20000..=0x2A6DF | 0x2A700..=0x2B73F
                | 0x2B740..=0x2B81F | 0x2B820..=0x2CEAF | 0x2CEB0..=0x2EBEF | 0x2EBF0..=0x2EE5F
                | 0x30000..=0x3134F | 0x31350..=0x323AF | 0x323B0..=0x33479)
}
