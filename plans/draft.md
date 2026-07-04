Create a plan/roadmap for adopting `CharacterStandard` options to the IME like the macOS/Swift version of code.

We already implemented the `CharacterStandard` converters as the plan `plans\3.character-standard-converters.md` in commit `804a582cf9d24cf0f72486aa56ad5c207706aff7`.

What IME behaviors we expect:

1. Shortcuts/hotkeys to select/switch the `CharacterStandard`:
- Control + Shift + 1: Traditional (Preset) (default/fallback)
- Control + Shift + 2: Traditional (HongKong)
- Control + Shift + 3: Traditional (Taiwan)
- Control + Shift + 4: Simplified (Mutilated)

The numbers are the number row keys.

And, in the future, we will add more hotkeys with the rest of the number row 5, 6, 7, 8, 9, and 0 for other (not-CharacterStandard) options.


2. Persistent settings for the selected `CharacterStandard` across sessions.

We may use the key `CharacterVariant` in the registry:
```text
CharacterVariant  REG_DWORD  1 = Traditional, 2 = HongKong, 3 = Taiwan, 4 = Simplified
```


3. The displaying `Candidate.text` should be the converted `Lexicon.text` based on the selected `CharacterStandard`. Just like the macOS/Swift version.


4. We're NOT porting the `TraditionalCharacterStandard` from the macOS/Swift version in this roadmap.


The roadmap should be stored in the `plans` folder.

