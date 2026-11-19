use std::cmp::Reverse;
use std::collections::HashSet;
use std::io;

use crate::normal::{apostrophe_consumed_input, scheme_complexity, scheme_length, scheme_origin_keys, validate_keys};
use crate::segmenter::Scheme;
use crate::tones::{is_tone_digit, is_tone_key, strip_tones, tone_converted};
use crate::{Engine, Lexicon, LexiconKind, anchor_key, combined_code, is_syllable_letter};

#[derive(Debug, PartialEq, Eq)]
pub struct MemorySerialFields {
        pub char_count: i64,
        pub letter_count: i64,
        pub complexity: i64,
        pub anchors: i64,
        pub spell: i64,
}

/// Derive lookup fields for the Windows memory2608 schema.
pub fn memory_serial_fields(word: &str, romanization: &str) -> MemorySerialFields {
        let toneless: String = romanization.chars().filter(|character| !character.is_ascii_digit()).collect();
        let syllables = toneless.split(' ').filter(|syllable| !syllable.is_empty());
        let complexity = syllables.clone().fold(0i64, |code, syllable| code.wrapping_mul(10).wrapping_add(syllable.encode_utf16().count() as i64));
        let anchors = syllables.filter_map(|syllable| match syllable.as_bytes()[0] {
                b'a'..=b'z' | b'\'' | 96 => Some(syllable.as_bytes()[0]),
                uppercase @ b'A'..=b'Z' => Some(uppercase.to_ascii_lowercase()),
                _ => None,
        });
        let letters = romanization.bytes().filter(u8::is_ascii_lowercase);
        MemorySerialFields {
                char_count: word.chars().count() as i64,
                letter_count: letters.clone().count() as i64,
                complexity,
                anchors: combined_code(anchors),
                spell: combined_code(letters),
        }
}

pub struct MemoryRow {
        pub word: String,
        pub romanization: String,
        pub frequency: i64,
        pub latest: i64,
}

pub trait MemorySource {
        fn by_anchors(&mut self, anchors: i64, count: usize, limit: i64) -> io::Result<Vec<MemoryRow>>;
        fn by_spell(&mut self, spell: i64, count: usize, complexity: i64, limit: i64) -> io::Result<Vec<MemoryRow>>;
}

struct Match {
        lexicon: Lexicon,
        frequency: i64,
        latest: i64,
}

impl Engine {
        pub fn suggest_memory(&self, source: &mut impl MemorySource, input: &str) -> io::Result<Vec<Lexicon>> {
                validate_keys(input)?;
                self.memory_with_segmentation(source, input, &self.segment(input))
        }

        pub(crate) fn memory_with_segmentation(&self, source: &mut dyn MemorySource, input: &str, schemes: &[Scheme<'_>]) -> io::Result<Vec<Lexicon>> {
                if input.is_empty() || input.starts_with('\'') && !input.bytes().any(is_tone_key) {
                        return Ok(Vec::new());
                }
                let has_apostrophe = input.contains('\'');
                let has_tone = input.bytes().any(is_tone_key);
                let letters;
                let search = if has_apostrophe || has_tone {
                        letters = input.bytes().filter(|&key| is_syllable_letter(key)).map(char::from).collect::<String>();
                        &letters
                } else {
                        input
                };
                let mut candidates = self.search_memory(source, search, schemes)?;
                if has_apostrophe || has_tone {
                        let converted = tone_converted(input);
                        let parts: Vec<_> = input.split('\'').filter(|part| !part.is_empty()).collect();
                        candidates.retain_mut(|candidate| {
                                let qualified = match (has_apostrophe, has_tone) {
                                        (true, true) => converted.starts_with(&candidate.romanization),
                                        (_, true) => matches_tone(input, &converted, candidate),
                                        (true, false) => matches_apostrophe(input, &parts, candidate),
                                        _ => unreachable!(),
                                };
                                if qualified {
                                        candidate.input = input.to_owned();
                                }
                                qualified
                        });
                }
                Ok(candidates)
        }

        fn search_memory(&self, source: &mut dyn MemorySource, input: &str, schemes: &[Scheme<'_>]) -> io::Result<Vec<Lexicon>> {
                let has_complete_scheme = schemes.iter().any(|scheme| scheme_length(scheme) == input.len());
                let mut queried = Vec::new();
                for scheme in schemes {
                        if has_complete_scheme && scheme_length(scheme) == input.len() {
                                for length in (1..=scheme.len()).rev() {
                                        queried.extend(query_scheme(source, &scheme[..length], if length == scheme.len() { 20 } else { 5 })?);
                                }
                        } else if !has_complete_scheme {
                                queried.extend(query_scheme(source, scheme, 5)?);
                        }
                }
                let limit = if queried.is_empty() { 20 } else { 5 };
                let (ideal, partial): (Vec<_>, Vec<_>) = queried.into_iter().partition(|item| item.lexicon.input_count() >= input.len());
                let anchors = query_anchors(source, input.bytes(), input, input, limit)?;
                let mut ideal = regular_sort(ideal);
                ideal.extend(regular_sort(anchors));
                let partial = sort_partial(partial);
                if ideal.is_empty() && (3..25).contains(&input.len()) && (!has_complete_scheme || input.starts_with('m') || input.ends_with('m')) {
                        let prefix_matches = self.memory_prefixes(source, input, schemes)?;
                        ideal.extend(sort_partial(prefix_matches).into_iter().take(5));
                }
                let result = ideal
                        .into_iter()
                        .map(|mut item| {
                                item.lexicon.number = -1;
                                item.lexicon
                        })
                        .chain(partial.into_iter().map(|mut item| {
                                item.lexicon.number = -2;
                                item.lexicon
                        }))
                        .collect();
                Ok(result)
        }

        fn memory_prefixes(&self, source: &mut dyn MemorySource, input: &str, schemes: &[Scheme<'_>]) -> io::Result<Vec<Match>> {
                let mut result = Vec::new();
                for scheme in schemes.iter().filter(|scheme| scheme.len() <= 9) {
                        let tail = &input[scheme_length(scheme)..];
                        if tail.is_empty() {
                                continue;
                        }
                        let origin = scheme.iter().map(|syllable| syllable.origin).collect::<Vec<_>>().join(" ");
                        let mark = scheme.iter().map(|syllable| syllable.alias).chain([tail]).collect::<Vec<_>>().join(" ");
                        let anchors: Vec<_> = scheme.iter().map(|syllable| syllable.alias.as_bytes()[0]).collect();
                        let conjoined: Vec<_> = anchors.iter().copied().chain(tail.bytes()).collect();
                        let matches = query_anchors(source, conjoined.into_iter(), input, &mark, 100)?;
                        result.extend(matches.into_iter().filter(|item| {
                                strip_tones(&item.lexicon.romanization)
                                        .strip_prefix(&origin)
                                        .is_some_and(|suffix| suffix.split(' ').filter_map(|syllable| syllable.bytes().next()).eq(tail.bytes().map(anchor_key)))
                        }));

                        let first = tail.as_bytes()[0];
                        let prefix = format!("{origin} {}{}", char::from(anchor_key(first)), &tail[1..]);
                        let anchors: Vec<_> = anchors.into_iter().chain([first]).collect();
                        let matches = query_anchors(source, anchors.into_iter(), input, &mark, 100)?;
                        result.extend(matches.into_iter().filter(|item| strip_tones(&item.lexicon.romanization).starts_with(&prefix)));
                }
                for length in (1..input.len()).rev().filter(|&length| length <= 9 && input.len() - length < 6) {
                        let tail = &input[length - 1..];
                        let matches = query_anchors(source, input[..length].bytes(), input, input, 100)?;
                        result.extend(matches.into_iter().filter(|item| {
                                let romanization = &item.lexicon.romanization;
                                let letters: String = romanization.chars().filter(char::is_ascii_alphabetic).collect();
                                letters.starts_with(input)
                                        || romanization.split(' ').rfind(|syllable| !syllable.is_empty()).is_some_and(|last| {
                                                let last = strip_tones(last);
                                                self.segmenter.syllable_text(tail).map_or_else(|| last.starts_with(tail), |origin| last == origin)
                                        })
                        }));
                }
                Ok(result)
        }
}

fn query_scheme(source: &mut dyn MemorySource, scheme: &[crate::Syllable<'_>], limit: i64) -> io::Result<Vec<Match>> {
        let count = scheme.iter().map(|syllable| syllable.origin.len()).sum();
        let rows = source.by_spell(combined_code(scheme_origin_keys(scheme)), count, scheme_complexity(scheme), limit)?;
        let input = scheme.iter().map(|syllable| syllable.alias).collect::<String>();
        let mark = scheme.iter().map(|syllable| syllable.alias).collect::<Vec<_>>().join(" ");
        Ok(matches_from_rows(rows, &input, &mark))
}

fn query_anchors(source: &mut dyn MemorySource, keys: impl ExactSizeIterator<Item = u8>, input: &str, mark: &str, limit: i64) -> io::Result<Vec<Match>> {
        let count = keys.len();
        let rows = source.by_anchors(combined_code(keys.map(anchor_key)), count, limit)?;
        Ok(matches_from_rows(rows, input, mark))
}

fn matches_from_rows(rows: Vec<MemoryRow>, input: &str, mark: &str) -> Vec<Match> {
        rows.into_iter()
                .map(|row| Match {
                        lexicon: Lexicon {
                                kind: LexiconKind::Cantonese,
                                text: row.word,
                                romanization: row.romanization,
                                input: input.to_owned(),
                                mark: mark.to_owned(),
                                number: 0,
                                attached: None,
                        },
                        frequency: row.frequency,
                        latest: row.latest,
                })
                .collect()
}

fn regular_sort(mut items: Vec<Match>) -> Vec<Match> {
        let mut by_date: Vec<_> = (0..items.len()).collect();
        by_date.sort_by_key(|&index| Reverse(items[index].latest));
        let mut by_frequency: Vec<_> = (0..items.len()).collect();
        by_frequency.sort_by_key(|&index| Reverse(items[index].frequency));
        let order = by_frequency.iter().take(3).chain(by_date.iter().take(5)).chain(&by_frequency);
        let mut seen = HashSet::new();
        let selected: Vec<_> = order
                .copied()
                .filter(|&index| {
                        let item = &items[index].lexicon;
                        seen.insert((&item.text, &item.romanization))
                })
                .collect();
        drop(seen);
        let mut items: Vec<_> = items.drain(..).map(Some).collect();
        selected.into_iter().map(|index| items[index].take().unwrap()).collect()
}

fn sort_partial(mut items: Vec<Match>) -> Vec<Match> {
        items.sort_by_key(|item| Reverse(item.lexicon.input_count()));
        let mut groups = Vec::<Vec<Match>>::new();
        for item in items {
                if let Some(group) = groups.last_mut() {
                        if group[0].lexicon.input_count() == item.lexicon.input_count() {
                                group.push(item);
                                continue;
                        }
                }
                groups.push(vec![item]);
        }
        groups.into_iter().flat_map(regular_sort).collect()
}

fn matches_tone(input: &str, converted: &str, item: &Lexicon) -> bool {
        let reading = item.romanization.replace(' ', "");
        if reading == converted {
                return true;
        }
        let input_tones: Vec<_> = converted.bytes().filter(|&key| is_tone_digit(key)).collect();
        let tones: Vec<_> = reading.bytes().filter(|&key| is_tone_digit(key)).collect();
        let trailing = converted.bytes().last().is_some_and(is_tone_digit);
        match (input_tones.as_slice(), tones.as_slice()) {
                ([typed], [tone]) => trailing && converted.len() == item.input_count() + 1 && typed == tone,
                ([typed], [_, last]) if trailing => typed == last && converted.as_bytes().get(item.input_count()).copied().is_some_and(is_tone_digit),
                ([typed], [first, _]) => typed == first,
                ([_, _], [_, _]) => trailing && input_tones == tones && item.input_count() == converted.len() - 2,
                _ => input == reading,
        }
}

fn matches_apostrophe(input: &str, parts: &[&str], item: &Lexicon) -> bool {
        let romanization = strip_tones(&item.romanization);
        let syllables = romanization.split(' ').filter(|syllable| !syllable.is_empty()).count();
        let separators = input.bytes().filter(|&key| key == b'\'').count();
        let trailing = input.ends_with('\'');
        if romanization.split(' ').filter(|syllable| !syllable.is_empty()).eq(parts.iter().copied()) {
                return true;
        }
        let eligible = match (separators, trailing) {
                (1, true) => syllables == 1,
                (1, false) | (2, true) => syllables == 2,
                (2 | 3, _) => parts.len() == 3 && syllables == 3 && input.len() == separators + 3,
                _ => false,
        };
        eligible && apostrophe_consumed_input(input, parts, &romanization, &item.input).is_some()
}
