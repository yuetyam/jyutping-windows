use std::io;

use crate::tones::strip_tones;
use crate::{Lexicon, LexiconKind};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RomanizationForm {
        Full = 1,
        Toneless = 2,
        Nothing = 3,
}

#[derive(Debug, PartialEq, Eq)]
pub struct Candidate {
        pub text: String,
        pub comment: Option<String>,
        pub lexicon: Lexicon,
}

fn same_candidate(left: &Candidate, right: &Candidate) -> bool {
        if left.text != right.text {
                return false;
        }
        if left.lexicon.kind == LexiconKind::Cantonese && right.lexicon.kind == LexiconKind::Cantonese && left.comment.is_none() {
                strip_tones(&left.lexicon.romanization) == strip_tones(&right.lexicon.romanization)
        } else {
                left.comment == right.comment
        }
}

pub fn present_candidates(lexicons: Vec<Lexicon>, form: RomanizationForm, mut display_text: impl FnMut(&str) -> io::Result<String>) -> io::Result<Vec<Candidate>> {
        let mut candidates = Vec::with_capacity(lexicons.len());
        for lexicon in lexicons {
                let (text, comment) = if lexicon.kind == LexiconKind::Cantonese {
                        let comment = match form {
                                RomanizationForm::Full => Some(lexicon.romanization.clone()),
                                RomanizationForm::Toneless => Some(strip_tones(&lexicon.romanization)),
                                RomanizationForm::Nothing => None,
                        };
                        (display_text(&lexicon.text)?, comment)
                } else {
                        (lexicon.text.clone(), None)
                };
                let candidate = Candidate { text, comment, lexicon };
                if !candidates.iter().any(|item| same_candidate(item, &candidate)) {
                        candidates.push(candidate);
                }
        }
        Ok(candidates)
}
