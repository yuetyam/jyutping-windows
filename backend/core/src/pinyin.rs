use std::collections::HashSet;
use std::io;

use crate::normal::{apostrophe_consumed_input, candidates_from_rows, concatenate, distinct, prioritize, rank, scheme_complexity, scheme_length, scheme_origin_keys};
use crate::reverse::reverse_word;
use crate::segmenter::{Scheme, Segmenter};
use crate::{Lexicon, ReverseLookupSource, combined_code};

pub(crate) fn reverse_lookup(segmenter: &Segmenter, source: &mut impl ReverseLookupSource, input: &str) -> io::Result<Vec<Lexicon>> {
        let has_separators = input.contains('\'');
        if input.starts_with('\'') {
                return Ok(Vec::new());
        }
        let letters;
        let search_input = if has_separators {
                letters = input.chars().filter(char::is_ascii_lowercase).collect::<String>();
                &letters
        } else {
                input
        };
        let mut lookup = PinyinLookup { segmenter, source };
        let schemes = segmenter.split(search_input.bytes().filter(u8::is_ascii_lowercase));
        let mut matched = if schemes.is_empty() { lookup.slices(search_input, None)? } else { lookup.search(search_input, &schemes, None)? };
        if has_separators {
                let parts: Vec<_> = input.split('\'').filter(|part| !part.is_empty()).collect();
                matched.retain_mut(|item| {
                        if let Some(consumed) = apostrophe_consumed_input(input, &parts, &item.romanization, &item.input) {
                                item.input = consumed;
                                true
                        } else {
                                false
                        }
                });
                matched.sort_by_key(rank);
        }
        let mut result = Vec::new();
        for item in matched {
                result.extend(reverse_word(lookup.source, &item.text, &item.input, &item.mark)?);
        }
        Ok(result)
}

struct PinyinLookup<'a, S> {
        segmenter: &'a Segmenter,
        source: &'a mut S,
}

impl<S: ReverseLookupSource> PinyinLookup<'_, S> {
        fn anchors(&mut self, keys: &[u8], input: &str, limit: i64) -> io::Result<Vec<Lexicon>> {
                let rows = self.source.pinyin_anchors(combined_code(keys.iter().copied()), keys.len(), limit)?;
                Ok(candidates_from_rows(rows, input, input))
        }

        fn query_scheme(&mut self, scheme: &[crate::Syllable<'_>], limit: i64) -> io::Result<Vec<Lexicon>> {
                let spell = combined_code(scheme_origin_keys(scheme));
                let mark = scheme.iter().map(|syllable| syllable.alias).collect::<Vec<_>>().join(" ");
                let input = scheme.iter().map(|syllable| syllable.alias).collect::<String>();
                let rows = self.source.pinyin_spell(spell, scheme_complexity(scheme), limit)?;
                Ok(candidates_from_rows(rows, &input, &mark))
        }

        fn slices(&mut self, input: &str, limit: Option<i64>) -> io::Result<Vec<Lexicon>> {
                let mut result = Vec::new();
                for length in (1..=input.len()).rev() {
                        let head = &input[..length];
                        let rows = self.source.pinyin_spell(combined_code(head.bytes()), length as i64, limit.unwrap_or(-1))?;
                        let mut exact = candidates_from_rows(rows, head, head);
                        let mut anchors = self.anchors(head.as_bytes(), head, if limit.is_some() { 100 } else { 300 })?;
                        for item in exact.iter_mut().chain(&mut anchors) {
                                if length < input.len() && completes_input(item, input) {
                                        item.input = input.to_owned();
                                        item.mark = input.to_owned();
                                }
                        }
                        anchors.sort_by_key(rank);
                        result.extend(exact);
                        result.extend(anchors.into_iter().take(72));
                }
                let mut result = distinct(result);
                result.sort_by_key(rank);
                Ok(result)
        }

        fn search(&mut self, input: &str, schemes: &[Scheme<'_>], limit: Option<i64>) -> io::Result<Vec<Lexicon>> {
                let anchors = self.anchors(input.as_bytes(), input, limit.unwrap_or(100))?;
                let has_complete_scheme = schemes.iter().any(|scheme| scheme_length(scheme) == input.len());
                let mut queried = Vec::new();
                for scheme in schemes {
                        if has_complete_scheme && scheme_length(scheme) == input.len() {
                                for count in (1..=scheme.len()).rev() {
                                        queried.extend(self.query_scheme(&scheme[..count], limit.unwrap_or(-1))?);
                                }
                        } else if !has_complete_scheme {
                                queried.extend(self.query_scheme(scheme, limit.unwrap_or(-1))?);
                        }
                }
                let (mut full, mut partial): (Vec<_>, Vec<_>) = queried.into_iter().partition(|item| item.input_count() == input.len());
                let match_prefixes = full.is_empty() && !has_complete_scheme;
                full.sort_by_key(|item| item.number);
                partial.sort_by_key(rank);
                full.extend(anchors);
                if match_prefixes {
                        full.extend(self.match_prefixes(input, schemes, if limit.is_some() { 200 } else { 500 })?);
                        for length in (1..input.len()).rev() {
                                let head = &input[..length];
                                for mut item in self.anchors(head.as_bytes(), head, 300)? {
                                        if completes_input(&item, input) {
                                                item.input = input.to_owned();
                                                item.mark = input.to_owned();
                                                full.push(item);
                                        }
                                }
                        }
                }
                let mut results = prioritize(distinct(full), distinct(partial));
                let Some(first) = results.first() else { return self.slices(input, limit) };
                if first.input_count() >= input.len() {
                        return Ok(results);
                }

                let mut seen_lengths = HashSet::new();
                let mut best = None;
                for head in &results {
                        let length = head.input_count();
                        if length == 0 || length >= input.len() || !seen_lengths.insert(length) {
                                continue;
                        }
                        let tail = &input[length..];
                        let schemes = self.segmenter.split(tail.bytes().filter(u8::is_ascii_lowercase));
                        if let Some(tail) = self.search(tail, &schemes, Some(50))?.into_iter().next() {
                                let combined = concatenate(head, &tail);
                                if best.as_ref().is_none_or(|item| rank(&combined) < rank(item)) {
                                        best = Some(combined);
                                }
                        }
                }
                if let Some(best) = best {
                        results.insert(0, best);
                }
                Ok(results)
        }

        fn match_prefixes(&mut self, input: &str, schemes: &[Scheme<'_>], limit: i64) -> io::Result<Vec<Lexicon>> {
                let mut result = Vec::new();
                for scheme in schemes {
                        let tail = &input[scheme_length(scheme)..];
                        if tail.is_empty() {
                                continue;
                        }
                        let scheme_mark = scheme.iter().map(|syllable| syllable.alias).collect::<Vec<_>>().join(" ");
                        let mark = format!("{scheme_mark} {tail}");
                        let mut anchors: Vec<_> = scheme.iter().map(|syllable| syllable.alias.as_bytes()[0]).collect();
                        let conjoined: Vec<_> = anchors.iter().copied().chain(tail.bytes()).collect();
                        for mut item in self.anchors(&conjoined, input, limit)? {
                                if let Some(suffix) = item.romanization.strip_prefix(&scheme_mark) {
                                        if suffix.split(' ').filter_map(|syllable| syllable.bytes().next()).eq(tail.bytes()) {
                                                item.mark.clone_from(&mark);
                                                result.push(item);
                                        }
                                }
                        }
                        anchors.push(tail.as_bytes()[0]);
                        for mut item in self.anchors(&anchors, input, limit)? {
                                if item.romanization.starts_with(&mark) {
                                        item.mark.clone_from(&mark);
                                        result.push(item);
                                }
                        }
                }
                Ok(result)
        }
}

fn completes_input(item: &Lexicon, input: &str) -> bool {
        if item.romanization.replace(' ', "").starts_with(input) {
                return true;
        }
        let mut syllables = item.romanization.split(' ').filter(|part| !part.is_empty());
        syllables.next_back().is_some_and(|last| input.ends_with(last) && syllables.count() + last.len() == input.len())
}
