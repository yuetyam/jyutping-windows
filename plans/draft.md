Create a plan/roadmap for implementing candidate paging/navigation and the corresponding key actions.

Key actions (while candidates not empty):
- `tab` : Move to the next candidate.
- `shift` + `tab` : Move to the previous candidate.
- `arrow-up` : Move to the previous candidate.
- `arrow-down` : Move to the next candidate.
- `arrow-left` : Move to the previous page of candidates.
- `arrow-right` : Move to the next page of candidates.
- `-` (minus) : Move to the previous page of candidates.
- `=` (equal) : Move to the next page of candidates.
- `bracket-left` : Move to the previous page of candidates.
- `bracket-right` : Move to the next page of candidates.
- `page-up` : Move to the previous page of candidates.
- `page-down` : Move to the next page of candidates.
- `home` : Move/jump to the first candidate of the first page.
- `end` : Move/jump to the last candidate of the last page.


When the last candidate of the current page is reached/highlighted, the "move to the next candiate" action should move to the next page and highlight the first candidate of that page. Similarly, when the first candidate of the current page is reached/highlighted, the "move to the previous candidate" action should move to the previous page and highlight the last candidate of that page.


The "move to the next page" and "move to the previous page" actions should stay highlighted on the current-position candidate if possible. If the current position is not available on the next page, it should highlight the last candidate of that page (normally it would be the last candidate of all candidates).


We may use the macOS/Swift version of code as a reference.

