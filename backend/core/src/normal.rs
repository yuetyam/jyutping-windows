use std::cmp::Reverse;
use std::collections::HashSet;
use std::io;

use crate::candidate::{Candidate, RomanizationForm, present_candidates};
use crate::extra_entries::EXTRA_ENTRIES;
use crate::segmenter::{Scheme, Segmenter, Syllable};
use crate::tones::{filter_tones, is_tone_key, strip_tones};
use crate::{MemorySource, anchor_key, combined_code, is_syllable_letter};

const MAX_WORD_SYLLABLES: usize = 9;
const COMPOUND_NUMBER_STEP: i64 = 1_000_000;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Lexicon {
        pub kind: LexiconKind,
        pub text: String,
        pub romanization: String,
        pub input: String,
        pub mark: String,
        pub number: i64,
        pub attached: Option<String>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum LexiconKind {
        Cantonese,
        Text,
        Emoji,
        Symbol,
}

impl Lexicon {
        /// Normal input consists of single-byte ASCII key events.
        pub fn input_count(&self) -> usize {
                self.input.len()
        }
}

pub struct LexiconRow {
        pub number: i64,
        pub word: String,
        pub romanization: String,
}

pub struct SymbolRow {
        pub category: i64,
        pub code_points: String,
        pub cantonese: String,
        pub romanization: String,
}

pub trait LexiconSource {
        fn by_anchors(&mut self, anchors: i64, syllable_count: usize, limit: i64) -> io::Result<Vec<LexiconRow>>;
        fn by_spell(&mut self, spell: i64, complexity: i64, limit: i64) -> io::Result<Vec<LexiconRow>>;
        fn plain_texts(&mut self, spell: i64, input_count: usize) -> io::Result<Vec<String>>;
        fn symbols(&mut self, spell: i64, complexity: i64) -> io::Result<Vec<SymbolRow>>;
}

pub struct Engine {
        pub(crate) segmenter: Segmenter,
        pub(crate) pinyin_segmenter: Segmenter,
}

impl Engine {
        pub fn new(segmenter: Segmenter, pinyin_segmenter: Segmenter) -> Self {
                Self { segmenter, pinyin_segmenter }
        }

        pub fn segment(&self, input: &str) -> Vec<Vec<Syllable<'_>>> {
                self.segmenter.segment(input)
        }

        /// Look up Cantonese lexicons using a normalized Windows QWERTY key snapshot.
        /// The caller should bound work before exposing this engine to remote callers.
        pub fn suggest(&self, source: &mut impl LexiconSource, input: &str, deep_search: bool) -> io::Result<Vec<Lexicon>> {
                validate_keys(input)?;
                self.suggest_with_segmentation(source, input, &self.segmenter.segment(input), deep_search)
        }

        fn suggest_with_segmentation(&self, source: &mut impl LexiconSource, input: &str, segmentation: &[Scheme<'_>], deep_search: bool) -> io::Result<Vec<Lexicon>> {
                let mut lookup = Lookup { segmenter: &self.segmenter, source };
                match input {
                        "" => return Ok(Vec::new()),
                        "a" => {
                                let mut result = lookup.spell("a".bytes(), 1, input, input, -1)?;
                                result.extend(lookup.spell("aa".bytes(), 2, input, input, -1)?);
                                result.extend(lookup.anchors(input.bytes(), input, 100)?);
                                return Ok(result);
                        }
                        "o" | "m" => {
                                let mut result = lookup.spell(input.bytes(), 1, input, input, -1)?;
                                result.extend(lookup.anchors(input.bytes(), input, 100)?);
                                return Ok(result);
                        }
                        _ if input.len() == 1 => return lookup.anchors(input.bytes(), input, 100),
                        _ => {}
                }

                let letters: String = input.bytes().filter(|&key| is_syllable_letter(key)).map(char::from).collect();
                let mut candidates;
                if let Some(best) = segmentation.first() {
                        candidates = lookup.search(&letters, segmentation, None, deep_search)?;
                        if (best[0].alias.len() == 1 && letters.len() > 1) || letters.len() != input.len() {
                                candidates.extend(lookup.slices(&letters, 300)?);
                        }
                } else {
                        candidates = extra_entries(input);
                        if deep_search {
                                candidates.extend(lookup.slices(&letters, 300)?);
                        } else {
                                candidates.extend(lookup.anchors(letters.bytes(), &letters, 100)?);
                        }
                }

                let has_apostrophe = input.contains('\'');
                let has_tone = input.bytes().any(is_tone_key);
                if has_tone {
                        Ok(filter_tones(input, candidates, has_apostrophe))
                } else if has_apostrophe {
                        lookup.filter_apostrophes(input, candidates)
                } else {
                        Ok(candidates)
                }
        }

        pub fn search_plain_texts(&self, source: &mut impl LexiconSource, input: &str) -> io::Result<Vec<Lexicon>> {
                validate_keys(input)?;
                if input.is_empty() {
                        return Ok(Vec::new());
                }
                let rows = source.plain_texts(combined_code(input.bytes()), input.len())?;
                Ok(rows.into_iter()
                        .map(|text| Lexicon {
                                kind: LexiconKind::Text,
                                text,
                                romanization: input.to_owned(),
                                input: input.to_owned(),
                                mark: input.to_owned(),
                                number: 0,
                                attached: None,
                        })
                        .collect())
        }

        pub fn search_symbols(&self, source: &mut impl LexiconSource, input: &str, segmentation: &[Vec<Syllable<'_>>]) -> io::Result<Vec<Lexicon>> {
                validate_keys(input)?;
                let syllable_key_count = input.bytes().filter(|&key| is_syllable_letter(key)).count();
                let mut seen = HashSet::new();
                let mut result = Vec::new();
                for scheme in segmentation.iter().filter(|scheme| scheme_length(scheme) == syllable_key_count && !scheme.is_empty()) {
                        let rows = source.symbols(combined_code(scheme_origin_keys(scheme)), scheme_complexity(scheme))?;
                        for row in rows {
                                let Some(text) = symbol_text(&row.code_points) else { continue };
                                let kind = if row.category < 10 { LexiconKind::Emoji } else { LexiconKind::Symbol };
                                if seen.insert((kind, text.clone())) {
                                        result.push(Lexicon {
                                                kind,
                                                text,
                                                romanization: row.romanization,
                                                input: input.to_owned(),
                                                mark: input.to_owned(),
                                                number: 0,
                                                attached: Some(row.cantonese),
                                        });
                                }
                        }
                }
                Ok(result)
        }

        pub fn suggest_bundled(
                &self,
                source: &mut impl LexiconSource,
                memory: Option<&mut dyn MemorySource>,
                input: &str,
                form: RomanizationForm,
                display_text: impl FnMut(&str) -> io::Result<String>,
        ) -> io::Result<Vec<Candidate>> {
                validate_keys(input)?;
                let schemes = self.segment(input);
                let learned = memory.map(|memory| self.memory_with_segmentation(memory, input, &schemes)).transpose()?.unwrap_or_default();
                let (ideal, partial): (Vec<_>, Vec<_>) = learned.into_iter().partition(|item| item.number == -1);
                let mut queried = self.suggest_with_segmentation(source, input, &schemes, true)?;
                if !ideal.is_empty() {
                        queried.retain(|item| item.number <= COMPOUND_NUMBER_STEP);
                }
                for item in partial.into_iter().rev() {
                        let position = queried.iter().position(|candidate| candidate.input_count() <= item.input_count()).unwrap_or(queried.len());
                        queried.insert(position, item);
                }
                let mut lexicons = Vec::new();
                lexicons.extend(ideal.iter().take(3).cloned());
                lexicons.extend(self.search_plain_texts(source, input)?);
                lexicons.extend(ideal);
                lexicons.extend(queried);
                for symbol in self.search_symbols(source, input, &schemes)?.into_iter().rev() {
                        let target = lexicons.iter().position(|item| {
                                item.kind == LexiconKind::Cantonese && Some(item.text.as_str()) == symbol.attached.as_deref() && item.romanization == symbol.romanization
                        });
                        if let Some(position) = target {
                                lexicons.insert(position + 1, symbol);
                        }
                }
                present_candidates(lexicons, form, display_text)
        }
}

struct Lookup<'a, S> {
        segmenter: &'a Segmenter,
        source: &'a mut S,
}

impl<S: LexiconSource> Lookup<'_, S> {
        fn anchors(&mut self, keys: impl ExactSizeIterator<Item = u8>, input: &str, limit: i64) -> io::Result<Vec<Lexicon>> {
                let count = keys.len();
                if !(1..=MAX_WORD_SYLLABLES).contains(&count) {
                        return Ok(Vec::new());
                }
                self.source.by_anchors(combined_code(keys.map(anchor_key)), count, limit).map(|rows| candidates_from_rows(rows, input, input))
        }

        fn spell(&mut self, keys: impl Iterator<Item = u8>, complexity: i64, input: &str, mark: &str, limit: i64) -> io::Result<Vec<Lexicon>> {
                self.source.by_spell(combined_code(keys), complexity, limit).map(|rows| candidates_from_rows(rows, input, mark))
        }

        fn query_schemes(&mut self, input_length: usize, schemes: &[Scheme<'_>], limit: i64) -> io::Result<Vec<Lexicon>> {
                let has_complete_scheme = schemes.iter().any(|scheme| scheme_length(scheme) == input_length);
                let mut result = Vec::new();
                for scheme in schemes {
                        if has_complete_scheme && scheme_length(scheme) == input_length {
                                for count in (1..=scheme.len().min(MAX_WORD_SYLLABLES)).rev() {
                                        result.extend(self.query_scheme(&scheme[..count], limit)?);
                                }
                        } else if !has_complete_scheme && scheme.len() <= MAX_WORD_SYLLABLES {
                                result.extend(self.query_scheme(scheme, limit)?);
                        }
                }
                Ok(result)
        }

        fn query_scheme(&mut self, scheme: &[Syllable<'_>], limit: i64) -> io::Result<Vec<Lexicon>> {
                let mut input = String::new();
                let mut mark = String::new();
                for syllable in scheme {
                        input.push_str(syllable.alias);
                        if !mark.is_empty() {
                                mark.push(' ');
                        }
                        mark.push_str(syllable.alias);
                }
                self.spell(scheme_origin_keys(scheme), scheme_complexity(scheme), &input, &mark, limit)
        }

        fn search(&mut self, input: &str, schemes: &[Scheme<'_>], limit: Option<i64>, deep_search: bool) -> io::Result<Vec<Lexicon>> {
                let anchors = self.anchors(input.bytes(), input, limit.unwrap_or(100))?;
                let queried = self.query_schemes(input.len(), schemes, limit.unwrap_or(-1))?;
                let (mut full, mut partial): (Vec<_>, Vec<_>) = queried.into_iter().partition(|item| item.input_count() == input.len());

                let should_match_prefixes = deep_search
                        && (3..25).contains(&input.len())
                        && (input.starts_with('m') || input.ends_with('m') || (full.is_empty() && !schemes.iter().any(|scheme| scheme_length(scheme) == input.len())));
                full.sort_by_key(|item| item.number);
                partial.sort_by_key(rank);
                let mut full = distinct(full);
                let partial = distinct(partial);
                if full.is_empty() {
                        full = extra_entries(input);
                }
                full.extend(anchors);
                if should_match_prefixes {
                        full.extend(self.match_prefixes(input, schemes, if limit.is_some() { 200 } else { 500 })?);
                        full.extend(self.match_leading_anchors(input)?);
                }
                let mut fetched = prioritize(distinct(full), partial);

                let Some(first) = fetched.first() else {
                        return if deep_search { self.slices(input, if limit.is_some() { 100 } else { 300 }) } else { Ok(fetched) };
                };
                if first.input_count() >= input.len() || !deep_search {
                        return Ok(fetched);
                }

                let mut seen_lengths = HashSet::new();
                let mut best = None;
                for head in &fetched {
                        let length = head.input_count();
                        if length == 0 || length >= input.len() || !seen_lengths.insert(length) {
                                continue;
                        }
                        let tail = &input[length..];
                        let tail_schemes = self.segmenter.segment(tail);
                        if let Some(tail) = self.search(tail, &tail_schemes, Some(50), true)?.into_iter().next() {
                                let compound = concatenate(head, &tail);
                                if best.as_ref().is_none_or(|item| rank(&compound) < rank(item)) {
                                        best = Some(compound);
                                }
                        }
                }
                if let Some(best) = best {
                        fetched.insert(0, best);
                }
                Ok(fetched)
        }

        fn match_prefixes(&mut self, input: &str, schemes: &[Scheme<'_>], limit: i64) -> io::Result<Vec<Lexicon>> {
                let mut result = Vec::new();
                for scheme in schemes {
                        let tail = &input[scheme_length(scheme)..];
                        if tail.is_empty() || scheme.len() > MAX_WORD_SYLLABLES {
                                continue;
                        }
                        let origin = scheme.iter().map(|syllable| syllable.origin).collect::<Vec<_>>().join(" ");
                        let mark = scheme.iter().map(|syllable| syllable.alias).chain([tail]).collect::<Vec<_>>().join(" ");
                        let anchors: Vec<_> = scheme.iter().map(|syllable| syllable.alias.as_bytes()[0]).collect();
                        let tail_anchors: Vec<_> = tail.bytes().map(anchor_key).collect();
                        let conjoined: Vec<_> = anchors.iter().copied().chain(tail.bytes()).collect();
                        for mut item in self.anchors(conjoined.into_iter(), input, limit)? {
                                let romanization = strip_tones(&item.romanization);
                                if let Some(suffix) = romanization.strip_prefix(&origin) {
                                        let suffix_anchors = suffix.split(' ').filter_map(|syllable| syllable.bytes().next());
                                        if suffix_anchors.eq(tail_anchors.iter().copied()) {
                                                item.mark.clone_from(&mark);
                                                result.push(item);
                                        }
                                }
                        }

                        let first = tail.as_bytes()[0];
                        let anchors: Vec<_> = anchors.into_iter().chain([first]).collect();
                        let prefix = format!("{origin} {}{}", char::from(anchor_key(first)), &tail[1..]);
                        for mut item in self.anchors(anchors.into_iter(), input, limit)? {
                                if strip_tones(&item.romanization).starts_with(&prefix) {
                                        item.mark.clone_from(&mark);
                                        result.push(item);
                                }
                        }
                }
                Ok(result)
        }

        fn match_leading_anchors(&mut self, input: &str) -> io::Result<Vec<Lexicon>> {
                let mut result = Vec::new();
                for length in (1..input.len()).rev().filter(|&length| length <= MAX_WORD_SYLLABLES) {
                        let head = &input[..length];
                        for mut item in self.anchors(head.bytes(), head, 300)? {
                                if input.len() - length < 6 && self.completes_input(&item, input) {
                                        item.input = input.to_owned();
                                        item.mark = input.to_owned();
                                        result.push(item);
                                }
                        }
                }
                Ok(result)
        }

        fn completes_input(&self, item: &Lexicon, input: &str) -> bool {
                let letters: String = item.romanization.chars().filter(char::is_ascii_alphabetic).collect();
                if letters.starts_with(input) {
                        return true;
                }
                let Some(last_syllable) = item.romanization.split(' ').rfind(|syllable| !syllable.is_empty()) else { return false };
                let last_syllable = strip_tones(last_syllable);
                let tail = &input[item.input_count().saturating_sub(1)..];
                tail.len() <= 6 && self.segmenter.syllable_text(tail).map_or_else(|| last_syllable.starts_with(tail), |origin| last_syllable == origin)
        }

        fn slices(&mut self, input: &str, limit: i64) -> io::Result<Vec<Lexicon>> {
                let mut result = Vec::new();
                for length in (1..=input.len().min(MAX_WORD_SYLLABLES)).rev() {
                        let head = &input[..length];
                        let mut candidates = self.anchors(head.bytes(), head, limit)?;
                        for item in &mut candidates {
                                if length < input.len() && self.completes_input(item, input) {
                                        item.input = input.to_owned();
                                        item.mark = input.to_owned();
                                }
                        }
                        candidates.sort_by_key(rank);
                        result.extend(candidates.into_iter().take(50));
                }
                let mut result = distinct(result);
                result.sort_by_key(rank);
                Ok(result)
        }

        fn filter_apostrophes(&mut self, input: &str, candidates: Vec<Lexicon>) -> io::Result<Vec<Lexicon>> {
                if input.starts_with('\'') {
                        return Ok(Vec::new());
                }
                let parts: Vec<_> = input.split('\'').filter(|part| !part.is_empty()).collect();
                let mut qualified = Vec::new();
                for mut item in candidates {
                        let romanization = strip_tones(&item.romanization);
                        if let Some(consumed_input) = apostrophe_consumed_input(input, &parts, &romanization, &item.input) {
                                item.input = consumed_input;
                                qualified.push(item);
                        }
                }
                if !qualified.is_empty() {
                        let mut result = distinct(qualified);
                        result.sort_by_key(|item| Reverse(item.input_count()));
                        return Ok(result);
                }

                let anchors: Vec<_> = parts.iter().map(|part| part.as_bytes()[0]).collect();
                let mut result = Vec::new();
                for item in self.anchors(anchors.into_iter(), input, 100)? {
                        let syllables = strip_tones(&item.romanization);
                        let syllables: Vec<_> = syllables.split(' ').filter(|syllable| !syllable.is_empty()).collect();
                        if syllables.len() == parts.len() && syllables.iter().zip(&parts).all(|(syllable, part)| syllable == part || is_initial(part, syllable)) {
                                result.push(item);
                        }
                }
                Ok(result)
        }
}

pub(crate) fn apostrophe_consumed_input(input: &str, parts: &[&str], romanization: &str, consumed: &str) -> Option<String> {
        let syllables: Vec<_> = romanization.split(' ').filter(|syllable| !syllable.is_empty()).collect();
        if syllables == parts {
                return Some(input.to_owned());
        }
        let separators = input.bytes().filter(|&key| key == b'\'').count();
        let trailing = input.ends_with('\'');
        let consumes_first = parts.first().is_some_and(|part| consumed.len() == part.len());
        match (separators, trailing, syllables.as_slice(), parts) {
                (1, true, [_], _) if consumed.len() == input.len() - 1 => Some(input.to_owned()),
                (1, false, [_], _) | (2, true, [_], _) if consumes_first => Some(format!("{consumed}'")),
                (1, false, [first, last], [head, .., tail]) if input.len() == 3 || first == head || (is_initial(head, first) && tail.starts_with(last)) => {
                        Some(format!("{consumed}'"))
                }
                (2, true, [first, last], [head, .., tail])
                        if consumed.len() == input.len() - 2 && (input.len() == 4 || first == head || (is_initial(head, first) && tail == last)) =>
                {
                        Some(input.to_owned())
                }
                (2 | 3, _, _, [_, _, _]) if input.len() == separators + 3 => match syllables.len() {
                        1 if consumed.len() == 1 => Some(format!("{consumed}'")),
                        2 if consumed.len() == 2 => Some(format!("{consumed}''")),
                        3 => Some(input.to_owned()),
                        _ => None,
                },
                (1, _, _, _) | (2, true, _, _) => None,
                _ if !syllables.is_empty() && syllables.len() < parts.len() && parts.starts_with(&syllables) => {
                        // Preserve the desktop engine's recorded input for partial matches.
                        Some(format!("{consumed}{}", "i".repeat(syllables.len() - 1)))
                }
                _ => None,
        }
}

fn is_initial(part: &str, syllable: &str) -> bool {
        part.len() == 1 && syllable.starts_with(part)
}

pub(crate) fn validate_keys(input: &str) -> io::Result<()> {
        if input.bytes().all(|key| key.is_ascii_lowercase() || key.is_ascii_digit() || key == b'\'') {
                Ok(())
        } else {
                Err(io::Error::new(io::ErrorKind::InvalidInput, "expected normalized Windows input keys"))
        }
}

pub(crate) fn scheme_length(scheme: &[Syllable<'_>]) -> usize {
        scheme.iter().map(|syllable| syllable.alias.len()).sum()
}

pub(crate) fn scheme_origin_keys<'a>(scheme: &'a [Syllable<'a>]) -> impl Iterator<Item = u8> + 'a {
        scheme.iter().flat_map(|syllable| syllable.origin.bytes())
}

pub(crate) fn scheme_complexity(scheme: &[Syllable<'_>]) -> i64 {
        scheme.iter().fold(0i64, |code, syllable| code.wrapping_mul(10).wrapping_add(syllable.origin.len() as i64))
}

fn symbol_text(code_points: &str) -> Option<String> {
        code_points
                .split('.')
                .map(|part| {
                        if !part.bytes().all(|digit| digit.is_ascii_hexdigit()) {
                                return None;
                        }
                        u32::from_str_radix(part, 16).ok().and_then(char::from_u32)
                })
                .collect()
}

pub(crate) fn rank(item: &Lexicon) -> (Reverse<usize>, i64) {
        (Reverse(item.input_count()), item.number)
}

pub(crate) fn distinct(items: Vec<Lexicon>) -> Vec<Lexicon> {
        let mut seen = HashSet::new();
        items.into_iter().filter(|item| seen.insert((item.text.clone(), item.romanization.clone()))).collect()
}

pub(crate) fn prioritize(full: Vec<Lexicon>, partial: Vec<Lexicon>) -> Vec<Lexicon> {
        // Give each ranking a place on the first page before appending the remaining candidates.
        let mut full_by_number: Vec<_> = full.iter().collect();
        full_by_number.sort_by_key(|item| rank(item));
        let mut partial_by_number: Vec<_> = partial.iter().collect();
        partial_by_number.sort_by_key(|item| item.number);
        let order = full
                .iter()
                .take(10)
                .chain(full_by_number.into_iter().take(10))
                .chain(partial.iter().take(10))
                .chain(partial_by_number.into_iter().take(10))
                .chain(full.iter())
                .chain(partial.iter());
        let mut seen = HashSet::new();
        order.filter(|item| seen.insert((&item.text, &item.romanization))).cloned().collect()
}

pub(crate) fn concatenate(head: &Lexicon, tail: &Lexicon) -> Lexicon {
        Lexicon {
                kind: LexiconKind::Cantonese,
                text: format!("{}{}", head.text, tail.text),
                romanization: format!("{} {}", head.romanization, tail.romanization),
                input: format!("{}{}", head.input, tail.input),
                mark: format!("{} {}", head.mark, tail.mark),
                number: head.number + tail.number + 2 * COMPOUND_NUMBER_STEP,
                attached: None,
        }
}

pub(crate) fn candidates_from_rows(rows: Vec<LexiconRow>, input: &str, mark: &str) -> Vec<Lexicon> {
        rows.into_iter()
                .map(|row| Lexicon {
                        kind: LexiconKind::Cantonese,
                        text: row.word,
                        romanization: row.romanization,
                        input: input.to_owned(),
                        mark: mark.to_owned(),
                        number: row.number,
                        attached: None,
                })
                .collect()
}

fn extra_entries(input: &str) -> Vec<Lexicon> {
        let code = combined_code(input.bytes());
        EXTRA_ENTRIES
                .iter()
                .filter(|&&(_, _, length, spell)| input.len() == length && code == spell)
                .map(|&(text, romanization, _, _)| Lexicon {
                        kind: LexiconKind::Cantonese,
                        text: text.to_owned(),
                        romanization: romanization.to_owned(),
                        input: input.to_owned(),
                        mark: strip_tones(romanization),
                        number: 0,
                        attached: None,
                })
                .collect()
}
