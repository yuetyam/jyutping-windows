use std::ffi::{CStr, CString};
use std::io;
use std::path::Path;

use jyutping_core::{
        Candidate, CangjieVariant, CharacterStandard, Engine, Lexicon, LexiconRow, LexiconSource, MemorySource, ReverseLookup, ReverseLookupSource, RomanizationForm, Segmenter,
        ShapeRow, SymbolRow,
};

use crate::memory::InputMemory;
use crate::sqlite::{Binding, Connection, OpenMode, Statement};

pub struct Dictionary(Connection);

pub struct LexiconQuery<'a> {
        anchors: Statement<'a>,
        spell: Statement<'a>,
        plain_text: Statement<'a>,
        symbols: Statement<'a>,
}

struct ReverseQuery<'a> {
        dictionary: &'a Dictionary,
        romanizations: Statement<'a>,
        pinyin: Option<(Statement<'a>, Statement<'a>)>,
}

impl Dictionary {
        pub fn open(path: &Path) -> io::Result<Self> {
                Connection::open(path, OpenMode::ReadOnly).map(Self)
        }

        fn prepare(&self, sql: &CStr) -> io::Result<Statement<'_>> {
                self.0.prepare(sql)
        }

        fn prepare_exact_lookup(&self) -> io::Result<Statement<'_>> {
                self.prepare(c"SELECT word, romanization FROM lexicon_core WHERE spell = ?1 ORDER BY rowid LIMIT 1")
        }

        pub fn verify_schema(&self) -> io::Result<()> {
                self.prepare_exact_lookup()?;
                Ok(())
        }

        pub fn load_engine(&self) -> io::Result<Engine> {
                let jyutping = self.load_segmenter(c"SELECT alias_code, origin_code FROM syllable_core_table")?;
                let pinyin = self.load_segmenter(c"SELECT code, code FROM syllable_pinyin_table")?;
                Ok(Engine::new(jyutping, pinyin))
        }

        fn load_segmenter(&self, sql: &CStr) -> io::Result<Segmenter> {
                let rows = self.prepare(sql)?.read_rows(|row| Ok((row.column_integer(0), row.column_integer(1))))?;
                Segmenter::from_codes(rows).ok_or_else(|| io::Error::new(io::ErrorKind::InvalidData, "invalid or empty syllable table"))
        }

        pub fn lexicon_query(&self) -> io::Result<LexiconQuery<'_>> {
                Ok(LexiconQuery {
                        anchors: self.prepare(c"SELECT rowid, word, romanization FROM lexicon_core WHERE anchors = ?1 AND char_count = ?2 ORDER BY rowid LIMIT ?3")?,
                        spell: self.prepare(c"SELECT rowid, word, romanization FROM lexicon_core WHERE spell = ?1 AND complexity = ?2 ORDER BY rowid LIMIT ?3")?,
                        plain_text: self.prepare(c"SELECT word FROM plain_text_table WHERE spell = ?1 AND letter_count = ?2 ORDER BY rowid")?,
                        symbols: self.prepare(
                                c"
                                SELECT category,
                                       CASE WHEN category IN (1, 4) THEN COALESCE(
                                           (SELECT target FROM emoji_skin_map WHERE source = symbol_table.code_point LIMIT 1),
                                           code_point
                                       ) ELSE code_point END,
                                       cantonese, romanization
                                FROM symbol_table WHERE spell = ?1 AND complexity = ?2 ORDER BY rowid
                        ",
                        )?,
                })
        }

        pub fn convert_text(&self, text: &str, standard: CharacterStandard) -> io::Result<String> {
                let mut convert = self.character_converter(standard)?;
                convert(text)
        }

        pub fn suggest(&self, engine: &Engine, memory: Option<&InputMemory>, input: &str, standard: CharacterStandard, form: RomanizationForm) -> io::Result<Vec<Candidate>> {
                let mut query = self.lexicon_query()?;
                let mut memory = memory.map(InputMemory::query).transpose()?;
                engine.suggest_bundled(&mut query, memory.as_mut().map(|query| query as &mut dyn MemorySource), input, form, self.character_converter(standard)?)
        }

        pub fn reverse_lookup(&self, engine: &Engine, method: ReverseLookup, input: &str) -> io::Result<Vec<Lexicon>> {
                let romanizations = self.prepare(c"SELECT romanization FROM lexicon_core WHERE word = ?1 ORDER BY rowid")?;
                let pinyin = if matches!(method, ReverseLookup::Pinyin) {
                        Some((
                                self.prepare(c"SELECT rowid, word, romanization FROM pinyin_lexicon WHERE anchors = ?1 AND char_count = ?2 ORDER BY rowid LIMIT ?3")?,
                                self.prepare(c"SELECT rowid, word, romanization FROM pinyin_lexicon WHERE spell = ?1 AND complexity = ?2 ORDER BY rowid LIMIT ?3")?,
                        ))
                } else {
                        None
                };
                let mut query = ReverseQuery { dictionary: self, romanizations, pinyin };
                engine.reverse_lookup(&mut query, method, input)
        }

        pub fn reverse_candidates(
                &self,
                engine: &Engine,
                method: ReverseLookup,
                input: &str,
                trigger: Option<char>,
                standard: CharacterStandard,
                form: RomanizationForm,
        ) -> io::Result<Vec<Candidate>> {
                let mut lexicons = Vec::new();
                if let Some(trigger) = trigger {
                        lexicons = engine.search_plain_texts(&mut self.lexicon_query()?, &format!("{trigger}{input}"))?;
                        for item in &mut lexicons {
                                item.input = input.to_owned();
                        }
                }
                lexicons.extend(self.reverse_lookup(engine, method, input)?);
                jyutping_core::present_candidates(lexicons, form, self.character_converter(standard)?)
        }

        fn character_converter(&self, standard: CharacterStandard) -> io::Result<impl FnMut(&str) -> io::Result<String> + '_> {
                let sql = match standard {
                        CharacterStandard::Inherited => Some(c"SELECT target FROM variant_old WHERE source = ?1"),
                        CharacterStandard::HongKong => Some(c"SELECT target FROM variant_hk WHERE source = ?1"),
                        CharacterStandard::Taiwan => Some(c"SELECT target FROM variant_tw WHERE source = ?1"),
                        CharacterStandard::PrcGeneral => Some(c"SELECT target FROM variant_prc WHERE source = ?1"),
                        CharacterStandard::AncientBooksPublishing => Some(c"SELECT target FROM variant_abp WHERE source = ?1"),
                        CharacterStandard::Mutilated => Some(c"SELECT target FROM variant_sim WHERE source = ?1"),
                        CharacterStandard::Preset | CharacterStandard::Custom | CharacterStandard::Etymology | CharacterStandard::OpenCC => None,
                };
                let mut lookup = sql.map(|sql| self.prepare(sql)).transpose()?;
                Ok(move |text: &str| match &mut lookup {
                        Some(lookup) => jyutping_core::convert_text(text, standard, |character| {
                                let targets = lookup.query([i64::from(u32::from(character))], |row| Ok(row.column_integer(0)))?;
                                Ok(targets.first().and_then(|&target| u32::try_from(target).ok()).and_then(char::from_u32))
                        }),
                        None => Ok(text.to_owned()),
                })
        }

        pub fn suggest_exact(&self, spell: i64, text: &mut [u8], romanization: &mut [u8]) -> io::Result<Option<(usize, usize)>> {
                let rows = self.prepare_exact_lookup()?.query([spell], |row| Ok((row.column_text(0)?.to_owned(), row.column_text(1)?.to_owned())))?;
                let Some((word, reading)) = rows.first() else { return Ok(None) };
                if word.is_empty() || reading.is_empty() || word.len() > text.len() || reading.len() > romanization.len() || word.contains('\0') || reading.contains('\0') {
                        return Err(io::Error::other("dictionary value is missing or exceeds the protocol limit"));
                }
                text[..word.len()].copy_from_slice(word.as_bytes());
                romanization[..reading.len()].copy_from_slice(reading.as_bytes());
                Ok(Some((word.len(), reading.len())))
        }
}

impl LexiconSource for LexiconQuery<'_> {
        fn by_anchors(&mut self, anchors: i64, syllable_count: usize, limit: i64) -> io::Result<Vec<LexiconRow>> {
                self.anchors.query([anchors, syllable_count as i64, limit], read_lexicon_row)
        }

        fn by_spell(&mut self, spell: i64, complexity: i64, limit: i64) -> io::Result<Vec<LexiconRow>> {
                self.spell.query([spell, complexity, limit], read_lexicon_row)
        }

        fn plain_texts(&mut self, spell: i64, input_count: usize) -> io::Result<Vec<String>> {
                self.plain_text.query([spell, input_count as i64], |row| Ok(row.column_text(0)?.to_owned()))
        }

        fn symbols(&mut self, spell: i64, complexity: i64) -> io::Result<Vec<SymbolRow>> {
                self.symbols.query([spell, complexity], |row| {
                        Ok(SymbolRow {
                                category: row.column_integer(0),
                                code_points: row.column_text(1)?.to_owned(),
                                cantonese: row.column_text(2)?.to_owned(),
                                romanization: row.column_text(3)?.to_owned(),
                        })
                })
        }
}

impl ReverseLookupSource for ReverseQuery<'_> {
        fn romanizations(&mut self, word: &str) -> io::Result<Vec<String>> {
                self.romanizations.query([Binding::Text(word)], |row| Ok(row.column_text(0)?.to_owned()))
        }

        fn pinyin_anchors(&mut self, anchors: i64, count: usize, limit: i64) -> io::Result<Vec<LexiconRow>> {
                let (statement, _) = self.pinyin.as_mut().ok_or_else(|| io::Error::other("Pinyin lookup was not prepared"))?;
                statement.query([anchors, count as i64, limit], read_lexicon_row)
        }

        fn pinyin_spell(&mut self, spell: i64, complexity: i64, limit: i64) -> io::Result<Vec<LexiconRow>> {
                let (_, statement) = self.pinyin.as_mut().ok_or_else(|| io::Error::other("Pinyin lookup was not prepared"))?;
                statement.query([spell, complexity, limit], read_lexicon_row)
        }

        fn cangjie_exact(&mut self, variant: CangjieVariant, code: i64) -> io::Result<Vec<ShapeRow>> {
                let (table, _, code_column, complexity) = cangjie_columns(variant);
                let sql = CString::new(format!("SELECT rowid, word, {complexity} FROM {table} WHERE {code_column} = ?1 ORDER BY rowid"))?;
                self.dictionary.prepare(&sql)?.query([code], read_shape_row)
        }

        fn cangjie_prefix(&mut self, variant: CangjieVariant, pattern: &str, limit: i64) -> io::Result<Vec<ShapeRow>> {
                let (table, shape, _, complexity) = cangjie_columns(variant);
                let sql = CString::new(format!("SELECT rowid, word, {complexity} FROM {table} WHERE {shape} GLOB ?1 ORDER BY {complexity}, rowid LIMIT ?2"))?;
                self.dictionary.prepare(&sql)?.query([Binding::Text(pattern), Binding::Integer(limit)], read_shape_row)
        }

        fn stroke_exact(&mut self, code: i64, count: usize) -> io::Result<Vec<ShapeRow>> {
                let sql = c"SELECT rowid, word, complex FROM stroke_table WHERE code = ?1 AND complex = ?2 ORDER BY rowid";
                self.dictionary.prepare(sql)?.query([code, count as i64], read_shape_row)
        }

        fn stroke_pattern(&mut self, pattern: &str, exact_wildcard: bool, limit: i64) -> io::Result<Vec<ShapeRow>> {
                let sql = if exact_wildcard {
                        c"SELECT rowid, word, complex FROM stroke_table WHERE stroke LIKE ?1 ORDER BY complex, rowid LIMIT ?2"
                } else {
                        c"SELECT rowid, word, complex FROM stroke_table WHERE stroke GLOB ?1 ORDER BY complex, rowid LIMIT ?2"
                };
                self.dictionary.prepare(sql)?.query([Binding::Text(pattern), Binding::Integer(limit)], read_shape_row)
        }

        fn structures(&mut self, spell: i64, complexity: i64) -> io::Result<Vec<(String, String)>> {
                let sql = c"SELECT word, romanization FROM structure_table WHERE spell = ?1 AND complexity = ?2 ORDER BY rowid";
                self.dictionary.prepare(sql)?.query([spell, complexity], |row| Ok((row.column_text(0)?.to_owned(), row.column_text(1)?.to_owned())))
        }
}

fn cangjie_columns(variant: CangjieVariant) -> (&'static str, &'static str, &'static str, &'static str) {
        match variant {
                CangjieVariant::Cangjie5 => ("cangjie_table", "cangjie5", "c5code", "c5complex"),
                CangjieVariant::Cangjie3 => ("cangjie_table", "cangjie3", "c3code", "c3complex"),
                CangjieVariant::Quick5 => ("quick_table", "quick5", "q5code", "q5complex"),
                CangjieVariant::Quick3 => ("quick_table", "quick3", "q3code", "q3complex"),
        }
}

fn read_shape_row(row: &Statement<'_>) -> io::Result<ShapeRow> {
        Ok(ShapeRow { number: row.column_integer(0), word: row.column_text(1)?.to_owned(), complexity: row.column_integer(2) })
}

fn read_lexicon_row(row: &Statement<'_>) -> io::Result<LexiconRow> {
        Ok(LexiconRow { number: row.column_integer(0), word: row.column_text(1)?.to_owned(), romanization: row.column_text(2)?.to_owned() })
}
