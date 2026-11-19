use std::collections::HashMap;

use crate::{combined_code, is_syllable_letter, letter_code};

const MAX_SYLLABLE_KEYS: usize = 6;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Syllable<'a> {
        pub alias: &'a str,
        pub origin: &'a str,
}

pub(crate) type Scheme<'a> = Vec<Syllable<'a>>;

#[derive(Debug)]
pub struct Segmenter {
        syllables: HashMap<i64, (String, String)>,
}

impl Segmenter {
        /// Load the Windows dictionary's base-100 alias and origin codes.
        pub fn from_codes(rows: impl IntoIterator<Item = (i64, i64)>) -> Option<Self> {
                let syllables: HashMap<_, _> = rows.into_iter().map(|(alias, origin)| Some((alias, (decode_syllable(alias)?, decode_syllable(origin)?)))).collect::<Option<_>>()?;
                (!syllables.is_empty()).then_some(Self { syllables })
        }

        fn lookup(&self, code: i64) -> Option<Syllable<'_>> {
                let (alias, origin) = self.syllables.get(&code)?;
                Some(Syllable { alias, origin })
        }

        pub(crate) fn syllable_text(&self, keys: &str) -> Option<&str> {
                if keys.len() > MAX_SYLLABLE_KEYS || !keys.bytes().all(is_syllable_letter) {
                        return None;
                }
                self.lookup(combined_code(keys.bytes())).map(|syllable| syllable.origin)
        }

        /// Return complete and partial schemes, longest consumed length first.
        /// Equal lengths retain the Windows breadth-first order.
        pub fn segment(&self, input: &str) -> Vec<Vec<Syllable<'_>>> {
                match input {
                        "" => return Vec::new(),
                        "a" => return vec![vec![Syllable { alias: "a", origin: "aa" }]],
                        "o" => return vec![vec![Syllable { alias: "o", origin: "o" }]],
                        "m" => return vec![vec![Syllable { alias: "m", origin: "m" }]],
                        _ if input.len() == 1 => return Vec::new(),
                        _ => {}
                }

                self.split(input.bytes().filter(|&key| is_syllable_letter(key)))
        }

        pub(crate) fn split(&self, letters: impl Iterator<Item = u8>) -> Vec<Scheme<'_>> {
                let letters: Vec<_> = letters.collect();
                let mut edges = Vec::with_capacity(letters.len() + 1);
                for start in 0..letters.len() {
                        let mut code = 0;
                        let end = (start + MAX_SYLLABLE_KEYS).min(letters.len());
                        let mut matches = Vec::new();
                        for (offset, &letter) in letters[start..end].iter().enumerate() {
                                code = code * 100 + letter_code(letter);
                                if let Some(syllable) = self.lookup(code) {
                                        matches.push((syllable, start + offset + 1));
                                }
                        }
                        edges.push(matches);
                }
                edges.push(Vec::new());

                let mut nodes: Vec<_> = edges[0].iter().map(|&(syllable, end)| (syllable, end, None)).collect();
                let mut nodes_by_end = vec![Vec::new(); edges.len()];
                let mut index = 0;
                while let Some(&(syllable, end, previous)) = nodes.get(index) {
                        nodes_by_end[end].push((syllable, previous));
                        nodes.extend(edges[end].iter().map(|&(syllable, end)| (syllable, end, Some(index))));
                        index += 1;
                }

                nodes_by_end
                        .into_iter()
                        .rev()
                        .flatten()
                        .map(|(syllable, mut previous)| {
                                let mut scheme = vec![syllable];
                                while let Some(index) = previous {
                                        let (syllable, _, parent) = nodes[index];
                                        scheme.push(syllable);
                                        previous = parent;
                                }
                                scheme.reverse();
                                scheme
                        })
                        .collect()
        }
}

fn decode_syllable(mut code: i64) -> Option<String> {
        let mut letters = Vec::new();
        while code > 0 {
                let digit = code % 100;
                if !(20..=45).contains(&digit) || letters.len() == MAX_SYLLABLE_KEYS {
                        return None;
                }
                letters.push(b'a' + (digit - 20) as u8);
                code /= 100;
        }
        if letters.is_empty() {
                return None;
        }
        letters.reverse();
        String::from_utf8(letters).ok()
}
