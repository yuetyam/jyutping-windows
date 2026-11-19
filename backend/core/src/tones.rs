use std::cmp::Reverse;

use crate::{Lexicon, is_syllable_letter};

pub(crate) fn is_tone_key(key: u8) -> bool {
        matches!(key, b'v' | b'x' | b'q') || is_tone_digit(key)
}

pub(crate) fn is_tone_digit(key: u8) -> bool {
        (b'1'..=b'6').contains(&key)
}

pub(crate) fn strip_tones(romanization: &str) -> String {
        romanization.chars().filter(|character| !matches!(character, '1'..='6')).collect()
}

pub(crate) fn tone_converted(input: &str) -> String {
        let mut keys = input.bytes().peekable();
        let mut converted = String::with_capacity(input.len());
        while let Some(key) = keys.next() {
                let mut digit = match key {
                        b'v' => b'1',
                        b'x' => b'2',
                        b'q' => b'3',
                        _ => key,
                };
                if digit != key && keys.next_if_eq(&key).is_some() {
                        digit += 3;
                }
                converted.push(char::from(digit));
        }
        converted
}

pub(crate) fn filter_tones(input: &str, candidates: Vec<Lexicon>, has_apostrophe: bool) -> Vec<Lexicon> {
        let converted = tone_converted(input);
        if has_apostrophe {
                // Windows matches the combined form directly against the recorded romanization.
                return candidates
                        .into_iter()
                        .filter_map(|mut item| {
                                converted.starts_with(&item.romanization).then(|| {
                                        item.input = input.to_owned();
                                        item
                                })
                        })
                        .collect();
        }

        let input_tones: Vec<_> = converted.bytes().filter(|&key| is_tone_digit(key)).collect();
        let ends_in_tone = converted.bytes().last().is_some_and(is_tone_digit);
        let non_syllables: String = input.bytes().filter(|&key| !is_syllable_letter(key)).map(char::from).collect();
        let leading_input_length = input.bytes().take_while(|&key| is_syllable_letter(key)).count();
        let leading_input_length = leading_input_length + input.bytes().skip(leading_input_length).take_while(|&key| !is_syllable_letter(key)).count();
        let mut qualified = Vec::new();
        for mut item in candidates {
                let romanization = item.romanization.replace(' ', "");
                if romanization.starts_with(&converted) {
                        item.input = input.to_owned();
                        qualified.push(item);
                        continue;
                }
                let tones: Vec<_> = romanization.bytes().filter(|&key| is_tone_digit(key)).collect();
                let tone_at_boundary = converted.as_bytes().get(item.input_count()).copied().is_some_and(is_tone_digit);
                match (input_tones.as_slice(), tones.as_slice()) {
                        ([typed], [tone]) if typed == tone && tone_at_boundary => item.input.push_str(&non_syllables),
                        ([typed], [_, last]) if ends_in_tone && typed == last && tone_at_boundary => item.input = input.to_owned(),
                        ([typed], [first, _]) if !ends_in_tone && typed == first => item.input.push_str(&non_syllables),
                        ([first, _], [tone]) if first == tone && tone_at_boundary => item.input = input[..leading_input_length].to_owned(),
                        ([_, _], [_, _]) if input_tones == tones => {
                                if ends_in_tone && item.input_count() == converted.len() - 2 {
                                        item.input = input.to_owned();
                                } else if !ends_in_tone && converted.as_bytes().get(item.input_count() + 1) == input_tones.last() {
                                        item.input.push_str(&non_syllables);
                                } else {
                                        continue;
                                }
                        }
                        ([_] | [_, _], [_] | [_, _]) => continue,
                        _ if input.starts_with(&romanization) => item.input = romanization,
                        _ => continue,
                }
                qualified.push(item);
        }
        qualified.sort_by_key(|item| Reverse(item.input_count()));
        qualified
}
