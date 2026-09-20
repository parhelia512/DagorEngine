from "%sqstd/srt.nut" import parseSrt
from "%sqstd/underscore.nut" import isEqual

const BOM = "\xEF\xBB\xBF"

let render = @(cues) ", ".join(cues.map(@(c) $"{c.from}-{c.to}:{c.text.replace("\n", "|")}"))

function check(name, text, expected) {
  let cues = parseSrt(text)
  assert(isEqual(cues, expected), $"srt {name}: got [{render(cues)}], expected [{render(expected)}]")
}

check("two cues", "1\n00:00:00,500 --> 00:00:04,000\nFirst\n\n2\n00:00:04,200 --> 00:01:08,000\nSecond\n",
  [{ from = 500, to = 4000, text = "First" }, { from = 4200, to = 68000, text = "Second" }])

check("multiline cue", "1\n00:00:01,000 --> 00:00:02,000\nOne\nTwo\n",
  [{ from = 1000, to = 2000, text = "One\nTwo" }])

check("crlf", "1\r\n00:00:01,000 --> 00:00:02,000\r\nText\r\n",
  [{ from = 1000, to = 2000, text = "Text" }])

check("bom before a cue index line", $"{BOM}1\n00:00:01,000 --> 00:00:02,000\nText\n",
  [{ from = 1000, to = 2000, text = "Text" }])

check("bom before a timing line", $"{BOM}00:00:01,000 --> 00:00:02,000\nText\n",
  [{ from = 1000, to = 2000, text = "Text" }])

check("dot as fraction separator", "00:00:01.250 --> 00:00:02.500\nText\n",
  [{ from = 1250, to = 2500, text = "Text" }])

check("hours", "01:02:03,004 --> 01:02:04,000\nText\n",
  [{ from = 3723004, to = 3724000, text = "Text" }])

check("no index line, no trailing newline", "00:00:01,000 --> 00:00:02,000\nText",
  [{ from = 1000, to = 2000, text = "Text" }])

check("cue text keeps its own spacing", "00:00:01,000 --> 00:00:02,000\n  indented  \n",
  [{ from = 1000, to = 2000, text = "  indented  " }])

check("a line of spaces ends the cue",
  "00:00:01,000 --> 00:00:02,000\nText\n   \n00:00:03,000 --> 00:00:04,000\nMore\n",
  [{ from = 1000, to = 2000, text = "Text" }, { from = 3000, to = 4000, text = "More" }])

check("out of order cues get sorted",
  "00:00:05,000 --> 00:00:06,000\nLate\n\n00:00:01,000 --> 00:00:02,000\nEarly\n",
  [{ from = 1000, to = 2000, text = "Early" }, { from = 5000, to = 6000, text = "Late" }])

check("empty text", "", [])
check("no timings", "just some text\nwithout any cue\n", [])
check("timing without milliseconds", "00:00:01 --> 00:00:02\nText\n",
  [{ from = 1000, to = 2000, text = "Text" }])
check("timing without hours", "00:01,000 --> 00:02,000\nText\n", [])
check("non numeric timing", "00:aa:01,000 --> 00:00:02,000\nText\n", [])
check("a cue with a broken timing does not stop the scan",
  "00:aa:01,000 --> 00:00:02,000\nDropped\n\n00:00:03,000 --> 00:00:04,000\nKept\n",
  [{ from = 3000, to = 4000, text = "Kept" }])
check("end before start", "00:00:02,000 --> 00:00:01,000\nText\n", [])
check("cue without text", "00:00:01,000 --> 00:00:02,000\n\n00:00:03,000 --> 00:00:04,000\nText\n",
  [{ from = 3000, to = 4000, text = "Text" }])

print("srt: all tests passed\n")
