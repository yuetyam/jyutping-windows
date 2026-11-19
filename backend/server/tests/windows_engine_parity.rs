use std::env;
use std::fs;
use std::path::PathBuf;
use std::process::Command;

use jyutping_core::{CangjieVariant, CharacterStandard, Lexicon, LexiconKind, ReverseLookup, RomanizationForm};
use jyutping_server::{Dictionary, InputMemory};

#[test]
#[ignore = "run backend/tests/Test-WindowsEngineParity.ps1 with the generated dictionary and ARM64 C++ tools"]
fn windows_reference() {
        let executable = env::var_os("JYUTPING_ENGINE_BASELINE").expect("set by Test-WindowsEngineParity.ps1");
        let dictionary = PathBuf::from(env::var_os("JYUTPING_ENGINE_DICTIONARY").expect("set by Test-WindowsEngineParity.ps1"));
        let samples = [
                "",
                "a",
                "o",
                "m",
                "n",
                "y",
                "6",
                "aa",
                "ng",
                "saam",
                "neihou",
                "nei5hou2",
                "neixxhoux",
                "neixhoux",
                "nei5hou2m",
                "ngo5",
                "ngoxx",
                "ngo4",
                "ngo5dei6",
                "ngoxxdeiqq",
                "sik6",
                "sikqq",
                "nei'hou",
                "nei'",
                "nei''hou",
                "'neihou",
                "nei5'hou2",
                "n'h",
                "n'h'",
                "h'g'd",
                "h'g'd'",
                "nei'hou'ngo'dei",
                "hoenggong",
                "hkg",
                "hg",
                "hoengg",
                "heunggong",
                "hoenggongy",
                "hoenggongj",
                "hoenggongjaa",
                "hoenggongm",
                "mhoenggong",
                "mgoi",
                "keuleu",
                "tap",
                "et",
                "depje",
                "deplokjaumei",
                "gongdungwaa",
                "zaangcaan",
                "saamcoengtoi",
                "ngung",
                "dm",
                "pt",
                "zzzzzzzzz",
                "ngo5dei6hai6",
                "soengshoi",
                "abc",
                "agentsmd",
                "activitykit",
                "mouzi",
                "siujung",
                "siu3jung4",
                "waang",
                "mouzi'",
        ];
        let output = Command::new(executable).arg(&dictionary).args(samples).output().expect("run C++ engine");
        assert!(output.status.success(), "{}", String::from_utf8_lossy(&output.stderr));
        let reference = String::from_utf8(output.stdout).expect("UTF-8 engine output");
        let mut expected = reference.lines();

        let dictionary = Dictionary::open(&dictionary).expect("open generated dictionary");
        let engine = dictionary.load_engine().expect("load syllables");
        let mut query = dictionary.lexicon_query().expect("prepare lexicon statements");
        for input in samples {
                assert_eq!(expected.next(), Some(format!("input\t{input}").as_str()));
                let schemes = engine.segment(input);
                for scheme in &schemes {
                        let alias = scheme.iter().map(|syllable| syllable.alias).collect::<Vec<_>>().join(" ");
                        let origin = scheme.iter().map(|syllable| syllable.origin).collect::<Vec<_>>().join(" ");
                        let line = format!("scheme\t{alias}\t{origin}");
                        assert_eq!(expected.next(), Some(line.as_str()), "{input:?} scheme");
                }

                let sections = [
                        ("shallow", engine.suggest(&mut query, input, false)),
                        ("deep", engine.suggest(&mut query, input, true)),
                        ("text", engine.search_plain_texts(&mut query, input)),
                        ("symbols", engine.search_symbols(&mut query, input, &schemes)),
                ];
                for (label, candidates) in sections {
                        assert_eq!(expected.next(), Some(label), "{input:?} {label} heading");
                        for (index, item) in candidates.expect("query Rust engine").iter().enumerate() {
                                let row = lexicon_row(item);
                                assert_eq!(expected.next(), Some(row.as_str()), "{input:?} {label} candidate {index}");
                        }
                }
                for (standard, form) in [
                        (CharacterStandard::Preset, RomanizationForm::Full),
                        (CharacterStandard::HongKong, RomanizationForm::Toneless),
                        (CharacterStandard::Mutilated, RomanizationForm::Nothing),
                ] {
                        let label = format!("display\t{}", standard as i32);
                        assert_eq!(expected.next(), Some(label.as_str()), "{input:?} {standard:?} heading");
                        let display = dictionary.suggest(&engine, None, input, standard, form).expect("query merged suggestions");
                        for (index, item) in display.iter().enumerate() {
                                let row = format!("{}\t{}\t{}", item.text, item.comment.as_deref().unwrap_or(""), lexicon_row(&item.lexicon));
                                assert_eq!(expected.next(), Some(row.as_str()), "{input:?} {standard:?} candidate {index}");
                        }
                }
        }
        assert!(expected.next().is_none(), "unexpected additional C++ output");
}

#[test]
#[ignore = "run backend/tests/Test-WindowsEngineParity.ps1 with the generated dictionary and ARM64 C++ tools"]
fn character_standards() {
        let executable = env::var_os("JYUTPING_ENGINE_BASELINE").expect("set by Test-WindowsEngineParity.ps1");
        let path = PathBuf::from(env::var_os("JYUTPING_ENGINE_DICTIONARY").expect("set by Test-WindowsEngineParity.ps1"));
        let samples = [
                "",
                "裏",
                "喫煙",
                "開發",
                "反反覆覆",
                "反覆思維同反覆",
                "交通恢復",
                "乾坤",
                "顛倒乾坤",
                "😂🧑🏻‍🦰𠀀",
                "𠁣羣👍",
                "嗰一個",
                "節哀順變",
                "某𬖋字",
                "爲之",
                "計畫",
                "某鑽石項鍊公司",
                "Hello, 香港。",
        ];
        let output = Command::new(executable).arg(&path).arg("--convert").args(samples).output().expect("run C++ conversion");
        assert!(output.status.success(), "{}", String::from_utf8_lossy(&output.stderr));
        let reference = String::from_utf8(output.stdout).expect("UTF-8 engine output");
        let mut expected = reference.lines();
        let dictionary = Dictionary::open(&path).expect("open generated dictionary");
        for sample in samples {
                for standard in [1, 2, 3, 4, 5, 6, 7, 8, 9, 51, -1] {
                        let actual = dictionary.convert_text(sample, standard.into()).expect("convert display text");
                        assert_eq!(expected.next(), Some(actual.as_str()), "{standard} {sample:?}");
                }
        }
        assert!(expected.next().is_none(), "unexpected additional C++ output");
}

#[test]
#[ignore = "run backend/tests/Test-WindowsEngineParity.ps1 with the generated dictionary and ARM64 C++ tools"]
fn reverse_lookup() {
        let executable = env::var_os("JYUTPING_ENGINE_BASELINE").expect("set by Test-WindowsEngineParity.ps1");
        let path = PathBuf::from(env::var_os("JYUTPING_ENGINE_DICTIONARY").expect("set by Test-WindowsEngineParity.ps1"));
        let samples = [
                "",
                "a",
                "ab",
                "aaa",
                "dd",
                "ddd",
                "vnd",
                "mnr",
                "z",
                "12",
                "12345",
                "16",
                "jx",
                "hk",
                "i",
                "o",
                "mukmuk",
                "muk'muk",
                "mukqqmuk",
                "mukqq'muk",
                "muk'mukqq",
                "muk6muk6",
                "muklam",
                "jatjyut",
                "jatjatjat",
                "neihou",
                "nihao",
                "ni'hao",
                "'nihao",
                "ni''hao",
                "ni'hao'",
                "ni3hao3",
                "nh",
                "n'h",
                "n'h'",
                "zhongguo",
                "zhongg",
                "zhgg",
                "xian",
                "xi'an",
                "nv",
                "lve",
                "putonghua",
                "nihaoshijie",
                "nihaokm",
                "xi'an'bei'jing",
                "w'h's'",
        ];
        let output = Command::new(executable).arg(&path).arg("--reverse").args(samples).output().expect("run C++ reverse lookup");
        assert!(output.status.success(), "{}", String::from_utf8_lossy(&output.stderr));
        let reference = String::from_utf8(output.stdout).expect("UTF-8 engine output");
        let mut expected = reference.lines();
        let dictionary = Dictionary::open(&path).expect("open generated dictionary");
        let engine = dictionary.load_engine().expect("load syllables");
        let methods = [
                ReverseLookup::Cangjie(CangjieVariant::Cangjie5),
                ReverseLookup::Cangjie(CangjieVariant::Cangjie3),
                ReverseLookup::Cangjie(CangjieVariant::Quick5),
                ReverseLookup::Cangjie(CangjieVariant::Quick3),
                ReverseLookup::Stroke,
                ReverseLookup::Structure,
                ReverseLookup::Pinyin,
        ];
        for sample in samples {
                assert_eq!(expected.next(), Some(format!("input\t{sample}").as_str()), "{sample:?} heading");
                for (method_index, method) in methods.into_iter().enumerate() {
                        assert_eq!(expected.next(), Some("lookup"), "{sample:?} method {method_index} heading");
                        let actual = dictionary.reverse_lookup(&engine, method, sample).expect("query reverse lookup");
                        for item in &actual {
                                assert_eq!(expected.next(), Some(lexicon_row(item).as_str()), "{sample:?} method {method_index}");
                        }
                }
        }
        assert!(expected.next().is_none(), "unexpected additional C++ output");
}

fn lexicon_row(item: &Lexicon) -> String {
        let kind = match item.kind {
                LexiconKind::Cantonese => 0,
                LexiconKind::Text => 1,
                LexiconKind::Emoji => 2,
                LexiconKind::Symbol => 3,
        };
        format!("{kind}\t{}\t{}\t{}\t{}\t{}\t{}\t{}", item.number, item.input_count(), item.text, item.romanization, item.input, item.mark, item.attached.as_deref().unwrap_or(""))
}

#[test]
#[ignore = "run backend/tests/Test-WindowsEngineParity.ps1 with the generated dictionary and ARM64 C++ tools"]
fn learned_suggestions() {
        let executable = env::var_os("JYUTPING_ENGINE_BASELINE").expect("set by Test-WindowsEngineParity.ps1");
        let path = PathBuf::from(env::var_os("JYUTPING_ENGINE_DICTIONARY").expect("set by Test-WindowsEngineParity.ps1"));
        let directory = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join(format!("../target/engine-parity/memory-{}", std::process::id()));
        fs::create_dir(&directory).expect("create isolated memory directory");
        let memory_path = directory.join("memory.sqlite3");
        let memory = InputMemory::open(&memory_path).expect("initialize isolated memory");
        for (text, romanization, repetitions) in [
                ("香港", "hoeng1 gong2", 6),
                ("鄉港", "hoeng1 gong2", 5),
                ("香港人", "hoeng1 gong2 jan4", 4),
                ("香港島", "hoeng1 gong2 dou2", 3),
                ("香", "hoeng1", 2),
                ("港", "gong2", 1),
                ("𠁣", "ngi1", 1),
                ("你好", "nei5 hou2", 3),
                ("你", "nei5", 2),
                ("我哋", "ngo5 dei6", 1),
        ] {
                let lexicon = Lexicon {
                        kind: LexiconKind::Cantonese,
                        text: text.to_owned(),
                        romanization: romanization.to_owned(),
                        input: String::new(),
                        mark: String::new(),
                        number: 0,
                        attached: None,
                };
                for _ in 0..repetitions {
                        memory.remember(&lexicon).expect("learn original lexicon");
                }
        }
        let samples = [
                "",
                "hg",
                "hgd",
                "hgy",
                "hoeng",
                "hoengg",
                "heunggong",
                "hoenggong",
                "hoenggongjan",
                "hoenggongj",
                "hoeng1gong2",
                "hoengvgongx",
                "hoeng'gong",
                "h'g'",
                "nei5hou2",
                "nei'hou",
                "nei'j",
                "ngoxxdeiqq",
                "ng",
                "ngi",
        ];
        let output = Command::new(executable).arg(&path).arg("--memory").arg(&directory).args(samples).output().expect("run C++ memory lookup");
        assert!(output.status.success(), "{}", String::from_utf8_lossy(&output.stderr));
        let reference = String::from_utf8(output.stdout).expect("UTF-8 engine output");
        let mut expected = reference.lines();
        let dictionary = Dictionary::open(&path).expect("open dictionary");
        let engine = dictionary.load_engine().expect("load syllables");
        for input in samples {
                assert_eq!(expected.next(), Some(format!("input\t{input}").as_str()));
                for item in memory.suggest(&engine, input).expect("query learned candidates") {
                        assert_eq!(expected.next(), Some(lexicon_row(&item).as_str()), "{input:?} learned candidate");
                }
        }
        assert!(expected.next().is_none(), "unexpected additional C++ output");
        drop(memory);
        fs::remove_file(memory_path).expect("remove isolated memory database");
        fs::remove_file(directory.join("memory.sqlite3-journal")).expect("remove isolated journal");
        fs::remove_dir(directory).expect("remove isolated memory directory");
}
