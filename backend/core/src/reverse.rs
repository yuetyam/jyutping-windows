use std::collections::HashSet;
use std::io;

use crate::normal::{scheme_complexity, scheme_length, scheme_origin_keys, validate_keys};
use crate::tones::{is_tone_digit, strip_tones, tone_converted};
use crate::{Engine, Lexicon, LexiconKind, LexiconRow, combined_code, is_syllable_letter, pinyin};

const SHAPE_LIMIT: i64 = 100;
const CANGJIE_ROOTS: [char; 26] =
        ['日', '月', '金', '木', '水', '火', '土', '竹', '戈', '十', '大', '中', '一', '弓', '人', '心', '手', '口', '尸', '廿', '山', '女', '田', '難', '卜', '重'];

#[derive(Clone, Copy)]
pub enum CangjieVariant {
        Cangjie5,
        Cangjie3,
        Quick5,
        Quick3,
}

#[derive(Clone, Copy)]
pub enum ReverseLookup {
        Pinyin,
        Cangjie(CangjieVariant),
        Stroke,
        Structure,
}

pub struct ShapeRow {
        pub number: i64,
        pub word: String,
        pub complexity: i64,
}

pub trait ReverseLookupSource {
        fn romanizations(&mut self, word: &str) -> io::Result<Vec<String>>;
        fn pinyin_anchors(&mut self, anchors: i64, count: usize, limit: i64) -> io::Result<Vec<LexiconRow>>;
        fn pinyin_spell(&mut self, spell: i64, complexity: i64, limit: i64) -> io::Result<Vec<LexiconRow>>;
        fn cangjie_exact(&mut self, variant: CangjieVariant, code: i64) -> io::Result<Vec<ShapeRow>>;
        fn cangjie_prefix(&mut self, variant: CangjieVariant, pattern: &str, limit: i64) -> io::Result<Vec<ShapeRow>>;
        fn stroke_exact(&mut self, code: i64, count: usize) -> io::Result<Vec<ShapeRow>>;
        fn stroke_pattern(&mut self, pattern: &str, exact_wildcard: bool, limit: i64) -> io::Result<Vec<ShapeRow>>;
        fn structures(&mut self, spell: i64, complexity: i64) -> io::Result<Vec<(String, String)>>;
}

impl Engine {
        pub fn reverse_lookup(&self, source: &mut impl ReverseLookupSource, method: ReverseLookup, input: &str) -> io::Result<Vec<Lexicon>> {
                validate_keys(input)?;
                if input.is_empty() {
                        return Ok(Vec::new());
                }
                match method {
                        ReverseLookup::Pinyin => pinyin::reverse_lookup(&self.pinyin_segmenter, source, input),
                        ReverseLookup::Cangjie(variant) => cangjie(source, variant, input),
                        ReverseLookup::Stroke => stroke(source, input),
                        ReverseLookup::Structure => self.structure(source, input),
                }
        }

        fn structure(&self, source: &mut impl ReverseLookupSource, input: &str) -> io::Result<Vec<Lexicon>> {
                let letters = input.bytes().filter(|&key| is_syllable_letter(key)).count();
                let schemes = self.segment(input);
                let mut rows = Vec::new();
                for scheme in schemes.iter().filter(|scheme| scheme_length(scheme) == letters) {
                        rows.extend(source.structures(combined_code(scheme_origin_keys(scheme)), scheme_complexity(scheme))?);
                }
                let converted = tone_converted(input);
                let has_tone_letter = converted != input;
                let has_apostrophe = input.contains('\'');
                let mark = if letters != input.len() {
                        let mut mark = String::new();
                        for character in converted.chars() {
                                mark.push(character);
                                if !character.is_ascii_alphabetic() {
                                        mark.push(' ');
                                }
                        }
                        mark
                } else if let Some(best) = schemes.first() {
                        best.iter().map(|syllable| syllable.alias).collect::<Vec<_>>().join(" ")
                } else {
                        input.to_owned()
                };
                let mut seen = HashSet::new();
                let mut result = Vec::new();
                for (word, components) in rows {
                        if matches_structure(input, &converted, &components, has_tone_letter, has_apostrophe) && seen.insert(word.clone()) {
                                result.extend(reverse_word(source, &word, input, &mark)?);
                        }
                }
                Ok(result)
        }
}

fn matches_structure(input: &str, converted: &str, components: &str, tone_letter: bool, apostrophe: bool) -> bool {
        if !tone_letter {
                return !apostrophe || strip_tones(components).split(' ').filter(|part| !part.is_empty()).eq(input.split('\'').filter(|part| !part.is_empty()));
        }
        let input_tones: Vec<_> = converted.bytes().filter(|&key| is_tone_digit(key)).collect();
        let tones: Vec<_> = components.bytes().filter(|&key| is_tone_digit(key)).collect();
        let matches_edge = if converted.bytes().last().is_some_and(is_tone_digit) { tones.ends_with(&input_tones) } else { tones.starts_with(&input_tones) };
        if apostrophe {
                return input_tones.len() == 1 && matches_edge;
        }
        components.replace(' ', "").starts_with(converted)
                || match input_tones.len() {
                        1 => matches_edge,
                        2 => input_tones == tones,
                        _ => false,
                }
}

fn cangjie(source: &mut impl ReverseLookupSource, variant: CangjieVariant, input: &str) -> io::Result<Vec<Lexicon>> {
        if !input.bytes().all(|key| key.is_ascii_lowercase()) {
                return Ok(Vec::new());
        }
        let mark: String = input.bytes().map(|key| CANGJIE_ROOTS[(key - b'a') as usize]).collect();
        let count = input.len() as i64;
        let mut shapes = source.cangjie_exact(variant, combined_code(input.bytes()))?;
        if matches!(variant, CangjieVariant::Quick5 | CangjieVariant::Quick3) {
                shapes.retain(|row| row.complexity == count);
        }
        for row in &mut shapes {
                row.complexity = count;
        }
        shapes.extend(source.cangjie_prefix(variant, &format!("{input}*"), SHAPE_LIMIT)?);
        map_shapes(source, shapes, input, &mark)
}

fn stroke_key(key: u8) -> Option<(u8, char)> {
        match key {
                b'w' | b'h' | b't' | b'j' | b'1' => Some((b'1', '⼀')),
                b's' | b'k' | b'2' => Some((b'2', '⼁')),
                b'a' | b'p' | b'l' | b'3' => Some((b'3', '⼃')),
                b'd' | b'n' | b'u' | b'4' => Some((b'4', '⼂')),
                b'z' | b'i' | b'5' => Some((b'5', '乛')),
                b'x' | b'o' | b'6' => Some((b'6', '＊')),
                _ => None,
        }
}

fn stroke(source: &mut impl ReverseLookupSource, keys: &str) -> io::Result<Vec<Lexicon>> {
        let mut input = String::new();
        let mut mark = String::new();
        let mut pattern = String::new();
        for (key, root) in keys.bytes().filter_map(stroke_key) {
                input.push(char::from(key));
                mark.push(root);
                if key == b'6' {
                        pattern.push_str("[12345]");
                } else {
                        pattern.push(char::from(key));
                }
        }
        let mut shapes = if input.contains('6') {
                source.stroke_pattern(&pattern, true, SHAPE_LIMIT)?
        } else {
                let code = input.bytes().fold(0i64, |code, key| code.wrapping_mul(10).wrapping_add(i64::from(key - b'0')));
                source.stroke_exact(code, input.len())?
        };
        pattern.push('*');
        shapes.extend(source.stroke_pattern(&pattern, false, SHAPE_LIMIT)?);
        map_shapes(source, shapes, &input, &mark)
}

fn map_shapes(source: &mut impl ReverseLookupSource, mut shapes: Vec<ShapeRow>, input: &str, mark: &str) -> io::Result<Vec<Lexicon>> {
        let mut seen = HashSet::new();
        shapes.retain(|row| seen.insert(row.word.clone()));
        shapes.sort_by_key(|row| (row.complexity, row.number));
        let mut result = Vec::new();
        for row in shapes {
                result.extend(reverse_word(source, &row.word, input, mark)?);
        }
        Ok(result)
}

pub(crate) fn reverse_word(source: &mut impl ReverseLookupSource, word: &str, input: &str, mark: &str) -> io::Result<Vec<Lexicon>> {
        if word.is_empty() {
                return Ok(Vec::new());
        }
        let mut readings = source.romanizations(word)?;
        if readings.is_empty() {
                let mut tail = word;
                let mut combined = Vec::new();
                // The dictionary may only have readings for pieces of a shape lookup's word.
                while !tail.is_empty() {
                        let mut matched = None;
                        for (prefix_end, _) in tail.char_indices().rev().filter(|&(end, _)| end > 0) {
                                if let Some(reading) = source.romanizations(&tail[..prefix_end])?.into_iter().next() {
                                        matched = Some((prefix_end, reading));
                                        break;
                                }
                        }
                        let Some((end, reading)) = matched else { return Ok(Vec::new()) };
                        combined.push(reading);
                        tail = &tail[end..];
                        if let Some(reading) = source.romanizations(tail)?.into_iter().next() {
                                combined.push(reading);
                                break;
                        }
                }
                readings.push(combined.join(" "));
        }
        Ok(readings
                .into_iter()
                .map(|romanization| Lexicon {
                        kind: LexiconKind::Cantonese,
                        text: word.to_owned(),
                        romanization,
                        input: input.to_owned(),
                        mark: mark.to_owned(),
                        number: 0,
                        attached: None,
                })
                .collect())
}
